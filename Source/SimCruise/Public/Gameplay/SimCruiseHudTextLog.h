// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The text the HUD draws on the canvas (speed, gear, car name, place cards, messages), for tests:
 * the canvas text is not Slate, so the pad walk (ASimCruisePadReview) cannot read it from the
 * widget tree. Off by default (a test turns it on); then each drawn string is kept with the time
 * it was last drawn.
 */
namespace SimCruiseHudTextLog
{
	SIMCRUISE_API void Enable(bool bOn);
	SIMCRUISE_API bool IsEnabled();
	/** Called by the HUD's text draw functions. Cheap when off. */
	SIMCRUISE_API void Note(const FString& Text);
	/** The strings drawn within the last Seconds. */
	SIMCRUISE_API TArray<FString> Recent(double Seconds);
}
