//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2009 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <stdafx.h>
#include <vd2/system/strutil.h>
#include <at/atnativeui/dialog.h>
#include "inputmanager.h"
#include "inputcontroller.h"
#include <at/atnativeui/uiproxies.h>
#include "resource.h"
#include "joystick.h"
#include <windows.h>

///////////////////////////////////////////////////////////////////////////

class ATUIDialogCreateInputMap final : public VDDialogFrameW32 {
public:
	ATUIDialogCreateInputMap(ATInputManager& inputMan);

	ATInputMap *GetInputMap() const { return mpInputMap; }

	void SetControllerType(ATInputControllerType type);

private:
	bool OnLoaded();
	void OnDataExchange(bool write);

	void UpdateAnalogModeEnable();
	void CreateInputMap();

	ATInputManager& mInputMan;
	vdrefptr<ATInputMap> mpInputMap;

	VDUIProxyComboBoxControl mControllerTypeView;
	VDUIProxyComboBoxControl mControllerPortView;
	VDUIProxyComboBoxControl mAnalogModeView;
	VDUIProxyComboBoxControl mInputSourceView;

	enum class VirtualInput {
		Stick_Left,
		Stick_Right,
		Stick_Up,
		Stick_Down,
		Stick_AnalogX,
		Stick_AnalogY,
		Stick_AnalogPosX,
		Stick_AnalogPosY,
		Stick_AnalogMoveX,
		Stick_AnalogMoveY,
		Stick_AnalogBeamX,
		Stick_AnalogBeamY,
		Button_0,
		Button_1,
		Button_2,
		Button_3,
		Contact,
		Key_0,
		Key_1,
		Key_2,
		Key_3,
		Key_4,
		Key_5,
		Key_6,
		Key_7,
		Key_8,
		Key_9,
		Key_Pound,
		Key_Star,
		Key_Start,
		Key_Pause,
		Key_Reset,
		Key_Period,
		Key_PlusEnter,
		Key_Minus,
		Key_Y,
		Key_N,
		Key_Del,
		Key_Esc
	};

	struct InputMapping {
		VirtualInput mVirtualInput;
		uint32 mInputCode;
	};

	struct InputSourceInfo {
		const wchar_t *mpName;
		const wchar_t *mpShortName;
		bool mbAllowDualADMapping;
		std::initializer_list<InputMapping> mMappings;
	};

	static const InputSourceInfo kInputSourceInfo[];

	struct TargetMapping {
		VirtualInput mVirtualInput;
		uint32 mTargetCode;
	};

	struct ControllerTypeInfo {
		ATInputControllerType mType;
		const wchar_t *mpName;
		const wchar_t *mpShortName;
		int mIndex;
		bool mbHasAbsoluteRelative : 1;
		bool mbRequiresRelative : 1;
		uint32 mAnalogRelativeModifier;
		uint32 mDigitalRelativeModifier;
		std::initializer_list<TargetMapping> mMappings;
		std::initializer_list<ATInputMap::Mapping> mExtraMappings;
	};

	static const ControllerTypeInfo kControllerTypeInfo[];
};

const ATUIDialogCreateInputMap::InputSourceInfo ATUIDialogCreateInputMap::kInputSourceInfo[] {
	{
		.mpName = L"Keyboard (arrows + left ctrl/shift)",
		.mpShortName = L"Keyboard (arrows)",
		.mMappings = {
			{ VirtualInput::Stick_Left,			kATInputCode_KeyLeft },
			{ VirtualInput::Stick_Right,		kATInputCode_KeyRight },
			{ VirtualInput::Stick_Up,			kATInputCode_KeyUp },
			{ VirtualInput::Stick_Down,			kATInputCode_KeyDown },
			{ VirtualInput::Button_0,			kATInputCode_KeyLControl },
			{ VirtualInput::Button_1,			kATInputCode_KeyLShift },
			{ VirtualInput::Key_0,				kATInputCode_Key0 },
			{ VirtualInput::Key_1,				kATInputCode_Key1 },
			{ VirtualInput::Key_2,				kATInputCode_Key2 },
			{ VirtualInput::Key_3,				kATInputCode_Key3 },
			{ VirtualInput::Key_4,				kATInputCode_Key4 },
			{ VirtualInput::Key_5,				kATInputCode_Key5 },
			{ VirtualInput::Key_6,				kATInputCode_Key6 },
			{ VirtualInput::Key_7,				kATInputCode_Key7 },
			{ VirtualInput::Key_8,				kATInputCode_Key8 },
			{ VirtualInput::Key_9,				kATInputCode_Key9 },
			{ VirtualInput::Key_Pound,			kATInputCode_KeyOemPlus },
			{ VirtualInput::Key_Star,			kATInputCode_KeyOemMinus },
			{ VirtualInput::Key_Start,			kATInputCode_KeyF2 },
			{ VirtualInput::Key_Pause,			kATInputCode_KeyF3 },
			{ VirtualInput::Key_Reset,			kATInputCode_KeyF4 },
			{ VirtualInput::Key_Period,			kATInputCode_KeyOemPeriod },
			{ VirtualInput::Key_PlusEnter,		kATInputCode_KeyReturn },
			{ VirtualInput::Key_Minus,			kATInputCode_KeyOemMinus },
			{ VirtualInput::Key_Y,				kATInputCode_KeyY },
			{ VirtualInput::Key_N,				kATInputCode_KeyN },
			{ VirtualInput::Key_Del,			kATInputCode_KeyDelete },
			{ VirtualInput::Key_Esc,			kATInputCode_KeyEscape },
		}
	},
	{
		.mpName = L"Keyboard (numpad)",
		.mpShortName = L"Keyboard (numpad)",
		.mMappings = {
			{ VirtualInput::Stick_Left,			kATInputCode_KeyNumpad7 },
			{ VirtualInput::Stick_Left,			kATInputCode_KeyNumpad4 },
			{ VirtualInput::Stick_Left,			kATInputCode_KeyNumpad1 },
			{ VirtualInput::Stick_Right,		kATInputCode_KeyNumpad9 },
			{ VirtualInput::Stick_Right,		kATInputCode_KeyNumpad6 },
			{ VirtualInput::Stick_Right,		kATInputCode_KeyNumpad3 },
			{ VirtualInput::Stick_Up,			kATInputCode_KeyNumpad7 },
			{ VirtualInput::Stick_Up,			kATInputCode_KeyNumpad8 },
			{ VirtualInput::Stick_Up,			kATInputCode_KeyNumpad9 },
			{ VirtualInput::Stick_Down,			kATInputCode_KeyNumpad1 },
			{ VirtualInput::Stick_Down,			kATInputCode_KeyNumpad2 },
			{ VirtualInput::Stick_Down,			kATInputCode_KeyNumpad3 },
			{ VirtualInput::Button_0,			kATInputCode_KeyNumpad0 },
			{ VirtualInput::Key_0,				kATInputCode_Key0 },
			{ VirtualInput::Key_1,				kATInputCode_Key1 },
			{ VirtualInput::Key_2,				kATInputCode_Key2 },
			{ VirtualInput::Key_3,				kATInputCode_Key3 },
			{ VirtualInput::Key_4,				kATInputCode_Key4 },
			{ VirtualInput::Key_5,				kATInputCode_Key5 },
			{ VirtualInput::Key_6,				kATInputCode_Key6 },
			{ VirtualInput::Key_7,				kATInputCode_Key7 },
			{ VirtualInput::Key_8,				kATInputCode_Key8 },
			{ VirtualInput::Key_9,				kATInputCode_Key9 },
			{ VirtualInput::Key_Pound,			kATInputCode_KeyOemPlus },
			{ VirtualInput::Key_Star,			kATInputCode_KeyOemMinus },
			{ VirtualInput::Key_Start,			kATInputCode_KeyF2 },
			{ VirtualInput::Key_Pause,			kATInputCode_KeyF3 },
			{ VirtualInput::Key_Reset,			kATInputCode_KeyF4 },
			{ VirtualInput::Key_Period,			kATInputCode_KeyOemPeriod },
			{ VirtualInput::Key_PlusEnter,		kATInputCode_KeyReturn },
			{ VirtualInput::Key_Minus,			kATInputCode_KeyOemMinus },
			{ VirtualInput::Key_Y,				kATInputCode_KeyY },
			{ VirtualInput::Key_N,				kATInputCode_KeyN },
			{ VirtualInput::Key_Del,			kATInputCode_KeyDelete },
			{ VirtualInput::Key_Esc,			kATInputCode_KeyEscape },
		}
	},
	{
		.mpName = L"Mouse",
		.mMappings = {
			{ VirtualInput::Stick_AnalogMoveX,	kATInputCode_MouseHoriz },
			{ VirtualInput::Stick_AnalogMoveY,	kATInputCode_MouseVert },
			{ VirtualInput::Stick_AnalogPosX,	kATInputCode_MousePadX },
			{ VirtualInput::Stick_AnalogPosY,	kATInputCode_MousePadY },
			{ VirtualInput::Stick_AnalogBeamX,	kATInputCode_MouseBeamX },
			{ VirtualInput::Stick_AnalogBeamY,	kATInputCode_MouseBeamY },
			{ VirtualInput::Stick_AnalogX,		kATInputCode_MouseEmuStickX },
			{ VirtualInput::Stick_AnalogY,		kATInputCode_MouseEmuStickY },
			{ VirtualInput::Stick_Left,			kATInputCode_MouseLeft },
			{ VirtualInput::Stick_Right,		kATInputCode_MouseRight },
			{ VirtualInput::Stick_Up,			kATInputCode_MouseUp },
			{ VirtualInput::Stick_Down,			kATInputCode_MouseDown },
			{ VirtualInput::Button_0,			kATInputCode_MouseLMB },
			{ VirtualInput::Button_1,			kATInputCode_MouseRMB },
			{ VirtualInput::Button_2,			kATInputCode_MouseMMB },
			{ VirtualInput::Key_0,				kATInputCode_Key0 },
			{ VirtualInput::Key_1,				kATInputCode_Key1 },
			{ VirtualInput::Key_2,				kATInputCode_Key2 },
			{ VirtualInput::Key_3,				kATInputCode_Key3 },
			{ VirtualInput::Key_4,				kATInputCode_Key4 },
			{ VirtualInput::Key_5,				kATInputCode_Key5 },
			{ VirtualInput::Key_6,				kATInputCode_Key6 },
			{ VirtualInput::Key_7,				kATInputCode_Key7 },
			{ VirtualInput::Key_8,				kATInputCode_Key8 },
			{ VirtualInput::Key_9,				kATInputCode_Key9 },
			{ VirtualInput::Key_Pound,			kATInputCode_KeyOemPlus },
			{ VirtualInput::Key_Star,			kATInputCode_KeyOemMinus },
			{ VirtualInput::Key_Start,			kATInputCode_KeyF2 },
			{ VirtualInput::Key_Pause,			kATInputCode_KeyF3 },
			{ VirtualInput::Key_Reset,			kATInputCode_KeyF4 },
		}
	},
	{
		.mpName = L"Gamepad",
		.mbAllowDualADMapping = true,
		.mMappings = {
			{ VirtualInput::Stick_Left,			kATInputCode_JoyPOVLeft },
			{ VirtualInput::Stick_Right,		kATInputCode_JoyPOVRight },
			{ VirtualInput::Stick_Up,			kATInputCode_JoyPOVUp },
			{ VirtualInput::Stick_Down,			kATInputCode_JoyPOVDown },
			{ VirtualInput::Stick_AnalogX,		kATInputCode_JoyHoriz1 },
			{ VirtualInput::Stick_AnalogY,		kATInputCode_JoyVert1 },
			{ VirtualInput::Button_0,			kATInputCode_JoyButton0 },
			{ VirtualInput::Button_1,			kATInputCode_JoyButton0+1 },
			{ VirtualInput::Button_2,			kATInputCode_JoyButton0+2 },
			{ VirtualInput::Button_3,			kATInputCode_JoyButton0+3 },
			{ VirtualInput::Key_0,				kATInputCode_Key0 },
			{ VirtualInput::Key_1,				kATInputCode_Key1 },
			{ VirtualInput::Key_2,				kATInputCode_Key2 },
			{ VirtualInput::Key_3,				kATInputCode_Key3 },
			{ VirtualInput::Key_4,				kATInputCode_Key4 },
			{ VirtualInput::Key_5,				kATInputCode_Key5 },
			{ VirtualInput::Key_6,				kATInputCode_Key6 },
			{ VirtualInput::Key_7,				kATInputCode_Key7 },
			{ VirtualInput::Key_8,				kATInputCode_Key8 },
			{ VirtualInput::Key_9,				kATInputCode_Key9 },
			{ VirtualInput::Key_Pound,			kATInputCode_KeyOemPlus },
			{ VirtualInput::Key_Star,			kATInputCode_KeyOemMinus },
			{ VirtualInput::Key_Start,			kATInputCode_KeyF2 },
			{ VirtualInput::Key_Pause,			kATInputCode_KeyF3 },
			{ VirtualInput::Key_Reset,			kATInputCode_KeyF4 },
		}
	}
};

