// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Environment/SimCruiseGraphics.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Gameplay/SimCruisePauseMenu.h"
#include "Gameplay/SimCruisePreferences.h"
#include "Gameplay/SimCruiseUiTheme.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace SimCruisePauseMenuTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	struct FScopedGraphicsPrefsDir
	{
		FString Before;
		FString Dir;
		FScopedGraphicsPrefsDir()
		{
			Before = FPlatformMisc::GetEnvironmentVariable(TEXT("SIMCRUISE_QA_PREFS_DIR"));
			Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"),
				TEXT("Graphics-") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
			IFileManager::Get().MakeDirectory(*Dir, true);
			FPlatformMisc::SetEnvironmentVar(TEXT("SIMCRUISE_QA_PREFS_DIR"), *Dir);
		}
		~FScopedGraphicsPrefsDir()
		{
			FPlatformMisc::SetEnvironmentVar(TEXT("SIMCRUISE_QA_PREFS_DIR"), *Before);
			IFileManager::Get().DeleteDirectory(*Dir, false, true);
		}
	};

	FReply Press(SWidget& Widget, const FKey& Key)
	{
		const FKeyEvent Event(Key, FModifierKeysState(), 0, false, 0, 0);
		return Widget.OnKeyDown(FGeometry(), Event);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruisePauseMenuPadTest, "SimCruise.UI.PauseMenuPad", SimCruisePauseMenuTest::Flags)
bool FSimCruisePauseMenuPadTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("No Slate application: the menu test needs one"));
		return true;
	}
	using SimCruisePauseMenuTest::Press;
	TArray<TPair<ESimCruiseMenuRow, int32>> Actions;
	TArray<int32> PagesSeen;
	int32 Closed = 0;
	bool bRoute = false;
	TSharedRef<SSimCruisePauseMenu> Menu = SNew(SSimCruisePauseMenu)
		.GetState_Lambda([&bRoute]() { FSimCruiseMenuState S; S.Place = TEXT("Ocean Drive"); S.bRoute = bRoute; return S; })
		.OnAction_Lambda([&Actions](ESimCruiseMenuRow Row, int32 Dir) { Actions.Add({ Row, Dir }); })
		.OnPage_Lambda([&PagesSeen](int32 Page) { PagesSeen.Add(Page); })
		.OnClose_Lambda([&Closed]() { ++Closed; });

	TestEqual(TEXT("opens on Drive"), Menu->GetPage(), 0);
	TestEqual(TEXT("on Continue driving"), Menu->GetSelectedRow(), ESimCruiseMenuRow::Continue);
	TestTrue(TEXT("D-pad down is handled"), Press(*Menu, EKeys::Gamepad_DPad_Down).IsEventHandled());
	Press(*Menu, EKeys::Gamepad_DPad_Down);
	TestEqual(TEXT("two down: Plan a route"), Menu->GetSelectedRow(), ESimCruiseMenuRow::PlanRoute);
	Press(*Menu, EKeys::Gamepad_FaceButton_Bottom);
	TestTrue(TEXT("A chooses Plan a route"), Actions.Num() == 1 && Actions[0].Key == ESimCruiseMenuRow::PlanRoute && Actions[0].Value == 0);
	Press(*Menu, EKeys::Gamepad_DPad_Up);
	Press(*Menu, EKeys::Gamepad_DPad_Up);
	Press(*Menu, EKeys::Gamepad_DPad_Up);
	TestTrue(TEXT("up from the first row goes onto the tab row"), Menu->IsOnTabs() && Menu->GetTabCursor() == 0);
	Press(*Menu, EKeys::Gamepad_DPad_Down);
	TestTrue(TEXT("down from the tab row: the first row"), !Menu->IsOnTabs() && Menu->GetSelectedRow() == ESimCruiseMenuRow::Continue);
	Press(*Menu, EKeys::Gamepad_DPad_Right);
	TestEqual(TEXT("right on an action row does nothing"), Actions.Num(), 1);
	Press(*Menu, EKeys::Gamepad_FaceButton_Left);
	TestEqual(TEXT("X without a route does nothing"), Actions.Num(), 1);
	bRoute = true;
	Menu->Tick(FGeometry(), 0.0, 0.016f);
	Press(*Menu, EKeys::Gamepad_FaceButton_Left);
	TestTrue(TEXT("X with a route ends it"), Actions.Num() == 2 && Actions[1].Key == ESimCruiseMenuRow::EndRoute);

	Press(*Menu, EKeys::Gamepad_RightShoulder);
	TestEqual(TEXT("RB: Your car"), Menu->GetPage(), 1);
	Press(*Menu, EKeys::Gamepad_DPad_Down);
	TestEqual(TEXT("the gearbox row"), Menu->GetSelectedRow(), ESimCruiseMenuRow::Transmission);
	Press(*Menu, EKeys::Gamepad_DPad_Right);
	TestTrue(TEXT("right changes the gearbox"), Actions.Last().Key == ESimCruiseMenuRow::Transmission && Actions.Last().Value == 1);
	Press(*Menu, EKeys::Gamepad_RightShoulder);
	TestEqual(TEXT("RB: Seat & view"), Menu->GetPage(), 2);
	Press(*Menu, EKeys::Gamepad_DPad_Down);
	Press(*Menu, EKeys::Gamepad_DPad_Left);
	TestTrue(TEXT("left lowers head motion"), Actions.Last().Key == ESimCruiseMenuRow::HeadMotion && Actions.Last().Value == -1);
	Press(*Menu, EKeys::Gamepad_FaceButton_Top);
	TestTrue(TEXT("Y: back to stock"), Actions.Last().Key == ESimCruiseMenuRow::HeadMotion && Actions.Last().Value == 2);
	Press(*Menu, EKeys::Gamepad_LeftShoulder);
	Press(*Menu, EKeys::Gamepad_LeftShoulder);
	Press(*Menu, EKeys::Gamepad_LeftShoulder);
	TestEqual(TEXT("LB from Drive wraps to Settings"), Menu->GetPage(), 3);
	TestEqual(TEXT("the page change is reported (Seat & view unpauses the world)"), PagesSeen.Contains(2), true);
	TestEqual(TEXT("every page opens on its first row"), (Menu->SetPage(1), Menu->GetSelectedRow()), ESimCruiseMenuRow::Car);

	// The keyboard does the same.
	Press(*Menu, EKeys::Q);
	TestEqual(TEXT("Q: previous page"), Menu->GetPage(), 0);
	TestEqual(TEXT("the Drive page opens on its first row"), Menu->GetSelectedRow(), ESimCruiseMenuRow::Continue);
	Press(*Menu, EKeys::Down);
	Press(*Menu, EKeys::Enter);
	TestEqual(TEXT("Enter chooses Where to?"), Actions.Last().Key, ESimCruiseMenuRow::WhereTo);
	TestFalse(TEXT("a letter that means nothing is not handled"), Press(*Menu, EKeys::J).IsEventHandled());

	Press(*Menu, EKeys::Gamepad_FaceButton_Right);
	TestEqual(TEXT("B closes"), Closed, 1);
	Press(*Menu, EKeys::Gamepad_Special_Right);
	TestEqual(TEXT("Menu closes"), Closed, 2);
	Press(*Menu, EKeys::Escape);
	TestEqual(TEXT("Esc closes"), Closed, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseGraphicsSettingPadTest, "SimCruise.UI.GraphicsSettingPad", SimCruisePauseMenuTest::Flags)
