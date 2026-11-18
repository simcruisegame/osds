// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "OsdsCookList.generated.h"

/**
 * The CARLA models the game places (OsdsCarlaCatalog and the signal poles), saved as hard
 * references by the OsdsBuildWorldMaterials commandlet into /Game/World/Materials (always
 * cooked): the package then carries exactly those models and their materials and textures, not
 * the whole CARLA folders they sit in.
 */
UCLASS()
class OSDS_API UOsdsCookList : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere)
	TArray<TObjectPtr<UObject>> Assets;
};
