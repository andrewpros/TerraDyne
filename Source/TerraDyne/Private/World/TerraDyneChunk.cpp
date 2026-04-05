// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "World/TerraDyneChunk.h"
#include "Engine/World.h"
#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneSaveGame.h"
#include "Core/TerraDyneSubsystem.h"
#include "Grass/TerraDyneGrassSystem.h"
#include "Settings/TerraDyneSettings.h"
#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshNormals.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/DynamicMeshOverlay.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/SceneComponent.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Canvas.h"
#include "Engine/StaticMesh.h"
#include "TextureResource.h"
#include "Engine/Texture2D.h"
#include "RenderingThread.h"
#include "DrawDebugHelpers.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"

// Helper for Fractal Brownian Motion
static float FractalNoise(float X, float Y, int32 Octaves, float Persistence = 0.5f, float Lacunarity = 2.0f)
{
	float Total = 0.0f;
	float Frequency = 1.0f;
	float Amplitude = 1.0f;
	float MaxValue = 0.0f;  // Used for normalizing result to 0.0 - 1.0

	for(int32 i = 0; i < Octaves; i++) 
	{
		// Simple pseudo-noise using Sin/Cos to avoid external dependencies
		// Offsetting frequencies slightly to avoid symmetry
		Total += (FMath::Sin(X * Frequency) + FMath::Cos(Y * Frequency * 0.85f)) * Amplitude;
		
		MaxValue += Amplitude * 2.0f; // *2 because Sin+Cos range is approx -2 to 2
		
		Amplitude *= Persistence;
		Frequency *= Lacunarity;
	}
	
	return (Total / MaxValue) + 0.5f; // Normalize to approx 0-1
}

static void ApplyTerrainMaterialParameters(
	UMaterialInstanceDynamic* MID,
	UTexture* HeightTexture,
	UTexture* WeightTexture,
	float WorldSize,
	float ZScale,
	int32 Resolution)
{
	if (!MID)
	{
		return;
	}

	if (HeightTexture)
	{
		MID->SetTextureParameterValue(TEXT("HeightMap"), HeightTexture);
	}

	if (WeightTexture)
	{
		MID->SetTextureParameterValue(TEXT("WeightMap"), WeightTexture);
	}

	MID->SetScalarParameterValue(TEXT("WorldSize"), WorldSize);
	MID->SetScalarParameterValue(TEXT("ZScale"), ZScale);
	MID->SetScalarParameterValue(
		TEXT("TexelStep"),
		(Resolution > 1) ? (1.0f / static_cast<float>(Resolution - 1)) : 0.0f);
}

static int32 ResolveSquareResolutionFromSampleCount(int32 SampleCount)
{
	if (SampleCount <= 0)
	{
		return 0;
	}

	const int32 DerivedResolution = FMath::RoundToInt(FMath::Sqrt(static_cast<double>(SampleCount)));
	return (DerivedResolution > 1 && DerivedResolution * DerivedResolution == SampleCount)
		? DerivedResolution
		: 0;
}

static void ApplyBrushCPUInternal(TArray<float>& HeightBuffer, int32 Resolution, float WorldSize, FVector LocalPos, float Radius, float Strength, float ZScale)
{
	float HalfSize = WorldSize * 0.5f;
	float Step = WorldSize / (Resolution - 1);
	
	int32 CenterX = FMath::RoundToInt((LocalPos.X + HalfSize) / Step);
	int32 CenterY = FMath::RoundToInt((LocalPos.Y + HalfSize) / Step);
	int32 RadiusInPixels = FMath::CeilToInt(Radius / Step);
	
	int32 MinX = FMath::Max(0, CenterX - RadiusInPixels);
	int32 MaxX = FMath::Min(Resolution - 1, CenterX + RadiusInPixels);
	int32 MinY = FMath::Max(0, CenterY - RadiusInPixels);
	int32 MaxY = FMath::Min(Resolution - 1, CenterY + RadiusInPixels);
	
	// Convert world strength to normalized height space (0-1 range)
	// Strength is in world units (e.g., 5000), ZScale is max height (e.g., 3000)
	float StrengthNorm = Strength / ZScale;
	
	int32 ModifiedPixels = 0;
	
	for (int32 Y = MinY; Y <= MaxY; Y++)
	{
		for (int32 X = MinX; X <= MaxX; X++)
		{
			float WorldX = -HalfSize + X * Step;
			float WorldY = -HalfSize + Y * Step;
			float DistSq = FVector2D::DistSquared(FVector2D(WorldX, WorldY), FVector2D(LocalPos.X, LocalPos.Y));
			
			if (DistSq <= Radius * Radius)
			{
				float Dist = FMath::Sqrt(DistSq);
				float Falloff = 1.0f - (Dist / Radius);
				Falloff = Falloff * Falloff * (3.0f - 2.0f * Falloff);
				
				int32 Index = Y * Resolution + X;
				float Delta = StrengthNorm * Falloff;
				HeightBuffer[Index] = FMath::Clamp(HeightBuffer[Index] + Delta, -1.f, 2.f);
				ModifiedPixels++;
			}
		}
	}
	
	UE_LOG(LogTemp, Verbose, TEXT("CPU Brush: Modified %d pixels"), ModifiedPixels);
}

static void ApplySmoothCPUInternal(TArray<float>& Buffer, int32 Resolution, float WorldSize, FVector LocalPos, float Radius, float Strength)
{
	float HalfSize = WorldSize * 0.5f;
	float Step = WorldSize / (Resolution - 1);

	int32 CenterX = FMath::RoundToInt((LocalPos.X + HalfSize) / Step);
	int32 CenterY = FMath::RoundToInt((LocalPos.Y + HalfSize) / Step);
	int32 RadiusInPixels = FMath::CeilToInt(Radius / Step);

	int32 MinX = FMath::Max(0, CenterX - RadiusInPixels);
	int32 MaxX = FMath::Min(Resolution - 1, CenterX + RadiusInPixels);
	int32 MinY = FMath::Max(0, CenterY - RadiusInPixels);
	int32 MaxY = FMath::Min(Resolution - 1, CenterY + RadiusInPixels);

	// Snapshot prevents pixels from influencing each other mid-pass
	TArray<float> Snapshot = Buffer;

	for (int32 Y = MinY; Y <= MaxY; Y++)
	{
		for (int32 X = MinX; X <= MaxX; X++)
		{
			float WorldX = -HalfSize + X * Step;
			float WorldY = -HalfSize + Y * Step;
			float DistSq = FVector2D::DistSquared(FVector2D(WorldX, WorldY), FVector2D(LocalPos.X, LocalPos.Y));
			if (DistSq > Radius * Radius) continue;

			float Dist = FMath::Sqrt(DistSq);
			float Falloff = 1.0f - (Dist / Radius);
			Falloff = Falloff * Falloff * (3.0f - 2.0f * Falloff);

			// 8-neighbor average from snapshot
			float Sum = 0.f;
			int32 Count = 0;
			for (int32 DY = -1; DY <= 1; DY++)
			{
				for (int32 DX = -1; DX <= 1; DX++)
				{
					if (DX == 0 && DY == 0) continue;
					int32 NX = X + DX;
					int32 NY = Y + DY;
					if (NX < 0 || NX >= Resolution || NY < 0 || NY >= Resolution) continue;
					Sum += Snapshot[NY * Resolution + NX];
					Count++;
				}
			}
			float NeighborAvg = (Count > 0) ? (Sum / Count) : Snapshot[Y * Resolution + X];
			Buffer[Y * Resolution + X] = FMath::Lerp(Buffer[Y * Resolution + X], NeighborAvg, Strength * Falloff);
		}
	}
}

