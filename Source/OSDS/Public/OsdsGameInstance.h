// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Vehicle/CarDefinition.h"
#include "OsdsGameInstance.generated.h"

/** Loads and owns the canonical, source-annotated car roster for the game. */
UCLASS()
class OSDS_API UOsdsGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;

	UFUNCTION(BlueprintPure, Category = "OSDS|Garage")
	TArray<FString> GetAvailableCarIds() const;

	UFUNCTION(BlueprintPure, Category = "OSDS|Garage")
	bool HasCarDefinition(const FString& CarId) const;

	const FOsdsCarDefinition* FindCarDefinition(const FString& CarId) const;

	/** Read data/cars/<id>.json and its calibration overlay again (hot reload). On an error the roster keeps the last good definition. */
	bool ReloadCarDefinition(const FString& CarId, FString& OutError);

private:
	TMap<FString, FOsdsCarDefinition> CarDefinitions;
};
