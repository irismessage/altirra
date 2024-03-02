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

#include "stdafx.h"
#include <vd2/system/bitmath.h>
#include <vd2/system/registry.h>
#include <vd2/system/strutil.h>
#include <vd2/Dita/accel.h>
#include "inputmanager.h"
#include "inputcontroller.h"
#include "joystick.h"

namespace {
	const int kSpeedScaleTable[11]={
		84,
		105,
		131,
		164,
		205,
		256,
		320,
		400,
		500,
		625,
		781
	};
}

ATInputMap::ATInputMap()
	: mSpecificInputUnit(-1)
{
}

ATInputMap::~ATInputMap() {
}

const wchar_t *ATInputMap::GetName() const {
	return mName.c_str();
}

void ATInputMap::SetName(const wchar_t *name) {
	mName = name;
}

bool ATInputMap::UsesPhysicalPort(int portIdx) const {
	for(Controllers::const_iterator it(mControllers.begin()), itEnd(mControllers.end()); it != itEnd; ++it) {
		const Controller& c = *it;

		switch(c.mType) {
			case kATInputControllerType_Joystick:
			case kATInputControllerType_STMouse:
			case kATInputControllerType_5200Controller:
			case kATInputControllerType_LightPen:
			case kATInputControllerType_Tablet:
			case kATInputControllerType_KoalaPad:
			case kATInputControllerType_AmigaMouse:
			case kATInputControllerType_Keypad:
			case kATInputControllerType_Trackball_CX80_V1:
			case kATInputControllerType_5200Trackball:
				if (c.mIndex == portIdx)
					return true;
				break;

			case kATInputControllerType_Paddle:
				if ((c.mIndex >> 1) == portIdx)
					return true;
				break;
		}
	}

	return false;
}

void ATInputMap::Clear() {
	mControllers.clear();
	mMappings.clear();
	mSpecificInputUnit = -1;
}

uint32 ATInputMap::GetControllerCount() const {
	return mControllers.size();
}

const ATInputMap::Controller& ATInputMap::GetController(uint32 i) const {
	return mControllers[i];
}

uint32 ATInputMap::AddController(ATInputControllerType type, uint32 index) {
	uint32 cindex = mControllers.size();
	Controller& c = mControllers.push_back();

	c.mType = type;
	c.mIndex = index;

	return cindex;
}

uint32 ATInputMap::GetMappingCount() const {
	return mMappings.size();
}

const ATInputMap::Mapping& ATInputMap::GetMapping(uint32 i) const {
	return mMappings[i];
}

void ATInputMap::AddMapping(uint32 inputCode, uint32 controllerId, uint32 code) {
	Mapping& m = mMappings.push_back();

	m.mInputCode = inputCode;
	m.mControllerId = controllerId;
	m.mCode = code;
}

bool ATInputMap::Load(VDRegistryKey& key, const char *name) {
	int len = key.getBinaryLength(name);

	if (len < 16)
		return false;

	vdfastvector<uint32> heap;
	const uint32 heapWords = (len + 3) >> 2;
	heap.resize(heapWords, 0);

	if (!key.getBinary(name, (char *)heap.data(), len))
		return false;

	const uint32 version = heap[0];
	uint32 headerWords = 4;
	if (version == 2)
		headerWords = 5;
	else if (version != 1)
		return false;

	uint32 nameLen = heap[1];
	uint32 nameWords = (nameLen + 1) >> 1;
	uint32 ctrlCount = heap[2];
	uint32 mapCount = heap[3];

	if (headerWords >= 5) {
		mSpecificInputUnit = heap[4];
	} else {
		mSpecificInputUnit = -1;
	}

	if (((nameLen | ctrlCount | mapCount) & 0xff000000) || headerWords + nameWords + 2*ctrlCount + 3*mapCount > heapWords)
		return false;

	const uint32 *src = heap.data() + headerWords;

	mName.assign((const wchar_t *)src, (const wchar_t *)src + nameLen);
	src += nameWords;

	mControllers.resize(ctrlCount);
	for(uint32 i=0; i<ctrlCount; ++i) {
		Controller& c = mControllers[i];

		c.mType = (ATInputControllerType)src[0];
		c.mIndex = src[1];
		src += 2;
	}

	mMappings.resize(mapCount);
	for(uint32 i=0; i<mapCount; ++i) {
		Mapping& m = mMappings[i];

		m.mInputCode = src[0];
		m.mControllerId = src[1];
		m.mCode = src[2];
		src += 3;
	}

	return true;
}

void ATInputMap::Save(VDRegistryKey& key, const char *name) {
	vdfastvector<uint32> heap;

	heap.push_back(2);
	heap.push_back(mName.size());
	heap.push_back(mControllers.size());
	heap.push_back(mMappings.size());
	heap.push_back(mSpecificInputUnit);

	uint32 offset = heap.size();
	heap.resize(heap.size() + ((mName.size() + 1) >> 1), 0);

	mName.copy((wchar_t *)&heap[offset], mName.size());

	for(Controllers::const_iterator it(mControllers.begin()), itEnd(mControllers.end()); it != itEnd; ++it) {
		const Controller& c = *it;

		heap.push_back(c.mType);
		heap.push_back(c.mIndex);
	}

	for(Mappings::const_iterator it(mMappings.begin()), itEnd(mMappings.end()); it != itEnd; ++it) {
		const Mapping& m = *it;

		heap.push_back(m.mInputCode);
		heap.push_back(m.mControllerId);
		heap.push_back(m.mCode);
	}

	key.setBinary(name, (const char *)heap.data(), heap.size() * 4);
}

///////////////////////////////////////////////////////////////////////

ATInputManager::ATInputManager()
	: mpSlowScheduler(NULL)
	, mpFastScheduler(NULL)
	, mpLightPen(NULL)
	, mAllocatedUnits(0)
	, mpCB(NULL)
	, m5200ControllerIndex(0)
	, mb5200PotsEnabled(true)
	, mbMouseMapped(false)
	, mbMouseAbsMode(false)
	, mbMouseActiveTarget(false)
	, mMouseAvgIndex(0)
{
	InitPresetMaps();

	mpPorts[0] = NULL;
	mpPorts[1] = NULL;

	std::fill(mMouseAvgQueue, mMouseAvgQueue + sizeof(mMouseAvgQueue)/sizeof(mMouseAvgQueue[0]), 0x20002000);
}

ATInputManager::~ATInputManager() {
	while(!mPresetMaps.empty()) {
		mPresetMaps.back()->Release();
		mPresetMaps.pop_back();
	}
}

void ATInputManager::Init(ATScheduler *fastSched, ATScheduler *slowSched, ATPortController *porta, ATPortController *portb, ATLightPenPort *lightPen) {
	mpLightPen = lightPen;
	mpSlowScheduler = slowSched;
	mpFastScheduler = fastSched;
	mpPorts[0] = porta;
	mpPorts[1] = portb;

	ResetToDefaults();
}

void ATInputManager::Shutdown() {
	RemoveAllInputMaps();
}

void ATInputManager::ResetToDefaults() {
	RemoveAllInputMaps();

	for(PresetMaps::const_iterator it(mPresetMaps.begin()), itEnd(mPresetMaps.end()); it != itEnd; ++it) {
		AddInputMap(*it);
	}
}

void ATInputManager::Select5200Controller(int index, bool potsEnabled) {
	if (m5200ControllerIndex != index || mb5200PotsEnabled != potsEnabled) {
		m5200ControllerIndex = index;
		mb5200PotsEnabled = potsEnabled;
		Update5200Controller();
	}
}

void ATInputManager::SelectMultiJoy(int multiIndex) {
	if (multiIndex < 0)
		mpPorts[0]->SetMultiMask(0xFF);
	else
		mpPorts[0]->SetMultiMask(1 << multiIndex);
}

