// Copyright OSDS contributors. All Rights Reserved.

#include "Gameplay/OsdsTuningUI.h"

#include "Gameplay/OsdsAtlasUI.h"
#include "Gameplay/OsdsUiTheme.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Vehicle/OsdsVehicle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "OsdsTuning"

void SOsdsTuningMenu::Construct(const FArguments& InArgs)
{
	Vehicle = InArgs._Vehicle;
	OnClose = InArgs._OnClose;
	if (const AOsdsVehicle* Car = Vehicle.Get())
	{
		Stock = Car->GetStockTuning();
		Working = Car->GetTuning();
		// The car's setup is made before its model (and with it the steering ratio) is attached:
		// an untuned ratio is the real car's.
		if (Working.SteeringRatio <= 0.0)
		{
			Working.SteeringRatio = Stock.SteeringRatio;
		}
	}
	Rows = {
		{ LOCTEXT("FrontSpring", "Front springs"), &FOsdsTuning::FrontSpringLbIn, 10.0, TEXT("lb/in"), 0 },
		{ LOCTEXT("RearSpring", "Rear springs"), &FOsdsTuning::RearSpringLbIn, 10.0, TEXT("lb/in"), 0 },
		{ LOCTEXT("Damping", "Damping"), &FOsdsTuning::DampingPct, 5.0, TEXT("% of stock"), 0 },
		{ LOCTEXT("RideHeight", "Ride height"), &FOsdsTuning::RideHeightMm, 5.0, TEXT("mm"), 0 },
		{ LOCTEXT("FrontBar", "Front anti-roll bar"), &FOsdsTuning::FrontBarPct, 10.0, TEXT("% of stock"), 0 },
		{ LOCTEXT("RearBar", "Rear anti-roll bar"), &FOsdsTuning::RearBarPct, 10.0, TEXT("% of stock"), 0 },
		{ LOCTEXT("Torque", "Engine torque"), &FOsdsTuning::TorquePct, 5.0, TEXT("% of stock"), 0 },
		{ LOCTEXT("FinalDrive", "Final drive"), &FOsdsTuning::FinalDrive, 0.05, TEXT(":1"), 2 },
		{ LOCTEXT("Steering", "Steering ratio"), &FOsdsTuning::SteeringRatio, 0.5, TEXT(":1"), 1 },
		{ LOCTEXT("FrontTyre", "Front tyre pressure"), &FOsdsTuning::FrontTyrePsi, 1.0, TEXT("psi"), 0 },
		{ LOCTEXT("RearTyre", "Rear tyre pressure"), &FOsdsTuning::RearTyrePsi, 1.0, TEXT("psi"), 0 },
	};

	using namespace OsdsUiStyle;
	// Readable across a room on a TV: rows at 20 pt (about 27 px), light text on a near-opaque
	// dark panel (well over 4.5:1), the selected row in lime.
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		List->AddSlot().AutoHeight().Padding(0.f, 4.f)
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Mono", 20))
			.Text(this, &SOsdsTuningMenu::RowText, Index)
			.ColorAndOpacity(this, &SOsdsTuningMenu::RowColor, Index)
		];
	}
	const FString CarName = Vehicle.IsValid() ? Vehicle->GetRealCar().Name : FString();
	FLinearColor Panel = Ink();
	Panel.A = 0.94f;
	ChildSlot
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(Panel)
		.Padding(32.f)
		[
			SNew(SBox)
			.MinDesiredWidth(900.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 26))
					.ColorAndOpacity(Paper())
					.Text(FText::Format(LOCTEXT("Title", "Tuning · {0}"), FText::FromString(CarName)))
				]
				+ SVerticalBox::Slot().AutoHeight()[ List ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 16.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 20))
					.ColorAndOpacity(Lime())
					.Text(this, &SOsdsTuningMenu::RollText)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 20))
					.ColorAndOpacity(Paper())
					.Text_Lambda([this]() { return Notice; })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 16.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 20))
					.ColorAndOpacity(Paper())
					.AutoWrapText(true)
					// From the hint list (GetPadHints), so the help says what the keys do (OSDS.UI.PadHintsMatch).
					.Text_Lambda([]() { OsdsTheme::EnsurePadWatcher(); return FText::FromString(OsdsTheme::HintLine(GetPadHints(), OsdsTheme::IsPadInUse())); })
				]
			]
		]
	];
}

FText SOsdsTuningMenu::RowText(int32 Index) const
{
	const FRow& Row = Rows[Index];
	const double Value = Working.*(Row.Field);
	const double Real = Stock.*(Row.Field);
	if (Row.Field == &FOsdsTuning::SteeringRatio && Real <= 0.0)
	{
		return FText::Format(LOCTEXT("RowNone", "{0}   no ratio for this car"), Row.Label);
	}
	const FString Shown = Row.Field == &FOsdsTuning::RideHeightMm
		? FString::Printf(TEXT("%+.0f"), Value)
		: FString::Printf(TEXT("%.*f"), Row.Decimals, Value);
	const FString RealShown = Row.Field == &FOsdsTuning::RideHeightMm
		? TEXT("0")
		: FString::Printf(TEXT("%.*f"), Row.Decimals, Real);
	return FText::FromString(FString::Printf(TEXT("%s %-22s %8s %s   (real %s)"), Index == Selected ? TEXT(">") : TEXT(" "),
		*Row.Label.ToString(), *Shown, *Row.Unit, *RealShown));
}

