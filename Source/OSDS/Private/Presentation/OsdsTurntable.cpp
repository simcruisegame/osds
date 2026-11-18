// Copyright OSDS contributors. All Rights Reserved.

#include "Presentation/OsdsTurntable.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/OsdsEnvironment.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Presentation/OsdsCarAssets.h"
#include "Presentation/OsdsCarMaterialLibrary.h"
#include "Presentation/OsdsCarModelRig.h"
#include "Presentation/OsdsCarPresentationComponent.h"
#include "Presentation/OsdsPresentationProfile.h"
#include "Scalability.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"
#include "Vehicle/CarDefinitionLoader.h"
#include "Engine/Engine.h"

DEFINE_LOG_CATEGORY_STATIC(LogOsdsTurntable, Log, All);

namespace OsdsTurntablePrivate
{
	// Sun as in the Godot taste test (lab/taste): 42 degrees up, 215 degrees from the nose.
	constexpr double SunElevationDeg = 42.0;
	constexpr double SunAzimuthDeg = 215.0;
	// Frames to let temporal AA, Lumen and auto-exposure settle before a shot.
	constexpr int32 SettleFrames = 90;

	FString DefaultOutput()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("Turntable")));
	}

	TArray<FString> ResolveIds(const FString& Arg)
	{
		TArray<FString> Ids;
		if (Arg.IsEmpty() || Arg.Equals(TEXT("all"), ESearchCase::IgnoreCase))
		{
			Ids = OsdsPresentationProfiles::AvailableIds();
		}
		else
		{
			Arg.ParseIntoArray(Ids, TEXT(","), true);
		}
		return Ids;
	}

	void ApplyCaptureQuality(UWorld* World)
	{
		// The owner's standard: captures at the highest settings.
		Scalability::FQualityLevels Levels = Scalability::GetQualityLevels();
		Levels.SetFromSingleQualityLevel(4);
		Scalability::SetQualityLevels(Levels, true);
		auto Set = [](const TCHAR* Name, int32 Value)
		{
			if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
			{
				Var->Set(Value, ECVF_SetByCode);
			}
		};
		Set(TEXT("r.DynamicGlobalIlluminationMethod"), 1);
		Set(TEXT("r.ReflectionMethod"), 1);
		Set(TEXT("r.Lumen.TranslucencyReflections.FrontLayer.Enable"), 1);
		Set(TEXT("r.MotionBlurQuality"), 0);
		Set(TEXT("r.ScreenPercentage"), 100);
	}
}

AOsdsTurntable::AOsdsTurntable()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

TArray<FOsdsTurntableView> AOsdsTurntable::DefaultViews()
{
	TArray<FOsdsTurntableView> V;
	auto Add = [&V](const TCHAR* Name, double Azimuth, double Height)
	{
		FOsdsTurntableView& View = V.AddDefaulted_GetRef();
		View.Name = Name;
		View.AzimuthDeg = Azimuth;
		View.CameraHeightM = Height;
		return &View;
	};
	Add(TEXT("front"), 0, 1.0);
	Add(TEXT("front_34_left"), -40, 1.3);
	Add(TEXT("side_left"), -90, 1.0);
	Add(TEXT("rear_34_left"), -140, 1.3);
	Add(TEXT("rear"), 180, 1.0);
	Add(TEXT("rear_34_right"), 140, 1.3);
	Add(TEXT("side_right"), 90, 1.0);
	Add(TEXT("front_34_right"), 40, 1.3);
	FOsdsTurntableView* Driver = Add(TEXT("driver"), 0, 0);
	Driver->bDriver = true;
	Driver->HorizontalFovDeg = 80.0;
	FOsdsTurntableView* Wheel = Add(TEXT("wheel"), -70, 0.55);
	Wheel->WheelCloseupM = 1.25;
	Wheel->HorizontalFovDeg = 45.0;
	FOsdsTurntableView* Steer = Add(TEXT("steer_front_34_left"), -35, 1.3);
	Steer->SteerDeg = 25.0;
	FOsdsTurntableView* SteerDriver = Add(TEXT("steer_driver"), 0, 0);
	SteerDriver->bDriver = true;
	SteerDriver->HorizontalFovDeg = 80.0;
	SteerDriver->SteerDeg = 8.0;
	FOsdsTurntableView* Brake = Add(TEXT("brake_rear_34_left"), -150, 1.2);
	Brake->bBrake = true;
	return V;
}

