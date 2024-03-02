//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2015 Avery Lee
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
#include <at/atcore/deviceimpl.h>
#include <at/atcore/devicesioimpl.h>
#include <at/atcore/propertyset.h>
#include "debuggerlog.h"
#include <windows.h>
#include <mmsystem.h>

ATDebuggerLogChannel g_ATLCMIDI(false, false, "MIDI", "MIDI command activity");

class ATDeviceMidiMate final
	: public ATDevice
	, public ATDeviceSIO
	, public IATDeviceRawSIO
{
	ATDeviceMidiMate(const ATDeviceMidiMate&) = delete;
	ATDeviceMidiMate& operator=(const ATDeviceMidiMate&) = delete;
public:
	ATDeviceMidiMate();
	~ATDeviceMidiMate();

	void *AsInterface(uint32 id);

public:
	void GetDeviceInfo(ATDeviceInfo& info) override;
	void GetSettings(ATPropertySet& settings) override;
	bool SetSettings(const ATPropertySet& settings) override;
	void Init() override;
	void Shutdown() override;
	void ColdReset() override;

public:	// IATDeviceSIO
	void InitSIO(IATDeviceSIOManager *mgr) override;

public:	// IATDeviceRawSIO
	void OnCommandStateChanged(bool asserted) override;
	void OnMotorStateChanged(bool asserted) override;
	void OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) override;
	void OnSendReady() override;

protected:
	IATDeviceSIOManager *mpSIOMgr;
	bool mbActive;
	bool mbInSysEx;
	uint32 mState;
	uint8 mMsgStatus;
	uint8 mMsgData1;
	uint8 mMsgData2;

	HMIDIOUT mhmo;
};

void ATCreateDeviceMidiMate(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceMidiMate> p(new ATDeviceMidiMate);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefMidiMate = { "midimate", nullptr, L"MIDIMATE", ATCreateDeviceMidiMate };

ATDeviceMidiMate::ATDeviceMidiMate()
	: mpSIOMgr(nullptr)
	, mbActive(false)
	, mhmo(nullptr)
{
}

ATDeviceMidiMate::~ATDeviceMidiMate() {
}

void *ATDeviceMidiMate::AsInterface(uint32 id) {
	switch(id) {
		case IATDeviceSIO::kTypeID:			return static_cast<IATDeviceSIO *>(this);
		case IATDeviceRawSIO::kTypeID:		return static_cast<IATDeviceRawSIO *>(this);
	}

	return ATDevice::AsInterface(id);
}

void ATDeviceMidiMate::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefMidiMate;
}

void ATDeviceMidiMate::GetSettings(ATPropertySet& settings) {
}

bool ATDeviceMidiMate::SetSettings(const ATPropertySet& settings) {
	return true;
}

void ATDeviceMidiMate::Init() {
	midiOutOpen(&mhmo, MIDI_MAPPER, NULL, 0, CALLBACK_NULL);
}

void ATDeviceMidiMate::Shutdown() {
	if (mhmo) {
		midiOutClose(mhmo);
	}

	if (mpSIOMgr) {
		mpSIOMgr->RemoveRawDevice(this);
		mpSIOMgr->RemoveDevice(this);
		mpSIOMgr = nullptr;
	}
}

void ATDeviceMidiMate::ColdReset() {
	mState = 0;
	mbInSysEx = false;
}

void ATDeviceMidiMate::InitSIO(IATDeviceSIOManager *mgr) {
	mpSIOMgr = mgr;
	mpSIOMgr->AddRawDevice(this);
}

void ATDeviceMidiMate::OnCommandStateChanged(bool asserted) {
}

void ATDeviceMidiMate::OnMotorStateChanged(bool asserted) {
	mbActive = asserted;

	if (asserted) {
		// The MidiMate uses a 4MHz clock crystal to drive a 74HC402 binary ripple
		// counter, tapping off bit 7 to produce the 31.250 Kbaud MIDI clock.

		mpSIOMgr->SetExternalClock(this, 0, 28);		// approximation to 31.250KHz
	} else {
		mpSIOMgr->SetExternalClock(this, 0, 0);
		mState = 0;
	}
}

void ATDeviceMidiMate::OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) {
	if (!mbActive)
		return;

	if (cyclesPerBit < 52 || cyclesPerBit > 62) {
		mState = 0;
		return;
	}

	switch(mState) {
		case 0:		// initial
			if (c < 0x80)
				break;

			if (c < 0xF0 && mbInSysEx)
				break;

			mMsgStatus = c;

			switch(c >> 4) {
				case 0x80:		// note off
				case 0x90:		// note on
				case 0xA0:		// aftertouch
				case 0xB0:		// control change
				case 0xE0:		// pitch bend change
					mState = 1;
					break;
				case 0xC0:		// program change
				case 0xD0:		// channel pressure
					mState = 3;
					break;

				case 0xF0:
					switch(c) {
						case 0xF0:		// sysex
							mbInSysEx = true;
							break;

						case 0xF1:		// time code quarter frame
							mState = 1;
							break;

						case 0xF2:		// song position pointer
							mState = 1;
							break;

						case 0xF3:		// song select
							mState = 3;
							break;

						case 0xF7:		// end sysex
							mbInSysEx = false;
							break;

						case 0xF6:		// tune request
						case 0xF8:		// timing clock (24 times/quarter note)
						case 0xFA:		// start
						case 0xFB:		// continue
						case 0xFC:		// stop
						case 0xFE:		// active sensing
						case 0xFF:		// reset
							g_ATLCMIDI("Message out: %02X\n", mMsgStatus);
							if (mhmo)
								midiOutShortMsg(mhmo, mMsgStatus);
							break;
					}
					break;
			}

		case 1:		// two data bytes, first byte
			mMsgData1 = c;
			++mState;
			break;

		case 2:
			mMsgData2 = c;
			g_ATLCMIDI("Message out: %02X %02X %02X\n", mMsgStatus, mMsgData1, mMsgData2);
			mState = 0;
			if (mhmo)
				midiOutShortMsg(mhmo, mMsgStatus + ((uint32)mMsgData1 << 8) + ((uint32)mMsgData2 << 16));
			break;

		case 3:
			mMsgData1 = c;
			g_ATLCMIDI("Message out: %02X %02X\n", mMsgStatus, mMsgData1);
			if (mhmo)
				midiOutShortMsg(mhmo, mMsgStatus + ((uint32)mMsgData1 << 8));
			mState = 0;
			break;
	}
}

void ATDeviceMidiMate::OnSendReady() {
}
