// Copyright OSDS contributors. All Rights Reserved.

#include "Presentation/OsdsCarAssets.h"
#include "Async/Async.h"

#include "Engine/StaticMesh.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/Material.h"
#include "MeshDescription.h"
#include "Misc/Paths.h"
#include "Presentation/OsdsCarMaterialLibrary.h"
#include "Presentation/OsdsCarModelRig.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "UObject/Package.h"
#include "Vehicle/CarDefinition.h"
#include "Vehicle/CarDefinitionLoader.h"

#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogOsdsCarAssets, Log, All);

namespace OsdsCarAssetsPrivate
{
	TMap<FString, TSharedPtr<FOsdsBuiltCar>> Cache;

	UStaticMesh* BuildMesh(const FString& Name, const TArray<const FOsdsRigSection*>& Sections, bool bNanite, bool bFullPrecisionUVs = false);
	void BuildPart(FOsdsBuiltCar& Car, const FOsdsRigPart& Part, FOsdsBuiltPart& Out);
}

UStaticMesh* OsdsCarAssets::BuildStaticMesh(const FString& Name, const TArray<const FOsdsRigSection*>& Sections, bool bNanite, bool bFullPrecisionUVs)
{
	return OsdsCarAssetsPrivate::BuildMesh(Name, Sections, bNanite, bFullPrecisionUVs);
}

namespace OsdsCarAssetsPrivate
{
	UStaticMesh* BuildMesh(const FString& Name, const TArray<const FOsdsRigSection*>& Sections, bool bNanite, bool bFullPrecisionUVs)
	{
		FMeshDescription Description;
		FStaticMeshAttributes Attributes(Description);
		Attributes.Register();
		TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector3f> Tangents = Attributes.GetVertexInstanceTangents();
		TVertexInstanceAttributesRef<float> Signs = Attributes.GetVertexInstanceBinormalSigns();
		TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
		UVs.SetNumChannels(1);
		TPolygonGroupAttributesRef<FName> SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();

		int32 VertexCount = 0, TriangleCount = 0;
		bool bAnyTangents = false;
		for (const FOsdsRigSection* Section : Sections)
		{
			VertexCount += Section->Positions.Num();
			TriangleCount += Section->NumTriangles();
			bAnyTangents |= Section->Tangents.Num() == Section->Positions.Num() && Section->Positions.Num() > 0;
		}
		Description.ReserveNewVertices(VertexCount);
		Description.ReserveNewVertexInstances(VertexCount);
		Description.ReserveNewTriangles(TriangleCount);
		Description.ReserveNewPolygonGroups(Sections.Num());

		// Unique: a rebuilt car (after ClearCache) must not replace meshes still drawn by older parts.
		UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage(),
			StaticFindObjectFast(nullptr, GetTransientPackage(), FName(*Name)) ? MakeUniqueObjectName(GetTransientPackage(), UStaticMesh::StaticClass(), FName(*Name)) : FName(*Name), RF_Transient);
		TArray<FVertexInstanceID> Instances;
		for (int32 S = 0; S < Sections.Num(); ++S)
		{
			const FOsdsRigSection& Section = *Sections[S];
			const FPolygonGroupID Group = Description.CreatePolygonGroup();
			const FName Slot(*FString::Printf(TEXT("Slot%02d"), S));
			SlotNames[Group] = Slot;
			Mesh->GetStaticMaterials().Add_GetRef(FStaticMaterial(UMaterial::GetDefaultMaterial(MD_Surface), Slot)).UVChannelData = FMeshUVChannelInfo(1.f);
			Instances.SetNumUninitialized(Section.Positions.Num());
			const bool bTangents = Section.Tangents.Num() == Section.Positions.Num();
			for (int32 V = 0; V < Section.Positions.Num(); ++V)
			{
				const FVertexID Vertex = Description.CreateVertex();
				Positions[Vertex] = Section.Positions[V];
				const FVertexInstanceID Instance = Description.CreateVertexInstance(Vertex);
				Normals[Instance] = Section.Normals.IsValidIndex(V) ? Section.Normals[V] : FVector3f::UpVector;
				if (bTangents)
				{
					const FVector4f& T = Section.Tangents[V];
					Tangents[Instance] = FVector3f(T.X, T.Y, T.Z);
					Signs[Instance] = T.W >= 0.f ? 1.f : -1.f;
				}
				UVs.Set(Instance, 0, Section.UV0.IsValidIndex(V) ? Section.UV0[V] : FVector2f::ZeroVector);
				Instances[V] = Instance;
			}
			for (int32 I = 0; I + 2 < Section.Indices.Num(); I += 3)
			{
				const uint32 A = Section.Indices[I], B = Section.Indices[I + 1], C = Section.Indices[I + 2];
				if (A == B || B == C || A == C) continue;
				Description.CreateTriangle(Group, {Instances[A], Instances[B], Instances[C]});
			}
		}