void AOsdsTurntable::Configure(const TArray<FString>& InCarIds, const FString& InOutputDir, bool bInQuitWhenDone)
{
	CarIds = InCarIds;
	OutputDir = InOutputDir.IsEmpty() ? OsdsTurntablePrivate::DefaultOutput() : FPaths::ConvertRelativePathToFull(InOutputDir);
	bQuitWhenDone = bInQuitWhenDone;
	Views = DefaultViews();
}

void AOsdsTurntable::BeginPlay()
{
	Super::BeginPlay();
	if (Views.Num() == 0)
	{
		Views = DefaultViews();
	}
	if (OutputDir.IsEmpty())
	{
		OutputDir = OsdsTurntablePrivate::DefaultOutput();
	}
	IFileManager::Get().MakeDirectory(*OutputDir, true);
	OsdsTurntablePrivate::ApplyCaptureQuality(GetWorld());
	BuildStage();
	Report = FString::Printf(TEXT("OSDS turntable, %s\nSun %.0f deg up, %.0f deg from the nose; scalability 4; Lumen GI and reflections.\n\n"),
		*FDateTime::Now().ToString(), OsdsTurntablePrivate::SunElevationDeg, OsdsTurntablePrivate::SunAzimuthDeg);
}

void AOsdsTurntable::BuildStage()
{
	using namespace OsdsTurntablePrivate;
	UWorld* World = GetWorld();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// The turntable is a fixed studio with its own sun and camera: the real-sky
	// environment every game world gets (Environment/) would add a second sun.
	for (TActorIterator<AOsdsEnvironment> It(World); It; ++It)
	{
		It->Destroy();
	}

	// Sun: illuminance outside the atmosphere; the sky atmosphere applies the transmittance.
	const double El = FMath::DegreesToRadians(SunElevationDeg), Az = FMath::DegreesToRadians(SunAzimuthDeg);
	const FVector ToSun(FMath::Cos(El) * FMath::Cos(Az), FMath::Cos(El) * FMath::Sin(Az), FMath::Sin(El));
	ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(FVector(0, 0, 1000), (-ToSun).Rotation(), Params);
	if (Sun)
	{
		UDirectionalLightComponent* Light = CastChecked<UDirectionalLightComponent>(Sun->GetLightComponent());
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetIntensity(120000.f);
		Light->SetAtmosphereSunLight(true);
		Light->SetLightSourceAngle(0.5357f);
		Light->SetDynamicShadowCascades(4);
		StageActors.Add(Sun);
	}
	AActor* Sky = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	if (Sky)
	{
		USkyAtmosphereComponent* Atmosphere = NewObject<USkyAtmosphereComponent>(Sky, TEXT("Atmosphere"));
		Sky->SetRootComponent(Atmosphere);
		Atmosphere->RegisterComponent();
		UVolumetricCloudComponent* Clouds = NewObject<UVolumetricCloudComponent>(Sky, TEXT("Clouds"));
		Clouds->SetupAttachment(Atmosphere);
		Clouds->SetLayerBottomAltitude(2.0f);
		Clouds->SetLayerHeight(4.0f);
		Clouds->RegisterComponent();
		StageActors.Add(Sky);
	}
	ASkyLight* SkyLight = World->SpawnActor<ASkyLight>(FVector(0, 0, 300), FRotator::ZeroRotator, Params);
	if (SkyLight)
	{
		USkyLightComponent* Light = SkyLight->GetLightComponent();
		Light->SetMobility(EComponentMobility::Movable);
		Light->bRealTimeCapture = true;
		Light->SetLowerHemisphereColor(FLinearColor(0.08f, 0.075f, 0.07f));
		Light->bLowerHemisphereIsBlack = false;
		Light->RecaptureSky();
		StageActors.Add(SkyLight);
	}
	APostProcessVolume* Post = World->SpawnActor<APostProcessVolume>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (Post)
	{
		Post->bUnbound = true;
		FPostProcessSettings& S = Post->Settings;
		// Like a phone camera: metered exposure within a daylight range.
		S.bOverride_AutoExposureMethod = true;
		S.AutoExposureMethod = AEM_Histogram;
		// Metering limits EV100 10..16; the project's luminance-range setting decides the units.
		const IConsoleVariable* Extended = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange"));
		const bool bEv100 = Extended && Extended->GetInt() != 0;
		auto Ev = [bEv100](float Ev100) { return bEv100 ? Ev100 : FMath::Pow(2.f, Ev100 - 3.f); };
		S.bOverride_AutoExposureMinBrightness = true;
		S.AutoExposureMinBrightness = Ev(10.f);
		S.bOverride_AutoExposureMaxBrightness = true;
		S.AutoExposureMaxBrightness = Ev(16.f);
		S.bOverride_HistogramLogMin = true;
		S.HistogramLogMin = bEv100 ? 4.f : 1.f;
		S.bOverride_HistogramLogMax = true;
		S.HistogramLogMax = bEv100 ? 18.f : 15.f;
		// No extra brightening on top of the metered exposure (the engine default biases +1 EV).
		S.bOverride_AutoExposureBias = true;
		S.AutoExposureBias = 0.f;
		S.bOverride_AutoExposureSpeedUp = true;
		S.AutoExposureSpeedUp = 10.f;
		S.bOverride_AutoExposureSpeedDown = true;
		S.AutoExposureSpeedDown = 10.f;
		S.bOverride_BloomIntensity = true;
		S.BloomIntensity = 0.25f;
		S.bOverride_VignetteIntensity = true;
		S.VignetteIntensity = 0.15f;
		S.bOverride_MotionBlurAmount = true;
		S.MotionBlurAmount = 0.f;
		S.bOverride_LensFlareIntensity = true;
		S.LensFlareIntensity = 0.f;
		StageActors.Add(Post);
	}

	// Ground: procedural asphalt (stone chips in binder, about 10 % albedo), 6 km
	// square so the horizon is ground, not the planet below the sky.
	FOsdsRigSection Ground;
	const float Half = 300000.f, Repeat = 120.f;
	const int32 N = 60;
	for (int32 Y = 0; Y <= N; ++Y)
	{
		for (int32 X = 0; X <= N; ++X)
		{
			const FVector3f P(-Half + 2.f * Half * X / N, -Half + 2.f * Half * Y / N, 0.f);
			Ground.Positions.Add(P);
			Ground.Normals.Add(FVector3f::UpVector);
			Ground.UV0.Add(FVector2f(P.X / Repeat, P.Y / Repeat));
		}
	}
	for (int32 Y = 0; Y < N; ++Y)
	{
		for (int32 X = 0; X < N; ++X)
		{
			const uint32 A = Y * (N + 1) + X, B = A + 1, C = A + (N + 1), D = C + 1;
			Ground.Indices.Append({A, C, B, B, C, D});
		}
	}
	UStaticMesh* GroundMesh = OsdsCarAssets::BuildStaticMesh(TEXT("SM_TurntableGround"), {&Ground}, false, true);
	AStaticMeshActor* GroundActor = World->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (GroundActor && GroundMesh)
	{
		UStaticMeshComponent* Component = GroundActor->GetStaticMeshComponent();
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(GroundMesh);
		UMaterialInstanceDynamic* Asphalt = UMaterialInstanceDynamic::Create(OsdsCarMaterials::GetMaster(EOsdsCarMaster::Opaque), GroundActor);
		Asphalt->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(1.f, 1.f, 1.f, 1.f));
		Asphalt->SetVectorParameterValue(TEXT("Surface"), FLinearColor(0.f, 0.88f, 0.f, 0.05f));
		Asphalt->SetVectorParameterValue(TEXT("Detail"), FLinearColor(17.f, 0.f, 0.f, 1.f));
		Asphalt->SetVectorParameterValue(TEXT("TextureFlags"), FLinearColor(0.f, 0.f, 0.f, 0.f));
		Component->SetMaterial(0, Asphalt);
		StageActors.Add(GroundActor);
	}

	Camera = World->SpawnActor<ACameraActor>(FVector(-600, 0, 150), FRotator::ZeroRotator, Params);
	if (Camera)
	{
		Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
		Camera->GetCameraComponent()->PostProcessBlendWeight = 0.f;
	}
}

