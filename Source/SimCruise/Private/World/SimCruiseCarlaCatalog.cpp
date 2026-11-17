// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseCarlaCatalog.h"

#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	TArray<FSimCruiseCarlaEntry> LoadCatalogue()
	{
		TArray<FSimCruiseCarlaEntry> Out;
		const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		// No art, no catalogue: the world is built as before.
		if (!IFileManager::Get().DirectoryExists(*FPaths::Combine(Root, TEXT("Content"), TEXT("Carla"))) &&
			!IFileManager::Get().DirectoryExists(*FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Carla"))))
		{
			return Out;
		}
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(Root, TEXT("data"), TEXT("carla"), TEXT("props.json")))) return Out;
		TSharedPtr<FJsonObject> Json;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid()) return Out;
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!Json->TryGetArrayField(TEXT("entries"), Rows)) return Out;
		for (const TSharedPtr<FJsonValue>& V : *Rows)
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			if (!V.IsValid() || !V->TryGetObject(O)) continue;
			FSimCruiseCarlaEntry E;
			(*O)->TryGetStringField(TEXT("key"), E.Key);
			(*O)->TryGetStringField(TEXT("path"), E.Path);
			(*O)->TryGetStringField(TEXT("kind"), E.Kind);
			(*O)->TryGetStringArrayField(TEXT("bands"), E.Bands);
			(*O)->TryGetNumberField(TEXT("cull_m"), E.CullM);
			(*O)->TryGetBoolField(TEXT("shadow"), E.bShadow);
			(*O)->TryGetBoolField(TEXT("collide"), E.bCollide);
			(*O)->TryGetNumberField(TEXT("min_scale"), E.MinScale);
			(*O)->TryGetNumberField(TEXT("max_scale"), E.MaxScale);
			if (!E.Path.IsEmpty() && !E.Kind.IsEmpty()) Out.Add(MoveTemp(E));
		}
		return Out;
	}

	FCriticalSection MeshLock;
	TMap<int32, UStaticMesh*> Meshes;
}

const TArray<FSimCruiseCarlaEntry>& SimCruiseCarlaCatalog::Entries()
{
	static const TArray<FSimCruiseCarlaEntry> Catalogue = LoadCatalogue();
	return Catalogue;
}

TArray<int32> SimCruiseCarlaCatalog::Find(const FString& Kind, const FString& Band)
{
	TArray<int32> Out;
	const TArray<FSimCruiseCarlaEntry>& All = Entries();
	for (int32 i = 0; i < All.Num(); ++i)
	{
		if (All[i].Kind == Kind && (All[i].Bands.IsEmpty() || All[i].Bands.Contains(Band))) Out.Add(i);
	}
	return Out;
}

UStaticMesh* SimCruiseCarlaCatalog::Mesh(int32 Index)
{
	const TArray<FSimCruiseCarlaEntry>& All = Entries();
	if (!All.IsValidIndex(Index)) return nullptr;
	FScopeLock Guard(&MeshLock);
	if (UStaticMesh** Known = Meshes.Find(Index)) return *Known;
	UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *All[Index].Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	// Kept for the session: a handful of meshes shared by every tile.
	if (M) M->AddToRoot();
	Meshes.Add(Index, M);
	return M;
}

FString SimCruiseCarlaCatalog::BandAt(double LatDeg, double LonDeg)
{
	if ((LonDeg < -114.0 && LatDeg < 38.0) || (LonDeg < -103.0 && LonDeg > -122.0 && LatDeg < 40.0)) return TEXT("dry");
	if (LonDeg < -116.0 && LatDeg >= 42.0) return TEXT("wet");
	if ((LatDeg < 28.5 && LonDeg > -83.0) || LonDeg < -150.0) return TEXT("tropic");
	if (LatDeg < 36.5 && LonDeg > -100.0) return TEXT("south");
	if (LonDeg < -103.0) return TEXT("dry");
	return TEXT("north");
}
