﻿#include "Public/DualSenseLibrary.h"

#include <Windows.h>
#include <iostream>
#include <algorithm>
#include "InputCoreTypes.h"

#define MAX_DEVICES 8

DS5W::_DeviceEnumInfo* UDualSenseLibrary::Infos;
TMap<int32, DS5W::_DS5InputState> UDualSenseLibrary::InputState;
TMap<int32, DS5W::DS5OutputState> UDualSenseLibrary::OutputState;
TMap<int32, DS5W::DeviceContext> UDualSenseLibrary::DeviceContexts;

TMap<int32, bool> UDualSenseLibrary::EnableAccelerometer;
TMap<int32, bool> UDualSenseLibrary::EnableGyroscope;
TMap<int32, bool> UDualSenseLibrary::EnableTouch1;
TMap<int32, bool> UDualSenseLibrary::EnableTouch2;

TMap<int32, float> UDualSenseLibrary::BatteryLevel;
TMap<int32, int32> UDualSenseLibrary::LeftTriggerFeedback;
TMap<int32, int32> UDualSenseLibrary::RightTriggerFeedback;
TMap<FInputDeviceId, TMap<FName, bool>> UDualSenseLibrary::ButtonStates;

int32 UDualSenseLibrary::ControllersCount = 0;
TMap<int32, bool> UDualSenseLibrary::IsInitialized;

UDualSenseLibrary::FOnDeviceRegistered UDualSenseLibrary::DeviceRegisteredEvent;

bool UDualSenseLibrary::Reconnect(int32 ControllerId)
{
	if (!DeviceContexts.Contains(ControllerId))
	{
		if (ControllersCount == 0)
		{
			return InitializeLibrary();
		}
		
		return false;
	}

	if (DS5W_SUCCESS(DS5W::reconnectDevice(&DeviceContexts[ControllerId])))
	{
		return true;
	}
	
	return false;
}


bool UDualSenseLibrary::InitializeLibrary()
{
	Infos = new DS5W::_DeviceEnumInfo[MAX_DEVICES];

	unsigned int Count = 0;
	switch (DS5W::enumDevices(Infos, MAX_DEVICES, &Count))
	{
	case DS5W_OK:

	case DS5W_E_INSUFFICIENT_BUFFER:
		break;

	default:
		std::cout << "Error enumDevices default: " << GetLastError() << std::endl;
		return false;
	}

	ControllersCount = static_cast<int32>(Count);
	return Connection();
}

bool UDualSenseLibrary::Connection()
{
	for (int32 i = 0; i < ControllersCount; i++)
	{
		if (OutputState.Contains(i))
		{
			ZeroMemory(&OutputState[i], sizeof(DS5W::DS5OutputState));
		}

		if (InputState.Contains(i))
		{
			ZeroMemory(&InputState[i], sizeof(DS5W::_DS5InputState));
		}

		if (DeviceContexts.Contains(i))
		{
			DS5W::freeDeviceContext(&DeviceContexts[i]);
		}

		DS5W::DeviceContext Context;
		if (DS5W_SUCCESS(DS5W::initDeviceContext(&Infos[i], &Context)))
		{
			DS5W::_DS5InputState InState;
			if (!DS5W_SUCCESS(DS5W::getDeviceInputState(&Context, &InState)))
			{
				UE_LOG(LogTemp, Error, TEXT("Error DeviceInputState: %d"), i);
				continue;
			}

			OutputState.Add(i, DS5W::DS5OutputState());
			InputState.Add(i, InState);
			DeviceContexts.Add(i, Context);

			BatteryLevel.Add(i, 0.0f);
			
			LeftTriggerFeedback.Add(i, 0);
			RightTriggerFeedback.Add(i, 0);
			
			EnableAccelerometer.Add(i, false);
			EnableGyroscope.Add(i, false);
			EnableTouch1.Add(i, false);
			EnableTouch2.Add(i, false);

			if (IsInitialized.Contains(i))
			{
				IsInitialized[i] = true;
				continue;
			}

			IsInitialized.Add(i, true);
			continue;
		}

		if (IsInitialized.Contains(i))
		{
			IsInitialized[i] = false;
			continue;
		}

		IsInitialized.Add(i, false);
	}

	return true;
}

