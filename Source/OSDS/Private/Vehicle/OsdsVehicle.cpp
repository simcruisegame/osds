// Copyright OSDS contributors. All Rights Reserved.

#include "Vehicle/OsdsVehicle.h"
#include "Vehicle/OsdsSurfaceKind.h"
#include "Vehicle/OsdsSteeringGeometry.h"

#include "Components/BoxComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Environment/OsdsEnvironment.h"
#include "Environment/OsdsEnvironmentSubsystem.h"
#include "Gameplay/OsdsHaptics.h"
#include "Audio/OsdsEngineSoundComponent.h"
#include "Presentation/OsdsPresentationProfile.h"
#include "AudioDevice.h"
#include "Gameplay/OsdsPlayerCameraManager.h"
#include "Gameplay/OsdsPlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Presentation/OsdsLowBeamPattern.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<float> CVarBeamScale(TEXT("osds.Lamps.BeamScale"), 1.f,
		TEXT("Look development: multiplies the low beams' intensity (1 = the real 15 000 cd)."));
	TAutoConsoleVariable<int32> CVarMirrors(TEXT("osds.Mirrors"), 1,
		TEXT("1: the player's car mirrors show the road behind (from the driver's seat: a 768x320 capture every third frame); 0: plain glass."));
	TAutoConsoleVariable<int32> CVarEngineSound(TEXT("osds.Audio.Engine"), 1,
		TEXT("1: the driven car's engine voice plays (M toggles sound); 0: silent."));
	TAutoConsoleVariable<int32> CVarRoadNoise(TEXT("osds.Audio.Road"), 1,
		TEXT("1: tyre, road and wind noise play for the driven car (M toggles sound); 0: silent."));
	TAutoConsoleVariable<int32> CVarMirrorInterval(TEXT("osds.Mirrors.Interval"), 3,
		TEXT("The mirror capture runs every Nth frame (1: every frame)."));
	TAutoConsoleVariable<int32> CVarBeamPattern(TEXT("osds.Lamps.Pattern"), 1,
		TEXT("1: the low beams carry their photometric pattern and cut-off (light function); 0: plain cones."));
}
#include "Vehicle/OsdsUnits.h"
#include "Vehicle/OsdsVehicleMovementComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogOsdsVehiclePawn, Log, All);

AOsdsVehicle::AOsdsVehicle()
{
	// Ticks only to pass the gearbox's refusals to the driver's screen.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	AutoPossessPlayer = EAutoReceiveInput::Disabled;

	Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
	Body->SetCollisionProfileName(UCollisionProfile::Vehicle_ProfileName);
	Body->SetSimulatePhysics(true);
	Body->SetEnableGravity(true);
	Body->BodyInstance.SetInertiaConditioningEnabled(false);
	Body->SetLinearDamping(0.0f);
	Body->SetHiddenInGame(true);
	Body->SetCanEverAffectNavigation(false);
	RootComponent = Body;

	VehicleMovement = CreateDefaultSubobject<UOsdsVehicleMovementComponent>(TEXT("VehicleMovement"));
	ApplyBodyShape();
}

void AOsdsVehicle::SetCarDefinition(const FOsdsCarDefinition& Car)
{
	// Shape first: a shape change can make Chaos recompute mass properties,
	// which the movement component then sets from the car.
	ApplyBodyShape(Car);
	VehicleMovement->SetCarDefinition(Car);
	CarId = Car.Id;
	bHasDefinition = true;
	RealCar = Car;
	bTuned = false;
	if (CarModel)
	{
		CarModel->SetTunedSteeringRatio(0.0);
	}
	Tuning = GetStockTuning();
}

FOsdsTuning AOsdsVehicle::GetStockTuning() const
{
	return FOsdsTuning::FromCar(RealCar, CarModel ? CarModel->GetSteeringRatio() : 0.0);
}

