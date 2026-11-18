// Copyright OSDS contributors. All Rights Reserved.

#include "Gameplay/OsdsTuningUI.h"

#include "Gameplay/OsdsAtlasUI.h"
#include "Gameplay/OsdsUiTheme.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Misc/Crc.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Vehicle/OsdsVehicle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "OsdsTuning"

namespace
{
	/** Row slots on screen: the longest page (the gearbox: final drive and up to ten gears). */
	constexpr int32 RowSlots = FOsdsTuning::MaxGears + 1;
	constexpr double MpsToMph = 2.2369363;
}

// ------------------------------------------------------------------ the curve

void SOsdsPowerCurve::SetCurves(TArray<FVector2D> InTorque, TArray<FVector2D> InPower, TArray<FVector2D> InStockTorque, TArray<FVector2D> InStockPower)
{
	Torque = MoveTemp(InTorque);
	Power = MoveTemp(InPower);
	StockTorque = MoveTemp(InStockTorque);
	StockPower = MoveTemp(InStockPower);
}

int32 SOsdsPowerCurve::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace OsdsUiStyle;
	const FVector2f Size = FVector2f(AllottedGeometry.GetLocalSize());
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	FLinearColor Back = Ink();
	Back.A = 1.f;
	Back = FLinearColor::LerpUsingHSV(Back, Paper(), 0.06f);
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), White, ESlateDrawEffect::None, Back);

	double MaxRpm = 1.0, MaxValue = 1.0;
	for (const TArray<FVector2D>* Line : { &Torque, &Power, &StockTorque, &StockPower })
	{
		for (const FVector2D& P : *Line)
		{
			MaxRpm = FMath::Max(MaxRpm, P.X);
			MaxValue = FMath::Max(MaxValue, P.Y);
		}
	}
	// Round the scale up to 50 (lb-ft or hp) and 1000 rpm, with a grid line at each.
	MaxValue = FMath::CeilToDouble(MaxValue * 1.05 / 50.0) * 50.0;
	MaxRpm = FMath::CeilToDouble(MaxRpm / 1000.0) * 1000.0;
	const float Left = 34.f, Bottom = 22.f, Top = 8.f, Right = 8.f;
	const float W = Size.X - Left - Right, H = Size.Y - Top - Bottom;
	auto ToScreen = [&](const FVector2D& P) { return FVector2f(Left + W * float(P.X / MaxRpm), Top + H * (1.f - float(P.Y / MaxValue))); };
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 11);
	FLinearColor Grid = Paper();
	Grid.A = 0.15f;
	FLinearColor Label = Paper();
	Label.A = 0.7f;
	const int32 Step = MaxValue > 400.0 ? 100 : 50;
	for (int32 V = 0; V <= int32(MaxValue); V += Step)
	{
		const float Y = Top + H * (1.f - float(V / MaxValue));
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), TArray<FVector2f>{ { Left, Y }, { Left + W, Y } }, ESlateDrawEffect::None, Grid, true, 1.f);
		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(FVector2f(32.f, 14.f), FSlateLayoutTransform(FVector2f(0.f, Y - 8.f))),
			FString::FromInt(V), Font, ESlateDrawEffect::None, Label);
	}
	const int32 RpmStep = MaxRpm > 10000.0 ? 4000 : 2000;
	for (int32 Rpm = RpmStep; Rpm <= int32(MaxRpm); Rpm += RpmStep)
	{
		const float X = Left + W * float(Rpm / MaxRpm);
		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(FVector2f(60.f, 14.f), FSlateLayoutTransform(FVector2f(X - 14.f, Top + H + 3.f))),
			FString::Printf(TEXT("%dk"), Rpm / 1000), Font, ESlateDrawEffect::None, Label);
	}
	auto Draw = [&](const TArray<FVector2D>& Line, FLinearColor Color, float Thickness)
	{
		if (Line.Num() < 2) return;
		TArray<FVector2f> Points;
		for (const FVector2D& P : Line) Points.Add(ToScreen(P));
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, Thickness);
	};
	FLinearColor Dim = Paper();
	Dim.A = 0.35f;
	Draw(StockTorque, Dim, 1.5f);
	Draw(StockPower, Dim, 1.5f);
	// Torque amber, power lime: the two lines a dyno sheet shows.
	Draw(Torque, FLinearColor(1.f, 0.72f, 0.2f), 2.5f);
	Draw(Power, Lime(), 2.5f);
	return LayerId + 3;
}