void UDualSenseLibrary::ShutdownLibrary()
{
	for (int32 i = 0; i < ControllersCount; i++)
	{
		if (!IsInitialized.Contains(i))
		{
			continue;
		}

		if (IsInitialized[i])
		{
			ZeroMemory(&OutputState[i], sizeof(DS5W::DS5OutputState));
			ZeroMemory(&InputState[i], sizeof(DS5W::DS5InputState));

			DS5W::freeDeviceContext(&DeviceContexts[i]);
			UE_LOG(LogTemp, Log, TEXT("DualSense ControllerId %d disconnected with success."), i);
		}
	}

	EnableTouch1.Reset();
	EnableTouch2.Reset();
	EnableGyroscope.Reset();
	EnableAccelerometer.Reset();
	IsInitialized.Reset();
}

bool UDualSenseLibrary::IsConnected(int32 ControllerId)
{
	if (!IsInitialized.Contains(ControllerId))
	{
		return false;
	}

	return IsInitialized[ControllerId];
}

void UDualSenseLibrary::SetConnectionIsValid(int32 ControllerId, bool IsValid)
{
	if (!IsInitialized.Contains(ControllerId))
	{
		return;
	}

	IsInitialized[ControllerId] = IsValid;
}

void UDualSenseLibrary::SendOut(const int32 ControllerId)
{
	if (!DeviceContexts.Contains(ControllerId) || !OutputState.Contains(ControllerId))
	{
		return;
	}

	DS5W::setDeviceOutputState(&DeviceContexts[ControllerId], &OutputState[ControllerId]);
}

int32 UDualSenseLibrary::GetTrirggersFeedback(int32 ControllerId, EControllerHand& HandTrigger)
{
	if (!LeftTriggerFeedback.Contains(ControllerId) && !RightTriggerFeedback.Contains(ControllerId))
	{
		return 0;
	}

	if (HandTrigger == EControllerHand::Left)
	{
		return LeftTriggerFeedback[ControllerId];
	}

	return RightTriggerFeedback[ControllerId];
}

void UDualSenseLibrary::CheckButtonInput(
	const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler,
	const FPlatformUserId UserId,
	const FInputDeviceId InputDeviceId,
	const FName ButtonName,
	const bool IsButtonPressed)
{
	TMap<FName, bool>& ButtonStateMap = ButtonStates.FindOrAdd(InputDeviceId);
	const bool PreviousState = ButtonStateMap.FindRef(ButtonName);

	if (IsButtonPressed && !PreviousState)
	{
		InMessageHandler.Get().OnControllerButtonPressed(ButtonName, UserId, InputDeviceId, false);
	}
	if (!IsButtonPressed && PreviousState)
	{
		InMessageHandler.Get().OnControllerButtonReleased(ButtonName, UserId, InputDeviceId, false);
	}

	ButtonStateMap.Add(ButtonName, IsButtonPressed);
}