bool AOsdsVehicle::ApplyTuning(const FOsdsTuning& InTuning, bool bSave)
{
	if (!bHasDefinition)
	{
		return false;
	}
	const FOsdsTuning Stock = GetStockTuning();
	Tuning = InTuning;
	Tuning.Clamp(Stock);
	// A new definition resets the drivetrain, so a car on the move keeps its setup until it
	// stops (Tick applies it then); the save happens now.
	if (bSave && FMath::Abs(VehicleMovement->GetTelemetry().SpeedMps) > 1.0)
	{
		bTuningPending = true;
		return Tuning.Save(RealCar.Id);
	}
	bTuningPending = false;
	const FOsdsCarDefinition Tuned = Tuning.Apply(RealCar, Stock);
	// The new definition resets the drivetrain, not where the car stands.
	ApplyBodyShape(Tuned);
	VehicleMovement->SetCarDefinition(Tuned);
	bTuned = true;
	// The cockpit wheel turns at the tuned ratio: the same road-wheel angle takes less rim with a quicker rack.
	if (CarModel)
	{
		CarModel->SetTunedSteeringRatio(Tuning.SteeringRatio);
	}
	const bool bSaved = !bSave || Tuning.Save(RealCar.Id);
	UE_LOG(LogTemp, Display, TEXT("OSDS tuning %s: springs %.0f/%.0f lb/in, damping %.0f %%, ride %+.0f mm, bars %.0f/%.0f %%, torque %.0f %%, final drive %.2f, steering %.1f:1, roll %.2f deg/g"),
		*RealCar.Id, Tuning.FrontSpringLbIn, Tuning.RearSpringLbIn, Tuning.DampingPct, Tuning.RideHeightMm, Tuning.FrontBarPct, Tuning.RearBarPct,
		Tuning.TorquePct, Tuning.FinalDrive, Tuning.SteeringRatio, FOsdsTuning::RollGradientDegPerG(Tuned));
	return bSaved;
}

bool AOsdsVehicle::LoadSavedTuning()
{
	FOsdsTuning Saved = GetStockTuning();
	if (!bHasDefinition || !Saved.Load(RealCar.Id))
	{
		return false;
	}
	ApplyTuning(Saved, false);
	return true;
}

void AOsdsVehicle::ApplyBodyShape()
{
	ApplyBodyShape(VehicleMovement->GetCarDefinition());
}

void AOsdsVehicle::ApplyBodyShape(const FOsdsCarDefinition& Car)
{
	// Godot's collision_box is (width, height, length).
	const FVector3d SizeM = FVector3d(Car.CollisionBoxSizeM.Z, Car.CollisionBoxSizeM.X, Car.CollisionBoxSizeM.Y);
	Body->SetBoxExtent(OsdsUnits::ToUnrealLength(SizeM * 0.5), false);
}

void AOsdsVehicle::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyBodyShape();
}

void AOsdsVehicle::BeginPlay()
{
	if (!bHasDefinition && !CarId.IsEmpty())
	{
		// Before Super::BeginPlay, so the component configures the body after the shape is set.
		bHasDefinition = VehicleMovement->LoadCarById(CarId);
		ApplyBodyShape();
	}
	Super::BeginPlay();
}

bool AOsdsVehicle::AttachCarModel()
{
	if (!CarModel)
	{
		CarModel = NewObject<UOsdsCarPresentationComponent>(this, TEXT("CarModel"));
		CarModel->SetMobility(EComponentMobility::Movable);
		CarModel->SetupAttachment(Body);
		CarModel->RegisterComponent();
		AddInstanceComponent(CarModel);
	}
	// The model's frame is the Godot car origin: the ground at ride height,
	// CollisionShapeCenterYM below the body's centre.
	const double OriginCm = OsdsUnits::ToUnrealLength(VehicleMovement->GetCarDefinition().CollisionShapeCenterYM);
	CarModel->SetRelativeTransform(FTransform(FVector(0.0, 0.0, -OriginCm)));
	if (!CarModel->InitializeForCar(CarId))
	{
		UE_LOG(LogOsdsVehiclePawn, Warning, TEXT("%s drives without a visible model: %s"), *CarId, *CarModel->GetLastError());
		return false;
	}
	// Every part is visual only (no collision), so nothing welds into the Chaos body.
	CarModel->SetWheelStateProvider(TScriptInterface<IOsdsWheelStateProvider>(this));
	// Pose after this pawn's physics results are in.
	CarModel->AddTickPrerequisiteActor(this);

	// The low beams: real lights from the head-lamp lenses, so the road ahead is lit at night.
	for (USpotLightComponent* Light : HeadLampLights)
	{
		if (Light)
		{
			Light->DestroyComponent();
		}
	}
	HeadLampLights.Reset();
	bHeadLampsShown = false;
	TArray<FVector> Lamps = CarModel->GetHeadLampLocations();
	if (Lamps.IsEmpty())
	{
		// No head-lamp surfaces on the model: the usual place, about 55 cm ahead of
		// the front hubs, just inside their track and 30 cm above them.
		const FVector FL = CarModel->GetStaticHubLocation(0), FR = CarModel->GetStaticHubLocation(1);
		Lamps.Add(FL + FVector(55.0, 12.0, 30.0));
		Lamps.Add(FR + FVector(55.0, -12.0, 30.0));
		UE_LOG(LogOsdsVehiclePawn, Display, TEXT("%s: no head-lamp lenses on the model; beams placed from the hubs"), *CarId);
	}
	for (int32 I = 0; I < Lamps.Num(); ++I)
	{
		USpotLightComponent* Light = NewObject<USpotLightComponent>(this, *FString::Printf(TEXT("LowBeam%d"), I));
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetupAttachment(CarModel);
		// Just ahead of the lens: from inside the housing the body would shadow its own beam.
		Light->SetRelativeLocation(Lamps[I] + FVector(18.0, 0.0, 0.0));
		Light->SetRelativeRotation(FRotator(-LowBeamAimDownDeg, 0.0, 0.0));
		Light->SetIntensityUnits(ELightUnits::Candelas);
		Light->SetIntensity(static_cast<float>(LowBeamPeakCandela));
		Light->SetInnerConeAngle(static_cast<float>(LowBeamCoreHalfAngleDeg));
		Light->SetOuterConeAngle(static_cast<float>(LowBeamEdgeHalfAngleDeg));
		// Unreal fades a light to zero at its radius: at 122 m (where the peak gives 1 lux) the
		// fade dimmed the whole beam; at 250 m the road ahead keeps its inverse-square light.
		Light->SetAttenuationRadius(static_cast<float>(OsdsUnits::ToUnrealLength(LowBeamRangeM)));
		Light->SetSourceRadius(6.f);
		Light->SetUseTemperature(true);
		Light->SetTemperature(3200.f);
		Light->SetCastShadows(true);
		// The beam pattern with its cut-off, so the lamps light the road and not the mirrors
		// of oncoming drivers. Driving on the right, the curb is the lamp's right.
		if (UMaterialInterface* Pattern = OsdsLowBeamPattern::Get())
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Pattern, Light);
			MID->SetScalarParameterValue(TEXT("TanOuter"), static_cast<float>(FMath::Tan(FMath::DegreesToRadians(LowBeamEdgeHalfAngleDeg))));
			MID->SetScalarParameterValue(TEXT("AimDown"), static_cast<float>(LowBeamAimDownDeg));
			Light->SetLightFunctionMaterial(MID);
			Light->SetLightFunctionScale(FVector(1.0, 1.0, 1.0));
		}
		Light->SetVisibility(false);
		Light->RegisterComponent();
		AddInstanceComponent(Light);
		HeadLampLights.Add(Light);
	}
	return true;
}

