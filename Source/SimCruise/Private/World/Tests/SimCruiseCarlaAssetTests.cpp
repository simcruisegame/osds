// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// The CARLA art (CC-BY 4.0, fetched by scripts/fetch-carla.ps1 into Content/Carla): every static mesh must
// load with real materials (no slot falling back to the engine's default material because a parent
// material was not fetched). Writes Saved/CarlaAssets.txt: each mesh, its size and its missing parents.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseCarlaAssetsTest, "SimCruise.World.CarlaAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimCruiseCarlaAssetsTest::RunTest(const FString& Parameters)
{
	if (!FPaths::DirectoryExists(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Carla"))))
	{
		AddInfo(TEXT("Content/Carla is not fetched (scripts/fetch-carla.ps1): nothing to check"));
		return true;
	}
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	Registry.ScanPathsSynchronous({ TEXT("/Game/Carla") }, true);
	TArray<FAssetData> Meshes;
	Registry.GetAssetsByPath(TEXT("/Game/Carla"), Meshes, true);
	Meshes.RemoveAll([](const FAssetData& A) { return A.AssetClassPath != UStaticMesh::StaticClass()->GetClassPathName(); });
	int32 Good = 0, Bad = 0;
	TArray<FString> Lines;
	for (const FAssetData& A : Meshes)
	{
		UStaticMesh* Mesh = Cast<UStaticMesh>(A.GetAsset());
		if (!Mesh) { ++Bad; Lines.Add(FString::Printf(TEXT("FAIL-LOAD %s"), *A.GetObjectPathString())); continue; }
		TArray<FString> Missing;
		for (const FStaticMaterial& Slot : Mesh->GetStaticMaterials())
		{
			UMaterialInterface* M = Slot.MaterialInterface;
			UMaterial* Base = M ? M->GetMaterial() : nullptr;
			if (!M || !Base || Base == UMaterial::GetDefaultMaterial(MD_Surface)) Missing.Add(M ? M->GetPathName() : TEXT("(none)"));
		}
		const FBox Box = Mesh->GetBoundingBox();
		const FVector Size = Box.GetSize() / 100.0;
		Lines.Add(FString::Printf(TEXT("%s %s %.2fx%.2fx%.2f m min(%.2f,%.2f,%.2f) max(%.2f,%.2f,%.2f) %s"), Missing.IsEmpty() ? TEXT("OK  ") : TEXT("MISS"), *A.GetObjectPathString(),
			Size.X, Size.Y, Size.Z, Box.Min.X / 100.0, Box.Min.Y / 100.0, Box.Min.Z / 100.0, Box.Max.X / 100.0, Box.Max.Y / 100.0, Box.Max.Z / 100.0, *FString::Join(Missing, TEXT(", "))));
		(Missing.IsEmpty() ? Good : Bad)++;
	}
	FFileHelper::SaveStringArrayToFile(Lines, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CarlaAssets.txt")));
	AddInfo(FString::Printf(TEXT("CARLA meshes: %d complete, %d with missing materials (Saved/CarlaAssets.txt)"), Good, Bad));
	UE_LOG(LogTemp, Display, TEXT("CARLAASSETS %d complete, %d missing"), Good, Bad);
	return true;
}

#endif
