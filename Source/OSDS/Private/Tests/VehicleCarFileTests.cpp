// Copyright OSDS contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Vehicle/CarDefinition.h"
#include "Vehicle/CarDefinitionLoader.h"

// Car files for the 3.0 cars that have no model yet (ticket #41) load through the strict loader
// and carry their published mass, wheelbase and gear count. Expected values are the published
// figures (Porsche 2020 US spec sheet 3,382 lb PDK; Audi/Car and Driver TT 3.2 test car 3,255 lb).

namespace
{
	constexpr EAutomationTestFlags CarFileFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::EngineFilter;

	bool RunCarFileTest(FAutomationTestBase& Test, const TCHAR* CarId, double MassKg, double WheelbaseM, int32 Gears, const TCHAR* Layout)
	{
		FOsdsCarDefinition Car;
		FString Error;
		if (!Test.TestTrue(FString::Printf(TEXT("%s loads"), CarId), FOsdsCarDefinitionLoader::LoadById(CarId, Car, Error)))
		{
			Test.AddError(Error);
			return false;
		}
		Test.TestEqual(TEXT("mass_kg"), Car.MassKg, MassKg, 1.0);
		Test.TestEqual(TEXT("wheelbase_m"), Car.WheelbaseM, WheelbaseM, 0.001);
		Test.TestEqual(TEXT("gear count"), Car.ForwardGearRatios.Num(), Gears);
		Test.TestEqual(TEXT("layout"), Car.DrivetrainLayout, FString(Layout));
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsCarFilePorsche992Test, "OSDS.Vehicle.CarFile.porsche992", CarFileFlags)
bool FOsdsCarFilePorsche992Test::RunTest(const FString& Parameters)
{
	return RunCarFileTest(*this, TEXT("porsche992"), 1534.0, 2.45, 8, TEXT("rwd"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsCarFileAuditTTTest, "OSDS.Vehicle.CarFile.audittt", CarFileFlags)
bool FOsdsCarFileAuditTTTest::RunTest(const FString& Parameters)
{
	return RunCarFileTest(*this, TEXT("audittt"), 1476.0, 2.468, 6, TEXT("awd"));
}

#endif