bool UDualSenseLibrary::UpdateInput(
	const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler,
	const FPlatformUserId UserId,
	const FInputDeviceId InputDeviceId
)
{
	if (!DeviceContexts.Contains(InputDeviceId.GetId()) && !InputState.Contains(InputDeviceId.GetId()))
	{
		return false;
	}

	if (
		DS5W_SUCCESS
		(
			DS5W::getDeviceInputState(&DeviceContexts[InputDeviceId.GetId()], &InputState[InputDeviceId.GetId()])
		)
	)
	{
		const auto ButtonsAndDpad = InputState[InputDeviceId.GetId()].buttonsAndDpad;
		const auto ButtonsA = InputState[InputDeviceId.GetId()].buttonsA;
		const auto ButtonsB = InputState[InputDeviceId.GetId()].buttonsB;
		
		if (BatteryLevel.Contains(InputDeviceId.GetId()))
		{
			const float Level = static_cast<float>(InputState[InputDeviceId.GetId()].battery.level) / 255.0f;
			BatteryLevel.Add(InputDeviceId.GetId(), FMath::Clamp(Level, 0.0f, 1.0f));
		}

		if (
			LeftTriggerFeedback.Contains(InputDeviceId.GetId()) &&
			RightTriggerFeedback.Contains(InputDeviceId.GetId())
		)
		{
			LeftTriggerFeedback[InputDeviceId.GetId()] = InputState[InputDeviceId.GetId()].leftTriggerFeedback;
			RightTriggerFeedback[InputDeviceId.GetId()] = InputState[InputDeviceId.GetId()].rightTriggerFeedback;
		}

		// Buttons
		const bool bCross = ButtonsAndDpad & DS5W_ISTATE_BTX_CROSS;
		const bool bCircle = ButtonsAndDpad & DS5W_ISTATE_BTX_CIRCLE;
		const bool bSquare = ButtonsAndDpad & DS5W_ISTATE_BTX_SQUARE;
		const bool bTriangle = ButtonsAndDpad & DS5W_ISTATE_BTX_TRIANGLE;
		CheckButtonInput(InMessageHandler, UserId, InputDeviceId, EKeys::Gamepad_FaceButton_Top.GetFName(), bTriangle);
		CheckButtonInput(InMessageHandler, UserId, InputDeviceId, EKeys::Gamepad_FaceButton_Bottom.GetFName(), bCross);
		CheckButtonInput(InMessageHandler, UserId, InputDeviceId, EKeys::Gamepad_FaceButton_Right.GetFName(), bCircle);
		CheckButtonInput(InMessageHandler, UserId, InputDeviceId, EKeys::Gamepad_FaceButton_Left.GetFName(), bSquare);
		CheckButtonInput(InMessageHandler, UserId, InputDeviceId, EKeys::Gamepad_FaceButton_Left.GetFName(), bSquare);

		// DPad
		const bool bDPadUp = ButtonsAndDpad & DS5W_ISTATE_DPAD_UP;
		const bool bDPadDown = ButtonsAndDpad & DS5W_ISTATE_DPAD_DOWN;
		const bool bDPadLeft = ButtonsAndDpad & DS5W_ISTATE_DPAD_LEFT;
		const bool bDPadRight = ButtonsAndDpad & DS5W_ISTATE_DPAD_RIGHT;
		CheckButtonInput(InMessageHandler, UserId, InputDeviceId, EKeys::Gamepad_DPad_Up.GetFName(), bDPadUp);
		CheckButtonInput(InMessageHandler, UserId, InputDeviceId, EKeys::Gamepad_DPad_Down.GetFName(), bDPadDown);
		CheckButtonInput(InMessageHandler, UserId, InputDeviceId, EKeys::Gamepad_DPad_Left.GetFName(), bDPadLeft);
		CheckButtonInput(InMessageHandler, UserId, InputDeviceId, EKeys::Gamepad_DPad_Right.GetFName(), bDPadRight);

		// Shoulders
		const bool bLeftBumper = ButtonsA & DS5W_ISTATE_BTN_A_LEFT_BUMPER;
		const bool bRightBumper = ButtonsA & DS5W_ISTATE_BTN_A_RIGHT_BUMPER;
		CheckButtonInput(InMessageHandler, UserId, InputDeviceId, EKeys::Gamepad_LeftShoulder.GetFName(), bLeftBumper);
		CheckButtonInput(InMessageHandler, UserId, InputDeviceId, EKeys::Gamepad_RightShoulder.GetFName(),
		                 bRightBumper);

		// Push Stick
		const bool PushLeftStick = ButtonsA & DS5W_ISTATE_BTN_A_LEFT_STICK;
		const bool PushRightStick = ButtonsA & DS5W_ISTATE_BTN_A_RIGHT_STICK;
		CheckButtonInput(InMessageHandler, UserId, InputDeviceId, FName("PS_PushLeftStick"), PushLeftStick);
		CheckButtonInput(InMessageHandler, UserId, InputDeviceId, FName("PS_PushRightStick"), PushRightStick);


		// Specials Actions
		const bool Mic = ButtonsB & DS5W_ISTATE_BTN_B_MIC_BUTTON;
		const bool TouchPad = ButtonsB & DS5W_ISTATE_BTN_B_PAD_BUTTON;
		const bool Playstation = ButtonsB & DS5W_ISTATE_BTN_B_PLAYSTATION_LOGO;
		CheckButtonInput(InMessageHandler, UserId, InputDeviceId, FName("PS_Mic"), Mic);
		CheckButtonInput(InMessageHandler, UserId, InputDeviceId, FName("PS_TouchButtom"), TouchPad);
		CheckButtonInput(InMessageHandler, UserId, InputDeviceId, FName("PS_Button"), Playstation);

		const bool Start = ButtonsA & DS5W_ISTATE_BTN_A_MENU;
		const bool Select = ButtonsA & DS5W_ISTATE_BTN_A_SELECT;
		CheckButtonInput(InMessageHandler, UserId, InputDeviceId, FName("PS_Menu"), Start);
		CheckButtonInput(InMessageHandler, UserId, InputDeviceId, FName("PS_Share"), Select);


		// Analog left
		const float LeftX = InputState[InputDeviceId.GetId()].leftStick.x / 128.f;
		const float LeftY = InputState[InputDeviceId.GetId()].leftStick.y / 128.f;
		InMessageHandler.Get().OnControllerAnalog(EKeys::Gamepad_LeftStick_Left.GetFName(), UserId, InputDeviceId,
		                                          LeftX);
		InMessageHandler.Get().OnControllerAnalog(EKeys::Gamepad_LeftStick_Up.GetFName(), UserId, InputDeviceId, LeftY);

		// Analog right
		const float RightX = InputState[InputDeviceId.GetId()].rightStick.x / 128.0f;
		const float RightY = InputState[InputDeviceId.GetId()].rightStick.y / 128.0f;
		InMessageHandler.Get().OnControllerAnalog(EKeys::Gamepad_RightStick_Left.GetFName(), UserId, InputDeviceId,
		                                          RightX);
		InMessageHandler.Get().OnControllerAnalog(EKeys::Gamepad_RightStick_Up.GetFName(), UserId, InputDeviceId,
		                                          RightY);

		// Triggers
		const float TriggerL = InputState[InputDeviceId.GetId()].leftTrigger / 256.0f;
		const float TriggerR = InputState[InputDeviceId.GetId()].rightTrigger / 256.0f;
		InMessageHandler.Get().OnControllerAnalog(EKeys::Gamepad_LeftTrigger.GetFName(), UserId, InputDeviceId,
		                                          TriggerL);
		InMessageHandler.Get().OnControllerAnalog(EKeys::Gamepad_RightTrigger.GetFName(), UserId, InputDeviceId,
		                                          TriggerR);

		
		if (EnableTouch1.Contains(InputDeviceId.GetId()) && EnableTouch2.Contains(InputDeviceId.GetId()))
		{
			// Accelerometer, Gyroscope and Touchs
			const unsigned int MaxX = 2000;
			const unsigned int MaxY = 2048;

			if (EnableTouch1[InputDeviceId.GetId()])
			{
				DS5W::Touch TouchPoint1 = InputState[InputDeviceId.GetId()].touchPoint1;
				float Touch1X = (2.0f * TouchPoint1.x / MaxX) - 1.0f;
				float Touch1Y = (2.0f * TouchPoint1.y / MaxY) - 1.0f;
				InMessageHandler->OnControllerAnalog(FName("Dualsense_Touch1_X"), UserId, InputDeviceId, Touch1X);
				InMessageHandler->OnControllerAnalog(FName("Dualsense_Touch1_Y"), UserId, InputDeviceId, Touch1Y);	
			}

			if (EnableTouch2[InputDeviceId.GetId()])
			{
				DS5W::Touch TouchPoint2 = InputState[InputDeviceId.GetId()].touchPoint2;
				float Touch2X = (2.0f * TouchPoint2.x / MaxX) - 1.0f;
				float Touch2Y = (2.0f * TouchPoint2.y / MaxY) - 1.0f;
				InMessageHandler->OnControllerAnalog(FName("Dualsense_Touch1_X"), UserId, InputDeviceId, Touch2X);
				InMessageHandler->OnControllerAnalog(FName("Dualsense_Touch2_Y"), UserId, InputDeviceId, Touch2Y);	
			}
		}
		

		if (
			EnableAccelerometer.Contains(InputDeviceId.GetId()) &&
			EnableGyroscope.Contains(InputDeviceId.GetId())
		)
		{
			DS5W::Vector3 Accelerometer = InputState[InputDeviceId.GetId()].accelerometer;
			DS5W::Vector3 Gyroscope = InputState[InputDeviceId.GetId()].gyroscope;

			if (EnableAccelerometer[InputDeviceId.GetId()])
			{
				UE_LOG(LogTemp, Warning, TEXT("Accelerometer: %hd, %hd, %hd"), Accelerometer.x, Accelerometer.y, Accelerometer.z);
				InMessageHandler.Get().OnControllerAnalog(EKeys::Acceleration.GetFName(), UserId, InputDeviceId, Accelerometer.x);
				InMessageHandler.Get().OnControllerAnalog(EKeys::Acceleration.GetFName(), UserId, InputDeviceId, Accelerometer.y);
				InMessageHandler.Get().OnControllerAnalog(EKeys::Acceleration.GetFName(), UserId, InputDeviceId, Accelerometer.z);	
			}
			
			if (EnableGyroscope[InputDeviceId.GetId()])
			{
				UE_LOG(LogTemp, Warning, TEXT("Gyroscope: %hd, %hd, %hd"), Gyroscope.x, Gyroscope.y, Gyroscope.z);
				InMessageHandler.Get().OnControllerAnalog(EKeys::RotationRate.GetFName(), UserId, InputDeviceId, Gyroscope.x);
				InMessageHandler.Get().OnControllerAnalog(EKeys::RotationRate.GetFName(), UserId, InputDeviceId, Gyroscope.y);
				InMessageHandler.Get().OnControllerAnalog(EKeys::RotationRate.GetFName(), UserId, InputDeviceId, Gyroscope.z);	
			}

			if (EnableGyroscope[InputDeviceId.GetId()] && EnableAccelerometer[InputDeviceId.GetId()])
			{
				FVector Tilt = FVector(Accelerometer.x, Accelerometer.y, Accelerometer.z) + FVector(Gyroscope.x, Gyroscope.y, Gyroscope.z);
				UE_LOG(LogTemp, Warning, TEXT("Tilt: %s"), *Tilt.ToString());
				
				InMessageHandler.Get().OnControllerAnalog(EKeys::Tilt.GetFName(), UserId, InputDeviceId, Tilt.X);
				InMessageHandler.Get().OnControllerAnalog(EKeys::Tilt.GetFName(), UserId, InputDeviceId, Tilt.Y);
				InMessageHandler.Get().OnControllerAnalog(EKeys::Tilt.GetFName(), UserId, InputDeviceId, Tilt.Z);		
			}
		}

		return true;
	}
	return false;
}

