// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OsdsBridges.generated.h"

class AOsdsWorldStreamer;
class UMaterialInterface;
class UProceduralMeshComponent;

/**
 * The structure of the mapped bridges (godot-final:game/BridgeDeck.gd): under each
 * bridge span of a road ribbon a concrete deck 1.2 m deep with its fascia, a solid
 * concrete parapet 0.85 m high on each side (the US 32-inch F-shape barrier height),
 * and a pier every 35 m down to the ground where the deck stands more than 4 m above
 * it. The parapets are solid; the deck's top is the carriageway itself.
 * -OsdsNoBridgeDecks turns them off.
 */
UCLASS(NotPlaceable, Transient)
class OSDS_API AOsdsBridges : public AActor
{
	GENERATED_BODY()

public:
	AOsdsBridges();
	virtual void Tick(float DeltaSeconds) override;

	TWeakObjectPtr<AOsdsWorldStreamer> Streamer;

	static UMaterial* BuildMaterial(UObject* Outer, FName Name, EObjectFlags Flags);
	static FString MaterialPath();

private:
	void BuildTile(const FIntPoint& Id);

	UPROPERTY(Transient)
	TMap<FIntPoint, TObjectPtr<UProceduralMeshComponent>> Built;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> Material;

	FString SeenTitle;
};
