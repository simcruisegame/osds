// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/OsdsRoute.h"
#include "Subsystems/WorldSubsystem.h"
#include "World/OsdsGeo.h"
#include "OsdsRouteSubsystem.generated.h"

/**
 * The GPS of one drive: the active route (FOsdsRoute), the stops the route
 * planner is putting together, and the helper's `route` jobs. Each frame it
 * follows the car; when the car leaves the route (more than 30 m for 3 s) it
 * asks for a new route from the car to the stops that are left.
 */
UCLASS()
class OSDS_API UOsdsRouteSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UOsdsRouteSubsystem* Get(const UWorld* World);

	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	/** The helper's answers come in while a menu pauses the game; the car is followed only while it runs. */
	virtual bool IsTickableWhenPaused() const override { return true; }

	/** Ask for a route from the car through these stops (the car's place is added first). */
	void StartRoute(const TArray<FOsdsRouteStop>& Stops);
	/** A route from Start through the stops, once the drive at Start is ready (famous roads: travel, then route). */
	void StartRouteAfterTravel(const TArray<FOsdsRouteStop>& Stops);
	void ClearRoute();

	bool HasRoute() const { return Route.IsActive(); }
	bool IsBusy() const { return Job.IsValid() || bPendingAfterTravel; }
	const FOsdsRoute& GetRoute() const { return Route; }
	/** "Finding the route", the helper's refusal, or empty. */
	const FString& GetStatus() const { return Status; }
	/** The frame the route was projected with (world metres = world cm / 100). */
	const FOsdsWorldFrame& GetFrame() const { return Frame; }
	/** The car's place in route metres (x east, y south), false without a car. */
	bool GetCarM(FVector2D& OutM, double* OutYawDeg = nullptr) const;
	/** Seconds the car has been on the route; the HUD fades its parts in over the first second. */
	double GetRouteAge() const { return RouteAge; }

	/** A route from the car through these stops that is only shown (the planner's list and map). */
	void RequestPreview(const TArray<FOsdsRouteStop>& Stops);
	const FOsdsRoute& GetPreview() const { return Preview; }
	bool IsPreviewBusy() const { return PreviewJob.IsValid(); }
	/** Goes up by one each time a new preview arrives. */
	int32 GetPreviewSerial() const { return PreviewSerial; }

	/** The route planner's stops, kept while the menu is closed. */
	TArray<FOsdsRouteStop> DraftStops;

	/** Use a ready answer instead of the helper (tests): the route is parsed and projected as if the helper sent it. */
	bool AcceptHelperAnswer(const FString& Raw, const TArray<FOsdsRouteStop>& StopsWithStart);

private:
	struct FJob;
	void Launch(const TArray<FOsdsRouteStop>& StopsWithStart, bool bReroute);
	bool StartStops(const TArray<FOsdsRouteStop>& Stops, TArray<FOsdsRouteStop>& OutWithStart);
	TSharedPtr<FJob, ESPMode::ThreadSafe> RunJob(const TArray<FOsdsRouteStop>& StopsWithStart);
	void CollectJob();
	void ProjectRoute();
	bool CurrentFrame(FOsdsWorldFrame& Out) const;

	FOsdsRoute Route;
	FOsdsWorldFrame Frame;
	TSharedPtr<FJob, ESPMode::ThreadSafe> Job;
	TSharedPtr<FJob, ESPMode::ThreadSafe> PreviewJob;
	FOsdsRoute Preview;
	int32 PreviewSerial = 0;
	FString Status;
	double StatusTime = 0.0;
	double RouteAge = 0.0;
	double RerouteBlockedUntil = 0.0;
	bool bPendingAfterTravel = false;
	TArray<FOsdsRouteStop> PendingStops;
	double PendingSince = 0.0;
};