		UStaticMesh::FBuildMeshDescriptionsParams Params;
		Params.bMarkPackageDirty = false;
		Params.bUseHashAsGuid = true;
		Params.bBuildSimpleCollision = false;
		Params.bCommitMeshDescription = true;
		Params.bAllowCpuAccess = false;
#if WITH_EDITOR
		// Full build: author normals kept, MikkTSpace tangents, Nanite for big parts.
		Params.bFastBuild = false;
		Mesh->SetNumSourceModels(1);
		FMeshBuildSettings& Build = Mesh->GetSourceModel(0).BuildSettings;
		Build.bRecomputeNormals = false;
		Build.bRecomputeTangents = !bAnyTangents;
		Build.bUseMikkTSpace = true;
		Build.bGenerateLightmapUVs = false;
		Build.bRemoveDegenerates = true;
		Build.bUseFullPrecisionUVs = bFullPrecisionUVs;
		Build.bUseHighPrecisionTangentBasis = false;
		FMeshNaniteSettings Nanite = Mesh->GetNaniteSettings();
		Nanite.bEnabled = bNanite;
		// Keep the car's fine curvature in the fallback mesh too (ray tracing, reflections).
		Nanite.FallbackPercentTriangles = 1.f;
		Mesh->SetNaniteSettings(Nanite);
#else
		Params.bFastBuild = true;
		FStaticMeshOperations::ComputeTriangleTangentsAndNormals(Description);
		FStaticMeshOperations::ComputeTangentsAndNormals(Description, EComputeNTBsFlags::Tangents | EComputeNTBsFlags::WeightedNTBs);
#endif
		Mesh->bSupportRayTracing = true;
		TArray<const FMeshDescription*> Descriptions = {&Description};
		if (!Mesh->BuildFromMeshDescriptions(Descriptions, Params))
		{
			UE_LOG(LogOsdsCarAssets, Error, TEXT("Mesh build failed for %s"), *Name);
			return nullptr;
		}
#if WITH_EDITOR
		FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif
		return Mesh;
	}

	void BuildPart(FOsdsBuiltCar& Car, const FOsdsRigPart& Part, FOsdsBuiltPart& Out)
	{
		Out.Name = Part.Name;
		Out.PartToVehicle = Part.PartToVehicle;
		Out.Triangles = Part.NumTriangles();
		TArray<const FOsdsRigSection*> Solid, Clear;
		for (const FOsdsRigSection& Section : Part.Sections)
		{
			if (Section.NumTriangles() == 0) continue;
			const FOsdsGltfMaterial* Author = Car.Model.Materials.IsValidIndex(Section.MaterialIndex) ? &Car.Model.Materials[Section.MaterialIndex] : nullptr;
			Car.Roles.Add(Section.MaterialIndex, Section.Role);
			if (OsdsCarMaterials::MasterFor(Section.Role, Author) == EOsdsCarMaster::Glass)
			{
				Clear.Add(&Section);
				Out.GlassMaterials.Add(Section.MaterialIndex);
			}
			else
			{
				Solid.Add(&Section);
				Out.MeshMaterials.Add(Section.MaterialIndex);
			}
		}
		int32 SolidTriangles = 0;
		for (const FOsdsRigSection* Section : Solid) SolidTriangles += Section->NumTriangles();
		const FString Base = FString::Printf(TEXT("SM_%s_%s"), *Car.CarId, *Part.Name.ToString());
		if (Solid.Num() > 0)
		{
			Out.bNanite = SolidTriangles >= OsdsCarAssets::NaniteMinTriangles;
			Out.Mesh.Reset(BuildMesh(Base, Solid, Out.bNanite));
		}
		if (Clear.Num() > 0)
		{
			Out.Glass.Reset(BuildMesh(Base + TEXT("_Glass"), Clear, false));
		}
	}
}

