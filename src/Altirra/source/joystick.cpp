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
#include <comutil.h>
#include <setupapi.h>
#include <xinput.h>
#include <vd2/system/bitmath.h>
#include <vd2/system/math.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/vdalloc.h>
#include "joystick.h"
#include "inputmanager.h"

#pragma comment(lib, "dxguid")
#pragma comment(lib, "dinput8")
#pragma comment(lib, "setupapi")

// This code is similar to that from the "XInput and DirectInput" article on MSDN to detect
// XInput controllers. However, it uses the Setup API to get access to the hardware IDs
// instead of WMI, which is slower and more annoying. Also, the original code leaked memory
// due to failing to call VariantClear().

namespace {
	#ifndef GUID_DEVINTERFACE_HID
		const GUID GUID_DEVINTERFACE_HID = {0x4D1E55B2, 0xF16F, 0x11CF, 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 };
	#endif

	void GetXInputDevices(vdfastvector<DWORD>& devs) {
		// open the device collection for all HID devices
		HDEVINFO hdi = SetupDiGetClassDevs(&GUID_DEVINTERFACE_HID, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
		if (hdi == INVALID_HANDLE_VALUE)
			return;

		// enumerate through all the HID-class devices
		vdblock<WCHAR> buf(128);

		UINT idx = 0;
		for(;;) {
			SP_DEVINFO_DATA sdd = {sizeof(SP_DEVINFO_DATA)};

			// get next specific device
			if (!SetupDiEnumDeviceInfo(hdi, idx++, &sdd))
				break;

			// read the hardware ID string
			DWORD regType = 0;
			DWORD required = 0;
			if (!SetupDiGetDeviceRegistryPropertyW(hdi, &sdd, SPDRP_HARDWAREID, &regType, (PBYTE)buf.data(), (buf.size() - 1) * sizeof(buf[0]), &required)
				&& GetLastError() == ERROR_INSUFFICIENT_BUFFER)
			{
				buf.resize(required / sizeof(WCHAR) + 2);

				if (!SetupDiGetDeviceRegistryPropertyW(hdi, &sdd, SPDRP_HARDWAREID, &regType, (PBYTE)buf.data(), (buf.size() - 1) * sizeof(buf[0]), &required))
					continue;
			}

			// check that we actually got a string -- it should be of MULTI_SZ type, but we can take
			// SZ too as we only care about the first one anyway
			if (regType != REG_MULTI_SZ && regType != REG_SZ)
				continue;

			// force null termination
			buf.back() = 0;

			// check if the hardware ID string contains &IG_
			const WCHAR *s = buf.data();
			if (wcsstr(s, L"&IG_")) {
				// yup, it's an Xbox 360 controller under the Xbox driver -- grab the VID and PID
				const WCHAR *pidstr = wcsstr(s, L"PID_");
				const WCHAR *vidstr = wcsstr(s, L"VID_");

				unsigned pid;
				unsigned vid;
				if (pidstr &&
					vidstr &&
					1 == swscanf(pidstr+4, L"%X", &pid) &&
					1 == swscanf(vidstr+4, L"%X", &vid))
				{
					devs.push_back(MAKELONG(vid, pid));
				}
			}
		}

		SetupDiDestroyDeviceInfoList(hdi);
	}

	template<class T>
	bool BindProcAddress(T *&ptr, HMODULE hmod, const char *name) {
		ptr = (T *)GetProcAddress(hmod, name);

		return ptr != NULL;
	}
}

///////////////////////////////////////////////////////////////////////////

struct ATXInputBinding {
	HMODULE mhmodXInput;
	DWORD (WINAPI *mpXInputGetState)(DWORD, XINPUT_STATE *);

	ATXInputBinding();
	~ATXInputBinding();

	bool IsInited() const { return mhmodXInput != NULL; }

