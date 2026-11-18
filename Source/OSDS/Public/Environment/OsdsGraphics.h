// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OsdsGraphics.generated.h"

/** The four graphics presets (Unreal scalability levels 0-3 plus OSDS's own settings). */
UENUM(BlueprintType)
enum class EOsdsGraphicsPreset : uint8
{
	Low,
	Medium,
	High,
	Ultra
};

/** The player's selection; Auto resolves to a preset for the graphics adapter. */
enum class EOsdsGraphicsChoice : uint8
{
	Auto,
	Low,
	Medium,
	High,
	Ultra
};

/** What a preset sets on top of Unreal's scalability groups. */
struct OSDS_API FOsdsPresetSpec
{
	/** Unreal scalability level for every group (0 Low .. 3 Epic). */
	int32 ScalabilityLevel = 2;
	/** 3D resolution before TSR upscales to the output, percent of the output's height. */
	float ScreenPercentage = 67.0f;
	/** Lumen reflections on (else the sky light and screen-space reflections). */
	bool bLumenReflections = true;
	/** Lumen global illumination on (else the sky light with distance-field AO). */
	bool bLumenGI = true;
	/** Frame-rate the preset is tuned to hold: the dynamic-resolution budget (High is tuned to the
	 *  owner's RTX 2070 Super Max-Q laptop, taking about 45 fps for a sharper, better-lit picture). */
	float TargetFps = 60.0f;
	/** Lumen traces the scene with hardware ray tracing (works on the meshes the game builds at run
	 *  time, which get no distance fields outside the editor); falls back to software where the GPU
	 *  or driver has none. */
	bool bHardwareLumen = false;
	/** Global illumination at Epic (else held at High). */
	bool bEpicGI = false;
	/** Scale on CARLA roadside plant density and foliage draw distance. */
	float FoliageScale = 1.0f;
	/** Mirrors render every Nth frame. */
	int32 MirrorInterval = 3;
};

struct OSDS_API FOsdsGraphics
{
	static FString Name(EOsdsGraphicsPreset Preset);

	/** "low" / "medium" / "high" / "ultra" (any case). */
	static bool Parse(const FString& Text, EOsdsGraphicsPreset& OutPreset);

	static FOsdsPresetSpec Spec(EOsdsGraphicsPreset Preset);
	/** Tonemapper sharpening (r.Tonemapper.Sharpen) for a TSR screen percentage: stronger when the internal picture is smaller. */
	static float SharpenFor(float ScreenPercentage);

	/**
	 * The preset this GPU should start on. Integrated and software adapters get Low;
	 * names are matched against the tiers measured in docs/unreal-architecture.md, with
	 * laptop (Max-Q / Laptop) parts one tier below their desktop name. Unknown adapters fall back
	 * to Unreal's synthetic benchmark when bAllowBenchmark is true, otherwise Medium.
	 */
	static EOsdsGraphicsPreset AutoPick(const FString& AdapterName, bool bIntegrated, int64 DedicatedVideoMemoryMB, bool bAllowBenchmark = false);

	/** AutoPick for the adapter the renderer actually opened. */
	static EOsdsGraphicsPreset AutoPickForThisMachine(bool bAllowBenchmark = true);

	/** Applies a preset now (scalability groups, screen percentage, TSR and the extras). */
	static void Apply(EOsdsGraphicsPreset Preset);

	/** The preset last applied (High before any Apply). */
	static EOsdsGraphicsPreset Current();

	/** The five choices shown on the Settings page and saved in preferences. */
	static FString ChoiceName(EOsdsGraphicsChoice Choice);
	static bool ParseChoice(const FString& Text, EOsdsGraphicsChoice& OutChoice);
	static EOsdsGraphicsChoice StepChoice(EOsdsGraphicsChoice Choice, int32 Direction);
	static EOsdsGraphicsChoice SavedChoice();
	static FString ChoiceDisplay();
	static void ApplyChoice(EOsdsGraphicsChoice Choice);
	static void SelectChoice(EOsdsGraphicsChoice Choice);
	/** Handle the Settings row: -1/1 step, 0 advances, and 2 resets to Auto. */
	static void ChooseQuality(int32 Direction);
};