// ------------------------------------------------------------------ the menu

void SOsdsTuningMenu::Construct(const FArguments& InArgs)
{
	Vehicle = InArgs._Vehicle;
	OnClose = InArgs._OnClose;
	if (const AOsdsVehicle* Car = Vehicle.Get())
	{
		RealCar = Car->GetRealCar();
		Stock = Car->GetStockTuning();
		Working = Car->GetTuning();
		// The car's setup is made before its model (and with it the steering ratio) is attached:
		// an untuned ratio is the real car's.
		if (Working.SteeringRatio <= 0.0)
		{
			Working.SteeringRatio = Stock.SteeringRatio;
		}
	}
	else
	{
		// No car (tests): the default car, a 1999 NB, so every row has its real range.
		Stock = FOsdsTuning::FromCar(RealCar, 15.0);
		Working = Stock;
	}
	BuildPages();

	using namespace OsdsUiStyle;
	// Readable across a room on a TV: rows at 20 pt (about 27 px), light text on a near-opaque
	// dark panel (well over 4.5:1), the selected row in lime.
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (int32 Index = 0; Index < RowSlots; ++Index)
	{
		List->AddSlot().AutoHeight().Padding(0.f, 3.f)
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Mono", 19))
			.Text(this, &SOsdsTuningMenu::RowText, Index)
			.ColorAndOpacity(this, &SOsdsTuningMenu::RowColor, Index)
		];
	}
	const FString CarName = RealCar.Name;
	FLinearColor Panel = Ink();
	Panel.A = 0.94f;
	ChildSlot
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(Panel)
		.Padding(28.f)
		[
			SNew(SBox)
			.MinDesiredWidth(1500.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 26))
					.ColorAndOpacity(Paper())
					.Text(FText::Format(LOCTEXT("Title", "Tuning · {0}"), FText::FromString(CarName)))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 19))
					.ColorAndOpacity(Lime())
					.Text(this, &SOsdsTuningMenu::PageText)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f)[ List ]
					+ SHorizontalBox::Slot().AutoWidth().Padding(24.f, 0.f, 0.f, 0.f)
					[
						SNew(SBox)
						.WidthOverride(480.f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock)
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
								.ColorAndOpacity(Paper())
								.Text(LOCTEXT("CurveKey", "Torque (lb-ft, amber) and power (hp, lime) over rpm; grey: the real car"))
								.AutoWrapText(true)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f)
							[
								SAssignNew(Curve, SOsdsPowerCurve)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
							[
								SNew(STextBlock)
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 18))
								.ColorAndOpacity(Lime())
								.AutoWrapText(true)
								.Text(this, &SOsdsTuningMenu::EffectText)
							]
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 19))
					.ColorAndOpacity(Paper())
					.Text_Lambda([this]() { return Notice; })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 19))
					.ColorAndOpacity(Paper())
					.AutoWrapText(true)
					// From the hint list (GetPadHints), so the help says what the keys do (OSDS.UI.PadHintsMatch).
					.Text_Lambda([]() { OsdsTheme::EnsurePadWatcher(); return FText::FromString(OsdsTheme::HintLine(GetPadHints(), OsdsTheme::IsPadInUse())); })
				]
			]
		]
	];
	UpdateEffects();
}

