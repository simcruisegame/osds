// Copyright OSDS contributors. All Rights Reserved.

#include "Vehicle/OsdsCarPicker.h"

TArray<FString> OsdsCarPicker::DrivableIdsInPickerOrder(const TArray<const FOsdsCarDefinition*>& Cars)
{
	TArray<const FOsdsCarDefinition*> Drivable;
	for (const FOsdsCarDefinition* Car : Cars)
	{
		if (Car && Car->bDrivable) Drivable.Add(Car);
	}
	Drivable.Sort([](const FOsdsCarDefinition& A, const FOsdsCarDefinition& B)
	{
		return A.PickerOrder != B.PickerOrder ? A.PickerOrder < B.PickerOrder : A.Id < B.Id;
	});
	TArray<FString> Ids;
	for (const FOsdsCarDefinition* Car : Drivable) Ids.Add(Car->Id);
	return Ids;
}

FString OsdsCarPicker::ChooseStartCar(const FString& Remembered, const TArray<FString>& Drivable,
	const FString& Override, const FString& DefaultId)
{
	if (!Override.IsEmpty()) return Override;
	if (!Remembered.IsEmpty() && Drivable.Contains(Remembered)) return Remembered;
	if (Drivable.IsEmpty() || Drivable.Contains(DefaultId)) return DefaultId;
	return Drivable[0];
}
