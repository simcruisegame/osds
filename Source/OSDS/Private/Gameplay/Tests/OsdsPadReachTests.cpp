// Copyright OSDS contributors. All Rights Reserved.

// OSDS.UI.PadReachAll: every control on every menu screen is reached and used with an
// Xbox pad alone. The screens sit in a virtual Slate window and the pad's buttons go in as
// key events through FSlateApplication (ProcessKeyDownEvent / ProcessKeyUpEvent, user 0),
// the way XInput sends them, so Slate's own focus, bubbling and buttons run. After every
// press the test asks Slate which widget has the pad's focus.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Gameplay/OsdsAtlasUI.h"
#include "Gameplay/OsdsFeatures.h"
#include "Gameplay/OsdsInputConfig.h"
#include "Gameplay/OsdsRoutePlanner.h"
#include "Misc/Crc.h"
#include "Gameplay/OsdsPauseMenu.h"
#include "Gameplay/OsdsSeatUI.h"
#include "Gameplay/OsdsTuningUI.h"
#include "Gameplay/OsdsUiTheme.h"
#include "Widgets/SWindow.h"

namespace OsdsPadReachTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	/**
	 * A screen in a hidden 1920 x 1080 Slate window, driven by pad buttons. Each press goes in
	 * through FSlateApplication like XInput's; where the headless editor does not route it to
	 * the window (no OS window behind it), the same event goes along the focused widget's path
	 * as Slate does it: OnPreviewKeyDown from the window down, then OnKeyDown back up.
	 */
	/** Does FSlateApplication route pad keys to the test's window here (the first press finds out)? */
	static bool GSlateRoutes = true;

	struct FPadScreen
	{
		TSharedRef<SWindow> Window;
		TSharedPtr<SWidget> Screen;
		int32 Routed = 0;
		int32 Walked = 0;

		explicit FPadScreen(const TSharedRef<SWidget>& InScreen)
			: Window(SNew(SWindow).ClientSize(FVector2D(1920.0, 1080.0)).IsInitiallyMaximized(false).FocusWhenFirstShown(false))
			, Screen(InScreen)
		{
			Window->SetContent(InScreen);
			FSlateApplication::Get().AddWindow(Window, false);
			Window->SlatePrepass(1.f);
			OsdsTheme::FocusPad(InScreen);
			Tick();
		}

		~FPadScreen()
		{
			FSlateApplication::Get().ClearAllUserFocus(EFocusCause::Cleared);
			FSlateApplication::Get().DestroyWindowImmediately(Window);
		}

		/** What the screen does each frame (its Tick puts lost focus back). */
		void Tick()
		{
			Window->SlatePrepass(1.f);
			if (Screen.IsValid()) Screen->Tick(Screen->GetCachedGeometry(), 0.0, 0.016f);
		}

		/** Does Slate's own focus path for user 0 run through the screen (so its routing reaches it)? */
		bool Routable() const
		{
			const TSharedPtr<FSlateUser> User = FSlateApplication::Get().GetUser(0);
			return User.IsValid() && Screen.IsValid() && User->GetWeakFocusPath().ToWidgetPath().ContainsWidget(Screen.Get());
		}

		/** The focused widget and its parents, the focused one first. */
		static TArray<TSharedPtr<SWidget>> FocusChain()
		{
			TArray<TSharedPtr<SWidget>> Chain;
			for (TSharedPtr<SWidget> W = Focus(); W.IsValid(); W = W->GetParentWidget()) Chain.Add(W);
			return Chain;
		}

		static bool Walk(const FKeyEvent& Event, bool bDown)
		{
			const TArray<TSharedPtr<SWidget>> Chain = FocusChain();
			if (bDown)
			{
				for (int32 i = Chain.Num() - 1; i >= 0; --i) if (Chain[i]->OnPreviewKeyDown(Chain[i]->GetCachedGeometry(), Event).IsEventHandled()) return true;
			}
			for (const TSharedPtr<SWidget>& W : Chain)
			{
				const FReply Reply = bDown ? W->OnKeyDown(W->GetCachedGeometry(), Event) : W->OnKeyUp(W->GetCachedGeometry(), Event);
				if (Reply.IsEventHandled()) return true;
			}
			return false;
		}

		void Press(const FKey& Key)
		{
			FSlateApplication& App = FSlateApplication::Get();
			const FKeyEvent Event(Key, FModifierKeysState(), 0, false, 0, 0);
			if (GSlateRoutes && Routable())
			{
				++Routed;
				App.ProcessKeyDownEvent(Event);
				App.ProcessKeyUpEvent(Event);
			}
			else
			{
				++Walked;
				Walk(Event, true);
				Walk(Event, false);
			}
			Tick();
		}

		/** The left stick pushed (Value) and let go, as XInput's analog events. */
		void Stick(const FKey& Axis, float Value)
		{
			FSlateApplication& App = FSlateApplication::Get();
			for (const float V : { Value, 0.f })
			{
				const FAnalogInputEvent Event(Axis, FModifierKeysState(), 0, false, 0, 0, V);
				if (GSlateRoutes && Routable()) { App.ProcessAnalogInputEvent(Event); continue; }
				for (const TSharedPtr<SWidget>& W : FocusChain()) if (W->OnAnalogValueChanged(W->GetCachedGeometry(), Event).IsEventHandled()) break;
			}
			Tick();
		}

		/** One analog event of the left stick, not let go (a held stick); then a frame. */
		void Hold(const FKey& Axis, float Value)
		{
			FSlateApplication& App = FSlateApplication::Get();
			const FAnalogInputEvent Event(Axis, FModifierKeysState(), 0, false, 0, 0, Value);
			if (GSlateRoutes && Routable()) App.ProcessAnalogInputEvent(Event);
			else for (const TSharedPtr<SWidget>& W : FocusChain()) if (W->OnAnalogValueChanged(W->GetCachedGeometry(), Event).IsEventHandled()) break;
			Tick();
		}

		static TSharedPtr<SWidget> Focus() { return FSlateApplication::Get().GetUserFocusedWidget(0); }

		/** Is the pad's focus on this widget (or inside it, as in a text box)? */
		static bool On(const TSharedPtr<SWidget>& Widget)
		{
			const TSharedPtr<SWidget> F = Focus();
			return Widget.IsValid() && F.IsValid() && (F == Widget || Widget->HasFocusedDescendants());
		}
	};

	FString Name(const TSharedPtr<SWidget>& W)
	{
		return W.IsValid() ? (W->GetTag().IsNone() ? W->GetTypeAsString() : W->GetTag().ToString()) : FString(TEXT("nothing"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsPadReachAllTest, "OSDS.UI.PadReachAll", OsdsPadReachTest::Flags)
bool FOsdsPadReachAllTest::RunTest(const FString& Parameters)
{
	using namespace OsdsPadReachTest;
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("No Slate application: the pad test needs one"));
		return true;
	}

	// ------------------------------------------------------------------ pause menu
	{
		TArray<TPair<EOsdsMenuRow, int32>> Actions;
		int32 Closed = 0;
		TSharedRef<SOsdsPauseMenu> Menu = SNew(SOsdsPauseMenu)
			.GetState_Lambda([]() { FOsdsMenuState S; S.Place = TEXT("Ocean Drive"); return S; })
			.OnAction_Lambda([&Actions](EOsdsMenuRow Row, int32 Dir) { Actions.Add({ Row, Dir }); })
			.OnClose_Lambda([&Closed]() { ++Closed; });
		FPadScreen Pad(Menu);
		AddInfo(FString::Printf(TEXT("pause menu: the focus after opening is on %s"), *Name(Pad.Focus())));
		GSlateRoutes = true;
		Pad.Press(EKeys::Gamepad_DPad_Down);
		AddInfo(FString::Printf(TEXT("probe: routable=%d, after one Down through Slate the row is %d, the focus on %s"), Pad.Routable() ? 1 : 0, Menu->GetSelected(), *Name(Pad.Focus())));
		if (Menu->GetSelected() != 1)
		{
			// The headless editor did not route it to the window: send each press along the focus path as Slate does.
			GSlateRoutes = false;
			AddInfo(TEXT("FSlateApplication does not route pad keys to a hidden window in this editor: presses go along the focus path (preview down, then up)"));
			Pad.Press(EKeys::Gamepad_DPad_Down);
		}
		const bool bRouted = Menu->GetSelected() == 1;
		Pad.Press(EKeys::Gamepad_DPad_Up);
		if (!TestTrue(FString::Printf(TEXT("a pad key reaches the menu (the focus is on %s)"), *Name(Pad.Focus())), bRouted)) return false;
		TestTrue(FString::Printf(TEXT("pause menu opens with the pad on Continue driving (on %s)"), *Name(Pad.Focus())), Pad.On(Menu->GetRowWidget(0, 0)));

		// The top row: Up from the first row, then along every tab.
		Pad.Press(EKeys::Gamepad_DPad_Up);
		TestTrue(FString::Printf(TEXT("Up from the first row reaches the Drive tab (on %s)"), *Name(Pad.Focus())), Menu->IsOnTabs() && Pad.On(Menu->GetTabWidget(0)));
		for (int32 T = 1; T < SOsdsPauseMenu::NumPages; ++T)
		{
			Pad.Press(EKeys::Gamepad_DPad_Right);
			TestTrue(FString::Printf(TEXT("Right reaches the %s tab (on %s)"), *SOsdsPauseMenu::PageName(T), *Name(Pad.Focus())), Pad.On(Menu->GetTabWidget(T)));
		}
		Pad.Press(EKeys::Gamepad_DPad_Right);
		TestTrue(TEXT("Right past the last tab comes round to Drive"), Pad.On(Menu->GetTabWidget(0)));
		Pad.Press(EKeys::Gamepad_DPad_Left);
		TestTrue(TEXT("Left from Drive goes to Settings"), Pad.On(Menu->GetTabWidget(3)));
		Pad.Press(EKeys::Gamepad_DPad_Up);
		TestTrue(TEXT("Up on the tab row stays on it (no trap, no jump)"), Pad.On(Menu->GetTabWidget(3)));

		// The rows the release has: no Traffic row while traffic is off; Settings has the HUD's Map row.
		auto HasRow = [](int32 P, EOsdsMenuRow Row) { for (const SOsdsPauseMenu::FRowInfo& R : SOsdsPauseMenu::Rows(P)) if (R.Row == Row) return true; return false; };
		TestEqual(TEXT("Drive has the Traffic row only while traffic is on"), HasRow(0, EOsdsMenuRow::Traffic), OsdsFeatures::IsTrafficEnabled());
		TestTrue(TEXT("Settings has the Map row (with route or always)"), HasRow(3, EOsdsMenuRow::HudMap));

		// Every tab opens with A (and Down), on its first row; every row is reached and used.
		for (int32 P = 0; P < SOsdsPauseMenu::NumPages; ++P)
		{
			if (!Menu->IsOnTabs()) Pad.Press(EKeys::Gamepad_DPad_Up);
			for (int32 Guard = 0; Menu->GetTabCursor() != P && Guard < 8; ++Guard) Pad.Press(EKeys::Gamepad_DPad_Right);
			Pad.Press(P % 2 ? EKeys::Gamepad_DPad_Down : EKeys::Gamepad_FaceButton_Bottom);
			TestEqual(FString::Printf(TEXT("%s opens from its tab"), *SOsdsPauseMenu::PageName(P)), Menu->GetPage(), P);
			const TArray<SOsdsPauseMenu::FRowInfo>& List = SOsdsPauseMenu::Rows(P);
			for (int32 i = 0; i < List.Num(); ++i)
			{
				if (i > 0) Pad.Press(EKeys::Gamepad_DPad_Down);
				const FString Row = List[i].Title;
				TestTrue(FString::Printf(TEXT("%s: the pad reaches %s (on %s)"), *SOsdsPauseMenu::PageName(P), *Row, *Name(Pad.Focus())), Pad.On(Menu->GetRowWidget(P, i)));
				const int32 Before = Actions.Num();
				if (List[i].Kind == SOsdsPauseMenu::EKind::Slider || List[i].Kind == SOsdsPauseMenu::EKind::Segmented)
				{
					Pad.Press(EKeys::Gamepad_DPad_Right);
					Pad.Press(EKeys::Gamepad_DPad_Left);
					TestTrue(FString::Printf(TEXT("%s changes with D-pad Right and Left"), *Row), Actions.Num() == Before + 2 && Actions.Last().Key == List[i].Row && Actions.Last().Value == -1);
				}
				else if (List[i].Row != EOsdsMenuRow::Continue)
				{
					Pad.Press(EKeys::Gamepad_FaceButton_Bottom);
					TestTrue(FString::Printf(TEXT("A uses %s"), *Row), Actions.Num() == Before + 1 && Actions.Last().Key == List[i].Row && Actions.Last().Value == 0);
				}
				TestTrue(FString::Printf(TEXT("the pad is still on %s after using it"), *Row), Pad.On(Menu->GetRowWidget(P, i)));
			}
			Pad.Press(EKeys::Gamepad_DPad_Down);
			TestTrue(FString::Printf(TEXT("%s: Down from the last row comes round to the first"), *SOsdsPauseMenu::PageName(P)), Pad.On(Menu->GetRowWidget(P, 0)));
		}

		// LB and RB from anywhere: a new page, on its first row.
		Pad.Press(EKeys::Gamepad_DPad_Down);
		Pad.Press(EKeys::Gamepad_DPad_Down);
		Pad.Press(EKeys::Gamepad_RightShoulder);
		TestTrue(TEXT("RB from a row: Drive, on its first row"), Menu->GetPage() == 0 && Pad.On(Menu->GetRowWidget(0, 0)));
		Pad.Press(EKeys::Gamepad_DPad_Up);
		Pad.Press(EKeys::Gamepad_LeftShoulder);
		TestTrue(TEXT("LB from the tab row: Settings, on its first row"), Menu->GetPage() == 3 && !Menu->IsOnTabs() && Pad.On(Menu->GetRowWidget(3, 0)));

		// The stick moves one row per push.
		Pad.Stick(EKeys::Gamepad_LeftY, -1.f);
		TestTrue(TEXT("one stick push down is one row"), Pad.On(Menu->GetRowWidget(3, 1)));
		Pad.Stick(EKeys::Gamepad_LeftY, 1.f);
		TestTrue(TEXT("one stick push up is one row"), Pad.On(Menu->GetRowWidget(3, 0)));

		// A mouse resting on a row while the menu changes under it does not take the pad's place.
		const TSharedPtr<SWidget> Resting = Menu->GetRowWidget(3, 5);
		Resting->OnMouseEnter(Resting->GetCachedGeometry(), FPointerEvent(0, FVector2D(600.0, 700.0), FVector2D(600.0, 700.0), TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState()));
		TestTrue(TEXT("a still mouse does not move the selection"), Pad.On(Menu->GetRowWidget(3, 0)) && Menu->GetSelected() == 0);
		Resting->OnMouseEnter(Resting->GetCachedGeometry(), FPointerEvent(0, FVector2D(600.0, 700.0), FVector2D(590.0, 690.0), TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState()));
		TestTrue(TEXT("a moving mouse does"), Menu->GetSelected() == 5 && Pad.On(Resting));

		// Lost focus comes back on the next frame.
		FSlateApplication::Get().ClearAllUserFocus(EFocusCause::Cleared);
		Pad.Tick();
		TestTrue(TEXT("lost focus comes back to the menu"), Pad.On(Menu->GetRowWidget(3, 5)));

		Pad.Press(EKeys::Gamepad_FaceButton_Right);
		TestEqual(TEXT("B closes the menu"), Closed, 1);
		Pad.Press(EKeys::Gamepad_DPad_Up);
		Pad.Press(EKeys::Gamepad_DPad_Up);
		Pad.Press(EKeys::Gamepad_DPad_Up);
		Pad.Press(EKeys::Gamepad_DPad_Up);
		Pad.Press(EKeys::Gamepad_DPad_Up);
		Pad.Press(EKeys::Gamepad_DPad_Up);
		Pad.Press(EKeys::Gamepad_FaceButton_Right);
		TestEqual(TEXT("B on the tab row closes the menu too"), Closed, 2);
		Pad.Press(EKeys::Gamepad_Special_Right);
		TestEqual(TEXT("Menu closes the menu"), Closed, 3);
		AddInfo(FString::Printf(TEXT("pause menu: %d presses routed by FSlateApplication, %d walked along the focus path"), Pad.Routed, Pad.Walked));
	}

	// ------------------------------------------------------------------ list menu (famous roads, a found place)
	{
		TArray<SOsdsListMenu::FItem> Items;
		for (const TCHAR* T : { TEXT("Pacific Coast Highway"), TEXT("Overseas Highway"), TEXT("Blue Ridge Parkway") })
		{
			SOsdsListMenu::FItem I;
			I.Title = T;
			Items.Add(I);
		}
		int32 Chosen = -1, Back = 0;
		TSharedRef<SOsdsListMenu> List = SNew(SOsdsListMenu).Title(TEXT("Famous roads")).Items(Items)
			.OnChoose_Lambda([&Chosen](int32 i) { Chosen = i; })
			.OnBack_Lambda([&Back]() { ++Back; });
		FPadScreen Pad(List);
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			if (i > 0) Pad.Press(EKeys::Gamepad_DPad_Down);
			TestTrue(FString::Printf(TEXT("list: the pad reaches %s (on %s)"), *Items[i].Title, *Name(Pad.Focus())), Pad.On(List->GetRowWidget(i)));
			Pad.Press(EKeys::Gamepad_FaceButton_Bottom);
			TestEqual(FString::Printf(TEXT("list: A chooses %s"), *Items[i].Title), Chosen, i);
		}
		Pad.Stick(EKeys::Gamepad_LeftY, -1.f);
		TestTrue(TEXT("list: the stick comes round to the first item"), Pad.On(List->GetRowWidget(0)));
		Pad.Press(EKeys::Gamepad_FaceButton_Right);
		TestEqual(TEXT("list: B goes back"), Back, 1);
	}

	// ------------------------------------------------------------------ road atlas (its top buttons)
	{
		TArray<int32> Settings;
		int32 Closed = 0, Found = 0, Cars = 0, Gears = 0;
		TSharedRef<SOsdsRoadAtlas> Atlas = SNew(SOsdsRoadAtlas)
			.OnSetting_Lambda([&Settings](int32 i) { Settings.Add(i); })
			.OnFindPlace_Lambda([&Found]() { ++Found; })
			.OnCycleCar_Lambda([&Cars]() { ++Cars; })
			.OnToggleTransmission_Lambda([&Gears]() { ++Gears; })
			.OnClose_Lambda([&Closed]() { ++Closed; });
		FPadScreen Pad(Atlas);
		TArray<TSharedPtr<SWidget>> All;
		for (const TArray<TSharedPtr<SWidget>>& Row : Atlas->GetPadRows()) All.Append(Row);
		// 23 with the Traffic setting; while traffic is off for the release it is not on the panel.
		const bool bTraffic = OsdsFeatures::IsTrafficEnabled();
		TestEqual(FString::Printf(TEXT("atlas: %d buttons on the top panel"), bTraffic ? 23 : 22), All.Num(), bTraffic ? 23 : 22);
		bool bTrafficButton = false;
		for (const TSharedPtr<SWidget>& B : All) bTrafficButton |= B.IsValid() && B->GetTag() == FName(TEXT("Setting.13"));
		TestEqual(TEXT("atlas: the Traffic setting is on the panel only while traffic is on"), bTrafficButton, bTraffic);
		TestTrue(TEXT("atlas: opens on the map"), Pad.On(Atlas));
		Pad.Press(EKeys::Gamepad_DPad_Right);
		TSet<SWidget*> Reached;
		for (int32 i = 0; i < All.Num(); ++i)
		{
			TestTrue(FString::Printf(TEXT("atlas: D-pad Right reaches %s (on %s)"), *Name(All[i]), *Name(Pad.Focus())), Pad.On(All[i]));
			Reached.Add(Pad.Focus().Get());
			Pad.Press(EKeys::Gamepad_DPad_Right);
		}
		TestEqual(TEXT("atlas: every top button reached"), Reached.Num(), All.Num());
		TestTrue(TEXT("atlas: Right past the last button comes round to the first"), Pad.On(All[0]));
		Pad.Press(EKeys::Gamepad_DPad_Down);
		TestTrue(TEXT("atlas: Down goes to the second row"), Pad.On(Atlas->GetPadRows()[1][0]));
		Pad.Press(EKeys::Gamepad_DPad_Up);
		TestTrue(TEXT("atlas: Up comes back to the top row"), Pad.On(Atlas->GetPadRows()[0][0]));
		Pad.Press(EKeys::Gamepad_DPad_Up);
		TestTrue(TEXT("atlas: Up on the top row stays"), Pad.On(Atlas->GetPadRows()[0][0]));
		// A presses the button the pad is on: a setting, Find a place, the car and the gearbox.
		for (int32 i = 0; i < 4; ++i) Pad.Press(EKeys::Gamepad_DPad_Down);
		Pad.Press(EKeys::Gamepad_FaceButton_Bottom);
		TestTrue(FString::Printf(TEXT("atlas: A presses a Your car setting (%d)"), Settings.Num() ? Settings.Last() : -1), Settings.Num() == 1 && Settings[0] >= 11);
		Pad.Press(EKeys::Gamepad_DPad_Up);
		Pad.Press(EKeys::Gamepad_DPad_Up);
		Pad.Press(EKeys::Gamepad_FaceButton_Bottom);
		TestEqual(TEXT("atlas: A presses a Settings button"), Settings.Num(), 2);
		Pad.Press(EKeys::Gamepad_DPad_Up);
		for (int32 Guard = 0; !Pad.On(Atlas->GetPadRows()[1][0]) && Guard < 30; ++Guard) Pad.Press(EKeys::Gamepad_DPad_Left);
		Pad.Press(EKeys::Gamepad_FaceButton_Bottom);
		TestEqual(TEXT("atlas: A presses Find a place"), Found, 1);
		Pad.Press(EKeys::Gamepad_DPad_Right);
		Pad.Press(EKeys::Gamepad_DPad_Right);
		Pad.Press(EKeys::Gamepad_FaceButton_Bottom);
		TestEqual(TEXT("atlas: A presses the car button"), Cars, 1);
		Pad.Press(EKeys::Gamepad_DPad_Right);
		Pad.Press(EKeys::Gamepad_FaceButton_Bottom);
		TestEqual(TEXT("atlas: A presses the gearbox button"), Gears, 1);
		Pad.Press(EKeys::Gamepad_FaceButton_Right);
		TestTrue(TEXT("atlas: B goes from the buttons back to the map"), Pad.On(Atlas) && Closed == 0);
		Pad.Press(EKeys::Gamepad_FaceButton_Right);
		TestEqual(TEXT("atlas: B on the map closes it"), Closed, 1);
		Pad.Press(EKeys::Gamepad_DPad_Left);
		TestTrue(TEXT("atlas: Left from the map reaches the last top-row button"), Pad.On(Atlas->GetPadRows()[0].Last()));
		Pad.Press(EKeys::Gamepad_FaceButton_Bottom);
		TestEqual(TEXT("atlas: A on Back to drive closes it"), Closed, 2);
	}

	// ------------------------------------------------------------------ Where to? (pad keyboard)
	{
		int32 Closed = 0;
		TSharedRef<SOsdsPlaceSearch> Search = SNew(SOsdsPlaceSearch)
			.OnClose_Lambda([&Closed]() { ++Closed; });
		FPadScreen Pad(Search);
		Search->FocusFirst(true);
		Pad.Tick();
		const TArray<TSharedPtr<SWidget>> Places = Search->GetChoiceButtons();
		TestTrue(TEXT("Where to?: places to choose"), Places.Num() > 0);
		TestTrue(FString::Printf(TEXT("Where to?: opens on the first place for a pad (on %s)"), *Name(Pad.Focus())), Places.Num() > 0 && Pad.On(Places[0]));
		for (int32 i = 1; i < Places.Num(); ++i)
		{
			Pad.Press(EKeys::Gamepad_DPad_Down);
			TestTrue(FString::Printf(TEXT("Where to?: Down reaches %s"), *Name(Places[i])), Pad.On(Places[i]));
		}
		Pad.Press(EKeys::Gamepad_DPad_Down);
		const TArray<TSharedPtr<SWidget>> Tools = Search->GetToolButtons();
		TestTrue(TEXT("Where to?: Down from the last place reaches Back to drive"), Tools.Num() == 3 && Pad.On(Tools[2]));
		Pad.Press(EKeys::Gamepad_DPad_Down);
		TestTrue(TEXT("Where to?: Down on the last control stays (no trap)"), Pad.On(Tools[2]));
		for (int32 i = 0; i < Places.Num() + 1; ++i) Pad.Press(EKeys::Gamepad_DPad_Up);
		TestTrue(FString::Printf(TEXT("Where to?: Up reaches the search box (on %s)"), *Name(Pad.Focus())), Pad.On(Search->GetField()));
		Pad.Press(EKeys::Gamepad_DPad_Right);
		TestTrue(TEXT("Where to?: Right reaches Search"), Pad.On(Tools[0]));
		Pad.Press(EKeys::Gamepad_DPad_Right);
		TestTrue(TEXT("Where to?: Right reaches Surprise me"), Pad.On(Tools[1]));
		Pad.Press(EKeys::Gamepad_DPad_Right);
		TestTrue(TEXT("Where to?: Right comes round to the search box"), Pad.On(Search->GetField()));

		// A on the box: the pad keyboard; every key is reached; typing works.
		Pad.Press(EKeys::Gamepad_FaceButton_Bottom);
		TestTrue(TEXT("Where to?: A on the box opens the pad keyboard"), Search->IsPadKeyboardOpen());
		const TArray<TArray<TSharedPtr<SWidget>>>& Keys = Search->GetKeyRows();
		TestTrue(FString::Printf(TEXT("Where to?: the pad is on q (on %s)"), *Name(Pad.Focus())), Keys.Num() == 5 && Pad.On(Keys[1][0]));
		Pad.Press(EKeys::Gamepad_DPad_Up);
		int32 KeysReached = 0, KeysAll = 0;
		for (int32 R = 0; R < Keys.Num(); ++R)
		{
			for (int32 C = 0; C < Keys[R].Num(); ++C)
			{
				++KeysAll;
				if (Pad.On(Keys[R][C])) ++KeysReached;
				else AddError(FString::Printf(TEXT("Where to?: key %s not reached (on %s)"), *Name(Keys[R][C]), *Name(Pad.Focus())));
				if (C + 1 < Keys[R].Num()) Pad.Press(EKeys::Gamepad_DPad_Right);
			}
			if (R + 1 < Keys.Num())
			{
				Pad.Press(EKeys::Gamepad_DPad_Down);
				for (int32 Guard = 0; !Pad.On(Keys[R + 1][0]) && Guard < 12; ++Guard) Pad.Press(EKeys::Gamepad_DPad_Left);
			}
		}
		TestTrue(FString::Printf(TEXT("Where to?: every pad keyboard key reached (%d)"), KeysReached), KeysReached >= 44);
		// Type "mia": m is row 3, i row 1, a row 2.
		auto TypeAt = [&](int32 R, int32 C)
		{
			OsdsTheme::FocusPad(Keys[R][C]);
			Pad.Press(EKeys::Gamepad_FaceButton_Bottom);
		};
		// The pad keyboard's buttons: X a space, B deletes, A types at the cursor, LB and RB move it.
		Pad.Press(EKeys::Gamepad_FaceButton_Left);
		TestEqual(TEXT("Where to?: X types a space"), Search->GetQuery(), FString(TEXT(" ")));
		Pad.Press(EKeys::Gamepad_FaceButton_Right);
		TestEqual(TEXT("Where to?: B deletes"), Search->GetQuery(), FString());
		TestTrue(TEXT("Where to?: B with something to delete keeps the keyboard open"), Search->IsPadKeyboardOpen());
		TypeAt(3, 6); TypeAt(1, 7); TypeAt(2, 0);
		TestEqual(TEXT("Where to?: A types the key the pad is on"), Search->GetQuery(), FString(TEXT("mia")));
		Pad.Press(EKeys::Gamepad_LeftShoulder);
		Pad.Press(EKeys::Gamepad_LeftShoulder);
		TestEqual(TEXT("Where to?: LB moves the cursor left"), Search->GetTextCursor(), 1);
		TypeAt(1, 2);
		TestEqual(TEXT("Where to?: A types at the cursor"), Search->GetQuery(), FString(TEXT("meia")));
		Pad.Press(EKeys::Gamepad_RightShoulder);
		Pad.Press(EKeys::Gamepad_FaceButton_Right);
		TestEqual(TEXT("Where to?: RB moves the cursor right; B deletes before it"), Search->GetQuery(), FString(TEXT("mea")));
		for (int32 i = 0; i < 4; ++i) Pad.Press(EKeys::Gamepad_RightShoulder);
		for (int32 i = 0; i < 3; ++i) Pad.Press(EKeys::Gamepad_FaceButton_Right);
		TestEqual(TEXT("Where to?: B deletes it all"), Search->GetQuery(), FString());
		Pad.Press(EKeys::Gamepad_FaceButton_Right);
		TestTrue(TEXT("Where to?: B on an empty box closes the pad keyboard, back on the box"), !Search->IsPadKeyboardOpen() && Pad.On(Search->GetField()) && Closed == 0);
		Pad.Press(EKeys::Gamepad_FaceButton_Right);
		TestEqual(TEXT("Where to?: B closes the screen"), Closed, 1);
	}

	// ------------------------------------------------------------------ Tune car and Seat & view
	{
		int32 Closed = 0;
		TSharedRef<SOsdsTuningMenu> Tune = SNew(SOsdsTuningMenu).OnClose_Lambda([&Closed]() { ++Closed; });
		FPadScreen Pad(Tune);
		TestTrue(TEXT("Tune car has the pad's focus"), Pad.On(Tune));
		Pad.Press(EKeys::Gamepad_DPad_Down);
		Pad.Press(EKeys::Gamepad_DPad_Right);
		TestTrue(TEXT("Tune car keeps the focus while rows change"), Pad.On(Tune) && Closed == 0);
		Pad.Press(EKeys::Gamepad_Special_Right);
		TestTrue(TEXT("Menu cancels Tune car"), Closed == 1 && Tune->GetResult() == SOsdsTuningMenu::ETuningResult::Cancelled);
	}
	{
		int32 Closed = 0;
		TSharedRef<SOsdsSeatMenu> Seat = SNew(SOsdsSeatMenu).OnClose_Lambda([&Closed]() { ++Closed; });
		FPadScreen Pad(Seat);
		TestTrue(TEXT("Seat & view has the pad's focus"), Pad.On(Seat));
		Pad.Press(EKeys::Gamepad_DPad_Down);
		Pad.Press(EKeys::Gamepad_FaceButton_Right);
		TestTrue(TEXT("B cancels Seat & view"), Closed == 1 && !Seat->WasSaved());
	}
	return true;
}

