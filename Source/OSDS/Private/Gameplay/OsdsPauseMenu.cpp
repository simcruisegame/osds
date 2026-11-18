// Copyright OSDS contributors. All Rights Reserved.

#include "Gameplay/OsdsPauseMenu.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace { const FSlateBrush* OsdsLogoBrush(); }
#include "Widgets/Text/STextBlock.h"
#include "Gameplay/OsdsFeatures.h"

using namespace OsdsTheme;

namespace
{
	using FRowInfo = SOsdsPauseMenu::FRowInfo;
	using EKind = SOsdsPauseMenu::EKind;

	FRowInfo Row(EOsdsMenuRow R, EKind Kind, EIcon Icon, const TCHAR* Title, const TCHAR* Note, const TCHAR* Help)
	{
		return FRowInfo{ R, Kind, Icon, Title, Note, Help };
	}

	TSharedRef<SWidget> Card(float Width, const TSharedRef<SWidget>& Content)
	{
		return SNew(SBox).WidthOverride(Width)
			[
				SNew(SBorder).BorderImage(RoundedBrush(Glass(), 16.f)).Padding(FMargin(36.f, 30.f))
				[
					Content
				]
			];
	}

	/** Fit points (world metres) into a box, north up. */
	struct FFit
	{
		FVector2D Min = FVector2D(TNumericLimits<double>::Max()), Max = FVector2D(-TNumericLimits<double>::Max());
		void Add(const FVector2D& P) { Min = FVector2D(FMath::Min(Min.X, P.X), FMath::Min(Min.Y, P.Y)); Max = FVector2D(FMath::Max(Max.X, P.X), FMath::Max(Max.Y, P.Y)); }
		FVector2D Map(const FVector2D& P, const FVector2D& Size, double Pad) const
		{
			const FVector2D Span = (Max - Min).ComponentMax(FVector2D(50.0, 50.0));
			const double K = FMath::Min((Size.X - 2 * Pad) / Span.X, (Size.Y - 2 * Pad) / Span.Y);
			const FVector2D Mid = (Min + Max) * 0.5;
			return Size * 0.5 + (P - Mid) * K;
		}
	};
}

TSharedRef<SWidget> OsdsMenuBackdrop(TAttribute<float> LeftFraction, TAttribute<bool> bBlur)
{
	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBackgroundBlur)
			.BlurStrength_Lambda([bBlur]() { return bBlur.Get() ? 14.f : 0.f; })
			.Visibility_Lambda([bBlur]() { return bBlur.Get() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		]
		+ SOverlay::Slot()
		[
			SNew(SOsdsPainted).Size(FVector2D(1920.0, 1080.0)).Visibility(EVisibility::HitTestInvisible)
			.OnPaintFn_Lambda([LeftFraction](const FGeometry& G, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool)
			{
				// Dark on the left where the rows are, lighter to the right (left to right).
				const FVector2f Size = FVector2f(G.GetLocalSize());
				const float Left = FMath::Clamp(LeftFraction.Get(), 0.05f, 1.f);
				const float W = Size.X * Left;
				TArray<FSlateGradientStop> Stops;
				Stops.Add(FSlateGradientStop(FVector2D(0.0, 0.0), Glass(0.92f)));
				Stops.Add(FSlateGradientStop(FVector2D(W * 0.55, 0.0), Glass(Left < 0.9f ? 0.72f : 0.80f)));
				Stops.Add(FSlateGradientStop(FVector2D(W, 0.0), Glass(Left < 0.9f ? 0.0f : 0.55f)));
				FSlateDrawElement::MakeGradient(Out, Layer, G.ToPaintGeometry(FVector2f(W, Size.Y), FSlateLayoutTransform()), Stops, Orient_Vertical);
			})
		];
}

// ====================================================================== pause menu