void UDualSenseLibrary::UpdateColorOutput(const int32 ControllerId, const FColor Color)
{
	if (!OutputState.Contains(ControllerId))
	{
		return;
	}

	OutputState[ControllerId].lightbar.r = Color.R;
	OutputState[ControllerId].lightbar.g = Color.G;
	OutputState[ControllerId].lightbar.b = Color.B;
}

void UDualSenseLibrary::SetVibration(int32 ControllerId, const FForceFeedbackValues& Vibration)
{
	if (!OutputState.Contains(ControllerId))
	{
		return;
	}

	OutputState[ControllerId].leftRumble = max(ConvertTo255(Vibration.LeftLarge), ConvertTo255(Vibration.RightLarge));
	OutputState[ControllerId].rightRumble = max(ConvertTo255(Vibration.LeftSmall), ConvertTo255(Vibration.RightSmall));
	SendOut(ControllerId);
}

void UDualSenseLibrary::SetHapticFeedbackValues(int32 ControllerId, int32 Hand, const FHapticFeedbackValues* Values)
{
	// Config (L2)
	if (Hand == static_cast<int32>(EControllerHand::Left) || Hand == static_cast<int32>(EControllerHand::AnyHand))
	{
		OutputState[ControllerId].leftTriggerEffect.EffectEx.frequency = ConvertTo255(Values->Frequency);
	}

	// Config (R2)
	if (Hand == static_cast<int32>(EControllerHand::Right) || Hand == static_cast<int32>(EControllerHand::AnyHand))
	{
		OutputState[ControllerId].rightTriggerEffect.EffectEx.frequency = ConvertTo255(Values->Frequency);
	}

	SendOut(ControllerId);
}