	bool Init();
	void Shutdown();
};

ATXInputBinding::ATXInputBinding()
	: mhmodXInput(NULL)
{
}

ATXInputBinding::~ATXInputBinding() {
	Shutdown();
}

bool ATXInputBinding::Init() {
	if (mhmodXInput)
		return true;

	// We prefer XInput 1.3 to get access to the guide button, but if we can't
	// get that, fall back to XInput 0.91.
	bool have13 = true;

	mhmodXInput = LoadLibraryW(L"xinput1_3");
	if (!mhmodXInput) {
		have13 = false;

		mhmodXInput = LoadLibraryW(L"xinput9_1_0");
		if (!mhmodXInput)
			return false;
	}

	// Try to get access to XInputGetStateEx() first, and if not, fall back
	// to regular XInputGetState().
	if (!have13 || !BindProcAddress(mpXInputGetState, mhmodXInput, (char *)100)) {
		if (!BindProcAddress(mpXInputGetState, mhmodXInput, "XInputGetState")) {
			Shutdown();
			return false;
		}
	}

	// all good
	return true;
}

void ATXInputBinding::Shutdown() {
	if (mhmodXInput) {
		FreeLibrary(mhmodXInput);
		mhmodXInput = NULL;
	}
}

///////////////////////////////////////////////////////////////////////////

class ATController {
public:
	ATController();
	virtual ~ATController() {}

	const ATInputUnitIdentifier& GetId() const { return mId; }

	bool IsMarked() const { return mbMarked; }
	void SetMarked(bool mark) { mbMarked = mark;}

	virtual void SetDeadZone(int zone) {}

	virtual void Poll() = 0;
	virtual bool PollForCapture(int& unit, uint32& inputCode) = 0;

protected:
	bool mbMarked;
	ATInputUnitIdentifier mId;
};

ATController::ATController()
	: mbMarked(false)
{
}

///////////////////////////////////////////////////////////////////////////

class ATControllerXInput : public ATController, public IATInputUnitNameSource {
public:
	ATControllerXInput(ATXInputBinding& xinput, uint32 xid, ATInputManager *inputMan, const ATInputUnitIdentifier& id);
	~ATControllerXInput();

	virtual void SetDeadZone(int zone) { mDeadZone = zone << 5; }

	virtual void Poll();
	virtual bool PollForCapture(int& unit, uint32& inputCode);

public:
	virtual bool GetInputCodeName(uint32 id, VDStringW& name) const;

protected:
	struct DecodedState {
		sint32 mAxisVals[6];
		uint32 mAxisButtons;
		uint32 mButtons;
	};

	void PollState(DecodedState& state, const XINPUT_STATE& xis);
	static void ConvertStick(sint32 dst[2], sint32 x, sint32 y, float threshold);

	ATXInputBinding& mXInput;
	ATInputManager *const mpInputManager;
	uint32 mXid;
	int mUnit;
	sint32 mDeadZone;

