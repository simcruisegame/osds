// Copyright OSDS contributors. All Rights Reserved.

#include "Environment/OsdsGraphics.h"
#include "RenderUtils.h"

#include "Gameplay/OsdsPreferences.h"
#include "HAL/IConsoleManager.h"
#include "RHIGlobals.h"
#include "Scalability.h"

DEFINE_LOG_CATEGORY_STATIC(LogOsdsGraphics, Log, All);

namespace
{
	EOsdsGraphicsPreset GCurrentPreset = EOsdsGraphicsPreset::High;

	EOsdsGraphicsPreset StepDown(EOsdsGraphicsPreset Preset)
	{
		return Preset == EOsdsGraphicsPreset::Low ? Preset : static_cast<EOsdsGraphicsPreset>(static_cast<uint8>(Preset) - 1);
	}

	TAutoConsoleVariable<float> CVarFoliageScale(TEXT("osds.World.FoliageScale"), 1.0f,
		TEXT("Scale on CARLA roadside plant density (set by the graphics preset: Ultra 1.5)."), ECVF_Default);

	void SetCVar(const TCHAR* Name, const FString& Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(*Value, ECVF_SetByGameSetting);
		}
	}
}

FString FOsdsGraphics::Name(EOsdsGraphicsPreset Preset)
{
	switch (Preset)
	{
	case EOsdsGraphicsPreset::Low: return TEXT("low");
	case EOsdsGraphicsPreset::Medium: return TEXT("medium");
	case EOsdsGraphicsPreset::High: return TEXT("high");
	case EOsdsGraphicsPreset::Ultra: return TEXT("ultra");
	}
	return TEXT("high");
}

bool FOsdsGraphics::Parse(const FString& Text, EOsdsGraphicsPreset& OutPreset)
{
	const FString Lower = Text.TrimStartAndEnd().ToLower();
	for (uint8 Index = 0; Index <= static_cast<uint8>(EOsdsGraphicsPreset::Ultra); ++Index)
	{
		const EOsdsGraphicsPreset Candidate = static_cast<EOsdsGraphicsPreset>(Index);
		if (Lower == Name(Candidate))
		{
			OutPreset = Candidate;
			return true;
		}
	}
	if (Lower == TEXT("epic"))
	{
		OutPreset = EOsdsGraphicsPreset::Ultra;
		return true;
	}
	return false;
}

float FOsdsGraphics::SharpenFor(float ScreenPercentage)
{
	return FMath::Clamp(0.4f + 1.2f * (1.0f - ScreenPercentage / 100.0f), 0.4f, 1.0f);
}

FOsdsPresetSpec FOsdsGraphics::Spec(EOsdsGraphicsPreset Preset)
{
	// Screen percentages and the resulting frame times on the RTX 2070 Super Max-Q at
	// 1920x1080 are recorded in docs/unreal-architecture.md (Light, sky and rendering).
	FOsdsPresetSpec Out;
	switch (Preset)
	{
	case EOsdsGraphicsPreset::Low:
		Out.ScalabilityLevel = 0;
		Out.ScreenPercentage = 50.0f;
		Out.bLumenGI = false;
		Out.bLumenReflections = false;
		break;
	case EOsdsGraphicsPreset::Medium:
		Out.ScalabilityLevel = 1;
		Out.ScreenPercentage = 58.0f;
		Out.bLumenGI = true;
		Out.bLumenReflections = false;
		break;
	case EOsdsGraphicsPreset::High:
		// The owner's laptop (RTX 2070 Super Max-Q): about 45 fps for ray-traced Lumen and a 75 %
		// picture rather than 60 fps at 67 % with a half-blind Lumen (owner, 2026-11-17: "we can
		// accept lower framerates on this PC").
		Out.ScalabilityLevel = 2;
		Out.ScreenPercentage = 75.0f;
		Out.TargetFps = 45.0f;
		Out.bHardwareLumen = true;
		Out.MirrorInterval = 2;
		break;
	case EOsdsGraphicsPreset::Ultra:
		// For stronger PCs than the owner's laptop: everything on at 60 fps.
		Out.ScalabilityLevel = 3;
		Out.ScreenPercentage = 100.0f;
		Out.TargetFps = 60.0f;
		Out.bHardwareLumen = true;
		Out.bEpicGI = true;
		Out.FoliageScale = 1.5f;
		Out.MirrorInterval = 1;
		break;
	}
	return Out;
}

