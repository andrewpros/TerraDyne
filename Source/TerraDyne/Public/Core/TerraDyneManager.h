// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/TerraDyneWorldTypes.h"
#include "World/TerraDyneTileData.h"
#include "Core/TerraDyneSaveGame.h"
#include "TerraDyneManager.generated.h"

// Forward Declarations
class ATerraDyneChunk;
class ALandscapeProxy;
class ULandscapeComponent;
class UMaterialInterface;
class UTerraDyneGrassProfile;
class ULandscapeLayerInfoObject;
class USceneComponent;
class ATerraDyneEditController; // forward declare for SendFullSyncToController
class UTerraDyneWorldPreset;

/** Buffers snapshot for a single chunk before a stroke begins. */
struct FTerraDyneChunkSnapshot
{
	FIntPoint Coordinate;
	TArray<float> SculptBuffer;
	TArray<TArray<float>> WeightBuffers; // 4 entries, one per weight layer
};

/** One undo/redo history entry — covers all chunks touched by a single stroke. */
struct FTerraDyneUndoEntry
{
	TMap<FIntPoint, FTerraDyneChunkSnapshot> Snapshots;
};

USTRUCT(BlueprintType)
struct FTerraDyneGPUStats
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Stats")
	FString AdapterName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Stats")
	bool bIsCudaAvailable = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Stats")
	int32 DedicatedVideoMemory = 0; // in MB

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Stats")
	FString ComputeBackend; // GPU+CPU Hybrid or CPU Fallback
};

USTRUCT(BlueprintType)
struct FTerraDyneLandscapeMigrationOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Migration")
	bool bHideSourceLandscape = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Migration")
	bool bClearExistingChunks = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Migration")
	bool bPauseStreamingDuringImport = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Migration")
	bool bImportWeightLayers = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Migration")
	bool bCaptureLayerMappings = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Migration")
	bool bRegenerateGrassFromImportedLayers = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Migration")
	bool bAdoptLandscapeMaterialAsMasterMaterial = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Migration")
	bool bTransferPlacedFoliage = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Migration")
	bool bTransferredFoliageFollowsTerrain = true;
};

UCLASS(Blueprintable)
class TERRADYNE_API ATerraDyneManager : public AActor
{
	GENERATED_BODY()

public:
	ATerraDyneManager();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne")
	TObjectPtr<USceneComponent> SceneRoot;

