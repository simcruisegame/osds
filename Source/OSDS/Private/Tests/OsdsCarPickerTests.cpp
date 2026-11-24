// Copyright OSDS contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Vehicle/CarDefinitionLoader.h"
#include "Vehicle/OsdsCarPicker.h"

/*
 * The car picker offers only Drivable cars, in picker order, and a drive starts in the Miata unless the
 * remembered car is Drivable (ADR 0001, ticket #40). The two seams: the picker list from Car files and the
 * start-car decision (remembered id, the Drivable list, the -OsdsCar= override).
 */
namespace
{
	FOsdsCarDefinition MakeCar(const TCHAR* Id, bool bDrivable, int32 Order)
	{
		FOsdsCarDefinition Car;
		Car.Id = Id;
		Car.bDrivable = bDrivable;
		Car.PickerOrder = Order;
		return Car;
	}

	const TArray<FString>& Picker()
	{
		static const TArray<FString> List = { TEXT("nb2001_lowered"), TEXT("meridian_z"), TEXT("mini15s"), TEXT("porsche992") };
		return List;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsCarPickerListTest, "OSDS.Gameplay.CarPicker.ListIsDrivableInPickerOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FOsdsCarPickerListTest::RunTest(const FString& Parameters)
{
	// Alphabetical and file order differ from picker order on purpose.
	const TArray<FOsdsCarDefinition> Cars = {
		MakeCar(TEXT("camaro87"), false, 0),
		MakeCar(TEXT("mini15s"), true, 3),
		MakeCar(TEXT("nb1999"), false, 0),
		MakeCar(TEXT("meridian_z"), true, 2),
		MakeCar(TEXT("nb2001_lowered"), true, 1),
		MakeCar(TEXT("porsche992"), true, 4),
	};
	TArray<const FOsdsCarDefinition*> Pointers;
	for (const FOsdsCarDefinition& Car : Cars) Pointers.Add(&Car);

	TestEqual(TEXT("exactly the Drivable ids, in picker order"), OsdsCarPicker::DrivableIdsInPickerOrder(Pointers), Picker());
	TestEqual(TEXT("no Car file is Drivable: empty list"), OsdsCarPicker::DrivableIdsInPickerOrder({ &Cars[0] }), TArray<FString>());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsCarPickerDataTest, "OSDS.Gameplay.CarPicker.CarFilesFlagTheDrivableCars",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FOsdsCarPickerDataTest::RunTest(const FString& Parameters)
{
	TArray<FOsdsCarDefinition> Cars;
	for (const FString& Id : FOsdsCarDefinitionLoader::FindAvailableIds())
	{
		FOsdsCarDefinition Car;
		FString Error;
		if (FOsdsCarDefinitionLoader::LoadById(Id, Car, Error)) Cars.Add(MoveTemp(Car));
	}
	TArray<const FOsdsCarDefinition*> Pointers;
	for (const FOsdsCarDefinition& Car : Cars) Pointers.Add(&Car);

	// The Audi joins from its own fit ticket; until then the picker holds four.
	TestEqual(TEXT("data/cars flags the Miata, Corvette, Mini and Porsche 911"), OsdsCarPicker::DrivableIdsInPickerOrder(Pointers), Picker());
	for (const FOsdsCarDefinition& Car : Cars)
	{
		if (Car.Id == TEXT("nb1999") || Car.Id == TEXT("camaro87"))
		{
			TestFalse(FString::Printf(TEXT("%s is not Drivable"), *Car.Id), Car.bDrivable);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsCarPickerStartTest, "OSDS.Gameplay.CarPicker.StartCar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FOsdsCarPickerStartTest::RunTest(const FString& Parameters)
{
	const FString Miata = TEXT("nb2001_lowered");
	const auto Start = [](const FString& Remembered, const FString& Override)
	{
		return OsdsCarPicker::ChooseStartCar(Remembered, Picker(), Override, TEXT("nb2001_lowered"));
	};

	TestEqual(TEXT("nothing saved: the Miata"), Start(FString(), FString()), Miata);
	TestEqual(TEXT("a saved Drivable car is kept"), Start(TEXT("mini15s"), FString()), FString(TEXT("mini15s")));
	TestEqual(TEXT("a saved Traffic car starts the Miata"), Start(TEXT("camaro87"), FString()), Miata);
	TestEqual(TEXT("the saved placeholder starts the Miata"), Start(TEXT("nb1999"), FString()), Miata);
	TestEqual(TEXT("a saved id with no Car file starts the Miata"), Start(TEXT("gone_car"), FString()), Miata);
	TestEqual(TEXT("-OsdsCar= reaches any Car file"), Start(TEXT("mini15s"), TEXT("nb1999")), FString(TEXT("nb1999")));
	TestEqual(TEXT("-OsdsCar= wins when nothing is saved"), Start(FString(), TEXT("camaro87")), FString(TEXT("camaro87")));
	TestEqual(TEXT("an empty Drivable list still starts the default"),
		OsdsCarPicker::ChooseStartCar(TEXT("camaro87"), {}, FString(), Miata), Miata);
	TestEqual(TEXT("a default that is not Drivable falls to the first picker car"),
		OsdsCarPicker::ChooseStartCar(FString(), Picker(), FString(), TEXT("nb1999")), Miata);
	return true;
}

#endif
