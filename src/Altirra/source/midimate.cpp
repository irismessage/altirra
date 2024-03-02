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
#include <at/atcore/deviceparentimpl.h>
#include <at/atcore/deviceserial.h>
#include <at/atcore/devicesioimpl.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/scheduler.h>
#include "debuggerlog.h"
#include <windows.h>
#include <mmsystem.h>

ATDebuggerLogChannel g_ATLCMIDI(false, false, "MIDI", "MIDI command activity");

////////////////////////////////////////////////////////////////////////////////

class ATMidiOutput final : public vdrefcounted<IATDeviceSerial> {
	ATMidiOutput(const ATMidiOutput&);
	ATMidiOutput& operator=(const ATMidiOutput&);
public:
	ATMidiOutput();
	~ATMidiOutput();

	void Reset();

public:
	void *AsInterface(uint32 id);

	void SetOnStatusChange(const vdfunction<void(const ATDeviceSerialStatus&)>& fn) override;
	void SetTerminalState(const ATDeviceSerialTerminalState&) override;
	ATDeviceSerialStatus GetStatus() override;
	void SetOnReadReady(vdfunction<void()> fn) override;
	bool Read(uint32 baudRate, uint8& c, bool& framingError) override;
	bool Read(uint32& baudRate, uint8& c) override;
	void Write(uint32 baudRate, uint8 c) override;

	void FlushBuffers() override;

private:
	void ResetState();
	void ProcessMessageByte(uint8 c);
	void ProcessVoiceMessageStatus(uint8 c);
	void ProcessSystemCommonMessageStatus(uint8 c);
	void ProcessDataByte(uint8 c);

	bool mbInSysEx {};
	uint32 mState {};
	uint8 mMsgStatus {};
	uint8 mMsgData1 {};
	uint8 mMsgData2 {};
	uint8 mSysExLength {};

	HMIDIOUT mhmo {};

	uint8 mSysExBuffer[256] {};
};

ATMidiOutput::ATMidiOutput() {
	midiOutOpen(&mhmo, MIDI_MAPPER, NULL, 0, CALLBACK_NULL);

	ResetState();
}

ATMidiOutput::~ATMidiOutput() {
	if (mhmo) {
		midiOutClose(mhmo);
		mhmo = nullptr;
	}
}

void ATMidiOutput::Reset() {
	ResetState();

	if (mhmo)
		midiOutReset(mhmo);
}

void *ATMidiOutput::AsInterface(uint32 id) {
	if (id == IATDeviceSerial::kTypeID)
		return static_cast<IATDeviceSerial *>(this);

	return nullptr;
}

void ATMidiOutput::SetOnStatusChange(const vdfunction<void(const ATDeviceSerialStatus&)>& fn) {
}

void ATMidiOutput::SetTerminalState(const ATDeviceSerialTerminalState&) {
}

ATDeviceSerialStatus ATMidiOutput::GetStatus() {
	return ATDeviceSerialStatus{};
}

void ATMidiOutput::SetOnReadReady(vdfunction<void()> fn) {
}

bool ATMidiOutput::Read(uint32 baudRate, uint8& c, bool& framingError) {
	return false;
}

bool ATMidiOutput::Read(uint32& baudRate, uint8& c) {
	return false;
}

