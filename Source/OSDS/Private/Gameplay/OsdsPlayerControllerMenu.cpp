// Copyright OSDS contributors. All Rights Reserved.

// The pause menu and the GPS screens of AOsdsPlayerController (2.0.2). The
// rows call the controller's own settings, with the same console variables and
// preference keys as the road atlas's settings.

#include "Gameplay/OsdsPlayerController.h"

#include "Engine/World.h"
#include "Environment/OsdsGraphics.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Gameplay/OsdsAtlasUI.h"
#include "Gameplay/OsdsDriveHud.h"
#include "Gameplay/OsdsFamousRoads.h"
#include "Gameplay/OsdsFeatures.h"
#include "Gameplay/OsdsGameMode.h"
#include "Gameplay/OsdsHUD.h"
#include "Gameplay/OsdsPauseMenu.h"
#include "Gameplay/OsdsPlayerCameraManager.h"
#include "Gameplay/OsdsPreferences.h"
#include "Gameplay/OsdsRoutePlanner.h"
#include "Gameplay/OsdsRouteSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Live/OsdsLiveWorldSubsystem.h"
#include "Vehicle/OsdsVehicle.h"

namespace
{
	bool MenuAutoClutchOn()
	{
		const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("osds.Gearbox.AutoClutch"));
		return !Var || Var->GetInt() != 0;
	}

	/** The driving update's traffic switch (ChangeSetting 13) and seat menu (OpenSeatMenu) are on main. */
	constexpr bool bTrafficSettingReady() { return true; }
	constexpr bool bSeatMenuReady() { return true; }

	float MenuVar(const TCHAR* Name, float Default = 1.f)
	{
		const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name);
		return Var ? Var->GetFloat() : Default;
	}

	/** One step up or down a ladder of values (no wrap), remembered under the atlas's key. */
	void StepLadder(const TCHAR* Name, const TArray<float>& Ladder, int32 Dir, float Stock, const TCHAR* Key, float PrefScale)
	{
		const float Now = MenuVar(Name);
		int32 Index = 0;
		for (int32 i = 0; i < Ladder.Num(); ++i) if (FMath::Abs(Ladder[i] - Now) < FMath::Abs(Ladder[Index] - Now)) Index = i;
		float Next = Ladder[FMath::Clamp(Index + (Dir > 0 ? 1 : Dir < 0 ? -1 : 0), 0, Ladder.Num() - 1)];
		if (Dir == 2 && Stock >= 0.f) Next = Stock;
		if (Dir == 0) Next = Ladder[(Index + 1) % Ladder.Num()];
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name)) Var->Set(Next, ECVF_SetByCode);
		OsdsPreferences::SetInt(Key, FMath::RoundToInt(Next * PrefScale));
	}

	float Along(const TArray<float>& Ladder, float Value)
	{
		return Ladder.Num() > 1 ? FMath::Clamp((Value - Ladder[0]) / (Ladder.Last() - Ladder[0]), 0.f, 1.f) : 0.f;
	}

	const TArray<float>& HeadLadder() { static const TArray<float> L = { 0.f, 0.25f, 0.5f, 0.75f, 1.f }; return L; }
	const TArray<float>& RumbleLadder() { static const TArray<float> L = { 0.f, 0.3f, 0.6f, 1.f }; return L; }
	const TArray<float>& SteerLadder() { static const TArray<float> L = { 0.6f, 0.8f, 1.f, 1.25f, 1.5f }; return L; }
	const TArray<float>& DeadLadder() { static const TArray<float> L = { 0.f, 0.05f, 0.1f, 0.15f }; return L; }
	const TArray<float>& ScreenLadder() { static const TArray<float> L = { 13.3f, 14.f, 15.6f, 17.3f, 24.f, 27.f, 32.f, 43.f, 55.f, 65.f }; return L; }
	const TArray<float>& EyeLadder() { static const TArray<float> L = { 40.f, 50.f, 60.f, 70.f, 80.f, 100.f, 150.f, 200.f, 250.f }; return L; }

	float LadderFraction(const TArray<float>& Ladder, float Value)
	{
		// Even steps along the track, whatever the values.
		int32 Index = 0;
		for (int32 i = 0; i < Ladder.Num(); ++i) if (FMath::Abs(Ladder[i] - Value) < FMath::Abs(Ladder[Index] - Value)) Index = i;
		return Ladder.Num() > 1 ? static_cast<float>(Index) / (Ladder.Num() - 1) : 0.f;
	}
}

