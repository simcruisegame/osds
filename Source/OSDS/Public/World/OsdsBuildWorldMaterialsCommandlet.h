// Copyright OSDS contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "OsdsBuildWorldMaterialsCommandlet.generated.h"

/**
 * Saves the code-defined world materials as assets for cooked builds:
 *   UnrealEditor-Cmd.exe OSDS.uproject -run=OsdsBuildWorldMaterials
 * writes Content/World/Materials/M_OSDS{Ground,Asphalt,Concrete,Paint,Water}.uasset.
 * The definitions live in OsdsWorldResources.cpp; the assets are outputs.
 */
UCLASS()
class OSDS_API UOsdsBuildWorldMaterialsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UOsdsBuildWorldMaterialsCommandlet();
	virtual int32 Main(const FString& Params) override;
};