const ATUIDialogCreateInputMap::ControllerTypeInfo ATUIDialogCreateInputMap::kControllerTypeInfo[] {
	{
		.mType = kATInputControllerType_Joystick,
		.mpName = L"Joystick (CX40)",
		.mpShortName = L"Joystick",
		.mbHasAbsoluteRelative = false,
		.mMappings = {
			{ VirtualInput::Stick_Left,			kATInputTrigger_Left			},
			{ VirtualInput::Stick_Right,		kATInputTrigger_Right			},
			{ VirtualInput::Stick_Up,			kATInputTrigger_Up				},
			{ VirtualInput::Stick_Down,			kATInputTrigger_Down			},
			{ VirtualInput::Button_0,			kATInputTrigger_Button0			},
		}
	},
	{
		.mType = kATInputControllerType_Paddle,
		.mpName = L"Paddle A (CX30)",
		.mpShortName = L"Paddle A",
		.mIndex = 0,
		.mbHasAbsoluteRelative = true,
		.mAnalogRelativeModifier = kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (6 << kATInputTriggerAccel_Shift),
		.mDigitalRelativeModifier = kATInputTriggerMode_Relative | (6 << kATInputTriggerSpeed_Shift) | (10 << kATInputTriggerAccel_Shift),
		.mMappings = {
			{ VirtualInput::Stick_AnalogMoveX,	kATInputTrigger_Axis0			},
			{ VirtualInput::Stick_AnalogX,		kATInputTrigger_Axis0			},
			{ VirtualInput::Stick_Left,			kATInputTrigger_Left			},
			{ VirtualInput::Stick_Right,		kATInputTrigger_Right			},
			{ VirtualInput::Button_0,			kATInputTrigger_Button0			},
		}
	},
	{
		.mType = kATInputControllerType_Paddle,
		.mpName = L"Paddle B (CX30)",
		.mpShortName = L"Paddle B",
		.mIndex = 1,
		.mbHasAbsoluteRelative = true,
		.mAnalogRelativeModifier = kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (6 << kATInputTriggerAccel_Shift),
		.mDigitalRelativeModifier = kATInputTriggerMode_Relative | (6 << kATInputTriggerSpeed_Shift) | (10 << kATInputTriggerAccel_Shift),
		.mMappings = {
			{ VirtualInput::Stick_AnalogMoveX,	kATInputTrigger_Axis0			},
			{ VirtualInput::Stick_AnalogX,		kATInputTrigger_Axis0			},
			{ VirtualInput::Stick_Left,			kATInputTrigger_Left			},
			{ VirtualInput::Stick_Right,		kATInputTrigger_Right			},
			{ VirtualInput::Button_0,			kATInputTrigger_Button0			},
		}
	},
	{
		.mType = kATInputControllerType_STMouse,
		.mpName = L"ST Mouse",
		.mIndex = 0,
		.mbHasAbsoluteRelative = false,
		.mbRequiresRelative = true,
		.mAnalogRelativeModifier = kATInputTriggerMode_Relative | (5 << kATInputTriggerSpeed_Shift) | (7 << kATInputTriggerAccel_Shift),
		.mDigitalRelativeModifier = kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (10 << kATInputTriggerAccel_Shift),
		.mMappings = {
			{ VirtualInput::Stick_AnalogMoveX,	kATInputTrigger_Axis0 },
			{ VirtualInput::Stick_AnalogMoveY,	kATInputTrigger_Axis0+1 },
			{ VirtualInput::Stick_AnalogX,		kATInputTrigger_Axis0			},
			{ VirtualInput::Stick_AnalogY,		kATInputTrigger_Axis0+1			},
			{ VirtualInput::Stick_Left,			kATInputTrigger_Left },
			{ VirtualInput::Stick_Right,		kATInputTrigger_Right },
			{ VirtualInput::Stick_Up,			kATInputTrigger_Up },
			{ VirtualInput::Stick_Down,			kATInputTrigger_Down },
			{ VirtualInput::Button_0,			kATInputTrigger_Button0 },
			{ VirtualInput::Button_1,			kATInputTrigger_Button0+1 },
		},
	},
	{
		.mType = kATInputControllerType_AmigaMouse,
		.mpName = L"Amiga Mouse",
		.mIndex = 0,
		.mbHasAbsoluteRelative = false,
		.mbRequiresRelative = true,
		.mAnalogRelativeModifier = kATInputTriggerMode_Relative | (5 << kATInputTriggerSpeed_Shift) | (7 << kATInputTriggerAccel_Shift),
		.mDigitalRelativeModifier = kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (10 << kATInputTriggerAccel_Shift),
		.mMappings = {
			{ VirtualInput::Stick_AnalogMoveX,	kATInputTrigger_Axis0 },
			{ VirtualInput::Stick_AnalogMoveY,	kATInputTrigger_Axis0+1 },
			{ VirtualInput::Stick_AnalogX,		kATInputTrigger_Axis0			},
			{ VirtualInput::Stick_AnalogY,		kATInputTrigger_Axis0+1			},
			{ VirtualInput::Stick_Left,			kATInputTrigger_Left },
			{ VirtualInput::Stick_Right,		kATInputTrigger_Right },
			{ VirtualInput::Stick_Up,			kATInputTrigger_Up },
			{ VirtualInput::Stick_Down,			kATInputTrigger_Down },
			{ VirtualInput::Button_0,			kATInputTrigger_Button0 },
			{ VirtualInput::Button_1,			kATInputTrigger_Button0+1 },
		},
	},
	{
		.mType = kATInputControllerType_LightPen,
		.mpName = L"Light Pen (CX70/CX75)",
		.mIndex = 0,
		.mbHasAbsoluteRelative = true,
		.mMappings = {
			{ VirtualInput::Stick_AnalogBeamX,	kATInputTrigger_Axis0 },
			{ VirtualInput::Stick_AnalogBeamY,	kATInputTrigger_Axis0+1 },
			{ VirtualInput::Stick_AnalogPosX,		kATInputTrigger_Axis0 },
			{ VirtualInput::Stick_AnalogPosY,		kATInputTrigger_Axis0+1 },
			{ VirtualInput::Stick_AnalogMoveX,	kATInputTrigger_Axis0 },
			{ VirtualInput::Stick_AnalogMoveY,	kATInputTrigger_Axis0+1 },
			{ VirtualInput::Stick_Left,			kATInputTrigger_Left },
			{ VirtualInput::Stick_Right,		kATInputTrigger_Right },
			{ VirtualInput::Stick_Up,			kATInputTrigger_Up },
			{ VirtualInput::Stick_Down,			kATInputTrigger_Down },
			{ VirtualInput::Button_0,			kATInputTrigger_Button0 },
		},
		.mExtraMappings = {
			{ kATInputCode_None,			0, kATInputTrigger_Button0+2 | kATInputTriggerMode_Inverted },
		},
	},
	{
		.mType = kATInputControllerType_LightGun,
		.mpName = L"Light Gun (XG-1)",
		.mIndex = 1,
		.mbHasAbsoluteRelative = true,
		.mMappings = {
			{ VirtualInput::Stick_AnalogBeamX,	kATInputTrigger_Axis0 },
			{ VirtualInput::Stick_AnalogBeamY,	kATInputTrigger_Axis0+1 },
			{ VirtualInput::Stick_AnalogPosX,		kATInputTrigger_Axis0 },
			{ VirtualInput::Stick_AnalogPosY,		kATInputTrigger_Axis0+1 },
			{ VirtualInput::Stick_AnalogMoveX,	kATInputTrigger_Axis0 },
			{ VirtualInput::Stick_AnalogMoveY,	kATInputTrigger_Axis0+1 },
			{ VirtualInput::Stick_Left,			kATInputTrigger_Left },
			{ VirtualInput::Stick_Right,		kATInputTrigger_Right },
			{ VirtualInput::Stick_Up,			kATInputTrigger_Up },
			{ VirtualInput::Stick_Down,			kATInputTrigger_Down },
			{ VirtualInput::Button_0,			kATInputTrigger_Button0 },
		},
		.mExtraMappings = {
			{ kATInputCode_None,			0, kATInputTrigger_Button0+2 | kATInputTriggerMode_Inverted },
		},
	},
	{
		.mType = kATInputControllerType_Tablet,
		.mpName = L"Tablet (Atari touch tablet)",
		.mbHasAbsoluteRelative = true,
		.mAnalogRelativeModifier = kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (7 << kATInputTriggerAccel_Shift),
		.mDigitalRelativeModifier = kATInputTriggerMode_Relative | (3 << kATInputTriggerSpeed_Shift) | (10 << kATInputTriggerAccel_Shift),
		.mMappings = {
			{ VirtualInput::Stick_AnalogMoveX,	kATInputTrigger_Axis0 },
			{ VirtualInput::Stick_AnalogMoveY,	kATInputTrigger_Axis0+1 },
			{ VirtualInput::Stick_AnalogBeamX,	kATInputTrigger_Axis0 },
			{ VirtualInput::Stick_AnalogBeamY,	kATInputTrigger_Axis0+1 },
			{ VirtualInput::Stick_AnalogPosX,	kATInputTrigger_Axis0 },
			{ VirtualInput::Stick_AnalogPosY,	kATInputTrigger_Axis0+1 },
			{ VirtualInput::Stick_AnalogX,		kATInputTrigger_Axis0 },
			{ VirtualInput::Stick_AnalogY,		kATInputTrigger_Axis0+1 },
			{ VirtualInput::Stick_Left,			kATInputTrigger_Left },
			{ VirtualInput::Stick_Right,		kATInputTrigger_Right },
			{ VirtualInput::Stick_Up,			kATInputTrigger_Up },
			{ VirtualInput::Stick_Down,			kATInputTrigger_Down },
			{ VirtualInput::Button_0,			kATInputTrigger_Button0 },
			{ VirtualInput::Button_1,			kATInputTrigger_Button0+1 | kATInputTriggerMode_Toggle },
			{ VirtualInput::Button_2,			kATInputTrigger_Button0+2 },
			{ VirtualInput::Button_3,			kATInputTrigger_Button0+3 },
		},
	},
	{
		.mType = kATInputControllerType_KoalaPad,
		.mpName = L"Tablet (KoalaPad)",
		.mbHasAbsoluteRelative = true,
		.mAnalogRelativeModifier = kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (7 << kATInputTriggerAccel_Shift),
		.mDigitalRelativeModifier = kATInputTriggerMode_Relative | (3 << kATInputTriggerSpeed_Shift) | (10 << kATInputTriggerAccel_Shift),
		.mMappings = {
			{ VirtualInput::Stick_AnalogMoveX,	kATInputTrigger_Axis0 },
			{ VirtualInput::Stick_AnalogMoveY,	kATInputTrigger_Axis0+1 },
			{ VirtualInput::Stick_AnalogBeamX,	kATInputTrigger_Axis0 },
			{ VirtualInput::Stick_AnalogBeamY,	kATInputTrigger_Axis0+1 },
			{ VirtualInput::Stick_AnalogPosX,	kATInputTrigger_Axis0 },
			{ VirtualInput::Stick_AnalogPosY,	kATInputTrigger_Axis0+1 },
			{ VirtualInput::Stick_AnalogX,		kATInputTrigger_Axis0 },
			{ VirtualInput::Stick_AnalogY,		kATInputTrigger_Axis0+1 },
			{ VirtualInput::Stick_Left,			kATInputTrigger_Left },
			{ VirtualInput::Stick_Right,		kATInputTrigger_Right },
			{ VirtualInput::Stick_Up,			kATInputTrigger_Up },
			{ VirtualInput::Stick_Down,			kATInputTrigger_Down },
			{ VirtualInput::Button_0,			kATInputTrigger_Button0 },
			{ VirtualInput::Button_1,			kATInputTrigger_Button0+3 | kATInputTriggerMode_Toggle },
			{ VirtualInput::Button_2,			kATInputTrigger_Button0+1 },
			{ VirtualInput::Button_3,			kATInputTrigger_Button0+2 },
		},
	},
	{
		.mType = kATInputControllerType_Keypad,
		.mpName = L"Numerical Keypad (CX85)",
		.mMappings = {
			{ VirtualInput::Key_1,				kATInputTrigger_Button0 },
			{ VirtualInput::Key_2,				kATInputTrigger_Button0+1 },
			{ VirtualInput::Key_3,				kATInputTrigger_Button0+2 },
			{ VirtualInput::Key_4,				kATInputTrigger_Button0+3 },
			{ VirtualInput::Key_5,				kATInputTrigger_Button0+4 },
			{ VirtualInput::Key_6,				kATInputTrigger_Button0+5 },
			{ VirtualInput::Key_7,				kATInputTrigger_Button0+6 },
			{ VirtualInput::Key_8,				kATInputTrigger_Button0+7 },
			{ VirtualInput::Key_9,				kATInputTrigger_Button0+8 },
			{ VirtualInput::Key_0,				kATInputTrigger_Button0+9 },
			{ VirtualInput::Key_Period,			kATInputTrigger_Button0+10 },
			{ VirtualInput::Key_PlusEnter,		kATInputTrigger_Button0+11 },
			{ VirtualInput::Key_Minus,			kATInputTrigger_Button0+12 },
			{ VirtualInput::Key_Y,				kATInputTrigger_Button0+13 },
			{ VirtualInput::Key_N,				kATInputTrigger_Button0+14 },
			{ VirtualInput::Key_Del,			kATInputTrigger_Button0+15 },
			{ VirtualInput::Key_Esc,			kATInputTrigger_Button0+16 },
		},
	},
	{
		.mType = kATInputControllerType_Trackball_CX80_V1,
		.mpName = L"Trak-Ball (CX80 V1)",
		.mbRequiresRelative = true,
		.mAnalogRelativeModifier = kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (6 << kATInputTriggerAccel_Shift),
		.mDigitalRelativeModifier = kATInputTriggerMode_Relative | (10 << kATInputTriggerSpeed_Shift) | (10 << kATInputTriggerAccel_Shift),
		.mMappings = {
			{ VirtualInput::Stick_AnalogMoveX,	kATInputTrigger_Axis0 },
			{ VirtualInput::Stick_AnalogMoveY,	kATInputTrigger_Axis0+1 },
			{ VirtualInput::Stick_AnalogX,		kATInputTrigger_Axis0 },
			{ VirtualInput::Stick_AnalogY,		kATInputTrigger_Axis0+1 },
			{ VirtualInput::Stick_Left,			kATInputTrigger_Left },
			{ VirtualInput::Stick_Right,		kATInputTrigger_Right },
			{ VirtualInput::Stick_Up,			kATInputTrigger_Up },
			{ VirtualInput::Stick_Down,			kATInputTrigger_Down },
			{ VirtualInput::Button_0,			kATInputTrigger_Button0 },
		}
	},
	{
		.mType = kATInputControllerType_Driving,
		.mpName = L"Driving Controller (CX20)",
		.mpShortName = L"Driving Controller",
		.mbRequiresRelative = true,
		.mAnalogRelativeModifier = kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (6 << kATInputTriggerAccel_Shift),
		.mDigitalRelativeModifier = kATInputTriggerMode_Relative | (5 << kATInputTriggerSpeed_Shift),
		.mMappings = {
			{ VirtualInput::Stick_AnalogMoveX,	kATInputTrigger_Axis0			},
			{ VirtualInput::Stick_Left,			kATInputTrigger_Left			},
			{ VirtualInput::Stick_Right,		kATInputTrigger_Right			},
			{ VirtualInput::Button_0,			kATInputTrigger_Button0			},
		}
	},
	{
		.mType = kATInputControllerType_Keyboard,
		.mpName = L"Keyboard Controller (CX21/23/50)",
		.mpShortName = L"Keyboard Controller",
		.mMappings = {
			{ VirtualInput::Key_1,				kATInputTrigger_Button0			},
			{ VirtualInput::Key_2,				kATInputTrigger_Button0+1		},
			{ VirtualInput::Key_3,				kATInputTrigger_Button0+2		},
			{ VirtualInput::Key_4,				kATInputTrigger_Button0+3		},
			{ VirtualInput::Key_5,				kATInputTrigger_Button0+4		},
			{ VirtualInput::Key_6,				kATInputTrigger_Button0+5		},
			{ VirtualInput::Key_7,				kATInputTrigger_Button0+6		},
			{ VirtualInput::Key_8,				kATInputTrigger_Button0+7		},
			{ VirtualInput::Key_9,				kATInputTrigger_Button0+8		},
			{ VirtualInput::Key_Star,			kATInputTrigger_Button0+9		},
			{ VirtualInput::Key_0,				kATInputTrigger_Button0+10		},
			{ VirtualInput::Key_Pound,			kATInputTrigger_Button0+11		},
		}
	},
	{
		.mType = kATInputControllerType_5200Controller,
		.mpName = L"5200 Controller (CX52)",
		.mbHasAbsoluteRelative = true,
		.mAnalogRelativeModifier = kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (6 << kATInputTriggerAccel_Shift),
		.mDigitalRelativeModifier = kATInputTriggerMode_Relative | (3 << kATInputTriggerSpeed_Shift) | (8 << kATInputTriggerAccel_Shift),
		.mMappings = {
			{ VirtualInput::Stick_AnalogMoveX,	kATInputTrigger_Axis0			},
			{ VirtualInput::Stick_AnalogMoveY,	kATInputTrigger_Axis0+1			},
			{ VirtualInput::Stick_AnalogX,		kATInputTrigger_Axis0			},
			{ VirtualInput::Stick_AnalogY,		kATInputTrigger_Axis0+1			},
			{ VirtualInput::Stick_Left,			kATInputTrigger_Left			},
			{ VirtualInput::Stick_Right,		kATInputTrigger_Right			},
			{ VirtualInput::Stick_Up,			kATInputTrigger_Up				},
			{ VirtualInput::Stick_Down,			kATInputTrigger_Down			},
			{ VirtualInput::Button_0,			kATInputTrigger_Button0			},
			{ VirtualInput::Button_1,			kATInputTrigger_Button0+1		},
			{ VirtualInput::Key_0,				kATInputTrigger_5200_0			},
			{ VirtualInput::Key_1,				kATInputTrigger_5200_1			},
			{ VirtualInput::Key_2,				kATInputTrigger_5200_2			},
			{ VirtualInput::Key_3,				kATInputTrigger_5200_3			},
			{ VirtualInput::Key_4,				kATInputTrigger_5200_4			},
			{ VirtualInput::Key_5,				kATInputTrigger_5200_5			},
			{ VirtualInput::Key_6,				kATInputTrigger_5200_6			},
			{ VirtualInput::Key_7,				kATInputTrigger_5200_7			},
			{ VirtualInput::Key_8,				kATInputTrigger_5200_8			},
			{ VirtualInput::Key_9,				kATInputTrigger_5200_9			},
			{ VirtualInput::Key_Star,			kATInputTrigger_5200_Star		},
			{ VirtualInput::Key_Pound,			kATInputTrigger_5200_Pound		},
			{ VirtualInput::Key_Start,			kATInputTrigger_5200_Start		},
			{ VirtualInput::Key_Pause,			kATInputTrigger_5200_Pause		},
			{ VirtualInput::Key_Reset,			kATInputTrigger_5200_Reset		},
		}
	},
	{
		.mType = kATInputControllerType_5200Trackball,
		.mpName = L"5200 Trak-Ball (CX53)",
		.mbHasAbsoluteRelative = false,
		.mbRequiresRelative = true,
		.mAnalogRelativeModifier = kATInputTriggerMode_Relative | (5 << kATInputTriggerSpeed_Shift) | (6 << kATInputTriggerAccel_Shift),
		.mDigitalRelativeModifier = kATInputTriggerMode_Relative | (3 << kATInputTriggerSpeed_Shift) | (8 << kATInputTriggerAccel_Shift),
		.mMappings = {
			{ VirtualInput::Stick_AnalogMoveX,	kATInputTrigger_Axis0			},
			{ VirtualInput::Stick_AnalogMoveY,	kATInputTrigger_Axis0+1			},
			{ VirtualInput::Stick_AnalogX,		kATInputTrigger_Axis0			},
			{ VirtualInput::Stick_AnalogY,		kATInputTrigger_Axis0+1			},
			{ VirtualInput::Stick_Left,			kATInputTrigger_Left			},
			{ VirtualInput::Stick_Right,		kATInputTrigger_Right			},
			{ VirtualInput::Stick_Up,			kATInputTrigger_Up				},
			{ VirtualInput::Stick_Down,			kATInputTrigger_Down			},
			{ VirtualInput::Button_0,			kATInputTrigger_Button0			},
			{ VirtualInput::Button_1,			kATInputTrigger_Button0+1		},
			{ VirtualInput::Key_0,				kATInputTrigger_5200_0			},
			{ VirtualInput::Key_1,				kATInputTrigger_5200_1			},
			{ VirtualInput::Key_2,				kATInputTrigger_5200_2			},
			{ VirtualInput::Key_3,				kATInputTrigger_5200_3			},
			{ VirtualInput::Key_4,				kATInputTrigger_5200_4			},
			{ VirtualInput::Key_5,				kATInputTrigger_5200_5			},
			{ VirtualInput::Key_6,				kATInputTrigger_5200_6			},
			{ VirtualInput::Key_7,				kATInputTrigger_5200_7			},
			{ VirtualInput::Key_8,				kATInputTrigger_5200_8			},
			{ VirtualInput::Key_9,				kATInputTrigger_5200_9			},
			{ VirtualInput::Key_Star,			kATInputTrigger_5200_Star		},
			{ VirtualInput::Key_Pound,			kATInputTrigger_5200_Pound		},
			{ VirtualInput::Key_Start,			kATInputTrigger_5200_Start		},
			{ VirtualInput::Key_Pause,			kATInputTrigger_5200_Pause		},
			{ VirtualInput::Key_Reset,			kATInputTrigger_5200_Reset		},
		}
	},
};