void ATInputManager::Update5200Controller() {
	// We do two passes here to make sure that everything is disconnected before
	// we connect a new controller.
	for(InputControllers::const_iterator it(mInputControllers.begin()), itEnd(mInputControllers.end()); it != itEnd; ++it) {
		ATPortInputController *pc = it->mpInputController;
		pc->Select5200Controller(-1, false);
	}

	for(InputControllers::const_iterator it(mInputControllers.begin()), itEnd(mInputControllers.end()); it != itEnd; ++it) {
		ATPortInputController *pc = it->mpInputController;
		pc->Select5200Controller(m5200ControllerIndex, mb5200PotsEnabled);
	}
}

void ATInputManager::Poll() {
	uint32 avgres = ((mMouseAvgQueue[0] + mMouseAvgQueue[1] + mMouseAvgQueue[2] + mMouseAvgQueue[3] + 0x00020002) & 0xfffcfffc) >> 2;
	int avgx = (avgres & 0xffff) - 0x2000;
	int avgy = (avgres >> 16) - 0x2000;
	int avgax = abs(avgx);
	int avgay = abs(avgy);

	// tan 22.5 deg = 0.4142135623730950488016887242097 ~= 53/128
	if (avgax * 53 >= avgay * 128)
		avgy = 0;

	if (avgay * 53 >= avgax * 128)
		avgx = 0;

	if (avgx < 0)
		OnButtonDown(0, kATInputCode_MouseLeft);
	else
		OnButtonUp(0, kATInputCode_MouseLeft);

	if (avgx > 0)
		OnButtonDown(0, kATInputCode_MouseRight);
	else
		OnButtonUp(0, kATInputCode_MouseRight);

	if (avgy < 0)
		OnButtonDown(0, kATInputCode_MouseUp);
	else
		OnButtonUp(0, kATInputCode_MouseUp);

	if (avgy > 0)
		OnButtonDown(0, kATInputCode_MouseDown);
	else
		OnButtonUp(0, kATInputCode_MouseDown);

	mMouseAvgQueue[++mMouseAvgIndex & 3] = 0x20002000;

	for(Mappings::iterator it(mMappings.begin()), itEnd(mMappings.end()); it != itEnd; ++it) {
		Mapping& mapping = it->second;

		if (!mapping.mbMotionActive)
			continue;

		Trigger& trigger = mTriggers[mapping.mTriggerIdx];
		const uint32 mode = trigger.mId & kATInputTriggerMode_Mask;

		switch(mode) {
			case kATInputTriggerMode_AutoFire:
			case kATInputTriggerMode_ToggleAF:
				if (++mapping.mAccel >= mapping.mDamping) {
					mapping.mAccel = 0;

					mapping.mValue = !mapping.mValue;

					bool newState = (mapping.mValue != 0);

					SetTrigger(mapping, newState);
				}
				break;

			case kATInputTriggerMode_Relative:
				if (!mapping.mAccel && !mapping.mValue) {
					mapping.mbMotionActive = false;
				} else {
					mapping.mValue += mapping.mAccel;
					mapping.mValue = (mapping.mValue * mapping.mDamping + 0x80) >> 8;

					trigger.mpController->ApplyImpulse(trigger.mId & kATInputTrigger_Mask, (mapping.mValue + 0x80) >> 8);
				}
				break;
		}
	}

	mbMouseActiveTarget = false;

	for(InputControllers::iterator it(mInputControllers.begin()), itEnd(mInputControllers.end()); it != itEnd; ++it) {
		ATPortInputController *c = it->mpInputController;

		c->Tick();

		if (it->mbBoundToMouseAbs && c->IsActive())
			mbMouseActiveTarget = true;
	}
}

int ATInputManager::GetInputUnitCount() const {
	return 32;
}

const wchar_t *ATInputManager::GetInputUnitName(int index) const {
	if ((unsigned)index >= 32)
		return NULL;

	if (!(mAllocatedUnits & ((uint32)1 << index)))
		return NULL;

	return mUnitNames[index].c_str();
}

int ATInputManager::GetInputUnitIndexById(const ATInputUnitIdentifier& id) const {
	for(int i=0; i<32; ++i) {
		if (!(mAllocatedUnits & ((uint32)1 << i)))
			continue;

		if (mUnitIds[i] == id)
			return i;
	}

	return -1;
}

int ATInputManager::RegisterInputUnit(const ATInputUnitIdentifier& id, const wchar_t *name) {
	if (mAllocatedUnits == 0xFFFFFFFF)
		return -1;

	int unit = VDFindLowestSetBitFast(~mAllocatedUnits);

	mAllocatedUnits |= (1 << unit);
	mUnitIds[unit] = id;
	mUnitNames[unit] = name;

	return unit;
}

void ATInputManager::UnregisterInputUnit(int unit) {
	if (unit < 0)
		return;

	VDASSERT(unit < 32);
	uint32 bit = (1 << unit);

	VDASSERT(mAllocatedUnits & bit);

	mAllocatedUnits &= ~bit;
}

bool ATInputManager::IsInputMapped(int unit, uint32 inputCode) const {
	return mMappings.find(inputCode) != mMappings.end()
		|| mMappings.find(inputCode | kATInputCode_SpecificUnit | (unit << kATInputCode_UnitShift)) != mMappings.end();
}

void ATInputManager::OnButtonDown(int unit, int id) {
	Buttons::iterator it(mButtons.insert(Buttons::value_type(id, 0)).first);
	uint32 oldVal = it->second;
	
	const uint32 bit = (1 << unit);

	if (!(it->second & bit)) {
		it->second |= bit;

		ActivateMappings(id | kATInputCode_SpecificUnit | (unit << kATInputCode_UnitShift), true);

		if (!oldVal)
			ActivateMappings(id, true);
	}
}

void ATInputManager::OnButtonUp(int unit, int id) {
	Buttons::iterator it(mButtons.find(id));
	if (it == mButtons.end())
		return;

	const uint32 bit = (1 << unit);

	if (it->second & bit) {
		it->second &= ~bit;

		ActivateMappings(id | kATInputCode_SpecificUnit | (unit << kATInputCode_UnitShift), false);

		if (!it->second) 
			ActivateMappings(id, false);
	}
}

void ATInputManager::OnAxisInput(int unit, int axis, sint32 value, sint32 deadifiedValue) {
	ActivateAnalogMappings(axis, value, deadifiedValue);
	ActivateAnalogMappings(axis | kATInputCode_SpecificUnit | (unit << kATInputCode_UnitShift), value, deadifiedValue);
}

void ATInputManager::OnMouseMove(int unit, int dx, int dy) {
	mMouseAvgQueue[mMouseAvgIndex & 3] += (uint32)(dx + (dy << 16));

	// Scale dx/dy to something reasonable in the +-1 range.
	dx *= 0x100;
	dy *= 0x100;

	ActivateImpulseMappings(kATInputCode_MouseHoriz, dx);
	ActivateImpulseMappings(kATInputCode_MouseHoriz | kATInputCode_SpecificUnit | (unit << kATInputCode_UnitShift), dx);
	ActivateImpulseMappings(kATInputCode_MouseVert, dy);
	ActivateImpulseMappings(kATInputCode_MouseVert | kATInputCode_SpecificUnit | (unit << kATInputCode_UnitShift), dy);
}

void ATInputManager::SetMouseBeamPos(int beamX, int beamY) {
	ActivateAnalogMappings(kATInputCode_MouseBeamX, beamX, beamX);
	ActivateAnalogMappings(kATInputCode_MouseBeamX | kATInputCode_SpecificUnit, beamX, beamX);
	ActivateAnalogMappings(kATInputCode_MouseBeamY, beamY, beamY);
	ActivateAnalogMappings(kATInputCode_MouseBeamY | kATInputCode_SpecificUnit, beamY, beamY);
}

void ATInputManager::SetMousePadPos(int padX, int padY) {
	ActivateAnalogMappings(kATInputCode_MousePadX, padX, padX);
	ActivateAnalogMappings(kATInputCode_MousePadX | kATInputCode_SpecificUnit, padX, padX);
	ActivateAnalogMappings(kATInputCode_MousePadY, padY, padY);
	ActivateAnalogMappings(kATInputCode_MousePadY | kATInputCode_SpecificUnit, padY, padY);
}

