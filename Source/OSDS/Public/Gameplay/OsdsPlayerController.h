// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/OsdsDriverShaping.h"
#include "Gameplay/OsdsHaptics.h"
#include "Gameplay/OsdsVehicleControl.h"
#include "Gameplay/OsdsWheelDevice.h"
#include "OsdsPlayerController.generated.h"

class UOsdsInputConfig;
class SOsdsPlaceSearch;
class SOsdsRoadAtlas;
class SWidget;
struct FOsdsPlace;
struct FOsdsAtlasView;
class AOsdsHUD;
class FOsdsInputTapeDriver;
class AOsdsPlayerCameraManager;
class SOsdsPauseMenu;
class SOsdsRoutePlanner;
struct FOsdsMenuState;
enum class EOsdsMenuRow : uint8;

/**
 * The driver. Reads the Enhanced Input actions built in code, shapes them into
 * pedal positions and a rack angle (FOsdsDriverShaper), hands them to the
 * possessed IOsdsVehicleControl pawn every frame, and drives the pad's
 * rumble from the tyres' state. Discrete controls map to Godot's: C/Y camera,
 * F2/L3 HUD, T/B gearbox, E,Q/RB,X shifts, R/View recover, Tab/Menu atlas.
 */
UCLASS()
class OSDS_API AOsdsPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AOsdsPlayerController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

	bool HasVehicle() const { return GetVehicle() != nullptr; }
	IOsdsVehicleControl* GetVehicle() const;
	const FOsdsDriveTelemetry& GetTelemetry() const { return Telemetry; }
	const FOsdsVehicleSetup& GetCachedVehicleSetup() const { return Setup; }
	const FOsdsDriverCommand& GetLastCommand() const { return LastCommand; }
	const FOsdsDriverShaper& GetShaper() const { return Shaper; }
	const FOsdsRumble& GetLastRumble() const { return LastRumble; }
	/** Pad record and replay (-OsdsRecordInput, F9, -OsdsReplayInput=FILE). */
	const FOsdsInputTapeDriver* GetInputTape() const { return InputTape.Get(); }
	bool IsAbsActive() const { return Haptics.IsAbsActive(); }
	bool IsUsingGamepad() const { return bUsingGamepad; }
	UOsdsInputConfig* GetInputConfig() const { return InputConfig; }
	EOsdsRumbleBackend GetRumbleBackend() const { return RumbleBackend; }

	void ShowMessage(const FString& Text, float Seconds = 3.f);
	/** The gearbox refused a shift: the pad buzzes twice. */
	void NotifyShiftRefused() { Haptics.NotifyShiftRefused(); }

	/** Scripted end-to-end check of the input routing, like Godot's
	 *  checks/PadSteer.gd: injects stick, key and trigger events through the
	 *  real Enhanced Input stack and logs OSDS SHELL SELF-TEST PASSED/FAILED.
	 *  With Quit=1 the game exits afterwards: exit code 0 when it passes,
	 *  1 (a forced exit after the log is flushed) when it fails. */
	UFUNCTION(Exec)
	void OsdsShellSelfTest(int32 Quit = 0);

	/** Scripted drive for documentation captures: accelerate, corner, brake,
	 *  and screenshot each view and HUD level into Saved/Screenshots. */
	UFUNCTION(Exec)
	void OsdsShellCapture(int32 Quit = 0);

	/** The place search (/ key; X in the atlas): the game pauses while it is open.
	 *  From the atlas, B goes back to the atlas. */
	void OpenPlaceSearch(bool bGamepad, bool bFromAtlas = false);
	/** The road atlas (Tab, Menu). */
	void OpenRoadAtlas();
	/** Close the search or the atlas and give the drive back. */
	void CloseOverlays();
	bool IsOverlayOpen() const { return OverlayWidget.IsValid(); }
	TSharedPtr<SOsdsRoadAtlas> GetRoadAtlas() const { return AtlasPanel; }
	TSharedPtr<SOsdsPlaceSearch> GetPlaceSearch() const { return SearchPanel; }

	/** Simulated key and axis events through the real Enhanced Input stack (scripts and captures). */
	void InjectKey(const FKey& Key, bool bPressed);
	void InjectAxis(const FKey& Key, float Value);

private:
	FOsdsRawControls ReadRawControls() const;
	void UpdateRumble(float DeltaTime);
	void StopRumble();
	void TickSelfTest(float DeltaTime);
	void TickCapture(float DeltaTime);
	AOsdsHUD* GetSimHUD() const;
	void ShowOverlay(const TSharedRef<SWidget>& Widget, const TSharedPtr<SWidget>& Focus, bool bPause = true);
	void StartMenuInput();
	void StopMenuInput();
	void ChoosePlace(const FOsdsPlace& Place);
	/** The atlas's cockpit settings: 0 sound, 1 HUD level, 2 head motion, 3 rumble strength. */
	void ChangeSetting(int32 Index);
	bool SurpriseMe();
	FOsdsAtlasView AtlasView() const;
	AOsdsPlayerCameraManager* GetSimCamera() const;

public:
	void OnToggleTransmission();
private:
	void OnShiftUp();
	void OnShiftDown();
	void OnStartEngine();
	void OnCycleCamera();
	void OnCycleHud();
	void OnToggleAtlas();
	void OnFindPlace();
	void OnRecover();
	void OnToggleSound();
	/** D-pad Right / F4: the tuning menu, standing still. */
	void OnTuning();