static void ApplyFlattenCPUInternal(TArray<float>& SculptBuffer, const TArray<float>& BaseBuffer, const TArray<float>& DetailBuffer,
	int32 Resolution, float WorldSize, FVector LocalPos, float Radius, float Strength, float FlattenHeightNorm)
{
	float HalfSize = WorldSize * 0.5f;
	float Step = WorldSize / (Resolution - 1);

	int32 CenterX = FMath::RoundToInt((LocalPos.X + HalfSize) / Step);
	int32 CenterY = FMath::RoundToInt((LocalPos.Y + HalfSize) / Step);
	int32 RadiusInPixels = FMath::CeilToInt(Radius / Step);

	int32 MinX = FMath::Max(0, CenterX - RadiusInPixels);
	int32 MaxX = FMath::Min(Resolution - 1, CenterX + RadiusInPixels);
	int32 MinY = FMath::Max(0, CenterY - RadiusInPixels);
	int32 MaxY = FMath::Min(Resolution - 1, CenterY + RadiusInPixels);

	for (int32 Y = MinY; Y <= MaxY; Y++)
	{
		for (int32 X = MinX; X <= MaxX; X++)
		{
			float WorldX = -HalfSize + X * Step;
			float WorldY = -HalfSize + Y * Step;
			float DistSq = FVector2D::DistSquared(FVector2D(WorldX, WorldY), FVector2D(LocalPos.X, LocalPos.Y));
			if (DistSq > Radius * Radius) continue;

			float Dist = FMath::Sqrt(DistSq);
			float Falloff = 1.0f - (Dist / Radius);
			Falloff = Falloff * Falloff * (3.0f - 2.0f * Falloff);

			int32 Idx = Y * Resolution + X;
			// Target sculpt value: total height = FlattenHeightNorm => Sculpt = target - Base - Detail
			float TargetSculpt = FlattenHeightNorm - BaseBuffer[Idx] - DetailBuffer[Idx];
			SculptBuffer[Idx] = FMath::Lerp(SculptBuffer[Idx], TargetSculpt, Strength * Falloff);
		}
	}
}

static void ApplyPaintCPUInternal(TArray<float>& WeightBuffer, int32 Resolution, float WorldSize, FVector LocalPos, float Radius, float Strength)
{
	float HalfSize = WorldSize * 0.5f;
	float Step = WorldSize / (Resolution - 1);

	int32 CenterX = FMath::RoundToInt((LocalPos.X + HalfSize) / Step);
	int32 CenterY = FMath::RoundToInt((LocalPos.Y + HalfSize) / Step);
	int32 RadiusInPixels = FMath::CeilToInt(Radius / Step);

	int32 MinX = FMath::Max(0, CenterX - RadiusInPixels);
	int32 MaxX = FMath::Min(Resolution - 1, CenterX + RadiusInPixels);
	int32 MinY = FMath::Max(0, CenterY - RadiusInPixels);
	int32 MaxY = FMath::Min(Resolution - 1, CenterY + RadiusInPixels);

	for (int32 Y = MinY; Y <= MaxY; Y++)
	{
		for (int32 X = MinX; X <= MaxX; X++)
		{
			float WorldX = -HalfSize + X * Step;
			float WorldY = -HalfSize + Y * Step;
			float DistSq = FVector2D::DistSquared(FVector2D(WorldX, WorldY), FVector2D(LocalPos.X, LocalPos.Y));
			if (DistSq > Radius * Radius) continue;

			float Dist = FMath::Sqrt(DistSq);
			float Falloff = 1.0f - (Dist / Radius);
			Falloff = Falloff * Falloff * (3.0f - 2.0f * Falloff);

			int32 Idx = Y * Resolution + X;
			WeightBuffer[Idx] = FMath::Clamp(WeightBuffer[Idx] + Strength * Falloff, 0.f, 1.f);
		}
	}
}

ATerraDyneChunk::ATerraDyneChunk()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	DynamicMeshComp = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("DynamicMeshComp"));
	SetRootComponent(DynamicMeshComp);

	DynamicMeshComp->SetCollisionProfileName(TEXT("BlockAll"));
	DynamicMeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	DynamicMeshComp->SetCollisionObjectType(ECC_WorldStatic);
	DynamicMeshComp->CollisionType = ECollisionTraceFlag::CTF_UseComplexAsSimple;
	DynamicMeshComp->bEnableComplexCollision = true;
	DynamicMeshComp->bDeferCollisionUpdates = false;
	
	DynamicMeshComp->SetVisibility(true);
	DynamicMeshComp->SetHiddenInGame(false);
	DynamicMeshComp->SetCastShadow(true);
	
	ZScale = 3000.0f;
	WorldSize = 10000.0f;
	Resolution = 128; // Increased from 64 for better visual quality
	bInitialized = false;
	bUseGPU = false;
	HeightRT = nullptr;
	HeightRT_Swap = nullptr;
	CollisionDebounceTimer = 0.0f;
	bCollisionDirty = false;
	GrassDebounceTimer = 0.0f;
	bGrassDirty = false;
	TransferredFoliageDebounceTimer = 0.0f;
	bTransferredFoliageDirty = false;
	bTransferredFoliageFollowsTerrain = false;
	WeightTexture = nullptr;
}

void ATerraDyneChunk::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATerraDyneChunk, ZScale);
	DOREPLIFETIME(ATerraDyneChunk, WorldSize);
	DOREPLIFETIME(ATerraDyneChunk, Resolution);
	DOREPLIFETIME(ATerraDyneChunk, GridCoordinate);
	DOREPLIFETIME(ATerraDyneChunk, ChunkSizeWorldUnits);
	DOREPLIFETIME(ATerraDyneChunk, ProceduralSeed);
	DOREPLIFETIME(ATerraDyneChunk, bIsAuthoredChunk);
	DOREPLIFETIME(ATerraDyneChunk, PrimaryBiomeTag);
}

void ATerraDyneChunk::BeginPlay()
{
	Super::BeginPlay();
	
	if (!bInitialized)
	{
		Initialize(Resolution, WorldSize);
	}

	if (UTerraDyneSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneSubsystem>() : nullptr)
	{
		if (ATerraDyneManager* Manager = Subsystem->GetTerrainManager())
		{
			Manager->RegisterChunk(this);
		}
	}
}