namespace OsdsCarAssetsPrivate
{
	/** The CPU part of a build (profile, car data, glTF, rig): no UObjects, safe on a worker thread. */
	struct FPrepared
	{
		TSharedPtr<FOsdsBuiltCar> Car;
		FOsdsRiggedCar Rigged;
		FOsdsRigInputs Inputs;
		double Start = 0.0;
		double PrepareSeconds = 0.0;
		bool bOk = false;
		FString Error;
	};

	TSharedPtr<FPrepared> PrepareCar(const FString& CarId)
	{
		TSharedPtr<FPrepared> Out = MakeShared<FPrepared>();
		FString OutError;
		FOsdsRigInputs& Inputs = Out->Inputs;
		FOsdsRiggedCar& Rigged = Out->Rigged;
		const double Start = FPlatformTime::Seconds();
		Out->Start = Start;
		TSharedPtr<FOsdsBuiltCar> Car = MakeShared<FOsdsBuiltCar>();
		Out->Car = Car;
		Car->CarId = CarId;
		if (!OsdsPresentationProfiles::Load(CarId, Car->Profile, OutError))
		{
			{ Out->Error = OutError; return Out; }
		}
		// A driven car is fitted to its driving data; a car that only parks, to its profile's dimensions.
		if (const TOptional<FOsdsCarPresentationProfile::FParkedDimensions>& Parked = Car->Profile.Parked; Parked.IsSet())
		{
			Inputs.WheelbaseM = Parked->WheelbaseM;
			Inputs.FrontTrackM = Parked->FrontTrackM;
			Inputs.RearTrackM = Parked->RearTrackM;
			Inputs.TireRadiusM = Parked->TyreRadiusM;
			Inputs.TireWidthM = Parked->TyreWidthM > 0.0 ? Parked->TyreWidthM : Inputs.TireWidthM;
			Car->SteeringRatio = Car->Profile.SteeringRatio;
		}
		else
		{
			FOsdsCarDefinition Definition;
			if (!FOsdsCarDefinitionLoader::LoadById(CarId, Definition, OutError))
			{
				{ Out->Error = OutError; return Out; }
			}
			Inputs = FOsdsRigInputs::FromCar(Definition);
		}
		const FString ModelPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), Car->Profile.ModelPath));
		if (!OsdsGltf::Load(ModelPath, Car->Model, OutError))
		{
			{ Out->Error = OutError; return Out; }
		}
		FString RigError;
		const bool bRigged = OsdsCarModelRig::Build(Car->Model, Car->Profile, Inputs, Rigged, RigError);
		Car->Log = Rigged.Log;
		if (bRigged && Car->Profile.WheelDesign.IsSet())
		{
			// The profile's wheel design replaces the model's rims and tyres; discs and calipers stay.
			// Its own untextured materials: a bright silver-painted face, a polished lip, a dark cap.
			auto AddMaterial = [&Car](const TCHAR* Name, const FLinearColor& Base, float Metallic, float Roughness)
			{
				FOsdsGltfMaterial M;
				M.Name = Name;
				M.BaseColorFactor = Base;
				M.MetallicFactor = Metallic;
				M.RoughnessFactor = Roughness;
				return Car->Model.Materials.Add(M);
			};
			OsdsWheelBuilder::FMaterials Materials;
			Materials.Face = AddMaterial(TEXT("osds_wheel_face"), FLinearColor(0.52f, 0.53f, 0.55f), 1.f, 0.38f);
			Materials.Lip = AddMaterial(TEXT("osds_wheel_lip"), FLinearColor(0.91f, 0.91f, 0.92f), 1.f, 0.1f);
			Materials.Cap = AddMaterial(TEXT("osds_wheel_cap"), FLinearColor(0.02f, 0.02f, 0.022f), 0.f, 0.45f);
			// Plain rubber: the model's tyre material carries its own texture and sidewall detail, laid out for its UVs.
			Materials.Tyre = AddMaterial(TEXT("osds_wheel_tyre"), FLinearColor(0.025f, 0.025f, 0.026f), 0.f, 0.85f);
			for (int32 W = 0; W < 4; ++W)
			{
				OsdsWheelBuilder::FMaterials Own = Materials;
				TArray<FOsdsRigSection> Kept;
				for (FOsdsRigSection& Section : Rigged.Wheels[W].Sections)
				{
					if (Section.Role == EOsdsCarMaterialRole::BrakeDisc || Section.Role == EOsdsCarMaterialRole::BrakeCaliper) Kept.Add(MoveTemp(Section));
				}
				OsdsWheelBuilder::BuildMeshWheel(Car->Profile.WheelDesign, Inputs.TireRadiusM * 100.0, Inputs.TireWidthM * 100.0,
					(W == 0 || W == 2) ? -1.0 : 1.0, Own, Kept);
				Rigged.Wheels[W].Sections = MoveTemp(Kept);
			}
			Rigged.Log.Add(FString::Printf(TEXT("wheel design '%s': %.0fx%.1f in, %.1f in lip, %d spokes (%s)"), *Car->Profile.WheelDesign.Style,
				Car->Profile.WheelDesign.RimDiameterIn, Car->Profile.WheelDesign.RimWidthIn, Car->Profile.WheelDesign.LipIn, Car->Profile.WheelDesign.Spokes,
				*Car->Profile.WheelDesign.Source));
			Car->Log = Rigged.Log;
		}
		if (!bRigged)
		{
			OutError = FString::Printf(TEXT("%s: could not fit %s: %s"), *CarId, *ModelPath, *RigError);
			{ Out->Error = OutError; return Out; }
		}
		// Role areas split some author materials: their copies follow the model's own.
		for (const FOsdsRiggedCar::FExtraMaterial& Extra : Rigged.ExtraMaterials)
		{
			FOsdsGltfMaterial Copy = Car->Model.Materials.IsValidIndex(Extra.Source) ? Car->Model.Materials[Extra.Source] : FOsdsGltfMaterial();
			Copy.Name += FString(TEXT("#")) + OsdsCarMaterialRoles::ToString(Extra.Role);
			Car->Model.Materials.Add(Copy);
		}
		// Geometry now lives in the rig parts; keep only materials and images.
		Car->Model.Primitives.Empty();
		Out->PrepareSeconds = FPlatformTime::Seconds() - Start;
		Out->bOk = true;
		return Out;
	}

	/** Builds started on a worker thread (PrepareAsync), by car. Game thread only. */
	TMap<FString, TFuture<TSharedPtr<FPrepared>>> Pending;
}