ATUIDialogCreateInputMap::ATUIDialogCreateInputMap(ATInputManager& inputMan)
	: VDDialogFrameW32(IDD_CREATEINPUTMAP)
	, mInputMan(inputMan)
{
	mControllerTypeView.SetOnSelectionChanged([this](int) { UpdateAnalogModeEnable(); });
}

void ATUIDialogCreateInputMap::SetControllerType(ATInputControllerType type) {
	auto it = std::find_if(std::begin(kControllerTypeInfo), std::end(kControllerTypeInfo), [type](const auto& e) { return e.mType == type; });

	if (it != std::end(kControllerTypeInfo)) {
		mControllerTypeView.SetSelection((int)(it - std::begin(kControllerTypeInfo)));
		UpdateAnalogModeEnable();
	}
}

bool ATUIDialogCreateInputMap::OnLoaded() {
	AddProxy(&mControllerTypeView, IDC_CONTROLLERTYPE);
	AddProxy(&mControllerPortView, IDC_CONTROLLERPORT);
	AddProxy(&mAnalogModeView, IDC_ANALOGCONTROLMODE);
	AddProxy(&mInputSourceView, IDC_INPUTSOURCE);

	VDStringW s;
	for(int i=0; i<4; ++i) {
		s.sprintf(L"Port %d", i + 1);
		mControllerPortView.AddItem(s.c_str());
	}

	mControllerPortView.SetSelection(0);

	for(const auto& e : kControllerTypeInfo)
		mControllerTypeView.AddItem(e.mpName);

	mAnalogModeView.AddItem(L"Absolute - Map input position directly");
	mAnalogModeView.AddItem(L"Relative - Gradually move in input direction");

	for(const auto& e : kInputSourceInfo)
		mInputSourceView.AddItem(e.mpName);

	mInputSourceView.SetSelection(0);

	SetControllerType(mInputMan.Is5200Mode() ? kATInputControllerType_5200Controller : kATInputControllerType_Joystick);

	SetFocusToControl(mControllerTypeView.GetWindowId());
	return true;
}

void ATUIDialogCreateInputMap::OnDataExchange(bool write) {
	if (write)
		CreateInputMap();
}

void ATUIDialogCreateInputMap::UpdateAnalogModeEnable() {
	int typeIdx = mControllerTypeView.GetSelection();
	bool available = false;

	if (typeIdx >= 0 && typeIdx < (int)vdcountof(kControllerTypeInfo)) {
		if (kControllerTypeInfo[typeIdx].mbHasAbsoluteRelative)
			available = true;
	}

	mAnalogModeView.SetEnabled(available);

	int modeIdx = mAnalogModeView.GetSelection();
	if (available) {
		if (modeIdx < 0)
			mAnalogModeView.SetSelection(0);
	} else {
		mAnalogModeView.SetSelection(-1);
	}
}

void ATUIDialogCreateInputMap::CreateInputMap() {
	int typeIdx = mControllerTypeView.GetSelection();
	int portIdx = mControllerPortView.GetSelection();
	int sourceIdx = mInputSourceView.GetSelection();

	if (typeIdx < 0 || typeIdx >= (int)vdcountof(kControllerTypeInfo))
		return;

	if (sourceIdx < 0 || sourceIdx >= (int)vdcountof(kInputSourceInfo))
		return;

	if (portIdx < 0 || portIdx > 3)
		portIdx = 0;

	const bool usingRelative = mAnalogModeView.GetSelection() > 0;

	const ControllerTypeInfo& ctinfo = kControllerTypeInfo[typeIdx];
	const InputSourceInfo& isinfo = kInputSourceInfo[sourceIdx];

	vdrefptr<ATInputMap> imap(new ATInputMap);

	VDStringW s;
	const wchar_t *modeStr = ctinfo.mbHasAbsoluteRelative ? usingRelative ? L"relative; " : L"absolute; " : L"";
	s.sprintf(L"%ls -> %ls (%lsport %u)", isinfo.mpShortName ? isinfo.mpShortName : isinfo.mpName, ctinfo.mpShortName ? ctinfo.mpShortName : ctinfo.mpName, modeStr, portIdx + 1);
	imap->SetName(s.c_str());

	uint32 controllerIdx = (uint32)portIdx;

	if (ctinfo.mType == kATInputControllerType_Paddle)
		controllerIdx = controllerIdx * 2 + ctinfo.mIndex;

	imap->AddController(ctinfo.mType, controllerIdx);

	uint32 analogAxesBound = 0;

	for(const TargetMapping& targetMapping : ctinfo.mMappings) {
		for(const InputMapping& inputMapping : isinfo.mMappings) {
			if (inputMapping.mVirtualInput != targetMapping.mVirtualInput)
				continue;

			uint32 targetCode = targetMapping.mTargetCode;

			// check if this is an analog or digital axis mapping (node that this
			// includes the absolute positioning mappings)
			bool isDigitalAxisMapping = false;
			bool isAnalogAxisMapping = false;
			bool isAbsoluteMapping = false;

			switch(inputMapping.mVirtualInput) {
				case VirtualInput::Stick_Left:
				case VirtualInput::Stick_Right:
				case VirtualInput::Stick_Up:
				case VirtualInput::Stick_Down:
					isDigitalAxisMapping = true;
					break;

				case VirtualInput::Stick_AnalogMoveX:
				case VirtualInput::Stick_AnalogMoveY:
					isAnalogAxisMapping = true;

					if (!usingRelative && ctinfo.mbHasAbsoluteRelative)
						continue;
					break;

				case VirtualInput::Stick_AnalogX:
				case VirtualInput::Stick_AnalogY:
					isAnalogAxisMapping = true;
					break;

				case VirtualInput::Stick_AnalogBeamX:
				case VirtualInput::Stick_AnalogBeamY:
				case VirtualInput::Stick_AnalogPosX:
				case VirtualInput::Stick_AnalogPosY:
					isAbsoluteMapping = true;
					break;
			}

			// check if this targets one of the axis mappings
			if (targetCode == kATInputTrigger_Axis0
				|| targetCode == kATInputTrigger_Axis0+1
				|| targetCode == kATInputTrigger_Left
				|| targetCode == kATInputTrigger_Right
				|| targetCode == kATInputTrigger_Up
				|| targetCode == kATInputTrigger_Down
			) {
				// check if this axis has already been bound to an analog input
				uint32 axisBit = 0;
				switch(targetCode) {
					case kATInputTrigger_Axis0:
					case kATInputTrigger_Left:
					case kATInputTrigger_Right:
						axisBit = 1;
						break;

					case kATInputTrigger_Axis0+1:
					case kATInputTrigger_Up:
					case kATInputTrigger_Down:
						axisBit = 2;
						break;
				}

				const bool isAxisAnalogBound = (analogAxesBound & axisBit) != 0;

				if (isAxisAnalogBound) {
					// if we have made an absolute binding, suppress the analog binding
					if (!isDigitalAxisMapping || isAbsoluteMapping)
						continue;

					// if we have made an absolute or analog binding, suppress the
					// digital binding unless the input source has separate analog and
					// digital inputs
					if (isDigitalAxisMapping && !isinfo.mbAllowDualADMapping)
						continue;
				} else {
					if (!isDigitalAxisMapping)
						analogAxesBound |= axisBit;
				}
			}

			// if this is an analog or digital axis mapping, then apply relative modifiers
			// if relative mode is enabled
			if (usingRelative || ((isDigitalAxisMapping || isAnalogAxisMapping) && ctinfo.mbRequiresRelative)) {
				if (isAnalogAxisMapping)
					targetCode |= ctinfo.mAnalogRelativeModifier;
				else if (isDigitalAxisMapping)
					targetCode |= ctinfo.mDigitalRelativeModifier;
			}

			imap->AddMapping(inputMapping.mInputCode, 0, targetCode);

			// continue searching, as some input sources require multibinding, particularly
			// the numpad for diagonals
		}
	}

	imap->AddMappings(ctinfo.mExtraMappings);

	mInputMan.AddInputMap(imap);

	mpInputMap = std::move(imap);
}

///////////////////////////////////////////////////////////////////////////

bool ATUIShowRebindDialog(VDGUIHandle hParent,
						  ATInputManager& inputManager,
						  IATJoystickManager *joyMan,
						  uint32 targetCode,
						  ATInputControllerType controllerType,
						  const vdpoint32& anchorPos,
						  uint32& inputCode);

void ATGetInputTriggerModeName(uint32 mode, bool addSpeed, VDStringW& s) {
	switch(mode & kATInputTriggerMode_Mask) {
		case kATInputTriggerMode_Default:
		default:
			s += L"Default";
			addSpeed = false;
			break;

		case kATInputTriggerMode_AutoFire:
			s += L"Auto-fire";
			break;

		case kATInputTriggerMode_Toggle:
			s += L"Toggle";
			addSpeed = false;
			break;

		case kATInputTriggerMode_ToggleAF:
			s += L"Toggle auto-fire";
			break;

		case kATInputTriggerMode_Relative:
			s += L"Relative";
			break;

		case kATInputTriggerMode_Absolute:
			s += L"Absolute";
			addSpeed = false;
			break;

		case kATInputTriggerMode_Inverted:
			s += L"Inverted";
			addSpeed = false;
			break;
	}

	if (addSpeed) {
		const uint32 idx = (mode & kATInputTriggerSpeed_Mask) >> kATInputTriggerSpeed_Shift;
		s.append_sprintf(L" %u", idx);

		const uint32 accel = (mode & kATInputTriggerAccel_Mask) >> kATInputTriggerAccel_Shift;
		if (accel)
			s.append_sprintf(L" >>%u", accel);
	}
}

