// Copyright OSDS contributors. All Rights Reserved.

#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Presentation/OsdsCarAssets.h"
#include "Presentation/OsdsCarPresentationComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace OsdsPresentationComponentTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter;
}

using namespace OsdsPresentationComponentTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsPresentationConventionsTest, "OSDS.Presentation.Component.Conventions", Flags)
bool FOsdsPresentationConventionsTest::RunTest(const FString& Parameters)
{
	using C = UOsdsCarPresentationComponent;
	const FVector Hub(113.3, -70.0, 29.3);

	// A left steer (positive) points the wheel's front toward -Y (left).
	FOsdsWheelVisualState Wheel;
	Wheel.SteerAngleRad = 0.3f;
	Wheel.SuspensionTravelCm = 2.5f;
	FTransform Knuckle = C::KnuckleTransform(Hub, Wheel, true);
	TestTrue(TEXT("left steer turns the wheel's front to -Y"), Knuckle.TransformVectorNoScale(FVector::ForwardVector).Y < -0.25);
	TestNearlyEqual(TEXT("steer angle kept"), FMath::Acos(Knuckle.TransformVectorNoScale(FVector::ForwardVector).X), 0.3, 1e-4);
	TestNearlyEqual(TEXT("bump raises the hub"), Knuckle.GetLocation().Z, Hub.Z + 2.5, 1e-4);

	// Positive camber leans the top of the wheel outboard on either side.
	Wheel = FOsdsWheelVisualState();
	Wheel.CamberRad = 0.05f;
	TestTrue(TEXT("left wheel camber: top leans to -Y"), C::KnuckleTransform(Hub, Wheel, true).TransformVectorNoScale(FVector::UpVector).Y < -0.04);
	TestTrue(TEXT("right wheel camber: top leans to +Y"), C::KnuckleTransform(FVector(Hub.X, -Hub.Y, Hub.Z), Wheel, false).TransformVectorNoScale(FVector::UpVector).Y > 0.04);

	// Rolling forward carries the top of the tyre forward.
	const FVector Top = C::SpinRotation(0.2f).RotateVector(FVector(0.0, 0.0, 30.0));
	TestTrue(TEXT("forward roll moves the top of the wheel forward"), Top.X > 5.0);

	// The steering wheel turns by the mean front angle times the ratio, and a
	// left turn turns its top to the driver's left.
	TestNearlyEqual(TEXT("steering wheel angle = mean road angle x ratio"), C::SteeringWheelAngleFor(0.10, 0.08, 15.0), 1.35, 1e-9);
	const FVector RimTop = C::SteeringWheelTurn(0.5).RotateVector(FVector::UpVector);
	TestTrue(TEXT("left turn carries the rim's top to the driver's left (-Y)"), RimTop.Y < -0.4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOsdsPresentationComponentBuildTest, "OSDS.Presentation.Component.BuildsAndPoses", Flags)
bool FOsdsPresentationComponentBuildTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("OsdsPresentationTest"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	AOsdsCarPresentationActor* Actor = World->SpawnActor<AOsdsCarPresentationActor>();
	UOsdsCarPresentationComponent* Car = Actor ? Actor->Presentation.Get() : nullptr;
	if (TestNotNull(TEXT("presentation actor"), Car) && TestTrue(TEXT("meridian_z builds: ") + (Car ? Car->GetLastError() : FString()), Car->InitializeForCar(TEXT("meridian_z"))))
	{
		TArray<UStaticMeshComponent*> Meshes;
		Actor->GetComponents<UStaticMeshComponent>(Meshes);
		// Body, body glass, 4 wheels, 4 calipers, steering wheel.
		TestTrue(FString::Printf(TEXT("at least 11 mesh parts (%d)"), Meshes.Num()), Meshes.Num() >= 11);
		TestTrue(TEXT("steering wheel present"), Car->HasSteeringWheel());
		TestNearlyEqual(TEXT("steering ratio from the profile"), Car->GetSteeringRatio(), 15.7, 1e-9);
		// Tune car's ratio turns the cockpit wheel; the real car's stays the stock one.
		Car->SetTunedSteeringRatio(12.0);
		TestNearlyEqual(TEXT("the cockpit wheel turns at the tuned ratio"), Car->GetWheelSteeringRatio(), 12.0, 1e-9);
		TestNearlyEqual(TEXT("the stock ratio is kept"), Car->GetSteeringRatio(), 15.7, 1e-9);
		Car->SetTunedSteeringRatio(0.0);
		TestNearlyEqual(TEXT("untuned: the real ratio"), Car->GetWheelSteeringRatio(), 15.7, 1e-9);

		FOsdsCarVisualState State;
		State.Wheels[0].SteerAngleRad = 0.1f;
		State.Wheels[1].SteerAngleRad = 0.1f;
		State.Wheels[2].SpinAngleRad = 1.0f;
		State.Wheels[3].SuspensionTravelCm = 3.0f;
		State.bBrake = true;
		Car->ApplyVisualState(State);
		TestNearlyEqual(TEXT("steering wheel shows 0.1 rad x 15.7"), Car->GetSteeringWheelAngleRad(), 1.57, 1e-6);

		// The steering-wheel MESH really turns (#43): its orientation is the rest pose turned by
		// road-wheel angle x 15.7 about the column axis (the part's local X), and the column
		// itself does not move.
		UStaticMeshComponent* WheelMesh = nullptr;
		for (UStaticMeshComponent* Mesh : Meshes)
		{
			if (Mesh->GetName().StartsWith(TEXT("steering_wheel")) && !Mesh->GetName().Contains(TEXT("glass"))) { WheelMesh = Mesh; }
		}
		if (TestNotNull(TEXT("steering wheel mesh component"), WheelMesh))
		{
			const FTransform Rest = Car->GetSteeringWheelRestTransform();
			const FVector Column = Rest.GetUnitAxis(EAxis::X);
			const FQuat Turned = WheelMesh->GetRelativeRotation().Quaternion();
			const FQuat Delta = Rest.GetRotation().Inverse() * Turned;
			FVector TurnAxis;
			float TurnAngle = 0.f;
			Delta.ToAxisAndAngle(TurnAxis, TurnAngle);
			TestNearlyEqual(TEXT("the wheel mesh turns 1.57 rad (0.1 x 15.7)"), static_cast<double>(TurnAngle), 1.57, 1e-4);
			TestTrue(TEXT("the wheel mesh turns about the column axis"), FMath::Abs(FVector::DotProduct(TurnAxis, FVector::ForwardVector)) > 0.9999);
			const FVector ColumnNow = WheelMesh->GetRelativeRotation().Quaternion().RotateVector(FVector::ForwardVector);
			TestTrue(TEXT("the column axis stays put"), ColumnNow.Equals(Rest.GetRotation().RotateVector(FVector::ForwardVector), 1e-4));
			TestTrue(TEXT("the mesh stays on the column centre"), WheelMesh->GetRelativeLocation().Equals(Rest.GetLocation(), 0.01));
			TestTrue(TEXT("column axis points forward"), Column.X > 0.5);
		}

		// The wheel meshes sit on knuckles at the hubs.
		int32 OnHubs = 0;
		for (UStaticMeshComponent* Mesh : Meshes)
		{
			if (Mesh->GetName().StartsWith(TEXT("wheel_rr")) && !Mesh->GetName().Contains(TEXT("upright")))
			{
				const FVector Expected = Car->GetStaticHubLocation(3) + FVector(0, 0, 3.0);
				TestTrue(TEXT("rear-right wheel follows suspension travel"), Mesh->GetComponentLocation().Equals(Expected, 0.01));
				++OnHubs;
			}
		}
		TestEqual(TEXT("rear-right wheel mesh found"), OnHubs, 1);
		FString Error;
		const TSharedPtr<FOsdsBuiltCar> Built = OsdsCarAssets::Get(TEXT("meridian_z"), Error);
		TestTrue(TEXT("big parts use Nanite in editor builds"), Built.IsValid() && (!WITH_EDITOR || Built->Body.bNanite));
	}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
