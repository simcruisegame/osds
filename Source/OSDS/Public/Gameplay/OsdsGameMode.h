// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Gameplay/OsdsPlaces.h"
#include "Math/RandomStream.h"
#include "OsdsGameMode.generated.h"

class APlayerController;
class AOsdsWorldStreamer;

/**
 * The drivable loop. By default it is a drive on the real place: it starts
 * the world stream at the bundled Miami Beach start (Ocean Drive, offline),
 * waits until the tiles around the start are built and their collision is in
 * the physics scene, then puts the player's car in the right-hand lane of the
 * start road, facing the direction of travel, with the OSDS controller,
 * camera manager and HUD. Each frame it hands the car's position to the world
 * stream and moves the car with the floating origin.
 *
 * The pawn class comes from config so the vehicle stream can plug its own in
 * without touching the shell:
 *   [/Script/OSDS.OsdsGameMode]
 *   VehiclePawnClass=/Script/OSDS.OsdsVehicle
 * Any pawn that implements IOsdsVehicleControl works; without one the
 * placeholder single-track car is used.
 *
 * Command line:
 *   -OsdsCar=<id>             car file under data/cars (default StartCarId)
 *   -OsdsStart=lat,lon        start elsewhere (needs the atlas helper); nearest road
 *   -OsdsRandomStart          a random start-pool road (the default of a normal drive)
 *   -OsdsBundledStart         the bundled Ocean Drive start (the default of checks and captures)
 *   -OsdsStartSeed=<n>        repeat a random start sequence
 *   -OsdsStartRoad=<name>     the road to start on near the start point
 *   -OsdsNoWorld              drive the loaded map instead (its player start)
 *   -OsdsDriveCapture         documentation captures (see AOsdsDriveCapture)
 */
UCLASS(config = Game)
class OSDS_API AOsdsGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AOsdsGameMode();

	virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Put the player's car back upright at rest: on the nearest road when the
	 *  real place is streamed, otherwise on the ground below it. */
	void RecoverVehicle(APlayerController* Player);

	/** True while the real place streams around the car. */
	bool IsWorldDrive() const { return bWorldDrive; }
	AOsdsWorldStreamer* GetStreamer() const;
	/** The car is on the road and the player drives it. */
	bool IsDriveReady() const { return bCarPlaced; }

	/**
	 * Go to a place (place search, presets, Surprise me, the road atlas): the
	 * world restarts there, the car waits until the road under it is built,
	 * then starts on the place's street (or the nearest road), facing its
	 * bearing on a two-way road. A place that does not load (no network, no
	 * road) falls back to the bundled Miami Beach streets, which work offline.
	 */
	void TravelTo(const FOsdsPlace& Place);

	/** A random start-pool road other than this place, with up to Retries more behind it. Returns false without a pool. */
	bool SurpriseMe();

	/** Where this drive is (the place last chosen). */
	const FOsdsPlace& GetPlace() const { return CurrentPlace; }
	/** The car was placed by the nearest-road rule because the start pick found no start (a start fault for the release judge). */
	bool StartedByNearestRoad() const { return bStartByNearestRoad; }

	/** Latitude and longitude of a world position (false before the world starts). */
	bool WorldToLonLat(const FVector& WorldCm, double& OutLon, double& OutLat) const;

	/** The next drivable car, at the same spot and heading (the atlas car button). */
	void CycleCar(APlayerController* Player);
	/** The player's car again, at rest where it stands, from the current car files (hot reload, input replay);
	 *  a NewCarId drives that car instead. */
	void RespawnCar(APlayerController* Player, const FString& NewCarId = FString());
	const FString& GetCarId() const { return CarId; }

	UPROPERTY(config, EditAnywhere, Category = "OSDS")
	FSoftClassPath VehiclePawnClass;

	/** The car a drive starts in (data/cars/<id>.json). */
	UPROPERTY(config, EditAnywhere, Category = "OSDS")
	FString StartCarId = TEXT("nb1999");

	/** Road the bundled start puts the car on (Godot StartPool FALLBACK: Ocean Drive). */
	UPROPERTY(config, EditAnywhere, Category = "OSDS")
	FString StartRoad = TEXT("Ocean Drive");

	/** A normal drive starts on a random start-pool road (Godot's launch); checks and captures start on Ocean Drive. */
	UPROPERTY(config, EditAnywhere, Category = "OSDS")
	bool bRandomStart = true;

	/** Seconds to wait for the start tiles before placing the car on whatever is there. */
	UPROPERTY(config, EditAnywhere, Category = "OSDS")
	float StartTimeoutSeconds = 180.f;

private:
	void BeginWorldDrive();
	bool TryPlacePendingPlayers();
	APawn* SpawnVehicleAt(AController* Player, const FVector& GroundCm, const FRotator& Heading);
	void HandleRebased(const FVector& OffsetCm);
	void UpdateLoadingMessage();
	/** The place did not load: the next place of the chain, else the bundled place. */
	void TravelFailed(const FString& Reason);
	/** Hold the players' cars (destroyed) until the next place is built. */
	void HoldPlayers();
	/** Point the world at a place (restarting it when asked) and wait for its tiles; keeps any start chain. */
	void BeginTravel(const FOsdsPlace& Place, bool bRestartWorld);

	/**
	 * A ground trace at a start: down onto the road at its own height (within 8 cm) under the
	 * car's middle and corners (FOsdsStartPose::CheckCm): no kerb, bank, building or hole.
	 */
	bool GroundTraceOnRoad(const TArray<FVector>& PointsCm, FString& OutWhy) const;

	bool bWorldDrive = false;
	bool bCarPlaced = false;
	bool bStartByNearestRoad = false;
	/** The player's car model was built while the start tiles loaded (once per place). */
	bool bStartCarBuilt = false;
	/** Traffic and parked-car models built so far while the start tiles load. */
	int32 PrebuiltModels = 0;
	FVector StartCm = FVector::ZeroVector;
	FString StartRoadName;
	FString CarId;
	double WaitStartedSeconds = 0.0;
	double LastMessageSeconds = -100.0;
	TArray<TWeakObjectPtr<AController>> PendingPlayers;
	TWeakObjectPtr<AOsdsWorldStreamer> Streamer;

	FOsdsPlace CurrentPlace;
	/** Places to try, in order, when the current one does not load (StartPool RETRIES). */
	TArray<FOsdsPlace> StartChain;
	/** The game's first place: past the chain it drives the bundled place rather than give up. */
	bool bStartLaunch = false;
	FString StartNote;
	TArray<FOsdsPlace> StartPool;
	FRandomStream StartRng;
};