void ATerraDyneChunk::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearTransferredActorFoliageActors();
	ClearTransferredFoliageComponents();

	if (UTerraDyneSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneSubsystem>() : nullptr)
	{
		if (ATerraDyneManager* Manager = Subsystem->GetTerrainManager())
		{
			Manager->UnregisterChunk(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void ATerraDyneChunk::InitializeChunk(FIntPoint Coord, float Size, int32 InRes, UTexture2D* SourceHeight, UTexture2D* SourceWeight)
{
	GridCoordinate = Coord;
	ChunkSizeWorldUnits = Size;
	WorldSize = Size;

	Initialize(FMath::Clamp(InRes, 32, 256), Size);

#if WITH_EDITORONLY_DATA
	// Seed BaseBuffer from a baked height texture if provided
	if (SourceHeight && SourceHeight->GetPlatformData() && SourceHeight->GetPlatformData()->Mips.Num() > 0)
	{
		FTexture2DMipMap& Mip = SourceHeight->GetPlatformData()->Mips[0];
		const void* RawData = Mip.BulkData.LockReadOnly();
		if (RawData)
		{
			const uint8* Pixels = static_cast<const uint8*>(RawData);
			int32 NumPx = Resolution * Resolution;
			for (int32 i = 0; i < NumPx && i < BaseBuffer.Num(); i++)
			{
				// Assumes PF_G8 (single greyscale byte per pixel)
				BaseBuffer[i] = Pixels[i] / 255.f;
				HeightBuffer[i] = FMath::Clamp(BaseBuffer[i] + SculptBuffer[i] + DetailBuffer[i], 0.f, 1.f);
			}
			Mip.BulkData.Unlock();
			RebuildMesh();
			UpdateCollision();
		}
	}

	// Seed WeightBuffers from a baked RGBA8 weight texture if provided
	if (SourceWeight && SourceWeight->GetPlatformData() && SourceWeight->GetPlatformData()->Mips.Num() > 0)
	{
		FTexture2DMipMap& Mip = SourceWeight->GetPlatformData()->Mips[0];
		const void* RawData = Mip.BulkData.LockReadOnly();
		if (RawData)
		{
			const uint8* Pixels = static_cast<const uint8*>(RawData);
			int32 NumPx = Resolution * Resolution;
			for (int32 i = 0; i < NumPx && i < WeightBuffers[0].Num(); i++)
			{
				WeightBuffers[0][i] = Pixels[i * 4 + 0] / 255.f;
				WeightBuffers[1][i] = Pixels[i * 4 + 1] / 255.f;
				WeightBuffers[2][i] = Pixels[i * 4 + 2] / 255.f;
				WeightBuffers[3][i] = Pixels[i * 4 + 3] / 255.f;
			}
			Mip.BulkData.Unlock();
			UploadWeightTexture();
		}
	}
#endif
}

void ATerraDyneChunk::Initialize(int32 InResolution, float InSize)
{
	if (bInitialized) return;
	
	Resolution = InResolution;
	WorldSize = InSize;
	
	UE_LOG(LogTemp, Log, TEXT("Chunk [%d,%d]: Initializing Res=%d Size=%.0f"), 
		GridCoordinate.X, GridCoordinate.Y, Resolution, WorldSize);
	
	int32 NumVerts = Resolution * Resolution;
	BaseBuffer.SetNumZeroed(NumVerts);
	SculptBuffer.SetNumZeroed(NumVerts);
	DetailBuffer.SetNumZeroed(NumVerts);
	HeightBuffer.SetNumZeroed(NumVerts);

	for (int32 L = 0; L < NumWeightLayers; L++)
	{
		WeightBuffers[L].SetNumZeroed(NumVerts);
	}

	// Create weight texture (RGBA8, one pixel per heightmap vertex)
	WeightTexture = UTexture2D::CreateTransient(Resolution, Resolution, PF_B8G8R8A8, TEXT("WeightTexture"));
	if (WeightTexture)
	{
		WeightTexture->NeverStream = true;
		WeightTexture->Filter = TF_Bilinear;
		WeightTexture->UpdateResource();
	}
	UploadWeightTexture();

	// Procedural terrain (goes to Base layer)
	// Use GridCoordinate * WorldSize for noise origin — GetActorLocation() may not be
	// finalized during deferred BeginPlay, causing all chunks to sample noise from (0,0,0)
	FRandomStream SeedStream(ProceduralSeed);
	const float SeedOffsetX = SeedStream.FRandRange(-250000.0f, 250000.0f);
	const float SeedOffsetY = SeedStream.FRandRange(-250000.0f, 250000.0f);
	FVector ChunkOrigin(GridCoordinate.X * WorldSize, GridCoordinate.Y * WorldSize, 0.0);
	for (int32 Y = 0; Y < Resolution; Y++)
	{
		for (int32 X = 0; X < Resolution; X++)
		{
			float WorldX = ChunkOrigin.X + SeedOffsetX + (((float)X / (Resolution - 1)) - 0.5f) * WorldSize;
			float WorldY = ChunkOrigin.Y + SeedOffsetY + (((float)Y / (Resolution - 1)) - 0.5f) * WorldSize;
			
			// FBM Noise for Base Layer (Mountains/Hills)
			// Scale inputs down for macro features
			float Height = FractalNoise(WorldX * 0.0005f, WorldY * 0.0005f, 4);
			
			BaseBuffer[Y * Resolution + X] = FMath::Clamp(Height, 0.0f, 1.0f);

			// Add detail layer noise (high frequency "erosion" simulation)
			// Using FBM again for better detail
			float DetailHeight = FractalNoise(WorldX * 0.01f, WorldY * 0.01f, 2) * 0.02f; // Low amplitude
			DetailBuffer[Y * Resolution + X] = DetailHeight;
		}
	}
	
	// Initial sum (Base + Detail)
	for (int32 i = 0; i < NumVerts; i++)
	{
		HeightBuffer[i] = FMath::Clamp(BaseBuffer[i] + DetailBuffer[i], 0.0f, 1.0f);
	}
	
	SetupGPU();
	RebuildMesh();
	UpdateCollision();
	
	bInitialized = true;
	
	UE_LOG(LogTemp, Log, TEXT("Chunk [%d,%d]: Ready (GPU: %s)"), 
		GridCoordinate.X, GridCoordinate.Y, bUseGPU ? TEXT("YES") : TEXT("NO"));
}

bool ATerraDyneChunk::SetupGPU()
{
	if (!BrushMaterialBase)
	{
		UE_LOG(LogTemp, Verbose, TEXT("Chunk [%d,%d]: No brush material — GPU disabled"), GridCoordinate.X, GridCoordinate.Y);
		bUseGPU = false;
		return false;
	}

	// Create ping-pong render targets
	HeightRT = NewObject<UTextureRenderTarget2D>(this);
	HeightRT->InitCustomFormat(Resolution, Resolution, PF_FloatRGBA, false);
	HeightRT->UpdateResource();

	HeightRT_Swap = NewObject<UTextureRenderTarget2D>(this);
	HeightRT_Swap->InitCustomFormat(Resolution, Resolution, PF_FloatRGBA, false);
	HeightRT_Swap->UpdateResource();

	// Upload CPU height data to HeightRT and wait for completion
	UpdateRenderTargetFromHeightmap();
	FlushRenderingCommands();

	// Validate: readback should match the uploaded data within tolerance
	FTextureRenderTargetResource* RTResource = HeightRT->GameThread_GetRenderTargetResource();
	if (!RTResource)
	{
		UE_LOG(LogTemp, Warning, TEXT("Chunk [%d,%d]: RT resource unavailable — GPU disabled"), GridCoordinate.X, GridCoordinate.Y);
		bUseGPU = false;
		return false;
	}

	TArray<FLinearColor> Readback;
	if (!RTResource->ReadLinearColorPixels(Readback) || Readback.Num() != HeightBuffer.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("Chunk [%d,%d]: RT readback size mismatch — GPU disabled"), GridCoordinate.X, GridCoordinate.Y);
		bUseGPU = false;
		return false;
	}

	// Spot-check a few samples to verify data integrity
	float MaxError = 0.f;
	int32 SampleStride = FMath::Max(1, Readback.Num() / 16);
	for (int32 i = 0; i < Readback.Num(); i += SampleStride)
	{
		MaxError = FMath::Max(MaxError, FMath::Abs(Readback[i].R - HeightBuffer[i]));
	}

	if (MaxError > 0.01f)
	{
		UE_LOG(LogTemp, Warning, TEXT("Chunk [%d,%d]: RT validation failed (max error %.4f) — GPU disabled"),
			GridCoordinate.X, GridCoordinate.Y, MaxError);
		bUseGPU = false;
		return false;
	}

	bUseGPU = true;
	return true;
}

void ATerraDyneChunk::UpdateRenderTargetFromHeightmap()
{
	if (!HeightRT || HeightBuffer.Num() != Resolution * Resolution) return;

	FTextureRenderTargetResource* RenderTargetResource = HeightRT->GameThread_GetRenderTargetResource();
	if (!RenderTargetResource)
	{
		HeightRT->UpdateResourceImmediate(false);
		FlushRenderingCommands();
		RenderTargetResource = HeightRT->GameThread_GetRenderTargetResource();
		if (!RenderTargetResource)
		{
			return;
		}
	}

	FTextureRHIRef RenderTargetTexture = RenderTargetResource->GetRenderTargetTexture();
	if (!RenderTargetTexture.IsValid())
	{
		return;
	}

	TArray<FFloat16Color> PixelData;
	PixelData.SetNumUninitialized(Resolution * Resolution);
	for (int32 i = 0; i < HeightBuffer.Num(); i++)
	{
		PixelData[i] = FFloat16Color(FLinearColor(HeightBuffer[i], HeightBuffer[i], HeightBuffer[i], 1.f));
	}

	ENQUEUE_RENDER_COMMAND(TerraDyneUploadHeightRT)(
		[Texture = MoveTemp(RenderTargetTexture), Data = MoveTemp(PixelData), Res = Resolution](FRHICommandListImmediate& RHICmdList)
		{
			FRHITexture* TextureResource = Texture.GetReference();
			if (!TextureResource) return;
			uint32 Stride = 0;
			void* MipData = RHICmdList.LockTexture2D(TextureResource, 0, RLM_WriteOnly, Stride, false);
			if (MipData)
			{
				// Copy row-by-row respecting GPU row pitch (Stride may include padding)
				const int32 BytesPerRow = Res * sizeof(FFloat16Color);
				for (int32 Row = 0; Row < Res; Row++)
				{
					FMemory::Memcpy(
						static_cast<uint8*>(MipData) + Row * Stride,
						reinterpret_cast<const uint8*>(Data.GetData()) + Row * BytesPerRow,
						BytesPerRow);
				}
				RHICmdList.UnlockTexture2D(TextureResource, 0, false);
			}
		});
}

void ATerraDyneChunk::SetMaterial(UMaterialInterface* InMaterial)
{
	if (!DynamicMeshComp) return;
	
	if (InMaterial)
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(InMaterial, this);
		if (MID)
		{
			ApplyTerrainMaterialParameters(MID, HeightRT, WeightTexture, WorldSize, ZScale, Resolution);
			DynamicMeshComp->SetMaterial(0, MID);
		}
	}
	else
	{
		UMaterialInterface* DefaultMat = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (DefaultMat)
		{
			DynamicMeshComp->SetMaterial(0, DefaultMat);
		}
	}
}

void ATerraDyneChunk::UpdateDisplayMaterialParameters()
{
	if (!DynamicMeshComp)
	{
		return;
	}

	if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(DynamicMeshComp->GetMaterial(0)))
	{
		ApplyTerrainMaterialParameters(MID, HeightRT, WeightTexture, WorldSize, ZScale, Resolution);
	}
}