void AOsdsVehicle::UpdateHeadLamps()
{
	const bool bLit = AreLampsWanted();
	if (bLit != bHeadLampsShown)
	{
		bHeadLampsShown = bLit;
		for (USpotLightComponent* Light : HeadLampLights)
		{
			if (Light)
			{
				Light->SetVisibility(bLit);
			}
		}
	}
	// Look development switches (no cost unless they change).
	{
		const float Scale = FMath::Max(CVarBeamScale.GetValueOnGameThread(), 0.f);
		const bool bPattern = CVarBeamPattern.GetValueOnGameThread() != 0;
		for (USpotLightComponent* Light : HeadLampLights)
		{
			if (!Light) continue;
			const float Want = static_cast<float>(LowBeamPeakCandela) * Scale;
			if (!FMath::IsNearlyEqual(Light->Intensity, Want)) Light->SetIntensity(Want);
			if (!bPattern && Light->LightFunctionMaterial) { BeamPatterns.Add(Light, Light->LightFunctionMaterial); Light->SetLightFunctionMaterial(nullptr); }
			if (bPattern && !Light->LightFunctionMaterial)
			{
				if (TObjectPtr<UMaterialInterface>* Saved = BeamPatterns.Find(Light)) Light->SetLightFunctionMaterial(*Saved);
			}
		}
	}
	// The player's eye adapts to the road his own lamps light.
	if (IsPlayerControlled())
	{
		if (UOsdsEnvironmentSubsystem* Subsystem = UOsdsEnvironmentSubsystem::Get(GetWorld()))
		{
			if (AOsdsEnvironment* Environment = Subsystem->GetEnvironment())
			{
				const double Lux = bLit && !HeadLampLights.IsEmpty() ? NightAdaptationLux : 0.0;
				Environment->SetArtificialIlluminanceLux(Lux);
			}
		}
	}
}

FOsdsVehicleTelemetry AOsdsVehicle::GetVehicleTelemetry() const
{
	return VehicleMovement->GetTelemetry();
}

void AOsdsVehicle::SetThrottle(float Value) { VehicleMovement->SetThrottle(Value); }
void AOsdsVehicle::SetBrake(float Value) { VehicleMovement->SetBrake(Value); }
void AOsdsVehicle::SetSteer(float Value, bool bFromThumbstick) { VehicleMovement->SetSteer(Value, bFromThumbstick); }
void AOsdsVehicle::SetHandbrake(bool bEngaged) { VehicleMovement->SetHandbrake(bEngaged); }
void AOsdsVehicle::SetClutch(float Value) { VehicleMovement->SetClutch(Value); }
void AOsdsVehicle::ShiftUp() { VehicleMovement->ShiftUp(); }
void AOsdsVehicle::ShiftDown() { VehicleMovement->ShiftDown(); }
void AOsdsVehicle::ToggleManual() { VehicleMovement->ToggleManual(); }