const TArray<FRowInfo>& SOsdsPauseMenu::Rows(int32 InPage)
{
	static const TArray<FRowInfo> DriveWithTraffic = {
		Row(EOsdsMenuRow::Continue, EKind::Action, EIcon::Drive, TEXT("Continue driving"), TEXT("Back to the road"), TEXT("Close the menu and drive on.")),
		Row(EOsdsMenuRow::WhereTo, EKind::Action, EIcon::Search, TEXT("Where to?"), TEXT("Find a place, then go there or make it your stop"), TEXT("Type a place or an address. Go there now, or let the GPS take you there.")),
		Row(EOsdsMenuRow::PlanRoute, EKind::Action, EIcon::Route, TEXT("Plan a route"), TEXT("Put stops on the map"), TEXT("Move the cross over the map and add stops. The GPS finds the roads between them.")),
		Row(EOsdsMenuRow::FamousRoads, EKind::Action, EIcon::Star, TEXT("Famous roads"), TEXT("Great drives with the route ready"), TEXT("Go to the start of a famous road. The route to its end starts when you arrive.")),
		Row(EOsdsMenuRow::RaceTracks, EKind::Action, EIcon::Flag, TEXT("Race tracks"), TEXT("Ovals and road courses across the US"), TEXT("Start on the start/finish straight of a real race track, facing the way the races run.")),
		Row(EOsdsMenuRow::Traffic, EKind::Value, EIcon::Traffic, TEXT("Traffic"), TEXT("Other cars on the road"), TEXT("How many other cars drive near you.")),
		Row(EOsdsMenuRow::Atlas, EKind::Action, EIcon::Map, TEXT("Road atlas"), TEXT("Jump to any place on the map"), TEXT("The full map. Pin a place and drive there now.")),
		Row(EOsdsMenuRow::Quit, EKind::Action, EIcon::Back, TEXT("Quit the game"), TEXT("Back to the desktop"), TEXT("You are asked first. Your settings are already saved.")),
	};
	// Traffic off for this release (OsdsFeatures): the same page without its row.
	static const TArray<FRowInfo> Drive = DriveWithTraffic.FilterByPredicate([](const FRowInfo& R) { return R.Row != EOsdsMenuRow::Traffic; });
	static const TArray<FRowInfo> Car = {
		Row(EOsdsMenuRow::Car, EKind::Value, EIcon::Car, TEXT("Car"), TEXT("Change the car you drive"), TEXT("Change to the next car. You keep your place on the road.")),
		Row(EOsdsMenuRow::Transmission, EKind::Value, EIcon::Gearbox, TEXT("Gearbox"), TEXT("Automatic, manual, or manual with your clutch"), TEXT("Manual: shift with RB and LB (E and Q); the game works the clutch and blips the throttle on a downshift. Manual with your clutch: hold A (Shift) to shift.")),
		Row(EOsdsMenuRow::Stability, EKind::Value, EIcon::Car, TEXT("Stability control"), TEXT("ESC, on the cars that have it"), TEXT("Brakes one wheel and eases the engine when the car starts to slide, as the real car's system does. Off: you catch the slides yourself. A 2001 Miata and a 1965 Mustang never had it.")),
		Row(EOsdsMenuRow::Tune, EKind::Action, EIcon::Wrench, TEXT("Tune car"), TEXT("Springs, dampers, brakes and steering"), TEXT("Change the setup of this car in workshop units. Stop the car first.")),
		Row(EOsdsMenuRow::Lights, EKind::Value, EIcon::Lights, TEXT("Headlights"), TEXT("On, or on when it gets dark"), TEXT("Auto turns the headlights on at dusk.")),
	};
	static const TArray<FRowInfo> Seat = {
		Row(EOsdsMenuRow::Lens, EKind::Segmented, EIcon::Lens, TEXT("Lens"), TEXT(""), TEXT("Natural looks like a camera. True FOV shows the road at its real size for your screen. Wide shows more round you.")),
		Row(EOsdsMenuRow::HeadMotion, EKind::Slider, EIcon::Eye, TEXT("Head motion"), TEXT(""), TEXT("How much your head moves with the car in turns and bumps.")),
		Row(EOsdsMenuRow::ScreenSize, EKind::Slider, EIcon::Screen, TEXT("Screen size"), TEXT(""), TEXT("The diagonal of your screen. True FOV uses it.")),
		Row(EOsdsMenuRow::EyeDistance, EKind::Slider, EIcon::Eye, TEXT("Eye to screen"), TEXT(""), TEXT("How far your eyes are from the screen. True FOV uses it.")),
		Row(EOsdsMenuRow::Seat, EKind::Action, EIcon::Seat, TEXT("Seat position"), TEXT("Forward, back and height"), TEXT("Move the seat and your eyes in the car.")),
	};
	static const TArray<FRowInfo> Settings = {
		Row(EOsdsMenuRow::Sound, EKind::Value, EIcon::Sound, TEXT("Sound"), TEXT("The engine and the road"), TEXT("Turn the sound on or off.")),
		Row(EOsdsMenuRow::HudLevel, EKind::Value, EIcon::Hud, TEXT("HUD"), TEXT("Off, Glance or Full"), TEXT("Glance shows the speed and fades while you cruise. Full shows all the readouts.")),
		Row(EOsdsMenuRow::HudMap, EKind::Value, EIcon::Map, TEXT("Map"), TEXT("With route or always"), TEXT("The round map in the corner. With route shows it only while the GPS has a route.")),
		Row(EOsdsMenuRow::SpeedUnits, EKind::Segmented, EIcon::Hud, TEXT("Speed units"), TEXT(""), TEXT("The units of the speed on the HUD and of the GPS distances: mph and miles, or km/h and kilometres.")),
		Row(EOsdsMenuRow::Rumble, EKind::Slider, EIcon::Rumble, TEXT("Rumble"), TEXT(""), TEXT("How strong the pad shakes for the tyres and the road.")),
		Row(EOsdsMenuRow::StickSpeed, EKind::Slider, EIcon::Stick, TEXT("Stick steering speed"), TEXT(""), TEXT("How fast the wheels turn when you move the stick.")),
		Row(EOsdsMenuRow::StickResponse, EKind::Value, EIcon::Stick, TEXT("Stick response"), TEXT("Linear, between or fine centre"), TEXT("Fine centre gives small steering near the middle of the stick.")),
		Row(EOsdsMenuRow::StickDeadZone, EKind::Slider, EIcon::Stick, TEXT("Stick dead zone"), TEXT(""), TEXT("How far the stick moves before the wheels turn.")),
		Row(EOsdsMenuRow::Trigger, EKind::Value, EIcon::Trigger, TEXT("Throttle trigger"), TEXT("Linear to very progressive"), TEXT("Progressive gives fine throttle at the start of the trigger.")),
		Row(EOsdsMenuRow::Graphics, EKind::Segmented, EIcon::Screen, TEXT("Graphics quality"), TEXT("Auto, Low, Medium, High or Ultra"), TEXT("Auto chooses a quality preset for this graphics card. Lower settings reduce the GPU load.")),
	};
	switch (InPage)
	{
	case 1: return Car;
	case 2: return Seat;
	case 3: return Settings;
	default: return OsdsFeatures::IsTrafficEnabled() ? DriveWithTraffic : Drive;
	}
}

