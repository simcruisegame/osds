// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Async/Future.h"
#include "HAL/Thread.h"
#include <atomic>
#include "Gameplay/SimCruiseReview.h"
#include "SimCruisePreflight.generated.h"

class APawn;
class APlayerController;
class ASimCruisePlayerController;
class ASimCruiseWorldStreamer;
struct FSimCruiseRibbon;

/** What one pre-flight run measured. */
struct SIMCRUISE_API FSimCruisePreflightStats
{
	// The drive.
	float DriveSeconds = 0.f;
	float DistanceM = 0.f;
	/** The least distance the drive must cover (m): MinDistanceM, less for a slow run (-SimCruisePreflightKmh). */
	float MinDistanceAskedM = 200.f;
	float MeanSpeedMps = 0.f;
	/** Longest time (s) without a road surface under the car. */
	float OffRoadSeconds = 0.f;
	/** The start fell back to the bundled place (the requested place did not load). */
	bool bFellBack = false;
	/** The start pick found no start and the nearest-road rule placed the car ("SimCruise START FALLBACK" in the log). */
	bool bStartByNearestRoad = false;
	/** The start's VIEWCHECK (when it ran). */
	bool bViewCheckRan = false;
	bool bViewCheckPassed = false;
	FString ViewCheckReasons;

	// White sky: of the frame pixels where a view ray hits nothing, the percent that is near white.
	int32 SkySamples = 0;
	float SkyWhitePercent = 0.f;
	float ReferenceEV100 = 0.f;

	// Pop-in: tiles installed, or cars appearing, within the pop-in range in front of the camera.
	int32 TilePopIns = 0;
	int32 CarPopIns = 0;
	/** Frames whose picture changed much more than the drive's usual change, with the camera steady. */
	int32 FlashFrames = 0;
	float MedianFrameDiff = 0.f;

	// Road seams, from the road surface under the path.
	int32 RoadSteps = 0;
	float MaxStepCm = 0.f;
	int32 RoadGaps = 0;
	/** Vertical body jolts, and those near a tile border. */
	int32 Jolts = 0;
	int32 BorderJolts = 0;

	// Traffic.
	int32 StuckTraffic = 0;
	/** Traffic cars that came into our path within one car length ahead while we drove (crossing or from the side). */
	int32 TrafficCutIns = 0;
	/** Traffic cars whose body went into our car's body (drove through it). */
	int32 TrafficOverlaps = 0;
	/** Traffic cars that turned round (more than TurnRoundDeg of heading in TurnRoundWindowS). */
	int32 TrafficTurnRounds = 0;
	/** Traffic cars on a motorway ribbon that crossed our road at our level. */
	int32 TrafficAtGrade = 0;
	// The car's own state (2026-11-14: a 64-to-1 mph crash at Doraville and 27 s at the rev limit
	// passed the judge).
	/** Longest time (s) with the engine above StuckRpmShare of the redline and the car under 1 m/s. */
	float StuckSeconds = 0.f;
	/** Hits: the speed fell by more than HitDropMps within HitWindowS. */
	int32 Hits = 0;
	/** Places where the body's pitch or roll changed by more than DeckDeg within DeckWindowM of road. */
	int32 DeckJumps = 0;
	float MaxDeckDeg = 0.f;
	/** Longest time (s) with two or more wheels off the ground while the car is on a road surface. */
	float WheelsOffSeconds = 0.f;
	/** Longest time (s) the game thread did not tick (the hang watchdog). */
	float LongestHangS = 0.f;

	/** -SimCruisePreflightStopAtJunction=N: the seconds asked for, and the seconds held at the stop line. */
	float JunctionHoldAskedS = 0.f;
	float JunctionHoldS = 0.f;

	int32 Frames = 0;