void AOsdsTurntable::StartCar()
{
	if (Car)
	{
		Car->Destroy();
		Car = nullptr;
	}
	++CarIndex;
	ViewIndex = 0;
	FramesInView = 0;
	bShotRequested = false;
	FrameTimeSum = 0.0;
	FrameTimeCount = 0;
	if (!CarIds.IsValidIndex(CarIndex))
	{
		Finish();
		return;
	}
	const FString& Id = CarIds[CarIndex];
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Car = GetWorld()->SpawnActor<AOsdsCarPresentationActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	const double Start = FPlatformTime::Seconds();
	if (!Car || !Car->Presentation->InitializeForCar(Id))
	{
		const FString Error = Car ? Car->Presentation->GetLastError() : FString(TEXT("spawn failed"));
		UE_LOG(LogOsdsTurntable, Error, TEXT("%s: %s"), *Id, *Error);
		Report += FString::Printf(TEXT("== %s\nFAILED: %s\n\n"), *Id, *Error);
		StartCar();
		return;
	}
	// The body sits on its tyres: lift the origin so the static contact patch is on the ground.
	FOsdsCarDefinition Definition;
	FString Error;
	double ContactZ = 0.0;
	if (FOsdsCarDefinitionLoader::LoadById(Id, Definition, Error))
	{
		const double HubCm = Car->Presentation->GetStaticHubLocation(0).Z;
		// Loaded radius: about 3 % of the free radius goes into the contact patch.
		ContactZ = HubCm - Definition.TireFreeRadiusM() * 100.0 * 0.97;
	}
	Car->SetActorLocation(FVector(0, 0, -ContactZ));
	Report += FString::Printf(TEXT("== %s (built and placed in %.1f s)\n"), *Id, FPlatformTime::Seconds() - Start);
	for (const FString& Line : Car->Presentation->GetRigLog())
	{
		Report += TEXT("  ") + Line + TEXT("\n");
	}
	PoseView();
}