///////////////////////////////////////////////////////////////////////////

class ATUIDialogEditControllerMapping : public VDDialogFrameW32 {
public:
	ATUIDialogEditControllerMapping(ATInputControllerType ctype, int cindex);

	void EnableDefaultOption(bool enable) { mbDefaultEnabled = enable; }
	bool IsDefaultRequested() const { return mbDefaultRequested; }

	void Set(ATInputControllerType controllerType, int controllerIndex, uint32 flagCheckBits) {
		mControllerType = controllerType;
		mControllerIndex = controllerIndex;
		mFlagCheckBits = flagCheckBits;
	}

	ATInputControllerType GetControllerType() const { return mControllerType; }
	int GetControllerIndex() const { return mControllerIndex; }
	uint32 GetFlagCheckBits() const { return mFlagCheckBits; }

protected:
	bool OnLoaded();
	void OnDestroy();
	void OnDataExchange(bool write);

	uint32 mInputCode;
	ATInputControllerType mControllerType;
	int mControllerIndex;
	uint32 mTargetCode;
	uint32 mFlagCheckBits;

	bool	mbDefaultEnabled;
	bool	mbDefaultRequested;

	static const uint32 kInputCodes[];
	static const uint32 kTargetCodes[];
};

ATUIDialogEditControllerMapping::ATUIDialogEditControllerMapping(ATInputControllerType ctype, int cindex)
	: VDDialogFrameW32(IDD_INPUTMAP_ADDCONTROLLER)
	, mControllerType(ctype)
	, mControllerIndex(cindex)
	, mFlagCheckBits(0)
	, mbDefaultEnabled(false)
	, mbDefaultRequested(true)
{
}

bool ATUIDialogEditControllerMapping::OnLoaded() {
	CBAddString(IDC_CONTROLLER, L"Joystick (CX40)");
	CBAddString(IDC_CONTROLLER, L"Mouse (Atari ST)");
	CBAddString(IDC_CONTROLLER, L"Mouse (Amiga)");
	CBAddString(IDC_CONTROLLER, L"Paddle A (CX30)");
	CBAddString(IDC_CONTROLLER, L"Paddle B (CX30)");
	CBAddString(IDC_CONTROLLER, L"Console");
	CBAddString(IDC_CONTROLLER, L"5200 Controller (CX52)");
	CBAddString(IDC_CONTROLLER, L"Input State");
	CBAddString(IDC_CONTROLLER, L"Light Pen (CX70/CX75)");
	CBAddString(IDC_CONTROLLER, L"Tablet (Atari touch tablet)");
	CBAddString(IDC_CONTROLLER, L"Tablet (KoalaPad)");
	CBAddString(IDC_CONTROLLER, L"Numerical Keypad (CX85)");
	CBAddString(IDC_CONTROLLER, L"Trak-Ball (CX80 V1)");
	CBAddString(IDC_CONTROLLER, L"5200 Trak-Ball (CX53)");
	CBAddString(IDC_CONTROLLER, L"Driving Controller (CX20)");
	CBAddString(IDC_CONTROLLER, L"Keyboard Controller (CX21/23/50)");

	CBAddString(IDC_PORT, L"Port 1");
	CBAddString(IDC_PORT, L"Port 2");
	CBAddString(IDC_PORT, L"Port 3 (800 only)");
	CBAddString(IDC_PORT, L"Port 4 (800 only)");
	CBAddString(IDC_PORT, L"MultiJoy #1");
	CBAddString(IDC_PORT, L"MultiJoy #2");
	CBAddString(IDC_PORT, L"MultiJoy #3");
	CBAddString(IDC_PORT, L"MultiJoy #4");
	CBAddString(IDC_PORT, L"MultiJoy #5");
	CBAddString(IDC_PORT, L"MultiJoy #6");
	CBAddString(IDC_PORT, L"MultiJoy #7");
	CBAddString(IDC_PORT, L"MultiJoy #8");

	OnDataExchange(false);
	SetFocusToControl(IDC_SOURCE);

	return true;
}

void ATUIDialogEditControllerMapping::OnDestroy() {
}

void ATUIDialogEditControllerMapping::OnDataExchange(bool write) {
	if (write) {
		int index = CBGetSelectedIndex(IDC_PORT);

		switch(CBGetSelectedIndex(IDC_CONTROLLER)) {
			case 0:
				mControllerType = kATInputControllerType_Joystick;
				mControllerIndex = index;
				break;
			case 1:
				mControllerType = kATInputControllerType_STMouse;
				mControllerIndex = index;
				break;
			case 2:
				mControllerType = kATInputControllerType_AmigaMouse;
				mControllerIndex = index;
				break;
			case 3:
				mControllerType = kATInputControllerType_Paddle;
				mControllerIndex = index*2;
				break;
			case 4:
				mControllerType = kATInputControllerType_Paddle;
				mControllerIndex = index*2+1;
				break;
			case 5:
				mControllerType = kATInputControllerType_Console;
				mControllerIndex = 0;
				break;
			case 6:
				mControllerType = kATInputControllerType_5200Controller;
				mControllerIndex = index;
				break;
			case 7:
				mControllerType = kATInputControllerType_InputState;
				mControllerIndex = 0;
				break;
			case 8:
				mControllerType = kATInputControllerType_LightPen;
				mControllerIndex = index;
				break;
			case 9:
				mControllerType = kATInputControllerType_Tablet;
				mControllerIndex = index;
				break;
			case 10:
				mControllerType = kATInputControllerType_KoalaPad;
				mControllerIndex = index;
				break;
			case 11:
				mControllerType = kATInputControllerType_Keypad;
				mControllerIndex = index;
				break;
			case 12:
				mControllerType = kATInputControllerType_Trackball_CX80_V1;
				mControllerIndex = index;
				break;
			case 13:
				mControllerType = kATInputControllerType_5200Trackball;
				mControllerIndex = index;
				break;
			case 14:
				mControllerType = kATInputControllerType_Driving;
				mControllerIndex = index;
				break;
			case 15:
				mControllerType = kATInputControllerType_Keyboard;
				mControllerIndex = index;
				break;
		}

		if (mbDefaultEnabled)
			mbDefaultRequested = IsButtonChecked(IDC_ADDDEFAULT);

		mFlagCheckBits = 0;

		switch(GetButtonTriState(IDC_FLAG1)) {
			case 0:
				mFlagCheckBits |= kATInputCode_FlagCheck0;
				break;

			case 2:
				mFlagCheckBits |= kATInputCode_FlagCheck0 | kATInputCode_FlagValue0;
				break;
		}

		switch(GetButtonTriState(IDC_FLAG2)) {
			case 0:
				mFlagCheckBits |= kATInputCode_FlagCheck1;
				break;

			case 2:
				mFlagCheckBits |= kATInputCode_FlagCheck1 | kATInputCode_FlagValue1;
				break;
		}
	} else {
		switch(mControllerType) {
			case kATInputControllerType_Joystick:
				CBSetSelectedIndex(IDC_CONTROLLER, 0);
				CBSetSelectedIndex(IDC_PORT, mControllerIndex);
				break;
			case kATInputControllerType_STMouse:
				CBSetSelectedIndex(IDC_CONTROLLER, 1);
				CBSetSelectedIndex(IDC_PORT, mControllerIndex);
				break;
			case kATInputControllerType_AmigaMouse:
				CBSetSelectedIndex(IDC_CONTROLLER, 2);
				CBSetSelectedIndex(IDC_PORT, mControllerIndex);
				break;
			case kATInputControllerType_Paddle:
				CBSetSelectedIndex(IDC_CONTROLLER, mControllerIndex & 1 ? 4 : 3);
				CBSetSelectedIndex(IDC_PORT, mControllerIndex >> 1);
				break;
			case kATInputControllerType_Console:
				CBSetSelectedIndex(IDC_CONTROLLER, 5);
				CBSetSelectedIndex(IDC_PORT, 0);
				break;
			case kATInputControllerType_5200Controller:
				CBSetSelectedIndex(IDC_CONTROLLER, 6);
				CBSetSelectedIndex(IDC_PORT, mControllerIndex);
				break;
			case kATInputControllerType_InputState:
				CBSetSelectedIndex(IDC_CONTROLLER, 7);
				CBSetSelectedIndex(IDC_PORT, 0);
				break;
			case kATInputControllerType_LightPen:
				CBSetSelectedIndex(IDC_CONTROLLER, 8);
				CBSetSelectedIndex(IDC_PORT, mControllerIndex);
				break;
			case kATInputControllerType_Tablet:
				CBSetSelectedIndex(IDC_CONTROLLER, 9);
				CBSetSelectedIndex(IDC_PORT, mControllerIndex);
				break;
			case kATInputControllerType_KoalaPad:
				CBSetSelectedIndex(IDC_CONTROLLER, 10);
				CBSetSelectedIndex(IDC_PORT, mControllerIndex);
				break;
			case kATInputControllerType_Keypad:
				CBSetSelectedIndex(IDC_CONTROLLER, 11);
				CBSetSelectedIndex(IDC_PORT, mControllerIndex);
				break;
			case kATInputControllerType_Trackball_CX80_V1:
				CBSetSelectedIndex(IDC_CONTROLLER, 12);
				CBSetSelectedIndex(IDC_PORT, mControllerIndex);
				break;
			case kATInputControllerType_5200Trackball:
				CBSetSelectedIndex(IDC_CONTROLLER, 13);
				CBSetSelectedIndex(IDC_PORT, mControllerIndex);
				break;
			case kATInputControllerType_Driving:
				CBSetSelectedIndex(IDC_CONTROLLER, 14);
				CBSetSelectedIndex(IDC_PORT, mControllerIndex);
				break;
			case kATInputControllerType_Keyboard:
				CBSetSelectedIndex(IDC_CONTROLLER, 15);
				CBSetSelectedIndex(IDC_PORT, mControllerIndex);
				break;
		}

		
		ShowControl(IDC_ADDDEFAULT, mbDefaultEnabled);

		if (mbDefaultEnabled)
			CheckButton(IDC_ADDDEFAULT, mbDefaultRequested);

		switch(mFlagCheckBits & (kATInputCode_FlagCheck0 | kATInputCode_FlagValue0)) {
			case 0:
			default:
				SetButtonTriState(IDC_FLAG1, 1);
				break;

			case kATInputCode_FlagCheck0:
				SetButtonTriState(IDC_FLAG1, 0);
				break;

			case kATInputCode_FlagCheck0 | kATInputCode_FlagValue0:
				SetButtonTriState(IDC_FLAG1, 2);
				break;
		}

		switch(mFlagCheckBits & (kATInputCode_FlagCheck1 | kATInputCode_FlagValue1)) {
			case 0:
			default:
				SetButtonTriState(IDC_FLAG2, 1);
				break;

			case kATInputCode_FlagCheck1:
				SetButtonTriState(IDC_FLAG2, 0);
				break;

			case kATInputCode_FlagCheck1 | kATInputCode_FlagValue1:
				SetButtonTriState(IDC_FLAG2, 2);
				break;
		}
	}
}

///////////////////////////////////////////////////////////////////////////

class ATUIDialogEditInputMapping : public VDDialogFrameW32 {
public:
	ATUIDialogEditInputMapping(ATInputManager& iman, IATJoystickManager *ijoy, uint32 inputCode, uint32 targetCode);

	void SetControllerType(ATInputControllerType ctype);

	void Set(uint32 inputCode, uint32 targetCode) {
		mInputCode = inputCode;
		mTargetCode = targetCode;
	}

	uint32 GetInputCode() const { return mInputCode; }
	uint32 GetTargetCode() { return mTargetCode; }

	static const vdspan<const uint32> GetTargetCodes(ATInputControllerType type);

protected:
	enum { kTimerId_JoyPoll = 100 };

	struct ModeEntry {
		int mMode;
		const wchar_t *mpName;
	};

	bool OnLoaded();
	void OnDestroy();
	void OnDataExchange(bool write);
	bool OnTimer(uint32 id);
	bool OnCommand(uint32 id, uint32 extcode);
	void OnHScroll(uint32 id, int code);
	void UpdateSpeedLabel();
	void UpdateAccelLabel();
	void UpdateSpeedAccelEnable();

	ATInputControllerType mControllerType;
	uint32 mInputCode;
	uint32 mTargetCode;

	ATInputManager& mInputMan;
	IATJoystickManager *const mpJoyMan;

	const uint32 *mpTargetCodes;
	uint32 mTargetCodeCount;

	static const uint32 kInputCodes[];
	static const uint32 kTargetCodesJoystick[];
	static const uint32 kTargetCodesPaddle[];
	static const uint32 kTargetCodesMouse[];
	static const uint32 kTargetCodes5200Controller[];
	static const uint32 kTargetCodesConsole[];
	static const uint32 kTargetCodesInputState[];
	static const uint32 kTargetCodesLightPenGun[];
	static const uint32 kTargetCodesTablet[];
	static const uint32 kTargetCodesKeypad[];
	static const uint32 kTargetCodesTrackballCX80V1[];
	static const uint32 kTargetCodes5200Trackball[];
	static const uint32 kTargetCodesDriving[];
	static const uint32 kTargetCodesKeyboard[];
	static const uint32 kTargetModes[];
};