	//--- CONFIGURATION ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Import Settings")
	TObjectPtr<ALandscapeProxy> TargetLandscapeSource;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "TerraDyne|Import Settings")
	float GlobalChunkSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "TerraDyne|Config")
	TSubclassOf<ATerraDyneChunk> ChunkClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "TerraDyne|Config")
	ETerraDyneLayer ActiveLayer = ETerraDyneLayer::Sculpt;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_MasterMaterial, Category = "TerraDyne|Config")
	TObjectPtr<UMaterialInterface> MasterMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Tools")
	TObjectPtr<UMaterialInterface> HeightBrushMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Tools")
	TObjectPtr<UMaterialInterface> WeightBrushMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Tools")
	TObjectPtr<UMaterialInterface> NoiseBrushMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_ActiveGrassProfile, Category = "TerraDyne|Grass")
	TObjectPtr<UTerraDyneGrassProfile> ActiveGrassProfile;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Migration")
	FTerraDyneLandscapeMigrationOptions LandscapeMigrationOptions;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_LandscapeMigrationState, Category = "TerraDyne|Migration")
	FTerraDyneLandscapeMigrationState LandscapeMigrationState;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Framework")
	TObjectPtr<UTerraDyneWorldPreset> WorldPreset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_FrameworkConfig, Category = "TerraDyne|Procedural")
	FTerraDyneProceduralWorldSettings ProceduralWorldSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_FrameworkConfig, Category = "TerraDyne|Procedural")
	TArray<FTerraDyneBiomeOverlay> BiomeOverlays;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Population")
	TArray<FTerraDynePopulationSpawnRule> PopulationSpawnRules;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_FrameworkConfig, Category = "TerraDyne|Gameplay")
	TArray<FTerraDyneAISpawnZone> AISpawnZones;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_FrameworkConfig, Category = "TerraDyne|Gameplay")
	TArray<FTerraDyneBuildPermissionZone> BuildPermissionZones;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Gameplay")
	bool bRefreshNavMeshOnTerrainChanges = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Gameplay")
	bool bRefreshNavMeshOnPopulationChanges = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Gameplay")
	float NavigationDirtyBoundsPadding = 300.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Population")
	TArray<FTerraDynePersistentPopulationEntry> PersistentPopulationEntries;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Procedural")
	TArray<FTerraDyneProceduralChunkState> ProceduralChunkStates;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Bootstrap")
	bool bAutoImportAtRuntime = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Bootstrap")
	bool bSpawnDefaultChunksOnBeginPlay = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Bootstrap")
	bool bSetupDefaultLightingOnBeginPlay = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|Bootstrap")
	bool bSpawnShowcaseOnBeginPlay = false;

	//--- RUNTIME API ---
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Interaction")
	void ApplyGlobalBrush(
		FVector WorldLocation,
		float Radius,
		float Strength,
		ETerraDyneBrushMode BrushMode,
		int32 WeightLayerIndex = 0,
		float FlattenHeight = 0.f);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Interaction")
	void ApplyGlobalNoise(float Strength, float Frequency, float Seed);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Query")
	ATerraDyneChunk* GetChunkAtLocation(FVector WorldLocation) const;

	/** Returns all active chunks whose AABB overlaps the given world-space circle. */
	TArray<ATerraDyneChunk*> GetChunksInRadius(FVector WorldLocation, float Radius);

	/** Returns the chunk at the given grid coordinate, or nullptr. */
	ATerraDyneChunk* GetChunkAtCoord(FIntPoint Coord) const;

	void RegisterChunk(ATerraDyneChunk* Chunk);
	void UnregisterChunk(ATerraDyneChunk* Chunk);

	void BeginStroke(FVector WorldLocation, float Radius, APlayerController* Controller);
	void CommitStroke(APlayerController* Controller);
	void RestoreSnapshots(const FTerraDyneUndoEntry& Entry);
	void Undo(APlayerController* Controller);
	void Redo(APlayerController* Controller);

	/** Server → All Clients: broadcast a brush application so every client updates visuals. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_ApplyBrush(const FTerraDyneBrushParams& Params);

	/** Send all current chunk data to a specific controller (late-join sync). Server only. */
	void SendFullSyncToController(ATerraDyneEditController* Controller);

	/** Server → All Clients: replay the full terrain state after an undo/redo. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_SyncChunkState(FIntPoint Coord, const TArray<float>& InSculptBuffer, const TArray<uint8>& InWeightData);

	/** Remove undo/redo stacks for a disconnected player. */
	void CleanupPlayerStacks(APlayerController* Controller);

	/** Returns true if a stroke is pending for the given controller. */
	bool HasPendingStroke(APlayerController* Controller) const
	{
		return PendingStroke.IsSet() && PendingStrokeOwner == Controller;
	}

	/** Returns true if undo is available for this player. */
	bool CanUndo(APlayerController* Controller) const
	{
		const TArray<FTerraDyneUndoEntry>* Stack = UndoStacks.Find(Controller);
		return Stack && Stack->Num() > 0;
	}

	/** Returns true if redo is available for this player. */
	bool CanRedo(APlayerController* Controller) const
	{
		const TArray<FTerraDyneUndoEntry>* Stack = RedoStacks.Find(Controller);
		return Stack && Stack->Num() > 0;
	}

	/** Depth of undo stack for a player (for testing). */
	int32 GetUndoDepth(APlayerController* Controller) const
	{
		const TArray<FTerraDyneUndoEntry>* Stack = UndoStacks.Find(Controller);
		return Stack ? Stack->Num() : 0;
	}

	/** Depth of redo stack for a player (for testing). */
	int32 GetRedoDepth(APlayerController* Controller) const
	{
		const TArray<FTerraDyneUndoEntry>* Stack = RedoStacks.Find(Controller);
		return Stack ? Stack->Num() : 0;
	}

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|System")
	void RebuildChunkMap();

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|System")
	void SaveWorld(FString SlotName = "TerraDyneSave");

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|System")
	void LoadWorld(FString SlotName = "TerraDyneSave");

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Migration")
	void RegenerateImportedGrass();

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Migration")
	void SetAuthoredChunkCoordinates(const TArray<FIntPoint>& ChunkCoords, bool bWasImportedFromLandscape = true);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Framework")
	void ApplyWorldPreset(UTerraDyneWorldPreset* Preset = nullptr);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Population")
	FGuid RegisterPersistentActor(
		AActor* Actor,
		ETerraDynePopulationKind Kind = ETerraDynePopulationKind::Prop,
		bool bAllowRegrowth = false,
		float RegrowthDelaySeconds = 0.0f,
		bool bSnapToTerrain = false);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Population")
	FGuid PlacePersistentActorFromDescriptor(
		const FTerraDynePopulationDescriptor& Descriptor,
		const FTransform& WorldTransform,
		FName SourceRuleId = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Population")
	FGuid PlacePersistentActor(
		TSubclassOf<AActor> ActorClass,
		const FTransform& WorldTransform,
		ETerraDynePopulationKind Kind = ETerraDynePopulationKind::RuntimePlaced,
		bool bSnapToTerrain = true,
		float TerrainOffset = 0.0f,
		bool bAllowRegrowth = false,
		float RegrowthDelaySeconds = 0.0f);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Population")
	bool HarvestPersistentPopulation(const FGuid& PopulationId);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Population")
	bool SetPersistentPopulationDestroyed(const FGuid& PopulationId, bool bDestroyed);

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Population")
	void RefreshPopulationForLoadedChunks();

	UFUNCTION(BlueprintPure, Category = "TerraDyne|Population")
	int32 GetPersistentPopulationCount() const { return PersistentPopulationEntries.Num(); }

	UFUNCTION(BlueprintPure, Category = "TerraDyne|Procedural")
	FName GetBiomeTagAtLocation(FVector WorldLocation) const;

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Gameplay")
	bool CanBuildAtLocation(FVector WorldLocation, FString& OutReason) const;

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Gameplay")
	TArray<FTerraDyneAISpawnZone> GetAISpawnZonesAtLocation(FVector WorldLocation) const;

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|PCG")
	TArray<FTerraDynePCGPoint> GetPCGSeedPointsForChunk(
		FIntPoint Coord,
		bool bIncludePopulation = true,
		bool bIncludeAISpawnZones = true) const;

	UPROPERTY(BlueprintAssignable, Category = "TerraDyne|Events")
	FTerraDyneTerrainChangedDelegate OnTerrainChanged;

	UPROPERTY(BlueprintAssignable, Category = "TerraDyne|Events")
	FTerraDyneFoliageChangedDelegate OnFoliageChanged;

	UPROPERTY(BlueprintAssignable, Category = "TerraDyne|Events")
	FTerraDynePopulationChangedDelegate OnPopulationChanged;

	/** When true, streaming is frozen — no chunks are loaded or unloaded. */
	UPROPERTY(Transient, BlueprintReadWrite, Category = "TerraDyne|Streaming")
	bool bStreamingPaused = false;

	//--- STATS ---
	UFUNCTION(BlueprintPure, Category = "TerraDyne|Stats")
	int32 GetActiveChunkCount() const { return ActiveChunkMap.Num(); }

	UFUNCTION(BlueprintPure, Category = "TerraDyne|Stats")
	int32 GetTotalVertexCount() const;

	UFUNCTION(BlueprintPure, Category = "TerraDyne|Stats")
	FTerraDyneGPUStats GetGPUStats() const;

	//--- EDITOR TOOLS ---
#if WITH_EDITOR
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "TerraDyne|Tools")
	void ManualImport();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "TerraDyne|Migration")
	void MigrateLandscapeProject();

	void ImportFromLandscape(ALandscapeProxy* TargetLandscape, bool bHideSource = true);
	void ImportFromLandscapeWithOptions(ALandscapeProxy* TargetLandscape, const FTerraDyneLandscapeMigrationOptions& Options);
	void ResampleLandscapeData(ATerraDyneChunk* Chunk, ULandscapeComponent* SourceComponent, bool bImportWeightLayers);
	void ImportInternal(ALandscapeProxy* Source, const FTerraDyneLandscapeMigrationOptions& Options);
