// Copyright OSDS contributors. All Rights Reserved.

#include "Gameplay/OsdsSeatUI.h"

#include "Gameplay/OsdsAtlasUI.h"
#include "Gameplay/OsdsUiTheme.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "OsdsSeat"

void SOsdsSeatMenu::Construct(const FArguments& InArgs)
{
	Camera = InArgs._Camera;
	CarId = InArgs._CarId;
	OnClose = InArgs._OnClose;
	if (const AOsdsPlayerCameraManager* Cam = Camera.Get())
	{
		Original = Cam->Seat;
	}
	Working = Original;

	using namespace OsdsUiStyle;
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		List->AddSlot().AutoHeight().Padding(0.f, 4.f)
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Mono", 20))
			.Text(this, &SOsdsSeatMenu::RowText, Index)
			.ColorAndOpacity(this, &SOsdsSeatMenu::RowColor, Index)
		];
	}
	// A panel down the left side: the windscreen, the hood and the dash stay in sight.
	FLinearColor Panel = Ink();
	Panel.A = 0.88f;
	ChildSlot
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Bottom)
	.Padding(24.f)
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(Panel)
		.Padding(24.f)
		[
			SNew(SBox)
			.MinDesiredWidth(620.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 26))
					.ColorAndOpacity(Paper())
					.Text(FText::Format(LOCTEXT("Title", "Seat & view · {0}"), FText::FromString(InArgs._CarName)))
				]
				+ SVerticalBox::Slot().AutoHeight()[ List ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 18))
					.ColorAndOpacity(Paper())
					.AutoWrapText(true)
					// From the hint list (GetPadHints), so the help says what the keys do (OSDS.UI.PadHintsMatch).
					.Text_Lambda([]() { OsdsTheme::EnsurePadWatcher(); return FText::FromString(OsdsTheme::HintLine(GetPadHints(), OsdsTheme::IsPadInUse())); })
				]
			]
		]
	];
}

FText SOsdsSeatMenu::RowText(int32 Index) const
{
	const TCHAR* Mark = Index == Selected ? TEXT(">") : TEXT(" ");
	switch (Index)
	{
	case 0: return FText::FromString(FString::Printf(TEXT("%s %-16s %+5.0f cm"), Mark, TEXT("Seat height"), Working.UpCm));
	case 1: return FText::FromString(FString::Printf(TEXT("%s %-16s %+5.0f cm  %s"), Mark, TEXT("Seat fore-aft"), Working.ForwardCm,
		Working.ForwardCm > 0.0 ? TEXT("(forward)") : Working.ForwardCm < 0.0 ? TEXT("(back)") : TEXT("")));
	case 2: return FText::FromString(FString::Printf(TEXT("%s %-16s %+5.1f deg %s"), Mark, TEXT("View tilt"), Working.PitchDeg,
		Working.PitchDeg > 0.0 ? TEXT("(up)") : Working.PitchDeg < 0.0 ? TEXT("(down)") : TEXT("")));
	default:
		return Working.FovDeg <= 0.0
			? FText::FromString(FString::Printf(TEXT("%s %-16s  Auto (the lens: F6 or the atlas)"), Mark, TEXT("View angle")))
			: FText::FromString(FString::Printf(TEXT("%s %-16s %5.0f deg tall"), Mark, TEXT("View angle"), Working.FovDeg));
	}
}

FSlateColor SOsdsSeatMenu::RowColor(int32 Index) const
{
	using namespace OsdsUiStyle;
	return FSlateColor(Index == Selected ? Lime() : Paper());
}

void SOsdsSeatMenu::Show()
{
	Working.Clamp();
	if (AOsdsPlayerCameraManager* Cam = Camera.Get())
	{
		Cam->Seat = Working;
	}
}

void SOsdsSeatMenu::Nudge(int32 Steps)
{
	switch (Selected)
	{
	case 0: Working.UpCm += Steps * 1.0; break;
	case 1: Working.ForwardCm += Steps * 1.0; break;
	case 2: Working.PitchDeg += Steps * 0.5; break;
	default:
		// Auto steps to 45 deg (the natural lens) and on from there; below 30 goes back to Auto.
		if (Working.FovDeg <= 0.0)
		{
			Working.FovDeg = 45.0 + Steps * 1.0;
		}
		else
		{
			Working.FovDeg += Steps * 1.0;
			if (Working.FovDeg < FOsdsSeat::MinFovDeg) Working.FovDeg = 0.0;
		}
		break;
	}
	Show();
}

void SOsdsSeatMenu::Close(bool bSave)
{
	bSaved = bSave;
	if (bSave)
	{
		Working.Clamp();
		// Saved per car; with no car (a test) nothing is written.
		if (!CarId.IsEmpty()) Working.Save(CarId);
	}
	else
	{
		Working = Original;
	}
	Show();
	OnClose.ExecuteIfBound();
}

FReply SOsdsSeatMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::W) { Selected = (Selected + 3) % 4; return FReply::Handled(); }
	if (Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::S) { Selected = (Selected + 1) % 4; return FReply::Handled(); }
	if (Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::A) { Nudge(-1); return FReply::Handled(); }
	if (Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right || Key == EKeys::D) { Nudge(1); return FReply::Handled(); }
	if (Key == EKeys::Gamepad_FaceButton_Left || Key == EKeys::BackSpace)
	{
		const FOsdsSeat Default;
		switch (Selected)
		{
		case 0: Working.UpCm = Default.UpCm; break;
		case 1: Working.ForwardCm = Default.ForwardCm; break;
		case 2: Working.PitchDeg = Default.PitchDeg; break;
		default: Working.FovDeg = Default.FovDeg; break;
		}
		Show();
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Top || Key == EKeys::Delete) { Working = FOsdsSeat(); Show(); return FReply::Handled(); }
	if (Key == EKeys::Gamepad_FaceButton_Bottom || Key == EKeys::Enter) { Close(true); return FReply::Handled(); }
	if (Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_Special_Right || Key == EKeys::Escape) { Close(false); return FReply::Handled(); }
	return FReply::Unhandled();
}

FReply SOsdsSeatMenu::OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent)
{
	// The left stick is the D-pad here (the menus' rule, OsdsTheme::StickNav): one push one
	// step, held it repeats; all of the stick is the menu's.
	using namespace OsdsTheme;
	const FKey Key = InAnalogInputEvent.GetKey();
	if (!IsLeftStick(Key)) return FReply::Unhandled();
	switch (StickNav(Key, InAnalogInputEvent.GetAnalogValue(), Stick, FPlatformTime::Seconds()))
	{
	case ENav::Up: Selected = (Selected + 3) % 4; break;
	case ENav::Down: Selected = (Selected + 1) % 4; break;
	case ENav::Left: Nudge(-1); break;
	case ENav::Right: Nudge(1); break;
	default: break;
	}
	return FReply::Handled();
}

TArray<OsdsTheme::FHint> SOsdsSeatMenu::GetPadHints()
{
	return { { TEXT("D-pad Up Down"), TEXT("Choose"), TEXT("Up Down") }, { TEXT("D-pad Left Right"), TEXT("Change (the view moves as you go)"), TEXT("Left Right") },
		{ TEXT("X"), TEXT("Reset this row"), TEXT("Backspace") }, { TEXT("Y"), TEXT("Reset all"), TEXT("Delete") },
		{ TEXT("A"), TEXT("Save for this car"), TEXT("Enter") }, { TEXT("B / Menu"), TEXT("Cancel"), TEXT("Esc") } };
}

#undef LOCTEXT_NAMESPACE