const uint32 ATUIDialogEditInputMapping::kInputCodes[] = {
	kATInputCode_None,

	kATInputCode_KeyLShift,
	kATInputCode_KeyRShift,
	kATInputCode_KeyLControl,
	kATInputCode_KeyRControl,

	kATInputCode_KeyLeft,
	kATInputCode_KeyUp,
	kATInputCode_KeyRight,
	kATInputCode_KeyDown,

	kATInputCode_Key0,
	kATInputCode_Key1,
	kATInputCode_Key2,
	kATInputCode_Key3,
	kATInputCode_Key4,
	kATInputCode_Key5,
	kATInputCode_Key6,
	kATInputCode_Key7,
	kATInputCode_Key8,
	kATInputCode_Key9,

	kATInputCode_KeyA + 0,
	kATInputCode_KeyA + 1,
	kATInputCode_KeyA + 2,
	kATInputCode_KeyA + 3,
	kATInputCode_KeyA + 4,
	kATInputCode_KeyA + 5,
	kATInputCode_KeyA + 6,
	kATInputCode_KeyA + 7,
	kATInputCode_KeyA + 8,
	kATInputCode_KeyA + 9,
	kATInputCode_KeyA + 10,
	kATInputCode_KeyA + 11,
	kATInputCode_KeyA + 12,
	kATInputCode_KeyA + 13,
	kATInputCode_KeyA + 14,
	kATInputCode_KeyA + 15,
	kATInputCode_KeyA + 16,
	kATInputCode_KeyA + 17,
	kATInputCode_KeyA + 18,
	kATInputCode_KeyA + 19,
	kATInputCode_KeyA + 20,
	kATInputCode_KeyA + 21,
	kATInputCode_KeyA + 22,
	kATInputCode_KeyA + 23,
	kATInputCode_KeyA + 24,
	kATInputCode_KeyA + 25,


	kATInputCode_KeyBack,
	kATInputCode_KeyTab,
	kATInputCode_KeyReturn,
	kATInputCode_KeyEscape,
	kATInputCode_KeySpace,
	kATInputCode_KeyPrior,
	kATInputCode_KeyNext,
	kATInputCode_KeyEnd,
	kATInputCode_KeyHome,
	kATInputCode_KeyInsert,
	kATInputCode_KeyDelete,
	kATInputCode_KeyNumpad0,
	kATInputCode_KeyNumpad1,
	kATInputCode_KeyNumpad2,
	kATInputCode_KeyNumpad3,
	kATInputCode_KeyNumpad4,
	kATInputCode_KeyNumpad5,
	kATInputCode_KeyNumpad6,
	kATInputCode_KeyNumpad7,
	kATInputCode_KeyNumpad8,
	kATInputCode_KeyNumpad9,
	kATInputCode_KeyNumpadEnter,
	kATInputCode_KeyMultiply,
	kATInputCode_KeyAdd,
	kATInputCode_KeySubtract,
	kATInputCode_KeyDecimal,
	kATInputCode_KeyDivide,
	kATInputCode_KeyF1,
	kATInputCode_KeyF2,
	kATInputCode_KeyF3,
	kATInputCode_KeyF4,
	kATInputCode_KeyF5,
	kATInputCode_KeyF6,
	kATInputCode_KeyF7,
	kATInputCode_KeyF8,
	kATInputCode_KeyF9,
	kATInputCode_KeyF10,
	kATInputCode_KeyF11,
	kATInputCode_KeyF12,
	kATInputCode_KeyOem1,
	kATInputCode_KeyOemPlus,
	kATInputCode_KeyOemComma,
	kATInputCode_KeyOemMinus,
	kATInputCode_KeyOemPeriod,
	kATInputCode_KeyOem2,
	kATInputCode_KeyOem3,
	kATInputCode_KeyOem4,
	kATInputCode_KeyOem5,
	kATInputCode_KeyOem6,
	kATInputCode_KeyOem7,

	kATInputCode_MouseHoriz,
	kATInputCode_MouseVert,
	kATInputCode_MousePadX,
	kATInputCode_MousePadY,
	kATInputCode_MouseBeamX,
	kATInputCode_MouseBeamY,
	kATInputCode_MouseEmuStickX,
	kATInputCode_MouseEmuStickY,
	kATInputCode_MouseLeft,
	kATInputCode_MouseRight,
	kATInputCode_MouseUp,
	kATInputCode_MouseDown,
	kATInputCode_MouseLMB,
	kATInputCode_MouseMMB,
	kATInputCode_MouseRMB,
	kATInputCode_MouseX1B,
	kATInputCode_MouseX2B,
	kATInputCode_JoyHoriz1,
	kATInputCode_JoyVert1,
	kATInputCode_JoyVert2,
	kATInputCode_JoyHoriz3,
	kATInputCode_JoyVert3,
	kATInputCode_JoyVert4,
	kATInputCode_JoyPOVHoriz,
	kATInputCode_JoyPOVVert,
	kATInputCode_JoyStick1Left,
	kATInputCode_JoyStick1Right,
	kATInputCode_JoyStick1Up,
	kATInputCode_JoyStick1Down,
	kATInputCode_JoyStick2Up,
	kATInputCode_JoyStick2Down,
	kATInputCode_JoyStick3Left,
	kATInputCode_JoyStick3Right,
	kATInputCode_JoyStick3Up,
	kATInputCode_JoyStick3Down,
	kATInputCode_JoyStick4Up,
	kATInputCode_JoyStick4Down,
	kATInputCode_JoyPOVLeft,
	kATInputCode_JoyPOVRight,
	kATInputCode_JoyPOVUp,
	kATInputCode_JoyPOVDown,
	kATInputCode_JoyButton0+0,
	kATInputCode_JoyButton0+1,
	kATInputCode_JoyButton0+2,
	kATInputCode_JoyButton0+3,
	kATInputCode_JoyButton0+4,
	kATInputCode_JoyButton0+5,
	kATInputCode_JoyButton0+6,
	kATInputCode_JoyButton0+7,
	kATInputCode_JoyButton0+8,
	kATInputCode_JoyButton0+9,
	kATInputCode_JoyButton0+10,
	kATInputCode_JoyButton0+11,
	kATInputCode_JoyButton0+12,
	kATInputCode_JoyButton0+13,
	kATInputCode_JoyButton0+14,
	kATInputCode_JoyButton0+15,
	kATInputCode_JoyButton0+16,
	kATInputCode_JoyButton0+17,
	kATInputCode_JoyButton0+18,
	kATInputCode_JoyButton0+19,
	kATInputCode_JoyButton0+20,
	kATInputCode_JoyButton0+21,
	kATInputCode_JoyButton0+22,
	kATInputCode_JoyButton0+23,
	kATInputCode_JoyButton0+24,
	kATInputCode_JoyButton0+25,
	kATInputCode_JoyButton0+26,
	kATInputCode_JoyButton0+27,
	kATInputCode_JoyButton0+28,
	kATInputCode_JoyButton0+29,
	kATInputCode_JoyButton0+30,
	kATInputCode_JoyButton0+31,
};

const uint32 ATUIDialogEditInputMapping::kTargetCodesJoystick[] = {
	kATInputTrigger_Left,
	kATInputTrigger_Right,
	kATInputTrigger_Up,
	kATInputTrigger_Down,
	kATInputTrigger_Button0,
};

const uint32 ATUIDialogEditInputMapping::kTargetCodesPaddle[] = {
	kATInputTrigger_Left,
	kATInputTrigger_Right,
	kATInputTrigger_Axis0,
	kATInputTrigger_Axis0+1,
	kATInputTrigger_Axis0+2,
	kATInputTrigger_Button0,
};

const uint32 ATUIDialogEditInputMapping::kTargetCodesMouse[] = {
	kATInputTrigger_Axis0,
	kATInputTrigger_Axis0+1,
	kATInputTrigger_Left,
	kATInputTrigger_Right,
	kATInputTrigger_Up,
	kATInputTrigger_Down,
	kATInputTrigger_Button0,
	kATInputTrigger_Button0+1,
};

const uint32 ATUIDialogEditInputMapping::kTargetCodes5200Controller[] = {
	kATInputTrigger_Button0,
	kATInputTrigger_Button0+1,
	kATInputTrigger_Left,
	kATInputTrigger_Right,
	kATInputTrigger_Up,
	kATInputTrigger_Down,
	kATInputTrigger_Axis0,
	kATInputTrigger_Axis0+1,
	kATInputTrigger_5200_0,
	kATInputTrigger_5200_1,
	kATInputTrigger_5200_2,
	kATInputTrigger_5200_3,
	kATInputTrigger_5200_4,
	kATInputTrigger_5200_5,
	kATInputTrigger_5200_6,
	kATInputTrigger_5200_7,
	kATInputTrigger_5200_8,
	kATInputTrigger_5200_9,
	kATInputTrigger_5200_Star,
	kATInputTrigger_5200_Pound,
	kATInputTrigger_5200_Start,
	kATInputTrigger_5200_Reset,
	kATInputTrigger_5200_Pause
};

const uint32 ATUIDialogEditInputMapping::kTargetCodesConsole[] = {
	kATInputTrigger_Start,
	kATInputTrigger_Select,
	kATInputTrigger_Option,
	kATInputTrigger_Turbo,
	kATInputTrigger_ColdReset,
	kATInputTrigger_WarmReset,
	kATInputTrigger_KeySpace,
	kATInputTrigger_UILeft,
	kATInputTrigger_UIRight,
	kATInputTrigger_UIUp,
	kATInputTrigger_UIDown,
	kATInputTrigger_UIAccept,
	kATInputTrigger_UIReject,
	kATInputTrigger_UIMenu,
	kATInputTrigger_UIOption,
	kATInputTrigger_UISwitchLeft,
	kATInputTrigger_UISwitchRight,
	kATInputTrigger_UILeftShift,
	kATInputTrigger_UIRightShift,
};

const uint32 ATUIDialogEditInputMapping::kTargetCodesInputState[] = {
	kATInputTrigger_Flag0,
	kATInputTrigger_Flag0+1
};

const uint32 ATUIDialogEditInputMapping::kTargetCodesLightPenGun[] = {
	kATInputTrigger_Axis0,
	kATInputTrigger_Axis0+1,
	kATInputTrigger_Left,
	kATInputTrigger_Right,
	kATInputTrigger_Up,
	kATInputTrigger_Down,
	kATInputTrigger_Button0,
	kATInputTrigger_Button0+2,
};

const uint32 ATUIDialogEditInputMapping::kTargetCodesTablet[] = {
	kATInputTrigger_Axis0,
	kATInputTrigger_Axis0+1,
	kATInputTrigger_Left,
	kATInputTrigger_Right,
	kATInputTrigger_Up,
	kATInputTrigger_Down,
	kATInputTrigger_Button0,
	kATInputTrigger_Button0+1,
	kATInputTrigger_Button0+2,
	kATInputTrigger_Button0+3,
};

const uint32 ATUIDialogEditInputMapping::kTargetCodesKeypad[] = {
	kATInputTrigger_Button0,
	kATInputTrigger_Button0+1,
	kATInputTrigger_Button0+2,
	kATInputTrigger_Button0+3,
	kATInputTrigger_Button0+4,
	kATInputTrigger_Button0+5,
	kATInputTrigger_Button0+6,
	kATInputTrigger_Button0+7,
	kATInputTrigger_Button0+8,
	kATInputTrigger_Button0+9,
	kATInputTrigger_Button0+10,
	kATInputTrigger_Button0+11,
	kATInputTrigger_Button0+12,
	kATInputTrigger_Button0+13,
	kATInputTrigger_Button0+14,
	kATInputTrigger_Button0+15,
	kATInputTrigger_Button0+16,
};

const uint32 ATUIDialogEditInputMapping::kTargetCodesTrackballCX80V1[] = {
	kATInputTrigger_Axis0,
	kATInputTrigger_Axis0+1,
	kATInputTrigger_Left,
	kATInputTrigger_Right,
	kATInputTrigger_Up,
	kATInputTrigger_Down,
	kATInputTrigger_Button0,
};

const uint32 ATUIDialogEditInputMapping::kTargetCodes5200Trackball[] = {
	kATInputTrigger_Button0,
	kATInputTrigger_Button0+1,
	kATInputTrigger_Left,
	kATInputTrigger_Right,
	kATInputTrigger_Up,
	kATInputTrigger_Down,
	kATInputTrigger_Axis0,
	kATInputTrigger_Axis0+1,
	kATInputTrigger_5200_0,
	kATInputTrigger_5200_1,
	kATInputTrigger_5200_2,
	kATInputTrigger_5200_3,
	kATInputTrigger_5200_4,
	kATInputTrigger_5200_5,
	kATInputTrigger_5200_6,
	kATInputTrigger_5200_7,
	kATInputTrigger_5200_8,
	kATInputTrigger_5200_9,
	kATInputTrigger_5200_Star,
	kATInputTrigger_5200_Pound,
	kATInputTrigger_5200_Start,
	kATInputTrigger_5200_Reset,
	kATInputTrigger_5200_Pause
};

const uint32 ATUIDialogEditInputMapping::kTargetCodesKeyboard[] = {
	kATInputTrigger_Button0,
	kATInputTrigger_Button0+1,
	kATInputTrigger_Button0+2,
	kATInputTrigger_Button0+3,
	kATInputTrigger_Button0+4,
	kATInputTrigger_Button0+5,
	kATInputTrigger_Button0+6,
	kATInputTrigger_Button0+7,
	kATInputTrigger_Button0+8,
	kATInputTrigger_Button0+9,
	kATInputTrigger_Button0+10,
	kATInputTrigger_Button0+11,
};

const uint32 ATUIDialogEditInputMapping::kTargetModes[] = {
	kATInputTriggerMode_Default,
	kATInputTriggerMode_AutoFire,
	kATInputTriggerMode_Toggle,
	kATInputTriggerMode_ToggleAF,
	kATInputTriggerMode_Relative,
	kATInputTriggerMode_Absolute,
	kATInputTriggerMode_Inverted
};

const uint32 ATUIDialogEditInputMapping::kTargetCodesDriving[] = {
	kATInputTrigger_Axis0,
	kATInputTrigger_Left,
	kATInputTrigger_Right,
	kATInputTrigger_Button0,
};

ATUIDialogEditInputMapping::ATUIDialogEditInputMapping(ATInputManager& iman, IATJoystickManager *ijoy, uint32 inputCode, uint32 targetCode)
	: VDDialogFrameW32(IDD_INPUTMAP_ADDMAPPING)
	, mInputCode(inputCode)
	, mTargetCode(targetCode)
	, mInputMan(iman)
	, mpJoyMan(ijoy)
{
}

void ATUIDialogEditInputMapping::SetControllerType(ATInputControllerType ctype) {
	mControllerType = ctype;
}

const vdspan<const uint32> ATUIDialogEditInputMapping::GetTargetCodes(ATInputControllerType type) {
	switch(type) {
		case kATInputControllerType_Joystick:
		default:
			return vdspan<const uint32>(kTargetCodesJoystick);

		case kATInputControllerType_Paddle:
			return vdspan<const uint32>(kTargetCodesPaddle);

		case kATInputControllerType_STMouse:
		case kATInputControllerType_AmigaMouse:
			return vdspan<const uint32>(kTargetCodesMouse);

		case kATInputControllerType_5200Controller:
			return vdspan<const uint32>(kTargetCodes5200Controller);

		case kATInputControllerType_Console:
			return vdspan<const uint32>(kTargetCodesConsole);

		case kATInputControllerType_InputState:
			return vdspan<const uint32>(kTargetCodesInputState);

		case kATInputControllerType_LightPen:
		case kATInputControllerType_LightGun:
			return vdspan<const uint32>(kTargetCodesLightPenGun);

		case kATInputControllerType_Tablet:
		case kATInputControllerType_KoalaPad:
			return vdspan<const uint32>(kTargetCodesTablet);

		case kATInputControllerType_Keypad:
			return vdspan<const uint32>(kTargetCodesKeypad);

		case kATInputControllerType_Trackball_CX80_V1:
			return vdspan<const uint32>(kTargetCodesTrackballCX80V1);

		case kATInputControllerType_5200Trackball:
			return vdspan<const uint32>(kTargetCodes5200Trackball);

		case kATInputControllerType_Driving:
			return vdspan<const uint32>(kTargetCodesDriving);

		case kATInputControllerType_Keyboard:
			return vdspan<const uint32>(kTargetCodesKeyboard);
	}
}