	// Car control: a spin, a slide backwards, a run-off.
	/** Peak slip angle of the rear axle (deg, above 5 m/s): the drive robot's spin measure. */
	float PeakRearSlipDeg = 0.f;
	float PeakYawDps = 0.f;
	/** Longest time (s) the car slid backwards faster than 2 m/s. */
	float ReversedSeconds = 0.f;
	/** Farthest the car's centre went past the paved edge of the road ribbon (m), and the longest time (s) more than MaxOffRibbonM past it. */
	float MaxOffRibbonM = 0.f;
	float OffRibbonSeconds = 0.f;
	/** -SimCruisePreflightForceSpin: the negative test yanked the stick with the handbrake on. */
	bool bForcedSpin = false;

	// Frame times after the warm-up, without the frames the robot's own screenshots slow down.
	int32 TimedFrames = 0;
	float FrameP95Ms = 0.f;
	/** Frames over 50 ms, and over 100 ms (hitches a player sees). */
	int32 SlowFrames = 0;
	int32 Hitches = 0;
};

/** The pass rules and the measurements that need no world, kept apart for the tests. */
namespace SimCruisePreflightRules
{
	constexpr float MinDistanceM = 200.f;
	/** Off the road surface longer than this is a run-off (a crack under the wheels takes about 0.1 s). */
	constexpr float MaxOffRoadSeconds = 1.f;
	/** A spin: the rear axle slips past 15 deg (the drive robot's SPUN line), or the car slides backwards. */
	constexpr float MaxRearSlipDeg = 15.f;
	constexpr float MaxReversedSeconds = 0.3f;
	/** A run-off: the car's centre more than 2 m past the paved edge for more than 0.5 s. */
	constexpr float MaxOffRibbonM = 2.f;
	constexpr float MaxOffRibbonSeconds = 0.5f;
	/** Slow frames, after FrameWarmupS of driving: the 95th percentile frame time, and frames over 100 ms. */
	constexpr float FrameWarmupS = 5.f;
	constexpr float MaxFrameP95Ms = 50.f;
	constexpr int32 MaxHitches = 5;
	/** Frame-time rules apply only with at least this many timed frames. */
	constexpr int32 MinTimedFrames = 100;
	constexpr float MaxSkyWhitePercent = 25.f;
	/** Luminance (0-255) from which a sky pixel counts as near white. */
	constexpr float WhiteLuma = 245.f;
	constexpr float PopInRangeM = 150.f;
	constexpr int32 MaxCarPopIns = 2;
	constexpr int32 MaxFlashFrames = 2;
	constexpr float StepCm = 5.f;
	constexpr float JoltMps2 = 8.f;

	// Traffic behaviour, measured ten times a second on each traffic car in our car's frame
	// (Along: m ahead of our centre, Side: m to the side, both centre to centre).
	/** A turn round: the heading changes by at least TurnRoundDeg within TurnRoundWindowS. */
	constexpr double TurnRoundDeg = 150.0;
	constexpr double TurnRoundWindowS = 3.0;
	/** One car length (m): the room a cut-in leaves in front of our bumper at most. */
	constexpr double CarLengthM = 4.5;
	/** Half the length and half the width of our car (m), for the bumper gap and the overlap. */
	constexpr double OwnHalfLengthM = 2.2;
	constexpr double OwnHalfWidthM = 0.9;
	/** No cut-in is counted below this speed of our car (m/s): at a stop line the crossing traffic has priority. */
	constexpr double CutInMinSpeedMps = 2.0;