void UDualSenseLibrary::SetTriggers(int32 ControllerId, const FInputDeviceProperty* Property)
{
	if (Property->Name == FName("InputDeviceTriggerResistance"))
	{
		const FInputDeviceTriggerResistanceProperty* Resistance = static_cast<const
			FInputDeviceTriggerResistanceProperty*>(Property);
		if (
			Resistance->AffectedTriggers == EInputDeviceTriggerMask::Left ||
			Resistance->AffectedTriggers == EInputDeviceTriggerMask::All
		)
		{
			OutputState[ControllerId].leftTriggerEffect.effectType = DS5W::_TriggerEffectType::SectionResitance;
			OutputState[ControllerId].leftTriggerEffect._u1_raw[0] = ConvertTo255(Resistance->StartPosition, 9);
			OutputState[ControllerId].leftTriggerEffect._u1_raw[1] = ConvertTo255(Resistance->EndPosition, 9);
			OutputState[ControllerId].leftTriggerEffect._u1_raw[2] = ConvertTo255(Resistance->StartStrengh, 8);
			OutputState[ControllerId].leftTriggerEffect._u1_raw[3] = ConvertTo255(Resistance->EndStrengh, 8);
		}

		if (
			Resistance->AffectedTriggers == EInputDeviceTriggerMask::Right ||
			Resistance->AffectedTriggers == EInputDeviceTriggerMask::All
		)
		{
			OutputState[ControllerId].rightTriggerEffect.effectType = DS5W::_TriggerEffectType::SectionResitance;
			OutputState[ControllerId].rightTriggerEffect._u1_raw[0] = ConvertTo255(Resistance->StartPosition, 9);
			OutputState[ControllerId].rightTriggerEffect._u1_raw[1] = ConvertTo255(Resistance->EndPosition, 9);
			OutputState[ControllerId].rightTriggerEffect._u1_raw[2] = ConvertTo255(Resistance->StartStrengh, 8);
			OutputState[ControllerId].rightTriggerEffect._u1_raw[3] = ConvertTo255(Resistance->EndStrengh, 8);
		}
	}

	SendOut(ControllerId);
}