EOsdsGraphicsPreset FOsdsGraphics::AutoPick(const FString& AdapterName, bool bIntegrated, int64 DedicatedVideoMemoryMB, bool bAllowBenchmark)
{
	const FString Name = AdapterName.ToLower();
	if (Name.Contains(TEXT("microsoft basic")) || Name.Contains(TEXT("llvmpipe")) || Name.Contains(TEXT("swiftshader")) || Name.Contains(TEXT("warp")))
	{
		return EOsdsGraphicsPreset::Low;
	}
	if (bIntegrated || (Name.Contains(TEXT("intel")) && !Name.Contains(TEXT("arc"))))
	{
		return EOsdsGraphicsPreset::Low;
	}

	const bool bLaptop = Name.Contains(TEXT("max-q")) || Name.Contains(TEXT("laptop")) || Name.Contains(TEXT("mobile"));
	TOptional<EOsdsGraphicsPreset> Desktop;
	struct FTier { const TCHAR* Needle; EOsdsGraphicsPreset Preset; };
	// Order matters: the first needle found wins.
	static const FTier Tiers[] = {
		{TEXT("rtx 50"), EOsdsGraphicsPreset::Ultra}, {TEXT("rtx 40"), EOsdsGraphicsPreset::Ultra},
		{TEXT("rtx 3050"), EOsdsGraphicsPreset::Medium}, {TEXT("rtx 30"), EOsdsGraphicsPreset::Ultra},
		{TEXT("rx 9"), EOsdsGraphicsPreset::Ultra}, {TEXT("rx 7"), EOsdsGraphicsPreset::Ultra},
		// The desktop 2070/2080 start on Ultra; laptop parts one step lower, Max-Q parts two
		// (measured on the RTX 2070 Super Max-Q, see below).
		{TEXT("rtx 2080"), EOsdsGraphicsPreset::Ultra}, {TEXT("rtx 2070"), EOsdsGraphicsPreset::Ultra},
		{TEXT("rtx 2060"), EOsdsGraphicsPreset::High}, {TEXT("rx 6"), EOsdsGraphicsPreset::Ultra},
		{TEXT("arc"), EOsdsGraphicsPreset::Medium}, {TEXT("gtx 16"), EOsdsGraphicsPreset::Medium},
		{TEXT("gtx 10"), EOsdsGraphicsPreset::Low}, {TEXT("rx 5"), EOsdsGraphicsPreset::Medium},
		{TEXT("apple m"), EOsdsGraphicsPreset::Medium},
	};
	for (const FTier& Tier : Tiers)
	{
		if (Name.Contains(Tier.Needle))
		{
			Desktop = Tier.Preset;
			break;
		}
	}

	EOsdsGraphicsPreset Pick = EOsdsGraphicsPreset::Medium;
	if (Desktop.IsSet())
	{
		Pick = bLaptop ? StepDown(Desktop.GetValue()) : Desktop.GetValue();
		// A Max-Q part used to step down once more (High 57 fps on Ocean Drive missed 60); High is
		// now tuned to 45 fps on exactly that laptop (the owner's), so the laptop step is enough.
	}
	else if (bAllowBenchmark)
	{
		const Scalability::FQualityLevels Measured = Scalability::BenchmarkQualityLevels();
		Pick = static_cast<EOsdsGraphicsPreset>(FMath::Clamp(Measured.GlobalIlluminationQuality, 0, 3));
	}

	// Ultra's virtual shadow pages, Lumen surface cache and history need room.
	if (DedicatedVideoMemoryMB > 0 && DedicatedVideoMemoryMB < 7000 && Pick == EOsdsGraphicsPreset::Ultra)
	{
		Pick = EOsdsGraphicsPreset::High;
	}
	if (DedicatedVideoMemoryMB > 0 && DedicatedVideoMemoryMB < 4500 && Pick >= EOsdsGraphicsPreset::High)
	{
		Pick = EOsdsGraphicsPreset::Medium;
	}
	return Pick;
}

