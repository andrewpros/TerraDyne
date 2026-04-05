// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Grass/TerraDyneGrassTypes.h"
#include <atomic>

// Forward declarations
class ATerraDyneChunk;

/**
 * Per-variety data snapshot captured on the game thread.
 * Plain data only — no UObject pointers, safe to copy to a worker thread.
 */
struct FTerraDyneGrassVarietyData
{
	int32   VarietyIndex   = 0;
	float   Density        = 100.f;   // instances per 10000 cm² (1 m²)
	int32   WeightLayerIdx = 0;
	float   MinWeight      = 0.2f;
	float   ScaleMin       = 0.8f;
	float   ScaleMax       = 1.2f;
	bool    bAlignToSurface = true;
	float   MaxSlopeAngle  = 45.f;
};

/**
 * FTerraDyneGrassSystem
 *
 * The background scheduler for procedural vegetation.
 * Manages thread-safe access to grass data and dispatches generation tasks.
 */
class TERRADYNE_API FTerraDyneGrassSystem : public TSharedFromThis<FTerraDyneGrassSystem>
{
public:
	// Lifecycle
	void Initialize(UWorld* InWorld);
	void Shutdown();

	/**
	 * Called by Chunks when sculpt/paint data changes.
	 * Captures the chunk's height/weight data on the game thread and
	 * dispatches a background task that writes results back via
	 * Chunk->ApplyGrassResult() on the game thread.
	 *
	 * @param ChunkPtr         The requesting chunk (weak — may expire before task finishes).
	 * @param HeightBuffer     Snapshot of the chunk's combined height (0-1, normalised).
	 * @param WeightBuffers    4 weight layer snapshots (per-pixel, 0-1).
	 * @param NumWeightLayers  Number of weight layers (must match array size).
	 * @param Profile          Grass profile data asset (read only; called on GT after).
	 * @param ChunkResolution  Height map resolution.
	 * @param ChunkWorldSize   World-space side length of the chunk (cm).
	 * @param ChunkZScale      Height scale (cm).
	 * @param ChunkOrigin      World-space centre of the chunk.
	 */
	void RequestRegenForChunk(
		TWeakObjectPtr<ATerraDyneChunk> ChunkPtr,
		const TArray<float>& HeightBuffer,
		const TArray<float> WeightBuffers[],
		int32 NumWeightLayers,
		UTerraDyneGrassProfile* Profile,
		int32 ChunkResolution,
		float ChunkWorldSize,
		float ChunkZScale,
		FVector ChunkOrigin);

	/** Legacy interface — kept for source compatibility but no-ops. */
	void RequestRegen(const FBox& WorldBounds);

	/**
	 * Emergency stop for all tasks (e.g. Level Unload).
	 * In-flight tasks check the cancel token before posting results back to the game thread.
	 */
	void CancelAllTasks();

private:
	TWeakObjectPtr<UWorld> WorldRef;

	/** Shared with all in-flight tasks; set to true by CancelAllTasks(). */
	TSharedPtr<std::atomic<bool>> CancelToken;
};