FSlateColor SOsdsTuningMenu::RowColor(int32 Index) const
{
	using namespace OsdsUiStyle;
	return FSlateColor(Index == Selected ? Lime() : Paper());
}

FText SOsdsTuningMenu::RollText() const
{
	if (!Vehicle.IsValid())
	{
		return FText::GetEmpty();
	}
	const FOsdsCarDefinition& Real = Vehicle->GetRealCar();
	FOsdsTuning Clamped = Working;
	Clamped.Clamp(Stock);
	const double Roll = FOsdsTuning::RollGradientDegPerG(Clamped.Apply(Real, Stock));
	const double RealRoll = FOsdsTuning::RollGradientDegPerG(Real);
	return FText::FromString(FString::Printf(TEXT("Body roll %.2f deg per g of cornering (real car %.2f)"), Roll, RealRoll));
}

void SOsdsTuningMenu::Nudge(int32 Steps)
{
	if (!Rows.IsValidIndex(Selected))
	{
		return;
	}
	const FRow& Row = Rows[Selected];
	Working.*(Row.Field) += Steps * Row.Step;
	Working.Clamp(Stock);
}

bool SOsdsTuningMenu::Commit()
{
	AOsdsVehicle* Car = Vehicle.Get();
	return Car && Car->ApplyTuning(Working, true);
}

void SOsdsTuningMenu::Close(bool bSave)
{
	// With no car left to tune (it was removed while the menu was open) nothing was applied.
	Result = !bSave || !Vehicle.IsValid() ? ETuningResult::Cancelled
		: Commit() ? ETuningResult::Saved : ETuningResult::SaveFailed;
	OnClose.ExecuteIfBound();
}

FReply SOsdsTuningMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	const bool bResetKey = Key == EKeys::Gamepad_FaceButton_Top || Key == EKeys::Delete;
	if (!bResetKey && bConfirmReset)
	{
		bConfirmReset = false;
		Notice = FText::GetEmpty();
	}
	if (Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::W)
	{
		Selected = (Selected + Rows.Num() - 1) % Rows.Num();
		return FReply::Handled();
	}
	if (Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::S)
	{
		Selected = (Selected + 1) % Rows.Num();
		return FReply::Handled();
	}
	if (Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::A)
	{
		Nudge(-1);
		return FReply::Handled();
	}
	if (Key == EKeys::Right || Key == EKeys::D || Key == EKeys::Gamepad_DPad_Right)
	{
		Nudge(1);
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Left || Key == EKeys::BackSpace)
	{
		if (Rows.IsValidIndex(Selected))
		{
			Working.*(Rows[Selected].Field) = Stock.*(Rows[Selected].Field);
		}
		return FReply::Handled();
	}
	if (bResetKey)
	{
		// Everything back to the real car takes a second press, so one slip of a thumb loses nothing.
		if (!bConfirmReset)
		{
			bConfirmReset = true;
			Notice = LOCTEXT("ConfirmReset", "Press Y again to put everything back as the real car");
		}
		else
		{
			bConfirmReset = false;
			Working = Stock;
			Notice = LOCTEXT("DidReset", "Back to the real car (A saves it, B keeps your old setup)");
		}
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Bottom || Key == EKeys::Enter)
	{
		Close(true);
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_Special_Right || Key == EKeys::Escape || Key == EKeys::F4)
	{
		Close(false);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}
FReply SOsdsTuningMenu::OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent)
{
	// The left stick is the D-pad here (the menus' rule, OsdsTheme::StickNav): one push one
	// step, held it repeats. All of the stick is the menu's, so Slate's own stick navigation never
	// moves the focus off it.
	using namespace OsdsTheme;
	const FKey Key = InAnalogInputEvent.GetKey();
	if (!IsLeftStick(Key)) return FReply::Unhandled();
	switch (StickNav(Key, InAnalogInputEvent.GetAnalogValue(), Stick, FPlatformTime::Seconds()))
	{
	case ENav::Up: Selected = (Selected + Rows.Num() - 1) % Rows.Num(); break;
	case ENav::Down: Selected = (Selected + 1) % Rows.Num(); break;
	case ENav::Left: Nudge(-1); break;
	case ENav::Right: Nudge(1); break;
	default: break;
	}
	return FReply::Handled();
}

TArray<OsdsTheme::FHint> SOsdsTuningMenu::GetPadHints()
{
	return { { TEXT("D-pad Up Down"), TEXT("Choose"), TEXT("Up Down") }, { TEXT("D-pad Left Right"), TEXT("Change"), TEXT("Left Right") },
		{ TEXT("X"), TEXT("Reset this row"), TEXT("Backspace") }, { TEXT("Y"), TEXT("Reset all (press twice)"), TEXT("Delete") },
		{ TEXT("A"), TEXT("Save for this car"), TEXT("Enter") }, { TEXT("B / Menu"), TEXT("Cancel"), TEXT("Esc") } };
}

#undef LOCTEXT_NAMESPACE
