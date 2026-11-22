// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Dom/JsonValue.h"
#include "HAL/CriticalSection.h"
#include "Templates/SharedPointer.h"
#include <atomic>

/**
 * Client for the atlas helper (helper/atlas.mjs, shipped as tools/atlas.exe).
 *
 * The helper stays the only network-facing process (docs/unreal-architecture.md):
 * each request launches one helper job `<command> <cache dir> <args...>` and
 * reads one JSON document from its stdout. Jobs run on their own worker
 * threads and never block the game thread; at most MaxConcurrent run at once
 * (the Godot client ran one tile job at a time plus a few side jobs) and the
 * rest wait in a queue. Every job has a timeout and can be cancelled; a
 * timeout or cancel kills the helper's process tree. Exit codes, empty and
 * malformed output are typed errors, never a crash.
 */

enum class EOsdsAtlasError : uint8
{
	None,
	NotConfigured,   // no helper executable found
	LaunchFailed,
	Timeout,
	Cancelled,
	ExitCode,        // the helper reported a failure (stderr carries its message)
	EmptyOutput,
	MalformedOutput, // stdout was not one JSON document
};

OSDS_API const TCHAR* LexToString(EOsdsAtlasError Error);

struct FOsdsAtlasRequest
{
	FString Command;            // "tile", "search", "aerial", ...
	TArray<FString> Args;       // after the cache directory
	double TimeoutSeconds = 60.0;
};

struct FOsdsAtlasResult
{
	EOsdsAtlasError Error = EOsdsAtlasError::None;
	int32 ExitCode = 0;
	FString Stdout;
	FString Stderr;
	TSharedPtr<FJsonValue> Json;
	double Seconds = 0.0;

	bool IsOk() const { return Error == EOsdsAtlasError::None && Json.IsValid(); }
	FString Describe() const;
};

/** How to launch the helper. */
struct FOsdsAtlasConfig
{
	/** Executable to run (tools/atlas.exe, or bun). Empty: not configured. */
	FString Executable;
	/** Arguments placed before the command (for bun: the path of helper/atlas.mjs). */
	TArray<FString> PrefixArgs;
	/** The helper's bounded source cache (it evicts at 96 MB itself). */
	FString CacheDir;
	FString WorkingDirectory;
	int32 MaxConcurrent = 2;

	bool IsConfigured() const { return !Executable.IsEmpty(); }

	/**
	 * Resolution order: OSDS_MAP_HELPER (as in the Godot client), then
	 * <project>/tools/atlas(.exe), then atlas(.exe) beside the game binary, then
	 * bun (~/.bun/bin) running helper/atlas.mjs from the repository.
	 * The cache is <user settings dir>/OSDS/map-cache unless OSDS_MAP_CACHE is set.
	 */
	static FOsdsAtlasConfig ResolveDefault();
};

/** Handle to an issued request; Cancel() is safe from any thread. */
class OSDS_API FOsdsAtlasJob : public TSharedFromThis<FOsdsAtlasJob, ESPMode::ThreadSafe>
{
public:
	void Cancel() { bCancelled = true; }
	bool IsCancelled() const { return bCancelled; }
	bool IsDone() const { return bDone; }
	uint64 Serial() const { return JobSerial; }
	TFuture<FOsdsAtlasResult> Future;

private:
	friend class FOsdsAtlasClient;
	FOsdsAtlasRequest Request;
	TPromise<FOsdsAtlasResult> Promise;
	std::atomic<bool> bCancelled{ false };
	std::atomic<bool> bDone{ false };
	uint64 JobSerial = 0;
};

class OSDS_API FOsdsAtlasClient : public TSharedFromThis<FOsdsAtlasClient, ESPMode::ThreadSafe>
{
public:
	explicit FOsdsAtlasClient(const FOsdsAtlasConfig& InConfig);
	~FOsdsAtlasClient();

	const FOsdsAtlasConfig& GetConfig() const { return Config; }

	/** Queue a request. The future completes on a worker thread. */
	TSharedRef<FOsdsAtlasJob, ESPMode::ThreadSafe> Run(const FOsdsAtlasRequest& Request);

	/** Convenience: the helper's `tile <x> <y>` document for a zoom-16 tile. */
	TSharedRef<FOsdsAtlasJob, ESPMode::ThreadSafe> RequestTile(const FIntPoint& Tile, double TimeoutSeconds = 90.0);

	/** Cancel every queued and running job (shutdown, a new search). */
	void CancelAll();

	int32 NumRunning() const;
	/** Change how many helper processes may run at once (1-8); queued jobs start at once if there is room. */
	void SetMaxConcurrent(int32 MaxConcurrent);
	int32 NumQueued() const;

	/** Helper processes run, and network requests they report, since the game started. */
	static int64 HelperRuns() { return HelperRunCounter.load(); }
	static int64 HelperRequests() { return HelperRequestCounter.load(); }
	/** Run one helper process synchronously on the calling thread (worker threads and tests only). */
	static FOsdsAtlasResult RunBlocking(const FOsdsAtlasConfig& Config, const FOsdsAtlasRequest& Request, const std::atomic<bool>* Cancel);

private:
	static std::atomic<int64> HelperRunCounter;
	static std::atomic<int64> HelperRequestCounter;
	void StartNextLocked();
	void Finish(const TSharedRef<FOsdsAtlasJob, ESPMode::ThreadSafe>& Job, FOsdsAtlasResult&& Result);

	FOsdsAtlasConfig Config;
	mutable FCriticalSection Lock;
	TArray<TSharedRef<FOsdsAtlasJob, ESPMode::ThreadSafe>> Queue;
	TArray<TSharedRef<FOsdsAtlasJob, ESPMode::ThreadSafe>> Running;
	uint64 NextSerial = 1;
	std::atomic<bool> bShuttingDown{ false };
};
