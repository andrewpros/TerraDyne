// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "UI/TerraDyneToolWidget.h"
#include "Core/TerraDyneSaveGame.h"
#include "World/TerraDyneTileData.h"
#include "TerraDyneEditController.generated.h"

class UTerraDyneToolWidget;

UCLASS()
class TERRADYNE_API ATerraDyneEditController : public APlayerController
{
	GENERATED_BODY()

public:
	ATerraDyneEditController();

	UPROPERTY(EditDefaultsOnly, Category = "TerraDyne|UI")
	TSubclassOf<UTerraDyneToolWidget> UIClass;

	// Called from STerraDynePanel (Task 6) and keyboard binding
	void OnUndoPressed();
	void OnRedoPressed();

	/** Client → Server: request a brush application. */
	UFUNCTION(Server, Reliable)
	void Server_ApplyBrush(const FTerraDyneBrushParams& Params);

	/** Client → Server: commit the current stroke (mouse released). */
	UFUNCTION(Server, Reliable)
	void Server_CommitStroke();

	/** Client → Server: request undo. */
	UFUNCTION(Server, Reliable)
	void Server_Undo();

	/** Client → Server: request redo. */
	UFUNCTION(Server, Reliable)
	void Server_Redo();

	/** Server → Owning Client: send a single chunk's full data for late-join sync. */
	UFUNCTION(Client, Reliable)
	void Client_ReceiveChunkSync(const FTerraDyneChunkData& Data);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	virtual void Tick(float DeltaTime) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UTerraDyneToolWidget> ActiveUI;

	TMap<FIntPoint, FTerraDyneChunkData> PendingChunkSyncs;

	// Mouse State
	bool bIsClicking;
	bool bHasValidHit;
	FVector LastHitLocation;
	
	// Default values when UI is not available
	ETerraDyneToolMode CurrentTool;
	float BrushRadius;
	float BrushStrength;
	
	// Debug
	bool bShowDebugCursor;
	FVector LastValidHitLocation;

	// Flatten tool state — height locked on press-begin, cleared on release
	bool bFlattenHeightLocked;
	float LockedFlattenHeight;

	// Stroke tracking for undo/redo
	bool bStrokeBegun = false;

	// Input Handlers
	void OnLeftClickStart();
	void OnLeftClickStop();
	void OnMouseWheel(float Val);

	// Helpers
	bool GetTerrainHit(FHitResult& OutHit);
	void PerformToolAction(const FVector& Location);
};
