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
#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/vdalloc.h>
#include "joystick.h"

#pragma comment(lib, "dxguid")
#pragma comment(lib, "dinput8")

class ATController {
public:
	ATController(IDirectInput8 *di);
	~ATController();

	bool Init(LPCDIDEVICEINSTANCE devInst, HWND hwnd);
	void Shutdown();

	uint32 Poll();

protected:
	vdrefptr<IDirectInput8> mpDI;
	vdrefptr<IDirectInputDevice8> mpDevice;

	uint32		mDigitalEmuState;
	DIJOYSTATE	mState;
};

ATController::ATController(IDirectInput8 *di)
	: mpDI(di)
	, mDigitalEmuState(0)
{
	memset(&mState, 0, sizeof mState);
}

ATController::~ATController() {
	Shutdown();
}

bool ATController::Init(LPCDIDEVICEINSTANCE devInst, HWND hwnd) {
	HRESULT hr = mpDI->CreateDevice(devInst->guidInstance, ~mpDevice, NULL);
	if (FAILED(hr))
		return false;

	hr = mpDevice->SetDataFormat(&c_dfDIJoystick);
	if (FAILED(hr)) {
		Shutdown();
		return false;
	}

	hr = mpDevice->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
	if (FAILED(hr)) {
		Shutdown();
		return false;
	}

	return true;
}

void ATController::Shutdown() {
	mpDevice = NULL;
}

uint32 ATController::Poll() {
	HRESULT hr = mpDevice->Poll();

	if (FAILED(hr))
		hr = mpDevice->Acquire();

	if (SUCCEEDED(hr))
		hr = mpDevice->GetDeviceState(sizeof(DIJOYSTATE), &mState);

	if (FAILED(hr))
		memset(&mState, 0, sizeof mState);

	mDigitalEmuState = 0;

	uint32 pov = mState.rgdwPOV[0] & 0xffff;
	if (pov < 0xffff) {
		uint32 octant = ((pov + 2250) / 4500) & 7;

		static const uint32 kPOVLookup[8]={
			0x01,	// up
			0x09,
			0x08,	// right
			0x0a,
			0x02,	// down
			0x06,
			0x04,	// left
			0x05
		};

		mDigitalEmuState = kPOVLookup[octant];
	}

	if (mState.rgbButtons[0] | mState.rgbButtons[1] | mState.rgbButtons[2] | mState.rgbButtons[3])
		mDigitalEmuState |= 0x10000;

	return mDigitalEmuState;
}

///////////////////////////////////////////////////////////////////////////////

class ATJoystickManager : public IATJoystickManager {
public:
	ATJoystickManager();
	~ATJoystickManager();

	bool Init(void *hwnd);
	void Shutdown();

	void Poll();

	uint32 GetJoystickPortStates() const;

protected:
	static BOOL CALLBACK StaticJoystickCallback(LPCDIDEVICEINSTANCE devInst, LPVOID pThis);
	BOOL JoystickCallback(LPCDIDEVICEINSTANCE devInst);

	bool mbCOMInitialized;
	HWND mhwnd;
	vdrefptr<IDirectInput8> mpDI;

	uint32	mJoyPortStates;

	typedef vdfastvector<ATController *> Controllers;
	Controllers mControllers;
};

IATJoystickManager *ATCreateJoystickManager() {
	return new ATJoystickManager;
}

ATJoystickManager::ATJoystickManager()
	: mbCOMInitialized(false)
	, mhwnd(NULL)
	, mJoyPortStates(0)
{
}

ATJoystickManager::~ATJoystickManager() {
}

bool ATJoystickManager::Init(void *hwnd) {
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
	mpDI->EnumDevices(DI8DEVCLASS_GAMECTRL, StaticJoystickCallback, this, DIEDFL_ATTACHEDONLY);

	return true;
}

void ATJoystickManager::Shutdown() {
	while(!mControllers.empty()) {
		ATController *ctrl = mControllers.back();
		mControllers.pop_back();

		delete ctrl;
	}

	if (mpDI) {
		mpDI->Release();
		mpDI = NULL;
	}

	if (mbCOMInitialized) {
		CoUninitialize();
		mbCOMInitialized = false;
	}
}

void ATJoystickManager::Poll() {
	uint32 result = 0;

	Controllers::const_iterator it(mControllers.begin()), itEnd(mControllers.end());
	for(; it!=itEnd; ++it) {
		ATController *ctrl = *it;

		result |= ctrl->Poll();
	}

	mJoyPortStates = result;
}

uint32 ATJoystickManager::GetJoystickPortStates() const {
	return mJoyPortStates;
}

BOOL CALLBACK ATJoystickManager::StaticJoystickCallback(LPCDIDEVICEINSTANCE devInst, LPVOID pvThis) {
	ATJoystickManager *pThis = (ATJoystickManager *)pvThis;

	return pThis->JoystickCallback(devInst);
}

BOOL ATJoystickManager::JoystickCallback(LPCDIDEVICEINSTANCE devInst) {
	vdautoptr<ATController> dev(new ATController(mpDI));

	if (dev) {
		if (dev->Init(devInst, mhwnd)) {
			mControllers.push_back(dev);
			dev.release();
		}
	}

	return DIENUM_CONTINUE;
}