FTransform AOsdsVehicle::BodyTransformForGroundPose(const FVector& GroundLocation, float YawDeg) const
{
	const FQuat Rotation(FRotator(0.0, YawDeg, 0.0));
	const double BoxCentreCm = OsdsUnits::ToUnrealLength(VehicleMovement->GetCarDefinition().CollisionShapeCenterYM);
	return FTransform(Rotation, GroundLocation + Rotation.RotateVector(FVector(0.0, 0.0, BoxCentreCm)));
}

void AOsdsVehicle::ResetAt(const FVector& GroundLocation, float YawDeg)
{
	VehicleMovement->ResetVehicle(BodyTransformForGroundPose(GroundLocation, YawDeg));
}

// --- Driving shell ---------------------------------------------------------

FOsdsVehicleSetup AOsdsVehicle::GetVehicleSetup() const
{
	const FOsdsCarDefinition& Car = VehicleMovement->GetCarDefinition();
	FOsdsVehicleSetup Setup;
	Setup.CarId = Car.Id;
	Setup.DisplayName = Car.Name;
	Setup.WheelbaseM = Car.WheelbaseM;
	Setup.CenterOfMassToFrontAxleM = Car.WheelbaseM * (1.0 - Car.FrontWeightFraction);
	Setup.ParkingRackAngleRad = FMath::DegreesToRadians(Car.ParkingRackAngleDeg);
	Setup.SteeringAssistLateralG = Car.SteeringAssistLateralG;
	Setup.SteeringSlipLimitSurplusRad = FMath::DegreesToRadians(Car.SteeringSlipLimitSurplusDeg);
	Setup.SteeringRatio = CarModel ? CarModel->GetSteeringRatio() : 0.0;
	// A tuned steering ratio: the wheel and the on-screen wheel use it, and a stick moves the
	// rack quicker or slower by the same factor.
	if (bTuned && Tuning.SteeringRatio > 0.0 && Setup.SteeringRatio > 0.0)
	{
		Setup.SteeringQuickness = Setup.SteeringRatio / Tuning.SteeringRatio;
		Setup.SteeringRatio = Tuning.SteeringRatio;
	}
	// The rim's lock-to-lock turns scale with the ratio, as a quicker rack's would.
	Setup.SteeringLockTurns = Car.SteeringLockTurns / FMath::Max(Setup.SteeringQuickness, 0.1);
	// The car file's eye is relative to the Godot origin; this pawn's origin is the body centre above it.
	Setup.DriverEyeLocalCm = OsdsGodotToLocalCm(Car.DriverEyeM) - FVector(0.0, 0.0, OsdsUnits::ToUnrealLength(Car.CollisionShapeCenterYM));
	Setup.IdleRpm = Car.IdleSpeedRpm;
	Setup.UpshiftRpm = Car.AutomaticUpshiftSpeedRpm;
	Setup.RedlineRpm = Car.FuelCutSpeedRpm;
	Setup.ForwardGearCount = Car.ForwardGearRatios.Num();
	Setup.SkidpadG = Car.TargetSkidpadG;
	const double SeventyMph = 70.0 * 0.44704;
	const double StopM = Car.TargetSeventyToZeroFeet * 0.3048;
	Setup.BrakingG = StopM > 1.0 ? SeventyMph * SeventyMph / (2.0 * StopM) / 9.80665 : 0.9;
	return Setup;
}

void AOsdsVehicle::ToDriveTelemetry(const FOsdsCarDefinition& Car, const FOsdsVehicleTelemetry& In, FOsdsDriveTelemetry& Out)
{
	Out.BodyVelocityMps = In.BodyVelocityLocalMps;
	Out.BodyAngularVelocityRadPerS = In.BodyAngularVelocityLocalRadPerSecond;
	Out.EngineSpeedRpm = In.EngineRpm;
	Out.SimTimeS = In.SimTimeS;
	Out.Gear = In.Gear;
	Out.bManualTransmission = In.bManual;
	Out.bEngineStalled = In.bStalled;
	Out.bElectric = Car.EngineKind == TEXT("electric");
	Out.bServiceBrakeOn = In.bServiceBrake;
	Out.bStabilityControlActive = In.bStabilityControlActive;
	Out.RackAngleRad = FMath::DegreesToRadians(In.SteeringAngleDeg);
	Out.EngineLoad = FMath::Clamp(In.SoundCombustion, 0.0, 1.0);
	Out.RoadRoughness = 0.0;
	Out.RackAligningTorqueNm = In.RackAligningTorqueNm;
	const double PlateauRad = OsdsHaptics::PlateauSlipAngleRad(Car);
	for (int32 I = 0; I < 4; ++I)
	{
		FOsdsDriveWheelTelemetry& W = Out.Wheels[I];
		W = FOsdsDriveWheelTelemetry();
		if (!In.Wheels.IsValidIndex(I))
		{
			continue;
		}
		const FOsdsWheelTelemetry& V = In.Wheels[I];
		W.bContact = V.bContact;
		W.bDriven = Car.IsDrivenWheel(I);
		W.bAbsActive = V.AbsRelease < 0.95;
		W.NormalLoadN = V.LoadN;
		W.SlipRatio = V.SlipRatio;
		W.PeakSlipRatio = V.PeakSlipRatio;
		W.SlipAngleRad = FMath::DegreesToRadians(V.SlipAngleDeg);
		W.PeakSlipAngleRad = PlateauRad;
		// Compression from full droop (the longest the strut can be).
		W.SuspensionTravelM = FMath::Max(0.0, Car.MaximumSuspensionTravelM - V.SuspensionLengthCm / 100.0);
		W.SurfaceGrip = V.SurfaceGrip;
		W.SurfaceKind = V.SurfaceKind;
		W.bKerb = V.bKerb;
	}
}