void UDualSenseLibrary::SetAcceleration(int32 ControllerId, bool bIsAccelerometer)
{
	if (!EnableAccelerometer.Contains(ControllerId))
	{
		return;
	}
	
	EnableAccelerometer.Add(ControllerId, bIsAccelerometer);
}

void UDualSenseLibrary::SetGyroscope(int32 ControllerId, bool bIsGyroscope)
{
	if (!EnableGyroscope.Contains(ControllerId))
	{
		return;
	}
	
	EnableGyroscope.Add(ControllerId, bIsGyroscope);
}

void UDualSenseLibrary::SetTouch1(int32 ControllerId, bool bIsGyroscope)
{
	if (!EnableTouch1.Contains(ControllerId))
	{
		return;
	}
	
	EnableTouch1.Add(ControllerId, bIsGyroscope);
}

void UDualSenseLibrary::SetTouch2(int32 ControllerId, bool bIsGyroscope)
{
	if (!EnableTouch2.Contains(ControllerId))
	{
		return;
	}
	
	EnableTouch2.Add(ControllerId, bIsGyroscope);
}

void UDualSenseLibrary::ConfigTriggerHapticFeedbackEffect(int32 ControllerId, int32 StartPosition, int32 BeginForce,
                                                          int32 MiddleForce, int32 EndForce,
                                                          const EControllerHand& Hand, bool KeepEffect)
{
	if (!OutputState.Contains(ControllerId))
	{
		return;
	}

	if (Hand == EControllerHand::Left || Hand == EControllerHand::AnyHand)
	{
		OutputState[ControllerId].leftTriggerEffect.effectType = DS5W::_TriggerEffectType::EffectEx;
		OutputState[ControllerId].leftTriggerEffect.EffectEx.startPosition = ConvertTo255(StartPosition, 9);
		OutputState[ControllerId].leftTriggerEffect.EffectEx.keepEffect = KeepEffect;
		OutputState[ControllerId].leftTriggerEffect.EffectEx.beginForce = ConvertTo255(BeginForce, 8);
		OutputState[ControllerId].leftTriggerEffect.EffectEx.middleForce = ConvertTo255(MiddleForce, 8);
		OutputState[ControllerId].leftTriggerEffect.EffectEx.endForce = ConvertTo255(EndForce, 8);
		OutputState[ControllerId].leftTriggerEffect.EffectEx.frequency = 0;
	}

	if (Hand == EControllerHand::Right || Hand == EControllerHand::AnyHand)
	{
		OutputState[ControllerId].rightTriggerEffect.effectType = DS5W::_TriggerEffectType::EffectEx;
		OutputState[ControllerId].rightTriggerEffect.EffectEx.startPosition = ConvertTo255(StartPosition, 9);
		OutputState[ControllerId].rightTriggerEffect.EffectEx.keepEffect = KeepEffect;
		OutputState[ControllerId].rightTriggerEffect.EffectEx.beginForce = ConvertTo255(BeginForce, 8);
		OutputState[ControllerId].rightTriggerEffect.EffectEx.middleForce = ConvertTo255(MiddleForce, 8);
		OutputState[ControllerId].rightTriggerEffect.EffectEx.endForce = ConvertTo255(EndForce, 8);
		OutputState[ControllerId].rightTriggerEffect.EffectEx.frequency = 0;
	}

	SendOut(ControllerId);
}

