// Copyright OSDS contributors. All Rights Reserved.

#include "World/OsdsSurfaceKnowledge.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "World/OsdsTileData.h"

namespace
{
	struct FRoadTable
	{
		TMap<FString, float> ClassWear;
		float DefaultWear = 0.5f;
		float WheelCentre = 0.75f, WheelHalf = 0.34f, OilHalf = 0.16f;
		float AgeMin = 0.15f, AgeMax = 0.95f, ScaleNew = 0.62f, ScaleOld = 1.45f, DryBleach = 0.18f;
		TArray<FString> Pool;
		bool bLoaded = false;
	};

	const FRoadTable& Table()
	{
		static const FRoadTable T = []
		{
			FRoadTable R;
			R.Pool = { TEXT("data/art/ground/asphalt.jpg") };
			const TCHAR* Defaults[][2] = { { TEXT("motorway"), TEXT("1.0") }, { TEXT("trunk"), TEXT("1.0") }, { TEXT("primary"), TEXT("0.95") }, { TEXT("secondary"), TEXT("0.8") },
				{ TEXT("tertiary"), TEXT("0.65") }, { TEXT("residential"), TEXT("0.45") }, { TEXT("unclassified"), TEXT("0.45") }, { TEXT("service"), TEXT("0.3") } };
			for (const auto& D : Defaults) R.ClassWear.FindOrAdd(FString(D[0])) = FCString::Atof(D[1]);
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(OsdsTileData::RepositoryRoot(), TEXT("data/knowledge/surface/road.json")))) return R;
			TSharedPtr<FJsonObject> Root;
			if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()) return R;
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (Root->TryGetObjectField(TEXT("class_wear"), Obj))
			{
				const TSharedPtr<FJsonObject>* V = nullptr;
				if ((*Obj)->TryGetObjectField(TEXT("value"), V))
				{
					for (const auto& P : (*V)->Values)
					{
						double X = 0.0;
						if (P.Value->TryGetNumber(X)) { if (P.Key == TEXT("default")) R.DefaultWear = static_cast<float>(X); else R.ClassWear.FindOrAdd(FString(P.Key)) = static_cast<float>(X); }
					}
				}
			}
			double X = 0.0;
			if (Root->TryGetObjectField(TEXT("wheel_path"), Obj))
			{
				if ((*Obj)->TryGetNumberField(TEXT("centre_from_lane_centre_m"), X)) R.WheelCentre = static_cast<float>(X);
				if ((*Obj)->TryGetNumberField(TEXT("half_width_m"), X)) R.WheelHalf = static_cast<float>(X);
			}
			if (Root->TryGetObjectField(TEXT("oil_strip"), Obj) && (*Obj)->TryGetNumberField(TEXT("half_width_m"), X)) R.OilHalf = static_cast<float>(X);
			if (Root->TryGetObjectField(TEXT("age"), Obj))
			{
				const TArray<TSharedPtr<FJsonValue>>* Range = nullptr;
				if ((*Obj)->TryGetArrayField(TEXT("range"), Range) && Range->Num() == 2) { R.AgeMin = static_cast<float>((*Range)[0]->AsNumber()); R.AgeMax = static_cast<float>((*Range)[1]->AsNumber()); }
				if ((*Obj)->TryGetNumberField(TEXT("albedo_multiplier_new"), X)) R.ScaleNew = static_cast<float>(X);
				if ((*Obj)->TryGetNumberField(TEXT("albedo_multiplier_old"), X)) R.ScaleOld = static_cast<float>(X);
			}
			if (Root->TryGetObjectField(TEXT("dry_climate"), Obj) && (*Obj)->TryGetNumberField(TEXT("bleach"), X)) R.DryBleach = static_cast<float>(X);
			if (Root->TryGetObjectField(TEXT("pool"), Obj))
			{
				const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
				if ((*Obj)->TryGetArrayField(TEXT("textures"), List) && List->Num() > 0)
				{
					R.Pool.Reset();
					for (const TSharedPtr<FJsonValue>& V : *List) R.Pool.Add(V->AsString());
				}
			}
			R.bLoaded = true;
			return R;
		}();
		return T;
	}

	/** 32-bit finaliser (Wang): one seed, many independent draws. */
	uint32 Mix(uint32 K)
	{
		K = (K ^ 61u) ^ (K >> 16); K *= 9u; K = K ^ (K >> 4); K *= 0x27d4eb2du; K = K ^ (K >> 15);
		return K;
	}
}

namespace OsdsSurface
{
	float ClassWear(const FString& Class)
	{
		const FRoadTable& T = Table();
		const float* W = T.ClassWear.Find(Class);
		return W ? *W : T.DefaultWear;
	}

	FRoadPlace RoadPlace(uint32 TileSeed, float Dry)
	{
		const FRoadTable& T = Table();
		FRoadPlace Out;
		const float H = static_cast<float>(Mix(TileSeed ^ 0x9e3779b9u) >> 8) / 16777216.f;
		Out.Age = FMath::Lerp(T.AgeMin, T.AgeMax, H);
		const float Dry01 = FMath::Clamp(Dry, 0.f, 1.f);
		Out.AlbedoScale = FMath::Lerp(T.ScaleNew, T.ScaleOld, Out.Age) * (1.f + T.DryBleach * Dry01);
		Out.PoolSeed = static_cast<float>(Mix(TileSeed ^ 0x85ebca6bu) >> 8) / 16777216.f;
		return Out;
	}

	void WheelPathM(float& OutCentre, float& OutHalfWidth, float& OutOilHalfWidth)
	{
		const FRoadTable& T = Table();
		OutCentre = T.WheelCentre; OutHalfWidth = T.WheelHalf; OutOilHalfWidth = T.OilHalf;
	}

	const TArray<FString>& PoolTextures() { return Table().Pool; }
	bool TableLoaded() { return Table().bLoaded; }
}
