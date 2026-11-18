// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Gameplay/OsdsCameraModels.h"
#include "Gameplay/OsdsSpeedUnits.h"
#include "OsdsHUD.generated.h"

class UFont;
class FOsdsDriveHud;
struct FOsdsDriveTelemetry;

/** HUD levels, cycled with F2 or the left-stick click (godot-final:game/Hud.gd).
 *  Off: only prompts. Glance: a translucent speed tag and g-meter that fade
 *  while cruising. Full: an opaque strip with every readout and the hints. */
enum class EOsdsHudLevel : uint8
{
	Off = 0,
	Glance = 1,
	Full = 2,
};

namespace OsdsHudRules
{
	constexpr float GlanceFaded = 0.4f;
	constexpr float GlanceFadeS = 0.35f;
	constexpr int32 SpeedWakeMph = 3;
	constexpr float WakeS = 2.5f;

	OSDS_API EOsdsHudLevel Next(EOsdsHudLevel Level);
	OSDS_API const TCHAR* Name(EOsdsHudLevel Level);
	/** Gear readout: R, N, 1..n; automatic shows D before the gear. */
	OSDS_API FString GearText(int32 Gear, bool bManual);
	/** Alpha of the Glance panel after DeltaTimeS: 1 while awake, fading to GlanceFaded. */
	OSDS_API float GlanceAlpha(float Current, bool bAwake, float DeltaTimeS);
}

/** The speed and engine numbers every HUD level shows, from one frame's telemetry. */
struct FOsdsHudReadout
{
	/** Ground speed along the car in the chosen units, rounded, never negative (0 at a stop). */
	int32 Speed = 0;
	EOsdsSpeedUnits Units = EOsdsSpeedUnits::Mph;
	/** The same speed in mph (the Glance panel wakes on a change of SpeedWakeMph). */
	int32 Mph = 0;
	/** The engine's own rpm (not a wheel's), to the nearest 50: idle at a stop, 0 only when stalled. */
	int32 Rpm = 0;

	/** The car body's velocity projected on its forward axis; the engine speed from the drivetrain. */
	OSDS_API static FOsdsHudReadout From(const FOsdsDriveTelemetry& Telemetry, EOsdsSpeedUnits Units);
};

/** Canvas HUD drawn in code (no UMG asset): speed, gear, rpm, a g-g meter
 *  with the car's published skidpad and braking limits as an ellipse. */
UCLASS()
class OSDS_API AOsdsHUD : public AHUD
{
	GENERATED_BODY()

public:
	AOsdsHUD();
	/** The 2.0.2 HUD (OsdsDriveHud.cpp) reads the frame's snapshot and draws the g-g meter. */
	friend class FOsdsDriveHud;

	virtual void DrawHUD() override;

	void CycleLevel();
	void SetLevel(EOsdsHudLevel InLevel);
	EOsdsHudLevel GetLevel() const { return Level; }
	/** Closer (-1) or wider (+1) minimap, five steps (MapHUD.gd). */
	void ZoomMinimap(int32 Step) { MinimapZoom = FMath::Clamp(MinimapZoom + Step, 0, 4); }
	void ShowMessage(const FString& Text, float Seconds = 3.f);
	const FString& GetMessage() const { return Message; }
	/** What the speed and rpm readouts show this frame (tests and captures compare it with the car). */
	const FOsdsHudReadout& GetReadout() const { return Readout; }

private:
	void DrawGlance(float Scale, float DeltaTimeS);
	void DrawFull(float Scale);
	void DrawMessage(float Scale);
	/** The co-driver's card for the notable building the car passes (UOsdsPlaceGraphSubsystem). */
	void DrawPlaceCard(float Scale);
	/** The roads round the car, heading up (MapHUD.gd). */
	void DrawMinimap(float Scale, float Alpha);
	/** While the car waits for a new place to be built. */
	void DrawLoading(float Scale);
	void DrawGMeter(const FVector2D& Centre, float Radius, float Alpha);
	void DrawRpmBar(float X, float Y, float W, float H, float Alpha);
	/** Roboto from the engine content, Bold or Regular, at PointSize (already scaled). */
	void DrawTextAt(const FString& Text, bool bBold, float PointSize, float X, float Y, const FLinearColor& Color, bool bRightAlign = false);
	FVector2D MeasureText(const FString& Text, bool bBold, float PointSize);
	void SampleVehicle(float DeltaTimeS);
	/** The drive robot's test pad is in the world (looked up once a second). */
	bool UpdateTestPad(double Now);
	/** A wide banner: the car is on the drive robot's test pad, not on the real road. */
	void DrawTestPadBanner(float Scale);

	UFont* HudFont(bool bBold);

	/** Engine Roboto duplicated per typeface (Regular, Bold) at a 24 pt base,
	 *  drawn scaled: runtime fonts rasterise at the drawn size, so text stays crisp. */
	UPROPERTY(Transient)
	TObjectPtr<UFont> RobotoRegular;
	UPROPERTY(Transient)
	TObjectPtr<UFont> RobotoBold;

	/** The guide-sign HUD and GPS (osds.Hud.Look 1); the drawing below is the earlier HUD. */
	TSharedPtr<FOsdsDriveHud> DriveHud;

	EOsdsHudLevel Level = EOsdsHudLevel::Glance;
	FString Message;
	float MessageTime = 0.f;
	/** Index into the minimap scales (MapHUD.gd starts at 1). */
	int32 MinimapZoom = 1;
	float GlanceAlphaValue = 1.f;
	float WakeTime = OsdsHudRules::WakeS;
	int32 ShownMph = 0;
	double LastDrawTime = -1.0;
	TWeakObjectPtr<AActor> TestPad;
	double NextTestPadSearch = 0.0;
	bool bOnTestPad = false;

	// Snapshot of the vehicle for this frame.
	bool bHasVehicle = false;
	FOsdsHudReadout Readout;
	int32 Mph = 0;
	int32 Gear = 0;
	bool bManual = false;
	/** The gear last drawn and when it last changed (real seconds), for the shift flash. */
	int32 DrawnGear = MIN_int32;
	double GearChangedAtS = -10.0;
	bool bAbs = false;
	/** Stability control acting: its light flashes, as on the dash. */
	bool bEsc = false;
	double Rpm = 0.0;
	double UpshiftRpm = 6700.0;
	double RedlineRpm = 7000.0;
	double SkidpadG = 0.89;
	double BrakingG = 0.95;
	FString CarName;
	FOsdsGMeter GMeter;
	TArray<FVector2D> GTrail;
	float TrailClock = 0.f;
};
