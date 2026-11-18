// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vehicle/CarDefinition.h"

/** Strict reader for the canonical data/cars/*.json files. */
class FOsdsCarDefinitionLoader
{
public:
	/** Load a car by its canonical JSON id from the repository's data/cars folder. */
	static bool LoadById(const FString& CarId, FOsdsCarDefinition& OutCar, FString& OutError);

	/** Load and validate one car JSON file, preserving its full source document. */
	static bool LoadFromFile(const FString& FilePath, FOsdsCarDefinition& OutCar, FString& OutError);

	/** Parse in-memory JSON with the same path-specific diagnostics and validation. */
	static bool ParseJson(const FString& JsonText, const FString& SourcePath, FOsdsCarDefinition& OutCar, FString& OutError);

	/** Return the sorted ids present in the canonical source directory. */
	static TArray<FString> FindAvailableIds();
};