void AOsdsTurntable::PoseView()
{
	if (!Car || !Views.IsValidIndex(ViewIndex) || !Camera)
	{
		return;
	}
	const FOsdsTurntableView& View = Views[ViewIndex];
	UOsdsCarPresentationComponent* P = Car->Presentation;

	FOsdsCarVisualState State;
	const float Steer = FMath::DegreesToRadians(View.SteerDeg);
	State.Wheels[0].SteerAngleRad = Steer;
	State.Wheels[1].SteerAngleRad = Steer;
	State.bBrake = View.bBrake;
	P->ApplyVisualState(State);

	const FTransform CarToWorld = Car->GetActorTransform();
	FVector Location, Target;
	if (View.bDriver)
	{
		Location = CarToWorld.TransformPosition(P->GetDriverEye());
		Target = Location + CarToWorld.TransformVector(FVector(100.0, 0.0, -10.0));
	}
	else if (View.WheelCloseupM > 0.0)
	{
		Target = CarToWorld.TransformPosition(P->GetStaticHubLocation(0));
		const double Az = FMath::DegreesToRadians(View.AzimuthDeg);
		Location = Target + FVector(FMath::Cos(Az), FMath::Sin(Az), 0.0) * View.WheelCloseupM * 100.0;
		Location.Z = View.CameraHeightM * 100.0;
	}
	else
	{
		// Frame the car: its projected width from this azimuth plus a margin.
		const double Length = (P->GetStaticHubLocation(0) - P->GetStaticHubLocation(2)).Size() * 1.75;
		const double Width = FMath::Abs(P->GetStaticHubLocation(1).Y - P->GetStaticHubLocation(0).Y) * 1.25;
		const double Az = FMath::DegreesToRadians(View.AzimuthDeg);
		const double Projected = Length * FMath::Abs(FMath::Sin(Az)) + Width * FMath::Abs(FMath::Cos(Az));
		const double HalfFov = FMath::DegreesToRadians(View.HorizontalFovDeg) * 0.5;
		const double Distance = (Projected * 0.5 * 1.3) / FMath::Tan(HalfFov) + (Length * FMath::Abs(FMath::Cos(Az)) + Width * FMath::Abs(FMath::Sin(Az))) * 0.5;
		Target = CarToWorld.TransformPosition(FVector(0.0, 0.0, 0.0)) + FVector(0, 0, 55.0);
		Location = Target + FVector(FMath::Cos(Az), FMath::Sin(Az), 0.0) * Distance;
		Location.Z = View.CameraHeightM * 100.0;
	}
	Camera->SetActorLocationAndRotation(Location, (Target - Location).Rotation());
	Camera->GetCameraComponent()->SetFieldOfView(View.HorizontalFovDeg);
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->SetViewTarget(Camera);
	}
	FramesInView = 0;
	bShotRequested = false;
}