void SOsdsTuningMenu::SetSetup(const FOsdsTuning& InStock, const FOsdsTuning& InWorking)
{
	Stock = InStock;
	Working = InWorking;
	EffectsFor = 0;
	bStockEffects = false;
	BuildPages();
}

void SOsdsTuningMenu::SetSetup(const FOsdsCarDefinition& InCar, const FOsdsTuning& InStock, const FOsdsTuning& InWorking)
{
	RealCar = InCar;
	SetSetup(InStock, InWorking);
}

void SOsdsTuningMenu::BuildPages()
{
	Pages.Reset();
	const FOsdsTuning& S = Stock;
	auto Row = [](const TCHAR* Name, FText Label, double FOsdsTuning::* Field, double Step, const TCHAR* Unit, int32 Decimals)
	{
		FRow R;
		R.Name = Name;
		R.Label = MoveTemp(Label);
		R.Field = Field;
		R.Step = Step;
		R.Unit = Unit;
		R.Decimals = Decimals;
		return R;
	};

	FPage Engine{ LOCTEXT("PageEngine", "Engine") };
	Engine.Rows.Add(Row(TEXT("peak_torque"), S.bElectric ? LOCTEXT("MotorTorque", "Motor torque") : LOCTEXT("Torque", "Peak torque"), &FOsdsTuning::PeakTorqueLbFt, 5.0, TEXT("lb-ft"), 0));
	Engine.Rows.Add(Row(TEXT("peak_power"), S.bElectric ? LOCTEXT("MotorPower", "Motor power") : LOCTEXT("Power", "Peak power"), &FOsdsTuning::PeakPowerHp, 5.0, TEXT("hp"), 0));
	if (!S.bElectric)
	{
		Engine.Rows.Add(Row(TEXT("rev_limit"), LOCTEXT("RevLimit", "Rev limit"), &FOsdsTuning::RevLimitRpm, 100.0, TEXT("rpm"), 0));
	}
	Pages.Add(MoveTemp(Engine));

	FPage Gearbox{ LOCTEXT("PageGearbox", "Gearbox") };
	Gearbox.Rows.Add(Row(TEXT("final_drive"), LOCTEXT("FinalDrive", "Final drive"), &FOsdsTuning::FinalDrive, 0.05, TEXT(":1"), 2));
	if (S.NumGears >= 2)
	{
		for (int32 G = 0; G < S.NumGears; ++G)
		{
			FRow R = Row(*FString::Printf(TEXT("gear_%d"), G + 1), FText::Format(LOCTEXT("Gear", "Gear {0}"), FText::AsNumber(G + 1)), nullptr, 0.02, TEXT(":1"), 3);
			R.Gear = G;
			Gearbox.Rows.Add(MoveTemp(R));
		}
	}
	Pages.Add(MoveTemp(Gearbox));

	FPage Chassis{ LOCTEXT("PageChassis", "Springs and dampers") };
	Chassis.Rows.Add(Row(TEXT("front_springs"), LOCTEXT("FrontSpring", "Front springs"), &FOsdsTuning::FrontSpringLbIn, 10.0, TEXT("lb/in"), 0));
	Chassis.Rows.Add(Row(TEXT("rear_springs"), LOCTEXT("RearSpring", "Rear springs"), &FOsdsTuning::RearSpringLbIn, 10.0, TEXT("lb/in"), 0));
	Chassis.Rows.Add(Row(TEXT("front_bump"), LOCTEXT("FrontBump", "Front bump"), &FOsdsTuning::FrontBumpPct, 5.0, TEXT("% of stock"), 0));
	Chassis.Rows.Add(Row(TEXT("front_rebound"), LOCTEXT("FrontRebound", "Front rebound"), &FOsdsTuning::FrontReboundPct, 5.0, TEXT("% of stock"), 0));
	Chassis.Rows.Add(Row(TEXT("rear_bump"), LOCTEXT("RearBump", "Rear bump"), &FOsdsTuning::RearBumpPct, 5.0, TEXT("% of stock"), 0));
	Chassis.Rows.Add(Row(TEXT("rear_rebound"), LOCTEXT("RearRebound", "Rear rebound"), &FOsdsTuning::RearReboundPct, 5.0, TEXT("% of stock"), 0));
	Chassis.Rows.Add(Row(TEXT("front_bar"), LOCTEXT("FrontBar", "Front anti-roll bar"), &FOsdsTuning::FrontBarPct, 10.0, TEXT("% of stock"), 0));
	Chassis.Rows.Add(Row(TEXT("rear_bar"), LOCTEXT("RearBar", "Rear anti-roll bar"), &FOsdsTuning::RearBarPct, 10.0, TEXT("% of stock"), 0));
	{
		FRow F = Row(TEXT("front_ride_height"), LOCTEXT("FrontRide", "Front ride height"), &FOsdsTuning::FrontRideHeightMm, 5.0, TEXT("mm"), 0);
		F.bSigned = true;
		Chassis.Rows.Add(MoveTemp(F));
		FRow R = Row(TEXT("rear_ride_height"), LOCTEXT("RearRide", "Rear ride height"), &FOsdsTuning::RearRideHeightMm, 5.0, TEXT("mm"), 0);
		R.bSigned = true;
		Chassis.Rows.Add(MoveTemp(R));
	}
	Pages.Add(MoveTemp(Chassis));

	FPage Alignment{ LOCTEXT("PageAlignment", "Alignment") };
	if (S.bFrontCamberAdjustable) Alignment.Rows.Add(Row(TEXT("front_camber"), LOCTEXT("FrontCamber", "Front camber"), &FOsdsTuning::FrontCamberDeg, 0.1, TEXT("deg"), 1));
	if (S.bRearCamberAdjustable) Alignment.Rows.Add(Row(TEXT("rear_camber"), LOCTEXT("RearCamber", "Rear camber"), &FOsdsTuning::RearCamberDeg, 0.1, TEXT("deg"), 1));
	Alignment.Rows.Add(Row(TEXT("front_toe"), LOCTEXT("FrontToe", "Front toe (+ in)"), &FOsdsTuning::FrontToeDeg, 0.05, TEXT("deg"), 2));
	if (S.bRearToeAdjustable) Alignment.Rows.Add(Row(TEXT("rear_toe"), LOCTEXT("RearToe", "Rear toe (+ in)"), &FOsdsTuning::RearToeDeg, 0.05, TEXT("deg"), 2));
	if (S.bCasterAdjustable) Alignment.Rows.Add(Row(TEXT("caster"), LOCTEXT("Caster", "Caster"), &FOsdsTuning::CasterDeg, 0.25, TEXT("deg"), 2));
	Alignment.Rows.Add(Row(TEXT("steering_ratio"), LOCTEXT("Steering", "Steering ratio"), &FOsdsTuning::SteeringRatio, 0.5, TEXT(":1"), 1));
	Pages.Add(MoveTemp(Alignment));

	FPage Brakes{ LOCTEXT("PageBrakes", "Brakes and differential") };
	Brakes.Rows.Add(Row(TEXT("brake_bias"), LOCTEXT("BrakeBias", "Brake bias (front)"), &FOsdsTuning::BrakeBiasFrontPct, 1.0, TEXT("%"), 0));
	Brakes.Rows.Add(Row(TEXT("brake_force"), LOCTEXT("BrakeForce", "Brake force"), &FOsdsTuning::BrakeForcePct, 5.0, TEXT("% of stock"), 0));
	if (S.bHasLsd)
	{
		Brakes.Rows.Add(Row(TEXT("diff_power"), LOCTEXT("DiffPower", "Diff lock, power"), &FOsdsTuning::DiffPowerLock, 0.1, TEXT(":1 bias"), 1));
		Brakes.Rows.Add(Row(TEXT("diff_coast"), LOCTEXT("DiffCoast", "Diff lock, coast"), &FOsdsTuning::DiffCoastLock, 0.1, TEXT(":1 bias"), 1));
		if (FOsdsTuning::HasDiffPreload())
		{
			Brakes.Rows.Add(Row(TEXT("diff_preload"), LOCTEXT("DiffPreload", "Diff preload"), &FOsdsTuning::DiffPreloadNm, 10.0, TEXT("N m"), 0));
		}
	}
	Pages.Add(MoveTemp(Brakes));

	FPage Tyres{ LOCTEXT("PageTyres", "Tyres and aids") };
	Tyres.Rows.Add(Row(TEXT("front_tyre"), LOCTEXT("FrontTyre", "Front tyre pressure"), &FOsdsTuning::FrontTyrePsi, 1.0, TEXT("psi"), 0));
	Tyres.Rows.Add(Row(TEXT("rear_tyre"), LOCTEXT("RearTyre", "Rear tyre pressure"), &FOsdsTuning::RearTyrePsi, 1.0, TEXT("psi"), 0));
	if (S.bHasEsc)
	{
		FRow R = Row(TEXT("esc"), LOCTEXT("Esc", "Stability control"), &FOsdsTuning::EscLevel, 1.0, TEXT(""), 0);
		R.Choices = { TEXT("Off"), TEXT("Sport"), TEXT("On") };
		Tyres.Rows.Add(MoveTemp(R));
	}
	if (S.bHasTc)
	{
		FRow R = Row(TEXT("tc"), LOCTEXT("Tc", "Traction control"), &FOsdsTuning::TcLevel, 1.0, TEXT(""), 0);
		R.Choices = { TEXT("Off"), TEXT("Sport"), TEXT("On") };
		Tyres.Rows.Add(MoveTemp(R));
	}
	Pages.Add(MoveTemp(Tyres));

	Page = FMath::Clamp(Page, 0, Pages.Num() - 1);
	Selected = FMath::Clamp(Selected, 0, FMath::Max(NumRows() - 1, 0));
}

