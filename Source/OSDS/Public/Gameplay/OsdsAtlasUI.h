// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/OsdsPlaces.h"
#include "Gameplay/OsdsUiTheme.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "World/OsdsGeo.h"

class AOsdsWorldStreamer;
class SEditableTextBox;
class SVerticalBox;
class STextBlock;
class UTexture2D;
struct FSlateBrush;

/**
 * The place search and the road atlas, the two screens that take the driver
 * anywhere (godot-final:game/Hud.gd "Where to?" and godot-final:game/RoadMap.gd).
 * Both are Slate widgets the player controller adds to the viewport while the
 * game is paused; the place itself is chosen through callbacks the controller
 * forwards to AOsdsGameMode::TravelTo.
 */

/** What the atlas shows of the drive, read each frame. */
struct FOsdsAtlasView
{
	FOsdsWorldFrame Frame;
	FVector CarCm = FVector::ZeroVector;
	double CarYawDeg = 0.0;
	bool bHasCar = false;
	FString Title;
	FString CarName;
	FString Transmission;
	/** The cockpit settings shown on the atlas (Hud.gd): sound, HUD level, head motion, rumble. */
	TArray<FString> Settings;
	TWeakObjectPtr<AOsdsWorldStreamer> Streamer;
};

DECLARE_DELEGATE_OneParam(FOsdsOnPlace, const FOsdsPlace&);
DECLARE_DELEGATE_RetVal(bool, FOsdsOnSurprise);
DECLARE_DELEGATE_RetVal(FOsdsAtlasView, FOsdsGetAtlasView);
DECLARE_DELEGATE_OneParam(FOsdsOnSetting, int32);

/** Shared look (Hud.gd palette: INK #15221f, GLASS #15221f9e, WHITE #f4efdb, LIME #c6ef96). */
namespace OsdsUiStyle
{
	OSDS_API FLinearColor Ink();
	OSDS_API FLinearColor Glass();
	OSDS_API FLinearColor Paper();
	OSDS_API FLinearColor Lime();
	OSDS_API FLinearColor Dim();
}

/** "Where to?": a search box, the presets, Surprise me, and the helper's answers. */
class OSDS_API SOsdsPlaceSearch : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOsdsPlaceSearch) {}
		SLATE_EVENT(FOsdsOnPlace, OnChoose)
		SLATE_EVENT(FOsdsOnSurprise, OnSurprise)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SOsdsPlaceSearch() override;

	/** Focus for the device that opened it: the text box for a keyboard, the first place for a pad. */
	void FocusFirst(bool bGamepad);
	TSharedPtr<SWidget> FocusTarget(bool bGamepad) const;
	/** Run a query as if typed (also used by checks). */
	void Submit(const FString& Query);

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** The pad keyboard under the search box (A on the box opens it). */
	void OpenPadKeyboard();
	void ClosePadKeyboard();
	bool IsPadKeyboardOpen() const { return bKeys; }
	/** Type into the search box as the pad keyboard does. */
	void TypeKey(const FString& Key);
	FString GetQuery() const;
	/** The pad keyboard's keys, row by row, and the screen's other controls (tests). */
	const TArray<TArray<TSharedPtr<SWidget>>>& GetKeyRows() const { return KeyRows; }
	TSharedPtr<SWidget> GetField() const;
	const TArray<TSharedPtr<SWidget>>& GetChoiceButtons() const { return ChoiceButtons; }
	const TArray<TSharedPtr<SWidget>>& GetToolButtons() const { return ToolButtons; }
	/** The pad hints now: the screen's, or the pad keyboard's while it is open. */
	TArray<OsdsTheme::FHint> GetPadHints() const;
	/** Where the pad keyboard types (characters from the start); LB and RB move it. */
	int32 GetTextCursor() const { return Cursor; }
	void MoveCursor(int32 Dir);

private:
	void MoveKey(int32 DX, int32 DY);
	void MoveFocus(int32 DX, int32 DY);
	TArray<TArray<TSharedPtr<SWidget>>> PadLayout() const;
	TSharedRef<SWidget> BuildPadKeys();
	void ShowResults(const TArray<FOsdsPlace>& Places, const FString& Note);
	void Choose(const FOsdsPlace& Place);
	void DoSurprise();

	FOsdsOnPlace OnChoose;
	FOsdsOnSurprise OnSurprise;
	FSimpleDelegate OnClose;
	TSharedPtr<SEditableTextBox> Field;
	TSharedPtr<SVerticalBox> Choices;
	TSharedPtr<STextBlock> Status;
	TArray<TSharedPtr<SWidget>> ChoiceButtons;
	/** Search, Surprise me, Back to drive. */
	TArray<TSharedPtr<SWidget>> ToolButtons;
	TSharedPtr<SWidget> KeysPanel;
	TArray<TArray<TSharedPtr<SWidget>>> KeyRows;
	bool bKeys = false;
	int32 Cursor = 0;
	bool bPadUsed = false;
	OsdsTheme::FStickRepeat Stick;

	/** The newest search; older answers are dropped (Drive.gd query_serial). */
	struct FPending;
	TSharedPtr<FPending, ESPMode::ThreadSafe> Pending;
	uint32 Serial = 0;
};