EOsdsGraphicsPreset FOsdsGraphics::AutoPickForThisMachine(bool bAllowBenchmark)
{
	const int64 VideoMB = static_cast<int64>(GRHIGlobals.GpuInfo.DedicatedVideoMemory / (1024ull * 1024ull));
	const EOsdsGraphicsPreset Pick = AutoPick(GRHIAdapterName, GRHIDeviceIsIntegrated, VideoMB, bAllowBenchmark);
	UE_LOG(LogOsdsGraphics, Display, TEXT("Graphics auto-pick: '%s' (%lld MB, integrated %d) -> %s"),
		*GRHIAdapterName, VideoMB, GRHIDeviceIsIntegrated ? 1 : 0, *Name(Pick));
	return Pick;
}

void FOsdsGraphics::Apply(EOsdsGraphicsPreset Preset)
{
	const FOsdsPresetSpec S = Spec(Preset);
	Scalability::FQualityLevels Levels;
	Levels.SetFromSingleQualityLevel(S.ScalabilityLevel);
	// Ultra keeps Lumen GI at High (scalability 2): Epic GI cost about 1.5 ms of the 21.5 ms Ultra
	// frame for a difference in bounce light the release review could not see (2026-11-14).
	if (!S.bEpicGI) Levels.GlobalIlluminationQuality = FMath::Min(Levels.GlobalIlluminationQuality, 2);
	Levels.ResolutionQuality = S.ScreenPercentage;
	Scalability::SetQualityLevels(Levels, true);

	// TSR on every preset: it is what makes the lower internal resolutions hold detail.
	// r.AntiAliasingMethod is 4 (TSR) in DefaultEngine.ini; set here it was refused with a warning on every launch.
	SetCVar(TEXT("r.ScreenPercentage"), FString::SanitizeFloat(S.ScreenPercentage));
	// Sharpen after the upscale, more the lower the internal resolution: TSR's output from a
	// 58 % picture is soft (the project owner: "the graphics look fuzzy"). 100 % gets a light 0.4.
	SetCVar(TEXT("r.Tonemapper.Sharpen"), FString::SanitizeFloat(SharpenFor(S.ScreenPercentage)));
	// Frame guard: when the GPU cannot hold 60 fps (a laptop held at 28 W), TSR's internal picture
	// shrinks, down to 50 %, so frames come on time; the preset's screen percentage stays the top.
	SetCVar(TEXT("r.DynamicRes.OperationMode"), TEXT("2"));
	// Headroom under the preset's frame time (15 ms at 60 fps): a frame never waits a whole refresh.
	SetCVar(TEXT("r.DynamicRes.FrameTimeBudget"), FString::SanitizeFloat(FMath::RoundToFloat(900.0f / S.TargetFps)));
	// The floor sits 15 % under the preset, not at 50 %: the release test found Ultra held at
	// 50-53 % on Tail of the Dragon and Ocean Drive (smeared trees, a coarse mirror) although at a
	// fixed 75 % the frame p95 was 16.9 and 20.1 ms (main 8456dea, 2026-11-14).
	SetCVar(TEXT("r.DynamicRes.MinScreenPercentage"), FString::SanitizeFloat(FMath::Max(50.0f, S.ScreenPercentage * 0.85f)));
	SetCVar(TEXT("r.DynamicRes.MaxScreenPercentage"), FString::SanitizeFloat(S.ScreenPercentage));
	SetCVar(TEXT("r.Lumen.DiffuseIndirect.Allow"), S.bLumenGI ? TEXT("1") : TEXT("0"));
	SetCVar(TEXT("r.Lumen.Reflections.Allow"), S.bLumenReflections ? TEXT("1") : TEXT("0"));
	// Hardware ray tracing only where the RHI has it (r.RayTracing is on in DefaultEngine.ini; a GPU
	// without it keeps software Lumen).
	SetCVar(TEXT("r.Lumen.HardwareRayTracing"), (S.bHardwareLumen && IsRayTracingEnabled()) ? TEXT("1") : TEXT("0"));
	SetCVar(TEXT("osds.Mirrors.Interval"), FString::FromInt(S.MirrorInterval));
	SetCVar(TEXT("osds.World.FoliageScale"), FString::SanitizeFloat(S.FoliageScale));

	GCurrentPreset = Preset;
	UE_LOG(LogOsdsGraphics, Display, TEXT("Graphics preset %s: scalability %d, %.0f%% screen percentage with TSR, %.0f fps target, Lumen GI %d reflections %d hardware %d"),
		*Name(Preset), S.ScalabilityLevel, S.ScreenPercentage, S.TargetFps, S.bLumenGI ? 1 : 0, S.bLumenReflections ? 1 : 0, (S.bHardwareLumen && IsRayTracingEnabled()) ? 1 : 0);
}

