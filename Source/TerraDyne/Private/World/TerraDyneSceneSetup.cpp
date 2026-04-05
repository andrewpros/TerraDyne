// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "World/TerraDyneSceneSetup.h"
#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneWorldPreset.h"
#include "World/TerraDyneChunk.h"
#include "World/TerraDyneOrchestrator.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"

ATerraDyneSceneSetup::ATerraDyneSceneSetup()
{
	PrimaryActorTick.bCanEverTick = false;
	ManagerClass = ATerraDyneManager::StaticClass();
}

void ATerraDyneSceneSetup::InitializeWorld()
{
#if WITH_EDITOR
	UWorld* World = GetWorld();
	if (!World) return;

	UE_LOG(LogTemp, Log, TEXT("TerraDyne Wizard: Starting One-Click Showcase Setup..."));

	// 1. Clean old Managers/Orchestrators
	TArray<AActor*> OldActors;
	UGameplayStatics::GetAllActorsOfClass(World, ATerraDyneManager::StaticClass(), OldActors);
	UGameplayStatics::GetAllActorsOfClass(World, ATerraDyneOrchestrator::StaticClass(), OldActors);
	for (AActor* Act : OldActors) Act->Destroy();

	// 2. Spawn Manager at Origin
	FActorSpawnParameters Params;
	UClass* ClassToSpawn = ManagerClass.Get() ? ManagerClass.Get() : ATerraDyneManager::StaticClass();

	ATerraDyneManager* Manager = World->SpawnActor<ATerraDyneManager>(
		ClassToSpawn,
		FVector::ZeroVector, FRotator::ZeroRotator, Params
	);

	if (Manager)
	{
		Manager->SetActorLabel(TEXT("TerraDyne_System_Manager"));

		Manager->ActiveLayer = ETerraDyneLayer::Sculpt;
		Manager->GlobalChunkSize = 10000.0f;
		Manager->WorldPreset = WorldPreset;
		if (WorldPreset)
		{
			Manager->ApplyWorldPreset(WorldPreset);
		}

		switch (DemoTemplate)
		{
		case ETerraDyneDemoTemplate::FullFeatureShowcase:
			Manager->bSpawnDefaultChunksOnBeginPlay = false;
			Manager->bSetupDefaultLightingOnBeginPlay = true;
			Manager->bSpawnShowcaseOnBeginPlay = false;
			break;

		case ETerraDyneDemoTemplate::SurvivalFramework:
			Manager->bSpawnDefaultChunksOnBeginPlay = true;
			Manager->bSetupDefaultLightingOnBeginPlay = true;
			Manager->bSpawnShowcaseOnBeginPlay = false;
			break;

		case ETerraDyneDemoTemplate::AuthoredWorldConversion:
			Manager->bSpawnDefaultChunksOnBeginPlay = false;
			Manager->bSetupDefaultLightingOnBeginPlay = true;
			Manager->bSpawnShowcaseOnBeginPlay = false;
			break;

		case ETerraDyneDemoTemplate::Sandbox:
		default:
			Manager->bSpawnDefaultChunksOnBeginPlay = true;
			Manager->bSetupDefaultLightingOnBeginPlay = false;
			Manager->bSpawnShowcaseOnBeginPlay = false;
			break;
		}

		Manager->RebuildChunkMap();
	}

	// 3. Spawn Orchestrator (The Showcase Director)
	ATerraDyneOrchestrator* Director = World->SpawnActor<ATerraDyneOrchestrator>(
		ATerraDyneOrchestrator::StaticClass(),
		FVector(0,0,1000), FRotator::ZeroRotator
	);
	if (Director)
	{
		Director->SetActorLabel(TEXT("TerraDyne_Showcase_Director"));
	}

	// 4. Setup Spectacular Lighting
	if (!UGameplayStatics::GetActorOfClass(World, ADirectionalLight::StaticClass()))
	{
		ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FVector(0,0,5000), FRotator(-45, -45, 0));
		if (Sun && Sun->GetLightComponent())
		{
			Sun->SetActorLabel(TEXT("TerraDyne_Sun"));
			Sun->GetLightComponent()->SetIntensity(3.0f);
			Sun->SetCastShadows(true);
		}
	}

	if (!UGameplayStatics::GetActorOfClass(World, ASkyLight::StaticClass()))
	{
		ASkyLight* Sky = World->SpawnActor<ASkyLight>(ASkyLight::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		if (Sky && Sky->GetLightComponent())
		{
			Sky->SetActorLabel(TEXT("TerraDyne_Sky"));
			Sky->GetLightComponent()->bRealTimeCapture = true;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("SHOWCASE READY: Just press PLAY to watch the feature tour."));
#endif
}
