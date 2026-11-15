// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SimCruiseSystemCheck.generated.h"

/**
 * The start check: what this PC lacks for the game, told to the player in plain words
 * (docs/install.md "When the game starts"). It reads the graphics API, the graphics card,
 * the memory and the map helper, and asks one web server whether there is an internet
 * connection. It cannot report a missing Visual C++ runtime: without it the game does not
 * start, so the installer and the launcher SimCruise.exe deal with that
 * (packaging/windows/simcruise.iss).
 */
enum class ESimCruiseNet : uint8
{
	Unknown,
	Online,
	Offline,
};

/** What the start check knows about this PC. */
struct SIMCRUISE_API FSimCruisePcFacts
{
	/** The game draws with DirectX 12 (else it fell back to DirectX 11, or something else). */
	bool bDirectX12 = true;
	/** The graphics card runs Shader Model 6 (Nanite, Lumen and virtual shadow maps). */
	bool bShaderModel6 = true;
	FString AdapterName;
	bool bIntegrated = false;
	/** Dedicated video memory in MB; 0 when unknown. */
	int64 VideoMemoryMB = 0;
	/** Installed memory in GB; 0 when unknown. */
	int32 MemoryGB = 0;
	/** tools/atlas.exe (every place but the bundled Miami Beach streets needs it). */
	bool bMapHelper = true;
	ESimCruiseNet Net = ESimCruiseNet::Unknown;
};

/** One thing the player should know. */
struct SIMCRUISE_API FSimCruisePcProblem
{
	/** gpu-dx12, gpu-integrated, video-memory, memory, map-helper or offline. */
	FString Id;
	/** One line for the HUD, in plain words. */
	FString Text;
	/** Shown at every start (else only when the PC's problems change). */
	bool bEveryStart = false;
};

namespace SimCruiseSystemCheck
{
	/** 6 GB cards report a little less (5.8-6.0 GB). */
	inline constexpr int64 MinVideoMemoryMB = 5600;
	/** 16 GB PCs report 15.7-15.9 GB. */
	inline constexpr int32 MinMemoryGB = 15;

	/** The problems in these facts, most important first. */
	SIMCRUISE_API TArray<FSimCruisePcProblem> Evaluate(const FSimCruisePcFacts& Facts);

	/** The facts of this PC, without the internet answer (Net is Unknown). */
	SIMCRUISE_API FSimCruisePcFacts GatherThisPc();

	/** The key that says which problems the player saw already (preferences "systemCheckSeen"). */
	SIMCRUISE_API FString SeenKey(const TArray<FSimCruisePcProblem>& Problems);

	/** The address asked for the internet check: the weather server the game uses, at Miami Beach. */
	SIMCRUISE_API FString ProbeUrl();
}

/**
 * Runs the start check once per game start and shows each problem on the HUD after the
 * start hint. Off in checks, captures and automation runs (SimCruisePreferences::IsEnabled)
 * unless -SimCruiseSystemCheck is on the command line; every result goes to the log as
 * "SYSTEMCHECK:" lines.
 */
UCLASS()
class SIMCRUISE_API USimCruiseSystemCheckSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	const FSimCruisePcFacts& GetFacts() const { return Facts; }
	const TArray<FSimCruisePcProblem>& GetProblems() const { return Problems; }

private:
	bool Tick(float DeltaSeconds);
	void StartProbe();
	void OnProbeDone(bool bOnline);
	void Finish();

	FTSTicker::FDelegateHandle TickHandle;
	FSimCruisePcFacts Facts;
	TArray<FSimCruisePcProblem> Problems;
	/** The problems still to show, in order. */
	TArray<FString> ToShow;
	bool bGathered = false;
	bool bProbeDone = false;
	bool bFinished = false;
	double StartedSeconds = 0.0;
	/** When the player's car was first seen (0: not yet). */
	double CarSeenSeconds = 0.0;
	double NextShowSeconds = 0.0;
};
