// Copyright OSDS contributors. All Rights Reserved.

#include "Gameplay/OsdsFrameGrab.h"

#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "FrameGrabber.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SViewport.h"

FIntPoint OsdsFrameGrab::FitSize(FIntPoint Native, int32 MaxWidth)
{
	Native.X = FMath::Max(Native.X, 2);
	Native.Y = FMath::Max(Native.Y, 2);
	FIntPoint Out = Native;
	if (MaxWidth > 0 && Native.X > MaxWidth)
	{
		Out.X = MaxWidth;
		Out.Y = FMath::RoundToInt(static_cast<double>(Native.Y) * MaxWidth / Native.X);
	}
	Out.X = FMath::Max(2, Out.X & ~1);
	Out.Y = FMath::Max(2, Out.Y & ~1);
	return Out;
}

bool FOsdsGrabGate::CanRequest(double NowS)
{
	if (bInFlight && NowS - RequestedAtS > TimeoutS)
	{
		bInFlight = false;
		++LostCount;
	}
	return !bInFlight;
}

void FOsdsGrabGate::Requested(double NowS)
{
	bInFlight = true;
	RequestedAtS = NowS;
}

void FOsdsGrabGate::Delivered()
{
	bInFlight = false;
}

namespace
{
	/** The note travels with the frame through the render thread and back. */
	struct FNotePayload : IFramePayload
	{
		FOsdsGrabNote Note;
	};
}

FOsdsFrameGrab::FOsdsFrameGrab() = default;

FOsdsFrameGrab::~FOsdsFrameGrab()
{
	Stop();
}

bool FOsdsFrameGrab::Start(UWorld* World, int32 MaxWidth)
{
	UGameViewportClient* Client = World ? World->GetGameViewport() : nullptr;
	const TSharedPtr<SViewport> Widget = Client ? Client->GetGameViewportWidget() : nullptr;
	const TSharedPtr<FSceneViewport> Viewport = Widget.IsValid() ? StaticCastSharedPtr<FSceneViewport>(Widget->GetViewportInterface().Pin()) : nullptr;
	if (!Viewport.IsValid() || !FSlateApplication::IsInitialized())
	{
		return false;
	}
	Size = OsdsFrameGrab::FitSize(Viewport->GetSize(), MaxWidth);
	Grabber = MakeUnique<FFrameGrabber>(Viewport.ToSharedRef(), Size, PF_B8G8R8A8, 3);
	Grabber->StartCapturingFrames();
	return true;
}

void FOsdsFrameGrab::Request(const FOsdsGrabNote& Note)
{
	if (Grabber.IsValid() && Grabber->IsCapturingFrames())
	{
		TSharedRef<FNotePayload, ESPMode::ThreadSafe> Payload = MakeShared<FNotePayload, ESPMode::ThreadSafe>();
		Payload->Note = Note;
		Grabber->CaptureThisFrame(Payload);
	}
}

bool FOsdsFrameGrab::Poll(FOsdsGrabbedFrame& Out)
{
	if (!Grabber.IsValid() || !Grabber->HasOutstandingFrames())
	{
		return false;
	}
	TArray<FCapturedFrameData> Frames = Grabber->GetCapturedFrames();
	if (Frames.IsEmpty())
	{
		return false;
	}
	// Frames are one at a time here (FOsdsGrabGate); keep the newest if the engine hands back more.
	FCapturedFrameData& Frame = Frames.Last();
	if (Frame.ColorBuffer.Num() < Frame.BufferSize.X * Frame.BufferSize.Y)
	{
		return false;
	}
	Out.Size = Frame.BufferSize;
	Out.Pixels = MoveTemp(Frame.ColorBuffer);
	if (const FNotePayload* Payload = Frame.GetPayload<FNotePayload>())
	{
		Out.Note = Payload->Note;
	}
	return true;
}

void FOsdsFrameGrab::Stop()
{
	if (Grabber.IsValid())
	{
		Grabber->StopCapturingFrames();
		Grabber->Shutdown();
		Grabber.Reset();
	}
}