	// The car's own state.
	constexpr double StuckRpmShare = 0.8;
	constexpr double StuckMaxMps = 1.0;
	constexpr float MaxStuckSeconds = 2.f;
	/** A hit: more speed lost in HitWindowS than the hardest braking gives (about 1.2 g). */
	constexpr double HitDropMps = 6.0;
	constexpr double HitWindowS = 0.5;
	/** An uneven deck or road: pitch or roll changes by more than DeckDeg within DeckWindowM. */
	constexpr double DeckDeg = 4.0;
	constexpr double DeckWindowM = 5.0;
	constexpr float MaxWheelsOffSeconds = 1.f;
	/** The hang watchdog: a stack in the log after HangLogS without a tick; a note over that, a FAIL over HangFailS. */
	constexpr double HangLogS = 10.0;
	constexpr float HangFailS = 30.f;
	/** Samples (time s, speed m/s) in time order: the largest speed loss within HitWindowS. */
	SIMCRUISE_API double MaxSpeedDrop(const TArray<FVector2d>& TimeSpeed);
	/** Samples (path m, pitch deg, roll deg) in path order: the largest change of pitch or roll within DeckWindowM. */
	SIMCRUISE_API double MaxAngleChange(const TArray<FVector>& PathPitchRoll);

	/** Samples (time s, yaw deg) in time order: true when the heading turned round within the window. */
	SIMCRUISE_API bool IsTurnRound(const TArray<FVector2d>& TimeYawDeg);
	/**
	 * A cut-in: our car drives (CutInMinSpeedMps or more), the other car is in our lane
	 * (|Side| < 1.8 m) with at most one car length from our front bumper to its rear, and
	 * it came across (heading more than 30 deg from ours) or from the side (last sample
	 * |PrevSide| > 2.5 m).
	 */
	SIMCRUISE_API bool IsCutIn(double AlongM, double SideM, double PrevSideM, double RelHeadingDeg, double OurSpeedMps, double TheirLengthM);
	/** The two bodies overlap (a car drove into or through ours). */
	SIMCRUISE_API bool Overlaps(double AlongM, double SideM, double TheirLengthM);
	/** A motorway car crossing our road (45 to 135 deg) on it (|Side| < 8 m, -10 to 60 m ahead) at our level (|Dz| < 2.5 m). */
	SIMCRUISE_API bool IsAtGradeMotorway(const FString& TheirClass, double AlongM, double SideM, double DzM, double RelHeadingDeg);

	/** Information that is not a FAIL (the traffic counts), comma-separated. */
	SIMCRUISE_API FString Notes(const FSimCruisePreflightStats& Stats);

	/** Empty when the run passes, else the reasons (comma-separated). */
	SIMCRUISE_API FString Judge(const FSimCruisePreflightStats& Stats);

	/** Fills the frame-time fields of Stats (p95, frames over 50 and 100 ms) from the timed frames (ms). */
	SIMCRUISE_API void MeasureFrames(const TArray<float>& TimedMs, FSimCruisePreflightStats& Stats);

	/**
	 * Height steps along a path: samples (distance m, height m) in path order. A step is a
	 * sample further than StepCm from the line through its neighbours, all within MaxGapM.
	 */
	SIMCRUISE_API int32 CountSteps(const TArray<FVector2f>& DistanceHeight, float& OutMaxStepCm, float MaxGapM = 1.f, TArray<int32>* OutIndices = nullptr);

	/**
	 * Frames whose change (the percent of the picture that changed by more than 40 of 255) is above
	 * max(Floor, Factor x the median), skipping those marked turning.
	 */
	SIMCRUISE_API int32 CountFlashes(const TArray<float>& Diffs, const TArray<bool>& Turning, float& OutMedian, float Factor = 3.f, float Floor = 50.f);
}

/**
 * The pre-flight robot (-SimCruisePreflight): it drives the real road for 60 s
 * (-SimCruisePreflightSeconds=N) through the real input stack, keeping to the
 * right lane of the road ribbon under it near the road's speed limit, slowing
 * for curves and for cars ahead. It records the view at 10 frames a second as
 * JPEG frames, and measures white sky, pop-in, road seams and jolts, and stuck
 * traffic. When -SimCruiseViewCheck is on, it drives after the VIEWCHECK line.
 *
 * Output: -SimCruisePreflightOut=DIR (default Saved/Preflight/manual-<time>)
 * holds frames/00000.jpg ... and preflight.json, and the log has one line
 * PREFLIGHT PASS|FAIL road=<-SimCruisePreflightRoad=> car=<id> reasons=<...>.
 * It quits the game when done (-SimCruiseKeepOpen keeps it).
 */
