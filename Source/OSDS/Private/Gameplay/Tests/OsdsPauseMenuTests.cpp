// Copyright OSDS contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Environment/OsdsGraphics.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Gameplay/OsdsPauseMenu.h"
#include "Gameplay/OsdsPreferences.h"
#include "Gameplay/OsdsUiTheme.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace OsdsPauseMenuTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	struct FScopedGraphicsPrefsDir
	{
		FString Before;
		FString Dir;
		FScopedGraphicsPrefsDir()
		{
			Before = FPlatformMisc::GetEnvironmentVariable(TEXT("OSDS_QA_PREFS_DIR"));
			Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"),
				TEXT("Graphics-") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
			IFileManager::Get().MakeDirectory(*Dir, true);
			FPlatformMisc::SetEnvironmentVar(TEXT("OSDS_QA_PREFS_DIR"), *Dir);
		}
		~FScopedGraphicsPrefsDir()
		{
			FPlatformMisc::SetEnvironmentVar(TEXT("OSDS_QA_PREFS_DIR"), *Before);
			IFileManager::Get().DeleteDirectory(*Dir, false, true);
		}
	};

	FReply Press(SWidget& Widget, const FKey& Key)
	{
		const FKeyEvent Event(Key, FModifierKeysState(), 0, false, 0, 0);
		return Widget.OnKeyDown(FGeometry(), Event);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsPauseMenuPadTest, "OSDS.UI.PauseMenuPad", OsdsPauseMenuTest::Flags)
bool FOsdsPauseMenuPadTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("No Slate application: the menu test needs one"));
		return true;
	}
	using OsdsPauseMenuTest::Press;
	TArray<TPair<EOsdsMenuRow, int32>> Actions;
	TArray<int32> PagesSeen;
	int32 Closed = 0;
	bool bRoute = false;
	TSharedRef<SOsdsPauseMenu> Menu = SNew(SOsdsPauseMenu)
		.GetState_Lambda([&bRoute]() { FOsdsMenuState S; S.Place = TEXT("Ocean Drive"); S.bRoute = bRoute; return S; })
		.OnAction_Lambda([&Actions](EOsdsMenuRow Row, int32 Dir) { Actions.Add({ Row, Dir }); })
		.OnPage_Lambda([&PagesSeen](int32 Page) { PagesSeen.Add(Page); })
		.OnClose_Lambda([&Closed]() { ++Closed; });

	TestEqual(TEXT("opens on Drive"), Menu->GetPage(), 0);
	TestEqual(TEXT("on Continue driving"), Menu->GetSelectedRow(), EOsdsMenuRow::Continue);
	TestTrue(TEXT("D-pad down is handled"), Press(*Menu, EKeys::Gamepad_DPad_Down).IsEventHandled());
	Press(*Menu, EKeys::Gamepad_DPad_Down);
	TestEqual(TEXT("two down: Plan a route"), Menu->GetSelectedRow(), EOsdsMenuRow::PlanRoute);
	Press(*Menu, EKeys::Gamepad_FaceButton_Bottom);
	TestTrue(TEXT("A chooses Plan a route"), Actions.Num() == 1 && Actions[0].Key == EOsdsMenuRow::PlanRoute && Actions[0].Value == 0);
	Press(*Menu, EKeys::Gamepad_DPad_Up);
	Press(*Menu, EKeys::Gamepad_DPad_Up);
	Press(*Menu, EKeys::Gamepad_DPad_Up);
	TestTrue(TEXT("up from the first row goes onto the tab row"), Menu->IsOnTabs() && Menu->GetTabCursor() == 0);
	Press(*Menu, EKeys::Gamepad_DPad_Down);
	TestTrue(TEXT("down from the tab row: the first row"), !Menu->IsOnTabs() && Menu->GetSelectedRow() == EOsdsMenuRow::Continue);
	Press(*Menu, EKeys::Gamepad_DPad_Right);
	TestEqual(TEXT("right on an action row does nothing"), Actions.Num(), 1);
	Press(*Menu, EKeys::Gamepad_FaceButton_Left);
	TestEqual(TEXT("X without a route does nothing"), Actions.Num(), 1);
	bRoute = true;
	Menu->Tick(FGeometry(), 0.0, 0.016f);
	Press(*Menu, EKeys::Gamepad_FaceButton_Left);
	TestTrue(TEXT("X with a route ends it"), Actions.Num() == 2 && Actions[1].Key == EOsdsMenuRow::EndRoute);

	Press(*Menu, EKeys::Gamepad_RightShoulder);
	TestEqual(TEXT("RB: Your car"), Menu->GetPage(), 1);
	Press(*Menu, EKeys::Gamepad_DPad_Down);
	TestEqual(TEXT("the gearbox row"), Menu->GetSelectedRow(), EOsdsMenuRow::Transmission);
	Press(*Menu, EKeys::Gamepad_DPad_Right);
	TestTrue(TEXT("right changes the gearbox"), Actions.Last().Key == EOsdsMenuRow::Transmission && Actions.Last().Value == 1);
	Press(*Menu, EKeys::Gamepad_RightShoulder);
	TestEqual(TEXT("RB: Seat & view"), Menu->GetPage(), 2);
	Press(*Menu, EKeys::Gamepad_DPad_Down);
	Press(*Menu, EKeys::Gamepad_DPad_Left);
	TestTrue(TEXT("left lowers head motion"), Actions.Last().Key == EOsdsMenuRow::HeadMotion && Actions.Last().Value == -1);
	Press(*Menu, EKeys::Gamepad_FaceButton_Top);
	TestTrue(TEXT("Y: back to stock"), Actions.Last().Key == EOsdsMenuRow::HeadMotion && Actions.Last().Value == 2);
	Press(*Menu, EKeys::Gamepad_LeftShoulder);
	Press(*Menu, EKeys::Gamepad_LeftShoulder);
	Press(*Menu, EKeys::Gamepad_LeftShoulder);
	TestEqual(TEXT("LB from Drive wraps to Settings"), Menu->GetPage(), 3);
	TestEqual(TEXT("the page change is reported (Seat & view unpauses the world)"), PagesSeen.Contains(2), true);
	TestEqual(TEXT("every page opens on its first row"), (Menu->SetPage(1), Menu->GetSelectedRow()), EOsdsMenuRow::Car);

	// The keyboard does the same.
	Press(*Menu, EKeys::Q);
	TestEqual(TEXT("Q: previous page"), Menu->GetPage(), 0);
	TestEqual(TEXT("the Drive page opens on its first row"), Menu->GetSelectedRow(), EOsdsMenuRow::Continue);
	Press(*Menu, EKeys::Down);
	Press(*Menu, EKeys::Enter);
	TestEqual(TEXT("Enter chooses Where to?"), Actions.Last().Key, EOsdsMenuRow::WhereTo);
	TestFalse(TEXT("a letter that means nothing is not handled"), Press(*Menu, EKeys::J).IsEventHandled());

	Press(*Menu, EKeys::Gamepad_FaceButton_Right);
	TestEqual(TEXT("B closes"), Closed, 1);
	Press(*Menu, EKeys::Gamepad_Special_Right);
	TestEqual(TEXT("Menu closes"), Closed, 2);
	Press(*Menu, EKeys::Escape);
	TestEqual(TEXT("Esc closes"), Closed, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsGraphicsSettingPadTest, "OSDS.UI.GraphicsSettingPad", OsdsPauseMenuTest::Flags)