#endif

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void OnRep_MasterMaterial();

	UFUNCTION()
	void OnRep_ActiveGrassProfile();

	UFUNCTION()
	void OnRep_LandscapeMigrationState();

	UFUNCTION()
	void OnRep_FrameworkConfig();

private:
	UPROPERTY(Transient)
	TMap<FIntPoint, ATerraDyneChunk*> ActiveChunkMap;

	bool bMaterialsLoaded = false;
	float LODTimer = 0.25f;

	// --- Streaming ---
	// Streaming iterates all player controllers and computes the union of load regions.
	TSet<FIntPoint> PendingLoadQueue;
	TSet<FIntPoint> PendingUnloadQueue;
	TSet<FIntPoint> DirtyChunkSet;
	TSet<FIntPoint> ImportedChunkCoords;
	TMap<FGuid, int32> PopulationEntryIndexById;
	TMap<FIntPoint, int32> ProceduralChunkStateIndexByCoord;
	TMap<AActor*, FGuid> PopulationIdByActor;
	TMap<FGuid, TWeakObjectPtr<AActor>> PopulationActorById;
	TSet<FGuid> PopulationDestroyInProgress;
	uint32 LastStreamingHash = 0;
	float PopulationMaintenanceTimer = 0.0f;

	FIntPoint WorldToChunkCoord(const FVector& WorldPos) const;
	void UpdateStreaming(const TArray<FVector>& PlayerPositions);
	void ProcessStreamingQueues(const TArray<FVector>& PlayerPositions);
	void LoadOrSpawnChunk(FIntPoint Coord);
	void UnloadChunk(FIntPoint Coord);
	void MarkChunkDirty(FIntPoint Coord);
	ATerraDyneChunk* SpawnConfiguredChunk(FIntPoint Coord, int32 OverrideResolution = INDEX_NONE);
	FString GetChunkCachePath(FIntPoint Coord) const;
	void SaveChunkToCache(FIntPoint Coord, const FTerraDyneChunkData& Data);
	bool LoadChunkFromCache(FIntPoint Coord, FTerraDyneChunkData& OutData);
	void ApplyMaterialToActiveChunks();
	void ApplyGrassProfileToActiveChunks(bool bTriggerRegen);
	void ClearUndoRedoState();
	void DestroyTrackedPopulationActors();
	bool IsImportedChunkCoord(FIntPoint Coord) const;
	bool ShouldAllowChunkCoord(FIntPoint Coord) const;
	int32 GetDistanceFromAuthoredChunks(FIntPoint Coord) const;
	int32 MakeChunkSeed(FIntPoint Coord) const;
	void RebuildWorldStateIndices();
	void EnsureChunkFrameworkState(FIntPoint Coord, ATerraDyneChunk* Chunk);
	FTerraDyneProceduralChunkState* FindProceduralChunkState(FIntPoint Coord);
	const FTerraDyneProceduralChunkState* FindProceduralChunkState(FIntPoint Coord) const;
	FName ResolveBiomeForChunk(FIntPoint Coord, const ATerraDyneChunk* Chunk, TArray<FName>* OutOverlays = nullptr) const;
	void GeneratePopulationForChunk(FIntPoint Coord, ATerraDyneChunk* Chunk);
	void SyncPopulationForChunk(FIntPoint Coord);
	void DespawnPopulationForChunk(FIntPoint Coord);
	AActor* SpawnPopulationActor(const FTerraDynePersistentPopulationEntry& Entry);
	void BindPopulationActor(const FGuid& PopulationId, AActor* Actor);
	UFUNCTION()
	void HandlePopulationActorDestroyed(AActor* DestroyedActor);
	void SetPopulationStateInternal(const FGuid& PopulationId, ETerraDynePopulationState NewState, FName Reason);
	void TickPopulationState(float DeltaTime);
	FTerraDynePopulationDescriptor MakeDescriptorFromActor(
		AActor* Actor,
		ETerraDynePopulationKind Kind,
		bool bAllowRegrowth,
		float RegrowthDelaySeconds,
		bool bSnapToTerrain) const;
	void BroadcastTerrainChanged(const TArray<FIntPoint>& ChangedCoords);
	void BroadcastFoliageChanged(const TArray<FIntPoint>& ChangedCoords);
	void BroadcastPopulationChanged(
		const FTerraDynePersistentPopulationEntry& Entry,
		FName Reason);
	void RefreshNavigationForBounds(const FBox& Bounds, bool bPopulationChange = false);
	float GetSlopeDegreesAtLocation(FVector WorldLocation) const;

	// --- Undo/Redo ---
	// NOTE: Undo/redo entries referencing unloaded (streamed-out) chunks are silently skipped.
	// A future improvement could persist undo snapshots alongside chunk cache data.
	TMap<APlayerController*, TArray<FTerraDyneUndoEntry>> UndoStacks;
	TMap<APlayerController*, TArray<FTerraDyneUndoEntry>> RedoStacks;
	TOptional<FTerraDyneUndoEntry> PendingStroke;
	APlayerController* PendingStrokeOwner = nullptr;

	void LoadMaterials();
	void SetupLighting();
	void SpawnDefaultSandboxChunk();
};
