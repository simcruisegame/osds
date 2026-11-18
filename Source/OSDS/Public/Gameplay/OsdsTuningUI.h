// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/OsdsUiTheme.h"
#include "Vehicle/CarDefinition.h"
#include "Vehicle/OsdsTuning.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class AOsdsVehicle;
class SVerticalBox;

/** The engine's torque (lb-ft) and power (hp) over rpm, tuned over the real car's (dimmed). */
class OSDS_API SOsdsPowerCurve : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SOsdsPowerCurve) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs) {}
	/** Points are (rpm, value): torque in lb-ft and power in hp share one scale, as a dyno sheet does. */
	void SetCurves(TArray<FVector2D> InTorque, TArray<FVector2D> InPower, TArray<FVector2D> InStockTorque, TArray<FVector2D> InStockPower);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(460.0, 210.0); }

private:
	TArray<FVector2D> Torque, Power, StockTorque, StockPower;
};

/**
 * The tuning menu (D-pad Right or F4, standing still): the car's setup in workshop units on six
 * pages (engine, gearbox, springs and dampers, alignment, brakes and differential, tyres and aids),
 * each row inside a safe range around the real car's and only the rows the real car has. Beside
 * them, the effect before you drive: the torque and power curve, an estimated 0-60 and top speed,
 * and the body roll per g. LB and RB change page, up and down choose a row, left and right change
 * it, X resets the row, Y twice resets everything to the real car, A saves for this car and
 * applies, B cancels.
 */
class OSDS_API SOsdsTuningMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOsdsTuningMenu) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AOsdsVehicle>, Vehicle)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

	/** Apply the working setup to the car and save it; false when the save failed. */
	bool Commit();
	enum class ETuningResult : uint8 { Open, Saved, SaveFailed, Cancelled };
	ETuningResult GetResult() const { return Result; }

	/** Change the selected row by Steps (tests). */
	void Nudge(int32 Steps);
	/** Select a row on the current page (tests). */
	void Select(int32 Row) { Selected = FMath::Clamp(Row, 0, FMath::Max(NumRows() - 1, 0)); }
	/** Show a page (tests); the selection goes to its first row. */
	void ShowPage(int32 InPage);
	const FOsdsTuning& GetWorking() const { return Working; }
	int32 GetSelected() const { return Selected; }
	int32 GetPage() const { return Page; }
	int32 NumPages() const { return Pages.Num(); }
	int32 NumRows() const { return Pages.IsValidIndex(Page) ? Pages[Page].Rows.Num() : 0; }
	/** The selected row's file key-like name (tests): which setting a test is moving. */
	FString GetSelectedRowName() const;
	/** A real car's setup with no car in the world (tests): the stock values set each row's range. */
	void SetSetup(const FOsdsTuning& InStock, const FOsdsTuning& InWorking);
	void SetSetup(const FOsdsCarDefinition& InCar, const FOsdsTuning& InStock, const FOsdsTuning& InWorking);
	/** Y was pressed once: the next Y puts everything back as the real car. */
	bool IsConfirmingReset() const { return bConfirmReset; }
	/** The pad hints at the bottom (the help line is made from them). */
	static TArray<OsdsTheme::FHint> GetPadHints();
	/** The effect lines under the curve (tests read them). */
	FText EffectText() const;

private:
	struct FRow
	{
		FString Name;
		FText Label;
		double FOsdsTuning::* Field = nullptr;
		int32 Gear = -1;
		double Step = 1.0;
		FString Unit;
		int32 Decimals = 0;
		/** Named levels (ESC, traction control) instead of a number. */
		TArray<FString> Choices;
		bool bSigned = false;
	};
	struct FPage
	{
		FText Title;
		TArray<FRow> Rows;
	};
	static double& Value(FOsdsTuning& T, const FRow& Row);
	static double Value(const FOsdsTuning& T, const FRow& Row);
	void BuildPages();
	FText RowText(int32 Index) const;
	FSlateColor RowColor(int32 Index) const;
	FText PageText() const;
	void UpdateEffects() const;
	void Close(bool bSave);
	void Move(int32 Rows);
	void Turn(int32 Pages);

	TWeakObjectPtr<AOsdsVehicle> Vehicle;
	FSimpleDelegate OnClose;
	FOsdsCarDefinition RealCar;
	FOsdsTuning Working;
	FOsdsTuning Stock;
	TArray<FPage> Pages;
	int32 Page = 0;
	int32 Selected = 0;
	/** The left stick as the D-pad: one push one step, held it repeats (OsdsTheme::StickNav). */
	OsdsTheme::FStickRepeat Stick;
	bool bConfirmReset = false;
	FText Notice;
	ETuningResult Result = ETuningResult::Open;

	// The effect panel, worked out again only when the setup changes.
	TSharedPtr<SOsdsPowerCurve> Curve;
	mutable uint32 EffectsFor = 0;
	mutable FText Effects;
	mutable bool bStockEffects = false;
	mutable FOsdsTuning::FEstimate StockEstimate;
	mutable double StockRoll = 0.0;
};
