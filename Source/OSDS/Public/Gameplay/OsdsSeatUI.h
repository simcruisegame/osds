// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/OsdsPlayerCameraManager.h"
#include "Gameplay/OsdsUiTheme.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

/**
 * The seat & view menu: a panel at the side of the live driver's view, so each change shows at
 * once. Seat height and fore-aft move the eye from the car's measured driver eye, the tilt looks
 * down or up, and the view angle sets the vertical field of view (Auto = the lens). Up/Down
 * choose, Left/Right change, X resets the row, Y resets all, A saves for this car, B puts the
 * seat back as it was.
 */
class OSDS_API SOsdsSeatMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOsdsSeatMenu) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AOsdsPlayerCameraManager>, Camera)
		SLATE_ARGUMENT(FString, CarId)
		SLATE_ARGUMENT(FString, CarName)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

	/** Change the selected row by Steps; the camera shows it at once (tests call it). */
	void Nudge(int32 Steps);
	void Select(int32 Row) { Selected = FMath::Clamp(Row, 0, 3); }
	/** Save (true) or put the old seat back (false), then close. */
	void Close(bool bSave);
	bool WasSaved() const { return bSaved; }
	const FOsdsSeat& GetWorking() const { return Working; }
	int32 GetSelected() const { return Selected; }
	/** The pad hints at the bottom (the help line is made from them). */
	static TArray<OsdsTheme::FHint> GetPadHints();

private:
	FText RowText(int32 Index) const;
	FSlateColor RowColor(int32 Index) const;
	void Show();

	TWeakObjectPtr<AOsdsPlayerCameraManager> Camera;
	FString CarId;
	FSimpleDelegate OnClose;
	FOsdsSeat Original;
	FOsdsSeat Working;
	int32 Selected = 0;
	/** The left stick as the D-pad: one push one step, held it repeats (OsdsTheme::StickNav). */
	OsdsTheme::FStickRepeat Stick;
	bool bSaved = false;
};