void ATInputManager::GetNameForInputCode(uint32 code, VDStringW& name) const {
	code &= 0xffff;

	switch(code) {
		case kATInputCode_None:
			name = L"None";
			break;

		case kATInputCode_KeyLShift:
			name = L"Key: Left Shift";
			break;

		case kATInputCode_KeyRShift:
			name = L"Key: Right Shift";
			break;

		case kATInputCode_KeyLControl:
			name = L"Key: Left Ctrl";
			break;

		case kATInputCode_KeyRControl:
			name = L"Key: Right Ctrl";
			break;

		case kATInputCode_MouseHoriz:
			name = L"Mouse Move Horiz";
			break;

		case kATInputCode_MouseVert:
			name = L"Mouse Move Vert";
			break;

		case kATInputCode_MousePadX:
			name = L"Mouse Pos X (pad)";
			break;

		case kATInputCode_MousePadY:
			name = L"Mouse Pos Y (pad)";
			break;

		case kATInputCode_MouseBeamX:
			name = L"Mouse Pos X (light pen)";
			break;

		case kATInputCode_MouseBeamY:
			name = L"Mouse Pos Y (light pen)";
			break;

		case kATInputCode_MouseLeft:
			name = L"Mouse Left";
			break;
		case kATInputCode_MouseRight:
			name = L"Mouse Right";
			break;
		case kATInputCode_MouseUp:
			name = L"Mouse Up";
			break;
		case kATInputCode_MouseDown:
			name = L"Mouse Down";
			break;
		case kATInputCode_MouseLMB:
			name = L"Mouse LMB";
			break;
		case kATInputCode_MouseMMB:
			name = L"Mouse MMB";
			break;
		case kATInputCode_MouseRMB:
			name = L"Mouse RMB";
			break;
		case kATInputCode_MouseX1B:
			name = L"Mouse X1B";
			break;
		case kATInputCode_MouseX2B:
			name = L"Mouse X2B";
			break;
		case kATInputCode_JoyHoriz1:
			name = L"Joy Axis 1H";
			break;
		case kATInputCode_JoyVert1:
			name = L"Joy Axis 1V";
			break;
		case kATInputCode_JoyVert2:
			name = L"Joy Axis 2V";
			break;
		case kATInputCode_JoyHoriz3:
			name = L"Joy Axis 3H";
			break;
		case kATInputCode_JoyVert3:
			name = L"Joy Axis 3V";
			break;
		case kATInputCode_JoyVert4:
			name = L"Joy Axis 4V";
			break;
		case kATInputCode_JoyPOVHoriz:
			name = L"Joy POV H";
			break;
		case kATInputCode_JoyPOVVert:
			name = L"Joy POV V";
			break;
		case kATInputCode_JoyStick1Left:
			name = L"Joy Axis 1L";
			break;
		case kATInputCode_JoyStick1Right:
			name = L"Joy Axis 1R";
			break;
		case kATInputCode_JoyStick1Up:
			name = L"Joy Axis 1U";
			break;
		case kATInputCode_JoyStick1Down:
			name = L"Joy Axis 1D";
			break;
		case kATInputCode_JoyStick2Up:
			name = L"Joy Axis 2U";
			break;
		case kATInputCode_JoyStick2Down:
			name = L"Joy Axis 2D";
			break;
		case kATInputCode_JoyStick3Left:
			name = L"Joy Axis 3L";
			break;
		case kATInputCode_JoyStick3Right:
			name = L"Joy Axis 3R";
			break;
		case kATInputCode_JoyStick3Up:
			name = L"Joy Axis 3U";
			break;
		case kATInputCode_JoyStick3Down:
			name = L"Joy Axis 3D";
			break;
		case kATInputCode_JoyStick4Up:
			name = L"Joy Axis 4U";
			break;
		case kATInputCode_JoyStick4Down:
			name = L"Joy Axis 4D";
			break;
		case kATInputCode_JoyPOVLeft:
			name = L"Joy POV Left";
			break;
		case kATInputCode_JoyPOVRight:
			name = L"Joy POV Right";
			break;
		case kATInputCode_JoyPOVUp:
			name = L"Joy POV Up";
			break;
		case kATInputCode_JoyPOVDown:
			name = L"Joy POV Down";
			break;

		default:
			if (code < 0x200) {
				VDUIAccelerator accel;
				accel.mModifiers = 0;

				switch(code) {
					case kATInputCode_KeyNumpadEnter:
						code = kATInputCode_KeyReturn;
						accel.mModifiers |= VDUIAccelerator::kModExtended;
						break;
					case kATInputCode_KeyInsert:
					case kATInputCode_KeyDelete:
					case kATInputCode_KeyHome:
					case kATInputCode_KeyEnd:
					case kATInputCode_KeyNext:
					case kATInputCode_KeyPrior:
					case kATInputCode_KeyLeft:
					case kATInputCode_KeyRight:
					case kATInputCode_KeyUp:
					case kATInputCode_KeyDown:
						accel.mModifiers |= VDUIAccelerator::kModExtended;
						break;
				}

				accel.mVirtKey = code & 0xff;
				VDUIGetAcceleratorString(accel, name);
				name = VDStringW(L"Key: ") + name;
			} else if ((code & ~0xff) == kATInputCode_JoyButton0)
				name.sprintf(L"Joy Button %d", (code & 0xff) + 1);
			else
				name.sprintf(L"Unknown %x", code);
			break;
	}
}

void ATInputManager::GetNameForTargetCode(uint32 code, ATInputControllerType type, VDStringW& name) const {
	static const wchar_t *const kKeypadButtons[]={
		L"1 Key",
		L"2 Key",
		L"3 Key",
		L"4 Key",
		L"5 Key",
		L"6 Key",
		L"7 Key",
		L"8 Key",
		L"9 Key",
		L"0 Key",
		L"Period",
		L"Plus/Enter",
		L"Minus",
		L"Y",
		L"N",
		L"Del",
		L"Esc"
	};

	static const wchar_t *const kLightPenButtons[]={
		L"Gun trigger / inverted pen switch",
		L"Secondary button",
		L"On-screen",
	};

	static const wchar_t *const kTabletButtons[]={
		L"Stylus button",
		L"Left tablet button",
		L"Right tablet button",
		L"Raise stylus",
	};

	static const wchar_t *const kPaddleAxes[]={
		L"Paddle knob (linear)",
		L"Paddle knob (2D rotation X)",
		L"Paddle knob (2D rotation Y)"
	};

	name.clear();

	uint32 index = code & 0xFF;
	switch(code & 0xFF00) {
		case kATInputTrigger_Button0:
			switch(type) {
				case kATInputControllerType_Keypad:
					if (index < sizeof(kKeypadButtons)/sizeof(kKeypadButtons[0])) {
						name = kKeypadButtons[index];
						return;
					}
					break;

				case kATInputControllerType_Tablet:
				case kATInputControllerType_KoalaPad:
					if (index < sizeof(kTabletButtons)/sizeof(kTabletButtons[0])) {
						name = kTabletButtons[index];
						return;
					}
					break;

				case kATInputControllerType_LightPen:
					if (index < sizeof(kLightPenButtons)/sizeof(kLightPenButtons[0])) {
						name = kLightPenButtons[index];
						return;
					}
					break;
			}

			name.sprintf(L"Button %d", index + 1);
			break;
		case kATInputTrigger_Axis0:
			switch(type) {
				case kATInputControllerType_Paddle:
					if (index < sizeof(kPaddleAxes)/sizeof(kPaddleAxes[0])) {
						name = kPaddleAxes[index];
						return;
					}
					break;
			}

			name.sprintf(L"Axis %d", index + 1);
			break;
		case kATInputTrigger_Flag0:
			name.sprintf(L"Flag %d", index + 1);
			break;
		default:
			switch(code) {
			case kATInputTrigger_Up:
				name = L"Up";
				break;
			case kATInputTrigger_Down:
				name = L"Down";
				break;
			case kATInputTrigger_Left:
				name = L"Left";
				break;
			case kATInputTrigger_Right:
				name = L"Right";
				break;
			case kATInputTrigger_Start:
				name = L"Start";
				break;
			case kATInputTrigger_Select:
				name = L"Select";
				break;
			case kATInputTrigger_Option:
				name = L"Option";
				break;
			case kATInputTrigger_Turbo:
				name = L"Turbo";
				break;
			case kATInputTrigger_ColdReset:
				name = L"Cold Reset";
				break;
			case kATInputTrigger_WarmReset:
				name = L"Warm Reset";
				break;
			case kATInputTrigger_KeySpace:
				name = L"Space Bar";
				break;
			case kATInputTrigger_5200_0:
				name = L"0 Key";
				break;
			case kATInputTrigger_5200_1:
				name = L"1 Key";
				break;
			case kATInputTrigger_5200_2:
				name = L"2 Key";
				break;
			case kATInputTrigger_5200_3:
				name = L"3 Key";
				break;
			case kATInputTrigger_5200_4:
				name = L"4 Key";
				break;
			case kATInputTrigger_5200_5:
				name = L"5 Key";
				break;
			case kATInputTrigger_5200_6:
				name = L"6 Key";
				break;
			case kATInputTrigger_5200_7:
				name = L"7 Key";
				break;
			case kATInputTrigger_5200_8:
				name = L"8 Key";
				break;
			case kATInputTrigger_5200_9:
				name = L"9 Key";
				break;
			case kATInputTrigger_5200_Pound:
				name = L"# Key";
				break;
			case kATInputTrigger_5200_Star:
				name = L"* Key";
				break;
			case kATInputTrigger_5200_Start:
				name = L"Start";
				break;
			case kATInputTrigger_5200_Pause:
				name = L"Pause";
				break;
			case kATInputTrigger_5200_Reset:
				name = L"Reset";
				break;
			}
			break;
	}

	if (name.empty())
		name.sprintf(L"Unknown %x", code);
}