double& SOsdsTuningMenu::Value(FOsdsTuning& T, const FRow& Row)
{
	return Row.Gear >= 0 ? T.GearRatio[Row.Gear] : T.*(Row.Field);
}

double SOsdsTuningMenu::Value(const FOsdsTuning& T, const FRow& Row)
{
	return Row.Gear >= 0 ? T.GearRatio[Row.Gear] : T.*(Row.Field);
}

FString SOsdsTuningMenu::GetSelectedRowName() const
{
	return Pages.IsValidIndex(Page) && Pages[Page].Rows.IsValidIndex(Selected) ? Pages[Page].Rows[Selected].Name : FString();
}

FText SOsdsTuningMenu::PageText() const
{
	FString Line = TEXT("LB  ");
	for (int32 Index = 0; Index < Pages.Num(); ++Index)
	{
		const FString Title = Pages[Index].Title.ToString();
		Line += Index == Page ? FString::Printf(TEXT("[ %s ]"), *Title) : Title;
		Line += Index + 1 < Pages.Num() ? TEXT("  ·  ") : TEXT("  RB");
	}
	return FText::FromString(Line);
}

FText SOsdsTuningMenu::RowText(int32 Index) const
{
	if (!Pages.IsValidIndex(Page) || !Pages[Page].Rows.IsValidIndex(Index))
	{
		return FText::GetEmpty();
	}
	const FRow& Row = Pages[Page].Rows[Index];
	const double Now = Value(Working, Row);
	const double Real = Value(Stock, Row);
	if (Row.Field == &FOsdsTuning::SteeringRatio && Real <= 0.0)
	{
		return FText::Format(LOCTEXT("RowNone", "  {0}   no ratio for this car"), Row.Label);
	}
	auto Show = [&Row](double V)
	{
		if (Row.Choices.Num() > 0)
		{
			return Row.Choices[FMath::Clamp(FMath::RoundToInt(V), 0, Row.Choices.Num() - 1)];
		}
		return Row.bSigned ? FString::Printf(TEXT("%+.*f"), Row.Decimals, V) : FString::Printf(TEXT("%.*f"), Row.Decimals, V);
	};
	const bool bChanged = FMath::Abs(Now - Real) > 1.0e-6;
	return FText::FromString(FString::Printf(TEXT("%s %-22s %8s %-11s (real %s)%s"), Index == Selected ? TEXT(">") : TEXT(" "),
		*Row.Label.ToString(), *Show(Now), *Row.Unit, *Show(Real), bChanged ? TEXT(" *") : TEXT("")));
}

