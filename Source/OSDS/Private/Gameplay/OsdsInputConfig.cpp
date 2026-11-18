// Copyright OSDS contributors. All Rights Reserved.

#include "Gameplay/OsdsInputConfig.h"

#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

namespace
{
	// Godot Drive.gd dead zones: 0.04 on both triggers, 0.12 on the stick's X.
	constexpr float TriggerDeadZone = 0.04f;
	constexpr float StickDeadZone = 0.12f;

	struct FActionSpec
	{
		const TCHAR* Name;
		EInputActionValueType Type;
		TObjectPtr<UInputAction> UOsdsInputConfig::* Member;
	};
}

const TArray<FOsdsBindingSpec>& UOsdsInputConfig::Bindings()
{
	static const TArray<FOsdsBindingSpec> Table = {
		{TEXT("ThrottlePad"), EKeys::Gamepad_RightTriggerAxis, TriggerDeadZone},
		{TEXT("ThrottleKey"), EKeys::W},
		{TEXT("ThrottleKey"), EKeys::Up},
		{TEXT("BrakePad"), EKeys::Gamepad_LeftTriggerAxis, TriggerDeadZone},
		{TEXT("BrakeKey"), EKeys::S},
		{TEXT("BrakeKey"), EKeys::Down},
		{TEXT("SteerPad"), EKeys::Gamepad_LeftX, StickDeadZone},
		{TEXT("SteerKey"), EKeys::A, 0.f, true},
		{TEXT("SteerKey"), EKeys::Left, 0.f, true},
		{TEXT("SteerKey"), EKeys::D},
		{TEXT("SteerKey"), EKeys::Right},
		{TEXT("FullBrake"), EKeys::SpaceBar},
		{TEXT("ParkingBrake"), EKeys::H},
		// Release bug #10: the clutch was on LB, the same finger as the brake trigger, so a
		// downshift under braking needed one finger in two places. The right thumb now holds
		// the clutch (A) or the parking brake (B) while the bumpers change gear like paddles.
		{TEXT("ParkingBrake"), EKeys::Gamepad_FaceButton_Right},
		{TEXT("ToggleTransmission"), EKeys::T},
		// The right stick does nothing while driving, so its click is free and never pressed by accident.
		{TEXT("ToggleTransmission"), EKeys::Gamepad_RightThumbstick},
		{TEXT("Clutch"), EKeys::LeftShift},
		{TEXT("Clutch"), EKeys::RightShift},
		{TEXT("Clutch"), EKeys::Gamepad_FaceButton_Bottom},
		{TEXT("ShiftUp"), EKeys::E},
		{TEXT("ShiftUp"), EKeys::Gamepad_RightShoulder},
		{TEXT("ShiftDown"), EKeys::Q},
		{TEXT("ShiftDown"), EKeys::Gamepad_LeftShoulder},
		{TEXT("StartEngine"), EKeys::I},
		{TEXT("StartEngine"), EKeys::Gamepad_DPad_Left},
		{TEXT("CycleCamera"), EKeys::C},
		{TEXT("CycleCamera"), EKeys::Gamepad_FaceButton_Top},
		{TEXT("CycleHud"), EKeys::F2},
		// Not on L3: steering hard presses the stick by accident.
		{TEXT("CycleHud"), EKeys::Gamepad_FaceButton_Left},
		{TEXT("ToggleStats"), EKeys::F3},
		// The lens is F6 and the road atlas's View setting only: the left-stick click is pressed by
		// accident in hard steering, and a lens change mid-corner is not harmless (the 2.0.0 playtest).
		{TEXT("CycleLens"), EKeys::F6},
		{TEXT("ToggleSound"), EKeys::M},
		{TEXT("ToggleLights"), EKeys::L},
		{TEXT("ToggleAtlas"), EKeys::Tab},
		{TEXT("ToggleAtlas"), EKeys::Gamepad_Special_Right},
		{TEXT("FindPlace"), EKeys::Slash},
		{TEXT("Recover"), EKeys::R},
		{TEXT("Recover"), EKeys::Gamepad_Special_Left},
		// MapHUD.gd: the D-pad zooms the minimap.
		{TEXT("MinimapIn"), EKeys::Gamepad_DPad_Up},
		{TEXT("MinimapIn"), EKeys::Equals},
		{TEXT("MinimapOut"), EKeys::Gamepad_DPad_Down},
		{TEXT("MinimapOut"), EKeys::Hyphen},
		{TEXT("Tuning"), EKeys::Gamepad_DPad_Right},
		{TEXT("Tuning"), EKeys::F4},
	};
	return Table;
}

