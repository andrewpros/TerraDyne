// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Core/TerraDyneEditController.h"
#include "UI/TerraDyneToolWidget.h"
#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneSubsystem.h"
#include "DrawDebugHelpers.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "World/TerraDyneTileData.h"
#include "World/TerraDyneChunk.h"

ATerraDyneEditController::ATerraDyneEditController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	bIsClicking = false;
	PrimaryActorTick.bCanEverTick = true;
	UIClass = UTerraDyneToolWidget::StaticClass();
	
	BrushRadius = 2000.0f;
	BrushStrength = 1.0f;
	CurrentTool = ETerraDyneToolMode::SculptRaise;
	bShowDebugCursor = true;
	bFlattenHeightLocked = false;
	LockedFlattenHeight = 0.f;
	LastValidHitLocation = FVector::ZeroVector;
}

void ATerraDyneEditController::SetupInputComponent()
{
	Super::SetupInputComponent();
	
	InputComponent->BindAction("TerraDyneClick", IE_Pressed, this, &ATerraDyneEditController::OnLeftClickStart);
	InputComponent->BindAction("TerraDyneClick", IE_Released, this, &ATerraDyneEditController::OnLeftClickStop);
	InputComponent->BindAxis("MouseWheelAxis", this, &ATerraDyneEditController::OnMouseWheel);
	InputComponent->BindAction("TerraDyneUndo", IE_Pressed, this, &ATerraDyneEditController::OnUndoPressed);
	InputComponent->BindAction("TerraDyneRedo", IE_Pressed, this, &ATerraDyneEditController::OnRedoPressed);
}

void ATerraDyneEditController::BeginPlay()
{
	Super::BeginPlay();
	
	UE_LOG(LogTemp, Log, TEXT("EditController: Initializing..."));
	
	// Spawn UI
	if (UIClass)
	{
		ActiveUI = CreateWidget<UTerraDyneToolWidget>(this, UIClass);
		if (ActiveUI)
		{
			ActiveUI->AddToViewport(100);
			ActiveUI->SetVisibility(ESlateVisibility::Visible);
			UE_LOG(LogTemp, Log, TEXT("EditController: UI spawned"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("EditController: No UIClass set"));
	}
	
	// Set input mode - Game AND UI so mouse works
	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);
	
	// Move player up so they don't spawn inside terrain
	APawn* MyPawn = GetPawn();
	if (MyPawn)
	{
		MyPawn->SetActorLocation(FVector(0, 0, 2000));
	}
	
	UE_LOG(LogTemp, Log, TEXT("EditController: Ready - Mouse cursor enabled"));

	// Late-join sync: on the server, push current terrain state to this client
	if (HasAuthority() && !IsLocalController())
	{
		UTerraDyneSubsystem* Sys = GetWorld()->GetSubsystem<UTerraDyneSubsystem>();
		if (ATerraDyneManager* Manager = Sys ? Sys->GetTerrainManager() : nullptr)
		{
			Manager->SendFullSyncToController(this);
		}
	}
}

void ATerraDyneEditController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clean up undo/redo stacks for this player on the server
	if (HasAuthority())
	{
		UTerraDyneSubsystem* Sys = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneSubsystem>() : nullptr;
		if (ATerraDyneManager* Manager = Sys ? Sys->GetTerrainManager() : nullptr)
		{
			Manager->CleanupPlayerStacks(this);
		}
	}
	PendingChunkSyncs.Empty();
	Super::EndPlay(EndPlayReason);
}

void ATerraDyneEditController::OnLeftClickStart()
{
	bIsClicking = true;
	bFlattenHeightLocked = false;  // Reset lock each new press
	bStrokeBegun = false;
}

void ATerraDyneEditController::OnLeftClickStop()
{
	bIsClicking = false;
	bFlattenHeightLocked = false;
	if (bStrokeBegun)
	{
		bStrokeBegun = false;
		if (HasAuthority())
		{
			UTerraDyneSubsystem* Sys = GetWorld()->GetSubsystem<UTerraDyneSubsystem>();
			if (ATerraDyneManager* Manager = Sys ? Sys->GetTerrainManager() : nullptr)
			{
				Manager->CommitStroke(this);
			}
		}
		else
		{
			Server_CommitStroke();
		}
	}
}