namespace OsdsPadReachTest
{
	/** One screen in one mode for the hint check: the widget, what it shows now, its hints, and how to get it into the mode. */
	struct FMade
	{
		TSharedPtr<SWidget> Widget;
		TFunction<FString()> State;
		TFunction<TArray<OsdsTheme::FHint>()> Hints;
		TFunction<void(FPadScreen&)> Prepare;
		/** Hint glyphs not pressed here, and why (for example: needs the GPS). */
		TMap<FString, FString> Skip;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsPadHintsMatchTest, "OSDS.UI.PadHintsMatch", OsdsPadReachTest::Flags)
bool FOsdsPadHintsMatchTest::RunTest(const FString& Parameters)
{
	// Every button hint on every screen says what the button does there: each hinted button (pressed
	// on a fresh screen in that mode) changes what the screen shows or does, and a face button with no
	// hint (A, B, X, Y) changes nothing. The help lines are made from these same hint lists.
	using namespace OsdsPadReachTest;
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("No Slate application: the hint test needs one"));
		return true;
	}
	int32 Checked = 0;
	auto Run = [this, &Checked](const FString& ScreenName, TFunction<FMade()> Make)
	{
		TArray<OsdsTheme::FHint> Hints;
		{
			FMade Probe = Make();
			FPadScreen Pad(Probe.Widget.ToSharedRef());
			if (Probe.Prepare) Probe.Prepare(Pad);
			Hints = Probe.Hints();
		}
		TestTrue(FString::Printf(TEXT("%s: has hints"), *ScreenName), Hints.Num() > 0);
		// What one key does on a fresh screen in this mode: the state before and after.
		auto Try = [&Make](const FKey& Key, FString& Before, FString& After)
		{
			FMade M = Make();
			FPadScreen Pad(M.Widget.ToSharedRef());
			if (M.Prepare) M.Prepare(Pad);
			Before = M.State() + TEXT(" | focus ") + Name(FPadScreen::Focus());
			Pad.Press(Key);
			After = M.State() + TEXT(" | focus ") + Name(FPadScreen::Focus());
		};
		TSet<FKey> Hinted;
		FMade Info = Make();
		for (const OsdsTheme::FHint& Hint : Hints)
		{
			const TArray<FKey> Keys = OsdsTheme::PadKeysOf(Hint.Glyph);
			Hinted.Append(Keys);
			if (Keys.Num() == 0) continue; // "Stick": no button
			if (const FString* Why = Info.Skip.Find(Hint.Glyph)) { AddInfo(FString::Printf(TEXT("%s: %s (%s) not pressed: %s"), *ScreenName, *Hint.Glyph, *Hint.Text, **Why)); continue; }
			// A D-pad hint holds when a way it names does something; a button hint, when each button does.
			const bool bDpad = Hint.Glyph.StartsWith(TEXT("D-pad")) || Hint.Glyph.Contains(TEXT("D-pad"));
			int32 Changed = 0;
			FString Seen;
			for (const FKey& Key : Keys)
			{
				FString Before, After;
				Try(Key, Before, After);
				const bool bChanged = Before != After;
				Changed += bChanged ? 1 : 0;
				Seen += FString::Printf(TEXT(" [%s: %s -> %s]"), *Key.ToString(), *Before, *After);
				if (!bDpad) TestTrue(FString::Printf(TEXT("%s: hint '%s: %s' - %s does something%s"), *ScreenName, *Hint.Glyph, *Hint.Text, *Key.ToString(), bChanged ? TEXT("") : *Seen), bChanged);
				++Checked;
			}
			if (bDpad) TestTrue(FString::Printf(TEXT("%s: hint '%s: %s' - the D-pad does something%s"), *ScreenName, *Hint.Glyph, *Hint.Text, Changed ? TEXT("") : *Seen), Changed > 0);
		}
		// No hidden face-button actions.
		for (const FKey& Key : { EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_FaceButton_Left, EKeys::Gamepad_FaceButton_Top })
		{
			if (Hinted.Contains(Key)) continue;
			FString Before, After;
			Try(Key, Before, After);
			TestEqual(FString::Printf(TEXT("%s: %s has no hint, so it does nothing"), *ScreenName, *Key.ToString()), After, Before);
			++Checked;
		}
	};

	// ------------------------------------------------------------------ pause menu, each kind of row
	auto Pause = [](bool bRoute, TFunction<void(FPadScreen&)> Prepare) -> TFunction<FMade()>
	{
		return [bRoute, Prepare]()
		{
			TSharedRef<int32> Actions = MakeShared<int32>(0), Closed = MakeShared<int32>(0);
			TSharedRef<SOsdsPauseMenu> Menu = SNew(SOsdsPauseMenu)
				.GetState_Lambda([bRoute]() { FOsdsMenuState S; S.bRoute = bRoute; S.bPad = true; return S; })
				.OnAction_Lambda([Actions](EOsdsMenuRow, int32) { ++*Actions; })
				.OnClose_Lambda([Closed]() { ++*Closed; });
			FMade M;
			M.Widget = Menu;
			M.State = [Menu, Actions, Closed]() { return FString::Printf(TEXT("page %d row %d tabs %d tab %d actions %d closed %d"), Menu->GetPage(), Menu->GetSelected(), Menu->IsOnTabs() ? 1 : 0, Menu->GetTabCursor(), *Actions, *Closed); };
			M.Hints = [Menu]() { return Menu->GetPadHints(); };
			M.Prepare = Prepare;
			return M;
		};
	};
	Run(TEXT("Pause menu, an action row"), Pause(false, nullptr));
	Run(TEXT("Pause menu, Drive with a route"), Pause(true, nullptr));
	// LB from Drive goes to Settings: its first row (Sound) is a value row; Rumble, four down, a slider.
	Run(TEXT("Pause menu, a value row"), Pause(false, [](FPadScreen& Pad) { Pad.Press(EKeys::Gamepad_LeftShoulder); }));
	Run(TEXT("Pause menu, a slider row"), Pause(false, [](FPadScreen& Pad)
	{
		Pad.Press(EKeys::Gamepad_LeftShoulder);
		for (int32 i = 0; i < SOsdsPauseMenu::Rows(3).Num(); ++i)
		{
			if (SOsdsPauseMenu::Rows(3)[i].Row == EOsdsMenuRow::Rumble) break;
			Pad.Press(EKeys::Gamepad_DPad_Down);
		}
	}));
	Run(TEXT("Pause menu, the tab row"), Pause(false, [](FPadScreen& Pad) { Pad.Press(EKeys::Gamepad_DPad_Up); }));

	// ------------------------------------------------------------------ list menu
	Run(TEXT("List menu"), []()
	{
		TArray<SOsdsListMenu::FItem> Items;
		for (const TCHAR* T : { TEXT("One"), TEXT("Two"), TEXT("Three") }) { SOsdsListMenu::FItem I; I.Title = T; Items.Add(I); }
		TSharedRef<int32> Chosen = MakeShared<int32>(0), Back = MakeShared<int32>(0);
		TSharedRef<SOsdsListMenu> List = SNew(SOsdsListMenu).Title(TEXT("List")).Items(Items)
			.OnChoose_Lambda([Chosen](int32) { ++*Chosen; }).OnBack_Lambda([Back]() { ++*Back; });
		FMade M;
		M.Widget = List;
		M.State = [List, Chosen, Back]() { return FString::Printf(TEXT("row %d chosen %d back %d"), List->GetSelected(), *Chosen, *Back); };
		M.Hints = []() { return SOsdsListMenu::GetPadHints(); };
		return M;
	});

	// ------------------------------------------------------------------ Where to? and its pad keyboard
	auto Search = [](bool bKeyboard) -> TFunction<FMade()>
	{
		return [bKeyboard]()
		{
			TSharedRef<int32> Chosen = MakeShared<int32>(0), Surprised = MakeShared<int32>(0), Closed = MakeShared<int32>(0);
			TSharedRef<SOsdsPlaceSearch> S = SNew(SOsdsPlaceSearch)
				.OnChoose_Lambda([Chosen](const FOsdsPlace&) { ++*Chosen; })
				.OnSurprise_Lambda([Surprised]() { ++*Surprised; return true; })
				.OnClose_Lambda([Closed]() { ++*Closed; });
			FMade M;
			M.Widget = S;
			M.State = [S, Chosen, Surprised, Closed]() { return FString::Printf(TEXT("query '%s' cursor %d keys %d chosen %d surprise %d closed %d"), *S->GetQuery(), S->GetTextCursor(), S->IsPadKeyboardOpen() ? 1 : 0, *Chosen, *Surprised, *Closed); };
			M.Hints = [S]() { return S->GetPadHints(); };
			M.Prepare = [S, bKeyboard](FPadScreen& Pad)
			{
				S->FocusFirst(true);
				Pad.Tick();
				if (!bKeyboard) return;
				OsdsTheme::FocusPad(S->GetField());
				Pad.Tick();
				S->OpenPadKeyboard();
				Pad.Tick();
				// Something to delete, and the cursor inside it (so LB and RB both move it).
				S->TypeKey(TEXT("a"));
				S->TypeKey(TEXT("b"));
				S->MoveCursor(-1);
			};
			return M;
		};
	};
	Run(TEXT("Where to?"), Search(false));
	Run(TEXT("Where to?, the pad keyboard"), Search(true));

	// ------------------------------------------------------------------ road atlas: the map, and its buttons
	auto Atlas = [](bool bButtons) -> TFunction<FMade()>
	{
		return [bButtons]()
		{
			TSharedRef<int32> Chosen = MakeShared<int32>(0), Found = MakeShared<int32>(0), Cars = MakeShared<int32>(0), Gears = MakeShared<int32>(0), Closed = MakeShared<int32>(0), Surprised = MakeShared<int32>(0);
			TSharedRef<SOsdsRoadAtlas> A = SNew(SOsdsRoadAtlas)
				.OnChoose_Lambda([Chosen](const FOsdsPlace&) { ++*Chosen; })
				.OnSurprise_Lambda([Surprised]() { ++*Surprised; return true; })
				.OnFindPlace_Lambda([Found]() { ++*Found; })
				.OnCycleCar_Lambda([Cars]() { ++*Cars; })
				.OnToggleTransmission_Lambda([Gears]() { ++*Gears; })
				.OnClose_Lambda([Closed]() { ++*Closed; });
			FMade M;
			M.Widget = A;
			M.State = [A, Chosen, Found, Cars, Gears, Closed, Surprised]()
			{
				return FString::Printf(TEXT("centre %.1f,%.1f zoom %d chosen %d find %d car %d gear %d closed %d surprise %d"), A->GetCenterM().X, A->GetCenterM().Y, A->GetLevel(), *Chosen, *Found, *Cars, *Gears, *Closed, *Surprised);
			};
			M.Hints = [A]() { return A->GetPadHints(); };
			// No map frame here (it would fetch real map tiles): Drive here needs a place under the centre.
			if (!bButtons) M.Skip = { { TEXT("A"), TEXT("Drive here needs a place under the map centre (a world frame); the game's pad review drives it") } };
			M.Prepare = [A, bButtons](FPadScreen& Pad)
			{
				// The map off the car (so Recenter moves it), then onto a button in the middle of the panel.
				Pad.Press(EKeys::Right);
				if (!bButtons) return;
				OsdsTheme::FocusPad(A->GetPadRows()[1][1]);
				Pad.Tick();
			};
			return M;
		};
	};
	Run(TEXT("Road atlas, the map"), Atlas(false));
	Run(TEXT("Road atlas, the buttons"), Atlas(true));

	// ------------------------------------------------------------------ Tune car and Seat & view
	Run(TEXT("Tune car"), []()
	{
		TSharedRef<int32> Closed = MakeShared<int32>(0);
		TSharedRef<SOsdsTuningMenu> T = SNew(SOsdsTuningMenu).OnClose_Lambda([Closed]() { ++*Closed; });
		FMade M;
		M.Widget = T;
		M.State = [T, Closed]() { return FString::Printf(TEXT("page %d row %d setup %08x confirm %d result %d closed %d"), T->GetPage(), T->GetSelected(), FCrc::MemCrc32(&T->GetWorking(), sizeof(FOsdsTuning)), T->IsConfirmingReset() ? 1 : 0, static_cast<int32>(T->GetResult()), *Closed); };
		M.Hints = []() { return SOsdsTuningMenu::GetPadHints(); };
		// The rev limit row, changed so X has something to reset.
		M.Prepare = [T](FPadScreen&) { T->Select(2); T->Nudge(1); };
		return M;
	});
	Run(TEXT("Seat & view"), []()
	{
		TSharedRef<int32> Closed = MakeShared<int32>(0);
		TSharedRef<SOsdsSeatMenu> S = SNew(SOsdsSeatMenu).OnClose_Lambda([Closed]() { ++*Closed; });
		FMade M;
		M.Widget = S;
		M.State = [S, Closed]() { const FOsdsSeat& W = S->GetWorking(); return FString::Printf(TEXT("row %d seat %.2f,%.2f,%.2f,%.2f saved %d closed %d"), S->GetSelected(), W.UpCm, W.ForwardCm, W.PitchDeg, W.FovDeg, S->WasSaved() ? 1 : 0, *Closed); };
		M.Hints = []() { return SOsdsSeatMenu::GetPadHints(); };
		M.Prepare = [S](FPadScreen&) { S->Select(0); S->Nudge(1); };
		return M;
	});

	// ------------------------------------------------------------------ Plan a route (no GPS here: A and X need it)
	Run(TEXT("Plan a route"), []()
	{
		TSharedRef<int32> Started = MakeShared<int32>(0), Back = MakeShared<int32>(0);
		TSharedRef<SOsdsRoutePlanner> R = SNew(SOsdsRoutePlanner)
			.OnStart_Lambda([Started]() { ++*Started; })
			.OnBack_Lambda([Back]() { ++*Back; });
		FMade M;
		M.Widget = R;
		M.State = [R, Started, Back]() { return FString::Printf(TEXT("cross %.1f,%.1f zoom %d start %d back %d"), R->GetCrossM().X, R->GetCrossM().Y, R->GetLevel(), *Started, *Back); };
		M.Hints = []() { return SOsdsRoutePlanner::GetPadHints(); };
		M.Skip = { { TEXT("A"), TEXT("adding a stop needs the GPS (a world subsystem); these Slate tests have none") }, { TEXT("X"), TEXT("removing a stop needs the GPS") } };
		return M;
	});

	AddInfo(FString::Printf(TEXT("pad hints: %d button presses checked"), Checked));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsPadWalkAllTest, "OSDS.UI.PadWalkAll", OsdsPadReachTest::Flags)