void UDualSenseLibrary::NoResitance(int32 ControllerId, const EControllerHand& Hand)
{
	if (!OutputState.Contains(ControllerId))
	{
		return;
	}

	if (Hand == EControllerHand::Left || Hand == EControllerHand::AnyHand)
	{
		OutputState[ControllerId].leftTriggerEffect.effectType = DS5W::_TriggerEffectType::NoResitance;
	}

	if (Hand == EControllerHand::Right || Hand == EControllerHand::AnyHand)
	{
		OutputState[ControllerId].rightTriggerEffect.effectType = DS5W::_TriggerEffectType::NoResitance;
	}

	SendOut(ControllerId);
}

void UDualSenseLibrary::ContinuousResitance(int32 ControllerId, int32 StartPosition, int32 Force,
                                            const EControllerHand& Hand)
{
	if (!OutputState.Contains(ControllerId))
	{
		return;
	}

	if (Hand == EControllerHand::Left || Hand == EControllerHand::AnyHand)
	{
		OutputState[ControllerId].leftTriggerEffect.effectType = DS5W::_TriggerEffectType::ContinuousResitance;
		OutputState[ControllerId].leftTriggerEffect.Continuous.startPosition = ConvertTo255(StartPosition, 9);
		OutputState[ControllerId].leftTriggerEffect.Continuous.force = ConvertTo255(Force, 8);
	}

	if (Hand == EControllerHand::Right || Hand == EControllerHand::AnyHand)
	{
		OutputState[ControllerId].rightTriggerEffect.effectType = DS5W::_TriggerEffectType::ContinuousResitance;
		OutputState[ControllerId].rightTriggerEffect.Continuous.startPosition = ConvertTo255(StartPosition, 9);
		OutputState[ControllerId].rightTriggerEffect.Continuous.force = ConvertTo255(Force, 8);
	}

	SendOut(ControllerId);
}

void UDualSenseLibrary::SectionResitance(int32 ControllerId, int32 StartPosition, int32 EndPosition,
                                         const EControllerHand& Hand)
{
	if (!OutputState.Contains(ControllerId))
	{
		UE_LOG(LogTemp, Error, TEXT("SectionResitance: StartPosition %d, EndPosition %d"), StartPosition, EndPosition);
		return;
	}

	if (Hand == EControllerHand::Left || Hand == EControllerHand::AnyHand)
	{
		OutputState[ControllerId].leftTriggerEffect.effectType = DS5W::_TriggerEffectType::SectionResitance;
		OutputState[ControllerId].leftTriggerEffect.Section.startPosition = ConvertTo255(StartPosition, 9);
		OutputState[ControllerId].leftTriggerEffect.Section.endPosition = ConvertTo255(EndPosition, 9);
	}

	if (Hand == EControllerHand::Right || Hand == EControllerHand::AnyHand)
	{
		OutputState[ControllerId].rightTriggerEffect.effectType = DS5W::_TriggerEffectType::SectionResitance;
		OutputState[ControllerId].rightTriggerEffect.Section.startPosition = ConvertTo255(StartPosition, 9);
		OutputState[ControllerId].rightTriggerEffect.Section.endPosition = ConvertTo255(EndPosition, 9);
	}


	SendOut(ControllerId);
}

void UDualSenseLibrary::StopEffect(int32 ControllerId, const EControllerHand& Hand)
{
	if (!OutputState.Contains(ControllerId))
	{
		return;
	}

	if (Hand == EControllerHand::Left || Hand == EControllerHand::AnyHand)
	{
		OutputState[ControllerId].leftTriggerEffect.effectType = DS5W::_TriggerEffectType::NoResitance;
	}

	if (Hand == EControllerHand::Right || Hand == EControllerHand::AnyHand)
	{
		OutputState[ControllerId].rightTriggerEffect.effectType = DS5W::_TriggerEffectType::NoResitance;
	}

	SendOut(ControllerId);
}