/** A place name on a helper map tile: [name, x, y (0..4096 across the tile), priority, bold]. */
struct FOsdsMapLabel
{
	FString Text;
	FVector2D At = FVector2D::ZeroVector;
	int32 Priority = 0;
	bool bBold = false;
};

/** The road atlas: helper `map` tiles around the car, pan and zoom, pin a spot and drive there. */
class OSDS_API SOsdsRoadAtlas : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOsdsRoadAtlas) {}
		SLATE_EVENT(FOsdsGetAtlasView, GetView)
		SLATE_EVENT(FOsdsOnPlace, OnChoose)
		SLATE_EVENT(FOsdsOnSurprise, OnSurprise)
		SLATE_EVENT(FSimpleDelegate, OnFindPlace)
		SLATE_EVENT(FSimpleDelegate, OnCycleCar)
		SLATE_EVENT(FSimpleDelegate, OnToggleTransmission)
		SLATE_EVENT(FOsdsOnSetting, OnSetting)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SOsdsRoadAtlas() override;

	void Recenter();
	void Zoom(int32 Step, const FVector2D* AnchorPx = nullptr);
	void DriveToPin();

	/** Map tiles drawn and still to fetch (checks). */
	int32 NumMapImages() const;
	/** The top panel's buttons, row by row (tests). */
	const TArray<TArray<TSharedPtr<SWidget>>>& GetPadRows() const { return PadRows; }
	/** The map's centre, world metres, and the zoom step (Scales()) (tests). */
	FVector2D GetCenterM() const { return CenterM; }
	int32 GetLevel() const { return Level; }
	/** The pad hints now: the map's, or the buttons' while the pad is on the top panel. */
	TArray<OsdsTheme::FHint> GetPadHints() const;

	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	/** Zoom ladder, screen px per metre (RoadMap.gd SCALES). */
	static const TArray<double>& Scales();

private:
	struct FMapImage
	{
		TStrongObjectPtr<UTexture2D> Texture;
		TSharedPtr<FSlateBrush> Brush;
		TArray<FOsdsMapLabel> Names;
		double LastUsed = 0.0;
	};
	struct FFetch;

	FVector2D ScreenOf(const FVector2D& WorldM) const;
	FVector2D WorldOfScreen(const FVector2D& ScreenPx) const;
	void WantedTiles(TArray<FIntVector>& Out) const;
	void StartFetches();
	void CollectFetches();
	FOsdsPlace PlaceAt(const FVector2D& WorldM) const;

	FOsdsGetAtlasView GetView;
	FOsdsOnPlace OnChoose;
	FOsdsOnSurprise OnSurprise;
	FSimpleDelegate OnFindPlace;
	FSimpleDelegate OnCycleCar;
	FSimpleDelegate OnToggleTransmission;
	FOsdsOnSetting OnSetting;
	FSimpleDelegate OnClose;
	TArray<TSharedPtr<STextBlock>> SettingLabels;
	/** Every button on the top panel, row by row, as the pad walks them (D-pad), so each one is
	 *  reachable without a mouse. The pad is on the map while none of them has the focus. */
	TArray<TArray<TSharedPtr<SWidget>>> PadRows;
	/** Move the pad's focus between the buttons: Dir X along (across rows), Dir Y between rows. */
	void MovePad(int32 DX, int32 DY);
	bool IsOnButtons() const;

	FOsdsAtlasView View;
	int32 Level = 4;
	FVector2D CenterM = FVector2D::ZeroVector;
	bool bPinned = false;
	FVector2D PinM = FVector2D::ZeroVector;
	FVector2D SizePx = FVector2D(1280.0, 720.0);
	FVector2D Stick = FVector2D::ZeroVector;
	bool bDragging = false;
	bool bMoved = false;
	FVector2D PressAt = FVector2D::ZeroVector;
	FVector2D LastMouse = FVector2D::ZeroVector;
	bool bCentered = false;

	TMap<FString, FMapImage> Images;
	TMap<FString, double> RetryAt;
	TSharedPtr<FFetch, ESPMode::ThreadSafe> Fetch;
	TSharedPtr<STextBlock> Status;
	TSharedPtr<STextBlock> Coords;
	TSharedPtr<STextBlock> CarLabel;
	TSharedPtr<STextBlock> GearLabel;
};
