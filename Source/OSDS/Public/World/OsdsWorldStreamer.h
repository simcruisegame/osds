// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "GameFramework/Actor.h"
#include "World/OsdsGeo.h"
#include "World/OsdsTileManager.h"
#include "OsdsWorldStreamer.generated.h"

class AOsdsWorldTile;
class USpotLightComponent;
class FOsdsAtlasClient;
class UOsdsWorldResources;
struct FOsdsTileBuild;
struct FOsdsStartPose;

DECLARE_MULTICAST_DELEGATE_OneParam(FOsdsWorldRebased, const FVector& /*OffsetCm*/);

/**
 * Streams the real place around the car: a bounded ring of zoom-16 tiles in
 * the drive's local frame, each read (bundled offline data first, then the
 * atlas helper) and built off the game thread, installed on it one per
 * frame. A floating origin keeps the car near (0,0,0): past
 * RebaseDistanceM the origin moves to the car's tile, every tile actor moves
 * by whole tiles, and OnRebased tells the car, cameras and traffic to move
 * by the same offset.
 */
UCLASS()
class OSDS_API AOsdsWorldStreamer : public AActor
{
	GENERATED_BODY()

public:
	AOsdsWorldStreamer();

	/** Begin a drive at a place: a new frame and a new generation (older jobs are rejected). */
	void StartAt(double LonDeg, double LatDeg, const FString& Title);

	/** Where the car is (world cm) and how fast it moves (cm/s); without it the player camera is used. */
	void SetFocus(const FVector& WorldCm, const FVector& VelocityCmPerSecond);

	const FOsdsWorldFrame& GetFrame() const { return Frame; }
	const FString& GetTitle() const { return Title; }
	AOsdsWorldTile* GetTile(const FIntPoint& Tile) const;
	int32 NumTiles() const { return Tiles.Num(); }
	bool IsBusy() const;

	/** All tiles within Radius of the tile holding WorldCm are installed and their collision is in the physics scene. */
	bool AreTilesReadyAround(const FVector& WorldCm, int32 Radius = 1) const;

	/** Drivable surface height (world cm) under a world position; false where no tile is loaded. */
	bool SurfaceHeightAt(const FVector& WorldCm, double& OutZCm) const;

	/**
	 * A pose on a named road near a point: in the travel lane on the right,
	 * facing the direction of travel. Name match is case-insensitive; an empty
	 * name takes the nearest public road (not a service way).
	 */
	bool FindRoadPose(const FString& RoadName, const FVector& NearWorldCm, double SearchRadiusM, FVector& OutLocationCm, FRotator& OutRotation) const;

	/**
	 * As above, but on a two-way road the car faces the given yaw (degrees, world frame)
	 * when that way has room; a one-way road keeps its direction of travel (Drive.gd: "requested
	 * bearing honoured on two-way roads, one-way direction respected").
	 */
	bool FindRoadPoseFacing(const FString& RoadName, const FVector& NearWorldCm, double SearchRadiusM, double PreferredYawDeg, FVector& OutLocationCm, FRotator& OutRotation) const;
	/** Start places by OsdsStartPick's rules on the built tiles, best first. */
	TArray<FOsdsStartPose> FindStartCandidates(const FString& RoadName, const FVector& NearWorldCm, double SearchRadiusM, double PreferredYawDeg, FString* OutWhyNone = nullptr) const;

	/** Tiles of this drive (since StartAt) that failed to load or build: no network, no helper, a bad document. */
	int32 NumTilesFailed() const { return TilesFailed; }

	/** Tiles being fetched or built now. */
	int32 NumTilesInFlight() const { return Manager.NumInFlight(); }

	/**
	 * Before the car is placed nothing else needs the CPU: up to StartConcurrentBuilds tiles
	 * fetch and build at once (the start tile and its corner neighbours; edge neighbours still
	 * never build together), so a stall on the game thread then (the car's model) does not
	 * hold the tiles back. Off: MaxConcurrentBuilds again.
	 */
	/** On while a start waits for its tiles. Off: the start pick is done, and the tiles free the road faces it read. */
	void SetStartBoost(bool bOn);
	/** A start waits (any placement, not only the first): tiles installed from now keep the road faces for the pick. */
	void HoldRoadTops() { bStartPending = true; }
	static constexpr int32 StartConcurrentBuilds = 5;

	FOsdsWorldRebased OnRebased;

	/** One streaming step (called from Tick; tests drive it directly). */
	void TickStreaming(float DeltaSeconds);

	UPROPERTY(EditAnywhere, Category = "OSDS|World")
	int32 RingRadius = 1;

	UPROPERTY(EditAnywhere, Category = "OSDS|World")
	int32 MaxConcurrentBuilds = 2;

	UPROPERTY(EditAnywhere, Category = "OSDS|World")
	double RebaseDistanceM = 2000.0;

	/** Ask the atlas helper for tiles that are not bundled (needs the helper and, for new places, the network). */
	UPROPERTY(EditAnywhere, Category = "OSDS|World")
	bool bUseHelper = true;