bool FOsdsGraphicsSettingPadTest::RunTest(const FString& Parameters)
{
	using OsdsPauseMenuTest::Press;
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("No Slate application: the graphics setting test needs one"));
		return true;
	}
	const TArray<SOsdsPauseMenu::FRowInfo>& Settings = SOsdsPauseMenu::Rows(3);
	const int32 GraphicsIndex = Settings.IndexOfByPredicate([](const SOsdsPauseMenu::FRowInfo& Row)
	{
		return Row.Title == TEXT("Graphics quality");
	});
	if (!TestTrue(TEXT("Settings has a Graphics quality choice"), GraphicsIndex != INDEX_NONE)) return false;

	TArray<TPair<EOsdsMenuRow, int32>> Actions;
	TSharedRef<SOsdsPauseMenu> Menu = SNew(SOsdsPauseMenu)
		.GetState_Lambda([]() { return FOsdsMenuState(); })
		.OnAction_Lambda([&Actions](EOsdsMenuRow Row, int32 Dir) { Actions.Add({ Row, Dir }); });
	Menu->SetPage(3);
	for (int32 i = 0; i < GraphicsIndex; ++i) Press(*Menu, EKeys::Gamepad_DPad_Down);
	TestEqual(TEXT("D-pad reaches Graphics quality"), Settings[Menu->GetSelected()].Title, FString(TEXT("Graphics quality")));
	const TArray<OsdsTheme::FHint> Hints = Menu->GetPadHints();
	TestTrue(TEXT("Graphics quality explains the Y reset to Auto"), Hints.ContainsByPredicate([](const OsdsTheme::FHint& Hint)
	{
		return Hint.Glyph == TEXT("Y") && Hint.Text == TEXT("Reset to Auto");
	}));
	Press(*Menu, EKeys::Gamepad_DPad_Left);
	Press(*Menu, EKeys::Gamepad_DPad_Right);
	Press(*Menu, EKeys::Gamepad_FaceButton_Bottom);
	Press(*Menu, EKeys::Gamepad_FaceButton_Top);
	TestTrue(TEXT("left, right, A and Y reach the graphics choice and reset actions"),
		Actions.Num() == 4 &&
		Actions[0].Key == EOsdsMenuRow::Graphics && Actions[0].Value == -1 &&
		Actions[1].Key == Actions[0].Key && Actions[1].Value == 1 &&
		Actions[2].Key == Actions[0].Key && Actions[2].Value == 0 &&
		Actions[3].Key == Actions[0].Key && Actions[3].Value == 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsGraphicsSettingChoiceTest, "OSDS.UI.GraphicsSettingChoicePersistence", OsdsPauseMenuTest::Flags)