FString SOsdsPauseMenu::PageName(int32 InPage)
{
	static const TCHAR* Names[] = { TEXT("Drive"), TEXT("Your car"), TEXT("Seat & view"), TEXT("Settings") };
	return Names[FMath::Clamp(InPage, 0, NumPages - 1)];
}

FOsdsMenuValue SOsdsPauseMenu::ValueOf(EOsdsMenuRow InRow) const
{
	const FOsdsMenuValue* Found = State.Values.Find(InRow);
	return Found ? *Found : FOsdsMenuValue();
}

void SOsdsPauseMenu::Construct(const FArguments& InArgs)
{
	GetState = InArgs._GetState;
	OnAction = InArgs._OnAction;
	OnPage = InArgs._OnPage;
	OnClose = InArgs._OnClose;
	Page = FMath::Clamp(InArgs._Page, 0, NumPages - 1);
	Selected[Page] = FMath::Clamp(InArgs._Row, 0, Rows(Page).Num() - 1);
	TabCursor = Page;
	if (GetState.IsBound()) State = GetState.Execute();

	// The index is set, not bound: after LB or RB the new page is on screen at once, so the
	// pad's focus can move onto its first row in the same frame.
	TSharedRef<SWidgetSwitcher> Switcher = SNew(SWidgetSwitcher);
	for (int32 P = 0; P < NumPages; ++P) Switcher->AddSlot()[BuildPage(P)];
	Switcher->SetActiveWidgetIndex(Page);
	Pages = Switcher;

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			// The world keeps running behind Seat & view: no blur there, and only the left part dark.
			OsdsMenuBackdrop(TAttribute<float>::CreateLambda([this]() { return Page == 2 ? 0.45f : 1.f; }), TAttribute<bool>::CreateLambda([this]() { return Page != 2; }))
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(96.f, 56.f, 0.f, 0.f)
		[
			BuildTabs()
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0.f, 52.f, 96.f, 0.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
			[
				SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(State.Place); }).Font(Font(EWeight::Bold, 26.f)).ColorAndOpacity(White())
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
			[
				SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(State.Where); }).Font(Font(EWeight::Regular, 22.f)).ColorAndOpacity(Mist())
			]
		]
		+ SOverlay::Slot()
		[
			Switcher
		]
		// The OSDS logo (docs/brand, copied to data/brand so the package carries it loose), bottom right.
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 96.f, 44.f)
		[
			SNew(SImage).Image(OsdsLogoBrush()).ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.8f))
			.Visibility(OsdsLogoBrush() ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
		]
	];
	if (OnPage.IsBound()) OnPage.Execute(Page);
}

namespace
{
	/** The white horizontal OSDS lockup (1200 x 415), drawn 170 px wide; null when the file is missing. */
	const FSlateBrush* OsdsLogoBrush()
	{
		static TSharedPtr<FSlateDynamicImageBrush> Brush;
		static bool bTried = false;
		if (!bTried)
		{
			bTried = true;
			const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("data"), TEXT("brand"), TEXT("osds-lockup-horizontal-white.png")));
			if (IFileManager::Get().FileExists(*Path)) Brush = MakeShared<FSlateDynamicImageBrush>(FName(*Path), FVector2D(170.0, 170.0 * 415.0 / 1200.0));
		}
		return Brush.Get();
	}
}

TSharedRef<SWidget> SOsdsPauseMenu::BuildTabs()
{
	EnsurePadWatcher();
	const TAttribute<bool> bPad = TAttribute<bool>::CreateStatic(&IsPadInUse);
	TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
	auto Shoulder = [bPad](const FString& Pad, const FString& Key)
	{
		return SNew(SOverlay)
			+ SOverlay::Slot()[SNew(SBox).Visibility_Lambda([bPad]() { return bPad.Get() ? EVisibility::Visible : EVisibility::Collapsed; })[Glyph(Pad, 36.f)]]
			+ SOverlay::Slot()[SNew(SBox).Visibility_Lambda([bPad]() { return bPad.Get() ? EVisibility::Collapsed : EVisibility::Visible; })[Glyph(Key, 36.f)]];
	};
	Tabs->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 18, 0)[Shoulder(TEXT("LB"), TEXT("Q"))];
	for (int32 P = 0; P < NumPages; ++P)
	{
		TSharedRef<SOsdsTab> Tab = SNew(SOsdsTab)
			.Tag(FName(*FString::Printf(TEXT("Tab.%s"), *PageName(P))))
			.Label(FText::FromString(PageName(P)))
			.Current_Lambda([this, P]() { return Page == P; })
			.Focused_Lambda([this, P]() { return bTabs && TabCursor == P; })
			// The tab got the focus (the pad, or a real mouse moving onto it): the pad is on the tab row.
			.OnHover_Lambda([this, P]() { if (bSyncing) return; bTabs = true; TabCursor = P; SyncFocus(); })
			.OnClick_Lambda([this, P]() { SetPage(P); });
		TabWidgets[P] = Tab;
		Tabs->AddSlot().AutoWidth().VAlign(VAlign_Center)[Tab];
	}
	Tabs->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 0, 0)[Shoulder(TEXT("RB"), TEXT("E"))];
	return Tabs;
}

