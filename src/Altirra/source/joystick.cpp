//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008 Avery Lee
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
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <vd2/system/bitmath.h>
#include <vd2/system/math.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/vdalloc.h>
#include "joystick.h"
#include "inputmanager.h"

#pragma comment(lib, "dxguid")
#pragma comment(lib, "dinput8")

class ATController {
public:
	ATController(IDirectInput8 *di);
	~ATController();

	const ATInputUnitIdentifier& GetId() const { return mId; }

	bool Init(LPCDIDEVICEINSTANCE devInst, HWND hwnd, ATInputManager *inputMan);
	void Shutdown();

	bool IsMarked() { return mbMarked; }
	void SetMarked(bool mark) { mbMarked = mark;}

	void SetDeadZone(int zone) { mDeadZone = zone; }

	void Poll();
	bool PollForCapture(int& unit, uint32& inputCode);

protected:
	struct DecodedState {
		uint32	mButtonStates;
		uint32	mAxisButtonStates;
		sint32	mAxisVals[6];
		sint32	mAxisDeadVals[6];
	};

	void PollState(DecodedState& state);
	void UpdateButtons(int baseId, uint32 states, uint32 mask);

	vdrefptr<IDirectInput8> mpDI;
	vdrefptr<IDirectInputDevice8> mpDevice;
	ATInputManager *mpInputManager;

	bool		mbMarked;
	int			mUnit;
	DecodedState	mLastSentState;
	DecodedState	mLastPolledState;
	sint32		mDeadZone;
	DIJOYSTATE	mState;
	ATInputUnitIdentifier mId;
};

ATController::ATController(IDirectInput8 *di)
	: mpDI(di)
	, mbMarked(false)
	, mUnit(-1)
	, mpInputManager(NULL)
	, mDeadZone(512)
{
	memset(&mState, 0, sizeof mState);
	memset(&mLastSentState, 0, sizeof mLastSentState);
	memset(&mLastPolledState, 0, sizeof mLastPolledState);
}

ATController::~ATController() {
	Shutdown();
}