bool ATUIDialogEditInputMapping::OnLoaded() {
	const vdspan<const uint32>& targetCodes = GetTargetCodes(mControllerType);

	mpTargetCodes = targetCodes.data();
	mTargetCodeCount = (uint32)targetCodes.size();

	VDStringW name;
	for(uint32 i=0; i<sizeof(kInputCodes)/sizeof(kInputCodes[0]); ++i) {
		mInputMan.GetNameForInputCode(kInputCodes[i], name);
		CBAddString(IDC_SOURCE, name.c_str());
	}

	for(uint32 i=0; i<mTargetCodeCount; ++i) {
		mInputMan.GetNameForTargetCode(mpTargetCodes[i], mControllerType, name);
		CBAddString(IDC_TARGET, name.c_str());
	}

	for(uint32 i=0; i<sizeof(kTargetModes)/sizeof(kTargetModes[0]); ++i) {
		name.clear();
		ATGetInputTriggerModeName(kTargetModes[i], false, name);
		CBAddString(IDC_MODE, name.c_str());
	}

	CBSetSelectedIndex(IDC_MODE, 0);

	TBSetRange(IDC_SPEED, 0, 10);
	TBSetValue(IDC_SPEED, 5);
	TBSetPageStep(IDC_SPEED, 1);

	TBSetRange(IDC_ACCEL, 0, 10);
	TBSetValue(IDC_ACCEL, 5);
	TBSetPageStep(IDC_ACCEL, 1);

	OnDataExchange(false);
	SetFocusToControl(IDC_SOURCE);

	if (mpJoyMan) {
		mpJoyMan->SetCaptureMode(true);
		SetPeriodicTimer(kTimerId_JoyPoll, 20);
	}
	return true;
}

void ATUIDialogEditInputMapping::OnDestroy() {
	if (mpJoyMan)
		mpJoyMan->SetCaptureMode(false);
}

void ATUIDialogEditInputMapping::OnDataExchange(bool write) {
	if (write) {
		const uint32 targetMode = kTargetModes[CBGetSelectedIndex(IDC_MODE)];

		mInputCode = kInputCodes[CBGetSelectedIndex(IDC_SOURCE)];
		mTargetCode = mpTargetCodes[CBGetSelectedIndex(IDC_TARGET)]
					+ targetMode;

		switch(targetMode) {
			case kATInputTriggerMode_AutoFire:
			case kATInputTriggerMode_ToggleAF:
			case kATInputTriggerMode_Relative:
				mTargetCode += TBGetValue(IDC_SPEED) << kATInputTriggerSpeed_Shift;
				mTargetCode += TBGetValue(IDC_ACCEL) << kATInputTriggerAccel_Shift;
				break;
		}
	} else {
		int selIdx = 0;
		for(uint32 i=0; i<sizeof(kInputCodes)/sizeof(kInputCodes[0]); ++i) {
			if (kInputCodes[i] == (mInputCode & 0xffff)) {
				selIdx = i;
				break;
			}
		}

		CBSetSelectedIndex(IDC_SOURCE, selIdx);

		selIdx = 0;
		for(uint32 i=0; i<mTargetCodeCount; ++i) {
			if (mpTargetCodes[i] == (mTargetCode & kATInputTrigger_Mask)) {
				selIdx = i;
				break;
			}
		}

		CBSetSelectedIndex(IDC_TARGET, selIdx);

		const uint32 *tmEnd = kTargetModes + sizeof(kTargetModes)/sizeof(kTargetModes[0]);
		const uint32 *tm = std::find(kTargetModes, tmEnd, mTargetCode & kATInputTriggerMode_Mask);

		CBSetSelectedIndex(IDC_MODE, tm != tmEnd ? (sint32)(tm - kTargetModes) : 0);

		switch(mTargetCode & kATInputTriggerMode_Mask) {
			case kATInputTriggerMode_AutoFire:
			case kATInputTriggerMode_ToggleAF:
			case kATInputTriggerMode_Relative:
				TBSetValue(IDC_SPEED, (mTargetCode & kATInputTriggerSpeed_Mask) >> kATInputTriggerSpeed_Shift);
				TBSetValue(IDC_ACCEL, (mTargetCode & kATInputTriggerAccel_Mask) >> kATInputTriggerAccel_Shift);
				break;

			default:
				TBSetValue(IDC_SPEED, 5);
				TBSetValue(IDC_ACCEL, 0);
				break;
		}

		UpdateSpeedLabel();
		UpdateAccelLabel();
		UpdateSpeedAccelEnable();
	}
}

bool ATUIDialogEditInputMapping::OnTimer(uint32 id) {
	if (id == kTimerId_JoyPoll) {
		if (mpJoyMan) {
			int unit;
			uint32 digitalInputCode;
			uint32 analogInputCode;
			if (mpJoyMan->PollForCapture(unit, digitalInputCode, analogInputCode)) {
				int analogIndex = -1;
				int digitalIndex = -1;
				for(uint32 i=0; i<sizeof(kInputCodes)/sizeof(kInputCodes[0]); ++i) {
					if (kInputCodes[i] == digitalInputCode)
						digitalIndex = i;

					if (kInputCodes[i] == analogInputCode)
						analogIndex = i;
				}

				if (analogIndex >= 0 && mInputMan.IsAnalogTrigger(mTargetCode, mControllerType))
					CBSetSelectedIndex(IDC_SOURCE, analogIndex);
				else if (digitalIndex >= 0)
					CBSetSelectedIndex(IDC_SOURCE, digitalIndex);
			}
		}
		return true;
	}

	return false;
}

bool ATUIDialogEditInputMapping::OnCommand(uint32 id, uint32 extcode) {
	switch(id) {
		case IDC_MODE:
			UpdateSpeedAccelEnable();
			break;
	}

	return false;
}

void ATUIDialogEditInputMapping::OnHScroll(uint32 id, int code) {
	if (id == IDC_SPEED)
		UpdateSpeedLabel();
	else if (id == IDC_ACCEL)
		UpdateAccelLabel();
}

void ATUIDialogEditInputMapping::UpdateSpeedLabel() {
	SetControlTextF(IDC_STATIC_SPEED, L"%d", TBGetValue(IDC_SPEED));
}

void ATUIDialogEditInputMapping::UpdateAccelLabel() {
	SetControlTextF(IDC_STATIC_ACCEL, L"%d", TBGetValue(IDC_ACCEL));
}

void ATUIDialogEditInputMapping::UpdateSpeedAccelEnable() {
	bool speedValid = false;

	switch(kTargetModes[CBGetSelectedIndex(IDC_MODE)]) {
		case kATInputTriggerMode_Relative:
		case kATInputTriggerMode_AutoFire:
		case kATInputTriggerMode_ToggleAF:
			speedValid = true;
			break;
	}

	EnableControl(IDC_SPEED, speedValid);
	EnableControl(IDC_STATIC_SPEED, speedValid);
	EnableControl(IDC_ACCEL, speedValid);
	EnableControl(IDC_STATIC_ACCEL, speedValid);
}

///////////////////////////////////////////////////////////////////////////

class ATUIDialogInputMapListItem;

class ATUIDialogInputMapControllerItem : public vdrefcounted<IVDUITreeViewVirtualItem> {
public:
	enum { kTypeID = 'adic' };

	ATUIDialogInputMapControllerItem(ATInputControllerType controllerType, int unit, uint32 flagCheckBits);
	~ATUIDialogInputMapControllerItem();

	void *AsInterface(uint32 id);

	void Set(ATInputControllerType controllerType, int unit, uint32 flagCheckBits);

	void SetNodeRef(VDUIProxyTreeViewControl::NodeRef ref) { mNodeRef = ref; }
	VDUIProxyTreeViewControl::NodeRef GetNodeRef() const { return mNodeRef; }

	ATInputControllerType GetControllerType() const { return mControllerType; }
	int GetControllerUnit() const { return mControllerUnit; }

	uint32 GetFlagCheckBits() const { return mFlagCheckBits; }

	uint32 GetItemCount() const { return (uint32)mMappings.size(); }
	ATUIDialogInputMapListItem *GetItem(uint32 i) const { return mMappings[i]; }
	sint32 GetIndexForItem(ATUIDialogInputMapListItem *item) const;

	void AddItem(ATUIDialogInputMapListItem *item);
	void RemoveItem(ATUIDialogInputMapListItem *item);
	void RemoveAllItems();

	void GetText(VDStringW& s) const;

protected:
	VDUIProxyTreeViewControl::NodeRef mNodeRef;
	uint32	mFlagCheckBits;

	ATInputControllerType	mControllerType;
	int		mControllerUnit;

	typedef vdfastvector<ATUIDialogInputMapListItem *> Mappings;
	Mappings mMappings;
};

class ATUIDialogInputMapListItem : public vdrefcounted<IVDUITreeViewVirtualItem> {
public:
	enum { kTypeID = 'adil' };

	ATUIDialogInputMapListItem(ATInputManager& iman, uint32 inputCode, uint32 targetCode);

	void *AsInterface(uint32 id);

	void SetNodeRef(VDUIProxyTreeViewControl::NodeRef ref) { mNodeRef = ref; }
	VDUIProxyTreeViewControl::NodeRef GetNodeRef() const { return mNodeRef; }

	void GetText(VDStringW& s) const;

	void Set(uint32 inputCode, uint32 targetCode);

	void SetController(ATUIDialogInputMapControllerItem *controller) { mpController = controller; }
	ATUIDialogInputMapControllerItem *GetController() const { return mpController; }

	uint32 GetInputCode() const { return mInputCode; }
	uint32 GetTargetCode() const { return mTargetCode; }

protected:
	VDUIProxyTreeViewControl::NodeRef mNodeRef;

	ATUIDialogInputMapControllerItem *mpController;
	uint32	mInputCode;
	uint32	mTargetCode;
	ATInputManager& mInputMan;
};

///////////////////////////////////////////////////////////////////////////

ATUIDialogInputMapControllerItem::ATUIDialogInputMapControllerItem(ATInputControllerType controllerType, int unit, uint32 flagCheckBits)
	: mNodeRef(NULL)
	, mFlagCheckBits(flagCheckBits)
	, mControllerType(controllerType)
	, mControllerUnit(unit)
{
}

ATUIDialogInputMapControllerItem::~ATUIDialogInputMapControllerItem() {
	RemoveAllItems();
}

void *ATUIDialogInputMapControllerItem::AsInterface(uint32 id) {
	if (id == ATUIDialogInputMapControllerItem::kTypeID)
		return this;

	return NULL;
}

void ATUIDialogInputMapControllerItem::Set(ATInputControllerType controllerType, int unit, uint32 flagCheckBits) {
	mControllerType = controllerType;
	mControllerUnit = unit;
	mFlagCheckBits = flagCheckBits;
}

sint32 ATUIDialogInputMapControllerItem::GetIndexForItem(ATUIDialogInputMapListItem *item) const {
	Mappings::const_iterator it(std::find(mMappings.begin(), mMappings.end(), item));

	if (it == mMappings.end())
		return -1;

	return (sint32)(it - mMappings.begin());
}

void ATUIDialogInputMapControllerItem::AddItem(ATUIDialogInputMapListItem *item) {
	item->SetController(this);
	item->AddRef();
	mMappings.push_back(item);
}

void ATUIDialogInputMapControllerItem::RemoveItem(ATUIDialogInputMapListItem *item) {
	Mappings::iterator it(std::find(mMappings.begin(), mMappings.end(), item));

	if (it != mMappings.end()) {
		item->SetController(NULL);

		mMappings.erase(it);
		item->Release();
	}
}

void ATUIDialogInputMapControllerItem::RemoveAllItems() {
	while(!mMappings.empty()) {
		ATUIDialogInputMapListItem *item = mMappings.back();
		mMappings.pop_back();

		item->Release();
	}
}

void ATUIDialogInputMapControllerItem::GetText(VDStringW& s) const {
	switch(mControllerType) {
		case kATInputControllerType_Joystick:
			if (mControllerUnit >= 4)
				s.sprintf(L"Joystick (MultiJoy #%d)", mControllerUnit - 3);
			else
				s.sprintf(L"Joystick (port %d)", mControllerUnit + 1);
			break;

		case kATInputControllerType_Paddle:
			s.sprintf(L"Paddle %c (port %d)", mControllerUnit & 1 ? 'B' : 'A', (mControllerUnit >> 1) + 1);
			break;

		case kATInputControllerType_STMouse:
			s.sprintf(L"ST Mouse (port %d)", mControllerUnit + 1);
			break;

		case kATInputControllerType_AmigaMouse:
			s.sprintf(L"Amiga Mouse (port %d)", mControllerUnit + 1);
			break;

		case kATInputControllerType_Console:
			s = L"Console";
			break;

		case kATInputControllerType_5200Controller:
			s.sprintf(L"5200 Controller (port %d)", mControllerUnit + 1);
			break;

		case kATInputControllerType_InputState:
			s = L"Input State";
			break;

		case kATInputControllerType_LightGun:
			s.sprintf(L"Light Gun (XG-1) (port %d)", mControllerUnit + 1);
			break;

		case kATInputControllerType_Tablet:
			s = L"Tablet (Atari Touch Tablet)";
			break;

		case kATInputControllerType_KoalaPad:
			s = L"Tablet (KoalaPad)";
			break;

		case kATInputControllerType_Keypad:
			s.sprintf(L"Numerical Keypad (port %d)", mControllerUnit + 1);
			break;

		case kATInputControllerType_Trackball_CX80_V1:
			s.sprintf(L"Trak-Ball (port %d)", mControllerUnit + 1);
			break;

		case kATInputControllerType_5200Trackball:
			s.sprintf(L"5200 Trak-Ball (port %d)", mControllerUnit + 1);
			break;

		case kATInputControllerType_Driving:
			s.sprintf(L"Driving Controller (port %d)", mControllerUnit + 1);
			break;

		case kATInputControllerType_Keyboard:
			s.sprintf(L"Keyboard Controller (port %d)", mControllerUnit + 1);
			break;

		case kATInputControllerType_LightPen:
			s.sprintf(L"Light Pen (CX-70/CX-75) (port %d)", mControllerUnit + 1);
			break;
	}

	if (mFlagCheckBits & kATInputCode_FlagCheck0)
		s.append_sprintf(L" (F1 %s)", mFlagCheckBits & kATInputCode_FlagValue0 ? L"on" : L"off");

	if (mFlagCheckBits & kATInputCode_FlagCheck1)
		s.append_sprintf(L" (F2 %s)", mFlagCheckBits & kATInputCode_FlagValue1 ? L"on" : L"off");
}