void AOsdsVehicle::GetVehicleTelemetry(FOsdsDriveTelemetry& OutTelemetry) const
{
	FOsdsVehicleTelemetry Telemetry;
	VehicleMovement->CopyTelemetry(Telemetry);
	ToDriveTelemetry(VehicleMovement->GetCarDefinition(), Telemetry, OutTelemetry);
}

void AOsdsVehicle::ApplyDriverCommand(const FOsdsDriverCommand& Command)
{
	FOsdsVehicleControls Controls;
	Controls.Throttle = FMath::Clamp(Command.Throttle, 0.f, 1.f);
	Controls.Brake = FMath::Clamp(Command.Brake, 0.f, 1.f);
	Controls.Clutch = FMath::Clamp(Command.Clutch, 0.f, 1.f);
	Controls.bHandbrake = Command.ParkingBrake > 0.5f;
	Controls.bFullBrake = Command.bFullServiceBrake;
	// The shell's shaper owns steering rates, the speed cap and the countersteer allowance.
	Controls.bDirectRack = true;
	Controls.RackAngleRad = Command.RackAngleRad;
	VehicleMovement->SetDriverControls(Controls);
}

FString AOsdsVehicle::RequestGearChange(int32 Step)
{
	FOsdsVehicleTelemetry Telemetry;
	VehicleMovement->CopyTelemetry(Telemetry);
	if (!Telemetry.bManual && !bManualRequested)
	{
		return TEXT("Automatic shifting · T (right-stick click) for manual");
	}
	// The gearbox answers on the physics thread; a refusal reaches the screen from Tick.
	LastShownGearMessage.Reset();
	if (Step > 0)
	{
		VehicleMovement->ShiftUp();
	}
	else if (Step < 0)
	{
		VehicleMovement->ShiftDown();
	}
	return FString();
}

void AOsdsVehicle::SetManualTransmission(bool bManual)
{
	bManualRequested = bManual;
	VehicleMovement->SetManualTransmission(bManual);
}

void AOsdsVehicle::RequestEngineStart()
{
	VehicleMovement->RequestEngineStart();
}

void AOsdsVehicle::ResetVehicle(const FTransform& WorldTransform)
{
	UE_LOG(LogTemp, Display, TEXT("PLAYER MOVE reset: %s to X=%.1f Y=%.1f Z=%.1f heading %.1f deg"), *GetName(), WorldTransform.GetLocation().X, WorldTransform.GetLocation().Y, WorldTransform.GetLocation().Z, WorldTransform.Rotator().Yaw);
	ResetAt(WorldTransform.GetLocation(), static_cast<float>(WorldTransform.Rotator().Yaw));
}

