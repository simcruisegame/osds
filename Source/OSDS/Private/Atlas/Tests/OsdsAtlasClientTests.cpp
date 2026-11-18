// Copyright OSDS contributors. All Rights Reserved.

#include "Atlas/OsdsAtlasClient.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && PLATFORM_WINDOWS

namespace
{
	void ExpectError(FAutomationTestBase& Test, const TCHAR* What, EOsdsAtlasError Actual, EOsdsAtlasError Expected)
	{
		Test.TestTrue(FString::Printf(TEXT("%s (got %s, want %s)"), What, LexToString(Actual), LexToString(Expected)), Actual == Expected);
	}

	/** A stand-in helper: cmd.exe /c <command> (no network, no Bun). */
	FOsdsAtlasConfig ShellConfig(int32 MaxConcurrent = 2)
	{
		FOsdsAtlasConfig Config;
		Config.Executable = FPlatformMisc::GetEnvironmentVariable(TEXT("ComSpec"));
		if (Config.Executable.IsEmpty()) Config.Executable = TEXT("C:\\Windows\\System32\\cmd.exe");
		Config.PrefixArgs = { TEXT("/c") };
		Config.MaxConcurrent = MaxConcurrent;
		return Config;
	}

	FOsdsAtlasRequest Shell(const FString& Command, const TArray<FString>& Args, double Timeout = 20.0)
	{
		FOsdsAtlasRequest R;
		R.Command = Command;
		R.Args = Args;
		R.TimeoutSeconds = Timeout;
		return R;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsAtlasProcessResultsTest, "OSDS.Atlas.ProcessResults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FOsdsAtlasProcessResultsTest::RunTest(const FString& Parameters)
{
	const FOsdsAtlasConfig Config = ShellConfig();

	FOsdsAtlasResult Ok = FOsdsAtlasClient::RunBlocking(Config, Shell(TEXT("echo"), { TEXT("[1,2,3]") }), nullptr);
	ExpectError(*this, TEXT("valid JSON is accepted"), Ok.Error, EOsdsAtlasError::None);
	TestTrue(TEXT("the document is parsed"), Ok.Json.IsValid() && Ok.Json->Type == EJson::Array && Ok.Json->AsArray().Num() == 3);

	FOsdsAtlasResult Bad = FOsdsAtlasClient::RunBlocking(Config, Shell(TEXT("echo"), { TEXT("not-json") }), nullptr);
	ExpectError(*this, TEXT("malformed output is a typed error"), Bad.Error, EOsdsAtlasError::MalformedOutput);

	FOsdsAtlasResult Empty = FOsdsAtlasClient::RunBlocking(Config, Shell(TEXT("rem"), {}), nullptr);
	ExpectError(*this, TEXT("no output is a typed error"), Empty.Error, EOsdsAtlasError::EmptyOutput);

	FOsdsAtlasResult Exit = FOsdsAtlasClient::RunBlocking(Config, Shell(TEXT("exit"), { TEXT("3") }), nullptr);
	ExpectError(*this, TEXT("a failing helper reports its exit code"), Exit.Error, EOsdsAtlasError::ExitCode);
	TestEqual(TEXT("exit code"), Exit.ExitCode, 3);

	const double T0 = FPlatformTime::Seconds();
	FOsdsAtlasResult Slow = FOsdsAtlasClient::RunBlocking(Config, Shell(TEXT("ping"), { TEXT("-n"), TEXT("30"), TEXT("127.0.0.1") }, 1.0), nullptr);
	const double Elapsed = FPlatformTime::Seconds() - T0;
	ExpectError(*this, TEXT("a hung helper times out"), Slow.Error, EOsdsAtlasError::Timeout);
	TestTrue(FString::Printf(TEXT("timeout is prompt (%.2f s)"), Elapsed), Elapsed < 5.0);

	FOsdsAtlasConfig Missing = Config;
	Missing.Executable = TEXT("C:\\definitely\\not\\here\\atlas.exe");
	ExpectError(*this, TEXT("a missing helper fails to launch"), FOsdsAtlasClient::RunBlocking(Missing, Shell(TEXT("tile"), {}), nullptr).Error, EOsdsAtlasError::LaunchFailed);

	FOsdsAtlasConfig None;
	ExpectError(*this, TEXT("no helper configured"), FOsdsAtlasClient::RunBlocking(None, Shell(TEXT("tile"), {}), nullptr).Error, EOsdsAtlasError::NotConfigured);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsAtlasAsyncTest, "OSDS.Atlas.AsyncQueue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FOsdsAtlasAsyncTest::RunTest(const FString& Parameters)
{
	TSharedRef<FOsdsAtlasClient, ESPMode::ThreadSafe> Client = MakeShared<FOsdsAtlasClient, ESPMode::ThreadSafe>(ShellConfig(2));

	// Issuing work never blocks the caller (the game thread).
	const double T0 = FPlatformTime::Seconds();
	TArray<TSharedRef<FOsdsAtlasJob, ESPMode::ThreadSafe>> Jobs;
	for (int32 i = 0; i < 4; ++i)
	{
		Jobs.Add(Client->Run(Shell(TEXT("ping"), { TEXT("-n"), TEXT("2"), TEXT("127.0.0.1") })));
	}
	const double IssueSeconds = FPlatformTime::Seconds() - T0;
	TestTrue(FString::Printf(TEXT("issuing 4 jobs returns at once (%.3f s)"), IssueSeconds), IssueSeconds < 0.25);

	int32 MaxRunning = 0;
	bool bAllDone = false;
	while (!bAllDone && FPlatformTime::Seconds() - T0 < 30.0)
	{
		MaxRunning = FMath::Max(MaxRunning, Client->NumRunning());
		bAllDone = true;
		for (const auto& Job : Jobs) bAllDone &= Job->IsDone();
		FPlatformProcess::Sleep(0.01f);
	}
	TestTrue(TEXT("all queued jobs finish"), bAllDone);
	TestTrue(FString::Printf(TEXT("never more than 2 helper processes (%d)"), MaxRunning), MaxRunning <= 2 && MaxRunning >= 1);
	for (const auto& Job : Jobs)
	{
		const FOsdsAtlasResult R = Job->Future.Get();
		ExpectError(*this, TEXT("ping output is not a JSON document"), R.Error, EOsdsAtlasError::MalformedOutput);
	}

	// Cancelling a running job kills the helper promptly.
	TSharedRef<FOsdsAtlasJob, ESPMode::ThreadSafe> Long = Client->Run(Shell(TEXT("ping"), { TEXT("-n"), TEXT("60"), TEXT("127.0.0.1") }, 120.0));
	TSharedRef<FOsdsAtlasJob, ESPMode::ThreadSafe> Queued = Client->Run(Shell(TEXT("ping"), { TEXT("-n"), TEXT("60"), TEXT("127.0.0.1") }, 120.0));
	TSharedRef<FOsdsAtlasJob, ESPMode::ThreadSafe> Waiting = Client->Run(Shell(TEXT("ping"), { TEXT("-n"), TEXT("60"), TEXT("127.0.0.1") }, 120.0));
	FPlatformProcess::Sleep(0.3f);
	const double C0 = FPlatformTime::Seconds();
	Client->CancelAll();
	const FOsdsAtlasResult A = Long->Future.Get();
	const FOsdsAtlasResult B = Queued->Future.Get();
	const FOsdsAtlasResult C = Waiting->Future.Get();
	const double CancelSeconds = FPlatformTime::Seconds() - C0;
	ExpectError(*this, TEXT("running job cancelled"), A.Error, EOsdsAtlasError::Cancelled);
	ExpectError(*this, TEXT("second running job cancelled"), B.Error, EOsdsAtlasError::Cancelled);
	ExpectError(*this, TEXT("queued job cancelled"), C.Error, EOsdsAtlasError::Cancelled);
	TestTrue(FString::Printf(TEXT("cancel is prompt (%.2f s)"), CancelSeconds), CancelSeconds < 5.0);
	return true;
}

#endif