void ATerraDyneEditController::OnUndoPressed()
{
	if (HasAuthority())
	{
		UWorld* World = GetWorld();
		if (!World) return;
		UTerraDyneSubsystem* Sys = World->GetSubsystem<UTerraDyneSubsystem>();
		if (ATerraDyneManager* Manager = Sys ? Sys->GetTerrainManager() : nullptr)
		{
			Manager->Undo(this);
		}
	}
	else
	{
		Server_Undo();
	}
}

void ATerraDyneEditController::OnRedoPressed()
{
	if (HasAuthority())
	{
		UWorld* World = GetWorld();
		if (!World) return;
		UTerraDyneSubsystem* Sys = World->GetSubsystem<UTerraDyneSubsystem>();
		if (ATerraDyneManager* Manager = Sys ? Sys->GetTerrainManager() : nullptr)
		{
			Manager->Redo(this);
		}
	}
	else
	{
		Server_Redo();
	}
}

// --- Server RPCs ---

void ATerraDyneEditController::Server_ApplyBrush_Implementation(const FTerraDyneBrushParams& Params)
{
	UTerraDyneSubsystem* Sys = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneSubsystem>() : nullptr;
	ATerraDyneManager* Manager = Sys ? Sys->GetTerrainManager() : nullptr;
	if (!Manager) return;

	if (Params.bIsStrokeStart)
	{
		Manager->BeginStroke(Params.WorldLocation, Params.Radius, this);
	}

	Manager->ApplyGlobalBrush(Params.WorldLocation, Params.Radius, Params.Strength, Params.BrushMode, Params.WeightLayerIndex, Params.FlattenHeight);
	Manager->Multicast_ApplyBrush(Params);
}

void ATerraDyneEditController::Server_CommitStroke_Implementation()
{
	UTerraDyneSubsystem* Sys = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneSubsystem>() : nullptr;
	if (ATerraDyneManager* Manager = Sys ? Sys->GetTerrainManager() : nullptr)
	{
		Manager->CommitStroke(this);
	}
}

void ATerraDyneEditController::Server_Undo_Implementation()
{
	UTerraDyneSubsystem* Sys = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneSubsystem>() : nullptr;
	if (ATerraDyneManager* Manager = Sys ? Sys->GetTerrainManager() : nullptr)
	{
		Manager->Undo(this);
	}
}

void ATerraDyneEditController::Server_Redo_Implementation()
{
	UTerraDyneSubsystem* Sys = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneSubsystem>() : nullptr;
	if (ATerraDyneManager* Manager = Sys ? Sys->GetTerrainManager() : nullptr)
	{
		Manager->Redo(this);
	}
}

void ATerraDyneEditController::Client_ReceiveChunkSync_Implementation(const FTerraDyneChunkData& Data)
{
	UTerraDyneSubsystem* Sys = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneSubsystem>() : nullptr;
	ATerraDyneManager* Manager = Sys ? Sys->GetTerrainManager() : nullptr;
	if (!Manager)
	{
		PendingChunkSyncs.Add(Data.Coordinate, Data);
		return;
	}

	ATerraDyneChunk* Chunk = Manager->GetChunkAtCoord(Data.Coordinate);
	if (Chunk)
	{
		Chunk->LoadFromData(Data);
		PendingChunkSyncs.Remove(Data.Coordinate);
		UE_LOG(LogTemp, Log, TEXT("Client chunk sync: updated chunk [%d,%d]"),
			Data.Coordinate.X, Data.Coordinate.Y);
	}
	else
	{
		PendingChunkSyncs.Add(Data.Coordinate, Data);
		UE_LOG(LogTemp, Warning, TEXT("Client chunk sync: chunk [%d,%d] not ready yet, queued for retry."),
			Data.Coordinate.X, Data.Coordinate.Y);
	}
}