void ATerraDyneChunk::UploadWeightTexture()
{
	if (!WeightTexture) return;

	int32 NumPixels = Resolution * Resolution;
	if (NumPixels <= 0)
	{
		return;
	}

	for (int32 Layer = 0; Layer < NumWeightLayers; Layer++)
	{
		if (WeightBuffers[Layer].Num() != NumPixels)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Chunk [%d,%d]: Resizing invalid weight buffer %d (%d -> %d) before upload."),
				GridCoordinate.X, GridCoordinate.Y, Layer, WeightBuffers[Layer].Num(), NumPixels);
			WeightBuffers[Layer].SetNumZeroed(NumPixels);
		}
	}

	TArray<uint8> Pixels;
	Pixels.SetNumUninitialized(NumPixels * 4);

	for (int32 i = 0; i < NumPixels; i++)
	{
		Pixels[i * 4 + 0] = (uint8)FMath::Clamp(FMath::RoundToInt(WeightBuffers[0][i] * 255.f), 0, 255); // R
		Pixels[i * 4 + 1] = (uint8)FMath::Clamp(FMath::RoundToInt(WeightBuffers[1][i] * 255.f), 0, 255); // G
		Pixels[i * 4 + 2] = (uint8)FMath::Clamp(FMath::RoundToInt(WeightBuffers[2][i] * 255.f), 0, 255); // B
		Pixels[i * 4 + 3] = (uint8)FMath::Clamp(FMath::RoundToInt(WeightBuffers[3][i] * 255.f), 0, 255); // A
	}

	// Allocate data for render thread lifetime (freed in cleanup lambda)
	uint8* PixelsCopy = (uint8*)FMemory::Malloc(Pixels.Num());
	FMemory::Memcpy(PixelsCopy, Pixels.GetData(), Pixels.Num());

	FUpdateTextureRegion2D* Region = (FUpdateTextureRegion2D*)FMemory::Malloc(sizeof(FUpdateTextureRegion2D));
	*Region = FUpdateTextureRegion2D(0, 0, 0, 0, Resolution, Resolution);

	WeightTexture->UpdateTextureRegions(
		0,
		1,
		Region,
		Resolution * 4,  // SrcPitch: bytes per row
		4,               // SrcBpp: bytes per pixel
		PixelsCopy,
		[](uint8* DataPtr, const FUpdateTextureRegion2D* RegionPtr)
		{
			FMemory::Free(DataPtr);
			FMemory::Free((void*)RegionPtr);
		}
	);

	UpdateDisplayMaterialParameters();
}

void ATerraDyneChunk::ApplyLocalIdempotentEdit(
	FVector RelativePos, float Radius, float Strength,
	ETerraDyneBrushMode BrushMode,
	int32 WeightLayerIndex, float FlattenHeight)
{
	float HalfSize = WorldSize * 0.5f;
	bool bInBoundsX = RelativePos.X >= -HalfSize - Radius && RelativePos.X <= HalfSize + Radius;
	bool bInBoundsY = RelativePos.Y >= -HalfSize - Radius && RelativePos.Y <= HalfSize + Radius;
	if (!bInBoundsX || !bInBoundsY) return;

	UE_LOG(LogTemp, Verbose, TEXT("Chunk [%d,%d]: Brush Mode=%d at %s R=%.0f S=%.0f"),
		GridCoordinate.X, GridCoordinate.Y, (int32)BrushMode, *RelativePos.ToString(), Radius, Strength);

	bool bModifiedHeight = false;

	switch (BrushMode)
	{
	case ETerraDyneBrushMode::Raise:
		ApplyBrushCPUInternal(SculptBuffer, Resolution, WorldSize, RelativePos, Radius, FMath::Abs(Strength), ZScale);
		bModifiedHeight = true;
		break;

	case ETerraDyneBrushMode::Lower:
		ApplyBrushCPUInternal(SculptBuffer, Resolution, WorldSize, RelativePos, Radius, -FMath::Abs(Strength), ZScale);
		bModifiedHeight = true;
		break;

	case ETerraDyneBrushMode::Smooth:
		ApplySmoothCPUInternal(SculptBuffer, Resolution, WorldSize, RelativePos, Radius, FMath::Clamp(Strength / ZScale, 0.f, 1.f));
		bModifiedHeight = true;
		break;

	case ETerraDyneBrushMode::Flatten:
		ApplyFlattenCPUInternal(SculptBuffer, BaseBuffer, DetailBuffer, Resolution, WorldSize,
			RelativePos, Radius, FMath::Clamp(Strength / ZScale, 0.f, 1.f),
			FlattenHeight / ZScale);
		bModifiedHeight = true;
		break;

	case ETerraDyneBrushMode::Paint:
	{
		int32 LayerIdx = FMath::Clamp(WeightLayerIndex, 0, NumWeightLayers - 1);
		// Paint strength: UI slider maps [0,5] * 2500 = [0, 12500]. Use 10000 as "full opacity" divisor
		// so mid-slider (~0.5 * 2500 = 1250) gives ~0.125 opacity per stroke — suitable for layered painting.
		ApplyPaintCPUInternal(WeightBuffers[LayerIdx], Resolution, WorldSize, RelativePos, Radius,
			FMath::Clamp(Strength / 10000.f, 0.f, 1.f));
		UploadWeightTexture();
		bGrassDirty = true;
		float GrassDelay = 0.5f;
		if (const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>())
		{
			GrassDelay = Settings->GrassDebounceTime;
		}
		GrassDebounceTimer = GrassDelay;
		break;
	}
	default:
		UE_LOG(LogTemp, Warning, TEXT("Chunk [%d,%d]: Unknown BrushMode %d — no-op"),
			GridCoordinate.X, GridCoordinate.Y, (int32)BrushMode);
		break;
	}

	if (bModifiedHeight)
	{
		for (int32 i = 0; i < HeightBuffer.Num(); i++)
		{
			SculptBuffer[i] = FMath::Clamp(SculptBuffer[i], -1.f, 1.f);
			HeightBuffer[i] = FMath::Clamp(BaseBuffer[i] + SculptBuffer[i] + DetailBuffer[i], 0.f, 1.f);
		}
		RebuildMesh();
		bCollisionDirty = true;
		// Sculpt changes invalidate grass placement
		bGrassDirty = true;
		float DebounceTime = 0.2f;
		float GrassDelay = 0.5f;
		if (const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>())
		{
			DebounceTime = Settings->CollisionDebounceTime;
			GrassDelay = Settings->GrassDebounceTime;
		}
		CollisionDebounceTimer = DebounceTime;
		GrassDebounceTimer = GrassDelay;
		if (bTransferredFoliageFollowsTerrain &&
			(TransferredFoliageInstanceLocalTransforms.Num() > 0 ||
			 TransferredActorFoliageInstanceLocalTransforms.Num() > 0))
		{
			bTransferredFoliageDirty = true;
			TransferredFoliageDebounceTimer = GrassDelay;
		}
	}
}

void ATerraDyneChunk::ApplyBrushGPU(FVector LocalPos, float Radius, float Strength)
{
	if (!HeightRT || !HeightRT_Swap || !BrushMaterialBase) return;

	float HalfSize = WorldSize * 0.5f;
	float U = (LocalPos.X + HalfSize) / WorldSize;
	float V = (LocalPos.Y + HalfSize) / WorldSize;
	float UVRadius = Radius / WorldSize;

	UMaterialInstanceDynamic* DynMat = UMaterialInstanceDynamic::Create(BrushMaterialBase, this);
	if (!DynMat) return;

	DynMat->SetScalarParameterValue(TEXT("Radius"), UVRadius);
	DynMat->SetScalarParameterValue(TEXT("Strength"), Strength / ZScale);
	DynMat->SetVectorParameterValue(TEXT("BrushPos"), FLinearColor(U, V, 0, 0));
	// Ping-pong: material reads current height from HeightRT, writes result to HeightRT_Swap
	DynMat->SetTextureParameterValue(TEXT("PrevHeight"), HeightRT);

	UCanvas* Canvas = nullptr;
	FVector2D RTSize;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, HeightRT_Swap, Canvas, RTSize, Context);
	if (Canvas)
	{
		Canvas->K2_DrawMaterial(DynMat, FVector2D::ZeroVector, RTSize, FVector2D::ZeroVector, FVector2D::UnitVector);
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);

	// Swap: HeightRT_Swap (just written) becomes the new current HeightRT
	Swap(HeightRT, HeightRT_Swap);

	UpdateDisplayMaterialParameters();

	ReadbackRenderTarget();
}