TSharedPtr<FOsdsBuiltCar> OsdsCarAssets::Get(const FString& CarId, FString& OutError)
{
	using namespace OsdsCarAssetsPrivate;
	if (TSharedPtr<FOsdsBuiltCar>* Found = Cache.Find(CarId))
	{
		return *Found;
	}
	// The CPU part from a worker thread when PrepareAsync started it (it waits for it if still
	// running), else here; then the meshes, which need the game thread.
	TSharedPtr<FPrepared> Prepared;
	if (TFuture<TSharedPtr<FPrepared>>* Running = Pending.Find(CarId))
	{
		const double WaitStart = FPlatformTime::Seconds();
		Prepared = Running->Get();
		Pending.Remove(CarId);
		UE_LOG(LogOsdsCarAssets, Display, TEXT("%s: prepared on a worker thread in %.1f s (waited %.1f s for it)"), *CarId, Prepared ? Prepared->PrepareSeconds : 0.0, FPlatformTime::Seconds() - WaitStart);
	}
	else
	{
		Prepared = PrepareCar(CarId);
	}
	if (!Prepared || !Prepared->bOk)
	{
		OutError = Prepared ? Prepared->Error : FString(TEXT("no build"));
		return nullptr;
	}
	const double Start = FPlatformTime::Seconds() - Prepared->PrepareSeconds;
	const double FinishStart = FPlatformTime::Seconds();
	TSharedPtr<FOsdsBuiltCar> Car = Prepared->Car;
	FOsdsRiggedCar& Rigged = Prepared->Rigged;
	const FOsdsRigInputs& Inputs = Prepared->Inputs;

	BuildPart(*Car, Rigged.Body, Car->Body);
	for (int32 W = 0; W < 4; ++W)
	{
		BuildPart(*Car, Rigged.Wheels[W], Car->Wheels[W]);
		BuildPart(*Car, Rigged.Uprights[W], Car->Uprights[W]);
		Car->HubsCm[W] = Rigged.HubsCm[W];
	}
	Car->bHasSteeringWheel = Rigged.bHasSteeringWheel;
	if (Rigged.bHasSteeringWheel)
	{
		BuildPart(*Car, Rigged.SteeringWheel, Car->SteeringWheel);
	}
	Car->DriverEyeCm = Inputs.DriverEyeM * 100.0;
	Car->TyreRadiusCm = Inputs.TireRadiusM * 100.0;
	Car->HeadLampsCm = OsdsCarModelRig::HeadLampCentresCm(Rigged);
	Car->MirrorsCm = OsdsCarModelRig::MirrorCentresCm(Rigged);
	Car->Dials = Rigged.Dials;
	// Each dial's face: a disc in its own frame (X to the dial's 12 o'clock, Z toward the driver),
	// UVs across it for the live face drawn into a render target. Both windings: seen from either side.
	for (int32 D = 0; D < Car->Dials.Num(); ++D)
	{
		const FOsdsRiggedCar::FDial& Dial = Car->Dials[D];
		FOsdsRigPart Part;
		Part.Name = FName(*FString::Printf(TEXT("dial_%d"), D));
		const FVector X = Dial.Up, Z = Dial.Normal, Y = Z ^ X;
		Part.PartToVehicle = FTransform(X, Y, Z, Dial.CentreCm);
		FOsdsRigSection& Section = Part.Sections.AddDefaulted_GetRef();
		Section.MaterialIndex = OsdsCarAssets::DialMaterialIndex(D);
		Section.Role = EOsdsCarMaterialRole::Gauge;
		constexpr int32 Segments = 48;
		const float R = static_cast<float>(Dial.RadiusCm);
		const FOsdsDialSpec* Spec = Car->Profile.Dials.IsValidIndex(Dial.Spec) ? &Car->Profile.Dials[Dial.Spec] : nullptr;
		const bool bLinear = Spec && Spec->IsLinear();
		// A straight scale's face: a strip its length plus a margin, R high either side of the scale.
		const float HalfWide = bLinear ? static_cast<float>(Spec->LengthM * 50.0) + 0.6f * R : R;
		if (bLinear)
		{
			const FVector2f Corners[4] = { {R, -HalfWide}, {R, HalfWide}, {-R, HalfWide}, {-R, -HalfWide} };
			for (const FVector2f& Corner : Corners)
			{
				Section.Positions.Add(FVector3f(Corner.X, Corner.Y, 0.f));
				Section.Normals.Add(FVector3f(0.f, 0.f, 1.f));
				Section.UV0.Add(FVector2f(0.5f + 0.5f * Corner.Y / HalfWide, 0.5f - 0.5f * Corner.X / R));
			}
			// One side only: a second, reversed copy at the same depth drew its edges through the face.
			Section.Indices.Append({0u, 1u, 2u, 0u, 2u, 3u});
		}
		else
		{
			Section.Positions.Add(FVector3f::ZeroVector);
			Section.Normals.Add(FVector3f(0.f, 0.f, 1.f));
			Section.UV0.Add(FVector2f(0.5f, 0.5f));
			for (int32 K = 0; K < Segments; ++K)
			{
				const float A = UE_TWO_PI * K / Segments;
				const float C = FMath::Cos(A), S = FMath::Sin(A);
				Section.Positions.Add(FVector3f(R * C, R * S, 0.f));
				Section.Normals.Add(FVector3f(0.f, 0.f, 1.f));
				// u to the right (+Y), v down (-X): the canvas reads upright on the dial.
				Section.UV0.Add(FVector2f(0.5f + 0.5f * S, 0.5f - 0.5f * C));
			}
			for (int32 K = 0; K < Segments; ++K)
			{
				const uint32 A = 1 + K, B = 1 + (K + 1) % Segments;
				Section.Indices.Append({0u, A, B});
			}
		}
		BuildPart(*Car, Part, Car->DialParts.AddDefaulted_GetRef());

		// The needle: a tapered pointer from a short tail to just inside the ticks, standing 3 mm off
		// the face, and a round hub cap over its root. It turns as a mesh; the face is drawn once.
		FOsdsRigPart Needle;
		Needle.Name = FName(*FString::Printf(TEXT("dial_%d_needle"), D));
		Needle.PartToVehicle = Part.PartToVehicle;
		FOsdsRigSection& Pointer = Needle.Sections.AddDefaulted_GetRef();
		Pointer.MaterialIndex = OsdsCarAssets::NeedleMaterialIndex(D);
		Pointer.Role = EOsdsCarMaterialRole::Gauge;
		const float Lift = 0.3f, Tail = -0.18f * R, Tip = 0.88f * R, RootHalf = FMath::Max(0.045f * R, 0.12f), TipHalf = FMath::Max(0.012f * R, 0.05f);
		auto Vertex = [&Pointer](float X, float Y, float Z, float U, float V)
		{
			Pointer.Positions.Add(FVector3f(X, Y, Z));
			Pointer.Normals.Add(FVector3f(0.f, 0.f, 1.f));
			Pointer.UV0.Add(FVector2f(U, V));
		};
		if (bLinear)
		{
			// A straight scale's pointer: a thin upright bar across the scale, slid along it.
			const float Half = 0.75f * R, Thin = 0.08f;
			Vertex(-Half, -Thin, Lift, 0.f, 0.f);
			Vertex(-Half, Thin, Lift, 1.f, 0.f);
			Vertex(Half, Thin, Lift, 1.f, 1.f);
			Vertex(Half, -Thin, Lift, 0.f, 1.f);
			Pointer.Indices.Append({0u, 1u, 2u, 0u, 2u, 3u, 0u, 2u, 1u, 0u, 3u, 2u});
			BuildPart(*Car, Needle, Car->NeedleParts.AddDefaulted_GetRef());
			Car->Log.Add(FString::Printf(TEXT("dial %d (%s) at %s cm, a straight scale %.1f cm long"), D,
				*Spec->Quantity, *Dial.CentreCm.ToString(), Spec->LengthM * 100.0));
			continue;
		}
		Vertex(Tail, -RootHalf, Lift, 0.f, 0.f);
		Vertex(Tail, RootHalf, Lift, 1.f, 0.f);
		Vertex(Tip, TipHalf, Lift, 1.f, 1.f);
		Vertex(Tip, -TipHalf, Lift, 0.f, 1.f);
		Pointer.Indices.Append({0u, 1u, 2u, 0u, 2u, 3u, 0u, 2u, 1u, 0u, 3u, 2u});
		// The hub cap, a little nearer the driver than the pointer.
		const uint32 Hub = Pointer.Positions.Num();
		const float HubR = FMath::Max(0.09f * R, 0.25f);
		Vertex(0.f, 0.f, Lift + 0.1f, 0.5f, 0.5f);
		constexpr int32 HubSegments = 16;
		for (int32 K = 0; K < HubSegments; ++K)
		{
			const float A = UE_TWO_PI * K / HubSegments;
			Vertex(HubR * FMath::Cos(A), HubR * FMath::Sin(A), Lift + 0.1f, 0.5f, 0.5f);
		}
		for (int32 K = 0; K < HubSegments; ++K)
		{
			const uint32 A = Hub + 1 + K, B = Hub + 1 + (K + 1) % HubSegments;
			Pointer.Indices.Append({Hub, A, B, Hub, B, A});
		}
		BuildPart(*Car, Needle, Car->NeedleParts.AddDefaulted_GetRef());
		Car->Log.Add(FString::Printf(TEXT("dial %d (%s) at %s cm, radius %.1f cm"), D,
			Car->Profile.Dials.IsValidIndex(Dial.Spec) ? *Car->Profile.Dials[Dial.Spec].Quantity : TEXT("?"), *Dial.CentreCm.ToString(), Dial.RadiusCm));
	}
	for (const FVector4& Mirror : Car->MirrorsCm)
	{
		if (Mirror.W > 0.0) Car->Log.Add(FString::Printf(TEXT("mirror glass at %s cm"), *FVector(Mirror).ToString()));
	}
	for (const FVector& Lamp : Car->HeadLampsCm)
	{
		Car->Log.Add(FString::Printf(TEXT("head-lamp beam origin %s cm"), *Lamp.ToString()));
	}
	Car->SteeringRatio = Car->Profile.SteeringRatio;
	Car->BuildSeconds = FPlatformTime::Seconds() - Start;
	Car->Log.Add(FString::Printf(TEXT("built in %.1f s: %.1f s glTF and rig, %.1f s meshes (body %d triangles%s)"), Car->BuildSeconds, Prepared->PrepareSeconds,
		FPlatformTime::Seconds() - FinishStart, Car->Body.Triangles, Car->Body.bNanite ? TEXT(", Nanite") : TEXT("")));
	for (const FString& Line : Car->Log)
	{
		UE_LOG(LogOsdsCarAssets, Display, TEXT("%s: %s"), *CarId, *Line);
	}
	Cache.Add(CarId, Car);
	return Car;
}