void ATerraDyneEditController::OnMouseWheel(float Val)
{
	if (Val != 0.0f)
	{
		BrushRadius = FMath::Clamp(BrushRadius + Val * 200.0f, 100.0f, 10000.0f);
	}
}

bool ATerraDyneEditController::GetTerrainHit(FHitResult& OutHit)
{
	FVector WorldLoc, WorldDir;
	if (!DeprojectMousePositionToWorld(WorldLoc, WorldDir))
	{
		return false;
	}
	
	FCollisionQueryParams Params;
	Params.bTraceComplex = true;
	Params.bReturnFaceIndex = false;
	
	// Ignore player pawn
	if (APawn* MyPawn = GetPawn())
	{
		Params.AddIgnoredActor(MyPawn);
	}
	
	// Try WorldStatic first (terrain chunks)
	if (GetWorld()->LineTraceSingleByChannel(OutHit, WorldLoc, WorldLoc + WorldDir * 200000.0f, ECC_WorldStatic, Params))
	{
		return true;
	}
	
	// Fallback to Visibility
	if (GetWorld()->LineTraceSingleByChannel(OutHit, WorldLoc, WorldLoc + WorldDir * 200000.0f, ECC_Visibility, Params))
	{
		return true;
	}
	
	// Last resort - try Camera channel
	if (GetWorld()->LineTraceSingleByChannel(OutHit, WorldLoc, WorldLoc + WorldDir * 200000.0f, ECC_Camera, Params))
	{
		return true;
	}
	
	return false;
}

void ATerraDyneEditController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UTerraDyneSubsystem* Sys = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneSubsystem>() : nullptr;
	ATerraDyneManager* Manager = Sys ? Sys->GetTerrainManager() : nullptr;
	if (Manager && PendingChunkSyncs.Num() > 0)
	{
		TArray<FIntPoint> ResolvedCoords;
		for (const auto& Pair : PendingChunkSyncs)
		{
			if (ATerraDyneChunk* Chunk = Manager->GetChunkAtCoord(Pair.Key))
			{
				Chunk->LoadFromData(Pair.Value);
				ResolvedCoords.Add(Pair.Key);
			}
		}

		for (const FIntPoint& Coord : ResolvedCoords)
		{
			PendingChunkSyncs.Remove(Coord);
		}
	}
	
	APawn* MyPawn = GetPawn();
	if (!MyPawn) return;
	
	FHitResult Hit;
	bool bHit = GetTerrainHit(Hit);
	
	// Get player location for reference
	FVector PlayerLoc = MyPawn->GetActorLocation();
	
	// Read current tool settings from UI (sync with UI)
	ETerraDyneToolMode ToolMode = CurrentTool;
	float CurrentRadius = BrushRadius;
	if (ActiveUI)
	{
		ToolMode = ActiveUI->CurrentTool;
		CurrentRadius = ActiveUI->BrushRadius;
	}
	
	if (bHit)
	{
		LastValidHitLocation = Hit.Location;
		
		if (bShowDebugCursor)
		{
			// Determine cursor color based on tool
			FColor CursorColor = FColor::Green;
			switch (ToolMode)
			{
			case ETerraDyneToolMode::SculptLower:
				CursorColor = FColor::Red;
				break;
			case ETerraDyneToolMode::Flatten:
				CursorColor = FColor::Blue;
				break;
			case ETerraDyneToolMode::Smooth:
				CursorColor = FColor::Yellow;
				break;
			case ETerraDyneToolMode::Paint:
				CursorColor = FColor::Purple;
				break;
			default:
				CursorColor = FColor::Green;
			}
			
			// MAIN CURSOR - At hit location on terrain (using UI radius)
			DrawDebugSphere(GetWorld(), Hit.Location, CurrentRadius, 32, CursorColor, false, -1.0f, 0, 3.0f);
			DrawDebugPoint(GetWorld(), Hit.Location, 10.0f, FColor::White, false, -1.0f);
		}
		
		// Apply tool if clicking
		if (bIsClicking)
		{
			PerformToolAction(Hit.Location);
		}
	}
	else if (bShowDebugCursor)
	{
		// NO HIT - Draw cursor in front of player as fallback
		FVector CameraLoc;
		FRotator CameraRot;
		GetPlayerViewPoint(CameraLoc, CameraRot);
		
		FVector Forward = CameraRot.Vector();
		FVector FallbackLoc = CameraLoc + Forward * 5000.0f;
		
		DrawDebugPoint(GetWorld(), FallbackLoc, 5.0f, FColor::Red, false, -1.0f);
	}
	
	// Always draw player position marker if debugging
	if (bShowDebugCursor)
	{
		// DrawDebugBox(GetWorld(), PlayerLoc, FVector(50,50,50), FColor::Blue, false, 0.0f, 0, 2.0f);
	}
}