UCLASS()
class SIMCRUISE_API ASimCruisePreflight : public AActor
{
	GENERATED_BODY()

public:
	ASimCruisePreflight();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	enum class EStage : uint8 { WaitReady, Settle, Drive, Done };

	/** A road centreline in world cm, one per tile ribbon. */
	struct FLine
	{
		TArray<FVector2d> P;
		TArray<double> HalfW;
		FString Class;
		int8 Travel = 0;
		/** The tile's ribbon (alive while the tile is; the lines are gathered again each frame). */
		const FSimCruiseRibbon* Ribbon = nullptr;
	};
	/** A place on a line, walking in Dir (+1 along the stations, -1 against). */
	struct FCursor
	{
		int32 Line = INDEX_NONE;
		int32 Seg = 0;
		double T = 0.0;
		int32 Dir = 1;
		bool IsValid() const { return Line != INDEX_NONE; }
	};

	void GatherLines(const ASimCruiseWorldStreamer* Stream, const FVector& CarCm);
	FCursor FindCursor(const FVector2d& CarXY, const FVector2d& Forward) const;
	/** Walk DistanceCm along the road; false at a dead end (the cursor stops at the end). */
	bool Walk(FCursor& Cursor, double DistanceCm) const;
	FVector2d PointAt(const FCursor& Cursor) const;
	FVector2d TangentAt(const FCursor& Cursor) const;

	void DriveTick(ASimCruisePlayerController* Player, float DeltaSeconds);
	void MeasureTick(ASimCruisePlayerController* Player, float DeltaSeconds);
	void RequestFrame(APlayerController* Player);
	void OnFrame(int32 Width, int32 Height, const TArray<FColor>& Pixels);
	void OnRebased(const FVector& OffsetCm);
	void Finish();

	EStage Stage = EStage::WaitReady;
	double StageStarted = 0.0;
	double Started = 0.0;
	float DriveSeconds = 60.f;
	FString OutputDir;
	FString RoadName;
	FString CarId;
	bool bRequestedStart = false;
	double RequestedLat = 0.0, RequestedLon = 0.0;

	TArray<FLine> Lines;
	FCursor Last;
	float Stick = 0.f;
	float SteerIntegral = 0.f;
	double LastYawDeg = 0.0;
	float JoltCooldown = 0.f;

	// Recording.
	float FrameClock = 0.f;
	bool bFramePending = false;
	/** -SimCruisePreflightNoFrames: no screenshots, so the frame times are the game's own. */
	bool bNoFrames = false;
	/** -SimCruisePreflightKmh=: drive at this speed (slower only for curves and cars), not the road's limit. */
	double SpeedOverrideMps = 0.0;
	bool bKeen = false;
	float TelemetryClock = 0.f;
	/** The robot holds the car on the parking brake while it waits (the brake trigger at rest selects reverse). */
	bool bHoldingParkingBrake = false;
	/** The robot's felt yaw rate (low-passed) and its last stick, for a smooth hand. */
	double SmoothedYawRate = 0.0;
	float LastStick = 0.f;
	/** Every drive frame's real time (ms) and the path distance at the long ones. */
	TArray<float> FrameMs;
	/** The frame times the rules judge: after the warm-up, none a screenshot touched. */
	TArray<float> TimedFrameMs;
	/** Frames still to skip after a screenshot request or capture. */
	int32 CaptureSkip = 0;
	/** -SimCruisePreflightForceSpin: the negative test. */
	bool bForceSpin = false;
	float ForceSpinClock = 0.f;
	bool bForceSpinBrake = false;
	float ReverseNow = 0.f;
	/** The car has been above 5 m/s once in this drive. */
	bool bHasDriven = false;
	float OffRibbonNow = 0.f;
	/** The next pose log time (index into the pose times). */
	int32 PoseLogNext = 0;
	FDelegateHandle FrameHandle;
	int32 FrameIndex = 0;
	TArray<FVector2D> PendingSky;
	FRotator PendingRotation = FRotator::ZeroRotator;
	FRotator LastFrameRotation = FRotator::ZeroRotator;
	TArray<float> LastThumb;
	TArray<float> FrameDiffs;
	TArray<bool> FrameTurning;
	int32 SkyWhite = 0;