void AOsdsVehicle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateHeadLamps();
	// A setup saved on the move goes on when the car has stopped.
	if (bTuningPending && FMath::Abs(VehicleMovement->GetTelemetry().SpeedMps) < 0.5)
	{
		ApplyTuning(Tuning, false);
	}
	// Rain on the road reaches the tyres' grip.
	if (const UOsdsEnvironmentSubsystem* Environments = UOsdsEnvironmentSubsystem::Get(GetWorld()))
	{
		if (const AOsdsEnvironment* Environment = Environments->GetEnvironment())
		{
			VehicleMovement->SetRoadWetness(Environment->GetRoadWetness());
			// The air: ISA pressure at the car's height above sea level, the live temperature
			// (dry air, R = 287.05 J/(kg K)), and the live wind at about 0.7 of its 10 m speed
			// (a suburban log profile at car height). The map's +X is east and +Y south; the
			// wind comes from WindFromDeg (clockwise from north).
			const FOsdsWeatherConditions& Weather = Environment->GetWeather();
			const double HeightM = Environment->ElevationM + GetActorLocation().Z / 100.0;
			const double PressurePa = 101325.0 * FMath::Pow(FMath::Max(1.0 - 2.25577e-5 * HeightM, 0.1), 5.25588);
			const double Density = PressurePa / (287.05 * (Weather.TemperatureC + 273.15));
			const double Toward = FMath::DegreesToRadians(Weather.WindFromDeg + 180.0);
			const FVector Wind(FMath::Sin(Toward), -FMath::Cos(Toward), 0.0);
			AirWindMps = Wind * (0.7 * Weather.WindSpeedMs);
			VehicleMovement->SetAir(Density, AirWindMps);
		}
	}
	// Live mirrors for the car being driven, from the driver's seat (from outside they are
	// a few pixels; parked and traffic cars never have them).
	const APlayerController* PC = Cast<APlayerController>(GetController());
	const AOsdsPlayerCameraManager* Camera = PC ? Cast<AOsdsPlayerCameraManager>(PC->PlayerCameraManager) : nullptr;
	const bool bInSeat = !Camera || Camera->GetShownView() == EOsdsView::Driver;
	if (IsPlayerControlled())
	{
		if (const UOsdsEnvironmentSubsystem* Environments = UOsdsEnvironmentSubsystem::Get(GetWorld()))
		{
			if (AOsdsEnvironment* Environment = Environments->GetEnvironment()) Environment->SetDriverView(bInSeat && !IsHidden()); // hidden: a capture of the world alone
		}
	}
	const bool bWantMirrors = IsPlayerControlled() && bInSeat && CVarMirrors.GetValueOnGameThread() != 0 && CarModel;
	if (bWantMirrors != bMirrorsLive && CarModel)
	{
		bMirrorsLive = bWantMirrors;
		CarModel->SetMirrorsLive(bMirrorsLive, GetRootComponent(), GetVehicleSetup().DriverEyeLocalCm);
	}
	FOsdsVehicleTelemetry Telemetry;
	VehicleMovement->CopyTelemetry(Telemetry);
	UpdateEngineSound(Telemetry, bInSeat, DeltaSeconds);
	if (Telemetry.LastGearMessage.IsEmpty())
	{
		// A new shift request clears the message, so the same refusal again is shown again.
		LastShownGearMessage.Reset();
	}
	else if (Telemetry.LastGearMessage != LastShownGearMessage)
	{
		LastShownGearMessage = Telemetry.LastGearMessage;
		if (AOsdsPlayerController* Player = Cast<AOsdsPlayerController>(GetController()))
		{
			Player->ShowMessage(Telemetry.LastGearMessage, 2.5f);
			Player->NotifyShiftRefused();
		}
	}
}

// --- Engine sound -------------------------------------------------------------

void AOsdsVehicle::UpdateEngineSound(const FOsdsVehicleTelemetry& Telemetry, bool bInSeat, float DeltaSeconds)
{
	const AOsdsPlayerController* Player = Cast<AOsdsPlayerController>(GetController());
	const bool bElectric = VehicleMovement->GetCarDefinition().EngineKind == TEXT("electric");
	// An electric car has no combustion sound (a motor voice is future work); headless
	// runs have no audio device.
	const bool bSound = Player && Player->IsSoundOn() && GetWorld() && GetWorld()->GetAudioDevice().IsValid();
	UpdateRoadNoise(Telemetry, bInSeat, bSound && CVarRoadNoise.GetValueOnGameThread() != 0, DeltaSeconds);
	const bool bWant = bSound && !bElectric && CVarEngineSound.GetValueOnGameThread() != 0;
	if (!bWant)
	{
		if (EngineSound && EngineSound->IsPlaying()) EngineSound->Stop();
		return;
	}
	if (!EngineSound)
	{
		EngineSound = NewObject<UOsdsEngineSoundComponent>(this, TEXT("EngineSound"));
		EngineSound->SetupAttachment(GetRootComponent());
		// The Godot mix: the Engine bus at -6 dB and its trim at -6 dB (RoadNoise.gd).
		EngineSound->SetVolumeMultiplier(0.25f);
		// The car's own engine layout (a V8's firing order), else the inline four.
		if (const FOsdsCarPresentationProfile* Profile = CarModel ? CarModel->GetProfile() : nullptr)
		{
			if (Profile->EngineSound.IsSet())
			{
				EngineSound->ConfigureCrossPlaneV8(Profile->EngineSound->BankOfEvent, Profile->EngineSound->NearGain, Profile->EngineSound->FarGain);
			}
		}
		EngineSound->RegisterComponent();
	}
	FOsdsInlineFourSynth::FInputs In;
	In.Rpm = Telemetry.EngineRpm;
	In.Combustion = Telemetry.SoundCombustion;
	In.ShaftLoad = Telemetry.SoundShaftLoad;
	In.Pedal = Telemetry.SoundThrottle;
	In.bStalled = Telemetry.bStalled;
	In.bCabin = bInSeat;
	// The limiter's stutter at this car's own fuel cut (NB 7250, Mustang 5000, C8 6600 rpm).
	In.LimiterRpm = VehicleMovement->GetCarDefinition().FuelCutSpeedRpm - 25.0;
	In.PopAmount = VehicleMovement->GetCarDefinition().ExhaustPopAmount;
	// The intake's distance from the ear: a mid engine right behind the seats is louder inside.
	if (const FOsdsCarPresentationProfile* Profile = CarModel ? CarModel->GetProfile() : nullptr)
	{
		if (Profile->EngineSound.IsSet()) In.CabinInletGain = 0.5 * 2.0 / FMath::Max(Profile->EngineSound->CabinEngineDistanceM, 0.5);
	}
	EngineSound->SetEngineState(In);
	if (!EngineSound->IsPlaying())
	{
		EngineSound->Start();
		UE_LOG(LogOsdsVehiclePawn, Display, TEXT("Engine sound playing (%s voice, %d Hz, %s pickup)"), EngineSound->IsV8() ? TEXT("cross-plane V8") : TEXT("inline-four"), FOsdsInlineFourSynth::SampleRate, bInSeat ? TEXT("cabin") : TEXT("exterior"));
	}
}

