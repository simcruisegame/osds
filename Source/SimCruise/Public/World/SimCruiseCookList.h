// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SimCruiseCookList.generated.h"

/**
 * The CARLA models the game places (SimCruiseCarlaCatalog and the signal poles), saved as hard
 * references by the SimCruiseBuildWorldMaterials commandlet into /Game/World/Materials (always
 * cooked): the package then carries exactly those models and their materials and textures, not
 * the whole CARLA folders they sit in.
 */
UCLASS()
class SIMCRUISE_API USimCruiseCookList : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere)
	TArray<TObjectPtr<UObject>> Assets;
};
