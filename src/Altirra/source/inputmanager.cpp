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
			case kATInputControllerType_Mouse:
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
	: mpScheduler(NULL)
	, mAllocatedUnits(0)
	, mpCB(NULL)
{
}

ATInputManager::~ATInputManager() {
}

void ATInputManager::Init(ATScheduler *sched, ATPortController *porta, ATPortController *portb) {
	mpScheduler = sched;
	mpPorts[0] = porta;
	mpPorts[1] = portb;

	ResetToDefaults();
}

void ATInputManager::Shutdown() {
	RemoveAllInputMaps();
}

void ATInputManager::ResetToDefaults() {
	RemoveAllInputMaps();

	ATInputMap *imap;
	
	imap = new ATInputMap;
	imap->SetName(L"Arrow Keys -> Joystick (port 1)");
	imap->AddController(kATInputControllerType_Joystick, 0);
	imap->AddMapping(kATInputCode_KeyLeft, 0, kATInputTrigger_Left);
	imap->AddMapping(kATInputCode_KeyRight, 0, kATInputTrigger_Right);
	imap->AddMapping(kATInputCode_KeyUp, 0, kATInputTrigger_Up);
	imap->AddMapping(kATInputCode_KeyDown, 0, kATInputTrigger_Down);
	imap->AddMapping(kATInputCode_KeyLControl, 0, kATInputTrigger_Button0);
	AddInputMap(imap);

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
	AddInputMap(imap);

	imap = new ATInputMap;
	imap->SetName(L"Mouse -> Paddle A (port 1)");
	imap->AddController(kATInputControllerType_Paddle, 0);
	imap->AddMapping(kATInputCode_MouseHoriz, 0, kATInputTrigger_Axis0);
	imap->AddMapping(kATInputCode_MouseLMB, 0, kATInputTrigger_Button0);
	AddInputMap(imap);

	imap = new ATInputMap;
	imap->SetName(L"Mouse -> ST Mouse (port 1)");
	imap->AddController(kATInputControllerType_Mouse, 0);
	imap->AddMapping(kATInputCode_MouseHoriz, 0, kATInputTrigger_Axis0);
	imap->AddMapping(kATInputCode_MouseVert, 0, kATInputTrigger_Axis0+1);
	imap->AddMapping(kATInputCode_MouseLMB, 0, kATInputTrigger_Button0);
	imap->AddMapping(kATInputCode_MouseRMB, 0, kATInputTrigger_Button0+1);
	AddInputMap(imap);

	imap = new ATInputMap;
	imap->SetName(L"Arrow Keys -> Joystick (port 2)");
	imap->AddController(kATInputControllerType_Joystick, 1);
	imap->AddMapping(kATInputCode_KeyLeft, 0, kATInputTrigger_Left);
	imap->AddMapping(kATInputCode_KeyRight, 0, kATInputTrigger_Right);
	imap->AddMapping(kATInputCode_KeyUp, 0, kATInputTrigger_Up);
	imap->AddMapping(kATInputCode_KeyDown, 0, kATInputTrigger_Down);
	imap->AddMapping(kATInputCode_KeyLControl, 0, kATInputTrigger_Button0);
	AddInputMap(imap);

	imap = new ATInputMap;
	imap->SetName(L"Mouse -> ST Mouse (port 2)");
	imap->AddController(kATInputControllerType_Mouse, 1);
	imap->AddMapping(kATInputCode_MouseHoriz, 0, kATInputTrigger_Axis0);
	imap->AddMapping(kATInputCode_MouseVert, 0, kATInputTrigger_Axis0+1);
	imap->AddMapping(kATInputCode_MouseLMB, 0, kATInputTrigger_Button0);
	imap->AddMapping(kATInputCode_MouseRMB, 0, kATInputTrigger_Button0+1);
	AddInputMap(imap);

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
	AddInputMap(imap);

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
	AddInputMap(imap);

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
	AddInputMap(imap);
}

