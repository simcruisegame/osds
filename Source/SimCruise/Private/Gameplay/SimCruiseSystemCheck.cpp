// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseSystemCheck.h"

#include "Atlas/SimCruiseAtlasClient.h"
#include "DynamicRHI.h"
#include "Engine/GameInstance.h"
#include "Gameplay/SimCruisePlayerController.h"
#include "Gameplay/SimCruisePreferences.h"
#include "HAL/PlatformMemory.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "RHIFeatureLevel.h"
#include "RHIGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseSystemCheck, Log, All);

namespace
{
	/** The start hint shows for 6 s after the car is placed; the problems come after it. */
	constexpr double AfterCarSeconds = 7.0;
	/** Shown anyway when no car arrives (a start that stays in a menu, or a failed load). */
	constexpr double NoCarSeconds = 45.0;
	constexpr float ShowSeconds = 9.f;
	constexpr float ProbeTimeoutSeconds = 8.f;

	int64 RoundGB(int64 MB)
	{
		return (MB + 512) / 1024;
	}
}

namespace SimCruiseSystemCheck
{
	TArray<FSimCruisePcProblem> Evaluate(const FSimCruisePcFacts& Facts)
	{
		TArray<FSimCruisePcProblem> Out;
		auto Add = [&Out](const TCHAR* Id, FString Text, bool bEveryStart)
		{
			FSimCruisePcProblem& P = Out.AddDefaulted_GetRef();
			P.Id = Id;
			P.Text = MoveTemp(Text);
			P.bEveryStart = bEveryStart;
		};
		if (!Facts.bDirectX12 || !Facts.bShaderModel6)
		{
			Add(TEXT("gpu-dx12"), TEXT("Your graphics card or its driver has no DirectX 12 (Shader Model 6). The picture is simple. Update the graphics driver."), false);
		}
		if (Facts.bIntegrated)
		{
			Add(TEXT("gpu-integrated"), FString::Printf(TEXT("The game uses the built-in graphics (%s). If your PC has an NVIDIA or AMD card, select it in Windows graphics settings."),
				Facts.AdapterName.IsEmpty() ? TEXT("no name") : *Facts.AdapterName), false);
		}
		else if (Facts.VideoMemoryMB > 0 && Facts.VideoMemoryMB < SimCruiseSystemCheck::MinVideoMemoryMB)
		{
			Add(TEXT("video-memory"), FString::Printf(TEXT("Your graphics card has %lld GB of video memory. The game needs 6 GB, so it can be slow. Use lower graphics settings."),
				RoundGB(Facts.VideoMemoryMB)), false);
		}
		if (Facts.MemoryGB > 0 && Facts.MemoryGB < SimCruiseSystemCheck::MinMemoryGB)
		{
			Add(TEXT("memory"), FString::Printf(TEXT("Your PC has %d GB of memory. The game needs 16 GB, so it can be slow or stop."), Facts.MemoryGB), false);
		}
		if (!Facts.bMapHelper)
		{
			Add(TEXT("map-helper"), TEXT("The map helper (tools\\atlas.exe) is missing. Only Miami Beach works. Install the game again."), true);
		}
		else if (Facts.Net == ESimCruiseNet::Offline)
		{
			Add(TEXT("offline"), TEXT("No internet connection. Only Ocean Drive, Miami Beach works. Connect to the internet to go to other places."), true);
		}
		return Out;
	}

	FSimCruisePcFacts GatherThisPc()
	{
		FSimCruisePcFacts Facts;
		if (GDynamicRHI)
		{
			Facts.bDirectX12 = RHIGetInterfaceType() == ERHIInterfaceType::D3D12;
			Facts.bShaderModel6 = GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM6;
			Facts.AdapterName = GRHIAdapterName;
			Facts.bIntegrated = GRHIDeviceIsIntegrated;
			Facts.VideoMemoryMB = static_cast<int64>(GRHIGlobals.GpuInfo.DedicatedVideoMemory / (1024ull * 1024ull));
		}
		// TotalPhysicalGB rounds up; the check wants what is installed, so it uses the bytes.
		Facts.MemoryGB = static_cast<int32>(FPlatformMemory::GetConstants().TotalPhysical / (1024ull * 1024ull * 1024ull));
		Facts.bMapHelper = FSimCruiseAtlasConfig::ResolveDefault().IsConfigured();
		return Facts;
	}

	FString SeenKey(const TArray<FSimCruisePcProblem>& Problems)
	{
		FString Key;
		for (const FSimCruisePcProblem& P : Problems)
		{
			if (!P.bEveryStart)
			{
				Key += (Key.IsEmpty() ? TEXT("") : TEXT(",")) + P.Id;
			}
		}
		return Key;
	}

	FString ProbeUrl()
	{
		// No position of the player goes out: always the bundled start's point.
		return TEXT("https://api.open-meteo.com/v1/forecast?latitude=25.78&longitude=-80.13&current=temperature_2m");
	}
}

bool USimCruiseSystemCheckSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (FParse::Param(FCommandLine::Get(), TEXT("SimCruiseSystemCheck")))
	{
		return true;
	}
	// The editor and the checks, captures and automation runs stay quiet and offline.
	return !GIsEditor && !IsRunningCommandlet() && SimCruisePreferences::IsEnabled();
}

void USimCruiseSystemCheckSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	StartedSeconds = FPlatformTime::Seconds();
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &USimCruiseSystemCheckSubsystem::Tick), 0.25f);
}

void USimCruiseSystemCheckSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	Super::Deinitialize();
}

void USimCruiseSystemCheckSubsystem::StartProbe()
{
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	// -SimCruiseLiveOffline sends it to a closed local port, as for the live feeds: a test of an offline start.
	const bool bOffline = FParse::Param(FCommandLine::Get(), TEXT("SimCruiseLiveOffline"));
	Request->SetURL(bOffline ? FString(TEXT("http://127.0.0.1:9/offline")) : SimCruiseSystemCheck::ProbeUrl());
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("User-Agent"), TEXT("SimCruise/2.0 (+https://github.com/simcruisegame/simcruise)"));
	Request->SetTimeout(ProbeTimeoutSeconds);
	TWeakObjectPtr<USimCruiseSystemCheckSubsystem> Weak(this);
	Request->OnProcessRequestComplete().BindLambda([Weak](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
	{
		if (USimCruiseSystemCheckSubsystem* Self = Weak.Get())
		{
			// Any answer from the server, even an error code, means there is a connection.
			Self->OnProbeDone(bConnected && Response.IsValid() && Response->GetResponseCode() > 0);
		}
	});
	if (!Request->ProcessRequest())
	{
		OnProbeDone(false);
	}
}

void USimCruiseSystemCheckSubsystem::OnProbeDone(bool bOnline)
{
	if (bProbeDone)
	{
		return;
	}
	bProbeDone = true;
	Facts.Net = bOnline ? ESimCruiseNet::Online : ESimCruiseNet::Offline;
	Finish();
}

void USimCruiseSystemCheckSubsystem::Finish()
{
	Problems = SimCruiseSystemCheck::Evaluate(Facts);
	UE_LOG(LogSimCruiseSystemCheck, Display, TEXT("SYSTEMCHECK: rhi=%s sm6=%d gpu='%s' integrated=%d vram=%lld MB memory=%d GB helper=%d internet=%s problems=%d"),
		Facts.bDirectX12 ? TEXT("D3D12") : TEXT("other"), Facts.bShaderModel6 ? 1 : 0, *Facts.AdapterName, Facts.bIntegrated ? 1 : 0,
		Facts.VideoMemoryMB, Facts.MemoryGB, Facts.bMapHelper ? 1 : 0,
		Facts.Net == ESimCruiseNet::Online ? TEXT("yes") : Facts.Net == ESimCruiseNet::Offline ? TEXT("no") : TEXT("unknown"), Problems.Num());
	for (const FSimCruisePcProblem& P : Problems)
	{
		UE_LOG(LogSimCruiseSystemCheck, Display, TEXT("SYSTEMCHECK: %s: %s"), *P.Id, *P.Text);
	}
	// A problem of the PC itself is shown once (again when it changes); offline and a missing helper every start.
	const FString Key = SimCruiseSystemCheck::SeenKey(Problems);
	const bool bSeen = SimCruisePreferences::IsEnabled() && SimCruisePreferences::GetString(TEXT("systemCheckSeen"), TEXT("")) == Key;
	for (const FSimCruisePcProblem& P : Problems)
	{
		if (P.bEveryStart || !bSeen)
		{
			ToShow.Add(P.Text);
		}
	}
	if (SimCruisePreferences::IsEnabled())
	{
		SimCruisePreferences::SetString(TEXT("systemCheckSeen"), Key);
	}
	bFinished = true;
}

bool USimCruiseSystemCheckSubsystem::Tick(float DeltaSeconds)
{
	const double Now = FPlatformTime::Seconds();
	if (!bGathered)
	{
		// The first tick: the RHI is up and the engine loop runs.
		bGathered = true;
		Facts = SimCruiseSystemCheck::GatherThisPc();
		StartProbe();
		return true;
	}
	if (!bFinished)
	{
		return true;
	}
	if (ToShow.IsEmpty())
	{
		return false; // done: the ticker drops this
	}
	UGameInstance* Instance = GetGameInstance();
	ASimCruisePlayerController* Player = Instance ? Cast<ASimCruisePlayerController>(Instance->GetFirstLocalPlayerController()) : nullptr;
	if (!Player)
	{
		return true;
	}
	if (CarSeenSeconds <= 0.0 && Player->HasVehicle())
	{
		CarSeenSeconds = Now;
	}
	const bool bAfterHint = CarSeenSeconds > 0.0 ? Now - CarSeenSeconds >= AfterCarSeconds : Now - StartedSeconds >= NoCarSeconds;
	if (bAfterHint && Now >= NextShowSeconds)
	{
		Player->ShowMessage(ToShow[0], ShowSeconds);
		ToShow.RemoveAt(0);
		NextShowSeconds = Now + ShowSeconds;
	}
	return true;
}