///////////////////////////////////////////////////////////////////////////

ATUIDialogInputMapListItem::ATUIDialogInputMapListItem(ATInputManager& iman, uint32 inputCode, uint32 targetCode)
	: mNodeRef(NULL)
	, mpController(NULL)
	, mInputCode(inputCode)
	, mTargetCode(targetCode)
	, mInputMan(iman)
{
}

void *ATUIDialogInputMapListItem::AsInterface(uint32 id) {
	if (id == ATUIDialogInputMapListItem::kTypeID)
		return this;

	return NULL;
}

void ATUIDialogInputMapListItem::GetText(VDStringW& s) const {
	VDStringW t;

	mInputMan.GetNameForInputCode(mInputCode, t);
	mInputMan.GetNameForTargetCode(mTargetCode & kATInputTrigger_Mask, mpController->GetControllerType(), s);

	s += L" -> ";
	s += t;

	uint32 mode = mTargetCode & kATInputTriggerMode_Mask;
	if (mode) {
		s += L" (";
		ATGetInputTriggerModeName(mTargetCode, true, s);
		s += L')';
	}
}

void ATUIDialogInputMapListItem::Set(uint32 inputCode, uint32 targetCode) {
	mInputCode		= inputCode;
	mTargetCode		= targetCode;
}

///////////////////////////////////////////////////////////////////////////

class ATUIDialogEditInputMap final : public VDResizableDialogFrameW32 {
public:
	ATUIDialogEditInputMap(ATInputManager& iman, IATJoystickManager *ijoy, ATInputMap& imap);
	~ATUIDialogEditInputMap();

protected:
	bool OnLoaded() override;
	void OnDestroy() override;
	void OnDataExchange(bool write) override;
	bool OnCommand(uint32 id, uint32 extcode) override;
	void OnSize() override;
	bool OnErase(VDZHDC hdc) override;
	void OnItemSelectionChanged(VDUIProxyTreeViewControl *source, int); 
	void OnItemDoubleClicked(VDUIProxyTreeViewControl *source, bool *handled);

	ATUIDialogInputMapControllerItem *CreateController(ATInputControllerType ctype, int unit, uint32 flagCheckBits);

	VDUIProxyTreeViewControl mTreeView;

	ATInputManager& mInputMan;
	IATJoystickManager *const mpJoyMan;
	ATInputMap& mInputMap;

	ATUIDialogEditInputMapping mEditMappingDialog;
	ATUIDialogEditControllerMapping mEditControllerDialog;
	VDDelegate mItemSelectedDelegate;
	VDDelegate mItemDoubleClickedDelegate;

	typedef vdfastvector<ATUIDialogInputMapControllerItem *> Controllers;
	Controllers mControllers;
};

ATUIDialogEditInputMap::ATUIDialogEditInputMap(ATInputManager& iman, IATJoystickManager *ijoy, ATInputMap& imap)
	: VDResizableDialogFrameW32(IDD_INPUTMAP_EDIT)
	, mInputMan(iman)
	, mpJoyMan(ijoy)
	, mInputMap(imap)
	, mEditMappingDialog(iman, ijoy, kATInputCode_JoyButton0, kATInputTrigger_Button0)
	, mEditControllerDialog(kATInputControllerType_Joystick, 0)
{
	mTreeView.OnItemSelectionChanged() += mItemSelectedDelegate.Bind(this, &ATUIDialogEditInputMap::OnItemSelectionChanged);
	mTreeView.OnItemDoubleClicked() += mItemDoubleClickedDelegate.Bind(this, &ATUIDialogEditInputMap::OnItemDoubleClicked);
}

ATUIDialogEditInputMap::~ATUIDialogEditInputMap() {
}

bool ATUIDialogEditInputMap::OnLoaded() {
	mResizer.Add(IDC_TREE, VDDialogResizerW32::kMC | VDDialogResizerW32::kAvoidFlicker);
	mResizer.Add(IDC_ADDCONTROLLER, VDDialogResizerW32::kBL);
	mResizer.Add(IDC_ADDMAPPING, VDDialogResizerW32::kBL);
	mResizer.Add(IDC_DELETE, VDDialogResizerW32::kBL);
	mResizer.Add(IDC_EDIT, VDDialogResizerW32::kBL);
	mResizer.Add(IDC_REBIND, VDDialogResizerW32::kBL);
	mResizer.Add(IDC_STATIC_GAMEPAD, VDDialogResizerW32::kBL);
	mResizer.Add(IDC_GAMEPAD, VDDialogResizerW32::kBC);
	mResizer.Add(IDOK, VDDialogResizerW32::kBR);
	mResizer.Add(IDCANCEL, VDDialogResizerW32::kBR);

	AddProxy(&mTreeView, IDC_TREE);

	VDStringW s;
	CBAddString(IDC_GAMEPAD, L"Any");
	for(int i=0; i<7; ++i) {
		const wchar_t *name = mInputMan.GetInputUnitName(i);

		s.sprintf(L"Game controller %d", i+1);

		if (name)
			s.append_sprintf(L" [%ls]", name);

		CBAddString(IDC_GAMEPAD, s.c_str());
	}

	OnDataExchange(false);

	SetFocusToControl(IDC_TREE);
	return true;
}

void ATUIDialogEditInputMap::OnDestroy() {
	mTreeView.Clear();

	while(!mControllers.empty()) {
		mControllers.back()->Release();
		mControllers.pop_back();
	}
}

void ATUIDialogEditInputMap::OnDataExchange(bool write) {
	if (write) {
		mInputMap.Clear();

		int nc = 0;

		for(Controllers::const_iterator itC(mControllers.begin()), itCEnd(mControllers.end());
			itC != itCEnd;
			++itC)
		{
			const ATUIDialogInputMapControllerItem *controller = *itC;
			const ATInputControllerType ctype = controller->GetControllerType();
			const int cunit = controller->GetControllerUnit();

			int cid = -1;
			for(int j=0; j<nc; ++j) {
				const ATInputMap::Controller& controller = mInputMap.GetController(j);
				if (controller.mType == ctype && controller.mIndex == cunit) {
					cid = j;
					break;
				}
			}

			if (cid < 0) {
				cid = nc++;
				mInputMap.AddController(ctype, cunit);
			}

			uint32 n = controller->GetItemCount();
			for(uint32 i=0; i<n; ++i) {
				ATUIDialogInputMapListItem *item = controller->GetItem(i);

				const uint32 inputCode = item->GetInputCode() + controller->GetFlagCheckBits();
				const uint32 targetCode = item->GetTargetCode();

				mInputMap.AddMapping(inputCode, cid, targetCode);
			}
		}

		mInputMap.SetSpecificInputUnit(CBGetSelectedIndex(IDC_GAMEPAD) - 1);
	} else {
		uint32 n = mInputMap.GetMappingCount();
		for(uint32 i=0; i<n; ++i) {
			const ATInputMap::Mapping& mapping = mInputMap.GetMapping(i);
			const ATInputMap::Controller& imc = mInputMap.GetController(mapping.mControllerId);
			ATUIDialogInputMapControllerItem *controller = CreateController(imc.mType, imc.mIndex, mapping.mInputCode & kATInputCode_FlagMask);

			vdrefptr<ATUIDialogInputMapListItem> item(new ATUIDialogInputMapListItem(mInputMan, mapping.mInputCode & kATInputCode_IdMask, mapping.mCode));

			controller->AddItem(item);

			item->SetNodeRef(mTreeView.AddVirtualItem(controller->GetNodeRef(), VDUIProxyTreeViewControl::kNodeLast, item));
		}

		CBSetSelectedIndex(IDC_GAMEPAD, mInputMap.GetSpecificInputUnit() + 1);
	}
}

bool ATUIDialogEditInputMap::OnCommand(uint32 id, uint32 extcode) {
	if (id == IDC_ADDCONTROLLER) {
		mEditControllerDialog.EnableDefaultOption(true);

		if (mEditControllerDialog.ShowDialog((VDGUIHandle)mhdlg)) {
			ATUIDialogInputMapControllerItem *controller = CreateController(mEditControllerDialog.GetControllerType(), mEditControllerDialog.GetControllerIndex(), mEditControllerDialog.GetFlagCheckBits());

			SetFocusToControl(IDC_TREE);

			mTreeView.MakeNodeVisible(controller->GetNodeRef());
			mTreeView.SelectNode(controller->GetNodeRef());

			if (mEditControllerDialog.IsDefaultRequested()) {
				const vdspan<const uint32>& mappings = ATUIDialogEditInputMapping::GetTargetCodes(mEditControllerDialog.GetControllerType());

				for(vdspan<const uint32>::const_iterator it(mappings.begin()), itEnd(mappings.end());
					it != itEnd;
					++it)
				{
					vdrefptr<ATUIDialogInputMapListItem> item(new ATUIDialogInputMapListItem(mInputMan
						, kATInputCode_None
						, *it
						));

					controller->AddItem(item);
					item->SetNodeRef(mTreeView.AddVirtualItem(controller->GetNodeRef(), VDUIProxyTreeViewControl::kNodeLast, item));
				}
			}
		}

		return true;
	} else if (id == IDC_ADDMAPPING) {
		vdrefptr<IVDUITreeViewVirtualItem> vi(mTreeView.GetSelectedVirtualItem());

		if (vi) {
			ATUIDialogInputMapControllerItem *ctitem = vdpoly_cast<ATUIDialogInputMapControllerItem *>(vi);

			if (!ctitem) {
				ATUIDialogInputMapListItem *mapitem = vdpoly_cast<ATUIDialogInputMapListItem *>(vi);
				if (!mapitem)
					return true;

				ctitem = mapitem->GetController();
			}

			mEditMappingDialog.SetControllerType(ctitem->GetControllerType());

			if (mEditMappingDialog.ShowDialog((VDGUIHandle)mhdlg)) {
				vdrefptr<ATUIDialogInputMapListItem> item(new ATUIDialogInputMapListItem(mInputMan
					, mEditMappingDialog.GetInputCode()
					, mEditMappingDialog.GetTargetCode()
					));

				ctitem->AddItem(item);
				item->SetNodeRef(mTreeView.AddVirtualItem(ctitem->GetNodeRef(), VDUIProxyTreeViewControl::kNodeLast, item));
			}
		}

		return true;
	} else if (id == IDC_DELETE) {
		vdrefptr<IVDUITreeViewVirtualItem> vi(mTreeView.GetSelectedVirtualItem());

		if (vi) {
			ATUIDialogInputMapListItem *mapitem = vdpoly_cast<ATUIDialogInputMapListItem *>(vi);
			if (mapitem) {
				mapitem->GetController()->RemoveItem(mapitem);

				mTreeView.DeleteItem(mapitem->GetNodeRef());
			} else {
				ATUIDialogInputMapControllerItem *ctitem = vdpoly_cast<ATUIDialogInputMapControllerItem *>(vi);
				if (ctitem) {
					Controllers::iterator it(std::find(mControllers.begin(), mControllers.end(), ctitem));

					if (it != mControllers.end()) {
						mControllers.erase(it);
						ctitem->Release();
					}

					mTreeView.DeleteItem(ctitem->GetNodeRef());
				}
			}
		}

		return true;
	} else if (id == IDC_EDIT) {
		bool handled = false;
		OnItemDoubleClicked(&mTreeView, &handled);
		return true;
	} else if (id == IDC_REBIND) {
		vdrefptr<IVDUITreeViewVirtualItem> vi(mTreeView.GetSelectedVirtualItem());

		if (!vi)
			return true;

		ATUIDialogInputMapControllerItem *ctitem = vdpoly_cast<ATUIDialogInputMapControllerItem *>(vi);
		uint32 firstIndex = 0;

		if (!ctitem) {
			ATUIDialogInputMapListItem *mapitem = vdpoly_cast<ATUIDialogInputMapListItem *>(vi);
			if (!mapitem)
				return true;

			ctitem = mapitem->GetController();

			sint32 firstIndexFound = ctitem->GetIndexForItem(mapitem);

			if (firstIndexFound >= 0)
				firstIndex = (uint32)firstIndexFound;
		}

		uint32 n = ctitem->GetItemCount();

		for(uint32 i = firstIndex; i < n; ++i) {
			ATUIDialogInputMapListItem *item = ctitem->GetItem(i);

			const VDUIProxyTreeViewControl::NodeRef& nodeRef = item->GetNodeRef();
			mTreeView.MakeNodeVisible(nodeRef);
			mTreeView.SelectNode(nodeRef);

			const vdrect32& r = GetControlScreenPos(IDC_TREE);

			uint32 inputCode;
			if (!ATUIShowRebindDialog((VDGUIHandle)mhdlg, mInputMan, mpJoyMan, item->GetTargetCode(), ctitem->GetControllerType(), vdpoint32(r.left, r.bottom), inputCode))
				break;

			item->Set(inputCode, item->GetTargetCode());
			mTreeView.RefreshNode(item->GetNodeRef());
		}

		return true;
	}

	return VDDialogFrameW32::OnCommand(id, extcode);
}

void ATUIDialogEditInputMap::OnSize() {
	mResizer.Relayout();
}

bool ATUIDialogEditInputMap::OnErase(VDZHDC hdc) {
	mResizer.Erase(&hdc);
	return true;
}

void ATUIDialogEditInputMap::OnItemSelectionChanged(VDUIProxyTreeViewControl *source, int) {
	bool selected = (mTreeView.GetSelectedVirtualItem() != NULL);
	EnableControl(IDC_DELETE, selected);
	EnableControl(IDC_EDIT, selected);
	EnableControl(IDC_REBIND, selected);
}