EOsdsGraphicsPreset FOsdsGraphics::Current()
{
	return GCurrentPreset;
}

FString FOsdsGraphics::ChoiceName(EOsdsGraphicsChoice Choice)
{
	switch (Choice)
	{
	case EOsdsGraphicsChoice::Low: return TEXT("low");
	case EOsdsGraphicsChoice::Medium: return TEXT("medium");
	case EOsdsGraphicsChoice::High: return TEXT("high");
	case EOsdsGraphicsChoice::Ultra: return TEXT("ultra");
	default: return TEXT("auto");
	}
}

bool FOsdsGraphics::ParseChoice(const FString& Text, EOsdsGraphicsChoice& OutChoice)
{
	for (uint8 Index = 0; Index <= static_cast<uint8>(EOsdsGraphicsChoice::Ultra); ++Index)
	{
		const EOsdsGraphicsChoice Candidate = static_cast<EOsdsGraphicsChoice>(Index);
		if (ChoiceName(Candidate).Equals(Text, ESearchCase::IgnoreCase))
		{
			OutChoice = Candidate;
			return true;
		}
	}
	return false;
}

EOsdsGraphicsChoice FOsdsGraphics::StepChoice(EOsdsGraphicsChoice Choice, int32 Direction)
{
	const int32 Now = FMath::Clamp(static_cast<int32>(Choice), 0, 4);
	const int32 Delta = Direction < 0 ? -1 : 1;
	return static_cast<EOsdsGraphicsChoice>((Now + Delta + 5) % 5);
}

EOsdsGraphicsChoice FOsdsGraphics::SavedChoice()
{
	EOsdsGraphicsChoice Choice = EOsdsGraphicsChoice::Auto;
	ParseChoice(OsdsPreferences::GetString(TEXT("graphics_quality"), TEXT("auto")), Choice);
	return Choice;
}

FString FOsdsGraphics::ChoiceDisplay()
{
	const EOsdsGraphicsChoice Choice = SavedChoice();
	const TCHAR* Labels[] = { TEXT("Auto"), TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Ultra") };
	if (Choice == EOsdsGraphicsChoice::Auto)
	{
		const FString Actual = Name(Current());
		const FString ActualTitle = Actual.Left(1).ToUpper() + Actual.Mid(1);
		return FString::Printf(TEXT("Auto (%s)"), *ActualTitle);
	}
	return Labels[static_cast<uint8>(Choice)];
}

void FOsdsGraphics::ApplyChoice(EOsdsGraphicsChoice Choice)
{
	if (Choice == EOsdsGraphicsChoice::Auto)
	{
		// The machine does not change during a session: pick once (the synthetic benchmark, for a GPU
		// missing from the tier table, runs on the game thread and froze the menu on each Auto choice).
		static TOptional<EOsdsGraphicsPreset> AutoPick;
		if (!AutoPick.IsSet())
		{
			AutoPick = AutoPickForThisMachine(true);
		}
		Apply(AutoPick.GetValue());
		return;
	}
	Apply(static_cast<EOsdsGraphicsPreset>(static_cast<uint8>(Choice) - 1));
}

void FOsdsGraphics::SelectChoice(EOsdsGraphicsChoice Choice)
{
	OsdsPreferences::SetString(TEXT("graphics_quality"), ChoiceName(Choice));
	ApplyChoice(Choice);
}

void FOsdsGraphics::ChooseQuality(int32 Direction)
{
	const EOsdsGraphicsChoice Choice = Direction == 2
		? EOsdsGraphicsChoice::Auto
		: StepChoice(SavedChoice(), Direction);
	SelectChoice(Choice);
}
