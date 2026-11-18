// Copyright OSDS contributors. All Rights Reserved.

#include "OsdsGameInstance.h"

#include "Vehicle/CarDefinitionLoader.h"

void UOsdsGameInstance::Init()
{
	Super::Init();
	CarDefinitions.Reset();

	const TArray<FString> CarIds = FOsdsCarDefinitionLoader::FindAvailableIds();
	for (const FString& CarId : CarIds)
	{
		FOsdsCarDefinition Car;
		FString Error;
		if (!FOsdsCarDefinitionLoader::LoadById(CarId, Car, Error))
		{
			UE_LOG(LogTemp, Error, TEXT("OSDS rejected car definition: %s"), *Error);
			continue;
		}

		UE_LOG(LogTemp, Display, TEXT("Loaded %s (%s): %.1f kg, %.3f m wheelbase, %d gears; estimates: %s"),
			*Car.Name, *Car.Id, Car.MassKg, Car.WheelbaseM, Car.ForwardGearRatios.Num(), *Car.SourcesEstimates);
		CarDefinitions.Add(Car.Id, MoveTemp(Car));
	}

	if (CarDefinitions.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("OSDS found no valid car definitions under data/cars."));
	}
}

TArray<FString> UOsdsGameInstance::GetAvailableCarIds() const
{
	TArray<FString> Ids;
	CarDefinitions.GetKeys(Ids);
	Ids.Sort();
	return Ids;
}

bool UOsdsGameInstance::HasCarDefinition(const FString& CarId) const
{
	return CarDefinitions.Contains(CarId);
}

const FOsdsCarDefinition* UOsdsGameInstance::FindCarDefinition(const FString& CarId) const
{
	return CarDefinitions.Find(CarId);
}

bool UOsdsGameInstance::ReloadCarDefinition(const FString& CarId, FString& OutError)
{
	FOsdsCarDefinition Car;
	if (!FOsdsCarDefinitionLoader::LoadById(CarId, Car, OutError))
	{
		UE_LOG(LogTemp, Warning, TEXT("OSDS kept the last good %s: %s"), *CarId, *OutError);
		return false;
	}
	CarDefinitions.Add(Car.Id, MoveTemp(Car));
	return true;
}