void ATUIDialogEditInputMap::OnItemDoubleClicked(VDUIProxyTreeViewControl *source, bool *handled) {
	vdrefptr<IVDUITreeViewVirtualItem> vi(mTreeView.GetSelectedVirtualItem());

	if (!vi)
		return;

	ATUIDialogInputMapListItem *mapitem = vdpoly_cast<ATUIDialogInputMapListItem *>(vi);
	if (mapitem) {
		mEditMappingDialog.SetControllerType(mapitem->GetController()->GetControllerType());
		mEditMappingDialog.Set(mapitem->GetInputCode(), mapitem->GetTargetCode());

		if (mEditMappingDialog.ShowDialog((VDGUIHandle)mhdlg)) {
			mapitem->Set(mEditMappingDialog.GetInputCode(), mEditMappingDialog.GetTargetCode());
			mTreeView.RefreshNode(mapitem->GetNodeRef());
		}

		*handled = true;
		return;
	}

	ATUIDialogInputMapControllerItem *ctitem = vdpoly_cast<ATUIDialogInputMapControllerItem *>(vi);
	if (ctitem) {
		mEditControllerDialog.EnableDefaultOption(false);
		mEditControllerDialog.Set(ctitem->GetControllerType(), ctitem->GetControllerUnit(), ctitem->GetFlagCheckBits());

		if (mEditControllerDialog.ShowDialog((VDGUIHandle)mhdlg)) {
			ctitem->Set(mEditControllerDialog.GetControllerType(), mEditControllerDialog.GetControllerIndex(), mEditControllerDialog.GetFlagCheckBits());
			mTreeView.RefreshNode(ctitem->GetNodeRef());

			// refresh all of the items in case the controller type changed
			uint32 n = ctitem->GetItemCount();
			for(uint32 i=0; i<n; ++i) {
				ATUIDialogInputMapListItem *item = ctitem->GetItem(i);

				if (item)
					mTreeView.RefreshNode(item->GetNodeRef());
			}
		}

		*handled = true;
		return;
	}
}

ATUIDialogInputMapControllerItem *ATUIDialogEditInputMap::CreateController(ATInputControllerType ctype, int unit, uint32 flagCheckBits) {
	for(Controllers::iterator it(mControllers.begin()), itEnd(mControllers.end()); it != itEnd; ++it) {
		ATUIDialogInputMapControllerItem *controller = *it;

		if (controller->GetControllerType() == ctype &&
			controller->GetControllerUnit() == unit &&
			controller->GetFlagCheckBits() == flagCheckBits
			)
			return controller;
	}

	vdrefptr<ATUIDialogInputMapControllerItem> newController(new ATUIDialogInputMapControllerItem(ctype, unit, flagCheckBits));

	newController->SetNodeRef(mTreeView.AddVirtualItem(VDUIProxyTreeViewControl::kNodeRoot, VDUIProxyTreeViewControl::kNodeLast, newController));

	mControllers.push_back(newController);
	return newController.release();
}

///////////////////////////////////////////////////////////////////////////

class ATUIDialogInputListItem : public vdrefcounted<IVDUIListViewVirtualItem> {
public:
	ATUIDialogInputListItem(ATInputMap *imap) : mpInputMap(imap) {}

	ATInputMap *GetInputMap() const { return mpInputMap; }

	void GetText(int subItem, VDStringW& s) const;

protected:
	vdrefptr<ATInputMap> mpInputMap;
};

void ATUIDialogInputListItem::GetText(int subItem, VDStringW& s) const {
	if (subItem == 0)
		s = mpInputMap->GetName();
	else if (subItem == 1) {
		int unit = mpInputMap->GetSpecificInputUnit();

		if (unit < 0)
			s = L"Any";
		else
			s.sprintf(L"%d", unit + 1);
	} else if (subItem == 2) {
		if (mpInputMap->IsQuickMap())
			s = L"Yes";
		else
			s.clear();
	}
}

///////////////////////////////////////////////////////////////////////////

class ATUIDialogInput : public VDDialogFrameW32 {
public:
	ATUIDialogInput(ATInputManager& iman, IATJoystickManager *ijoy);
	~ATUIDialogInput();

protected:
	bool OnLoaded();
	void OnDestroy();
	bool OnCommand(uint32 id, uint32 extcode);
	void OnDataExchange(bool write);
	void SortItems();
	void OnItemCheckedChanged(VDUIProxyListView *source, int index); 
	void OnItemSelectionChanged(VDUIProxyListView *source, int index); 
	void OnItemLabelChanged(VDUIProxyListView *source, VDUIProxyListView::LabelChangedEvent *event); 
	void OnItemDoubleClicked(VDUIProxyListView *source, int index); 

	VDUIProxyListView mListView;
	ATInputManager& mInputMan;
	IATJoystickManager *const mpJoyMan;

	VDDelegate mItemCheckedDelegate;
	VDDelegate mItemSelectedDelegate;
	VDDelegate mItemLabelEditedDelegate;
	VDDelegate mItemDblClkDelegate;
};

ATUIDialogInput::ATUIDialogInput(ATInputManager& iman, IATJoystickManager *ijoy)
	: VDDialogFrameW32(IDD_INPUT_MAPPINGS)
	, mInputMan(iman)
	, mpJoyMan(ijoy)
{
}

ATUIDialogInput::~ATUIDialogInput() {
}

bool ATUIDialogInput::OnLoaded() {
	AddProxy(&mListView, IDC_LIST);

	mListView.OnItemCheckedChanged() += mItemCheckedDelegate.Bind(this, &ATUIDialogInput::OnItemCheckedChanged);
	mListView.OnItemSelectionChanged() += mItemSelectedDelegate.Bind(this, &ATUIDialogInput::OnItemSelectionChanged);
	mListView.OnItemLabelChanged() += mItemLabelEditedDelegate.Bind(this, &ATUIDialogInput::OnItemLabelChanged);
	mListView.OnItemDoubleClicked() += mItemDblClkDelegate.Bind(this, &ATUIDialogInput::OnItemDoubleClicked);
	mListView.SetFullRowSelectEnabled(true);
	mListView.SetItemCheckboxesEnabled(true);
	mListView.InsertColumn(0, L"Name", 10);
	mListView.InsertColumn(1, L"Unit", 10);
	mListView.InsertColumn(2, L"Quick", 10);

	OnDataExchange(false);
	SetFocusToControl(IDC_LIST);
	return true;
}

void ATUIDialogInput::OnDestroy() {
	mListView.Clear();
}

bool ATUIDialogInput::OnCommand(uint32 id, uint32 extcode) {
	if (id == IDC_ADD) {
		vdrefptr<ATInputMap> newMap(new ATInputMap);

		VDStringW s;
		s.sprintf(L"Input map %d", mInputMan.GetInputMapCount() + 1);
		newMap->SetName(s.c_str());

		vdrefptr<IVDUIListViewVirtualItem> item(new ATUIDialogInputListItem(newMap));
		int idx = mListView.InsertVirtualItem(mListView.GetItemCount(), item);

		if (idx >= 0) {
			mInputMan.AddInputMap(newMap);

			SetFocusToControl(IDC_LIST);
			mListView.SetSelectedIndex(idx);
			mListView.EnsureItemVisible(idx);
			mListView.EditItemLabel(idx);
		}
	} else if (id == IDC_CLONE) {
		int index = mListView.GetSelectedIndex();
		if (index >= 0) {
			ATUIDialogInputListItem *item = static_cast<ATUIDialogInputListItem *>(mListView.GetVirtualItem(index));

			if (item) {
				ATInputMap *imap = item->GetInputMap();

				vdrefptr<ATInputMap> newMap(new ATInputMap(*imap));

				VDStringW s;
				s.sprintf(L"Input map %d", mInputMan.GetInputMapCount() + 1);
				newMap->SetName(s.c_str());

				vdrefptr<IVDUIListViewVirtualItem> item(new ATUIDialogInputListItem(newMap));
				int idx = mListView.InsertVirtualItem(mListView.GetItemCount(), item);

				if (idx >= 0) {
					mInputMan.AddInputMap(newMap);

					SetFocusToControl(IDC_LIST);
					mListView.SetSelectedIndex(idx);
					mListView.EnsureItemVisible(idx);
					mListView.EditItemLabel(idx);
				}
			}
		}
	} else if (id == IDC_EDIT) {
		int index = mListView.GetSelectedIndex();
		if (index >= 0) {
			ATUIDialogInputListItem *item = static_cast<ATUIDialogInputListItem *>(mListView.GetVirtualItem(index));

			if (item) {
				ATInputMap *imap = item->GetInputMap();

				if (imap) {
					ATUIDialogEditInputMap dlg(mInputMan, mpJoyMan, *imap);

					bool wasEnabled = mInputMan.IsInputMapEnabled(imap);

					if (wasEnabled)
						mInputMan.ActivateInputMap(imap, false);

					dlg.ShowDialog((VDGUIHandle)mhdlg);

					mInputMan.ActivateInputMap(imap, wasEnabled);
				}
			}
		}

		return true;
	} else if (id == IDC_DELETE) {
		int index = mListView.GetSelectedIndex();
		if (index >= 0) {
			ATUIDialogInputListItem *item = static_cast<ATUIDialogInputListItem *>(mListView.GetVirtualItem(index));

			if (item) {
				ATInputMap *imap = item->GetInputMap();

				VDStringW msg;
				msg.sprintf(L"Are you sure you want to delete the input map \"%ls\"?", imap->GetName());
				if (Confirm2("DeleteInputMap", msg.c_str(), L"Deleting input map")) {
					mListView.DeleteItem(index);
					mInputMan.RemoveInputMap(imap);
				}
			}
		}

		return true;
	} else if (id == IDC_RESET) {
		if (Confirm2("ResetInputMaps", L"This will erase all custom input maps and restore the default ones. Continue?", L"Reset input maps")) {
			mInputMan.ResetToDefaults();
			OnDataExchange(false);
		}

		return true;
	} else if (id == IDC_PRESETS) {
		uint32 n = mInputMan.GetPresetInputMapCount();

		vdfastvector<const wchar_t *> items(n+3, NULL);
		vdvector<vdrefptr<ATInputMap>> imaps(n);

		for(uint32 i = 0; i < n; ++i) {
			if (mInputMan.GetPresetInputMapByIndex(i, ~imaps[i]))
				items[i] = imaps[i]->GetName();
			else
				items[i] = L"?";
		}

		items[n] = L"---";
		items[n+1] = L"&Create From Template...";

		int sel = ActivateMenuButton(id, items.data());

		if (sel == (int)n+1) {
			ATUIDialogCreateInputMap createdlg(mInputMan);

			if (createdlg.ShowDialog(this)) {
				OnDataExchange(false);

				int n = mListView.GetItemCount();
				for(int i=0; i<n; ++i) {
					auto *item = static_cast<ATUIDialogInputListItem *>(mListView.GetVirtualItem(i));

					if (item && item->GetInputMap() == createdlg.GetInputMap()) {
						mListView.SetSelectedIndex(i);
						break;
					}
				}

				SetFocusToControl(mListView.GetWindowId());
			}
		} else if (sel >= 0 && sel < (int)n) {
			vdrefptr<ATInputMap> selimap;

			if (mInputMan.GetPresetInputMapByIndex((uint32)sel, ~selimap)) {
				vdrefptr<ATInputMap> newMap(new ATInputMap(*selimap));

				vdrefptr<IVDUIListViewVirtualItem> item(new ATUIDialogInputListItem(newMap));
				int idx = mListView.InsertVirtualItem(mListView.GetItemCount(), item);

				if (idx >= 0) {
					mInputMan.AddInputMap(newMap);

					SetFocusToControl(IDC_LIST);
					mListView.SetSelectedIndex(idx);
					mListView.EnsureItemVisible(idx);
					mListView.EditItemLabel(idx);
				}
			}
		}

		return true;
	} else if (id == IDC_QUICKMAP) {
		ATUIDialogInputListItem *item = static_cast<ATUIDialogInputListItem *>(mListView.GetSelectedVirtualItem());

		if (item) {
			ATInputMap *imap = item->GetInputMap();
			bool quick = IsButtonChecked(IDC_QUICKMAP);

			if (imap->IsQuickMap() != quick) {
				imap->SetQuickMap(quick);

				mListView.RefreshItem(mListView.GetSelectedIndex());
			}
		}

		return true;
	}

	return VDDialogFrameW32::OnCommand(id, extcode);
}

void ATUIDialogInput::OnDataExchange(bool write) {
	if (!write) {
		mListView.Clear();

		uint32 n = mInputMan.GetInputMapCount();

		for(uint32 i=0; i<n; ++i) {
			vdrefptr<ATInputMap> imap;
			mInputMan.GetInputMapByIndex(i, ~imap);

			vdrefptr<IVDUIListViewVirtualItem> item(new ATUIDialogInputListItem(imap));
			int idx = mListView.InsertVirtualItem(i, item);

			if (idx >= 0)
				mListView.SetItemChecked(idx, mInputMan.IsInputMapEnabled(imap));
		}

		SortItems();

		mListView.AutoSizeColumns();
	}
}

namespace {
	struct InputMapComparer : public IVDUIListViewVirtualComparer {
		int Compare(IVDUIListViewVirtualItem *x, IVDUIListViewVirtualItem *y) {
			ATUIDialogInputListItem *a = static_cast<ATUIDialogInputListItem *>(x);
			ATUIDialogInputListItem *b = static_cast<ATUIDialogInputListItem *>(y);
			
			return vdwcsicmp(a->GetInputMap()->GetName(), b->GetInputMap()->GetName());
		}
	};
}

void ATUIDialogInput::SortItems() {
	InputMapComparer comparer;
	mListView.Sort(comparer);
}

void ATUIDialogInput::OnItemCheckedChanged(VDUIProxyListView *source, int index) {
	ATUIDialogInputListItem *item = static_cast<ATUIDialogInputListItem *>(mListView.GetVirtualItem(index));

	if (item) {
		ATInputMap *imap = item->GetInputMap();

		mInputMan.ActivateInputMap(imap, mListView.IsItemChecked(index));
	}
}

void ATUIDialogInput::OnItemSelectionChanged(VDUIProxyListView *source, int index) {
	EnableControl(IDC_DELETE, index >= 0);
	EnableControl(IDC_EDIT, index >= 0);
	EnableControl(IDC_CLONE, index >= 0);
	EnableControl(IDC_QUICKMAP, index >= 0);
	
	if (index >= 0) {
		ATUIDialogInputListItem *item = static_cast<ATUIDialogInputListItem *>(mListView.GetVirtualItem(index));

		if (item)
			CheckButton(IDC_QUICKMAP, item->GetInputMap()->IsQuickMap());
	}
}

void ATUIDialogInput::OnItemLabelChanged(VDUIProxyListView *source, VDUIProxyListView::LabelChangedEvent *event) {
	if (!event->mpNewLabel)
		return;

	ATUIDialogInputListItem *item = static_cast<ATUIDialogInputListItem *>(mListView.GetVirtualItem(event->mIndex));

	if (item) {
		ATInputMap *imap = item->GetInputMap();

		VDStringW s;
		mListView.GetItemText(event->mIndex, s);
		imap->SetName(event->mpNewLabel);
		mListView.RefreshItem(event->mIndex);
	}
}

void ATUIDialogInput::OnItemDoubleClicked(VDUIProxyListView *source, int index) {
	OnCommand(IDC_EDIT, 0);
}

void ATUIShowDialogInputMappings(VDZHWND parent, ATInputManager& iman, IATJoystickManager *ijoy) {
	ATUIDialogInput dlg(iman, ijoy);

	dlg.ShowDialog((VDGUIHandle)parent);
}