void ATInputManager::Poll() {
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

bool ATInputManager::IsMouseMapped() const {
	return	IsInputMapped(0, kATInputCode_MouseHoriz) ||
			IsInputMapped(0, kATInputCode_MouseVert) ||
			IsInputMapped(0, kATInputCode_MouseLeft) ||
			IsInputMapped(0, kATInputCode_MouseRight) ||
			IsInputMapped(0, kATInputCode_MouseUp) ||
			IsInputMapped(0, kATInputCode_MouseDown) ||
			IsInputMapped(0, kATInputCode_MouseLMB) ||
			IsInputMapped(0, kATInputCode_MouseMMB) ||
			IsInputMapped(0, kATInputCode_MouseRMB) ||
			IsInputMapped(0, kATInputCode_MouseX1B) ||
			IsInputMapped(0, kATInputCode_MouseX2B);
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

void ATInputManager::OnAxisInput(int unit, int axis, sint32 value) {
}

void ATInputManager::OnMouseMove(int unit, int dx, int dy) {
	ActivateImpulseMappings(kATInputCode_MouseHoriz, dx);
	ActivateImpulseMappings(kATInputCode_MouseHoriz | kATInputCode_SpecificUnit | (unit << kATInputCode_UnitShift), dx);
	ActivateImpulseMappings(kATInputCode_MouseVert, dy);
	ActivateImpulseMappings(kATInputCode_MouseVert | kATInputCode_SpecificUnit | (unit << kATInputCode_UnitShift), dy);
}

void ATInputManager::GetNameForInputCode(uint32 code, VDStringW& name) const {
	switch(code & 0xffff) {
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
			name = L"Mouse Horiz";
			break;
		case kATInputCode_MouseVert:
			name = L"Mouse Vert";
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
				name.sprintf(L"Joy Button %d", code & 0xff);
			else
				name.sprintf(L"Unknown %x", code);
			break;
	}

	if (code & kATInputCode_SpecificUnit) {
		name.append_sprintf(L" (unit %u)", (code >> kATInputCode_UnitShift) & 31);
	}
}

void ATInputManager::GetNameForTargetCode(uint32 code, VDStringW& name) const {
	name.clear();

	switch(code & 0xFF00) {
		case kATInputTrigger_Button0:
			name.sprintf(L"Button %d", code & 0xFF);
			break;
		case kATInputTrigger_Axis0:
			name.sprintf(L"Axis %d", code & 0xFF);
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
		ATPortInputController *pc = it->second;
		pc->Detach();
		delete pc;
	}

	mInputControllers.clear();
	mMappings.clear();

	mpPorts[0]->ResetPotPositions();
	mpPorts[1]->ResetPotPositions();
	
	uint32 triggerCount = 0;

	vdfastvector<ATPortInputController *> controllerTable;
	for(InputMaps::iterator it(mInputMaps.begin()), itEnd(mInputMaps.end()); it != itEnd; ++it) {
		const bool enabled = it->second;
		if (!enabled)
			continue;

		ATInputMap *imap = it->first;
		const uint32 controllerCount = imap->GetControllerCount();
		int specificUnit = imap->GetSpecificInputUnit();

		controllerTable.resize(controllerCount);
		for(uint32 i=0; i<controllerCount; ++i) {
			const ATInputMap::Controller& c = imap->GetController(i);

			uint32 code = (c.mType << 16) + c.mIndex;
			InputControllers::iterator itC(mInputControllers.find(code));
			ATPortInputController *pic = NULL;
			if (itC != mInputControllers.end()) {
				pic = itC->second;
			} else {
				switch(c.mType) {
					case kATInputControllerType_Joystick:
						if (c.mIndex < 4) {
							ATJoystickController *joy = new ATJoystickController;

							joy->Attach(mpPorts[c.mIndex >> 1], (c.mIndex & 1) != 0);

							pic = joy;
						}
						break;

					case kATInputControllerType_Mouse:
						if (c.mIndex < 4) {
							ATMouseController *mouse = new ATMouseController;

							mouse->Init(mpScheduler);
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
				}

				if (pic)
					mInputControllers.insert(InputControllers::value_type(code, pic));
			}

			controllerTable[i] = pic;
		}

		const uint32 mappingCount = imap->GetMappingCount();
		for(uint32 i=0; i<mappingCount; ++i) {
			const ATInputMap::Mapping& m = imap->GetMapping(i);
			ATPortInputController *pic = controllerTable[m.mControllerId];

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
			if ((inputCode & kATInputCode_ClassMask) == kATInputCode_JoyClass) {
				if (specificUnit >= 0) {
					inputCode |= kATInputCode_SpecificUnit;
					inputCode |= specificUnit << kATInputCode_UnitShift;
				}
			}

			mMappings.insert(Mappings::value_type(inputCode, triggerIdx));
		}
	}
}

void ATInputManager::ActivateMappings(uint32 id, bool state) {
	std::pair<Mappings::iterator, Mappings::iterator> result(mMappings.equal_range(id));

	for(; result.first != result.second; ++result.first) {
		uint32 trigger = result.first->second;

		SetTrigger(trigger, state);
	}
}

void ATInputManager::ActivateImpulseMappings(uint32 id, int ds) {
	std::pair<Mappings::iterator, Mappings::iterator> result(mMappings.equal_range(id));

	for(; result.first != result.second; ++result.first) {
		uint32 triggerIdx = result.first->second;
		Trigger& trigger = mTriggers[triggerIdx];
		
		trigger.mpController->ApplyImpulse(trigger.mId, ds);
	}
}

void ATInputManager::ClearTriggers() {
	while(!mTriggers.empty()) {
		const Trigger& trigger = mTriggers.back();

		if (trigger.mCount)
			trigger.mpController->SetDigitalTrigger(trigger.mId, false);

		mTriggers.pop_back();
	}
}

void ATInputManager::SetTrigger(uint32 triggerIdx, bool state) {
	Trigger& trigger = mTriggers[triggerIdx];

	if (state) {
		if (!trigger.mCount++)
			trigger.mpController->SetDigitalTrigger(trigger.mId, true);
	} else {
		VDASSERT(trigger.mCount);
		if (!--trigger.mCount)
			trigger.mpController->SetDigitalTrigger(trigger.mId, false);
	}
}