TSharedRef<SWidget> SOsdsPauseMenu::BuildPage(int32 P)
{
	const TArray<FRowInfo>& List = Rows(P);
	const bool bDrive = P == 0;
	const float RowWidth = bDrive ? 720.f : 1060.f;
	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
	for (int32 i = 0; i < List.Num(); ++i)
	{
		const FRowInfo Info = List[i];
		const TAttribute<bool> Focused = TAttribute<bool>::CreateLambda([this, P, i]() { return !bTabs && Page == P && Selected[P] == i; });
		// The row got the focus (the pad, Slate, or a real mouse moving onto it).
		const FSimpleDelegate Hover = FSimpleDelegate::CreateLambda([this, P, i]() { if (bSyncing || Page != P) return; bTabs = false; Selected[P] = i; SyncFocus(); });
		const FSimpleDelegate Click = FSimpleDelegate::CreateLambda([this, P, i]() { if (Page == P) { bTabs = false; Selected[P] = i; SyncFocus(); Choose(0); } });
		const EOsdsMenuRow R = Info.Row;
		TSharedPtr<SWidget> Widget;
		if (Info.Kind == EKind::Slider)
		{
			Widget = SNew(SOsdsSliderRow).Width(RowWidth).Focused(Focused).OnHover(Hover).OnClick(Click)
				.Name(FText::FromString(Info.Title))
				.Fraction_Lambda([this, R]() { return ValueOf(R).Fraction; })
				.Stock_Lambda([this, R]() { return ValueOf(R).Stock; })
				.Value_Lambda([this, R]() { return FText::FromString(ValueOf(R).Text); })
				.Unit_Lambda([this, R]() { return FText::FromString(ValueOf(R).Unit); });
		}
		else if (Info.Kind == EKind::Segmented)
		{
			TArray<FString> Options;
			if (R == EOsdsMenuRow::SpeedUnits) Options = { TEXT("mph"), TEXT("km/h") };
			else if (R == EOsdsMenuRow::Graphics) Options = { TEXT("Auto"), TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Ultra") };
			else Options = { TEXT("Natural"), TEXT("True FOV"), TEXT("Wide") };
			Widget = SNew(SOsdsSegmentedRow).Width(RowWidth).Focused(Focused).OnHover(Hover).OnClick(Click)
				.Name(FText::FromString(Info.Title))
				.Options(MoveTemp(Options))
				.Chosen_Lambda([this, R]() { return ValueOf(R).Chosen; });
		}
		else
		{
			const FString Note = Info.Note;
			Widget = SNew(SOsdsGuideRow).Width(RowWidth).Icon(Info.Icon).Focused(Focused).OnHover(Hover).OnClick(Click)
				.Title(FText::FromString(Info.Title))
				.Note_Lambda([this, R, Note]() { return FText::FromString(ValueOf(R).bUnavailable ? FString(TEXT("Comes with the next driving update")) : Note); })
				.Value_Lambda([this, R]()
				{
					const FOsdsMenuValue V = ValueOf(R);
					return FText::FromString(V.Unit.IsEmpty() ? V.Text : V.Text + TEXT(" ") + V.Unit);
				});
		}
		Widget->SetTag(FName(*FString::Printf(TEXT("Row.%s"), *Info.Title)));
		RowWidgets[P].Add(Widget);
		// Eight rows (Settings) sit closer so the list stays above the hints.
		Column->AddSlot().AutoHeight().Padding(0, 0, 0, List.Num() > 7 ? 4 : 14)[Widget.ToSharedRef()];
	}

	TSharedRef<SOverlay> Layout = SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(70.f, List.Num() > 7 ? 150.f : 190.f, 0.f, 0.f)
		[
			Column
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0.f, 190.f, 96.f, 0.f)
		[
			bDrive ? BuildRouteCard() : BuildHelpCard()
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(96.f, 0.f, 0.f, 52.f)
		[
			BuildHints()
		];
	if (P == 2)
	{
		// What the two main lenses do, at the bottom left.
		Layout->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(96.f, 0.f, 0.f, 128.f)
		[
			Card(720.f, SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Text(TEXT("Natural or True FOV?"), EWeight::ExtraBold, 26.f, White())]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
				[
					SNew(STextBlock).AutoWrapText(true).Font(Font(EWeight::Regular, 21.f)).ColorAndOpacity(Mist())
					.Text(FText::FromString(TEXT("Natural looks like a photo of the road. True FOV shows the road at its real size for your screen and how far you sit from it, so speed feels real. Set your screen size and eye distance first.")))
				])
		];
	}
	return Layout;
}

TSharedRef<SWidget> SOsdsPauseMenu::BuildRouteCard()
{
	return Card(760.f, SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Text(TEXT("Your route"), EWeight::SemiBold, 23.f, Mist())]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(14, 0, 0, 0)
			[
				SNew(SBorder).BorderImage(RoundedBrush(Amber(), 14.f)).Padding(FMargin(12.f, 2.f))
				.Visibility_Lambda([this]() { return State.bRoute ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					SNew(STextBlock).Font(Font(EWeight::Black, 19.f)).ColorAndOpacity(GuideDeep())
					.Text_Lambda([this]() { const int32 N = State.RouteStops.Num(); return FText::FromString(FString::Printf(TEXT("%d %s"), N, N == 1 ? TEXT("stop") : TEXT("stops"))); })
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
		[
			SNew(STextBlock).AutoWrapText(true).Font(Font(EWeight::ExtraBold, 44.f)).ColorAndOpacity(White())
			.Text_Lambda([this]() { return FText::FromString(State.bRoute ? State.RouteTitle : FString(TEXT("No route yet"))); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 18)
		[
			SNew(STextBlock).AutoWrapText(true).Font(Font(EWeight::Regular, 23.f)).ColorAndOpacity(White())
			.Text_Lambda([this]() { return FText::FromString(State.bRoute ? State.RouteSummary : FString(TEXT("Plan a route or choose a famous road. The magenta course then shows on the road, on the turn sign and on the map."))); })
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).HeightOverride(330.f).Visibility_Lambda([this]() { return State.bRoute ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SBorder).BorderImage(RoundedBrush(FLinearColor(FColor(0x1B, 0x24, 0x22)), 12.f)).Padding(0.f)
				[
					SNew(SOsdsPainted).Size(FVector2D(688.0, 330.0))
					.OnPaintFn_Lambda([this](const FGeometry& G, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool)
					{
						FFit Fit;
						for (const TArray<FVector2D>& Leg : State.RouteLegs) for (const FVector2D& P : Leg) Fit.Add(P);
						Fit.Add(State.CarM);
						const FVector2D Size = FVector2D(G.GetLocalSize());
						for (const TArray<FVector2D>& Leg : State.RouteLegs)
						{
							TArray<FVector2f> Pts;
							for (const FVector2D& P : Leg) Pts.Add(FVector2f(Fit.Map(P, Size, 30.0)));
							if (Pts.Num() >= 2) FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), MoveTemp(Pts), ESlateDrawEffect::None, Course(), true, 6.f);
						}
						const FVector2D Car = Fit.Map(State.CarM, Size, 30.0);
						FSlateDrawElement::MakeBox(Out, Layer + 1, G.ToPaintGeometry(FVector2f(18.f, 18.f), FSlateLayoutTransform(FVector2f(Car - FVector2D(9.0, 9.0)))), RoundedBrush(OsdsTheme::White(), 9.f), ESlateDrawEffect::None, White());
						for (int32 i = 0; i < State.RouteStops.Num(); ++i)
						{
							const FVector2D At = Fit.Map(State.RouteStops[i], Size, 30.0);
							FSlateDrawElement::MakeBox(Out, Layer + 1, G.ToPaintGeometry(FVector2f(28.f, 28.f), FSlateLayoutTransform(FVector2f(At - FVector2D(14.0, 14.0)))), RoundedBrush(OsdsTheme::White(), 14.f), ESlateDrawEffect::None, Amber());
							const FSlateFontInfo Number = Font(EWeight::Black, 17.f);
							FSlateDrawElement::MakeText(Out, Layer + 2, G.ToPaintGeometry(FVector2f(20.f, 22.f), FSlateLayoutTransform(FVector2f(At - FVector2D(i >= 9 ? 9.0 : 5.0, 11.0)))),
								FString::FromInt(i + 1), Number, ESlateDrawEffect::None, GuideDeep());
						}
					})
				]
			]
		]);
}

TSharedRef<SWidget> SOsdsPauseMenu::BuildHelpCard()
{
	auto Info = [this]() -> const FRowInfo& { return Rows(Page)[FMath::Clamp(Selected[Page], 0, Rows(Page).Num() - 1)]; };
	return Card(600.f, SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).AutoWrapText(true).Font(Font(EWeight::ExtraBold, 44.f)).ColorAndOpacity(White())
			.Text_Lambda([Info]() { return FText::FromString(Info().Title); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
		[
			SNew(STextBlock).AutoWrapText(true).Font(Font(EWeight::Regular, 23.f)).ColorAndOpacity(White())
			.Text_Lambda([Info]() { return FText::FromString(Info().Help); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 18, 0, 0)
		[
			SNew(STextBlock).Font(Font(EWeight::Bold, 26.f)).ColorAndOpacity(Amber())
			.Text_Lambda([this, Info]()
			{
				const FOsdsMenuValue V = ValueOf(Info().Row);
				if (V.bUnavailable) return FText::FromString(TEXT("Comes with the next driving update"));
				return FText::FromString(V.Unit.IsEmpty() ? V.Text : V.Text + TEXT(" ") + V.Unit);
			})
		]);
}

namespace
{
	/** The pause menu's hint sets: 0 an action row, 1 an action row on Drive with a route, 2 a value, slider or choice row, 3 the tab row. */
	const TArray<FHint>& PauseHintSet(int32 Index)
	{
		static const TArray<FHint> Sets[4] = {
			{ { TEXT("A"), TEXT("Choose"), TEXT("Enter") }, { TEXT("B"), TEXT("Back to the drive"), TEXT("Esc") }, { TEXT("LB RB"), TEXT("Change page"), TEXT("Q E") } },
			{ { TEXT("A"), TEXT("Choose"), TEXT("Enter") }, { TEXT("X"), TEXT("End the route"), TEXT("X") }, { TEXT("B"), TEXT("Back to the drive"), TEXT("Esc") }, { TEXT("LB RB"), TEXT("Change page"), TEXT("Q E") } },
			{ { TEXT("D-pad Left Right"), TEXT("Change"), TEXT("Left Right") }, { TEXT("A"), TEXT("Next value"), TEXT("Enter") }, { TEXT("B"), TEXT("Back to the drive"), TEXT("Esc") }, { TEXT("LB RB"), TEXT("Change page"), TEXT("Q E") } },
			{ { TEXT("D-pad Left Right"), TEXT("Choose a page"), TEXT("Left Right") }, { TEXT("A"), TEXT("Open"), TEXT("Enter") }, { TEXT("B"), TEXT("Back to the drive"), TEXT("Esc") }, { TEXT("LB RB"), TEXT("Change page"), TEXT("Q E") } },
		};
		return Sets[FMath::Clamp(Index, 0, 3)];
	}
	/** A slider row adds Y, back to stock. */
	const TArray<FHint>& PauseSliderHints()
	{
		static const TArray<FHint> Set = { { TEXT("D-pad Left Right"), TEXT("Change"), TEXT("Left Right") }, { TEXT("A"), TEXT("Next value"), TEXT("Enter") }, { TEXT("Y"), TEXT("Back to stock"), TEXT("Y") }, { TEXT("B"), TEXT("Back to the drive"), TEXT("Esc") }, { TEXT("LB RB"), TEXT("Change page"), TEXT("Q E") } };
		return Set;
	}
	const TArray<FHint>& PauseGraphicsHints()
	{
		static const TArray<FHint> Set = { { TEXT("D-pad Left Right"), TEXT("Change"), TEXT("Left Right") }, { TEXT("A"), TEXT("Next value"), TEXT("Enter") }, { TEXT("Y"), TEXT("Reset to Auto"), TEXT("Y") }, { TEXT("B"), TEXT("Back to the drive"), TEXT("Esc") }, { TEXT("LB RB"), TEXT("Change page"), TEXT("Q E") } };
		return Set;
	}
}

TArray<FHint> SOsdsPauseMenu::GetPadHints() const
{
	if (bTabs) return PauseHintSet(3);
	const FRowInfo& Info = Rows(Page)[FMath::Clamp(Selected[Page], 0, Rows(Page).Num() - 1)];
	if (Info.Row == EOsdsMenuRow::Graphics) return PauseGraphicsHints();
	if (Info.Kind == EKind::Slider) return PauseSliderHints();
	if (Info.Kind != EKind::Action) return PauseHintSet(2);
	return Page == 0 && State.bRoute ? PauseHintSet(1) : PauseHintSet(0);
}

TSharedRef<SWidget> SOsdsPauseMenu::BuildHints()
{
	const TAttribute<bool> bPad = TAttribute<bool>::CreateLambda([this]() { return State.bPad; });
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	// The hints of the row the pad is on (GetPadHints): sliders show Y, X ends a route on the Drive page.
	Box->AddSlot().AutoHeight()
	[
		SNew(SWidgetSwitcher).WidgetIndex_Lambda([this]()
		{
			if (bTabs) return 3;
			const FRowInfo& Info = Rows(Page)[FMath::Clamp(Selected[Page], 0, Rows(Page).Num() - 1)];
			const EKind Kind = Info.Kind;
			if (Kind == EKind::Slider) return 4;
			if (Info.Row == EOsdsMenuRow::Graphics) return 5;
			if (Kind != EKind::Action) return 2;
			return Page == 0 && State.bRoute ? 1 : 0;
		})
		+ SWidgetSwitcher::Slot()[Hints(PauseHintSet(0), bPad)]
		+ SWidgetSwitcher::Slot()[Hints(PauseHintSet(1), bPad)]
		+ SWidgetSwitcher::Slot()[Hints(PauseHintSet(2), bPad)]
		// On the tab row.
		+ SWidgetSwitcher::Slot()[Hints(PauseHintSet(3), bPad)]
		+ SWidgetSwitcher::Slot()[Hints(PauseSliderHints(), bPad)]
		+ SWidgetSwitcher::Slot()[Hints(PauseGraphicsHints(), bPad)]
	];
	return Box;
}

void SOsdsPauseMenu::SetPage(int32 InPage)
{
	const int32 Next = (InPage % NumPages + NumPages) % NumPages;
	const bool bChanged = Next != Page;
	Page = Next;
	// Every page opens on its first row, with the pad on it.
	Selected[Page] = 0;
	bTabs = false;
	TabCursor = Page;
	if (Pages.IsValid()) Pages->SetActiveWidgetIndex(Page);
	if (bChanged) OnPage.ExecuteIfBound(Page);
	SyncFocus();
}

TSharedPtr<SWidget> SOsdsPauseMenu::GetFocusTarget() const
{
	if (bTabs) return TabWidgets[FMath::Clamp(TabCursor, 0, NumPages - 1)];
	return RowWidgets[Page].IsValidIndex(Selected[Page]) ? RowWidgets[Page][Selected[Page]] : TSharedPtr<SWidget>();
}

void SOsdsPauseMenu::SyncFocus()
{
	if (bSyncing) return;
	TGuardValue<bool> Guard(bSyncing, true);
	bFocusDirty = !FocusPad(GetFocusTarget());
}

FReply SOsdsPauseMenu::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	// The menu itself was given the focus (it opened): hand it on to the row or tab the pad is on.
	bFocusDirty = true;
	const TSharedPtr<SWidget> Target = GetFocusTarget();
	return Target.IsValid() ? FReply::Handled().SetUserFocus(Target.ToSharedRef(), EFocusCause::SetDirectly, false) : FReply::Handled();
}

FReply SOsdsPauseMenu::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// A real mouse move shows the keyboard's keys in the hints.
	if (IsRealMouseMove(MouseEvent)) State.bPad = false;
	return FReply::Unhandled();
}

FNavigationReply SOsdsPauseMenu::OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent)
{
	// The menu moves the focus itself; Slate's own navigation never takes it off the menu.
	return FNavigationReply::Stop();
}

void SOsdsPauseMenu::Choose(int32 Dir)
{
	const TArray<FRowInfo>& List = Rows(Page);
	const FRowInfo& Info = List[FMath::Clamp(Selected[Page], 0, List.Num() - 1)];
	if (Dir != 0 && Info.Kind == EKind::Action) return;
	OnAction.ExecuteIfBound(Info.Row, Dir);
	if (GetState.IsBound()) State = GetState.Execute();
}

void SOsdsPauseMenu::Navigate(ENav Nav)
{
	const int32 N = Rows(Page).Num();
	if (bTabs)
	{
		// On the tab row: left and right move along it, A or Down opens the tab (on its first row).
		switch (Nav)
		{
		case ENav::Left: TabCursor = (TabCursor + NumPages - 1) % NumPages; SyncFocus(); break;
		case ENav::Right: TabCursor = (TabCursor + 1) % NumPages; SyncFocus(); break;
		case ENav::Accept: case ENav::Down: SetPage(TabCursor); break;
		case ENav::PrevPage: SetPage(Page - 1); break;
		case ENav::NextPage: SetPage(Page + 1); break;
		case ENav::Back: case ENav::Menu: OnClose.ExecuteIfBound(); break;
		default: break;
		}
		return;
	}
	switch (Nav)
	{
	case ENav::Up:
		// Up from the first row goes onto the tab row, on the open page's tab.
		if (Selected[Page] == 0) { bTabs = true; TabCursor = Page; }
		else --Selected[Page];
		SyncFocus();
		break;
	case ENav::Down: Selected[Page] = (Selected[Page] + 1) % N; SyncFocus(); break;
	case ENav::Left: Choose(-1); break;
	case ENav::Right: Choose(+1); break;
	case ENav::Accept: Choose(0); break;
	case ENav::PrevPage: SetPage(Page - 1); break;
	case ENav::NextPage: SetPage(Page + 1); break;
	case ENav::X:
		if (Page == 0 && State.bRoute) OnAction.ExecuteIfBound(EOsdsMenuRow::EndRoute, 0);
		break;
	case ENav::Y:
		// Back to stock on a slider, or reset graphics quality to Auto.
		if (Rows(Page)[Selected[Page]].Kind == EKind::Slider || Rows(Page)[Selected[Page]].Row == EOsdsMenuRow::Graphics) Choose(2);
		break;
	case ENav::Back:
	case ENav::Menu:
		OnClose.ExecuteIfBound();
		break;
	default: break;
	}
}

FReply SOsdsPauseMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const ENav Nav = NavOf(InKeyEvent.GetKey());
	if (Nav == ENav::None) return FReply::Unhandled();
	State.bPad = InKeyEvent.GetKey().IsGamepadKey();
	Navigate(Nav);
	return FReply::Handled();
}

FReply SOsdsPauseMenu::OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent)
{
	const FKey Key = InAnalogInputEvent.GetKey();
	if (!IsLeftStick(Key)) return FReply::Unhandled();
	const ENav Nav = StickNav(Key, InAnalogInputEvent.GetAnalogValue(), Stick, FPlatformTime::Seconds());
	if (Nav != ENav::None)
	{
		State.bPad = true;
		Navigate(Nav);
	}
	// All of the stick is the menu's: Slate's own stick navigation must not move the focus as well.
	return FReply::Handled();
}

void SOsdsPauseMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const bool bPad = State.bPad;
	if (GetState.IsBound()) State = GetState.Execute();
	// The last device used wins over the controller's guess.
	State.bPad = bPad;
	// The pad's focus is always on the menu: put it back if it went (the menu just opened,
	// a click outside a row, the window came back).
	if (bFocusDirty || !HasFocusedDescendants()) SyncFocus();
}

// ====================================================================== list menu

void SOsdsListMenu::Construct(const FArguments& InArgs)
{
	Items = InArgs._Items;
	OnChoose = InArgs._OnChoose;
	OnBack = InArgs._OnBack;
	bPad = InArgs._bPad;
	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		const FItem& Item = Items[i];
		TSharedRef<SOsdsGuideRow> Row = SNew(SOsdsGuideRow).Width(980.f).Icon(Item.Icon)
			.Tag(FName(*FString::Printf(TEXT("Item.%s"), *Item.Title)))
			.Fill(Item.bCourse ? Course() : Guide())
			.Title(FText::FromString(Item.Title)).Note(FText::FromString(Item.Note)).Value(FText::FromString(Item.Value))
			.Focused_Lambda([this, i]() { return Selected == i; })
			.OnHover_Lambda([this, i]() { if (bSyncing) return; Selected = i; SyncFocus(); })
			.OnClick_Lambda([this, i]() { Selected = i; OnChoose.ExecuteIfBound(i); });
		RowWidgets.Add(Row);
		Column->AddSlot().AutoHeight().Padding(0, 0, 0, 14)[Row];
	}
	const TAttribute<bool> Pad = TAttribute<bool>::CreateLambda([this]() { return bPad; });
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()[OsdsMenuBackdrop(1.f, true)]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(96.f, 56.f, 0.f, 0.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Text(InArgs._Title, EWeight::ExtraBold, 44.f, White())]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[Text(InArgs._Help, EWeight::Regular, 23.f, Mist())]
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Fill).Padding(70.f, 190.f, 0.f, 150.f)
		[
			SAssignNew(Scroll, SScrollBox).ScrollBarVisibility(EVisibility::Collapsed)
			+ SScrollBox::Slot()[Column]
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(96.f, 0.f, 0.f, 104.f)
		[
			Text(InArgs._Footer, EWeight::Regular, 18.f, Mist())
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(96.f, 0.f, 0.f, 52.f)
		[
			Hints(GetPadHints(), Pad)
		]
	];
}