UOsdsInputConfig* UOsdsInputConfig::Create(UObject* Outer)
{
	UOsdsInputConfig* Config = NewObject<UOsdsInputConfig>(Outer, TEXT("OsdsInputConfig"));

	static const FActionSpec Actions[] = {
		{TEXT("SteerPad"), EInputActionValueType::Axis1D, &UOsdsInputConfig::SteerPad},
		{TEXT("SteerKey"), EInputActionValueType::Axis1D, &UOsdsInputConfig::SteerKey},
		{TEXT("ThrottlePad"), EInputActionValueType::Axis1D, &UOsdsInputConfig::ThrottlePad},
		{TEXT("BrakePad"), EInputActionValueType::Axis1D, &UOsdsInputConfig::BrakePad},
		{TEXT("ThrottleKey"), EInputActionValueType::Boolean, &UOsdsInputConfig::ThrottleKey},
		{TEXT("BrakeKey"), EInputActionValueType::Boolean, &UOsdsInputConfig::BrakeKey},
		{TEXT("FullBrake"), EInputActionValueType::Boolean, &UOsdsInputConfig::FullBrake},
		{TEXT("ParkingBrake"), EInputActionValueType::Boolean, &UOsdsInputConfig::ParkingBrake},
		{TEXT("Clutch"), EInputActionValueType::Boolean, &UOsdsInputConfig::Clutch},
		{TEXT("ToggleTransmission"), EInputActionValueType::Boolean, &UOsdsInputConfig::ToggleTransmission},
		{TEXT("ShiftUp"), EInputActionValueType::Boolean, &UOsdsInputConfig::ShiftUp},
		{TEXT("ShiftDown"), EInputActionValueType::Boolean, &UOsdsInputConfig::ShiftDown},
		{TEXT("StartEngine"), EInputActionValueType::Boolean, &UOsdsInputConfig::StartEngine},
		{TEXT("CycleCamera"), EInputActionValueType::Boolean, &UOsdsInputConfig::CycleCamera},
		{TEXT("CycleHud"), EInputActionValueType::Boolean, &UOsdsInputConfig::CycleHud},
		{TEXT("ToggleAtlas"), EInputActionValueType::Boolean, &UOsdsInputConfig::ToggleAtlas},
		{TEXT("FindPlace"), EInputActionValueType::Boolean, &UOsdsInputConfig::FindPlace},
		{TEXT("Recover"), EInputActionValueType::Boolean, &UOsdsInputConfig::Recover},
		{TEXT("ToggleSound"), EInputActionValueType::Boolean, &UOsdsInputConfig::ToggleSound},
		{TEXT("ToggleLights"), EInputActionValueType::Boolean, &UOsdsInputConfig::ToggleLights},
		{TEXT("CycleLens"), EInputActionValueType::Boolean, &UOsdsInputConfig::CycleLens},
		{TEXT("ToggleStats"), EInputActionValueType::Boolean, &UOsdsInputConfig::ToggleStats},
		{TEXT("MinimapIn"), EInputActionValueType::Boolean, &UOsdsInputConfig::MinimapIn},
		{TEXT("MinimapOut"), EInputActionValueType::Boolean, &UOsdsInputConfig::MinimapOut},
		{TEXT("Tuning"), EInputActionValueType::Boolean, &UOsdsInputConfig::Tuning},
	};

	for (const FActionSpec& Spec : Actions)
	{
		UInputAction* Action = NewObject<UInputAction>(Config, FName(FString(TEXT("IA_")) + Spec.Name));
		Action->ValueType = Spec.Type;
		Config->*(Spec.Member) = Action;
		Config->ActionsByName.Add(FName(Spec.Name), Action);
	}
	// A and D held together cancel, as Godot's left - right strengths did
	// (the shaper clamps A + Left arrow back to -1).
	Config->SteerKey->AccumulationBehavior = EInputActionAccumulationBehavior::Cumulative;

	Config->DrivingContext = NewObject<UInputMappingContext>(Config, TEXT("IMC_Driving"));
	for (const FOsdsBindingSpec& Binding : Bindings())
	{
		UInputAction* Action = Config->FindAction(Binding.Action);
		check(Action);
		FEnhancedActionKeyMapping& Mapping = Config->DrivingContext->MapKey(Action, Binding.Key);
		if (Binding.DeadZone > 0.f)
		{
			UInputModifierDeadZone* DeadZone = NewObject<UInputModifierDeadZone>(Config->DrivingContext);
			DeadZone->LowerThreshold = Binding.DeadZone;
			DeadZone->UpperThreshold = 1.f;
			DeadZone->Type = EDeadZoneType::Axial;
			Mapping.Modifiers.Add(DeadZone);
		}
		if (Binding.bNegate)
		{
			UInputModifierNegate* Negate = NewObject<UInputModifierNegate>(Config->DrivingContext);
			Negate->bX = true;
			Negate->bY = false;
			Negate->bZ = false;
			Mapping.Modifiers.Add(Negate);
		}
	}
	return Config;
}

UInputAction* UOsdsInputConfig::FindAction(FName Name) const
{
	const TObjectPtr<UInputAction>* Found = ActionsByName.Find(Name);
	return Found ? Found->Get() : nullptr;
}