FSlateColor SOsdsTuningMenu::RowColor(int32 Index) const
{
	using namespace OsdsUiStyle;
	return FSlateColor(Index == Selected ? Lime() : Paper());
}

void SOsdsTuningMenu::UpdateEffects() const
{
	FOsdsTuning Clamped = Working;
	Clamped.Clamp(Stock);
	const uint32 Hash = FCrc::MemCrc32(&Clamped, sizeof(FOsdsTuning)) | 1u;
	if (Hash == EffectsFor)
	{
		return;
	}
	EffectsFor = Hash;
	const FOsdsCarDefinition Tuned = Clamped.Apply(RealCar, Stock);
	if (!bStockEffects)
	{
		StockEstimate = FOsdsTuning::EstimatePerformance(RealCar);
		StockRoll = FOsdsTuning::RollGradientDegPerG(RealCar);
		bStockEffects = true;
	}
	const FOsdsTuning::FEstimate Est = FOsdsTuning::EstimatePerformance(Tuned);
	const FOsdsTuning::FPeaks Peaks = FOsdsTuning::EnginePeaks(Tuned);
	// The estimate's change on the real car's published 0-60 (the car file's target), so a stock
	// car reads its magazine time; with no target, the estimate itself.
	const double Published = RealCar.TargetZeroToSixtySeconds;
	const double ZeroToSixty = Published > 0.0 && StockEstimate.ZeroToSixtyS > 0.0
		? Est.ZeroToSixtyS * Published / StockEstimate.ZeroToSixtyS : Est.ZeroToSixtyS;
	const double RealZeroToSixty = Published > 0.0 ? Published : StockEstimate.ZeroToSixtyS;
	using namespace OsdsTuningUnits;
	Effects = FText::FromString(FString::Printf(
		TEXT("%.0f lb-ft at %.0f rpm, %.0f hp at %.0f rpm\n0-60 mph about %.1f s (real %.1f)\nTop speed about %.0f mph (real car's %.0f)\nBody roll %.2f deg per g (real %.2f)"),
		Peaks.TorqueNm / NmPerLbFt, Peaks.TorqueRpm, Peaks.PowerW / WattsPerHp, Peaks.PowerRpm,
		ZeroToSixty, RealZeroToSixty, Est.TopSpeedMps * MpsToMph, StockEstimate.TopSpeedMps * MpsToMph,
		FOsdsTuning::RollGradientDegPerG(Tuned), StockRoll));

	if (Curve.IsValid())
	{
		auto Sample = [](const FOsdsCarDefinition& Car, TArray<FVector2D>& OutTorque, TArray<FVector2D>& OutPower)
		{
			const bool bElectric = Car.EngineKind == TEXT("electric");
			const double Top = bElectric ? (Car.ElectricMotorMaximumSpeedRpm > 0.0 ? Car.ElectricMotorMaximumSpeedRpm : 16000.0) : Car.FuelCutSpeedRpm;
			const double From = bElectric ? 200.0 : Car.IdleSpeedRpm;
			for (int32 I = 0; I <= 48; ++I)
			{
				const double Rpm = FMath::Lerp(From, Top, I / 48.0);
				const double T = FOsdsTuning::TorqueAtRpm(Car, Rpm);
				OutTorque.Add(FVector2D(Rpm, T / NmPerLbFt));
				OutPower.Add(FVector2D(Rpm, T * Rpm / RpmPerRadPerS / WattsPerHp));
			}
		};
		TArray<FVector2D> T, P, ST, SP;
		Sample(Tuned, T, P);
		Sample(RealCar, ST, SP);
		Curve->SetCurves(MoveTemp(T), MoveTemp(P), MoveTemp(ST), MoveTemp(SP));
	}
}

