// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Gameplay/SimCruiseSystemCheck.h"

namespace SimCruiseSystemCheckTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	/** The tested laptop: RTX 2070 Super Max-Q, 8 GB, 32 GB of memory, online. */
	FSimCruisePcFacts TestedPc()
	{
		FSimCruisePcFacts F;
		F.AdapterName = TEXT("NVIDIA GeForce RTX 2070 Super with Max-Q Design");
		F.VideoMemoryMB = 8031;
		F.MemoryGB = 31;
		F.Net = ESimCruiseNet::Online;
		return F;
	}

	TArray<FString> Ids(const TArray<FSimCruisePcProblem>& Problems)
	{
		TArray<FString> Out;
		for (const FSimCruisePcProblem& P : Problems)
		{
			Out.Add(P.Id);
		}
		return Out;
	}
}

// docs/install.md "When the game starts": each lack of the PC gives one plain line, most important first.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseSystemCheckRulesTest, "SimCruise.Gameplay.SystemCheck.Rules", SimCruiseSystemCheckTest::Flags)
bool FSimCruiseSystemCheckRulesTest::RunTest(const FString& Parameters)
{
	using namespace SimCruiseSystemCheckTest;
	TestEqual(TEXT("the tested PC has no problem"), SimCruiseSystemCheck::Evaluate(TestedPc()).Num(), 0);
	{
		FSimCruisePcFacts F = TestedPc();
		F.Net = ESimCruiseNet::Unknown;
		TestEqual(TEXT("no internet answer yet is not a problem"), SimCruiseSystemCheck::Evaluate(F).Num(), 0);
	}
	{
		FSimCruisePcFacts F = TestedPc();
		F.VideoMemoryMB = 5900;
		F.MemoryGB = 15;
		TestEqual(TEXT("a 6 GB card and a 16 GB PC (as Windows reports them) pass"), SimCruiseSystemCheck::Evaluate(F).Num(), 0);
	}
	{
		// An old PC: DirectX 11 fallback, 4 GB card, 8 GB of memory, offline.
		FSimCruisePcFacts F = TestedPc();
		F.bDirectX12 = false;
		F.bShaderModel6 = false;
		F.VideoMemoryMB = 4096;
		F.MemoryGB = 7;
		F.Net = ESimCruiseNet::Offline;
		const TArray<FSimCruisePcProblem> P = SimCruiseSystemCheck::Evaluate(F);
		TestEqual(TEXT("the old PC's problems, in order"), FString::Join(Ids(P), TEXT(",")), FString(TEXT("gpu-dx12,video-memory,memory,offline")));
		if (P.Num() == 4)
		{
			TestTrue(TEXT("the card's memory in GB"), P[1].Text.Contains(TEXT("has 4 GB of video memory")));
			TestTrue(TEXT("the PC's memory in GB"), P[2].Text.Contains(TEXT("has 7 GB of memory")));
			TestTrue(TEXT("offline names the place that works"), P[3].Text.Contains(TEXT("Miami Beach")));
			TestTrue(TEXT("offline shows at every start"), P[3].bEveryStart && !P[0].bEveryStart);
		}
		TestEqual(TEXT("the seen key leaves out the every-start problems"), SimCruiseSystemCheck::SeenKey(P), FString(TEXT("gpu-dx12,video-memory,memory")));
	}
	{
		FSimCruisePcFacts F = TestedPc();
		F.bShaderModel6 = false;
		TestEqual(TEXT("DirectX 12 without Shader Model 6 is a graphics problem"), FString::Join(Ids(SimCruiseSystemCheck::Evaluate(F)), TEXT(",")), FString(TEXT("gpu-dx12")));
	}
	{
		// Built-in graphics: the card's own memory is not the point (it shares the PC's).
		FSimCruisePcFacts F = TestedPc();
		F.bIntegrated = true;
		F.AdapterName = TEXT("Intel(R) UHD Graphics");
		F.VideoMemoryMB = 128;
		const TArray<FSimCruisePcProblem> P = SimCruiseSystemCheck::Evaluate(F);
		TestEqual(TEXT("integrated: one line"), FString::Join(Ids(P), TEXT(",")), FString(TEXT("gpu-integrated")));
		TestTrue(TEXT("it names the adapter"), P.Num() == 1 && P[0].Text.Contains(TEXT("Intel(R) UHD Graphics")));
	}
	{
		// No helper: only Miami Beach works whatever the network says, and that line replaces the offline one.
		FSimCruisePcFacts F = TestedPc();
		F.bMapHelper = false;
		F.Net = ESimCruiseNet::Offline;
		TestEqual(TEXT("no helper"), FString::Join(Ids(SimCruiseSystemCheck::Evaluate(F)), TEXT(",")), FString(TEXT("map-helper")));
	}
	{
		FSimCruisePcFacts F = TestedPc();
		F.VideoMemoryMB = 0;
		F.MemoryGB = 0;
		TestEqual(TEXT("unknown sizes are not reported"), SimCruiseSystemCheck::Evaluate(F).Num(), 0);
	}
	TestFalse(TEXT("the internet check sends no position of the player"), SimCruiseSystemCheck::ProbeUrl().IsEmpty() || !SimCruiseSystemCheck::ProbeUrl().Contains(TEXT("latitude=25.78")));
	for (const FSimCruisePcProblem& P : SimCruiseSystemCheck::Evaluate(FSimCruisePcFacts{ false, false, TEXT("x"), true, 1, 4, false, ESimCruiseNet::Offline }))
	{
		TestTrue(*FString::Printf(TEXT("%s fits one HUD line (%d characters)"), *P.Id, P.Text.Len()), P.Text.Len() <= 150);
	}
	return true;
}

#endif