	// Measurements.
	FSimCruisePreflightStats Stats;
	/** -SimCruiseReview: frames, sidecars and close-ups for a person to look at. */
	FSimCruiseReviewRecorder Review;
	/** Frames still being written as JPEG on worker threads. */
	TArray<TFuture<void>> JpegJobs;
	double SpeedSum = 0.0;
	int32 SpeedSamples = 0;
	FVector LastCarCm = FVector::ZeroVector;
	bool bHasLastCar = false;
	double PathM = 0.0;
	TArray<FVector2f> Surface;
	TArray<uint8> SurfaceIsRoad;
	/** Where each surface sample was (lat, lon), so a step can be found again. */
	TArray<FVector2d> SurfaceLatLon;
	float OffRoadNow = 0.f;
	double LastVz = 0.0;
	bool bHasLastVz = false;
	float PopClock = 0.f;
	TSet<FIntPoint> KnownTiles;
	TMap<TWeakObjectPtr<AActor>, FVector> KnownCars;
	TSet<TWeakObjectPtr<AActor>> StuckCars;

	/** What the traffic checks keep per traffic car between samples. */
	struct FTrafficWatch
	{
		TArray<FVector2d> TimeYaw;
		FVector LastCm = FVector::ZeroVector;
		double PrevSideM = 0.0;
		double LastCutInS = -100.0;
		bool bHasPrev = false;
		bool bTurnRoundCounted = false;
		bool bOverlapCounted = false;
		bool bAtGradeCounted = false;
	};
	TMap<TWeakObjectPtr<AActor>, FTrafficWatch> TrafficWatch;
	void WatchTraffic(const ASimCruiseWorldStreamer* Stream, APawn* Pawn, const FVector& CarCm, const FVector& Forward, double Speed);

	/** Traffic off for the run (the default; -SimCruisePreflightTraffic keeps it). */
	bool bTrafficOff = true;

	/** The audio mixer is recording the drive's sound (review runs). */
	bool bRecordingSound = false;

	/** "frame,drive_s,path_m" per saved video frame (frames.csv). */
	TArray<FString> FrameTimes;

	/** The hang watchdog thread (see StartWatchdog). */
	void StartWatchdog();
	void StopWatchdog();
	TUniquePtr<FThread> Watchdog;
	std::atomic<double> HeartbeatS{0.0};
	std::atomic<double> LongestHangS{0.0};
	std::atomic<bool> bWatchdogStop{false};

	/** The car's own state, measured each frame (see the rules). */
	void WatchCar(ASimCruisePlayerController* Player, APawn* Pawn, float DeltaSeconds, bool bOnRoad);
	TArray<FVector2d> SpeedHistory;
	TArray<FVector> AngleHistory;
	float StuckClock = 0.f;
	float WheelsOffClock = 0.f;
	double LastHitS = -100.0;
	double LastDeckPathM = -100.0;
	float WheelLogClock = 0.f;

	/** -SimCruisePreflightStopAtJunction=N: stop at the first stop line ahead and hold N s, then go. */
	float StopAtJunctionS = 0.f;
	double JunctionHeldS = 0.0;
	bool bJunctionDone = false;
	FString JunctionCrossName;
	/** Metres from the car to the stop line of the first road ahead that this road stops for (within 60 m), or -1. */
	double DistanceToStopLine(const FCursor& Here, FString& OutCrossName) const;
	FDelegateHandle RebaseHandle;
	TWeakObjectPtr<ASimCruiseWorldStreamer> RebaseStream;
};