void AOsdsVehicle::UpdateRoadNoise(const FOsdsVehicleTelemetry& Telemetry, bool bInSeat, bool bWant, float DeltaSeconds)
{
	FOsdsDriveTelemetry Drive;
	ToDriveTelemetry(VehicleMovement->GetCarDefinition(), Telemetry, Drive);
	// Roughness: the suspension travel rate, high-passed so steady roll or dive does not
	// read as a rough road; 0 smooth .. 1 rough (RoadNoise.gd update_roughness).
	const double Dt = FMath::Max(static_cast<double>(DeltaSeconds), 1.0 / 240.0);
	double Rate = 0.0;
	for (int32 I = 0; I < 4; ++I)
	{
		const double Y = Drive.Wheels[I].SuspensionTravelM;
		if (bTravelKnown) Rate += FMath::Min(1.5, FMath::Abs(Y - Travel[I]) / Dt);
		Travel[I] = Y;
	}
	bTravelKnown = true;
	Rate *= .25;
	TravelSlow += (Rate - TravelSlow) * (1.0 - FMath::Exp(-Dt / .5));
	Roughness += (FMath::Clamp((Rate - TravelSlow) / .35, 0.0, 1.0) - Roughness) * (1.0 - FMath::Exp(-Dt / .1));
	if (!bWant)
	{
		if (RoadNoise && RoadNoise->IsPlaying()) RoadNoise->Stop();
		return;
	}
	if (!RoadNoise)
	{
		RoadNoise = NewObject<UOsdsRoadNoiseComponent>(this, TEXT("RoadNoise"));
		RoadNoise->SetupAttachment(GetRootComponent());
		RoadNoise->RegisterComponent();
	}
	const FOsdsCarDefinition& Car = VehicleMovement->GetCarDefinition();
	FOsdsRoadNoiseSynth::FInputs In;
	const double Speed = Drive.BodyVelocityMps.Size2D();
	In.SpeedMps = Speed;
	In.Bump = Roughness;
	In.bCabin = bInSeat;
	In.bTyres = true;
	double Grip = 0.0;
	int32 Contacts = 0;
	// Slip angle counts as the car rolls (OsdsHaptics: none at a standstill).
	const double LateralFeel = FMath::SmoothStep(OsdsHaptics::LateralFeelFromMps, OsdsHaptics::LateralFeelToMps, Speed);
	for (int32 I = 0; I < 4; ++I)
	{
		const FOsdsDriveWheelTelemetry& W = Drive.Wheels[I];
		Grip += W.bContact ? W.SurfaceGrip : 0.0;
		Contacts += W.bContact ? 1 : 0;
		FOsdsRoadNoiseSynth::FTyre& T = In.Tyres[I];
		T.S = W.bContact ? OsdsHaptics::NormalizedSlip(W.SlipRatio, W.PeakSlipRatio, W.SlipAngleRad * LateralFeel, W.PeakSlipAngleRad) : 0.0;
		T.Load = W.NormalLoadN / FMath::Max(Car.ReferenceCornerLoadN, 1.0);
		// The contact patch slides at the ground speed times the combined slip.
		T.Slide = Speed * FMath::Sqrt(FMath::Square(W.SlipRatio) + FMath::Square(FMath::Tan(FMath::Clamp(W.SlipAngleRad, -1.4, 1.4))));
	}
	In.Contact = Contacts / 4.0;
	In.Grip = Contacts > 0 ? Grip / Contacts : 1.0;
	// What the tyres roll on: the share on concrete (a kerb, a concrete road) and any kerb.
	int32 OnConcrete = 0;
	bool bAnyKerb = false;
	for (int32 I = 0; I < 4; ++I)
	{
		OnConcrete += Drive.Wheels[I].SurfaceKind == static_cast<uint8>(EOsdsSurfaceKind::Concrete) ? 1 : 0;
		bAnyKerb |= Drive.Wheels[I].bKerb;
	}
	In.ConcreteShare = Contacts > 0 ? OnConcrete / static_cast<double>(Contacts) : 0.0;
	if (bAnyKerb) In.Bump = FMath::Max(In.Bump, 0.6);
	// The wind noise follows the air past the car, not the road: a head wind is louder.
	In.AirSpeedMps = (FVector(GetVelocity()) / 100.0 - AirWindMps).Size();
	RoadNoise->SetRoadState(In);
	if (!RoadNoise->IsPlaying())
	{
		RoadNoise->Start();
		UE_LOG(LogOsdsVehiclePawn, Display, TEXT("Road noise playing (tyres, road and wind, %d Hz stereo)"), FOsdsRoadNoiseSynth::SampleRate);
	}
}