FText SOsdsTuningMenu::EffectText() const
{
	UpdateEffects();
	return Effects;
}

void SOsdsTuningMenu::Nudge(int32 Steps)
{
	if (!Pages.IsValidIndex(Page) || !Pages[Page].Rows.IsValidIndex(Selected))
	{
		return;
	}
	const FRow& Row = Pages[Page].Rows[Selected];
	const double Before = Value(Working, Row);
	Value(Working, Row) += Steps * Row.Step;
	// Torque moves the whole curve, so the power goes with it; the power row then shapes the top end.
	if (Row.Field == &FOsdsTuning::PeakTorqueLbFt && Before > 0.0)
	{
		FOsdsTuning Probe = Working;
		Probe.Clamp(Stock);
		Working.PeakPowerHp *= Probe.PeakTorqueLbFt / Before;
	}
	Working.Clamp(Stock);
}

void SOsdsTuningMenu::Move(int32 Rows)
{
	const int32 N = NumRows();
	if (N > 0)
	{
		Selected = ((Selected + Rows) % N + N) % N;
	}
}

void SOsdsTuningMenu::Turn(int32 InPages)
{
	const int32 N = Pages.Num();
	if (N > 0)
	{
		ShowPage(((Page + InPages) % N + N) % N);
	}
}

