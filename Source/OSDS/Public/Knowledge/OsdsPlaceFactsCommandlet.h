// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "OsdsPlaceFactsCommandlet.generated.h"

/**
 * Fetches the open data of the place knowledge graph for the bundled tiles, so the
 * offline start knows its buildings:
 *   UnrealEditor-Cmd.exe OSDS.uproject -run=OsdsPlaceFacts [-Tiles=x_y,x_y]
 * writes data/knowledge/places/<x>_<y>.json (OpenStreetMap ODbL + Wikidata CC0).
 * One request at a time with a pause (the Overpass fair-use rule).
 */
UCLASS()
class OSDS_API UOsdsPlaceFactsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UOsdsPlaceFactsCommandlet();
	virtual int32 Main(const FString& Params) override;
};