bool FOsdsGraphicsSettingChoiceTest::RunTest(const FString& Parameters)
{
	const OsdsPauseMenuTest::FScopedGraphicsPrefsDir Prefs;
	const EOsdsGraphicsPreset BeforePreset = FOsdsGraphics::Current();
	ON_SCOPE_EXIT
	{
		FOsdsGraphics::Apply(BeforePreset);
	};

	TestEqual(TEXT("settings default to Auto"), FOsdsGraphics::SavedChoice(), EOsdsGraphicsChoice::Auto);
	FOsdsGraphics::ChooseQuality(0);
	TestEqual(TEXT("A advances Auto to Low"), FOsdsGraphics::SavedChoice(), EOsdsGraphicsChoice::Low);
	FOsdsGraphics::ChooseQuality(-1);
	TestEqual(TEXT("left wraps Low to Auto"), FOsdsGraphics::SavedChoice(), EOsdsGraphicsChoice::Auto);
	FOsdsGraphics::ChooseQuality(1);
	FOsdsGraphics::ChooseQuality(1);
	FOsdsGraphics::ChooseQuality(1);
	TestEqual(TEXT("right cycles to High"), FOsdsGraphics::SavedChoice(), EOsdsGraphicsChoice::High);
	TestEqual(TEXT("the current choice is shown"), FOsdsGraphics::ChoiceDisplay(), FString(TEXT("High")));

	FString SavedPrefs;
	TestTrue(TEXT("the selected quality is persisted"), FFileHelper::LoadFileToString(SavedPrefs, *OsdsPreferences::Path()));
	TSharedPtr<FJsonObject> PrefsDoc;
	const TSharedRef<TJsonReader<>> PrefsReader = TJsonReaderFactory<>::Create(SavedPrefs);
	TestTrue(TEXT("the preference file is valid JSON"), FJsonSerializer::Deserialize(PrefsReader, PrefsDoc) && PrefsDoc.IsValid());
	FString Quality;
	TestTrue(TEXT("the preference file records High"), PrefsDoc.IsValid() && PrefsDoc->TryGetStringField(TEXT("graphics_quality"), Quality) && Quality == TEXT("high"));

	FOsdsGraphics::ChooseQuality(2);
	TestEqual(TEXT("Y resets the choice to Auto"), FOsdsGraphics::SavedChoice(), EOsdsGraphicsChoice::Auto);
	TestTrue(TEXT("Auto shows the resolved current preset"), FOsdsGraphics::ChoiceDisplay().StartsWith(TEXT("Auto (")));
	TestTrue(TEXT("the reset is persisted"), FFileHelper::LoadFileToString(SavedPrefs, *OsdsPreferences::Path()));
	PrefsDoc.Reset();
	const TSharedRef<TJsonReader<>> ResetReader = TJsonReaderFactory<>::Create(SavedPrefs);
	TestTrue(TEXT("the reset preference file is valid JSON"), FJsonSerializer::Deserialize(ResetReader, PrefsDoc) && PrefsDoc.IsValid());
	Quality.Reset();
	TestTrue(TEXT("the preference file records Auto"), PrefsDoc.IsValid() && PrefsDoc->TryGetStringField(TEXT("graphics_quality"), Quality) && Quality == TEXT("auto"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsThemeTest, "OSDS.UI.Theme", OsdsPauseMenuTest::Flags)
bool FOsdsThemeTest::RunTest(const FString& Parameters)
{
	using namespace OsdsTheme;
	TestTrue(TEXT("the Overpass files are in data/fonts/overpass"), HasOverpass());
	TestEqual(TEXT("guide green"), Guide().ToFColor(true), FColor(0x0A, 0x5C, 0x3A));
	TestEqual(TEXT("course magenta"), Course().ToFColor(true), FColor(0xE2, 0x3A, 0x9C));
	TestEqual(TEXT("a font for 34 px is 25.5 pt"), Font(EWeight::ExtraBold, 34.f).Size, 25.5f);
	TestEqual(TEXT("LB is the previous page"), NavOf(EKeys::Gamepad_LeftShoulder), ENav::PrevPage);
	TestEqual(TEXT("A accepts"), NavOf(EKeys::Gamepad_FaceButton_Bottom), ENav::Accept);
	TestEqual(TEXT("RT zooms in"), NavOf(EKeys::Gamepad_RightTrigger), ENav::ZoomIn);
	for (int32 i = 0; i <= static_cast<int32>(EOsdsTurn::Arrive); ++i)
	{
		TArray<TArray<FVector2D>> Strokes;
		TurnStrokes(static_cast<EOsdsTurn>(i), Strokes);
		bool bInside = Strokes.Num() > 0;
		for (const TArray<FVector2D>& S : Strokes) for (const FVector2D& P : S) bInside &= P.X >= -0.01 && P.X <= 1.01 && P.Y >= -0.01 && P.Y <= 1.01;
		TestTrue(FString::Printf(TEXT("turn arrow %d is inside its box"), i), bInside);
	}
	return true;
}

#endif