uint32 ATInputManager::GetInputMapCount() const {
	return mInputMaps.size();
}

bool ATInputManager::GetInputMapByIndex(uint32 index, ATInputMap **ppimap) const {
	if (index >= mInputMaps.size())
		return false;

	InputMaps::const_iterator it(mInputMaps.begin());
	std::advance(it, index);

	ATInputMap *imap = it->first;
	imap->AddRef();

	*ppimap = imap;
	return true;
}

bool ATInputManager::IsInputMapEnabled(ATInputMap *imap) const {
	InputMaps::const_iterator it(mInputMaps.find(imap));
	if (it == mInputMaps.end())
		return false;

	return it->second;
}

void ATInputManager::AddInputMap(ATInputMap *imap) {
	if (mInputMaps.insert(InputMaps::value_type(imap, false)).second)
		imap->AddRef();
}

void ATInputManager::RemoveInputMap(ATInputMap *imap) {
	InputMaps::iterator it = mInputMaps.find(imap);
	if (it != mInputMaps.end()) {
		mInputMaps.erase(it);
		imap->Release();
	}

	RebuildMappings();
}

void ATInputManager::RemoveAllInputMaps() {
	for(InputMaps::iterator it(mInputMaps.begin()), itEnd(mInputMaps.end()); it != itEnd; ++it) {
		it->first->Release();
	}

	mInputMaps.clear();

	RebuildMappings();
}

void ATInputManager::ActivateInputMap(ATInputMap *imap, bool enable) {
	InputMaps::iterator it = mInputMaps.find(imap);

	if (it != mInputMaps.end()) {
		if (it->second != enable) {
			it->second = enable;
			RebuildMappings();
		}
	}
}

uint32 ATInputManager::GetPresetInputMapCount() const {
	return mPresetMaps.size();
}

bool ATInputManager::GetPresetInputMapByIndex(uint32 index, ATInputMap **imap) const {
	if (index >= mPresetMaps.size())
		return false;

	*imap = mPresetMaps[index];
	(*imap)->AddRef();
	return true;
}

bool ATInputManager::Load(VDRegistryKey& key) {
	vdfastvector<uint32> bitfield;
	int len = key.getBinaryLength("Active maps");
	if (len < 4)
		return false;

	bitfield.resize((len + 3) >> 2, 0);
	if (!key.getBinary("Active maps", (char *)bitfield.data(), len))
		return false;

	uint32 inputMaps = bitfield[0];

	if (inputMaps >= 0x10000 || len < (int)((inputMaps + 31) >> 5))
		return false;

	RemoveAllInputMaps();

	VDStringA valName;
	for(uint32 i=0; i<inputMaps; ++i) {
		valName.sprintf("Input map %u", i);

		vdrefptr<ATInputMap> imap(new ATInputMap);

		if (imap->Load(key, valName.c_str())) {
			AddInputMap(imap);

			if (bitfield[1 + (i >> 5)] & (1 << (i & 31)))
				ActivateInputMap(imap, true);
		}
	}

	return true;
}

namespace {
	struct InputMapSorter {
		bool operator()(const std::pair<ATInputMap *, bool>& x, const std::pair<ATInputMap *, bool>& y) const {
			return vdwcsicmp(x.first->GetName(), y.first->GetName()) < 0;
		}
	};
}

void ATInputManager::Save(VDRegistryKey& key) {
	vdfastvector<std::pair<ATInputMap *, bool> > sortedMaps;

	for(InputMaps::iterator it(mInputMaps.begin()), itEnd(mInputMaps.end()); it != itEnd; ++it)
		sortedMaps.push_back(*it);

	std::sort(sortedMaps.begin(), sortedMaps.end(), InputMapSorter());

	const uint32 n = sortedMaps.size();
	vdfastvector<uint32> bitfield(1 + ((n + 31) >> 5), 0);

	bitfield[0] = n;

	VDStringA valName;
	for(uint32 i=0; i<n; ++i) {
		ATInputMap *imap = sortedMaps[i].first;

		if (sortedMaps[i].second)
			bitfield[1 + (i >> 5)] |= (1 << (i & 31));

		valName.sprintf("Input map %u", i);
		imap->Save(key, valName.c_str());
	}

	key.setBinary("Active maps", (const char *)bitfield.data(), bitfield.size() * 4);
}

