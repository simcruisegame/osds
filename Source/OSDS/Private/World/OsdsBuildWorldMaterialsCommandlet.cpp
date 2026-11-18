// Copyright OSDS contributors. All Rights Reserved.

#include "World/OsdsBuildWorldMaterialsCommandlet.h"

#include "Materials/Material.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "World/OsdsBridges.h"
#include "World/OsdsGuardRails.h"
#include "World/OsdsRegionalTerrain.h"
#include "World/OsdsSignals.h"
#include "World/OsdsTrees.h"
#include "World/OsdsWorldResources.h"
#include "World/OsdsCarlaCatalog.h"
#include "World/OsdsCookList.h"
#include "Engine/StaticMesh.h"
#include "Presentation/OsdsCarClock.h"
#include "Presentation/OsdsCarMaterialLibrary.h"
#include "Presentation/OsdsCarMirror.h"
#include "Presentation/OsdsLowBeamPattern.h"

DEFINE_LOG_CATEGORY_STATIC(LogOsdsMaterials, Log, All);

UOsdsBuildWorldMaterialsCommandlet::UOsdsBuildWorldMaterialsCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UOsdsBuildWorldMaterialsCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	int32 Failures = 0;
	// A fresh copy of each: the old files go first (re-saving over a package the engine has
	// only partly loaded is refused).
	for (const TCHAR* Dir : { TEXT("World/Materials"), TEXT("Vehicles/Materials"), TEXT("Vehicle/Materials") })
	{
		TArray<FString> Old;
		const FString Folder = FPaths::Combine(FPaths::ProjectContentDir(), Dir);
		IFileManager::Get().FindFiles(Old, *FPaths::Combine(Folder, TEXT("*.uasset")), true, false);
		for (const FString& File : Old) IFileManager::Get().Delete(*FPaths::Combine(Folder, File));
	}
	for (int32 i = 0; i < static_cast<int32>(EOsdsTileLayer::Count); ++i)
	{
		const EOsdsTileLayer Layer = static_cast<EOsdsTileLayer>(i);
		const FString Name = TEXT("M_OSDS") + OsdsWorldMaterials::LayerName(Layer);
		const FString PackagePath = TEXT("/Game/World/Materials/") + Name;
		UPackage* Package = CreatePackage(*PackagePath);
		Package->FullyLoad();
		UMaterial* Material = OsdsWorldMaterials::BuildMaterial(Package, FName(*Name), Layer, RF_Public | RF_Standalone);
		Package->MarkPackageDirty();
		const FString File = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Package, Material, *File, Args))
		{
			UE_LOG(LogOsdsMaterials, Error, TEXT("Could not save %s"), *File);
			++Failures;
			continue;
		}
		UE_LOG(LogOsdsMaterials, Display, TEXT("Saved %s"), *File);
	}

	// The CARLA models the game places, as one list of hard references (cooked with this folder).
	{
		const FString PackagePath = TEXT("/Game/World/Materials/DA_OsdsCarlaCookList");
		UPackage* Package = CreatePackage(*PackagePath);
		UOsdsCookList* List = NewObject<UOsdsCookList>(Package, TEXT("DA_OsdsCarlaCookList"), RF_Public | RF_Standalone);
		TArray<FString> Paths;
		for (const FOsdsCarlaEntry& E : OsdsCarlaCatalog::Entries()) Paths.Add(E.Path);
		Paths.Append(AOsdsSignals::CarlaMeshPaths());
		Paths.Add(TEXT("/Game/Carla/Static/GenericMaterials/Facade/Textures/T_Brick_02_d.T_Brick_02_d"));
		Paths.Add(TEXT("/Game/Carla/Static/GenericMaterials/Facade/Textures/T_Facade05_d.T_Facade05_d"));
		for (const TCHAR* Decal : { TEXT("/Game/Carla/Static/FX/Decals/Road/Cracks/Textures/T_cracks01_msk.T_cracks01_msk"),
			TEXT("/Game/Carla/Static/FX/Decals/Road/TarSnakes/Textures/T_tarsnakeBig01_msk.T_tarsnakeBig01_msk"),
			TEXT("/Game/Carla/Static/FX/Decals/Road/RoadDirt/Textures/T_DecRoad01_msk.T_DecRoad01_msk"),
			TEXT("/Game/Carla/Static/FX/Decals/Road/Cracks/Textures/T_DecRodCrack02_msk.T_DecRodCrack02_msk") }) Paths.Add(Decal);
		// Engine assets loaded by name at run time (the streamer's horizon ground).
		Paths.Add(TEXT("/Engine/BasicShapes/Plane.Plane"));
		Paths.Add(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		for (const FString& Path : Paths)
		{
			if (UObject* Asset = LoadObject<UObject>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet)) List->Assets.Add(Asset);
		}
		Package->MarkPackageDirty();
		const FString File = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (UPackage::SavePackage(Package, List, *File, Args))
		{
			UE_LOG(LogOsdsMaterials, Display, TEXT("Saved %s (%d CARLA assets of %d)"), *File, List->Assets.Num(), Paths.Num());
		}
		else
		{
			UE_LOG(LogOsdsMaterials, Error, TEXT("Could not save %s"), *File);
			++Failures;
		}
	}

	// The car's: its three masters, the dashboard clock, the mirror glass and the low-beam
	// light function (each built from the code, then saved as a copy).
	FString Report;
	if (!OsdsCarMaterials::SaveMasters(Report)) ++Failures;
	UE_LOG(LogOsdsMaterials, Display, TEXT("%s"), *Report);
	auto SaveCopy = [&](UMaterialInterface* Built, const FString& PackagePath)
	{
		UMaterial* Source = Built ? Built->GetMaterial() : nullptr;
		if (!Source)
		{
			UE_LOG(LogOsdsMaterials, Error, TEXT("Could not build %s"), *PackagePath);
			++Failures;
			return;
		}
		const FString Name = FPackageName::GetLongPackageAssetName(PackagePath);
		UPackage* Package = CreatePackage(*PackagePath);
		UMaterial* Saved = DuplicateObject<UMaterial>(Source, Package, *Name);
		OsdsCarMaterials::PrepareSavedCopy(Saved);
		Saved->PostEditChange();
		Package->MarkPackageDirty();
		const FString File = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (UPackage::SavePackage(Package, Saved, *File, Args))
		{
			UE_LOG(LogOsdsMaterials, Display, TEXT("Saved %s"), *File);
		}
		else
		{
			UE_LOG(LogOsdsMaterials, Error, TEXT("Could not save %s"), *File);
			++Failures;
		}
	};
	SaveCopy(OsdsCarClock::Get(), TEXT("/Game/Vehicle/Materials/M_OsdsCarClock"));
	SaveCopy(OsdsCarScreen::Get(), TEXT("/Game/Vehicle/Materials/M_OsdsCarScreen"));
	SaveCopy(OsdsCarMirror::Get(), TEXT("/Game/Vehicle/Materials/M_OsdsCarMirror"));
	SaveCopy(OsdsLowBeamPattern::Get(), TEXT("/Game/Vehicle/Materials/LF_OsdsLowBeam"));
	// The land to the horizon (World/OsdsRegionalTerrain).
	SaveCopy(AOsdsRegionalTerrain::BuildMaterial(GetTransientPackage(), TEXT("M_OsdsLandscapeBuild"), RF_Transient),
		FPackageName::ObjectPathToPackageName(AOsdsRegionalTerrain::MaterialPath()));
	// Trees anywhere (World/OsdsTrees).
	SaveCopy(AOsdsTrees::BuildLeafMaterial(GetTransientPackage(), TEXT("M_OsdsTreeLeafBuild"), RF_Transient),
		FPackageName::ObjectPathToPackageName(AOsdsTrees::LeafMaterialPath()));
	SaveCopy(AOsdsTrees::BuildBarkMaterial(GetTransientPackage(), TEXT("M_OsdsTreeBarkBuild"), RF_Transient),
		FPackageName::ObjectPathToPackageName(AOsdsTrees::BarkMaterialPath()));
	// Guard rails (World/OsdsGuardRails).
	SaveCopy(AOsdsGuardRails::BuildMaterial(GetTransientPackage(), TEXT("M_OsdsGuardRailBuild"), RF_Transient),
		FPackageName::ObjectPathToPackageName(AOsdsGuardRails::MaterialPath()));
	// Traffic signals (World/OsdsSignals).
	SaveCopy(AOsdsSignals::BuildMaterial(GetTransientPackage(), TEXT("M_OsdsSignalBuild"), RF_Transient),
		FPackageName::ObjectPathToPackageName(AOsdsSignals::MaterialPath()));
	// Bridge decks (World/OsdsBridges).
	SaveCopy(AOsdsBridges::BuildMaterial(GetTransientPackage(), TEXT("M_OsdsBridgeConcreteBuild"), RF_Transient),
		FPackageName::ObjectPathToPackageName(AOsdsBridges::MaterialPath()));
	return Failures == 0 ? 0 : 1;
#else
	return 1;
#endif
}