	UPROPERTY(VisibleAnywhere, Category = "OSDS|World")
	int32 TilesInstalled = 0;

	UPROPERTY(VisibleAnywhere, Category = "OSDS|World")
	int32 ResultsRejected = 0;

	UPROPERTY(VisibleAnywhere, Category = "OSDS|World")
	int32 TilesFailed = 0;
	/** The Tile the World cache last protected the Ring of (so it is set when the car changes Tile only). */
	FIntPoint ProtectedCacheTile = FIntPoint(MIN_int32, MIN_int32);

	/** Seconds a tile takes from request to result (a running mean; the look-ahead reads it). */
	double LoadSecondsMean = 6.0;
	/** How far ahead the streamer asks for tiles: speed x (2 x LoadSecondsMean + LookAheadMarginSeconds). */
	static constexpr double LookAheadMarginSeconds = 4.0;

	UPROPERTY(Transient)
	TObjectPtr<UOsdsWorldResources> Resources;

	/** Street lights that really light the road at night: the nearest lamp heads get one of these. */
	UPROPERTY(EditAnywhere, Category = "OSDS|World")
	int32 StreetLightPoolSize = 24;

	/** How much the night lighting is on (0 day .. 1 night), from the sun's elevation. */
	float GetLightsOn() const { return LightsOn; }

	/** The moving traffic round the player (drawn runs only). */
	UPROPERTY(Transient)
	TObjectPtr<class AOsdsTraffic> Traffic;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FResult
	{
		FOsdsTileJobTicket Ticket;
		TSharedPtr<FOsdsTileBuild> Build;
		FString Error;
		FString AerialKey;
		TArray<FColor> AerialPixels;
		/** Its mip chain, built on the worker so the game thread only copies it in. */
		TArray<TArray<FColor>> AerialMips;
		int32 AerialWidth = 0;
		int32 AerialHeight = 0;
		/** When the worker finished it (FPlatformTime::Seconds), for the load-time mean. */
		double FinishedAt = 0.0;
	};
	struct FInbox
	{
		TQueue<TSharedPtr<FResult, ESPMode::ThreadSafe>, EQueueMode::Mpsc> Queue;
		std::atomic<bool> bClosed{ false };
		// Aerial photographs being fetched by a worker: one helper run per photograph,
		// never two processes writing the same cache file.
		FCriticalSection AerialLock;
		TSet<FString> AerialsFetching;
	};

	void Launch(const FOsdsTileJobTicket& Ticket);
	/** When each job was launched (game thread), by ticket serial. */
	TMap<uint64, double> LaunchedAt;

	/** Flat collision under every tile in range that has none yet (OsdsFallbackGround). */
	void UpdateFallbackGround(const FVector& FocusCm);
	UPROPERTY(Transient)
	TMap<FIntPoint, TObjectPtr<class UBoxComponent>> FallbackGround;
	/** -OsdsFailTiles=x_y,x_y: these tiles' loads fail (the fallback-ground test). */
	TSet<FIntPoint> ForcedFailures;
public:
	int32 NumFallbackGround() const { return FallbackGround.Num(); }
	bool HasFallbackGround(const FIntPoint& Tile) const { return FallbackGround.Contains(Tile); }
private:
	/**
	 * A far ground under the world, following the camera, just below the lowest built tile: where
	 * the distant tiles are not built yet the eye sees hazy distant land instead of the
	 * atmosphere's planet surface, which under an overcast sky renders near black (2.1 owner
	 * screenshot: a black band at the end of a Miami Beach street).
	 */
	void UpdateHorizonGround(float DeltaSeconds);
	UPROPERTY(Transient)
	TObjectPtr<class UStaticMeshComponent> HorizonGround;
	float HorizonGroundClock = 0.f;
	double HorizonGroundZ = 0.0;
	void Rebase(const FIntPoint& NewOrigin);
	void RemoveTile(const FIntPoint& Tile);
	FVector CurrentFocus(FVector& OutVelocity) const;
	/** Windows, lenses and the street-light pool follow the sun (photocells switch near sunset). */
	void UpdateNightLighting(float DeltaSeconds);

	FOsdsWorldFrame Frame;
	FString Title;
	FOsdsTileManager Manager;
	TSharedPtr<FOsdsAtlasClient, ESPMode::ThreadSafe> Atlas;
	TSharedPtr<FInbox, ESPMode::ThreadSafe> Inbox;
	/** A finished tile held back a frame while its aerial photograph was made. */
	TSharedPtr<FResult, ESPMode::ThreadSafe> Deferred;

	UPROPERTY(Transient)
	TMap<FIntPoint, TObjectPtr<AOsdsWorldTile>> Tiles;

	bool bHasFocus = false;
	// A start is waiting for its tiles (the first from the beginning): installed tiles keep the
	// road faces the start pick reads until SetStartBoost(false).
	bool bStartPending = true;
	FVector FocusCm = FVector::ZeroVector;
	FVector FocusVelocity = FVector::ZeroVector;
	bool bStarted = false;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USpotLightComponent>> StreetLightPool;
	float LightsOn = 0.f;
	float NightLightingClock = 0.f;
};