void ATerraDyneChunk::ReadbackRenderTarget()
{
	if (!HeightRT) return;
	
	FlushRenderingCommands();
	
	FTextureRenderTargetResource* RTResource = HeightRT->GameThread_GetRenderTargetResource();
	if (!RTResource) return;
	
	TArray<FLinearColor> Output;
	if (RTResource->ReadLinearColorPixels(Output) && Output.Num() == HeightBuffer.Num())
	{
		for (int32 i = 0; i < Output.Num(); i++)
		{
			HeightBuffer[i] = Output[i].R;
		}
	}
}

void ATerraDyneChunk::RebuildMesh()
{
	if (!DynamicMeshComp) return;
	if (Resolution < 2)
	{
		return;
	}

	const int32 ExpectedSamples = Resolution * Resolution;
	if (HeightBuffer.Num() != ExpectedSamples)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Chunk [%d,%d]: RebuildMesh skipped due to invalid sample count (%d vs %d)."),
			GridCoordinate.X, GridCoordinate.Y, HeightBuffer.Num(), ExpectedSamples);
		return;
	}

	DynamicMeshComp->GetDynamicMesh()->EditMesh([&](UE::Geometry::FDynamicMesh3& Mesh)
	{
		Mesh.Clear();
		Mesh.EnableAttributes();

		UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = Mesh.Attributes()->PrimaryUV();
		UE::Geometry::FDynamicMeshNormalOverlay* NormalOverlay = Mesh.Attributes()->PrimaryNormals();

		const double Step = (double)WorldSize / (double)(Resolution - 1);
		const double Offset = (double)WorldSize * -0.5;
		const float UVTileScale = WorldSize / 1000.0f;

		// --- Vertices + UV elements (single top surface — no bottom slab) ---
		for (int32 Y = 0; Y < Resolution; Y++) {
			for (int32 X = 0; X < Resolution; X++) {
				double PX = Offset + (X * Step);
				double PY = Offset + (Y * Step);
				float Height = HeightBuffer[Y * Resolution + X];
				Mesh.AppendVertex(FVector3d(PX, PY, Height * ZScale));

				float U = ((float)X / (Resolution - 1)) * UVTileScale;
				float V = ((float)Y / (Resolution - 1)) * UVTileScale;
				UVOverlay->AppendElement(FVector2f(U, V));
			}
		}

		// --- Triangles (wire UV overlay to each triangle) ---
		for (int32 Y = 0; Y < Resolution - 1; Y++) {
			for (int32 X = 0; X < Resolution - 1; X++) {
				int32 BL = Y * Resolution + X;
				int32 BR = BL + 1;
				int32 TL = (Y + 1) * Resolution + X;
				int32 TR = TL + 1;

				int32 T0 = Mesh.AppendTriangle(BL, BR, TR);
				int32 T1 = Mesh.AppendTriangle(BL, TR, TL);
				if (T0 >= 0) UVOverlay->SetTriangle(T0, UE::Geometry::FIndex3i(BL, BR, TR));
				if (T1 >= 0) UVOverlay->SetTriangle(T1, UE::Geometry::FIndex3i(BL, TR, TL));
			}
		}

		// Populate the overlay normals explicitly so DynamicMesh builds stable lighting and tangents.
		UE::Geometry::FMeshNormals::QuickComputeVertexNormals(Mesh);
		if (NormalOverlay)
		{
			UE::Geometry::FMeshNormals::InitializeOverlayToPerVertexNormals(NormalOverlay, true);
		}
	});

	if (HeightRT)
	{
		UpdateRenderTargetFromHeightmap();
	}

	UpdateDisplayMaterialParameters();
}

void ATerraDyneChunk::UpdateCollision()
{
	if (!DynamicMeshComp) return;
	DynamicMeshComp->UpdateCollision(true);
}

void ATerraDyneChunk::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bCollisionDirty)
	{
		CollisionDebounceTimer -= DeltaTime;
		if (CollisionDebounceTimer <= 0.0f)
		{
			UpdateCollision();
			bCollisionDirty = false;
		}
	}

	if (bGrassDirty)
	{
		GrassDebounceTimer -= DeltaTime;
		if (GrassDebounceTimer <= 0.0f)
		{
			RequestGrassRegen();
			bGrassDirty = false;
		}
	}

	if (bTransferredFoliageDirty)
	{
		TransferredFoliageDebounceTimer -= DeltaTime;
		if (TransferredFoliageDebounceTimer <= 0.0f)
		{
			RefreshTransferredFoliagePlacement(true);
			RefreshTransferredActorFoliagePlacement(true);
		}
	}
}

void ATerraDyneChunk::RebuildPhysicsMesh()
{
	RebuildMesh();
	UpdateCollision();
}

FBox ATerraDyneChunk::GetWorldBounds() const
{
	const FVector Extent(WorldSize * 0.5f, WorldSize * 0.5f, FMath::Max(1000.0f, ZScale));
	return FBox::BuildAABB(GetActorLocation(), Extent);
}

float ATerraDyneChunk::GetHeightAtLocation(FVector LocalPos) const
{
	if (Resolution < 2 || HeightBuffer.Num() != Resolution * Resolution)
	{
		return 0.0f;
	}

	float HalfSize = WorldSize * 0.5f;
	float Step = WorldSize / (Resolution - 1);
	
	float NormX = (LocalPos.X + HalfSize) / WorldSize;
	float NormY = (LocalPos.Y + HalfSize) / WorldSize;
	
	float GridX = NormX * (Resolution - 1);
	float GridY = NormY * (Resolution - 1);
	
	int32 X0 = FMath::FloorToInt(GridX);
	int32 Y0 = FMath::FloorToInt(GridY);
	int32 X1 = FMath::Min(X0 + 1, Resolution - 1);
	int32 Y1 = FMath::Min(Y0 + 1, Resolution - 1);
	
	float FX = GridX - X0;
	float FY = GridY - Y0;
	
	float H00 = HeightBuffer[FMath::Clamp(Y0 * Resolution + X0, 0, HeightBuffer.Num() - 1)];
	float H10 = HeightBuffer[FMath::Clamp(Y0 * Resolution + X1, 0, HeightBuffer.Num() - 1)];
	float H01 = HeightBuffer[FMath::Clamp(Y1 * Resolution + X0, 0, HeightBuffer.Num() - 1)];
	float H11 = HeightBuffer[FMath::Clamp(Y1 * Resolution + X1, 0, HeightBuffer.Num() - 1)];

	// Triangle-aware interpolation matching mesh winding (BL→BR→TR / BL→TR→TL)
	float Height;
	if (FY <= FX)
		Height = H00 + FX * (H10 - H00) + FY * (H11 - H10);
	else
		Height = H00 + FX * (H11 - H01) + FY * (H01 - H00);

	return Height * ZScale;
}