void AOsdsTurntable::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bDone)
	{
		return;
	}
	if (CarIndex < 0)
	{
		StartCar();
		return;
	}
	if (!Car)
	{
		return;
	}
	++FramesInView;
	if (FramesInView > 10)
	{
		FrameTimeSum += FApp::GetDeltaTime();
		++FrameTimeCount;
	}
	if (!bShotRequested)
	{
		if (FramesInView < OsdsTurntablePrivate::SettleFrames)
		{
			return;
		}
#if WITH_EDITOR
		if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
		{
			GShaderCompilingManager->FinishAllCompilation();
			FramesInView = OsdsTurntablePrivate::SettleFrames / 2;
			return;
		}
#endif
		const FString Name = FString::Printf(TEXT("%s_%s.png"), *CarIds[CarIndex], *Views[ViewIndex].Name);
		PendingShot = FPaths::Combine(OutputDir, Name);
		IFileManager::Get().Delete(*PendingShot, false, true, true);
		FScreenshotRequest::RequestScreenshot(PendingShot, false, false);
		bShotRequested = true;
		FramesInView = 0;
		return;
	}
	if (IFileManager::Get().FileExists(*PendingShot) || FramesInView > 120)
	{
		if (FramesInView > 120)
		{
			UE_LOG(LogOsdsTurntable, Warning, TEXT("Screenshot did not appear: %s"), *PendingShot);
		}
		UE_LOG(LogOsdsTurntable, Display, TEXT("Captured %s"), *PendingShot);
		++ViewIndex;
		if (!Views.IsValidIndex(ViewIndex))
		{
			const double Ms = FrameTimeCount > 0 ? FrameTimeSum / FrameTimeCount * 1000.0 : 0.0;
			Report += FString::Printf(TEXT("  mean frame time while settling: %.2f ms (%d frames, includes capture overhead)\n\n"), Ms, FrameTimeCount);
			StartCar();
			return;
		}
		PoseView();
	}
}

void AOsdsTurntable::Finish()
{
	bDone = true;
	const FString ReportFile = FPaths::Combine(OutputDir, TEXT("report.txt"));
	FFileHelper::SaveStringToFile(Report, *ReportFile);
	UE_LOG(LogOsdsTurntable, Display, TEXT("Turntable done: %s"), *ReportFile);
	if (bQuitWhenDone)
	{
		FPlatformMisc::RequestExit(false, TEXT("OSDS turntable done"));
	}
}

AOsdsTurntableGameMode::AOsdsTurntableGameMode()
{
	DefaultPawnClass = nullptr;
	bStartPlayersAsSpectators = false;
}

void AOsdsTurntableGameMode::StartPlay()
{
	Super::StartPlay();
	FString Ids;
	if (!FParse::Value(FCommandLine::Get(), TEXT("OsdsTurntable="), Ids, false))
	{
		Ids = TEXT("all");
	}
	FString Out;
	FParse::Value(FCommandLine::Get(), TEXT("OsdsTurntableOut="), Out, false);
	AOsdsTurntable* Turntable = GetWorld()->SpawnActor<AOsdsTurntable>();
	Turntable->Configure(OsdsTurntablePrivate::ResolveIds(Ids), Out, !FParse::Param(FCommandLine::Get(), TEXT("OsdsTurntableStay")));
}

static FAutoConsoleCommandWithWorldAndArgs GOsdsTurntableCommand(
	TEXT("OSDS.Turntable"),
	TEXT("OSDS.Turntable [ids|all] [output dir]: photograph cars from 8 angles plus driver, wheel, steer and brake views."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		AOsdsTurntable* Turntable = World->SpawnActor<AOsdsTurntable>();
		Turntable->Configure(OsdsTurntablePrivate::ResolveIds(Args.Num() > 0 ? Args[0] : FString()), Args.Num() > 1 ? Args[1] : FString(), false);
	}));

#if WITH_EDITOR
static FAutoConsoleCommand GOsdsSaveCarMaterials(
	TEXT("OSDS.SaveCarMaterials"),
	TEXT("Save the generated car master materials under /Game/Vehicles/Materials (needed for cooked games)."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		FString Report;
		OsdsCarMaterials::SaveMasters(Report);
		UE_LOG(LogOsdsTurntable, Display, TEXT("%s"), *Report);
	}));
#endif
