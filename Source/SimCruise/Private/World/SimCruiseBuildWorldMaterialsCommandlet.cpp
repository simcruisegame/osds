// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseBuildWorldMaterialsCommandlet.h"

#include "Materials/Material.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "World/SimCruiseBridges.h"
#include "World/SimCruiseGuardRails.h"
#include "World/SimCruiseRegionalTerrain.h"
#include "World/SimCruiseSignals.h"
#include "World/SimCruiseTrees.h"
#include "World/SimCruiseWorldResources.h"
#include "World/SimCruiseCarlaCatalog.h"
#include "World/SimCruiseCookList.h"
#include "Engine/StaticMesh.h"
#include "Presentation/SimCruiseCarClock.h"
#include "Presentation/SimCruiseCarMaterialLibrary.h"
#include "Presentation/SimCruiseCarMirror.h"
#include "Presentation/SimCruiseLowBeamPattern.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseMaterials, Log, All);

USimCruiseBuildWorldMaterialsCommandlet::USimCruiseBuildWorldMaterialsCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 USimCruiseBuildWorldMaterialsCommandlet::Main(const FString& Params)
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
	for (int32 i = 0; i < static_cast<int32>(ESimCruiseTileLayer::Count); ++i)
	{
		const ESimCruiseTileLayer Layer = static_cast<ESimCruiseTileLayer>(i);
		const FString Name = TEXT("M_SimCruise") + SimCruiseWorldMaterials::LayerName(Layer);
		const FString PackagePath = TEXT("/Game/World/Materials/") + Name;
		UPackage* Package = CreatePackage(*PackagePath);
		Package->FullyLoad();
		UMaterial* Material = SimCruiseWorldMaterials::BuildMaterial(Package, FName(*Name), Layer, RF_Public | RF_Standalone);
		Package->MarkPackageDirty();
		const FString File = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Package, Material, *File, Args))
		{
			UE_LOG(LogSimCruiseMaterials, Error, TEXT("Could not save %s"), *File);
			++Failures;
			continue;
		}
		UE_LOG(LogSimCruiseMaterials, Display, TEXT("Saved %s"), *File);
	}

	// The CARLA models the game places, as one list of hard references (cooked with this folder).
	{
		const FString PackagePath = TEXT("/Game/World/Materials/DA_SimCruiseCarlaCookList");
		UPackage* Package = CreatePackage(*PackagePath);
		USimCruiseCookList* List = NewObject<USimCruiseCookList>(Package, TEXT("DA_SimCruiseCarlaCookList"), RF_Public | RF_Standalone);
		TArray<FString> Paths;
		for (const FSimCruiseCarlaEntry& E : SimCruiseCarlaCatalog::Entries()) Paths.Add(E.Path);
		Paths.Append(ASimCruiseSignals::CarlaMeshPaths());
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
			UE_LOG(LogSimCruiseMaterials, Display, TEXT("Saved %s (%d CARLA assets of %d)"), *File, List->Assets.Num(), Paths.Num());
		}
		else
		{
			UE_LOG(LogSimCruiseMaterials, Error, TEXT("Could not save %s"), *File);
			++Failures;
		}
	}

	// The car's: its three masters, the dashboard clock, the mirror glass and the low-beam
	// light function (each built from the code, then saved as a copy).
	FString Report;
	if (!SimCruiseCarMaterials::SaveMasters(Report)) ++Failures;
	UE_LOG(LogSimCruiseMaterials, Display, TEXT("%s"), *Report);
	auto SaveCopy = [&](UMaterialInterface* Built, const FString& PackagePath)
	{
		UMaterial* Source = Built ? Built->GetMaterial() : nullptr;
		if (!Source)
		{
			UE_LOG(LogSimCruiseMaterials, Error, TEXT("Could not build %s"), *PackagePath);
			++Failures;
			return;
		}
		const FString Name = FPackageName::GetLongPackageAssetName(PackagePath);
		UPackage* Package = CreatePackage(*PackagePath);
		UMaterial* Saved = DuplicateObject<UMaterial>(Source, Package, *Name);
		SimCruiseCarMaterials::PrepareSavedCopy(Saved);
		Saved->PostEditChange();
		Package->MarkPackageDirty();
		const FString File = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (UPackage::SavePackage(Package, Saved, *File, Args))
		{
			UE_LOG(LogSimCruiseMaterials, Display, TEXT("Saved %s"), *File);
		}
		else
		{
			UE_LOG(LogSimCruiseMaterials, Error, TEXT("Could not save %s"), *File);
			++Failures;
		}
	};
	SaveCopy(SimCruiseCarClock::Get(), TEXT("/Game/Vehicle/Materials/M_SimCruiseCarClock"));
	SaveCopy(SimCruiseCarScreen::Get(), TEXT("/Game/Vehicle/Materials/M_SimCruiseCarScreen"));
	SaveCopy(SimCruiseCarMirror::Get(), TEXT("/Game/Vehicle/Materials/M_SimCruiseCarMirror"));
	SaveCopy(SimCruiseLowBeamPattern::Get(), TEXT("/Game/Vehicle/Materials/LF_SimCruiseLowBeam"));
	// The land to the horizon (World/SimCruiseRegionalTerrain).
	SaveCopy(ASimCruiseRegionalTerrain::BuildMaterial(GetTransientPackage(), TEXT("M_SimCruiseLandscapeBuild"), RF_Transient),
		FPackageName::ObjectPathToPackageName(ASimCruiseRegionalTerrain::MaterialPath()));
	// Trees anywhere (World/SimCruiseTrees).
	SaveCopy(ASimCruiseTrees::BuildLeafMaterial(GetTransientPackage(), TEXT("M_SimCruiseTreeLeafBuild"), RF_Transient),
		FPackageName::ObjectPathToPackageName(ASimCruiseTrees::LeafMaterialPath()));
	SaveCopy(ASimCruiseTrees::BuildBarkMaterial(GetTransientPackage(), TEXT("M_SimCruiseTreeBarkBuild"), RF_Transient),
		FPackageName::ObjectPathToPackageName(ASimCruiseTrees::BarkMaterialPath()));
	// Guard rails (World/SimCruiseGuardRails).
	SaveCopy(ASimCruiseGuardRails::BuildMaterial(GetTransientPackage(), TEXT("M_SimCruiseGuardRailBuild"), RF_Transient),
		FPackageName::ObjectPathToPackageName(ASimCruiseGuardRails::MaterialPath()));
	// Traffic signals (World/SimCruiseSignals).
	SaveCopy(ASimCruiseSignals::BuildMaterial(GetTransientPackage(), TEXT("M_SimCruiseSignalBuild"), RF_Transient),
		FPackageName::ObjectPathToPackageName(ASimCruiseSignals::MaterialPath()));
	// Bridge decks (World/SimCruiseBridges).
	SaveCopy(ASimCruiseBridges::BuildMaterial(GetTransientPackage(), TEXT("M_SimCruiseBridgeConcreteBuild"), RF_Transient),
		FPackageName::ObjectPathToPackageName(ASimCruiseBridges::MaterialPath()));
	return Failures == 0 ? 0 : 1;
#else
	return 1;
#endif
}