void ATerraDyneEditController::PerformToolAction(const FVector& Location)
{
	UTerraDyneSubsystem* Sys = GetWorld()->GetSubsystem<UTerraDyneSubsystem>();
	if (!Sys) return;
	ATerraDyneManager* Manager = Sys->GetTerrainManager();
	if (!Manager) return;

	ETerraDyneToolMode ToolMode = CurrentTool;
	float UseRadius = BrushRadius;
	float UseStrength = BrushStrength;
	int32 LayerIndex = 0;

	if (ActiveUI)
	{
		ToolMode = ActiveUI->CurrentTool;
		UseRadius = ActiveUI->BrushRadius;
		UseStrength = ActiveUI->BrushStrength * 2500.0f;
		LayerIndex = ActiveUI->ActiveLayerIndex;
	}

	// Map ETerraDyneToolMode → ETerraDyneBrushMode
	ETerraDyneBrushMode BrushMode = ETerraDyneBrushMode::Raise;
	float FlattenHeight = 0.f;

	switch (ToolMode)
	{
	case ETerraDyneToolMode::SculptRaise:
		BrushMode = ETerraDyneBrushMode::Raise;
		break;
	case ETerraDyneToolMode::SculptLower:
		BrushMode = ETerraDyneBrushMode::Lower;
		break;
	case ETerraDyneToolMode::Smooth:
		BrushMode = ETerraDyneBrushMode::Smooth;
		break;
	case ETerraDyneToolMode::Flatten:
		BrushMode = ETerraDyneBrushMode::Flatten;
		// Lock target height on first click of this stroke
		if (!bFlattenHeightLocked)
		{
			if (ATerraDyneChunk* Chunk = Manager->GetChunkAtLocation(Location))
			{
				FVector RelPos = Location - Chunk->GetActorLocation();
				LockedFlattenHeight = Chunk->GetHeightAtLocation(RelPos);
			}
			bFlattenHeightLocked = true;
		}
		FlattenHeight = LockedFlattenHeight;
		break;
	case ETerraDyneToolMode::Paint:
		BrushMode = ETerraDyneBrushMode::Paint;
		break;
	default:
		BrushMode = ETerraDyneBrushMode::Raise;
		break;
	}

	const bool bFirstClick = !bStrokeBegun;
	if (bFirstClick)
	{
		bStrokeBegun = true;
	}

	FTerraDyneBrushParams Params;
	Params.WorldLocation = Location;
	Params.Radius = UseRadius;
	Params.Strength = UseStrength;
	Params.BrushMode = BrushMode;
	Params.WeightLayerIndex = LayerIndex;
	Params.FlattenHeight = FlattenHeight;
	Params.bIsStrokeStart = bFirstClick;

	if (HasAuthority())
	{
		// Listen-server / standalone: apply directly + multicast to clients
		if (bFirstClick)
		{
			Manager->BeginStroke(Location, UseRadius, this);
		}
		Manager->ApplyGlobalBrush(Location, UseRadius, UseStrength, BrushMode, LayerIndex, FlattenHeight);
		Manager->Multicast_ApplyBrush(Params);
	}
	else
	{
		// Client: send to server via RPC
		Server_ApplyBrush(Params);
	}
}