void SOsdsTuningMenu::ShowPage(int32 InPage)
{
	Page = FMath::Clamp(InPage, 0, FMath::Max(Pages.Num() - 1, 0));
	Selected = 0;
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
	if (Key == EKeys::Gamepad_LeftShoulder || Key == EKeys::Q || Key == EKeys::PageUp)
	{
		Turn(-1);
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_RightShoulder || Key == EKeys::E || Key == EKeys::PageDown)
	{
		Turn(1);
		return FReply::Handled();
	}
	if (Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::W)
	{
		Move(-1);
		return FReply::Handled();
	}
	if (Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::S)
	{
		Move(1);
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
		if (Pages.IsValidIndex(Page) && Pages[Page].Rows.IsValidIndex(Selected))
		{
			const FRow& Row = Pages[Page].Rows[Selected];
			Value(Working, Row) = Value(Stock, Row);
			Working.Clamp(Stock);
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
	case ENav::Up: Move(-1); break;
	case ENav::Down: Move(1); break;
	case ENav::Left: Nudge(-1); break;
	case ENav::Right: Nudge(1); break;
	default: break;
	}
	return FReply::Handled();
}

TArray<OsdsTheme::FHint> SOsdsTuningMenu::GetPadHints()
{
	return { { TEXT("LB RB"), TEXT("Page"), TEXT("Q E") }, { TEXT("D-pad Up Down"), TEXT("Choose"), TEXT("Up Down") },
		{ TEXT("D-pad Left Right"), TEXT("Change"), TEXT("Left Right") },
		{ TEXT("X"), TEXT("Reset this row"), TEXT("Backspace") }, { TEXT("Y"), TEXT("Reset all (press twice)"), TEXT("Delete") },
		{ TEXT("A"), TEXT("Save for this car"), TEXT("Enter") }, { TEXT("B / Menu"), TEXT("Cancel"), TEXT("Esc") } };
}

#undef LOCTEXT_NAMESPACE