bool FOsdsPadWalkAllTest::RunTest(const FString& Parameters)
{
	// Everything with a pad only, beyond OSDS.UI.PadReachAll (every menu row, tab, atlas button
	// and pad-keyboard key): the driving controls, quitting with a confirm, every row of Tune car and
	// Seat & view (D-pad and stick), and Plan a route.
	using namespace OsdsPadReachTest;

	// ------------------------------------------------------------------ driving: each control has a pad button
	{
		auto PadKeyOf = [](const TCHAR* Action)
		{
			TArray<FKey> Keys;
			for (const FOsdsBindingSpec& B : UOsdsInputConfig::Bindings()) if (B.Action == FName(Action) && B.Key.IsGamepadKey()) Keys.Add(B.Key);
			return Keys;
		};
		// docs/controls.md: the 2.0.0 pad layout.
		const TArray<TPair<const TCHAR*, FKey>> Expected = {
			{ TEXT("SteerPad"), EKeys::Gamepad_LeftX }, { TEXT("ThrottlePad"), EKeys::Gamepad_RightTriggerAxis }, { TEXT("BrakePad"), EKeys::Gamepad_LeftTriggerAxis },
			{ TEXT("Clutch"), EKeys::Gamepad_FaceButton_Bottom }, { TEXT("ParkingBrake"), EKeys::Gamepad_FaceButton_Right },
			{ TEXT("ShiftUp"), EKeys::Gamepad_RightShoulder }, { TEXT("ShiftDown"), EKeys::Gamepad_LeftShoulder },
			{ TEXT("ToggleTransmission"), EKeys::Gamepad_RightThumbstick }, { TEXT("StartEngine"), EKeys::Gamepad_DPad_Left },
			{ TEXT("CycleCamera"), EKeys::Gamepad_FaceButton_Top }, { TEXT("CycleHud"), EKeys::Gamepad_FaceButton_Left },
			{ TEXT("ToggleAtlas"), EKeys::Gamepad_Special_Right }, { TEXT("Recover"), EKeys::Gamepad_Special_Left },
			{ TEXT("MinimapIn"), EKeys::Gamepad_DPad_Up }, { TEXT("MinimapOut"), EKeys::Gamepad_DPad_Down }, { TEXT("Tuning"), EKeys::Gamepad_DPad_Right },
		};
		for (const TPair<const TCHAR*, FKey>& E : Expected)
		{
			const TArray<FKey> Keys = PadKeyOf(E.Key);
			TestTrue(FString::Printf(TEXT("driving: %s is on the pad's %s"), E.Key, *E.Value.ToString()), Keys.Contains(E.Value));
		}
		// One driving action per pad button.
		TMap<FKey, FName> Used;
		for (const FOsdsBindingSpec& B : UOsdsInputConfig::Bindings())
		{
			if (!B.Key.IsGamepadKey()) continue;
			if (const FName* Other = Used.Find(B.Key)) TestTrue(FString::Printf(TEXT("driving: %s is on one action only (%s, %s)"), *B.Key.ToString(), *Other->ToString(), *FName(B.Action).ToString()), *Other == FName(B.Action));
			else Used.Add(B.Key, B.Action);
		}
		// The right stick looks around (the controller reads it), so no action takes it.
		TestFalse(TEXT("driving: the right stick is free for looking around"), Used.Contains(EKeys::Gamepad_RightX) || Used.Contains(EKeys::Gamepad_RightY));
		// The left-stick click stays free (pressed by accident in hard steering, 2.0.1): the lens is in Seat & view.
		TestFalse(TEXT("driving: the left-stick click does nothing while driving"), Used.Contains(EKeys::Gamepad_LeftThumbstick));
		// The rest have no button of their own; the pad reaches them in the pause menu.
		auto HasRow = [](int32 P, EOsdsMenuRow Row) { for (const SOsdsPauseMenu::FRowInfo& R : SOsdsPauseMenu::Rows(P)) if (R.Row == Row) return true; return false; };
		TestTrue(TEXT("pad: the sound is in Settings"), PadKeyOf(TEXT("ToggleSound")).Num() > 0 || HasRow(3, EOsdsMenuRow::Sound));
		TestTrue(TEXT("pad: the headlights are in Your car"), PadKeyOf(TEXT("ToggleLights")).Num() > 0 || HasRow(1, EOsdsMenuRow::Lights));
		TestTrue(TEXT("pad: Where to? is on the Drive page"), PadKeyOf(TEXT("FindPlace")).Num() > 0 || HasRow(0, EOsdsMenuRow::WhereTo));
		TestTrue(TEXT("pad: the lens is in Seat & view"), PadKeyOf(TEXT("CycleLens")).Num() > 0 || HasRow(2, EOsdsMenuRow::Lens));
		TestTrue(TEXT("pad: the HUD level and the map are in Settings"), HasRow(3, EOsdsMenuRow::HudLevel) && HasRow(3, EOsdsMenuRow::HudMap));
		TestTrue(TEXT("pad: quitting is on the Drive page"), HasRow(0, EOsdsMenuRow::Quit));
	}

	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("No Slate application: the menu part needs one"));
		return true;
	}

	// ------------------------------------------------------------------ quit, with a confirm (the controller's list)
	{
		TArray<SOsdsListMenu::FItem> Items;
		for (const TCHAR* T : { TEXT("Keep driving"), TEXT("Quit to the desktop") }) { SOsdsListMenu::FItem I; I.Title = T; Items.Add(I); }
		int32 Chosen = -1, Back = 0;
		TSharedRef<SOsdsListMenu> Confirm = SNew(SOsdsListMenu).Title(TEXT("Quit the game?")).Items(Items)
			.OnChoose_Lambda([&Chosen](int32 i) { Chosen = i; }).OnBack_Lambda([&Back]() { ++Back; });
		FPadScreen Pad(Confirm);
		TestTrue(TEXT("quit: opens on Keep driving (a stray A keeps you driving)"), Pad.On(Confirm->GetRowWidget(0)));
		Pad.Press(EKeys::Gamepad_FaceButton_Bottom);
		TestEqual(TEXT("quit: A on Keep driving"), Chosen, 0);
		Pad.Press(EKeys::Gamepad_DPad_Down);
		Pad.Press(EKeys::Gamepad_FaceButton_Bottom);
		TestEqual(TEXT("quit: Down, A quits"), Chosen, 1);
		Pad.Press(EKeys::Gamepad_FaceButton_Right);
		TestEqual(TEXT("quit: B goes back"), Back, 1);
	}

	// ------------------------------------------------------------------ Tune car and Seat & view: every row, D-pad and stick
	{
		TSharedRef<SOsdsTuningMenu> Tune = SNew(SOsdsTuningMenu);
		// With no car the menu tunes the default car (a 1999 NB): every row has its real range.
		FPadScreen Pad(Tune);
		int32 Walked = 0;
		for (int32 PageIndex = 0; PageIndex < Tune->NumPages(); ++PageIndex)
		{
			TestEqual(TEXT("Tune car: RB pages through in order"), Tune->GetPage(), PageIndex);
			TSet<int32> Rows;
			for (int32 i = 0; i < 40 && !Rows.Contains(Tune->GetSelected()); ++i)
			{
				Rows.Add(Tune->GetSelected());
				const uint32 Before = FCrc::MemCrc32(&Tune->GetWorking(), sizeof(FOsdsTuning));
				Pad.Press(EKeys::Gamepad_DPad_Right);
				const uint32 Up = FCrc::MemCrc32(&Tune->GetWorking(), sizeof(FOsdsTuning));
				Pad.Press(EKeys::Gamepad_DPad_Left);
				Pad.Press(EKeys::Gamepad_DPad_Left);
				const uint32 Down = FCrc::MemCrc32(&Tune->GetWorking(), sizeof(FOsdsTuning));
				TestTrue(FString::Printf(TEXT("Tune car: page %d row %s changes with the D-pad"), PageIndex, *Tune->GetSelectedRowName()), Up != Before || Down != Before);
				// Y-less reset of this one row: X puts it back as the real car's.
				Pad.Press(EKeys::Gamepad_FaceButton_Left);
				Pad.Press(EKeys::Gamepad_DPad_Down);
			}
			TestTrue(TEXT("Tune car: the D-pad comes round to the page's first row"), Tune->GetSelected() == 0 && Rows.Num() == Tune->NumRows());
			Walked += Rows.Num();
			Pad.Press(EKeys::Gamepad_RightShoulder);
		}
		AddInfo(FString::Printf(TEXT("Tune car: %d rows on %d pages walked with the pad"), Walked, Tune->NumPages()));
		TestTrue(TEXT("Tune car: RB comes round to the first page"), Tune->GetPage() == 0 && Tune->NumPages() == 6 && Walked >= 25);
		Pad.Press(EKeys::Gamepad_LeftShoulder);
		TestEqual(TEXT("Tune car: LB goes back a page (round to the last)"), Tune->GetPage(), Tune->NumPages() - 1);
		Pad.Press(EKeys::Gamepad_RightShoulder);
		Pad.Stick(EKeys::Gamepad_LeftY, -1.f);
		TestEqual(TEXT("Tune car: one stick push down is one row"), Tune->GetSelected(), 1);
		Pad.Hold(EKeys::Gamepad_LeftY, -1.f);
		Pad.Hold(EKeys::Gamepad_LeftY, -1.f);
		TestEqual(TEXT("Tune car: a held stick is one step (until the repeat delay)"), Tune->GetSelected(), 2);
		Pad.Hold(EKeys::Gamepad_LeftY, 0.f);
		const uint32 Before = FCrc::MemCrc32(&Tune->GetWorking(), sizeof(FOsdsTuning));
		Pad.Stick(EKeys::Gamepad_LeftX, 1.f);
		TestTrue(TEXT("Tune car: the stick right changes the row"), FCrc::MemCrc32(&Tune->GetWorking(), sizeof(FOsdsTuning)) != Before);
		TestTrue(TEXT("Tune car: the pad stays on the menu"), Pad.On(Tune));
	}
	{
		TSharedRef<SOsdsSeatMenu> Seat = SNew(SOsdsSeatMenu);
		FPadScreen Pad(Seat);
		for (int32 Row = 0; Row < 4; ++Row)
		{
			TestEqual(FString::Printf(TEXT("Seat & view: the D-pad reaches row %d"), Row), Seat->GetSelected(), Row);
			const FOsdsSeat Before = Seat->GetWorking();
			Pad.Press(EKeys::Gamepad_DPad_Right);
			Pad.Press(EKeys::Gamepad_DPad_Right);
			const FOsdsSeat After = Seat->GetWorking();
			TestTrue(FString::Printf(TEXT("Seat & view: row %d changes with the D-pad"), Row), FMemory::Memcmp(&Before, &After, sizeof(FOsdsSeat)) != 0);
			Pad.Press(EKeys::Gamepad_FaceButton_Left);
			Pad.Press(EKeys::Gamepad_DPad_Down);
		}
		TestEqual(TEXT("Seat & view: Down from the last row comes round"), Seat->GetSelected(), 0);
		Pad.Stick(EKeys::Gamepad_LeftY, -1.f);
		TestEqual(TEXT("Seat & view: one stick push down is one row"), Seat->GetSelected(), 1);
		Pad.Stick(EKeys::Gamepad_LeftY, 1.f);
		TestEqual(TEXT("Seat & view: one stick push up is one row"), Seat->GetSelected(), 0);
	}

	// ------------------------------------------------------------------ Plan a route: move, zoom, start and back with the pad
	{
		int32 Started = 0, Back = 0;
		TSharedRef<SOsdsRoutePlanner> Planner = SNew(SOsdsRoutePlanner)
			.OnStart_Lambda([&Started]() { ++Started; })
			.OnBack_Lambda([&Back]() { ++Back; });
		FPadScreen Pad(Planner);
		const FVector2D Cross = Planner->GetCrossM();
		Pad.Press(EKeys::Gamepad_DPad_Right);
		TestTrue(TEXT("Plan a route: the D-pad moves the cross"), Planner->GetCrossM().X > Cross.X);
		const int32 Level = Planner->GetLevel();
		Pad.Press(EKeys::Gamepad_RightTrigger);
		TestTrue(TEXT("Plan a route: RT zooms"), Planner->GetLevel() != Level);
		Pad.Press(EKeys::Gamepad_FaceButton_Top);
		TestEqual(TEXT("Plan a route: Y starts the route"), Started, 1);
		Pad.Press(EKeys::Gamepad_FaceButton_Right);
		TestEqual(TEXT("Plan a route: B goes back"), Back, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsPadHintsTest, "OSDS.UI.PadHints", OsdsPadReachTest::Flags)
bool FOsdsPadHintsTest::RunTest(const FString& Parameters)
{
	// The button hints follow the last device used, on every screen: a pad key sent through
	// FSlateApplication shows the pad's buttons, a keyboard key the keyboard's keys.
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("No Slate application: the hints test needs one"));
		return true;
	}
	TSharedRef<SOsdsPauseMenu> Menu = SNew(SOsdsPauseMenu)
		.GetState_Lambda([]() { FOsdsMenuState S; S.bPad = false; return S; });
	FSlateApplication& App = FSlateApplication::Get();
	auto Tap = [&App](const FKey& Key)
	{
		const FKeyEvent Event(Key, FModifierKeysState(), 0, false, 0, 0);
		App.ProcessKeyDownEvent(Event);
		App.ProcessKeyUpEvent(Event);
	};
	// Keys nothing in the editor is bound to.
	Tap(EKeys::ScrollLock);
	TestFalse(TEXT("after a keyboard key the pause menu shows the keyboard's keys"), Menu->ShowsPadHints());
	Tap(EKeys::Gamepad_LeftThumbstick);
	TestTrue(TEXT("after one pad button the pause menu shows the pad's buttons"), Menu->ShowsPadHints());
	App.ProcessAnalogInputEvent(FAnalogInputEvent(EKeys::Gamepad_LeftY, FModifierKeysState(), 0, false, 0, 0, 0.2f));
	Tap(EKeys::ScrollLock);
	TestFalse(TEXT("a keyboard key switches back"), Menu->ShowsPadHints());
	App.ProcessAnalogInputEvent(FAnalogInputEvent(EKeys::Gamepad_LeftY, FModifierKeysState(), 0, false, 0, 0, 0.2f));
	TestFalse(TEXT("a stick resting near the middle does not switch"), Menu->ShowsPadHints());
	App.ProcessAnalogInputEvent(FAnalogInputEvent(EKeys::Gamepad_LeftY, FModifierKeysState(), 0, false, 0, 0, 0.9f));
	TestTrue(TEXT("a stick pushed switches to the pad"), Menu->ShowsPadHints());
	App.ProcessAnalogInputEvent(FAnalogInputEvent(EKeys::Gamepad_LeftY, FModifierKeysState(), 0, false, 0, 0, 0.f));
	Tap(EKeys::ScrollLock);
	return true;
}

#endif