void UDualSenseLibrary::StopAllEffects(const int32 ControllerId)
{
	if (!OutputState.Contains(ControllerId))
	{
		return;
	}

	OutputState[ControllerId].leftTriggerEffect.effectType = DS5W::_TriggerEffectType::NoResitance;
	OutputState[ControllerId].rightTriggerEffect.effectType = DS5W::_TriggerEffectType::NoResitance;
	SendOut(ControllerId);
}

void UDualSenseLibrary::StopAll(int32 ControllerId)
{
	if (!OutputState.Contains(ControllerId))
	{
		return;
	}

	ZeroMemory(&OutputState[ControllerId], sizeof(DS5W::DS5OutputState));
	SendOut(ControllerId);
}

void UDualSenseLibrary::SetLedPlayerEffects(int32 ControllerId, int32 NumberLeds, int32 BrightnessValue)
{
	if (!OutputState.Contains(ControllerId))
	{
		return;
	}

	OutputState[ControllerId].playerLeds.bitmask = 0x00;
	SendOut(ControllerId);

	NumberLeds = FMath::Clamp(NumberLeds, 0, 3);
	if (NumberLeds == 1)
	{
		OutputState[ControllerId].playerLeds.bitmask = DS5W_OSTATE_PLAYER_LED_LEFT | DS5W_OSTATE_PLAYER_LED_RIGHT;
	}

	if (NumberLeds == 2)
	{
		OutputState[ControllerId].playerLeds.bitmask = DS5W_OSTATE_PLAYER_LED_LEFT | DS5W_OSTATE_PLAYER_LED_RIGHT;
		OutputState[ControllerId].playerLeds.bitmask |= DS5W_OSTATE_PLAYER_LED_MIDDLE_RIGHT |
			DS5W_OSTATE_PLAYER_LED_MIDDLE_LEFT;
	}

	if (NumberLeds == 3)
	{
		OutputState[ControllerId].playerLeds.bitmask = DS5W_OSTATE_PLAYER_LED_LEFT | DS5W_OSTATE_PLAYER_LED_RIGHT;
		OutputState[ControllerId].playerLeds.bitmask |= DS5W_OSTATE_PLAYER_LED_MIDDLE_RIGHT |
			DS5W_OSTATE_PLAYER_LED_MIDDLE_LEFT;
		OutputState[ControllerId].playerLeds.bitmask |= DS5W_OSTATE_PLAYER_LED_MIDDLE;
	}

	DS5W::LedBrightness Brightness;
	switch (BrightnessValue)
	{
	case 1:
		Brightness = DS5W::LedBrightness::MEDIUM;
		break;
	case 2:
		Brightness = DS5W::LedBrightness::HIGH;
		break;
	case 3:
	default:
		Brightness = DS5W::LedBrightness::LOW;
		break;
	}
	OutputState[ControllerId].playerLeds.brightness = Brightness;

	SendOut(ControllerId);
}

void UDualSenseLibrary::SetLedMicEffects(int32 ControllerId, int32 LedMic)
{
	if (!OutputState.Contains(ControllerId))
	{
		return;
	}

	OutputState[ControllerId].microphoneLed = DS5W::MicLed::OFF;
	if (LedMic == 1)
	{
		OutputState[ControllerId].microphoneLed = DS5W::MicLed::ON;
	}

	if (LedMic == 2)
	{
		OutputState[ControllerId].microphoneLed = DS5W::MicLed::PULSE;
	}

	SendOut(ControllerId);
}

float UDualSenseLibrary::GetLevelBattery(int32 ControllerId)
{
	if (!BatteryLevel.Contains(ControllerId))
	{
		return 0.0f;
	}

	return BatteryLevel[ControllerId];
}

unsigned char UDualSenseLibrary::ConvertTo255(unsigned char value, unsigned char maxInput)
{
	return static_cast<unsigned char>((value * 255) / maxInput);
}


int UDualSenseLibrary::ConvertTo255(const float Value)
{
	const float Min = 0;
	const float Max = 1.0;
	const float NormalizedPosition = (Value - Min) / (Max - Min);
	const int Value255 = static_cast<int>(NormalizedPosition * 255);
	return std::clamp(Value255, 0, 255);
}