FTerraDyneChunkData ATerraDyneChunk::GetSerializedData()
{
	FTerraDyneChunkData Data;
	Data.Coordinate = GridCoordinate;
	Data.ZScale = ZScale;
	
	if (bUseGPU)
	{
		ReadbackRenderTarget();
	}

	int32 SerializedResolution = Resolution;
	int32 SerializedSamples = SerializedResolution * SerializedResolution;
	if (HeightBuffer.Num() != SerializedSamples)
	{
		const int32 DerivedResolution = ResolveSquareResolutionFromSampleCount(HeightBuffer.Num());
		if (DerivedResolution > 1)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Chunk [%d,%d]: Serializing with derived resolution %d because HeightBuffer has %d samples but Resolution is %d."),
				GridCoordinate.X, GridCoordinate.Y, DerivedResolution, HeightBuffer.Num(), Resolution);
			SerializedResolution = DerivedResolution;
			SerializedSamples = SerializedResolution * SerializedResolution;
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Chunk [%d,%d]: Invalid non-square HeightBuffer (%d samples). Serializing flat fallback for resolution %d."),
				GridCoordinate.X, GridCoordinate.Y, HeightBuffer.Num(), Resolution);
			SerializedResolution = FMath::Max(Resolution, 2);
			SerializedSamples = SerializedResolution * SerializedResolution;
		}
	}

	Data.Resolution = SerializedResolution;
	Data.HeightData = HeightBuffer.Num() == SerializedSamples
		? HeightBuffer
		: TArray<float>();
	if (Data.HeightData.Num() != SerializedSamples)
	{
		Data.HeightData.Init(0.0f, SerializedSamples);
	}

	Data.BaseData = BaseBuffer.Num() == SerializedSamples ? BaseBuffer : Data.HeightData;
	Data.SculptData = SculptBuffer.Num() == SerializedSamples ? SculptBuffer : TArray<float>();
	Data.DetailData = DetailBuffer.Num() == SerializedSamples ? DetailBuffer : TArray<float>();
	if (Data.SculptData.Num() != SerializedSamples)
	{
		Data.SculptData.Init(0.0f, SerializedSamples);
	}
	if (Data.DetailData.Num() != SerializedSamples)
	{
		Data.DetailData.Init(0.0f, SerializedSamples);
	}

	// Pack weight layers as RGBA8
	const int32 NumPx = SerializedSamples;
	Data.WeightData.SetNumZeroed(FMath::Max(0, NumPx) * 4);
	for (int32 i = 0; i < NumPx; i++)
	{
		Data.WeightData[i * 4 + 0] = WeightBuffers[0].IsValidIndex(i) ? (uint8)FMath::Clamp(FMath::RoundToInt(WeightBuffers[0][i] * 255.f), 0, 255) : 0;
		Data.WeightData[i * 4 + 1] = WeightBuffers[1].IsValidIndex(i) ? (uint8)FMath::Clamp(FMath::RoundToInt(WeightBuffers[1][i] * 255.f), 0, 255) : 0;
		Data.WeightData[i * 4 + 2] = WeightBuffers[2].IsValidIndex(i) ? (uint8)FMath::Clamp(FMath::RoundToInt(WeightBuffers[2][i] * 255.f), 0, 255) : 0;
		Data.WeightData[i * 4 + 3] = WeightBuffers[3].IsValidIndex(i) ? (uint8)FMath::Clamp(FMath::RoundToInt(WeightBuffers[3][i] * 255.f), 0, 255) : 0;
	}

	Data.bTransferredFoliageFollowsTerrain = bTransferredFoliageFollowsTerrain;
	Data.FoliageStaticMeshPaths = TransferredFoliageStaticMeshPaths;
	Data.FoliageMaterialCounts = TransferredFoliageMaterialCounts;
	Data.FoliageOverrideMaterialPaths = TransferredFoliageOverrideMaterialPaths;
	Data.FoliageDefinitionIndices = TransferredFoliageDefinitionIndices;
	Data.FoliageInstanceLocalTransforms = TransferredFoliageInstanceLocalTransforms;
	Data.FoliageInstanceTerrainOffsets = TransferredFoliageInstanceTerrainOffsets;
	Data.ActorFoliageClassPaths = TransferredActorFoliageClassPaths;
	Data.ActorFoliageAttachFlags = TransferredActorFoliageAttachFlags;
	Data.ActorFoliageDefinitionIndices = TransferredActorFoliageDefinitionIndices;
	Data.ActorFoliageInstanceLocalTransforms = TransferredActorFoliageInstanceLocalTransforms;
	Data.ActorFoliageInstanceTerrainOffsets = TransferredActorFoliageInstanceTerrainOffsets;

	return Data;
}

void ATerraDyneChunk::LoadFromData(const FTerraDyneChunkData& Data)
{
	int32 LoadResolution = Data.Resolution;
	int32 ExpectedPx = LoadResolution * LoadResolution;
	if (LoadResolution < 2 || Data.HeightData.Num() != ExpectedPx)
	{
		const int32 DerivedResolution = ResolveSquareResolutionFromSampleCount(Data.HeightData.Num());
		if (DerivedResolution > 1)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Chunk [%d,%d]: LoadFromData corrected resolution %d -> %d from %d serialized samples."),
				GridCoordinate.X, GridCoordinate.Y, Data.Resolution, DerivedResolution, Data.HeightData.Num());
			LoadResolution = DerivedResolution;
			ExpectedPx = LoadResolution * LoadResolution;
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Chunk [%d,%d]: LoadFromData received invalid height payload (%d samples for resolution %d). Falling back to flat terrain."),
				GridCoordinate.X, GridCoordinate.Y, Data.HeightData.Num(), Data.Resolution);
			LoadResolution = FMath::Max(Data.Resolution, 2);
			ExpectedPx = LoadResolution * LoadResolution;
		}
	}

	if (LoadResolution != Resolution || !bInitialized)
	{
		UE_LOG(LogTemp, Warning, TEXT("Chunk [%d,%d] Resolution mismatch on load (%d → %d). Re-initializing."),
			GridCoordinate.X, GridCoordinate.Y, Resolution, LoadResolution);
		bInitialized = false;
		Initialize(LoadResolution, WorldSize);
	}

	GridCoordinate = Data.Coordinate;
	ZScale = Data.ZScale;
	HeightBuffer = Data.HeightData.Num() == ExpectedPx ? Data.HeightData : TArray<float>();
	if (HeightBuffer.Num() != ExpectedPx)
	{
		HeightBuffer.Init(0.0f, ExpectedPx);
	}

	// Restore individual height layers; fall back to collapsing into BaseBuffer for old saves
	if (Data.BaseData.Num() == ExpectedPx)
	{
		BaseBuffer = Data.BaseData;
		SculptBuffer = Data.SculptData.Num() == ExpectedPx ? Data.SculptData : TArray<float>();
		DetailBuffer = Data.DetailData.Num() == ExpectedPx ? Data.DetailData : TArray<float>();
		if (SculptBuffer.Num() != ExpectedPx) SculptBuffer.Init(0.f, ExpectedPx);
		if (DetailBuffer.Num() != ExpectedPx) DetailBuffer.Init(0.f, ExpectedPx);
	}
	else
	{
		BaseBuffer = HeightBuffer;
		SculptBuffer.Init(0.0f, ExpectedPx);
		DetailBuffer.Init(0.0f, ExpectedPx);
	}

	// Restore weight buffers (graceful: old saves have empty WeightData)
	int32 NumPx = Resolution * Resolution;
	int32 ExpectedBytes = NumPx * 4;
	for (int32 L = 0; L < 4; L++)
	{
		WeightBuffers[L].SetNumZeroed(NumPx);
	}
	if (Data.WeightData.Num() == ExpectedBytes)
	{
		for (int32 i = 0; i < NumPx; i++)
		{
			WeightBuffers[0][i] = Data.WeightData[i * 4 + 0] / 255.f;
			WeightBuffers[1][i] = Data.WeightData[i * 4 + 1] / 255.f;
			WeightBuffers[2][i] = Data.WeightData[i * 4 + 2] / 255.f;
			WeightBuffers[3][i] = Data.WeightData[i * 4 + 3] / 255.f;
		}
	}
	UploadWeightTexture();

	if (bUseGPU)
	{
		UpdateRenderTargetFromHeightmap();
		FlushRenderingCommands();
	}

	RebuildMesh();
	UpdateCollision();
	SetTransferredFoliageData(
		Data.FoliageStaticMeshPaths,
		Data.FoliageMaterialCounts,
		Data.FoliageOverrideMaterialPaths,
		Data.FoliageDefinitionIndices,
		Data.FoliageInstanceLocalTransforms,
		Data.FoliageInstanceTerrainOffsets,
		Data.bTransferredFoliageFollowsTerrain);
	SetTransferredActorFoliageData(
		Data.ActorFoliageClassPaths,
		Data.ActorFoliageAttachFlags,
		Data.ActorFoliageDefinitionIndices,
		Data.ActorFoliageInstanceLocalTransforms,
		Data.ActorFoliageInstanceTerrainOffsets);
	if (GrassProfile)
	{
		RequestGrassRegen();
	}
}