public:
	/** Open the tuning menu for the car being driven (tests call it directly). */
	void OpenTuning();
	TSharedPtr<class SOsdsTuningMenu> GetTuningMenu() const { return TuningPanel; }
	/** Open the seat and view menu over the live driver's view. */
	void OpenSeatMenu();
	TSharedPtr<class SOsdsSeatMenu> GetSeatMenu() const { return SeatPanel; }

	/** The pause menu (Menu, Tab) and the GPS screens: OsdsPlayerControllerMenu.cpp. */
	void OpenPauseMenu(int32 Page = 0, int32 Row = 0);
	/** Leave a screen the pause menu opened: B goes back to the menu, on the row that opened it;
	 *  Menu (Start), or a screen opened from the drive, goes back to the drive. */
	void LeaveSubScreen();
	/** The last input was the pad (true) or the keyboard and mouse (false): the hints and the
	 *  mouse cursor follow it (the menu input watcher calls it). */
	void NotePadUse(bool bPad);
	/** Pad focus back onto the open screen when it was lost (the menu input watcher calls it). */
	void RefocusOverlay();
	/** Is the pad's Menu (Start) button down now? */
	bool IsMenuButtonDown() const;
	void OpenRoutePlanner();
	void OpenFamousRoads();
	/** Race tracks: Oval tracks or Road courses, then the track (OsdsTracks). */
	void OpenRaceTracks();
	void OpenTrackList(bool bOvals);
	void OpenWhereTo();
	/** "Quit the game?": Keep driving (first, so a stray A keeps you driving) or Quit to the desktop; B goes back. */
	void OpenQuitConfirm();
	TSharedPtr<SOsdsPauseMenu> GetPauseMenu() const { return PausePanel; }
	TSharedPtr<SOsdsRoutePlanner> GetRoutePlanner() const { return PlannerPanel; }
	/** What a pause menu row does (Dir 0 choose, -1 / +1 change, 2 back to stock). */
	void MenuAction(EOsdsMenuRow Row, int32 Dir);
	FOsdsMenuState MenuState() const;

	/** Engine sound on (M toggles it). */
	bool IsSoundOn() const { return bSoundOn; }

protected:
	void OnToggleLights();
	void OnCycleLens();
	void OnToggleStats();

	UPROPERTY(Transient)
	TObjectPtr<UOsdsInputConfig> InputConfig;

	FOsdsDriverShaper Shaper;
	TSharedPtr<FOsdsInputTapeDriver> InputTape;
	FOsdsHapticsModel Haptics;
	/** A USB racing wheel, read each frame (nothing happens without one). */
	FOsdsWheelDevice WheelDevice;
	/** Read the wheel into Raw and drive its force feedback from the last frame's telemetry. */
	void UpdateWheel(FOsdsRawControls& Raw, float DeltaTime);
	bool bWheelSeen = false;
	FOsdsVehicleSetup Setup;
	FOsdsDriveTelemetry Telemetry;
	FOsdsDriverCommand LastCommand;
	FOsdsRumble LastRumble;
	EOsdsRumbleBackend RumbleBackend = EOsdsRumbleBackend::XInput;
	uint64 RumbleHandles[4] = {0, 0, 0, 0};
	bool bUsingGamepad = false;
	bool bSoundOn = true;
	bool bLightsOn = false;
	bool bStatsShown = false;
	bool bPreferencesApplied = false;
	TSharedPtr<SWidget> OverlayWidget;
	TWeakPtr<SWidget> OverlayFocus;
	/** Watches every pad, key and mouse event before the screens get it (OsdsPlayerControllerMenu.cpp). */
	TSharedPtr<class FOsdsMenuInput> MenuInput;
	/** The pause menu page and row that opened the screen now on show, -1 for none (opened from the drive). */
	int32 ReturnPage = -1;
	int32 ReturnRow = 0;
	TSharedPtr<SOsdsPlaceSearch> SearchPanel;
	TSharedPtr<SOsdsRoadAtlas> AtlasPanel;
	TSharedPtr<class SOsdsTuningMenu> TuningPanel;
	TSharedPtr<class SOsdsSeatMenu> SeatPanel;
	/** A tuning saved on the move, waiting for the stop. */
	bool bTuningWaits = false;
	bool bMenuHintShown = false;
	/** The car whose seat the camera has (reloaded when the car changes). */
	FString SeatCarId;
	TSharedPtr<SOsdsPauseMenu> PausePanel;
	TSharedPtr<SOsdsRoutePlanner> PlannerPanel;
	/** Famous roads, and what to do with a found place. */
	TSharedPtr<SWidget> ListPanel;
	int32 MenuPage = 0;
	FString PendingMessage;
	float PendingMessageSeconds = 0.f;

	// Self-test state.
	int32 SelfTestStage = -1;
	float SelfTestClock = 0.f;
	bool bSelfTestQuit = false;
	FString SelfTestFailure;
	int32 SelfTestViewBefore = 0;
	int32 SelfTestHudBefore = 0;

	// Capture script state.
	float CaptureClock = -1.f;
	int32 CaptureShot = 0;
	int32 CaptureTraceTick = -1;
	bool bCaptureQuit = false;
	bool bCaptureParkingBrake = false;
};
