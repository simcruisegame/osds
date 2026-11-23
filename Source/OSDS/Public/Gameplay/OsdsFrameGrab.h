// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UWorld;
class FFrameGrabber;

/**
 * Preflight's picture of the drive (issue #69). FScreenshotRequest reads the whole back buffer on the
 * game thread and waits for the GPU (about 80 ms a frame at native ultrawide, so a watched run crawled).
 * This reads the back buffer through the engine's FFrameGrabber instead: the GPU scales it to the size
 * asked for into a staging surface, and the game thread only collects the finished pixels.
 */
namespace OsdsFrameGrab
{
	/** The widest picture Preflight keeps by default; 1080p is untouched, an ultrawide is scaled down to this. */
	constexpr int32 DefaultMaxWidth = 1920;

	/**
	 * The size to read back: Native scaled down so it is at most MaxWidth wide, aspect kept, both sides even
	 * (video encoders need that), never larger than Native. MaxWidth <= 0 means native.
	 */
	OSDS_API FIntPoint FitSize(FIntPoint Native, int32 MaxWidth);
}

/**
 * One frame in flight at a time. A request that never comes back (a minimised window draws nothing) is given
 * up after TimeoutS so the run is not left waiting; the count of those is kept for the log.
 */
struct OSDS_API FOsdsGrabGate
{
	static constexpr double TimeoutS = 2.0;

	/** True when a new request may be made now (nothing in flight, or the one in flight timed out). */
	bool CanRequest(double NowS);
	void Requested(double NowS);
	void Delivered();
	int32 Lost() const { return LostCount; }
	bool InFlight() const { return bInFlight; }

private:
	bool bInFlight = false;
	double RequestedAtS = 0.0;
	int32 LostCount = 0;
};

/** What the game knew when the frame was asked for, handed back with the pixels. */
struct FOsdsGrabNote
{
	TArray<FVector2D> Sky;
	FRotator Rotation = FRotator::ZeroRotator;
};

/** A finished frame: BGRA pixels at Size, and the note made when it was requested. */
struct FOsdsGrabbedFrame
{
	FIntPoint Size = FIntPoint::ZeroValue;
	TArray<FColor> Pixels;
	FOsdsGrabNote Note;
};

/** The back buffer of the game window, read back without a GPU stall on the game thread. */
class FOsdsFrameGrab
{
public:
	FOsdsFrameGrab();
	~FOsdsFrameGrab();

	/** Starts on the world's game viewport at most MaxWidth wide (<= 0 native). False when there is no window to read. */
	bool Start(UWorld* World, int32 MaxWidth);
	/** Asks for the frame being drawn now. */
	void Request(const FOsdsGrabNote& Note);
	/** Takes one finished frame, if any. Call every tick. */
	bool Poll(FOsdsGrabbedFrame& Out);
	/** Waits for the frames in flight and stops. */
	void Stop();
	FIntPoint GetSize() const { return Size; }

private:
	TUniquePtr<FFrameGrabber> Grabber;
	FIntPoint Size = FIntPoint::ZeroValue;
};