void OsdsCarAssets::PrepareAsync(const FString& CarId)
{
	using namespace OsdsCarAssetsPrivate;
	if (CarId.IsEmpty() || Cache.Contains(CarId) || Pending.Contains(CarId)) return;
	Pending.Add(CarId, Async(EAsyncExecution::ThreadPool, [CarId]() { return PrepareCar(CarId); }));
}

void OsdsCarAssets::ClearCache()
{
	OsdsCarAssetsPrivate::Cache.Reset();
}

bool OsdsCarAssets::IsReadyToGet(const FString& CarId)
{
	using namespace OsdsCarAssetsPrivate;
	if (CarId.IsEmpty() || Cache.Contains(CarId)) return true;
	if (const TFuture<TSharedPtr<FPrepared>>* Running = Pending.Find(CarId))
	{
		return Running->IsReady();
	}
	PrepareAsync(CarId);
	return false;
}

bool OsdsCarAssets::IsBuilt(const FString& CarId)
{
	return OsdsCarAssetsPrivate::Cache.Contains(CarId);
}

int64 OsdsCarAssets::ModelBytes(const FString& CarId)
{
	static TMap<FString, int64> Known;
	if (const int64* Found = Known.Find(CarId)) return *Found;
	int64 Bytes = 0;
	FOsdsCarPresentationProfile Profile;
	FString Error;
	if (OsdsPresentationProfiles::Load(CarId, Profile, Error) && !Profile.ModelPath.IsEmpty())
	{
		const FString Model = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), Profile.ModelPath));
		IFileManager& Files = IFileManager::Get();
		if (FPaths::GetExtension(Model).Equals(TEXT("glb"), ESearchCase::IgnoreCase))
		{
			Bytes = FMath::Max<int64>(Files.FileSize(*Model), 0);
		}
		else
		{
			// A .gltf scene: its geometry lies in the .bin buffers beside it.
			TArray<FString> Buffers;
			Files.FindFilesRecursive(Buffers, *FPaths::GetPath(Model), TEXT("*.bin"), true, false);
			for (const FString& Buffer : Buffers) Bytes += FMath::Max<int64>(Files.FileSize(*Buffer), 0);
		}
	}
	Known.Add(CarId, Bytes);
	return Bytes;
}