void ATMidiOutput::Write(uint32 baudRate, uint8 c) {
	// reject baud rates beyond +/-5% from ideal (31.25KHz)
	if (baudRate < 29687 || baudRate > 32812) {
		mState = 0;
		return;
	}

	// real-time category message -- may be interleaved with SysEx
	if (c >= 0xF8) {
		switch(c) {
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

		return;
	}

	ProcessMessageByte(c);
}

void ATMidiOutput::FlushBuffers() {
}

void ATMidiOutput::ResetState() {
	mbInSysEx = false;
	mState = 0;
	mMsgStatus = 0;
	mMsgData1 = 0;
	mMsgData2 = 0;
	mSysExLength = 0;
}

void ATMidiOutput::ProcessMessageByte(uint8 c) {
	if (mState) {
		if (c >= 0x80) {
			// Uhh... we got a control byte when we were expecting a data byte.
			// Abort the command and process it as a status byte. This is needed by the
			// CZ-101 demo, which sends a bogus $00 byte before a $C0 status.
			mState = 0;
		} else {
			ProcessDataByte(c);
			return;
		}
	}

	if (mbInSysEx) {
		if (c >= 0x80) {
			if (c == 0xF7) {
				// end SysEx
				g_ATLCMIDI("SysEx message (ignored)\n", mMsgStatus);
			} else {
				// only real-time messages are allowed within SysEx (already handled earlier)
				return;
			}
		}

		mSysExBuffer[mSysExLength++] = c;

		if (!mSysExLength) {
			// SysEx message too long -- terminate it
			mbInSysEx = false;
		}
		return;
	}

	// check for running status
	if (c < 0x80) {
		// running status
		if (mMsgStatus < 0x80 || mMsgStatus >= 0xF0) {
			// only voice messages can use running status
			return;
		}

		ProcessVoiceMessageStatus(mMsgStatus);
		ProcessDataByte(c);
		return;
	}

	// voice messages are not allowed within system exclusive messages
	if (c < 0xF0 && mbInSysEx)
		return;

	mMsgStatus = c;

	if (c < 0xF0)
		ProcessVoiceMessageStatus(c);
	else
		ProcessSystemCommonMessageStatus(c);
}

void ATMidiOutput::ProcessVoiceMessageStatus(uint8 c) {
	VDASSERT(c >= 0x80 && c < 0xF0);

	switch (c & 0xF0) {
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
	}
}

void ATMidiOutput::ProcessSystemCommonMessageStatus(uint8 c) {
	VDASSERT(c >= 0xF0 && c < 0xF8);

	switch (c) {
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
			g_ATLCMIDI("Message out: %02X\n", mMsgStatus);
			if (mhmo)
				midiOutShortMsg(mhmo, mMsgStatus);
			break;
	}
}

void ATMidiOutput::ProcessDataByte(uint8 c) {
	switch(mState) {
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

////////////////////////////////////////////////////////////////////////////////

class ATDeviceMidiMate final
	: public ATDevice
	, public ATDeviceSIO
	, public IATDeviceRawSIO
	, public IATSchedulerCallback
{
	ATDeviceMidiMate(const ATDeviceMidiMate&) = delete;
	ATDeviceMidiMate& operator=(const ATDeviceMidiMate&) = delete;
public:
	ATDeviceMidiMate() = default;

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

public:
	void OnScheduledEvent(uint32 id) override;

protected:
	IATDeviceSIOManager *mpSIOMgr = nullptr;
	bool mbActive {};

	vdrefptr<ATMidiOutput> mpMidiOutput;
	IATDeviceSerial *mpCurrentOutput = nullptr;

	ATScheduler *mpScheduler = nullptr;
	ATEvent *mpEvent = nullptr;


	ATDeviceParentSingleChild mMidiPort;
};

void ATCreateDeviceMidiMate(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceMidiMate> p(new ATDeviceMidiMate);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefMidiMate = { "midimate", nullptr, L"MidiMate", ATCreateDeviceMidiMate };

void *ATDeviceMidiMate::AsInterface(uint32 id) {
	switch(id) {
		case IATDeviceSIO::kTypeID:			return static_cast<IATDeviceSIO *>(this);
		case IATDeviceRawSIO::kTypeID:		return static_cast<IATDeviceRawSIO *>(this);
		case IATDeviceParent::kTypeID:		return static_cast<IATDeviceParent *>(&mMidiPort);
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
	mMidiPort.Init(IATDeviceSerial::kTypeID, "serial", L"MIDI I/O port", "midiport", this);
	mMidiPort.SetOnAttach(
		[this] {
			mpCurrentOutput = mMidiPort.GetChild<IATDeviceSerial>();
			mpMidiOutput = nullptr;

			if (mpCurrentOutput) {
				mpCurrentOutput->SetOnReadReady(
					[this] {
						if (!mpEvent)
							mpScheduler->SetEvent(1, this, 1, mpEvent);
					}
				);
			}
		}
	);

	mMidiPort.SetOnDetach(
		[this] {
			if (mpCurrentOutput) {
				mpCurrentOutput->SetOnReadReady(nullptr);
				mpCurrentOutput = nullptr;
			}
		}
	);

	mpScheduler = GetService<IATDeviceSchedulingService>()->GetMachineScheduler();
}

void ATDeviceMidiMate::Shutdown() {
	mpCurrentOutput = nullptr;
	mpMidiOutput = nullptr;

	mMidiPort.Shutdown();

	if (mpScheduler) {
		mpScheduler->UnsetEvent(mpEvent);
		mpScheduler = nullptr;
	}

	if (mpSIOMgr) {
		mpSIOMgr->RemoveRawDevice(this);
		mpSIOMgr->RemoveDevice(this);
		mpSIOMgr = nullptr;
	}
}

void ATDeviceMidiMate::ColdReset() {
	if (mpMidiOutput)
		mpMidiOutput->Reset();

	mpScheduler->SetEvent(1, this, 1, mpEvent);
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

		mpSIOMgr->SetExternalClock(this, 0, 57);		// approximation to 31.250KHz
	} else {
		mpSIOMgr->SetExternalClock(this, 0, 0);
	}
}

void ATDeviceMidiMate::OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) {
	if (!mbActive || !cyclesPerBit)
		return;

	if (!mpCurrentOutput) {
		mpMidiOutput = new ATMidiOutput;
		mpCurrentOutput = mpMidiOutput;
	}

	mpCurrentOutput->Write((uint32)(0.5f + 1789772.5f / (float)cyclesPerBit), c);
}

void ATDeviceMidiMate::OnSendReady() {
}

void ATDeviceMidiMate::OnScheduledEvent(uint32 id) {
	mpEvent = nullptr;

	if (mpCurrentOutput) {
		uint8 c = 0;
		bool framingError = false;
		if (mpCurrentOutput->Read(31250, c, framingError)) {
			mpScheduler->SetEvent(573, this, 1, mpEvent);

			if (mbActive)
				mpSIOMgr->SendRawByte(c, 57, true);
		}
	}
}