bool ATController::Init(LPCDIDEVICEINSTANCE devInst, HWND hwnd, ATInputManager *inputMan) {
	HRESULT hr = mpDI->CreateDevice(devInst->guidInstance, ~mpDevice, NULL);
	if (FAILED(hr))
		return false;

	hr = mpDevice->SetDataFormat(&c_dfDIJoystick);
	if (FAILED(hr)) {
		Shutdown();
		return false;
	}

	hr = mpDevice->SetCooperativeLevel(hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
	if (FAILED(hr)) {
		Shutdown();
		return false;
	}

	// Set the axis ranges.
	//
	// It'd be better to do this by enumerating the axis objects, but it seems that in
	// XP this can give completely erroneous dwOfs values, i.e. Y Axis = 0, X Axis = 4,
	// RZ axis = 12, etc. Therefore, we just loop through all of the axes.
	DIPROPRANGE range;
	range.diph.dwSize = sizeof(DIPROPRANGE);
	range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
	range.diph.dwHow = DIPH_BYOFFSET;
	range.lMin = -1024;
	range.lMax = +1024;

	static const int kOffsets[6]={
		DIJOFS_X,
		DIJOFS_Y,
		DIJOFS_Z,
		DIJOFS_RX,
		DIJOFS_RY,
		DIJOFS_RZ,
	};

	for(int i=0; i<6; ++i) {
		range.diph.dwObj = kOffsets[i];
		mpDevice->SetProperty(DIPROP_RANGE, &range.diph);
	}

//	mpDevice->EnumObjects(StaticInitAxisCallback, this, DIDFT_ABSAXIS);

	mpInputManager = inputMan;

	memcpy(&mId, &devInst->guidInstance, sizeof mId);
	mUnit = inputMan->RegisterInputUnit(mId, devInst->tszInstanceName);

	return true;
}

void ATController::Shutdown() {
	if (mUnit >= 0) {
		UpdateButtons(kATInputCode_JoyButton0, 0, mLastSentState.mButtonStates);
		UpdateButtons(kATInputCode_JoyStick1Left, 0, mLastSentState.mAxisButtonStates);
		memset(&mLastSentState, 0, sizeof mLastSentState);

		mpInputManager->UnregisterInputUnit(mUnit);
		mUnit = -1;
	}

	mpInputManager = NULL;
	mpDevice = NULL;
}

void ATController::Poll() {
	DecodedState state;
	PollState(state);

	uint32 buttonDelta = (state.mButtonStates ^ mLastSentState.mButtonStates);
	if (buttonDelta)
		UpdateButtons(kATInputCode_JoyButton0, state.mButtonStates, buttonDelta);

	uint32 axisButtonDelta = (state.mAxisButtonStates ^ mLastSentState.mAxisButtonStates);
	if (axisButtonDelta)
		UpdateButtons(kATInputCode_JoyStick1Left, state.mAxisButtonStates, axisButtonDelta);

	for(int i=0; i<6; ++i) {
		if (state.mAxisVals[i] != mLastSentState.mAxisVals[i] ||
			state.mAxisDeadVals[i] != mLastSentState.mAxisDeadVals[i]) {
			// We set DirectInput to use [-1024, 1024], but we want +/-64K.
			mpInputManager->OnAxisInput(mUnit, kATInputCode_JoyHoriz1 + i, state.mAxisVals[i] << 6, state.mAxisDeadVals[i] << 6);
		}
	}

	mLastSentState = state;
	mLastPolledState = state;
}

bool ATController::PollForCapture(int& unit, uint32& inputCode) {
	DecodedState state;
	PollState(state);

	const uint32 newButtons = state.mButtonStates & ~mLastPolledState.mButtonStates;
	const uint32 newAxisButtons = state.mAxisButtonStates & ~mLastPolledState.mAxisButtonStates;

	mLastPolledState = state;

	if (newButtons) {
		unit = mUnit;
		inputCode = kATInputCode_JoyButton0 + VDFindLowestSetBitFast(newButtons);
		return true;
	}

	if (newAxisButtons) {
		unit = mUnit;
		inputCode = kATInputCode_JoyStick1Left + VDFindLowestSetBitFast(newAxisButtons);
		return true;
	}

	return false;
}

void ATController::UpdateButtons(int baseId, uint32 states, uint32 mask) {
	for(int i=0; i<32; ++i) {
		uint32 bit = (1 << i);
		if (mask & bit) {
			if (states & bit)
				mpInputManager->OnButtonDown(mUnit, baseId + i);
			else
				mpInputManager->OnButtonUp(mUnit, baseId + i);
		}
	}
}

void ATController::PollState(DecodedState& state) {
	HRESULT hr = mpDevice->Poll();

	if (FAILED(hr))
		hr = mpDevice->Acquire();

	if (SUCCEEDED(hr))
		hr = mpDevice->GetDeviceState(sizeof(DIJOYSTATE), &mState);

	if (FAILED(hr)) {
		memset(&mState, 0, sizeof mState);
		mState.rgdwPOV[0] = 0xFFFFFFFFU;
	}

	uint32 axisButtonStates = 0;

	uint32 pov = mState.rgdwPOV[0] & 0xffff;
	if (pov < 0xffff) {
		uint32 octant = ((pov + 2250) / 4500) & 7;

		static const uint32 kPOVLookup[8]={
			(1 << 14),	// up
			(1 << 14) | (1 << 13),
			(1 << 13),	// right
			(1 << 15) | (1 << 13),
			(1 << 15),	// down
			(1 << 15) | (1 << 12),
			(1 << 12),	// left
			(1 << 12) | (1 << 14),
		};

		axisButtonStates = kPOVLookup[octant];
	}

	if (mState.lX < -mDeadZone)		axisButtonStates |= (1 << 0);
	if (mState.lX > +mDeadZone)		axisButtonStates |= (1 << 1);
	if (mState.lY < -mDeadZone)		axisButtonStates |= (1 << 2);
	if (mState.lY > +mDeadZone)		axisButtonStates |= (1 << 3);
	if (mState.lZ < -mDeadZone)		axisButtonStates |= (1 << 4);
	if (mState.lZ > +mDeadZone)		axisButtonStates |= (1 << 5);
	if (mState.lRx < -mDeadZone)	axisButtonStates |= (1 << 6);
	if (mState.lRx > +mDeadZone)	axisButtonStates |= (1 << 7);
	if (mState.lRy < -mDeadZone)	axisButtonStates |= (1 << 8);
	if (mState.lRy > +mDeadZone)	axisButtonStates |= (1 << 9);
	if (mState.lRz < -mDeadZone)	axisButtonStates |= (1 << 10);
	if (mState.lRz > +mDeadZone)	axisButtonStates |= (1 << 11);

	state.mAxisVals[0] = mState.lX;
	state.mAxisVals[1] = mState.lY;
	state.mAxisVals[2] = mState.lZ;
	state.mAxisVals[3] = mState.lRx;
	state.mAxisVals[4] = mState.lRy;
	state.mAxisVals[5] = mState.lRz;

	// We treat the X/Y axes as one 2D controller, and lRx/lRy as another.
	// The Z axes are deadified as 1D axes.
	int analogDeadZone = 256;
	for(int i=0; i<=3; i+=3) {
		float x = (float)state.mAxisVals[i];
		float y = (float)state.mAxisVals[i+1];

		float lensq = x*x + y*y;
		if (lensq < (float)(analogDeadZone * analogDeadZone)) {
			state.mAxisDeadVals[i] = 0;
			state.mAxisDeadVals[i+1] = 0;
		} else {
			float len = sqrtf(lensq);
			float scale = (1024 - analogDeadZone) / 1024.0f * (len - analogDeadZone) / len;

			state.mAxisDeadVals[i] = VDRoundToInt32((float)x * scale);
			state.mAxisDeadVals[i+1] = VDRoundToInt32((float)y * scale);
		}

		int z = state.mAxisVals[i+2];
		int zlen = abs(z);
		if (zlen < analogDeadZone)
			state.mAxisDeadVals[i+2] = 0;
		else
			state.mAxisDeadVals[i+2] = VDRoundToInt32((float)z * (1024.0f - analogDeadZone) * ((float)zlen - analogDeadZone) / (float)zlen);
	}

	uint32 buttonStates = 0;
	for(int i=0; i<32; ++i) {
		buttonStates >>= 1;
		if (mState.rgbButtons[i])
			buttonStates |= 0x80000000;
	}

	state.mButtonStates = buttonStates;
	state.mAxisButtonStates = axisButtonStates;
}

///////////////////////////////////////////////////////////////////////////////

class ATJoystickManager : public IATJoystickManager {
public:
	ATJoystickManager();
	~ATJoystickManager();

	bool Init(void *hwnd, ATInputManager *inputMan);
	void Shutdown();

	void SetCaptureMode(bool capture) { mbCaptureMode = capture; }

	void RescanForDevices();
	void Poll();
	bool PollForCapture(int& unit, uint32& inputCode);
	void SetDeadZone(int zone);

	uint32 GetJoystickPortStates() const;

protected:
	static BOOL CALLBACK StaticJoystickCallback(LPCDIDEVICEINSTANCE devInst, LPVOID pThis);
	BOOL JoystickCallback(LPCDIDEVICEINSTANCE devInst);

	bool mbCOMInitialized;
	bool mbCaptureMode;
	HWND mhwnd;
	vdrefptr<IDirectInput8> mpDI;
	ATInputManager *mpInputManager;

	int	mDeadZone;

	typedef vdfastvector<ATController *> Controllers;
	Controllers mControllers;
};

IATJoystickManager *ATCreateJoystickManager() {
	return new ATJoystickManager;
}

ATJoystickManager::ATJoystickManager()
	: mbCOMInitialized(false)
	, mbCaptureMode(false)
	, mhwnd(NULL)
	, mpInputManager(NULL)
	, mDeadZone(512)
{
}

ATJoystickManager::~ATJoystickManager() {
	Shutdown();
}

bool ATJoystickManager::Init(void *hwnd, ATInputManager *inputMan) {
	HRESULT hr;

	
	if (!mbCOMInitialized) {
		hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
		if (FAILED(hr))
			return false;

		mbCOMInitialized = true;
	}

	if (!mpDI) {
		hr = CoCreateInstance(CLSID_DirectInput8, NULL, CLSCTX_INPROC_SERVER, IID_IDirectInput8, (void **)~mpDI);
		if (FAILED(hr)) {
			Shutdown();
			return false;
		}

		hr = mpDI->Initialize(GetModuleHandle(NULL), DIRECTINPUT_VERSION);
		if (FAILED(hr)) {
			Shutdown();
			return false;
		}
	}

	mhwnd = (HWND)hwnd;
	mpInputManager = inputMan;

	RescanForDevices();
	return true;
}

void ATJoystickManager::Shutdown() {
	while(!mControllers.empty()) {
		ATController *ctrl = mControllers.back();
		mControllers.pop_back();

		delete ctrl;
	}

	mpDI.clear();

	mpInputManager = NULL;

	if (mbCOMInitialized) {
		CoUninitialize();
		mbCOMInitialized = false;
	}
}

void ATJoystickManager::RescanForDevices() {
	Controllers::iterator it(mControllers.begin()), itEnd(mControllers.end());
	for(; it!=itEnd; ++it) {
		ATController *ctrl = *it;
		ctrl->SetMarked(false);
	}

	mpDI->EnumDevices(DI8DEVCLASS_GAMECTRL, StaticJoystickCallback, this, DIEDFL_ATTACHEDONLY);

	it = mControllers.begin();
	while(it != mControllers.end()) {
		ATController *ctrl = *it;

		if (ctrl->IsMarked()) {
			++it;
		} else {
			ctrl->Shutdown();
			delete ctrl;

			if (&*it == &mControllers.back()) {
				mControllers.pop_back();
				break;
			}

			*it = mControllers.back();
			mControllers.pop_back();
		}
	}
}

void ATJoystickManager::Poll() {
	if (mbCaptureMode)
		return;

	Controllers::const_iterator it(mControllers.begin()), itEnd(mControllers.end());
	for(; it!=itEnd; ++it) {
		ATController *ctrl = *it;

		ctrl->Poll();
	}
}

bool ATJoystickManager::PollForCapture(int& unit, uint32& inputCode) {
	Controllers::const_iterator it(mControllers.begin()), itEnd(mControllers.end());
	for(; it!=itEnd; ++it) {
		ATController *ctrl = *it;

		if (ctrl->PollForCapture(unit, inputCode))
			return true;
	}

	return false;
}

void ATJoystickManager::SetDeadZone(int zone) {
	Controllers::const_iterator it(mControllers.begin()), itEnd(mControllers.end());
	for(; it!=itEnd; ++it) {
		ATController *ctrl = *it;

		ctrl->SetDeadZone(zone);
	}
}

uint32 ATJoystickManager::GetJoystickPortStates() const {
	return 0;
}

BOOL CALLBACK ATJoystickManager::StaticJoystickCallback(LPCDIDEVICEINSTANCE devInst, LPVOID pvThis) {
	ATJoystickManager *pThis = (ATJoystickManager *)pvThis;

	return pThis->JoystickCallback(devInst);
}

BOOL ATJoystickManager::JoystickCallback(LPCDIDEVICEINSTANCE devInst) {
	ATInputUnitIdentifier id;
	memcpy(&id, &devInst->guidInstance, sizeof id);

	Controllers::const_iterator it(mControllers.begin()), itEnd(mControllers.end());
	for(; it!=itEnd; ++it) {
		ATController *ctrl = *it;

		if (ctrl->GetId() == id) {
			ctrl->SetMarked(true);
			return DIENUM_CONTINUE;
		}
	}

	vdautoptr<ATController> dev(new ATController(mpDI));

	if (dev) {
		if (dev->Init(devInst, mhwnd, mpInputManager)) {
			dev->SetDeadZone(mDeadZone);
			dev->SetMarked(true);
			mControllers.push_back(dev);
			dev.release();
		}
	}

	return DIENUM_CONTINUE;
}