/**
 * Sees every pad, key and mouse event before Slate routes it: which device the player
 * uses now (the hints and the mouse cursor follow it), whether the pad's Menu button
 * is down (Menu closes every screen, B goes back one), and, while a screen is open,
 * puts the pad's focus back on it if it was lost, so a pad press always reaches it.
 * It never takes an event.
 */
class FOsdsMenuInput : public IInputProcessor
{
public:
	explicit FOsdsMenuInput(AOsdsPlayerController* InOwner) : Owner(InOwner) {}

	bool bMenuDown = false;

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		AOsdsPlayerController* PC = Owner.Get();
		if (!PC) return false;
		const bool bPad = InKeyEvent.GetKey().IsGamepadKey();
		if (InKeyEvent.GetKey() == EKeys::Gamepad_Special_Right) bMenuDown = true;
		PC->NotePadUse(bPad);
		if (bPad && PC->IsOverlayOpen()) PC->RefocusOverlay();
		return false;
	}

	virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		if (InKeyEvent.GetKey() == EKeys::Gamepad_Special_Right) bMenuDown = false;
		return false;
	}

	virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override
	{
		AOsdsPlayerController* PC = Owner.Get();
		if (PC && FMath::Abs(InAnalogInputEvent.GetAnalogValue()) > 0.5f && InAnalogInputEvent.GetKey().IsGamepadKey())
		{
			PC->NotePadUse(true);
			if (PC->IsOverlayOpen()) PC->RefocusOverlay();
		}
		return false;
	}

	virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
	{
		AOsdsPlayerController* PC = Owner.Get();
		if (PC && OsdsTheme::IsRealMouseMove(MouseEvent)) PC->NotePadUse(false);
		return false;
	}

	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
	{
		if (AOsdsPlayerController* PC = Owner.Get()) PC->NotePadUse(false);
		return false;
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("OsdsMenuInput"); }

private:
	TWeakObjectPtr<AOsdsPlayerController> Owner;
};

void AOsdsPlayerController::StartMenuInput()
{
	OsdsTheme::EnsurePadWatcher();
	if (MenuInput.IsValid() || !FSlateApplication::IsInitialized()) return;
	MenuInput = MakeShared<FOsdsMenuInput>(this);
	FSlateApplication::Get().RegisterInputPreProcessor(MenuInput);
}