bool FSimCruiseGraphicsSettingPadTest::RunTest(const FString& Parameters)
{
	using SimCruisePauseMenuTest::Press;
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("No Slate application: the graphics setting test needs one"));
		return true;
	}
	const TArray<SSimCruisePauseMenu::FRowInfo>& Settings = SSimCruisePauseMenu::Rows(3);
	const int32 GraphicsIndex = Settings.IndexOfByPredicate([](const SSimCruisePauseMenu::FRowInfo& Row)
	{
		return Row.Title == TEXT("Graphics quality");
	});
	if (!TestTrue(TEXT("Settings has a Graphics quality choice"), GraphicsIndex != INDEX_NONE)) return false;

	TArray<TPair<ESimCruiseMenuRow, int32>> Actions;
	TSharedRef<SSimCruisePauseMenu> Menu = SNew(SSimCruisePauseMenu)
		.GetState_Lambda([]() { return FSimCruiseMenuState(); })
		.OnAction_Lambda([&Actions](ESimCruiseMenuRow Row, int32 Dir) { Actions.Add({ Row, Dir }); });
	Menu->SetPage(3);
	for (int32 i = 0; i < GraphicsIndex; ++i) Press(*Menu, EKeys::Gamepad_DPad_Down);
	TestEqual(TEXT("D-pad reaches Graphics quality"), Settings[Menu->GetSelected()].Title, FString(TEXT("Graphics quality")));
	const TArray<SimCruiseTheme::FHint> Hints = Menu->GetPadHints();
	TestTrue(TEXT("Graphics quality explains the Y reset to Auto"), Hints.ContainsByPredicate([](const SimCruiseTheme::FHint& Hint)
	{
		return Hint.Glyph == TEXT("Y") && Hint.Text == TEXT("Reset to Auto");
	}));
	Press(*Menu, EKeys::Gamepad_DPad_Left);
	Press(*Menu, EKeys::Gamepad_DPad_Right);
	Press(*Menu, EKeys::Gamepad_FaceButton_Bottom);
	Press(*Menu, EKeys::Gamepad_FaceButton_Top);
	TestTrue(TEXT("left, right, A and Y reach the graphics choice and reset actions"),
		Actions.Num() == 4 &&
		Actions[0].Key == ESimCruiseMenuRow::Graphics && Actions[0].Value == -1 &&
		Actions[1].Key == Actions[0].Key && Actions[1].Value == 1 &&
		Actions[2].Key == Actions[0].Key && Actions[2].Value == 0 &&
		Actions[3].Key == Actions[0].Key && Actions[3].Value == 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseGraphicsSettingChoiceTest, "SimCruise.UI.GraphicsSettingChoicePersistence", SimCruisePauseMenuTest::Flags)