void ATInputManager::RebuildMappings() {
	ClearTriggers();

	for(InputControllers::const_iterator it(mInputControllers.begin()), itEnd(mInputControllers.end()); it != itEnd; ++it) {
		ATPortInputController *pc = it->mpInputController;
		pc->Detach();
		delete pc;
	}

	mInputControllers.clear();
	mMappings.clear();
	mFlags.clear();
	mFlags.push_back(true);

	mbMouseAbsMode = false;
	mbMouseMapped = false;
	mbMouseActiveTarget = false;

	for(int i=0; i<2; ++i) {
		if (mpPorts[i])
			mpPorts[i]->ResetPotPositions();
	}
	
	uint32 triggerCount = 0;

	vdfastvector<int> controllerTable;
	typedef vdhashmap<uint32, int> ControllerMap;
	ControllerMap controllerMap;
	for(InputMaps::iterator it(mInputMaps.begin()), itEnd(mInputMaps.end()); it != itEnd; ++it) {
		const bool enabled = it->second;
		if (!enabled)
			continue;

		uint32 flagIndex = mFlags.size();
		mFlags.push_back(false);
		mFlags.push_back(false);

		ATInputMap *imap = it->first;
		const uint32 controllerCount = imap->GetControllerCount();
		int specificUnit = imap->GetSpecificInputUnit();

		controllerTable.resize(controllerCount, -1);
		for(uint32 i=0; i<controllerCount; ++i) {
			const ATInputMap::Controller& c = imap->GetController(i);

			uint32 code = (c.mType << 16) + c.mIndex;
			ControllerMap::iterator itC(controllerMap.find(code));
			ATPortInputController *pic = NULL;
			if (itC != controllerMap.end()) {
				controllerTable[i] = itC->second;
			} else {
				switch(c.mType) {
					case kATInputControllerType_Joystick:
						if (c.mIndex < 12) {
							ATJoystickController *joy = new ATJoystickController;

							if (c.mIndex >= 4)
								joy->Attach(mpPorts[0], false, c.mIndex - 4);
							else
								joy->Attach(mpPorts[c.mIndex >> 1], (c.mIndex & 1) != 0, -1);

							pic = joy;
						}
						break;

					case kATInputControllerType_STMouse:
					case kATInputControllerType_AmigaMouse:
						if (c.mIndex < 4) {
							ATMouseController *mouse = new ATMouseController(c.mType == kATInputControllerType_AmigaMouse);

							mouse->Init(mpSlowScheduler);
							mouse->Attach(mpPorts[c.mIndex >> 1], (c.mIndex & 1) != 0);

							pic = mouse;
						}
						break;

					case kATInputControllerType_Paddle:
						if (c.mIndex < 8) {
							ATPaddleController *paddle = new ATPaddleController;

							paddle->SetHalf((c.mIndex & 1) != 0);
							paddle->Attach(mpPorts[c.mIndex >> 2], (c.mIndex & 2) != 0);

							pic = paddle;
						}
						break;

					case kATInputControllerType_Console:
						{
							ATConsoleController *console = new ATConsoleController(this);
							console->Attach(mpPorts[0], false);
							pic = console;
						}
						break;

					case kATInputControllerType_5200Controller:
					case kATInputControllerType_5200Trackball:
						if (c.mIndex < 4) {
							AT5200ControllerController *ctrl = new AT5200ControllerController(c.mIndex, c.mType == kATInputControllerType_5200Trackball);
							ctrl->Attach(mpPorts[c.mIndex >> 1], (c.mIndex & 1) != 0);
							pic = ctrl;
						}
						break;

					case kATInputControllerType_InputState:
						{
							ATInputStateController *ctrl = new ATInputStateController(this, flagIndex);
							pic = ctrl;
						}
						break;

					case kATInputControllerType_LightPen:
						if (c.mIndex < 4) {
							ATLightPenController *lp = new ATLightPenController;

							lp->Init(mpFastScheduler, mpLightPen);
							lp->Attach(mpPorts[c.mIndex >> 1], (c.mIndex & 1) != 0);

							pic = lp;
						}
						break;

					case kATInputControllerType_Tablet:
						if (c.mIndex < 4) {
							ATTabletController *tc = new ATTabletController(228, true);

							tc->Attach(mpPorts[c.mIndex >> 1], (c.mIndex & 1) != 0);

							pic = tc;
						}
						break;

					case kATInputControllerType_KoalaPad:
						if (c.mIndex < 4) {
							ATTabletController *tc = new ATTabletController(0, false);

							tc->Attach(mpPorts[c.mIndex >> 1], (c.mIndex & 1) != 0);

							pic = tc;
						}
						break;

					case kATInputControllerType_Keypad:
						if (c.mIndex < 4) {
							ATKeypadController *kpc = new ATKeypadController;

							kpc->Attach(mpPorts[c.mIndex >> 1], (c.mIndex & 1) != 0);

							pic = kpc;
						}
						break;

					case kATInputControllerType_Trackball_CX80_V1:
						if (c.mIndex < 4) {
							ATTrackballController *trakball = new ATTrackballController;

							trakball->Init(mpSlowScheduler);
							trakball->Attach(mpPorts[c.mIndex >> 1], (c.mIndex & 1) != 0);

							pic = trakball;
						}
						break;
				}

				if (pic) {
					controllerMap.insert(ControllerMap::value_type(code, (int)mInputControllers.size()));
					controllerTable[i] = (int)mInputControllers.size();

					ControllerInfo& ci = mInputControllers.push_back();
					ci.mpInputController = pic;
					ci.mbBoundToMouseAbs = false;
				}
			}
		}

		const uint32 mappingCount = imap->GetMappingCount();
		for(uint32 i=0; i<mappingCount; ++i) {
			const ATInputMap::Mapping& m = imap->GetMapping(i);
			const int controllerIndex = controllerTable[m.mControllerId];

			if (controllerIndex < 0)
				continue;

			ControllerInfo& ci = mInputControllers[controllerIndex];
			ATPortInputController *pic = ci.mpInputController;

			int32 triggerIdx = -1;
			for(uint32 j=0; j<triggerCount; ++j) {
				const Trigger& t = mTriggers[j];

				if (t.mId == m.mInputCode && t.mpController == pic) {
					triggerIdx = j;
					break;
				}
			}

			if (triggerIdx < 0) {
				triggerIdx = mTriggers.size();
				Trigger& t = mTriggers.push_back();

				t.mId = m.mCode;
				t.mpController = pic;
				t.mCount = 0;
			}

			uint32 inputCode = m.mInputCode;
			switch(inputCode & kATInputCode_ClassMask) {
				case kATInputCode_JoyClass:
					if (specificUnit >= 0) {
						inputCode |= kATInputCode_SpecificUnit;
						inputCode |= specificUnit << kATInputCode_UnitShift;
					}
					break;

				case kATInputCode_MouseClass:
					mbMouseMapped = true;
					switch(inputCode & kATInputCode_IdMask) {
						case kATInputCode_MousePadX:
						case kATInputCode_MousePadY:
						case kATInputCode_MouseBeamX:
						case kATInputCode_MouseBeamY:
							mbMouseAbsMode = true;
							ci.mbBoundToMouseAbs = true;
							break;
					}
					break;
			}

			Mapping& mapping = mMappings.insert(Mappings::value_type(inputCode & ~kATInputCode_FlagMask, Mapping()))->second;
			mapping.mTriggerIdx = triggerIdx;
			mapping.mbTriggerActivated = false;
			mapping.mbMotionActive = false;
			mapping.mValue = 0;
			mapping.mAccel = 0;
			mapping.mDamping = 0;

			if (inputCode & kATInputCode_FlagCheck0) {
				mapping.mFlagIndex1 = flagIndex + 0;
				mapping.mbFlagValue1 = (inputCode & kATInputCode_FlagValue0) != 0;
			} else {
				mapping.mFlagIndex1 = 0;
				mapping.mbFlagValue1 = true;
			}

			if (inputCode & kATInputCode_FlagCheck1) {
				mapping.mFlagIndex2 = flagIndex + 1;
				mapping.mbFlagValue2 = (inputCode & kATInputCode_FlagValue1) != 0;
			} else {
				mapping.mFlagIndex2 = 0;
				mapping.mbFlagValue2 = true;
			}
		}
	}

	Update5200Controller();

	// Pre-activate all inverted triggers.
	for(Triggers::iterator it(mTriggers.begin()), itEnd(mTriggers.end()); it != itEnd; ++it) {
		Trigger& trigger = *it;

		if ((trigger.mId & kATInputTriggerMode_Mask) != kATInputTriggerMode_Inverted)
			continue;

		const uint32 id = trigger.mId & kATInputTrigger_Mask;

		trigger.mpController->SetDigitalTrigger(id, true);
	}
}

void ATInputManager::ActivateMappings(uint32 id, bool state) {
	std::pair<Mappings::iterator, Mappings::iterator> result(mMappings.equal_range(id));

	for(; result.first != result.second; ++result.first) {
		Mapping& mapping = result.first->second;

		if (mFlags[mapping.mFlagIndex1] != mapping.mbFlagValue1
			|| mFlags[mapping.mFlagIndex2] != mapping.mbFlagValue2)
		{
			continue;
		}

		const uint32 triggerIdx = mapping.mTriggerIdx;
		const Trigger& trigger = mTriggers[triggerIdx];

		switch(trigger.mId & kATInputTriggerMode_Mask) {
			case kATInputTriggerMode_Toggle:
				if (!state)
					break;

				state = !mapping.mbTriggerActivated;
				// fall through

			case kATInputTriggerMode_Default:
			case kATInputTriggerMode_Inverted:
			default:
				SetTrigger(mapping, state);
				break;

			case kATInputTriggerMode_ToggleAF:
				if (!state)
					break;

				state = !mapping.mbTriggerActivated;
				// fall through

			case kATInputTriggerMode_AutoFire:
				if (state) {
					if (!mapping.mbMotionActive) {
						mapping.mbMotionActive = true;
						mapping.mAccel = 0;
						mapping.mDamping = 3;

						SetTrigger(mapping, true);
					}
				} else {
					if (mapping.mbMotionActive) {
						mapping.mbMotionActive = false;

						SetTrigger(mapping, false);
					}
				}
				break;

			case kATInputTriggerMode_Relative:
				if (state) {
					const int speedIndex = ((trigger.mId & kATInputTriggerSpeed_Mask) >> kATInputTriggerSpeed_Shift);
					int speedVal = kSpeedScaleTable[speedIndex];

					// fall through
					mapping.mbMotionActive = true;
					mapping.mAccel = speedVal * 0x90;
					mapping.mDamping = 0x100;

					if (mapping.mValue < speedVal * 0x80)
						mapping.mValue = speedVal * 0x80;
				} else {
					mapping.mAccel = 0;
					mapping.mDamping = 0x60;
				}
				break;
		}
	}
}