void AOsdsPlayerController::StopMenuInput()
{
	if (MenuInput.IsValid() && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(MenuInput);
	MenuInput.Reset();
}

bool AOsdsPlayerController::IsMenuButtonDown() const
{
	return MenuInput.IsValid() && MenuInput->bMenuDown;
}

void AOsdsPlayerController::NotePadUse(bool bPad)
{
	bUsingGamepad = bPad;
	OsdsTheme::SetPadInUse(bPad);
	// The cursor shows for the mouse and hides while the pad drives the screens.
	if (IsOverlayOpen()) bShowMouseCursor = !bPad;
}

void AOsdsPlayerController::RefocusOverlay()
{
	const TSharedPtr<SWidget> Overlay = OverlayWidget;
	if (!Overlay.IsValid() || !FSlateApplication::IsInitialized()) return;
	FSlateApplication& App = FSlateApplication::Get();
	const TSharedPtr<SWidget> Focused = App.GetUserFocusedWidget(0);
	// Still on the screen (the screen itself or a widget in it): nothing to do.
	if (Focused.IsValid() && (Focused == Overlay || Overlay->HasFocusedDescendants() || Overlay->HasAnyUserFocus().IsSet())) return;
	const TSharedPtr<SWidget> Target = OverlayFocus.IsValid() ? OverlayFocus.Pin() : Overlay;
	OsdsTheme::FocusPad(Target);
}

void AOsdsPlayerController::LeaveSubScreen()
{
	const int32 Page = ReturnPage, Row = ReturnRow;
	ReturnPage = -1;
	if (Page >= 0 && !IsMenuButtonDown())
	{
		OpenPauseMenu(Page, Row);
		return;
	}
	CloseOverlays();
}

FOsdsMenuState AOsdsPlayerController::MenuState() const
{
	FOsdsMenuState State;
	State.bPad = bUsingGamepad;
	const FOsdsAtlasView View = AtlasView();
	const AOsdsGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AOsdsGameMode>() : nullptr;
	const FOsdsPlace Place = Mode ? Mode->GetPlace() : FOsdsPlace();
	// "Ocean Drive, Miami Beach · Art Deco District": the name before the dot, the rest after it.
	FString Name = Place.Title, Rest;
	if (!Place.Title.Split(TEXT(" · "), &Name, &Rest)) Rest.Reset();
	State.Place = Name;
	double Lon = Place.Lon, Lat = Place.Lat;
	if (View.bHasCar && View.Frame.IsValid()) View.Frame.WorldCmToLonLat(View.CarCm, Lon, Lat);
	FString Where = OsdsRouteText::Clock(FOsdsDriveHud::LocalTime(GetWorld(), Lon));
	if (const UOsdsLiveWorldSubsystem* Live = UOsdsLiveWorldSubsystem::Get(GetWorld()))
	{
		if (Live->GetWeather().bValid) Where += FString::Printf(TEXT(", %d F"), FMath::RoundToInt(Live->GetWeather().TemperatureC * 9.f / 5.f + 32.f));
	}
	State.Where = Rest.IsEmpty() ? Where : Rest + TEXT(", ") + Where;

	auto Set = [&State](EOsdsMenuRow Row, const FString& Text, const FString& Unit = FString(), float Fraction = 0.f, float Stock = -1.f)
	{
		FOsdsMenuValue& V = State.Values.Add(Row);
		V.Text = Text; V.Unit = Unit; V.Fraction = Fraction; V.Stock = Stock;
		return &V;
	};
	// The atlas's settings text is "Name · Value".
	auto AfterDot = [&View](int32 Index) { FString L, R; return View.Settings.IsValidIndex(Index) && View.Settings[Index].Split(TEXT(" · "), &L, &R) ? R : FString(); };

	const float Cars = MenuVar(TEXT("osds.Traffic.Cars"), 20.f);
	// The driving update brings the traffic switch (ChangeSetting 13); until then the row says so.
	Set(EOsdsMenuRow::Traffic, Cars < 0.5f ? TEXT("Off") : Cars < 12.f ? TEXT("Light") : TEXT("Normal"))->bUnavailable = !bTrafficSettingReady();
	Set(EOsdsMenuRow::Car, View.CarName);
	Set(EOsdsMenuRow::Transmission, View.Transmission);
	Set(EOsdsMenuRow::Stability, AfterDot(15));
	Set(EOsdsMenuRow::Lights, bLightsOn ? TEXT("On") : TEXT("Auto"));
	const AOsdsPlayerCameraManager* Camera = Cast<AOsdsPlayerCameraManager>(PlayerCameraManager);
	FOsdsMenuValue* Lens = Set(EOsdsMenuRow::Lens, AfterDot(4));
	Lens->Chosen = !Camera ? 0 : Camera->GetLens() == EOsdsLens::Screen ? 1 : Camera->GetLens() == EOsdsLens::Wide ? 2 : 0;
	const float Head = MenuVar(TEXT("osds.Camera.HeadMotion"));
	Set(EOsdsMenuRow::HeadMotion, FString::FromInt(FMath::RoundToInt(Head * 100.f)), TEXT("%"), Along(HeadLadder(), Head), 1.f);
	const float Screen = MenuVar(TEXT("osds.Lens.ScreenInches"), 15.6f);
	Set(EOsdsMenuRow::ScreenSize, FString::Printf(TEXT("%.1f"), Screen), TEXT("in"), LadderFraction(ScreenLadder(), Screen));
	const float Eye = MenuVar(TEXT("osds.Lens.ViewDistanceCm"), 60.f);
	Set(EOsdsMenuRow::EyeDistance, FString::FromInt(FMath::RoundToInt(Eye)), TEXT("cm"), LadderFraction(EyeLadder(), Eye));
	Set(EOsdsMenuRow::Seat, FString())->bUnavailable = !bSeatMenuReady();
	Set(EOsdsMenuRow::Sound, bSoundOn ? TEXT("On") : TEXT("Off"));
	Set(EOsdsMenuRow::HudLevel, AfterDot(1));
	Set(EOsdsMenuRow::HudMap, FOsdsDriveHud::IsMapAlways() ? TEXT("Always") : TEXT("With route"));
	Set(EOsdsMenuRow::SpeedUnits, OsdsSpeedUnits::Label(OsdsSpeedUnits::Get()))->Chosen = OsdsSpeedUnits::Get() == EOsdsSpeedUnits::Kmh ? 1 : 0;
	const float Rumble = MenuVar(TEXT("osds.Rumble.Strength"));
	Set(EOsdsMenuRow::Rumble, FString::FromInt(FMath::RoundToInt(Rumble * 100.f)), TEXT("%"), Along(RumbleLadder(), Rumble), 1.f);
	const float Steer = MenuVar(TEXT("osds.Pad.SteerSpeed"));
	Set(EOsdsMenuRow::StickSpeed, FString::FromInt(FMath::RoundToInt(Steer * 100.f)), TEXT("%"), Along(SteerLadder(), Steer), Along(SteerLadder(), 1.f));
	Set(EOsdsMenuRow::StickResponse, AfterDot(8));
	const float Dead = MenuVar(TEXT("osds.Pad.SteerDeadZone"), 0.f);
	Set(EOsdsMenuRow::StickDeadZone, FString::FromInt(FMath::RoundToInt(100.f * (0.12f + 0.88f * Dead))), TEXT("%"), Along(DeadLadder(), Dead), 0.f);
	Set(EOsdsMenuRow::Trigger, AfterDot(10));
	FOsdsMenuValue* Graphics = Set(EOsdsMenuRow::Graphics, FOsdsGraphics::ChoiceDisplay());
	Graphics->Chosen = static_cast<int32>(FOsdsGraphics::SavedChoice());

	if (const UOsdsRouteSubsystem* Gps = UOsdsRouteSubsystem::Get(GetWorld()); Gps && Gps->HasRoute())
	{
		const FOsdsRoute& Route = Gps->GetRoute();
		State.bRoute = true;
		const TArray<FOsdsRouteStop>& Stops = Route.GetStops();
		const FString First = Stops.Num() > 1 ? Stops[1].Title : FString();
		State.RouteTitle = Stops.Num() > 2 ? FString::Printf(TEXT("%s to %s"), *First, *Stops.Last().Title) : FString::Printf(TEXT("To %s"), *First);
		double Left = Route.GetProgress().LegRemainingM, LeftS = Route.GetProgress().LegRemainingS;
		for (int32 i = Route.GetActiveLeg() + 1; i < Route.GetLegs().Num(); ++i) { Left += Route.GetLegs()[i].LengthM; LeftS += Route.GetLegs()[i].DurationS; }
		const int32 StopsLeft = Stops.Num() - 1 - Route.GetActiveLeg();
		State.RouteSummary = FString::Printf(TEXT("%s, %s, %d %s to go"), *OsdsRouteText::Distance(Left), *OsdsRouteText::Duration(LeftS), StopsLeft, StopsLeft == 1 ? TEXT("stop") : TEXT("stops"));
		for (int32 i = Route.GetActiveLeg(); i < Route.GetLegs().Num(); ++i)
		{
			State.RouteLegs.Add(Route.GetLegs()[i].M);
			if (Route.GetLegs()[i].M.Num()) State.RouteStops.Add(Route.GetLegs()[i].M.Last());
		}
		Gps->GetCarM(State.CarM);
	}
	return State;
}

void AOsdsPlayerController::MenuAction(EOsdsMenuRow Row, int32 Dir)
{
	AOsdsGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AOsdsGameMode>() : nullptr;
	UOsdsRouteSubsystem* Gps = UOsdsRouteSubsystem::Get(GetWorld());
	// A screen this row opens comes back here with B.
	if (Dir == 0 && PausePanel.IsValid())
	{
		ReturnPage = PausePanel->GetPage();
		ReturnRow = PausePanel->GetSelected();
	}
	switch (Row)
	{
	case EOsdsMenuRow::Continue: CloseOverlays(); break;
	case EOsdsMenuRow::WhereTo: OpenWhereTo(); break;
	case EOsdsMenuRow::PlanRoute: OpenRoutePlanner(); break;
	case EOsdsMenuRow::FamousRoads: OpenFamousRoads(); break;
	case EOsdsMenuRow::Atlas: OpenRoadAtlas(); break;
	case EOsdsMenuRow::Quit: OpenQuitConfirm(); break;
	case EOsdsMenuRow::EndRoute:
		if (Gps) Gps->ClearRoute();
		ShowMessage(TEXT("Route ended"), 2.f);
		break;
	case EOsdsMenuRow::Traffic:
		// The row is hidden while traffic is off for the release (OsdsFeatures).
		if (!OsdsFeatures::IsTrafficEnabled()) break;
		if (bTrafficSettingReady()) ChangeSetting(13);
		else ShowMessage(TEXT("The traffic setting comes with the next driving update"), 3.f);
		break;
	case EOsdsMenuRow::Car: if (Mode) Mode->CycleCar(this); break;
	case EOsdsMenuRow::Transmission:
		// Automatic -> Manual with the automatic clutch -> Manual with your clutch -> Automatic.
		if (Telemetry.bManualTransmission && MenuAutoClutchOn())
		{
			ChangeSetting(14);
			ShowMessage(TEXT("Manual · hold A (Shift) for the clutch, RB / LB (E / Q) to shift"), 4.f);
		}
		else
		{
			if (!Telemetry.bManualTransmission && !MenuAutoClutchOn())
			{
				ChangeSetting(14);
			}
			OnToggleTransmission();
		}
		break;
	case EOsdsMenuRow::Stability:
		ChangeSetting(15);
		ShowMessage(MenuVar(TEXT("osds.Assists.StabilityControl")) > 0.5f ? TEXT("Stability control on") : TEXT("Stability control off · you catch the slides"), 3.f);
		break;
	case EOsdsMenuRow::Lights: OnToggleLights(); break;
	case EOsdsMenuRow::Tune:
		if (FMath::Abs(Telemetry.BodyVelocityMps.X) > 1.0) ShowMessage(TEXT("Stop the car to tune it"), 2.f);
		else OpenTuning();
		break;
	case EOsdsMenuRow::Seat:
		OpenSeatMenu();
		break;
	case EOsdsMenuRow::Lens:
	{
		AOsdsPlayerCameraManager* Camera = GetSimCamera();
		if (!Camera) break;
		const int32 Now = Camera->GetLens() == EOsdsLens::Screen ? 1 : Camera->GetLens() == EOsdsLens::Wide ? 2 : 0;
		const int32 Want = Dir < 0 ? FMath::Max(Now - 1, 0) : Dir > 0 ? FMath::Min(Now + 1, 2) : (Now + 1) % 3;
		// The camera cycles Natural, True FOV, Wide.
		for (int32 i = 0; i < 3; ++i)
		{
			const int32 Lens = Camera->GetLens() == EOsdsLens::Screen ? 1 : Camera->GetLens() == EOsdsLens::Wide ? 2 : 0;
			if (Lens == Want) break;
			Camera->CycleLens();
		}
		OsdsPreferences::SetInt(TEXT("lens_v3"), static_cast<int32>(Camera->GetLens()));
		break;
	}
	case EOsdsMenuRow::HeadMotion: StepLadder(TEXT("osds.Camera.HeadMotion"), HeadLadder(), Dir, 1.f, TEXT("head_motion"), 100.f); break;
	case EOsdsMenuRow::ScreenSize: StepLadder(TEXT("osds.Lens.ScreenInches"), ScreenLadder(), Dir, -1.f, TEXT("screen_tenths_inch"), 10.f); break;
	case EOsdsMenuRow::EyeDistance: StepLadder(TEXT("osds.Lens.ViewDistanceCm"), EyeLadder(), Dir, -1.f, TEXT("view_distance_cm"), 1.f); break;
	case EOsdsMenuRow::Sound: OnToggleSound(); break;
	case EOsdsMenuRow::HudLevel: OnCycleHud(); break;
	case EOsdsMenuRow::HudMap:
		// Two values: left is With route, right is Always, A switches.
		if ((Dir < 0 && !FOsdsDriveHud::IsMapAlways()) || (Dir > 0 && FOsdsDriveHud::IsMapAlways())) break;
		FOsdsDriveHud::SetMapAlways(!FOsdsDriveHud::IsMapAlways());
		break;
	case EOsdsMenuRow::SpeedUnits:
		// Left picks mph, right km/h, A switches.
		if ((Dir < 0 && OsdsSpeedUnits::Get() == EOsdsSpeedUnits::Mph) || (Dir > 0 && OsdsSpeedUnits::Get() == EOsdsSpeedUnits::Kmh)) break;
		OsdsSpeedUnits::Toggle();
		ShowMessage(FString::Printf(TEXT("Speed in %s"), OsdsSpeedUnits::Label(OsdsSpeedUnits::Get())), 2.f);
		break;
	case EOsdsMenuRow::Rumble: StepLadder(TEXT("osds.Rumble.Strength"), RumbleLadder(), Dir, 1.f, TEXT("rumble"), 100.f); break;
	case EOsdsMenuRow::StickSpeed: StepLadder(TEXT("osds.Pad.SteerSpeed"), SteerLadder(), Dir, 1.f, TEXT("pad_steer_speed"), 100.f); break;
	case EOsdsMenuRow::StickDeadZone: StepLadder(TEXT("osds.Pad.SteerDeadZone"), DeadLadder(), Dir, 0.f, TEXT("pad_steer_dead_zone"), 100.f); break;
	case EOsdsMenuRow::StickResponse: ChangeSetting(8); break;
	case EOsdsMenuRow::Trigger: ChangeSetting(10); break;
	case EOsdsMenuRow::Graphics: FOsdsGraphics::ChooseQuality(Dir); break;
	default: break;
	}
}

void AOsdsPlayerController::OpenPauseMenu(int32 Page, int32 Row)
{
	CloseOverlays();
	MenuPage = Page;
	PausePanel = SNew(SOsdsPauseMenu)
		.Page(Page)
		.Row(Row)
		.GetState_Lambda([this]() { return MenuState(); })
		.OnAction_Lambda([this](EOsdsMenuRow Row, int32 Dir) { MenuAction(Row, Dir); })
		.OnPage_Lambda([this](int32 P)
		{
			MenuPage = P;
			// Seat & view keeps the world running so the view can be judged.
			if (PausePanel.IsValid()) SetPause(P != 2);
		})
		.OnClose_Lambda([this]() { ReturnPage = -1; CloseOverlays(); });
	ShowOverlay(PausePanel.ToSharedRef(), PausePanel);
	SetPause(Page != 2);
}

void AOsdsPlayerController::OpenRoutePlanner()
{
	CloseOverlays();
	PlannerPanel = SNew(SOsdsRoutePlanner)
		.GetView_Lambda([this]() { return AtlasView(); })
		.Gps(UOsdsRouteSubsystem::Get(GetWorld()))
		.bPad(bUsingGamepad)
		.OnStart_Lambda([this]()
		{
			UOsdsRouteSubsystem* Gps = UOsdsRouteSubsystem::Get(GetWorld());
			if (!Gps || Gps->DraftStops.Num() == 0)
			{
				ShowMessage(TEXT("Add a stop first"), 2.f);
				return;
			}
			const TArray<FOsdsRouteStop> Stops = Gps->DraftStops;
			CloseOverlays();
			Gps->StartRoute(Stops);
		})
		.OnBack_Lambda([this]() { LeaveSubScreen(); });
	ShowOverlay(PlannerPanel.ToSharedRef(), PlannerPanel);
}

void AOsdsPlayerController::OpenFamousRoads()
{
	CloseOverlays();
	TArray<SOsdsListMenu::FItem> Items;
	for (const FOsdsFamousRoad& Road : OsdsFamousRoads::All())
	{
		SOsdsListMenu::FItem Item;
		Item.Icon = OsdsTheme::EIcon::Star;
		Item.Title = Road.Title;
		Item.Note = Road.Note;
		Item.Value = OsdsRouteText::Distance(Road.LengthMi * 1609.344);
		Items.Add(Item);
	}
	TSharedRef<SOsdsListMenu> List = SNew(SOsdsListMenu)
		.Title(TEXT("Famous roads"))
		.Help(TEXT("Go to the start. The route to the end starts when the road is ready."))
		.Items(Items)
		.bPad(bUsingGamepad)
		.OnChoose_Lambda([this](int32 Index)
		{
			const TArray<FOsdsFamousRoad>& All = OsdsFamousRoads::All();
			if (!All.IsValidIndex(Index)) return;
			const FOsdsFamousRoad Road = All[Index];
			ChoosePlace(Road.Start);
			if (UOsdsRouteSubsystem* Gps = UOsdsRouteSubsystem::Get(GetWorld())) Gps->StartRouteAfterTravel(Road.Stops);
		})
		.OnBack_Lambda([this]() { LeaveSubScreen(); });
	ListPanel = List;
	ShowOverlay(List, List);
}

void AOsdsPlayerController::OpenQuitConfirm()
{
	CloseOverlays();
	TArray<SOsdsListMenu::FItem> Items;
	SOsdsListMenu::FItem Stay;
	Stay.Icon = OsdsTheme::EIcon::Drive;
	Stay.Title = TEXT("Keep driving");
	Stay.Note = TEXT("Back to the menu");
	Items.Add(Stay);
	SOsdsListMenu::FItem Quit;
	Quit.Icon = OsdsTheme::EIcon::Back;
	Quit.Title = TEXT("Quit to the desktop");
	Quit.Note = TEXT("Close the game");
	Items.Add(Quit);
	TSharedRef<SOsdsListMenu> Confirm = SNew(SOsdsListMenu)
		.Title(TEXT("Quit the game?"))
		.Help(TEXT("Your settings and car setups are already saved."))
		.Items(Items)
		.bPad(bUsingGamepad)
		.OnChoose_Lambda([this](int32 Index)
		{
			if (Index == 1)
			{
				UE_LOG(LogTemp, Display, TEXT("Quit to the desktop from the pause menu"));
				UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
				return;
			}
			LeaveSubScreen();
		})
		.OnBack_Lambda([this]() { LeaveSubScreen(); });
	ListPanel = Confirm;
	ShowOverlay(Confirm, Confirm);
}

void AOsdsPlayerController::OpenWhereTo()
{
	CloseOverlays();
	SearchPanel = SNew(SOsdsPlaceSearch)
		.OnChoose_Lambda([this](const FOsdsPlace& Place)
		{
			// Go there now, or make it the GPS's stop.
			const FOsdsPlace Found = Place;
			TArray<SOsdsListMenu::FItem> Items;
			SOsdsListMenu::FItem Go;
			Go.Icon = OsdsTheme::EIcon::Map;
			Go.Title = TEXT("Go there now");
			Go.Note = TEXT("Jump to the place and drive from there");
			Items.Add(Go);
			SOsdsListMenu::FItem Stop;
			Stop.Icon = OsdsTheme::EIcon::Route;
			Stop.Title = TEXT("Make it your stop");
			Stop.Note = TEXT("The GPS takes you there on the roads");
			Stop.bCourse = true;
			Items.Add(Stop);
			CloseOverlays();
			TSharedRef<SOsdsListMenu> Choice = SNew(SOsdsListMenu)
				.Title(Found.Title)
				.Help(FString::Printf(TEXT("%.5f, %.5f"), Found.Lat, Found.Lon))
				.Items(Items)
				.bPad(bUsingGamepad)
				.OnChoose_Lambda([this, Found](int32 Index)
				{
					if (Index == 0) { ChoosePlace(Found); return; }
					CloseOverlays();
					FOsdsRouteStop Stop;
					Stop.Title = Found.Title;
					Found.Title.Split(TEXT(","), &Stop.Title, nullptr);
					Stop.Lon = Found.Lon;
					Stop.Lat = Found.Lat;
					if (UOsdsRouteSubsystem* Gps = UOsdsRouteSubsystem::Get(GetWorld())) Gps->StartRoute({ Stop });
				})
				.OnBack_Lambda([this]() { if (IsMenuButtonDown()) CloseOverlays(); else OpenWhereTo(); });
			ListPanel = Choice;
			ShowOverlay(Choice, Choice);
		})
		.OnSurprise_Lambda([this]() { return SurpriseMe(); })
		.OnClose_Lambda([this]() { LeaveSubScreen(); });
	ShowOverlay(SearchPanel.ToSharedRef(), SearchPanel->FocusTarget(bUsingGamepad));
}