	DWORD mLastPacketId;
	DecodedState mLastState;
};

ATControllerXInput::ATControllerXInput(ATXInputBinding& xinput, uint32 xid, ATInputManager *inputMan, const ATInputUnitIdentifier& id)
	: mXInput(xinput)
	, mpInputManager(inputMan)
	, mXid(xid)
	, mDeadZone(16384)
	, mLastPacketId(0)
	, mLastState()
{
	mId = id;

	VDStringW name;
	name.sprintf(L"XInput Controller #%u", xid + 1);
	mUnit = inputMan->RegisterInputUnit(mId, name.c_str(), this);
}

ATControllerXInput::~ATControllerXInput() {
}

void ATControllerXInput::Poll() {
	XINPUT_STATE xis;

	if (ERROR_SUCCESS != mXInput.mpXInputGetState(mXid, &xis))
		return;

	if (mLastPacketId && mLastPacketId == xis.dwPacketNumber)
		return;

	mLastPacketId = xis.dwPacketNumber;

	DecodedState dstate;

	PollState(dstate, xis);

	const uint32 axisButtonDelta = dstate.mAxisButtons ^ mLastState.mAxisButtons;
	for(uint32 i=0; i<16; ++i) {
		if (axisButtonDelta & (1 << i)) {
			if (dstate.mAxisButtons & (1 << i))
				mpInputManager->OnButtonDown(mUnit, kATInputCode_JoyStick1Left + i);
			else
				mpInputManager->OnButtonUp(mUnit, kATInputCode_JoyStick1Left + i);
		}
	}

	const uint32 buttonDelta = dstate.mButtons ^ mLastState.mButtons;
	for(size_t i=0; i<11; ++i) {
		if (buttonDelta & (1 << i)) {
			if (dstate.mButtons & (1 << i))
				mpInputManager->OnButtonDown(mUnit, kATInputCode_JoyButton0 + i);
			else
				mpInputManager->OnButtonUp(mUnit, kATInputCode_JoyButton0 + i);
		}
	}

	for(int i=0; i<6; ++i) {
		if (dstate.mAxisVals[i] != mLastState.mAxisVals[i])
			mpInputManager->OnAxisInput(mUnit, kATInputCode_JoyHoriz1 + i, dstate.mAxisVals[i], dstate.mAxisVals[i]);
	}

	mLastState = dstate;
}

bool ATControllerXInput::PollForCapture(int& unit, uint32& inputCode) {

	XINPUT_STATE xis;

	if (ERROR_SUCCESS != mXInput.mpXInputGetState(mXid, &xis))
		return false;

	if (mLastPacketId && mLastPacketId == xis.dwPacketNumber)
		return false;

	mLastPacketId = xis.dwPacketNumber;

	DecodedState state;
	PollState(state, xis);

	const uint32 newButtons = state.mButtons & ~mLastState.mButtons;
	const uint32 newAxisButtons = state.mAxisButtons & ~mLastState.mAxisButtons;

	mLastState = state;

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

bool ATControllerXInput::GetInputCodeName(uint32 id, VDStringW& name) const {
	static const wchar_t *const kButtonNames[]={
		L"A button",
		L"B button",
		L"X button",
		L"Y button",
		L"LB button",
		L"RB button",
		L"Back button",
		L"Start button",
		L"Left thumb button",
		L"Right thumb button",
		L"Guide button",
	};

	uint32 buttonIdx = (uint32)(id - kATInputCode_JoyButton0);

	if (buttonIdx < vdcountof(kButtonNames)) {
		name = kButtonNames[buttonIdx];
		return true;
	}

	static const wchar_t *const kAxisButtonNames[]={
		L"Left stick left",
		L"Left stick right",
		L"Left stick up",
		L"Left stick down",
		L"Right trigger pressed",
		L"Left trigger pressed",
		L"Right stick left",
		L"Right stick right",
		L"Right stick up",
		L"Right stick down",
		NULL,
		NULL,
		L"D-pad left",
		L"D-pad right",
		L"D-pad up",
		L"D-pad down",
	};

	const uint32 axisButtonIdx = (uint32)(id - kATInputCode_JoyStick1Left);
	if (axisButtonIdx < vdcountof(kAxisButtonNames) && kAxisButtonNames[axisButtonIdx]) {
		name = kAxisButtonNames[axisButtonIdx];
		return true;
	}

	static const wchar_t *const kAxisNames[]={
		L"Left stick horiz.",
		L"Left stick vert.",
		L"Left trigger",
		L"Right stick horiz.",
		L"Right stick vert.",
		L"Right trigger",
	};

	const uint32 axisIdx = (uint32)(id - kATInputCode_JoyHoriz1);
	if (axisIdx < vdcountof(kAxisNames)) {
		name = kAxisNames[axisIdx];
		return true;
	}

	return false;
}

void ATControllerXInput::PollState(DecodedState& state, const XINPUT_STATE& xis) {
	ConvertStick(state.mAxisVals, xis.Gamepad.sThumbLX, xis.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
	ConvertStick(state.mAxisVals + 3, xis.Gamepad.sThumbRX, xis.Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);

	state.mAxisVals[1] = -state.mAxisVals[1];
	state.mAxisVals[4] = -state.mAxisVals[4];

	for(int i=0; i<2; ++i) {
		int val = i ? xis.Gamepad.bRightTrigger : xis.Gamepad.bLeftTrigger;
		sint32 res;

		if (val < XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
			res = 0;
		else {
			const sint32 lim = 255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD;

			res = ((val - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) * 65536 + (lim >> 1)) / lim;
		}

		if (i)
			state.mAxisVals[2] = res;
		else
			state.mAxisVals[5] = res;
	}

	// Axis buttons 0-3 map to -/+X and -/+Y on left stick.
	// Axis button 4 maps to +RT, to match +Z from DirectInput.
	// Axis button 5 maps to +LT, to match -Z from DirectInput.
	// Axis buttons 6-9 map to -/+X and -/+Y on right stick.
	// Axis button 12 is D-pad left.
	// Axis button 13 is D-pad right.
	// Axis button 14 is D-pad up.
	// Axis button 15 is D-pad down.

	uint32 axisButtonStates = 0;
	if (xis.Gamepad.sThumbLX < -mDeadZone)		axisButtonStates |= (1 << 0);
	if (xis.Gamepad.sThumbLX > +mDeadZone)		axisButtonStates |= (1 << 1);
	if (xis.Gamepad.sThumbLY > +mDeadZone)		axisButtonStates |= (1 << 2);
	if (xis.Gamepad.sThumbLY < -mDeadZone)		axisButtonStates |= (1 << 3);
	if (xis.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD)	axisButtonStates |= (1 << 4);
	if (xis.Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD)	axisButtonStates |= (1 << 5);
	if (xis.Gamepad.sThumbRX < -mDeadZone)		axisButtonStates |= (1 << 6);
	if (xis.Gamepad.sThumbRX > +mDeadZone)		axisButtonStates |= (1 << 7);
	if (xis.Gamepad.sThumbRY > +mDeadZone)		axisButtonStates |= (1 << 8);
	if (xis.Gamepad.sThumbRY < -mDeadZone)		axisButtonStates |= (1 << 9);
	if (xis.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT)				axisButtonStates |= (1 << 12);
	if (xis.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT)				axisButtonStates |= (1 << 13);
	if (xis.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP)					axisButtonStates |= (1 << 14);
	if (xis.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN)				axisButtonStates |= (1 << 15);

	state.mAxisButtons = axisButtonStates;

	// Button 0 is A (definitely not cross).
	// Button 1 is B (definitely not circle).
	// Button 2 is X (definitely not square).
	// Button 3 is Y (definitely not triangle).
	// Button 4 is left shoulder.
	// Button 5 is right shoulder.
	// Button 6 is back.
	// Button 7 is start.
	// Button 8 is left stick.
	// Button 9 is right stick.
	// Button 10 is the Xbox button.
	static const WORD kButtonMappings[]={
		XINPUT_GAMEPAD_A,
		XINPUT_GAMEPAD_B,
		XINPUT_GAMEPAD_X,
		XINPUT_GAMEPAD_Y,
		XINPUT_GAMEPAD_LEFT_SHOULDER,
		XINPUT_GAMEPAD_RIGHT_SHOULDER,
		XINPUT_GAMEPAD_BACK,
		XINPUT_GAMEPAD_START,
		XINPUT_GAMEPAD_LEFT_THUMB,
		XINPUT_GAMEPAD_RIGHT_THUMB,
		0x400,		// undocumented from XInputGetStateEx - guide button
	};

	uint32 buttonStates = 0;
	for(size_t i=0; i<vdcountof(kButtonMappings); ++i) {
		if (xis.Gamepad.wButtons & kButtonMappings[i])
			buttonStates += (1 << i);
	}

	state.mButtons = buttonStates;		
}

void ATControllerXInput::ConvertStick(sint32 dst[2], sint32 x, sint32 y, float threshold) {
	const float fx = (float)x;
	const float fy = (float)y;
	const float mag = sqrtf(fx*fx + fy*fy);
	sint32 rx = 0;
	sint32 ry = 0;

	if (mag > threshold) {
		const float scale = 65536.0f * (mag - threshold) / (mag * (32767 - threshold));

		rx = VDRoundToInt(fx * scale);
		ry = VDRoundToInt(fy * scale);

		if (rx < -65536) rx = -65536; else if (rx > 65536) rx = 65536;
		if (ry < -65536) ry = -65536; else if (ry > 65536) ry = 65536;
	}

	dst[0] = rx;
	dst[1] = ry;
}

///////////////////////////////////////////////////////////////////////////

class ATControllerDirectInput : public ATController {
public:
	ATControllerDirectInput(IDirectInput8 *di);
	~ATControllerDirectInput();

	bool Init(LPCDIDEVICEINSTANCE devInst, HWND hwnd, ATInputManager *inputMan);
	void Shutdown();

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

	int			mUnit;
	DecodedState	mLastSentState;
	DecodedState	mLastPolledState;
	sint32		mDeadZone;
	DIJOYSTATE	mState;
};

ATControllerDirectInput::ATControllerDirectInput(IDirectInput8 *di)
	: mpDI(di)
	, mUnit(-1)
	, mpInputManager(NULL)
	, mDeadZone(512)
{
	memset(&mState, 0, sizeof mState);
	memset(&mLastSentState, 0, sizeof mLastSentState);
	memset(&mLastPolledState, 0, sizeof mLastPolledState);
}

ATControllerDirectInput::~ATControllerDirectInput() {
	Shutdown();
}

bool ATControllerDirectInput::Init(LPCDIDEVICEINSTANCE devInst, HWND hwnd, ATInputManager *inputMan) {
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
	mUnit = inputMan->RegisterInputUnit(mId, devInst->tszInstanceName, NULL);

	return true;
}

void ATControllerDirectInput::Shutdown() {
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

void ATControllerDirectInput::Poll() {
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

bool ATControllerDirectInput::PollForCapture(int& unit, uint32& inputCode) {
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

void ATControllerDirectInput::UpdateButtons(int baseId, uint32 states, uint32 mask) {
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

void ATControllerDirectInput::PollState(DecodedState& state) {
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

	vdfastvector<DWORD> mXInputDeviceIds;
	ATXInputBinding mXInputBinding;
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

	// try to initialize XInput -- OK if this fails
	mXInputBinding.Init();

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

	mXInputBinding.Shutdown();

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

	if (mXInputBinding.IsInited()) {
		mXInputDeviceIds.clear();
		GetXInputDevices(mXInputDeviceIds);

		// Since we're XInput-based, we don't necessarily have a GUID. Therefore, we
		// make one up.
		// {B1C7FF47-2B26-4F71-9ADB-2B23C08BA8C3}
		static const ATInputUnitIdentifier kBaseXInputId = {
			{
				(char)0x47, (char)0xFF, (char)0xC7, (char)0xB1, (char)0x26, (char)0x2B, (char)0x71, (char)0x4F,
				(char)0x9A, (char)0xDB, (char)0x2B, (char)0x23, (char)0xC0, (char)0x8B, (char)0xA8, (char)0xC3
			}
		};

		ATInputUnitIdentifier id = kBaseXInputId;

		XINPUT_STATE xis;

		for(int i=0; i<4; ++i) {
			++id.buf[15];

			if (ERROR_SUCCESS == mXInputBinding.mpXInputGetState(i, &xis)) {
				ATInputUnitIdentifier id = kBaseXInputId;
				id.buf[15] += i;

				Controllers::const_iterator it(mControllers.begin()), itEnd(mControllers.end());
				bool found = false;

				for(; it!=itEnd; ++it) {
					ATController *ctrl = *it;

					if (ctrl->GetId() == id) {
						ctrl->SetMarked(true);
						found = true;
						break;
					}
				}

				if (!found) {
					vdautoptr<ATControllerXInput> dev(new ATControllerXInput(mXInputBinding, i, mpInputManager, id));

					if (dev) {
						dev->SetMarked(true);
						mControllers.push_back(dev);
						dev.release();
					}
				}
			}
		}
	}

	std::sort(mXInputDeviceIds.begin(), mXInputDeviceIds.end());

	mpDI->EnumDevices(DI8DEVCLASS_GAMECTRL, StaticJoystickCallback, this, DIEDFL_ATTACHEDONLY);

	// garbage collect dead controllers
	it = mControllers.begin();
	while(it != mControllers.end()) {
		ATController *ctrl = *it;

		if (ctrl->IsMarked()) {
			++it;
		} else {
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
	// check if this device is already covered under XInput
	if (std::binary_search(mXInputDeviceIds.begin(), mXInputDeviceIds.end(), devInst->guidProduct.Data1))
		return DIENUM_CONTINUE;

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

	vdautoptr<ATControllerDirectInput> dev(new ATControllerDirectInput(mpDI));

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