bool FSimCruiseGraphicsSettingChoiceTest::RunTest(const FString& Parameters)
{
	const SimCruisePauseMenuTest::FScopedGraphicsPrefsDir Prefs;
	const ESimCruiseGraphicsPreset BeforePreset = FSimCruiseGraphics::Current();
	ON_SCOPE_EXIT
	{
		FSimCruiseGraphics::Apply(BeforePreset);
	};

	TestEqual(TEXT("settings default to Auto"), FSimCruiseGraphics::SavedChoice(), ESimCruiseGraphicsChoice::Auto);
	FSimCruiseGraphics::ChooseQuality(0);
	TestEqual(TEXT("A advances Auto to Low"), FSimCruiseGraphics::SavedChoice(), ESimCruiseGraphicsChoice::Low);
	FSimCruiseGraphics::ChooseQuality(-1);
	TestEqual(TEXT("left wraps Low to Auto"), FSimCruiseGraphics::SavedChoice(), ESimCruiseGraphicsChoice::Auto);
	FSimCruiseGraphics::ChooseQuality(1);
	FSimCruiseGraphics::ChooseQuality(1);
	FSimCruiseGraphics::ChooseQuality(1);
	TestEqual(TEXT("right cycles to High"), FSimCruiseGraphics::SavedChoice(), ESimCruiseGraphicsChoice::High);
	TestEqual(TEXT("the current choice is shown"), FSimCruiseGraphics::ChoiceDisplay(), FString(TEXT("High")));

	FString SavedPrefs;
	TestTrue(TEXT("the selected quality is persisted"), FFileHelper::LoadFileToString(SavedPrefs, *SimCruisePreferences::Path()));
	TSharedPtr<FJsonObject> PrefsDoc;
	const TSharedRef<TJsonReader<>> PrefsReader = TJsonReaderFactory<>::Create(SavedPrefs);
	TestTrue(TEXT("the preference file is valid JSON"), FJsonSerializer::Deserialize(PrefsReader, PrefsDoc) && PrefsDoc.IsValid());
	FString Quality;
	TestTrue(TEXT("the preference file records High"), PrefsDoc.IsValid() && PrefsDoc->TryGetStringField(TEXT("graphics_quality"), Quality) && Quality == TEXT("high"));

	FSimCruiseGraphics::ChooseQuality(2);
	TestEqual(TEXT("Y resets the choice to Auto"), FSimCruiseGraphics::SavedChoice(), ESimCruiseGraphicsChoice::Auto);
	TestTrue(TEXT("Auto shows the resolved current preset"), FSimCruiseGraphics::ChoiceDisplay().StartsWith(TEXT("Auto (")));
	TestTrue(TEXT("the reset is persisted"), FFileHelper::LoadFileToString(SavedPrefs, *SimCruisePreferences::Path()));
	PrefsDoc.Reset();
	const TSharedRef<TJsonReader<>> ResetReader = TJsonReaderFactory<>::Create(SavedPrefs);
	TestTrue(TEXT("the reset preference file is valid JSON"), FJsonSerializer::Deserialize(ResetReader, PrefsDoc) && PrefsDoc.IsValid());
	Quality.Reset();
	TestTrue(TEXT("the preference file records Auto"), PrefsDoc.IsValid() && PrefsDoc->TryGetStringField(TEXT("graphics_quality"), Quality) && Quality == TEXT("auto"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseThemeTest, "SimCruise.UI.Theme", SimCruisePauseMenuTest::Flags)
bool FSimCruiseThemeTest::RunTest(const FString& Parameters)
{
	using namespace SimCruiseTheme;
	TestTrue(TEXT("the Overpass files are in data/fonts/overpass"), HasOverpass());
	TestEqual(TEXT("guide green"), Guide().ToFColor(true), FColor(0x0A, 0x5C, 0x3A));
	TestEqual(TEXT("course magenta"), Course().ToFColor(true), FColor(0xE2, 0x3A, 0x9C));
	TestEqual(TEXT("a font for 34 px is 25.5 pt"), Font(EWeight::ExtraBold, 34.f).Size, 25.5f);
	TestEqual(TEXT("LB is the previous page"), NavOf(EKeys::Gamepad_LeftShoulder), ENav::PrevPage);
	TestEqual(TEXT("A accepts"), NavOf(EKeys::Gamepad_FaceButton_Bottom), ENav::Accept);
	TestEqual(TEXT("RT zooms in"), NavOf(EKeys::Gamepad_RightTrigger), ENav::ZoomIn);
	for (int32 i = 0; i <= static_cast<int32>(ESimCruiseTurn::Arrive); ++i)
	{
		TArray<TArray<FVector2D>> Strokes;
		TurnStrokes(static_cast<ESimCruiseTurn>(i), Strokes);
		bool bInside = Strokes.Num() > 0;
		for (const TArray<FVector2D>& S : Strokes) for (const FVector2D& P : S) bInside &= P.X >= -0.01 && P.X <= 1.01 && P.Y >= -0.01 && P.Y <= 1.01;
		TestTrue(FString::Printf(TEXT("turn arrow %d is inside its box"), i), bInside);
	}
	return true;
}

#endif