void ATInputManager::ActivateAnalogMappings(uint32 id, int ds, int dsdead) {
	std::pair<Mappings::iterator, Mappings::iterator> result(mMappings.equal_range(id));

	for(; result.first != result.second; ++result.first) {
		Mapping& mapping = result.first->second;

		if (mFlags[mapping.mFlagIndex1] != mapping.mbFlagValue1
			|| mFlags[mapping.mFlagIndex2] != mapping.mbFlagValue2)
		{
			continue;
		}

		Trigger& trigger = mTriggers[mapping.mTriggerIdx];
		
		const uint32 id = trigger.mId & kATInputTrigger_Mask;

		switch(trigger.mId & kATInputTriggerMode_Mask) {
			case kATInputTriggerMode_Inverted:
				ds = -ds;
				// fall through

			case kATInputTriggerMode_Default:
			case kATInputTriggerMode_Absolute:
			default:
				trigger.mpController->ApplyAnalogInput(id, ds);
				break;

			case kATInputTriggerMode_Relative:
				dsdead = (dsdead * kSpeedScaleTable[((trigger.mId & kATInputTriggerSpeed_Mask) >> kATInputTriggerSpeed_Shift)] + 0x80) >> 8;

				mapping.mValue = dsdead << 5;

				if (dsdead) {
					mapping.mDamping = 0x100;
					mapping.mbMotionActive = true;
				} else {
					mapping.mDamping = 0;
					mapping.mbMotionActive = false;
				}
				break;
		}
	}
}

void ATInputManager::ActivateImpulseMappings(uint32 id, int ds) {
	std::pair<Mappings::iterator, Mappings::iterator> result(mMappings.equal_range(id));

	for(; result.first != result.second; ++result.first) {
		const Mapping& mapping = result.first->second;

		if (mFlags[mapping.mFlagIndex1] != mapping.mbFlagValue1
			|| mFlags[mapping.mFlagIndex2] != mapping.mbFlagValue2)
		{
			continue;
		}

		const uint32 triggerIdx = mapping.mTriggerIdx;
		Trigger& trigger = mTriggers[triggerIdx];
		
		const uint32 id = trigger.mId & kATInputTrigger_Mask;

		switch(trigger.mId & kATInputTriggerMode_Mask) {
			case kATInputTriggerMode_Absolute:
				trigger.mpController->ApplyAnalogInput(id, ds);
				break;

			case kATInputTriggerMode_Relative:
				ds = (ds * kSpeedScaleTable[((trigger.mId & kATInputTriggerSpeed_Mask) >> kATInputTriggerSpeed_Shift)] + 0x80) >> 8;
				// fall through
			case kATInputTriggerMode_Default:
			default:
				trigger.mpController->ApplyImpulse(id, ds);
				break;
		}
	}
}

void ATInputManager::ActivateFlag(uint32 id, bool state) {
	if (mFlags[id] == state)
		return;

	mFlags[id] = state;

	// Check for any mappings that need to be deactivated.
	for(Mappings::iterator it(mMappings.begin()), itEnd(mMappings.end()); it != itEnd; ++it) {
		Mapping& mapping = it->second;

		// Check if this mapping uses the flag being changed and that it's active
		// with the current flag set.
		if (mapping.mFlagIndex1 == id) {
			if (mapping.mbFlagValue1 == state || mFlags[mapping.mFlagIndex2] != mapping.mbFlagValue2)
				continue;
		} else if (mapping.mFlagIndex2 == id) {
			if (mapping.mbFlagValue2 == state || mFlags[mapping.mFlagIndex1] != mapping.mbFlagValue1)
				continue;
		} else {
			continue;
		}

		// Deactivate any triggers.
		if (mapping.mbTriggerActivated)
			SetTrigger(mapping, false);

		// Kill any motion.
		if (mapping.mbMotionActive) {
			mapping.mbMotionActive = false;
			mapping.mValue = 0;
			mapping.mAccel = 0;
		}
	}
}

void ATInputManager::ClearTriggers() {
	while(!mTriggers.empty()) {
		const Trigger& trigger = mTriggers.back();
		bool triggerActive = trigger.mCount != 0;

		if ((trigger.mId & kATInputTriggerMode_Mask) == kATInputTriggerMode_Inverted)
			triggerActive = !triggerActive;

		if (triggerActive) {
			const uint32 id = trigger.mId & kATInputTrigger_Mask;

			trigger.mpController->SetDigitalTrigger(id, false);
		}

		mTriggers.pop_back();
	}
}

void ATInputManager::SetTrigger(Mapping& mapping, bool state) {
	if (mapping.mbTriggerActivated == state)
		return;

	mapping.mbTriggerActivated = state;

	const uint32 triggerIdx = mapping.mTriggerIdx;
	Trigger& trigger = mTriggers[triggerIdx];
	const uint32 id = trigger.mId & kATInputTrigger_Mask;

	bool inverted = (trigger.mId & kATInputTriggerMode_Mask) == kATInputTriggerMode_Inverted;

	if (state) {
		if (!trigger.mCount++)
			trigger.mpController->SetDigitalTrigger(id, !inverted);
	} else {
		VDASSERT(trigger.mCount);
		if (!--trigger.mCount)
			trigger.mpController->SetDigitalTrigger(id, inverted);
	}
}