// --- Car model ---------------------------------------------------------------

void AOsdsVehicle::ToVisualState(const FOsdsCarDefinition& Car, const FOsdsVehicleTelemetry& In, FOsdsCarVisualState& Out)
{
	for (int32 I = 0; I < 4; ++I)
	{
		FOsdsWheelVisualState& W = Out.Wheels[I];
		if (!In.Wheels.IsValidIndex(I))
		{
			W = FOsdsWheelVisualState();
			continue;
		}
		const FOsdsWheelTelemetry& V = In.Wheels[I];
		const bool bLeft = I == 0 || I == 2;
		// The model's steer is positive to the left; the vehicle's to the right.
		W.SteerAngleRad = static_cast<float>(-FMath::DegreesToRadians(V.SteerAngleDeg));
		// Inclination leans the top toward the car's right; camber is positive with the top outboard.
		W.CamberRad = static_cast<float>(FMath::DegreesToRadians(V.InclinationDeg) * (bLeft ? -1.0 : 1.0));
		W.SpinAngleRad = static_cast<float>(FMath::Fmod(FMath::DegreesToRadians(V.SpinAngleDeg), UE_DOUBLE_TWO_PI));
		// Bump (wheel up toward the body) is positive: the strut shorter than at static ride.
		W.SuspensionTravelCm = static_cast<float>(Car.StaticSuspensionLengthM * 100.0 - V.SuspensionLengthCm);
	}
	Out.bBrake = In.bServiceBrake;
	// The instruments: the speedometer reads the road speed along the car, the tachometer the engine.
	Out.SpeedMps = static_cast<float>(FMath::Abs(In.ForwardSpeedMps));
	Out.EngineRpm = static_cast<float>(In.EngineRpm);
	Out.bEngineRunning = !In.bStalled;
}

bool AOsdsVehicle::AreLampsWanted() const
{
	if (bLampsOn)
	{
		return true;
	}
	const UOsdsEnvironmentSubsystem* Subsystem = UOsdsEnvironmentSubsystem::Get(GetWorld());
	const AOsdsEnvironment* Environment = Subsystem ? Subsystem->GetEnvironment() : nullptr;
	return Environment && Environment->GetSunPosition().ApparentElevationDeg < -1.0;
}

bool AOsdsVehicle::GetCarVisualState(FOsdsCarVisualState& OutState) const
{
	FOsdsVehicleTelemetry Telemetry;
	VehicleMovement->CopyTelemetry(Telemetry);
	if (Telemetry.PhysicsSteps <= 0)
	{
		return false;
	}
	ToVisualState(VehicleMovement->GetCarDefinition(), Telemetry, OutState);
	OutState.bLamps = AreLampsWanted();
	// The rim follows the rack through the column: the car's ratio on centre (or the tuned one),
	// quickening toward lock to the car's lock-to-lock turns.
	const FOsdsVehicleSetup Setup = GetVehicleSetup();
	if (Setup.SteeringRatio > 0.0)
	{
		// The rack is positive to the right; the model's steer to the left.
		OutState.SteeringWheelRad = static_cast<float>(-OsdsSteering::HandWheelRad(FMath::DegreesToRadians(Telemetry.SteeringAngleDeg),
			Setup.SteeringRatio, Setup.ParkingRackAngleRad, Setup.SteeringLockTurns));
		OutState.bHasSteeringWheel = true;
	}
	return true;
}
