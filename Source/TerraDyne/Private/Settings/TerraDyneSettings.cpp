// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Settings/TerraDyneSettings.h"

UTerraDyneSettings::UTerraDyneSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("TerraDyne");

	// Default Paths
	MasterMaterialPath = FSoftObjectPath(TEXT("/Game/TerraDyne/Materials/VHFM/M_TerraDyne_Master.M_TerraDyne_Master"));
	HeightBrushMaterialPath = FSoftObjectPath(TEXT("/Game/TerraDyne/Materials/Tools/M_HeightBrush.M_HeightBrush"));
	HUDWidgetPath = FSoftObjectPath();

	// Default Values
	DefaultChunkSize = 10000.0f;
	DefaultResolution = 128;

	// Performance Defaults
	LODDistanceThreshold = 50000.0f; // 500m
	CollisionDebounceTime = 0.2f;
	GrassDebounceTime = 0.5f;

	// Streaming Defaults
	ChunkLoadRadius = 5;
	ChunkUnloadRadius = 7;
	MaxChunkOpsPerTick = 2;
	GridExtent = 10;
	ChunkSaveDir = TEXT("TerraDyne/ChunkCache");

	// Undo/Redo Defaults
	MaxUndoHistory = 20;

	// Multiplayer Defaults
	MaxBrushRPCsPerSecond = 30.0f;
	MaxBrushRadius = 10000.0f;
	MaxBrushStrength = 5000.0f;
}