void ATInputManager::InitPresetMaps() {
	vdrefptr<ATInputMap> imap;
	
	imap = new ATInputMap;
	imap->SetName(L"Arrow Keys -> Joystick (port 1)");
	imap->AddController(kATInputControllerType_Joystick, 0);
	imap->AddMapping(kATInputCode_KeyLeft, 0, kATInputTrigger_Left);
	imap->AddMapping(kATInputCode_KeyRight, 0, kATInputTrigger_Right);
	imap->AddMapping(kATInputCode_KeyUp, 0, kATInputTrigger_Up);
	imap->AddMapping(kATInputCode_KeyDown, 0, kATInputTrigger_Down);
	imap->AddMapping(kATInputCode_KeyLControl, 0, kATInputTrigger_Button0);
	mPresetMaps.push_back(imap);
	imap.release();

	imap = new ATInputMap;
	imap->SetName(L"Numpad -> Joystick (port 1)");
	imap->AddController(kATInputControllerType_Joystick, 0);
	imap->AddMapping(kATInputCode_KeyNumpad1, 0, kATInputTrigger_Left);
	imap->AddMapping(kATInputCode_KeyNumpad1, 0, kATInputTrigger_Down);
	imap->AddMapping(kATInputCode_KeyNumpad2, 0, kATInputTrigger_Down);
	imap->AddMapping(kATInputCode_KeyNumpad3, 0, kATInputTrigger_Down);
	imap->AddMapping(kATInputCode_KeyNumpad3, 0, kATInputTrigger_Right);
	imap->AddMapping(kATInputCode_KeyNumpad4, 0, kATInputTrigger_Left);
	imap->AddMapping(kATInputCode_KeyNumpad6, 0, kATInputTrigger_Right);
	imap->AddMapping(kATInputCode_KeyNumpad7, 0, kATInputTrigger_Left);
	imap->AddMapping(kATInputCode_KeyNumpad7, 0, kATInputTrigger_Up);
	imap->AddMapping(kATInputCode_KeyNumpad8, 0, kATInputTrigger_Up);
	imap->AddMapping(kATInputCode_KeyNumpad9, 0, kATInputTrigger_Up);
	imap->AddMapping(kATInputCode_KeyNumpad9, 0, kATInputTrigger_Right);
	imap->AddMapping(kATInputCode_KeyNumpad0, 0, kATInputTrigger_Button0);
	mPresetMaps.push_back(imap);
	imap.release();

	imap = new ATInputMap;
	imap->SetName(L"Mouse -> Paddle A (port 1)");
	imap->AddController(kATInputControllerType_Paddle, 0);
	imap->AddMapping(kATInputCode_MouseHoriz, 0, kATInputTrigger_Axis0);
	imap->AddMapping(kATInputCode_MouseLMB, 0, kATInputTrigger_Button0);
	mPresetMaps.push_back(imap);
	imap.release();

	imap = new ATInputMap;
	imap->SetName(L"Mouse -> ST Mouse (port 1)");
	imap->AddController(kATInputControllerType_STMouse, 0);
	imap->AddMapping(kATInputCode_MouseHoriz, 0, kATInputTrigger_Axis0);
	imap->AddMapping(kATInputCode_MouseVert, 0, kATInputTrigger_Axis0+1);
	imap->AddMapping(kATInputCode_MouseLMB, 0, kATInputTrigger_Button0);
	imap->AddMapping(kATInputCode_MouseRMB, 0, kATInputTrigger_Button0+1);
	mPresetMaps.push_back(imap);
	imap.release();

	imap = new ATInputMap;
	imap->SetName(L"Arrow Keys -> Joystick (port 2)");
	imap->AddController(kATInputControllerType_Joystick, 1);
	imap->AddMapping(kATInputCode_KeyLeft, 0, kATInputTrigger_Left);
	imap->AddMapping(kATInputCode_KeyRight, 0, kATInputTrigger_Right);
	imap->AddMapping(kATInputCode_KeyUp, 0, kATInputTrigger_Up);
	imap->AddMapping(kATInputCode_KeyDown, 0, kATInputTrigger_Down);
	imap->AddMapping(kATInputCode_KeyLControl, 0, kATInputTrigger_Button0);
	mPresetMaps.push_back(imap);
	imap.release();

	imap = new ATInputMap;
	imap->SetName(L"Mouse -> ST Mouse (port 2)");
	imap->AddController(kATInputControllerType_STMouse, 1);
	imap->AddMapping(kATInputCode_MouseHoriz, 0, kATInputTrigger_Axis0);
	imap->AddMapping(kATInputCode_MouseVert, 0, kATInputTrigger_Axis0+1);
	imap->AddMapping(kATInputCode_MouseLMB, 0, kATInputTrigger_Button0);
	imap->AddMapping(kATInputCode_MouseRMB, 0, kATInputTrigger_Button0+1);
	mPresetMaps.push_back(imap);
	imap.release();

	imap = new ATInputMap;
	imap->SetName(L"Gamepad -> Joystick (port 1)");
	imap->AddController(kATInputControllerType_Joystick, 0);
	imap->AddMapping(kATInputCode_JoyPOVUp, 0, kATInputTrigger_Up);
	imap->AddMapping(kATInputCode_JoyPOVDown, 0, kATInputTrigger_Down);
	imap->AddMapping(kATInputCode_JoyPOVLeft, 0, kATInputTrigger_Left);
	imap->AddMapping(kATInputCode_JoyPOVRight, 0, kATInputTrigger_Right);
	imap->AddMapping(kATInputCode_JoyStick1Up, 0, kATInputTrigger_Up);
	imap->AddMapping(kATInputCode_JoyStick1Down, 0, kATInputTrigger_Down);
	imap->AddMapping(kATInputCode_JoyStick1Left, 0, kATInputTrigger_Left);
	imap->AddMapping(kATInputCode_JoyStick1Right, 0, kATInputTrigger_Right);
	imap->AddMapping(kATInputCode_JoyButton0, 0, kATInputTrigger_Button0);
	imap->AddMapping(kATInputCode_JoyButton0+1, 0, kATInputTrigger_Button0);
	imap->AddMapping(kATInputCode_JoyButton0+2, 0, kATInputTrigger_Button0);
	imap->AddMapping(kATInputCode_JoyButton0+3, 0, kATInputTrigger_Button0);
	mPresetMaps.push_back(imap);
	imap.release();

	imap = new ATInputMap;
	imap->SetName(L"Gamepad 1 -> Joystick (port 1)");
	imap->SetSpecificInputUnit(0);
	imap->AddController(kATInputControllerType_Joystick, 0);
	imap->AddMapping(kATInputCode_JoyPOVUp, 0, kATInputTrigger_Up);
	imap->AddMapping(kATInputCode_JoyPOVDown, 0, kATInputTrigger_Down);
	imap->AddMapping(kATInputCode_JoyPOVLeft, 0, kATInputTrigger_Left);
	imap->AddMapping(kATInputCode_JoyPOVRight, 0, kATInputTrigger_Right);
	imap->AddMapping(kATInputCode_JoyStick1Up, 0, kATInputTrigger_Up);
	imap->AddMapping(kATInputCode_JoyStick1Down, 0, kATInputTrigger_Down);
	imap->AddMapping(kATInputCode_JoyStick1Left, 0, kATInputTrigger_Left);
	imap->AddMapping(kATInputCode_JoyStick1Right, 0, kATInputTrigger_Right);
	imap->AddMapping(kATInputCode_JoyButton0, 0, kATInputTrigger_Button0);
	imap->AddMapping(kATInputCode_JoyButton0+1, 0, kATInputTrigger_Button0);
	imap->AddMapping(kATInputCode_JoyButton0+2, 0, kATInputTrigger_Button0);
	imap->AddMapping(kATInputCode_JoyButton0+3, 0, kATInputTrigger_Button0);
	mPresetMaps.push_back(imap);
	imap.release();

	imap = new ATInputMap;
	imap->SetName(L"Gamepad 2 -> Joystick (port 2)");
	imap->SetSpecificInputUnit(1);
	imap->AddController(kATInputControllerType_Joystick, 1);
	imap->AddMapping(kATInputCode_JoyPOVUp, 0, kATInputTrigger_Up);
	imap->AddMapping(kATInputCode_JoyPOVDown, 0, kATInputTrigger_Down);
	imap->AddMapping(kATInputCode_JoyPOVLeft, 0, kATInputTrigger_Left);
	imap->AddMapping(kATInputCode_JoyPOVRight, 0, kATInputTrigger_Right);
	imap->AddMapping(kATInputCode_JoyStick1Up, 0, kATInputTrigger_Up);
	imap->AddMapping(kATInputCode_JoyStick1Down, 0, kATInputTrigger_Down);
	imap->AddMapping(kATInputCode_JoyStick1Left, 0, kATInputTrigger_Left);
	imap->AddMapping(kATInputCode_JoyStick1Right, 0, kATInputTrigger_Right);
	imap->AddMapping(kATInputCode_JoyButton0, 0, kATInputTrigger_Button0);
	imap->AddMapping(kATInputCode_JoyButton0+1, 0, kATInputTrigger_Button0);
	imap->AddMapping(kATInputCode_JoyButton0+2, 0, kATInputTrigger_Button0);
	imap->AddMapping(kATInputCode_JoyButton0+3, 0, kATInputTrigger_Button0);
	mPresetMaps.push_back(imap);
	imap.release();

	imap = new ATInputMap;
	imap->SetName(L"Keyboard -> 5200 Controller (port 1)");
	imap->AddController(kATInputControllerType_5200Controller, 0);
	imap->AddMapping(kATInputCode_KeyLeft, 0, kATInputTrigger_Left);
	imap->AddMapping(kATInputCode_KeyRight, 0, kATInputTrigger_Right);
	imap->AddMapping(kATInputCode_KeyUp, 0, kATInputTrigger_Up);
	imap->AddMapping(kATInputCode_KeyDown, 0, kATInputTrigger_Down);
	imap->AddMapping(kATInputCode_KeyLControl, 0, kATInputTrigger_Button0);
	imap->AddMapping(kATInputCode_KeyLShift, 0, kATInputTrigger_Button0+1);
	imap->AddMapping(kATInputCode_Key0, 0, kATInputTrigger_5200_0);
	imap->AddMapping(kATInputCode_Key1, 0, kATInputTrigger_5200_1);
	imap->AddMapping(kATInputCode_Key2, 0, kATInputTrigger_5200_2);
	imap->AddMapping(kATInputCode_Key3, 0, kATInputTrigger_5200_3);
	imap->AddMapping(kATInputCode_Key4, 0, kATInputTrigger_5200_4);
	imap->AddMapping(kATInputCode_Key5, 0, kATInputTrigger_5200_5);
	imap->AddMapping(kATInputCode_Key6, 0, kATInputTrigger_5200_6);
	imap->AddMapping(kATInputCode_Key7, 0, kATInputTrigger_5200_7);
	imap->AddMapping(kATInputCode_Key8, 0, kATInputTrigger_5200_8);
	imap->AddMapping(kATInputCode_Key9, 0, kATInputTrigger_5200_9);
	imap->AddMapping(kATInputCode_KeyOemMinus, 0, kATInputTrigger_5200_Star);
	imap->AddMapping(kATInputCode_KeyOemPlus, 0, kATInputTrigger_5200_Pound);
	imap->AddMapping(kATInputCode_KeyF2, 0, kATInputTrigger_5200_Start);
	imap->AddMapping(kATInputCode_KeyF3, 0, kATInputTrigger_5200_Pause);
	imap->AddMapping(kATInputCode_KeyF4, 0, kATInputTrigger_5200_Reset);
	mPresetMaps.push_back(imap);
	imap.release();

	imap = new ATInputMap;
	imap->SetName(L"Xbox 360 Controller -> Joystick / Paddle (800/XL)");
	imap->AddController(kATInputControllerType_Joystick, 0);
	imap->AddController(kATInputControllerType_Joystick, 1);
	imap->AddController(kATInputControllerType_Paddle, 0);
	imap->AddController(kATInputControllerType_InputState, 0);
	imap->AddController(kATInputControllerType_Console, 0);

	static const uint32 F00 = kATInputCode_FlagCheck0 | kATInputCode_FlagCheck1;
	static const uint32 F01 = kATInputCode_FlagCheck0 | kATInputCode_FlagCheck1 | kATInputCode_FlagValue0;
	static const uint32 F10 = kATInputCode_FlagCheck0 | kATInputCode_FlagCheck1 | kATInputCode_FlagValue1;
	static const uint32 F11 = kATInputCode_FlagCheck0 | kATInputCode_FlagCheck1 | kATInputCode_FlagValue0 | kATInputCode_FlagValue1;
	static const uint32 F1x = kATInputCode_FlagCheck1 | kATInputCode_FlagValue1;

	// Joystick port 1 (-F1 -F2)
	imap->AddMapping(F00 | kATInputCode_JoyPOVLeft,		0, kATInputTrigger_Left);
	imap->AddMapping(F00 | kATInputCode_JoyPOVRight,	0, kATInputTrigger_Right);
	imap->AddMapping(F00 | kATInputCode_JoyPOVUp,		0, kATInputTrigger_Up);
	imap->AddMapping(F00 | kATInputCode_JoyPOVDown,		0, kATInputTrigger_Down);
	imap->AddMapping(F00 | kATInputCode_JoyStick1Left,	0, kATInputTrigger_Left);
	imap->AddMapping(F00 | kATInputCode_JoyStick1Right,	0, kATInputTrigger_Right);
	imap->AddMapping(F00 | kATInputCode_JoyStick1Up,	0, kATInputTrigger_Up);
	imap->AddMapping(F00 | kATInputCode_JoyStick1Down,	0, kATInputTrigger_Down);
	imap->AddMapping(F00 | kATInputCode_JoyButton0,		0, kATInputTrigger_Button0);
	imap->AddMapping(F00 | kATInputCode_JoyButton0+1,	0, kATInputTrigger_Button0 | kATInputTriggerMode_AutoFire | (5 << kATInputTriggerSpeed_Shift));

	// Joystick port 2 (+F1 -F2)
	imap->AddMapping(F01 | kATInputCode_JoyPOVLeft,		1, kATInputTrigger_Left);
	imap->AddMapping(F01 | kATInputCode_JoyPOVRight,	1, kATInputTrigger_Right);
	imap->AddMapping(F01 | kATInputCode_JoyPOVUp,		1, kATInputTrigger_Up);
	imap->AddMapping(F01 | kATInputCode_JoyPOVDown,		1, kATInputTrigger_Down);
	imap->AddMapping(F01 | kATInputCode_JoyStick1Left,	1, kATInputTrigger_Left);
	imap->AddMapping(F01 | kATInputCode_JoyStick1Right,	1, kATInputTrigger_Right);
	imap->AddMapping(F01 | kATInputCode_JoyStick1Up,	1, kATInputTrigger_Up);
	imap->AddMapping(F01 | kATInputCode_JoyStick1Down,	1, kATInputTrigger_Down);
	imap->AddMapping(F01 | kATInputCode_JoyButton0,		1, kATInputTrigger_Button0);
	imap->AddMapping(F01 | kATInputCode_JoyButton0+1,	1, kATInputTrigger_Button0 | kATInputTriggerMode_AutoFire | (5 << kATInputTriggerSpeed_Shift));

	// Paddle common (+F2)
	imap->AddMapping(F1x | kATInputCode_JoyButton0,		2, kATInputTrigger_Button0);
	imap->AddMapping(F1x | kATInputCode_JoyButton0+1,	2, kATInputTrigger_Button0 | kATInputTriggerMode_AutoFire | (5 << kATInputTriggerSpeed_Shift));

	// Paddle absolute mode (-F1 +F2)
	imap->AddMapping(F10 | kATInputCode_JoyHoriz1,		2, kATInputTrigger_Axis0);

	// Paddle relative mode (+F1 +F2)
	imap->AddMapping(F11 | kATInputCode_JoyHoriz1,		2, kATInputTrigger_Axis0 | kATInputTriggerMode_Relative | (5 << kATInputTriggerSpeed_Shift));
	imap->AddMapping(F11 | kATInputCode_JoyPOVLeft,		2, kATInputTrigger_Left | kATInputTriggerMode_Relative | (5 << kATInputTriggerSpeed_Shift));
	imap->AddMapping(F11 | kATInputCode_JoyPOVRight,	2, kATInputTrigger_Right | kATInputTriggerMode_Relative | (5 << kATInputTriggerSpeed_Shift));
	imap->AddMapping(F11 | kATInputCode_JoyHoriz3,		2, kATInputTrigger_Axis0+1);
	imap->AddMapping(F11 | kATInputCode_JoyVert3,		2, kATInputTrigger_Axis0+2);

	// Input state
	imap->AddMapping(kATInputCode_JoyButton0+8,			3, kATInputTrigger_Flag0 | kATInputTriggerMode_Toggle);
	imap->AddMapping(kATInputCode_JoyButton0+9,			3, kATInputTrigger_Flag0+1 | kATInputTriggerMode_Toggle);

	// Console
	imap->AddMapping(kATInputCode_JoyButton0+2,	4, kATInputTrigger_Option);
	imap->AddMapping(kATInputCode_JoyButton0+4,	4, kATInputTrigger_Turbo);
	imap->AddMapping(kATInputCode_JoyButton0+6,	4, kATInputTrigger_Select);
	imap->AddMapping(kATInputCode_JoyButton0+7,	4, kATInputTrigger_Start);

	mPresetMaps.push_back(imap);
	imap.release();
}