void ATerraDyneChunk::UpdateLOD(const FVector& ViewerPos)
{
	// Simple distance-based LOD
	if (!DynamicMeshComp) return;
	
	float DistSq = FVector::DistSquared(ViewerPos, GetActorLocation());
	float ThresholdSq = 2500000000.0f; // Default 500m

	if (const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>())
	{
		float Thresh = Settings->LODDistanceThreshold;
		ThresholdSq = Thresh * Thresh;
	}
	
	if (DistSq > ThresholdSq) 
	{
		if (DynamicMeshComp->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
		{
			DynamicMeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			// Optional: Reduce mesh resolution here if we had a simplified mesh generator
		}
	}
	else
	{
		if (DynamicMeshComp->GetCollisionEnabled() != ECollisionEnabled::QueryAndPhysics)
		{
			DynamicMeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			// Force update if we just woke up
			UpdateCollision();
		}
	}
}

void ATerraDyneChunk::SetGrassProfile(UTerraDyneGrassProfile* Profile)
{
	GrassProfile = Profile;
}

void ATerraDyneChunk::RequestGrassRegen()
{
	if (!GrassProfile || GrassProfile->Varieties.Num() == 0) return;

	UTerraDyneSubsystem* Sys = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneSubsystem>() : nullptr;
	if (!Sys) return;

	TSharedPtr<FTerraDyneGrassSystem> GrassSys = Sys->GetGrassSystem();
	if (!GrassSys.IsValid()) return;

	GrassSys->RequestRegenForChunk(
		this,
		HeightBuffer,
		WeightBuffers,
		NumWeightLayers,
		GrassProfile,
		Resolution,
		WorldSize,
		ZScale,
		GetActorLocation());
}

void ATerraDyneChunk::ApplyGrassResult(int32 VarietyIndex, TArray<FTransform>&& Transforms)
{
	if (!GrassProfile || !GrassProfile->Varieties.IsValidIndex(VarietyIndex)) return;

	const FTerraDyneGrassVariety& Variety = GrassProfile->Varieties[VarietyIndex];
	if (!Variety.GrassMesh) return;

	// Ensure the ISM array is large enough
	while (GrassISMs.Num() <= VarietyIndex)
	{
		UHierarchicalInstancedStaticMeshComponent* NewISM =
			NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
		NewISM->SetStaticMesh(Variety.GrassMesh);
		if (Variety.MaterialOverride)
		{
			NewISM->SetMaterial(0, Variety.MaterialOverride);
		}
		NewISM->SetCastShadow(false); // Grass shadows are expensive; disable by default
		NewISM->RegisterComponent();
		NewISM->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		GrassISMs.Add(NewISM);
	}

	UHierarchicalInstancedStaticMeshComponent* ISM = GrassISMs[VarietyIndex];
	if (!ISM) return;

	ISM->ClearInstances();
	for (const FTransform& T : Transforms)
	{
		ISM->AddInstance(T, /*bWorldSpace=*/true);
	}

	UE_LOG(LogTemp, Log, TEXT("Chunk [%d,%d] Grass[%d]: %d instances placed"),
		GridCoordinate.X, GridCoordinate.Y, VarietyIndex, Transforms.Num());
}

void ATerraDyneChunk::SetTransferredFoliageData(
	const TArray<FString>& InStaticMeshPaths,
	const TArray<int32>& InMaterialCounts,
	const TArray<FString>& InOverrideMaterialPaths,
	const TArray<int32>& InDefinitionIndices,
	const TArray<FTransform>& InLocalTransforms,
	const TArray<float>& InTerrainOffsets,
	bool bInFollowsTerrain)
{
	TransferredFoliageStaticMeshPaths = InStaticMeshPaths;
	TransferredFoliageMaterialCounts = InMaterialCounts;
	TransferredFoliageOverrideMaterialPaths = InOverrideMaterialPaths;
	TransferredFoliageDefinitionIndices = InDefinitionIndices;
	TransferredFoliageInstanceLocalTransforms = InLocalTransforms;
	TransferredFoliageInstanceTerrainOffsets = InTerrainOffsets;
	bTransferredFoliageFollowsTerrain = bInFollowsTerrain;
	bTransferredFoliageDirty = false;
	TransferredFoliageDebounceTimer = 0.0f;

	const int32 SharedInstanceCount = FMath::Min(
		TransferredFoliageDefinitionIndices.Num(),
		TransferredFoliageInstanceLocalTransforms.Num());
	if (SharedInstanceCount != TransferredFoliageDefinitionIndices.Num() ||
		SharedInstanceCount != TransferredFoliageInstanceLocalTransforms.Num())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Chunk [%d,%d]: Trimming transferred foliage data to %d shared instances."),
			GridCoordinate.X, GridCoordinate.Y, SharedInstanceCount);
		TransferredFoliageDefinitionIndices.SetNum(SharedInstanceCount);
		TransferredFoliageInstanceLocalTransforms.SetNum(SharedInstanceCount);
	}

	if (TransferredFoliageInstanceTerrainOffsets.Num() != SharedInstanceCount)
	{
		TransferredFoliageInstanceTerrainOffsets.SetNumZeroed(SharedInstanceCount);
	}

	ClearTransferredFoliageComponents();
	RefreshTransferredFoliagePlacement(bTransferredFoliageFollowsTerrain);
}

void ATerraDyneChunk::RefreshTransferredFoliagePlacement(bool bSnapToTerrain)
{
	const int32 DefinitionCount = TransferredFoliageStaticMeshPaths.Num();
	const int32 InstanceCount = TransferredFoliageInstanceLocalTransforms.Num();
	if (DefinitionCount == 0 || InstanceCount == 0)
	{
		ClearTransferredFoliageComponents();
		bTransferredFoliageDirty = false;
		TransferredFoliageDebounceTimer = 0.0f;
		return;
	}

	if (TransferredFoliageISMs.Num() != DefinitionCount)
	{
		ClearTransferredFoliageComponents();
		TransferredFoliageISMs.SetNum(DefinitionCount);

		int32 MaterialCursor = 0;
		for (int32 DefinitionIndex = 0; DefinitionIndex < DefinitionCount; DefinitionIndex++)
		{
			const FString& StaticMeshPath = TransferredFoliageStaticMeshPaths[DefinitionIndex];
			UStaticMesh* StaticMesh = StaticMeshPath.IsEmpty()
				? nullptr
				: LoadObject<UStaticMesh>(nullptr, *StaticMeshPath);

			int32 MaterialCount = TransferredFoliageMaterialCounts.IsValidIndex(DefinitionIndex)
				? FMath::Max(0, TransferredFoliageMaterialCounts[DefinitionIndex])
				: 0;
			MaterialCount = FMath::Min(
				MaterialCount,
				FMath::Max(0, TransferredFoliageOverrideMaterialPaths.Num() - MaterialCursor));

			if (!StaticMesh)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("Chunk [%d,%d]: Failed to load transferred foliage mesh '%s'."),
					GridCoordinate.X, GridCoordinate.Y, *StaticMeshPath);
				MaterialCursor += MaterialCount;
				continue;
			}

			UHierarchicalInstancedStaticMeshComponent* NewISM =
				NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
			NewISM->SetupAttachment(GetRootComponent());
			NewISM->SetMobility(EComponentMobility::Movable);
			NewISM->SetStaticMesh(StaticMesh);
			NewISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			NewISM->SetCanEverAffectNavigation(false);
			NewISM->SetCastShadow(true);

			for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; MaterialIndex++)
			{
				const FString& MaterialPath = TransferredFoliageOverrideMaterialPaths[MaterialCursor + MaterialIndex];
				if (MaterialPath.IsEmpty())
				{
					continue;
				}

				if (UMaterialInterface* OverrideMaterial = LoadObject<UMaterialInterface>(nullptr, *MaterialPath))
				{
					NewISM->SetMaterial(MaterialIndex, OverrideMaterial);
				}
			}
			MaterialCursor += MaterialCount;

			NewISM->RegisterComponent();
			TransferredFoliageISMs[DefinitionIndex] = NewISM;
		}
	}
	else
	{
		for (UHierarchicalInstancedStaticMeshComponent* ExistingISM : TransferredFoliageISMs)
		{
			if (ExistingISM)
			{
				ExistingISM->ClearInstances();
			}
		}
	}

	for (int32 InstanceIndex = 0; InstanceIndex < InstanceCount; InstanceIndex++)
	{
		const int32 DefinitionIndex = TransferredFoliageDefinitionIndices.IsValidIndex(InstanceIndex)
			? TransferredFoliageDefinitionIndices[InstanceIndex]
			: INDEX_NONE;
		if (!TransferredFoliageISMs.IsValidIndex(DefinitionIndex))
		{
			continue;
		}

		UHierarchicalInstancedStaticMeshComponent* ISM = TransferredFoliageISMs[DefinitionIndex];
		if (!ISM)
		{
			continue;
		}

		FTransform InstanceTransform = TransferredFoliageInstanceLocalTransforms[InstanceIndex];
		if (bSnapToTerrain)
		{
			FVector LocalLocation = InstanceTransform.GetLocation();
			const float TerrainOffset = TransferredFoliageInstanceTerrainOffsets.IsValidIndex(InstanceIndex)
				? TransferredFoliageInstanceTerrainOffsets[InstanceIndex]
				: 0.0f;
			LocalLocation.Z = GetHeightAtLocation(FVector(LocalLocation.X, LocalLocation.Y, 0.0f)) + TerrainOffset;
			InstanceTransform.SetLocation(LocalLocation);
		}

		ISM->AddInstance(InstanceTransform, false);
	}

	bTransferredFoliageDirty = false;
	TransferredFoliageDebounceTimer = 0.0f;
}

