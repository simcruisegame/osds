// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vehicle/CarDefinition.h"

/**
 * The car picker (ADR 0001): only Drivable cars, in picker order, and which car a drive starts in.
 * Pure functions over Car files so they are testable without a game instance.
 */
namespace OsdsCarPicker
{
	/** The ids of the Drivable Car files, sorted by picker_order (ties by id). Traffic cars are left out. */
	OSDS_API TArray<FString> DrivableIdsInPickerOrder(const TArray<const FOsdsCarDefinition*>& Cars);

	/**
	 * The car a drive starts in: the -OsdsCar= override when given (any Car file, for tests and Benches);
	 * otherwise the remembered car when it is in the Drivable list; otherwise DefaultId when it is Drivable
	 * (or the list is empty); otherwise the first Drivable car. The placeholder car never lands here.
	 */
	OSDS_API FString ChooseStartCar(const FString& Remembered, const TArray<FString>& Drivable,
		const FString& Override, const FString& DefaultId);
}