TArray<FHint> SOsdsListMenu::GetPadHints()
{
	return { { TEXT("D-pad Up Down"), TEXT("Move"), TEXT("Up Down") }, { TEXT("A"), TEXT("Choose"), TEXT("Enter") }, { TEXT("B"), TEXT("Back"), TEXT("Esc") } };
}

void SOsdsListMenu::Navigate(ENav Nav)
{
	const int32 N = FMath::Max(Items.Num(), 1);
	switch (Nav)
	{
	case ENav::Up: Selected = (Selected + N - 1) % N; SyncFocus(); break;
	case ENav::Down: Selected = (Selected + 1) % N; SyncFocus(); break;
	case ENav::Accept: OnChoose.ExecuteIfBound(Selected); break;
	case ENav::Back: case ENav::Menu: OnBack.ExecuteIfBound(); break;
	default: break;
	}
}

FReply SOsdsListMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const ENav Nav = NavOf(InKeyEvent.GetKey());
	if (Nav == ENav::None) return FReply::Unhandled();
	bPad = InKeyEvent.GetKey().IsGamepadKey();
	Navigate(Nav);
	return FReply::Handled();
}

FReply SOsdsListMenu::OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent)
{
	const FKey Key = InAnalogInputEvent.GetKey();
	if (!IsLeftStick(Key)) return FReply::Unhandled();
	const ENav Nav = StickNav(Key, InAnalogInputEvent.GetAnalogValue(), Stick, FPlatformTime::Seconds());
	if (Nav == ENav::Up || Nav == ENav::Down)
	{
		bPad = true;
		Navigate(Nav);
	}
	return FReply::Handled();
}

void SOsdsListMenu::SyncFocus()
{
	if (bSyncing) return;
	TGuardValue<bool> Guard(bSyncing, true);
	const TSharedPtr<SWidget> Row = GetRowWidget(Selected);
	if (Scroll.IsValid() && Row.IsValid()) Scroll->ScrollDescendantIntoView(Row, false, EDescendantScrollDestination::IntoView, 14.f);
	bFocusDirty = !FocusPad(Row);
}

FReply SOsdsListMenu::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	bFocusDirty = true;
	const TSharedPtr<SWidget> Target = GetRowWidget(Selected);
	return Target.IsValid() ? FReply::Handled().SetUserFocus(Target.ToSharedRef(), EFocusCause::SetDirectly, false) : FReply::Handled();
}

FNavigationReply SOsdsListMenu::OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent)
{
	return FNavigationReply::Stop();
}

void SOsdsListMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (bFocusDirty || !HasFocusedDescendants()) SyncFocus();
}