void ATerraDyneChunk::SetTransferredActorFoliageData(
	const TArray<FString>& InActorClassPaths,
	const TArray<uint8>& InAttachFlags,
	const TArray<int32>& InDefinitionIndices,
	const TArray<FTransform>& InLocalTransforms,
	const TArray<float>& InTerrainOffsets)
{
	TransferredActorFoliageClassPaths = InActorClassPaths;
	TransferredActorFoliageAttachFlags = InAttachFlags;
	TransferredActorFoliageDefinitionIndices = InDefinitionIndices;
	TransferredActorFoliageInstanceLocalTransforms = InLocalTransforms;
	TransferredActorFoliageInstanceTerrainOffsets = InTerrainOffsets;

	const int32 SharedInstanceCount = FMath::Min(
		TransferredActorFoliageDefinitionIndices.Num(),
		TransferredActorFoliageInstanceLocalTransforms.Num());
	if (SharedInstanceCount != TransferredActorFoliageDefinitionIndices.Num() ||
		SharedInstanceCount != TransferredActorFoliageInstanceLocalTransforms.Num())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Chunk [%d,%d]: Trimming transferred actor foliage data to %d shared instances."),
			GridCoordinate.X, GridCoordinate.Y, SharedInstanceCount);
		TransferredActorFoliageDefinitionIndices.SetNum(SharedInstanceCount);
		TransferredActorFoliageInstanceLocalTransforms.SetNum(SharedInstanceCount);
	}

	if (TransferredActorFoliageInstanceTerrainOffsets.Num() != SharedInstanceCount)
	{
		TransferredActorFoliageInstanceTerrainOffsets.SetNumZeroed(SharedInstanceCount);
	}

	ClearTransferredActorFoliageActors();
	RefreshTransferredActorFoliagePlacement(bTransferredFoliageFollowsTerrain);
}

void ATerraDyneChunk::RefreshTransferredActorFoliagePlacement(bool bSnapToTerrain)
{
	const int32 DefinitionCount = TransferredActorFoliageClassPaths.Num();
	const int32 InstanceCount = TransferredActorFoliageInstanceLocalTransforms.Num();
	if (DefinitionCount == 0 || InstanceCount == 0)
	{
		ClearTransferredActorFoliageActors();
		bTransferredFoliageDirty = false;
		TransferredFoliageDebounceTimer = 0.0f;
		return;
	}

	if (TransferredActorFoliageActors.Num() != InstanceCount)
	{
		ClearTransferredActorFoliageActors();
		TransferredActorFoliageActors.SetNum(InstanceCount);
	}

	const FTransform ChunkTransform = GetActorTransform();
	for (int32 InstanceIndex = 0; InstanceIndex < InstanceCount; InstanceIndex++)
	{
		const int32 DefinitionIndex = TransferredActorFoliageDefinitionIndices.IsValidIndex(InstanceIndex)
			? TransferredActorFoliageDefinitionIndices[InstanceIndex]
			: INDEX_NONE;
		if (!TransferredActorFoliageClassPaths.IsValidIndex(DefinitionIndex))
		{
			continue;
		}

		UClass* ActorClass = LoadClass<AActor>(nullptr, *TransferredActorFoliageClassPaths[DefinitionIndex]);
		if (!ActorClass)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Chunk [%d,%d]: Failed to load transferred actor foliage class '%s'."),
				GridCoordinate.X, GridCoordinate.Y, *TransferredActorFoliageClassPaths[DefinitionIndex]);
			continue;
		}

		const AActor* ActorCDO = ActorClass->GetDefaultObject<AActor>();
		const bool bSpawnLocally = HasAuthority() || !(ActorCDO && ActorCDO->GetIsReplicated());
		AActor* ExistingActor = TransferredActorFoliageActors[InstanceIndex];
		if (!bSpawnLocally)
		{
			if (ExistingActor)
			{
				ExistingActor->Destroy();
				TransferredActorFoliageActors[InstanceIndex] = nullptr;
			}
			continue;
		}

		FTransform LocalTransform = TransferredActorFoliageInstanceLocalTransforms[InstanceIndex];
		if (bSnapToTerrain)
		{
			FVector LocalLocation = LocalTransform.GetLocation();
			const float TerrainOffset = TransferredActorFoliageInstanceTerrainOffsets.IsValidIndex(InstanceIndex)
				? TransferredActorFoliageInstanceTerrainOffsets[InstanceIndex]
				: 0.0f;
			LocalLocation.Z = GetHeightAtLocation(FVector(LocalLocation.X, LocalLocation.Y, 0.0f)) + TerrainOffset;
			LocalTransform.SetLocation(LocalLocation);
		}

		const bool bAttachToChunk =
			TransferredActorFoliageAttachFlags.IsValidIndex(DefinitionIndex) &&
			TransferredActorFoliageAttachFlags[DefinitionIndex] != 0;
		const FTransform WorldTransform = LocalTransform * ChunkTransform;

		if (!ExistingActor || ExistingActor->GetClass() != ActorClass || ExistingActor->IsActorBeingDestroyed())
		{
			if (ExistingActor)
			{
				ExistingActor->Destroy();
			}

			FActorSpawnParameters SpawnParams;
			SpawnParams.Owner = this;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			SpawnParams.ObjectFlags |= RF_Transient;

			ExistingActor = GetWorld() ? GetWorld()->SpawnActor<AActor>(ActorClass, WorldTransform, SpawnParams) : nullptr;
			TransferredActorFoliageActors[InstanceIndex] = ExistingActor;
		}

		if (!ExistingActor)
		{
			continue;
		}

		if (bAttachToChunk && ExistingActor->GetRootComponent() && GetRootComponent())
		{
			ExistingActor->GetRootComponent()->SetMobility(EComponentMobility::Movable);
			ExistingActor->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
			ExistingActor->SetActorRelativeTransform(LocalTransform);
		}
		else
		{
			if (ExistingActor->GetAttachParentActor() == this)
			{
				ExistingActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			}
			ExistingActor->SetActorTransform(WorldTransform);
		}
	}

	bTransferredFoliageDirty = false;
	TransferredFoliageDebounceTimer = 0.0f;
}

bool ATerraDyneChunk::GetTransferredActorFoliageInstanceTransform(int32 InstanceIndex, FTransform& OutTransform) const
{
	if (!TransferredActorFoliageActors.IsValidIndex(InstanceIndex) || !TransferredActorFoliageActors[InstanceIndex])
	{
		return false;
	}

	OutTransform = TransferredActorFoliageActors[InstanceIndex]->GetActorTransform();
	return true;
}

void ATerraDyneChunk::RequestTransferredFoliageRefresh()
{
	if (!bTransferredFoliageFollowsTerrain ||
		(TransferredFoliageInstanceLocalTransforms.Num() == 0 &&
		 TransferredActorFoliageInstanceLocalTransforms.Num() == 0))
	{
		return;
	}

	float RefreshDelay = 0.5f;
	if (const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>())
	{
		RefreshDelay = Settings->GrassDebounceTime;
	}

	bTransferredFoliageDirty = true;
	TransferredFoliageDebounceTimer = RefreshDelay;
}

void ATerraDyneChunk::SetTransferredFoliageFollowsTerrain(bool bInFollowsTerrain)
{
	bTransferredFoliageFollowsTerrain = bInFollowsTerrain;
	if (bTransferredFoliageFollowsTerrain)
	{
		RequestTransferredFoliageRefresh();
	}
}

void ATerraDyneChunk::ClearTransferredFoliageComponents()
{
	for (UHierarchicalInstancedStaticMeshComponent* ISM : TransferredFoliageISMs)
	{
		if (ISM)
		{
			ISM->DestroyComponent();
		}
	}

	TransferredFoliageISMs.Reset();
}

void ATerraDyneChunk::ClearTransferredActorFoliageActors()
{
	for (AActor* Actor : TransferredActorFoliageActors)
	{
		if (Actor && !Actor->IsActorBeingDestroyed())
		{
			Actor->Destroy();
		}
	}

	TransferredActorFoliageActors.Reset();
}
