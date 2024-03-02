//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2011 Avery Lee
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
#include "rs232.h"
#include "cpu.h"
#include "cpumemory.h"
#include "cio.h"
#include "sio.h"
#include "ksyms.h"
#include "scheduler.h"
#include "modem.h"
#include "pokey.h"
#include "debuggerlog.h"
#include "pia.h"

ATDebuggerLogChannel g_ATLC850SIO(false, false, "850", "850 interface SIO activity");
ATDebuggerLogChannel g_ATLCModemData(false, false, "MODEMDATA", "Modem interface data");

//////////////////////////////////////////////////////////////////////////

class IATRS232ChannelCallback {
public:
	virtual void OnChannelReceiveReady(int index) = 0;
};

//////////////////////////////////////////////////////////////////////////

class ATRS232Channel : public IATSchedulerCallback, public IATRS232DeviceCallback {
public:
	virtual ~ATRS232Channel();

	virtual void Init(int index, IATRS232ChannelCallback *cb, ATCPUEmulatorMemory *mem, ATScheduler *sched, ATScheduler *slowsched, IATUIRenderer *uir, ATPokeyEmulator *pokey, ATPIAEmulator *pia) = 0;
	virtual void Shutdown() = 0;

	virtual void ColdReset() = 0;

	virtual uint8 Open(uint8 aux1, uint8 aux2, bool sioBased) = 0;
	virtual void Close() = 0;

	virtual uint8 GetCIOPermissions() const = 0;
	virtual uint32 GetOutputLevel() const = 0;

	virtual bool SetConcurrentMode() = 0;

	virtual void ReconfigureDataRate(uint8 aux1, uint8 aux2, bool sioBased) = 0;
	virtual void ReconfigureTranslation(uint8 aux1, uint8 aux2) = 0;

	virtual void SetConfig(const ATRS232Config& config) = 0;

	virtual void GetStatus() = 0;
	virtual bool GetByte(uint8& c) = 0;
	virtual bool PutByte(uint8 c) = 0;

	virtual bool IsSuspended() const = 0;
	virtual void ExecuteDeviceCommand(uint8 c) = 0;

	virtual void SetTerminalState(uint8 c) = 0;
};

ATRS232Channel::~ATRS232Channel() {
}

//////////////////////////////////////////////////////////////////////////

class ATRS232Channel850 : public ATRS232Channel {
public:
	ATRS232Channel850();

	void Init(int index, IATRS232ChannelCallback *cb, ATCPUEmulatorMemory *mem, ATScheduler *sched, ATScheduler *slowsched, IATUIRenderer *uir, ATPokeyEmulator *pokey, ATPIAEmulator *pia);
	void Shutdown();

	void ColdReset();

	uint8 Open(uint8 aux1, uint8 aux2, bool sioBased);
	void Close();

	uint8 GetCIOPermissions() const { return mOpenPerms; }
	uint32 GetOutputLevel() const { return mOutputLevel; }
	uint32 GetCyclesPerByte() const { return mCyclesPerByte; }
	uint32 GetCyclesPerByteDevice() const { return mCyclesPerByteDevice; }

	void SetCyclesPerByteDevice(uint32 cyc) { mCyclesPerByteDevice = cyc; }

	bool CheckMonitoredControlLines();

	bool CheckSerialFormatConcurrentOK(uint8 aux1);

	bool SetConcurrentMode();
	void ExitConcurrentMode();

	void ReconfigureDataRate(uint8 aux1, uint8 aux2, bool sioBased);
	void ReconfigureTranslation(uint8 aux1, uint8 aux2);

	void SetConfig(const ATRS232Config& config);

	void GetStatus();
	void ReadControlStatus(uint8& errors, uint8& control);
	bool GetByte(uint8& c);
	bool PutByte(uint8 c);

	bool IsSuspended() const { return mbSuspended; }
	void ExecuteDeviceCommand(uint8 c);

	void SetTerminalState(uint8 c);

protected:
	void FlushInputBuffer();
	void OnControlStateChanged(const ATRS232ControlState& status);
	void OnScheduledEvent(uint32 id);
	void PollDevice();
	void EnqueueReceivedByte(uint8 c);

	int mIndex;
	IATRS232ChannelCallback *mpCB;
	ATScheduler	*mpScheduler;
	ATEvent	*mpEvent;
	IATRS232Device *mpDevice;
	ATCPUEmulatorMemory *mpMemory;
	ATPokeyEmulator *mpPokey;
	ATPIAEmulator *mpPIA;

	uint32	mCyclesPerByte;
	uint32	mCyclesPerByteDevice;

	bool	mbAddLFAfterEOL;
	bool	mbTranslationEnabled;
	bool	mbTranslationHeavy;
	bool	mbLFPending;
	bool	mbConcurrentMode;
	bool	mbSuspended;
	uint8	mWontTranslateChar;

	uint8	mInputParityMask;

	uint8	mOpenPerms;		// Mirror of ICAX1 at open time.
	uint8	mControlState;

	// These must match the error flags returned by STATUS.
	enum {
		kErrorFlag850_FramingError = 0x80,
		kErrorFlag1030_FramingError = 0x80,
		kErrorFlag1030_Parity = 0x20,
		kErrorFlag1030_Wraparound = 0x10,
		kErrorFlag1030_IllegalCommand = 0x01
	};

	uint8	mErrorFlags;
	uint8	mWordSize;

	uint32	mBaudRate;

	// These must match bits 0-1 of AUX1 as passed to XIO 38.
	enum OutputParityMode {
		kOutputParityNone	= 0x00,
		kOutputParityOdd	= 0x01,
		kOutputParityEven	= 0x02,
		kOutputParitySet	= 0x03
	};

	OutputParityMode	mOutputParityMode;

	ATRS232TerminalState	mTerminalState;

	enum {
		kMonitorLine_CRX = 0x01,
		kMonitorLine_CTS = 0x02,
		kMonitorLine_DSR = 0x04
	};

	uint8	mControlMonitorMask;

	int		mInputReadOffset;
	int		mInputWriteOffset;
	int		mInputLevel;
	int		mInputBufferSize;
	uint16	mInputBufAddr;
	int		mOutputReadOffset;
	int		mOutputWriteOffset;
	int		mOutputLevel;

	// This buffer is 32 bytes long internally for R:.
	uint8	mInputBuffer[32];
	uint8	mOutputBuffer[32];

	ATRS232Config mConfig;
};

ATRS232Channel850::ATRS232Channel850()
	: mpCB(NULL)
	, mpScheduler(NULL)
	, mpEvent(NULL)
	, mpDevice(new ATModemEmulator)
	, mpMemory(NULL)
	, mpPokey(NULL)
	, mpPIA(NULL)
	, mbAddLFAfterEOL(false)
	, mbTranslationEnabled(false)
	, mbTranslationHeavy(false)
	, mbSuspended(true)
	, mWontTranslateChar(0)
	, mInputParityMask(0)
	, mOpenPerms(0)
	, mControlState(0)
	, mErrorFlags(0)
	, mBaudRate(300)
	, mOutputParityMode(kOutputParityNone)
	, mControlMonitorMask(0)
	, mInputReadOffset(0)
	, mInputWriteOffset(0)
	, mInputLevel(0)
	, mInputBufferSize(0)
	, mInputBufAddr(0)
	, mOutputReadOffset(0)
	, mOutputWriteOffset(0)
	, mOutputLevel(0)
{
}

void ATRS232Channel850::Init(int index, IATRS232ChannelCallback *cb, ATCPUEmulatorMemory *mem, ATScheduler *sched, ATScheduler *slowsched, IATUIRenderer *uir, ATPokeyEmulator *pokey, ATPIAEmulator *pia) {
	mIndex = index;
	mpCB = cb;
	mpMemory = mem;
	mpScheduler = sched;
	mpPokey = pokey;
	mpPIA = pia;

	mpDevice->SetCallback(this);
	static_cast<ATModemEmulator *>(mpDevice)->Init(sched, slowsched, uir);

	// compute initial control state
	ATRS232ControlState cstate(mpDevice->GetControlState());

	mControlState = 0;
	
	if (cstate.mbCarrierDetect)
		mControlState += 0x0c;

	if (cstate.mbClearToSend)
		mControlState += 0x30;

	if (cstate.mbDataSetReady)
		mControlState += 0xc0;

	// Default to 300 baud, 8 data bits, 1 stop bit, no input parity, space output parity,
	// no CR after LF, light translation. This must NOT be reapplied on each open or it
	// breaks BobTerm.
	ReconfigureDataRate(0, 0, false);
	ReconfigureTranslation(0x10, 0);

	mCyclesPerByteDevice = mCyclesPerByte;
}

void ATRS232Channel850::Shutdown() {
	if (mpDevice) {
		delete mpDevice;
		mpDevice = NULL;
	}

	if (mpScheduler) {
		if (mpEvent) {
			mpScheduler->RemoveEvent(mpEvent);
			mpEvent = NULL;
		}

		mpScheduler = NULL;
	}

	mpPIA = NULL;
	mpPokey = NULL;
}

void ATRS232Channel850::ColdReset() {
	Close();

	if (mpDevice)
		mpDevice->ColdReset();

	mControlMonitorMask = 0;
}

uint8 ATRS232Channel850::Open(uint8 aux1, uint8 aux2, bool sioBased) {
	if (!mpEvent)
		mpEvent = mpScheduler->AddEvent(mCyclesPerByte, this, 1);

	mOpenPerms = aux1;
	mInputReadOffset = 0;
	mInputWriteOffset = 0;
	mInputLevel = 0;
	mInputBufferSize = 32;
	mInputBufAddr =0;

	mOutputReadOffset = 0;
	mOutputWriteOffset = 0;
	mOutputLevel = 0;

	mErrorFlags = 0;

	mbLFPending = false;

	// AUX1:
	//	bit 0 = concurrent mode
	//	bit 2 = input mode
	//	bit 3 = output mode

	// We ignore the concurrent I/O bit for now because concurrent I/O doesn't actually
	// start until XIO 40 is issued. This is required by ForemXEP, which opens for
	// concurrent mode but polls modem status before actually entering it. Not handling
	// this correctly results in ForemXEP immediately dropping calls because it thinks
	// the modem has lost carrier (it reads the input empty indicator as !CD).
	//mbConcurrentMode = (aux1 & 1) != 0;

	mbSuspended = false;

	if (!sioBased) {
		// ICD Multi I/O Manual Chapter 6: Open sets RTS and DTR to high.
		mTerminalState.mbDataTerminalReady = true;
		mTerminalState.mbRequestToSend = true;
	}

	if (mpDevice)
		mpDevice->SetTerminalState(mTerminalState);

	FlushInputBuffer();

	return ATCIOSymbols::CIOStatSuccess;
}

void ATRS232Channel850::Close() {
	if (mpEvent) {
		mpScheduler->RemoveEvent(mpEvent);
		mpEvent = NULL;
	}

	if (mbConcurrentMode)
		mErrorFlags = 0;

	// LAMEAPP: This is required to make BBS Express work -- it expects to be able
	// to close a channel and then issue a status request to read CD without
	// re-opening the stream first.
	mbConcurrentMode = false;

	mbSuspended = true;
}

bool ATRS232Channel850::CheckMonitoredControlLines() {
	if (!mControlMonitorMask)
		return true;

	// We need to translate from the current+change pairs returned by
	// the status command to the test mask passed into XIO 36.
	uint8 currentState = 0;

	if (mControlState & 0x08)
		currentState += kMonitorLine_CRX;

	if (mControlState & 0x20)
		currentState += kMonitorLine_CTS;

	if (mControlState & 0x80)
		currentState += kMonitorLine_DSR;

	// Fail the transition if a monitored line is not asserted (on).
	if (~currentState & mControlMonitorMask) {
		// Set the external device not ready bit (bit 2).
		mErrorFlags |= 0x04;
		return false;
	}

	return true;
}

bool ATRS232Channel850::CheckSerialFormatConcurrentOK(uint8 iodir) {
	// The 850 can only handle input-only 300 baud max for odd word sizes.
	if (mWordSize < 8 && (mBaudRate > 300 || (iodir & 0x0c) != 0x04)) {
		// set interface error flag
		mErrorFlags |= 0x01;
		return false;
	}

	return true;
}

bool ATRS232Channel850::SetConcurrentMode() {
	if (!CheckMonitoredControlLines())
		return false;

	mbConcurrentMode = true;

	FlushInputBuffer();
	return true;
}

void ATRS232Channel850::ExitConcurrentMode() {
	mbConcurrentMode = false;
}

void ATRS232Channel850::ReconfigureDataRate(uint8 aux1, uint8 aux2, bool sioBased) {
	static const uint32 kBaudTable[16]={
		300,		// 0000 = 300 baud
		45,			// 0001 = 45.5 baud
		50,			// 0010 = 50 baud
		57,			// 0011 = 56.875 baud
		75,			// 0100 = 75 baud
		110,		// 0101 = 110 baud
		135,		// 0110 = 134.5 baud
		150,		// 0111 = 150 baud
		300,		// 1000 = 300 baud
		600,		// 1001 = 600 baud
		1200,		// 1010 = 1200 baud
		1800,		// 1011 = 1800 baud
		2400,		// 1100 = 2400 baud
		4800,		// 1101 = 4800 baud
		9600,		// 1110 = 9600 baud
		19200,		// 1111 = 19200 baud
	};

	static const uint32 kPeriodTable[16]={
		 59659,		// 0000 = 300 baud
		393357,		// 0001 = 45.5 baud
		357955,		// 0010 = 50 baud
		314685,		// 0011 = 56.875 baud
		238636,		// 0100 = 75 baud
		162707,		// 0101 = 110 baud
		133069,		// 0110 = 134.5 baud
		119318,		// 0111 = 150 baud
		 59659,		// 1000 = 300 baud
		 29830,		// 1001 = 600 baud
		 14915,		// 1010 = 1200 baud
		  9943,		// 1011 = 1800 baud
		  7457,		// 1100 = 2400 baud
		  3729,		// 1101 = 4800 baud
		  1864,		// 1110 = 9600 baud
		   932,		// 1111 = 19200 baud
	};

	uint32 baudIdx = aux1 & 15;
	mBaudRate = kBaudTable[baudIdx];
	mCyclesPerByte = kPeriodTable[baudIdx];

	if (!sioBased)
		mCyclesPerByteDevice = mCyclesPerByte;

	mWordSize = 8 - ((aux1 >> 4) & 3);

	if (mConfig.mbExtendedBaudRates) {	// Atari800 extension
		if (aux1 & 0x40) {
			mBaudRate = 230400;
			mCyclesPerByte = 78;		// 77.68
		} else if (baudIdx == 1) {
			mBaudRate = 57600;
			mCyclesPerByte = 311;		// 310.7
		} else if (baudIdx == 3) {
			mBaudRate = 115200;
			mCyclesPerByte = 155;		// 155.36
		}
	}

	mControlMonitorMask = aux2 & 7;
	mErrorFlags = 0;
}

void ATRS232Channel850::ReconfigureTranslation(uint8 aux1, uint8 aux2) {
	mbTranslationEnabled = (aux1 & 0x20) == 0;
	mbTranslationHeavy = (aux1 & 0x10) != 0;

	// EOL -> CR+LF is only available if translation is enabled.
	mbAddLFAfterEOL = mbTranslationEnabled && (aux1 & 0x40);

	mWontTranslateChar = aux2;

	mInputParityMask = (aux1 & 0x0c) ? 0x7F : 0xFF;
	mOutputParityMode = (OutputParityMode)(aux1 & 0x03);

	mErrorFlags = 0;
}

void ATRS232Channel850::SetConfig(const ATRS232Config& config) {
	mConfig = config;
	mpDevice->SetConfig(config);
}

void ATRS232Channel850::GetStatus() {
	if (mConfig.mbDisableThrottling)
		PollDevice();

	mpMemory->WriteByte(ATKernelSymbols::DVSTAT, mErrorFlags);

	if (mbConcurrentMode) {
		mpMemory->WriteByte(ATKernelSymbols::DVSTAT+1, mInputLevel & 0xFF);
	} else {
		mpMemory->WriteByte(ATKernelSymbols::DVSTAT+1, mControlState);
		mErrorFlags = 0;
	}

	mpMemory->WriteByte(ATKernelSymbols::DVSTAT+2, mInputLevel >> 8);
	mpMemory->WriteByte(ATKernelSymbols::DVSTAT+3, mOutputLevel);

	// reset sticky bits
	mControlState &= 0xa8;
	mControlState += (mControlState >> 1);
}

void ATRS232Channel850::ReadControlStatus(uint8& errors, uint8& control) {
	errors = 0;
	control = mControlState;

	mErrorFlags = 0;

	// reset sticky bits
	mControlState &= 0xa8;
	mControlState += (mControlState >> 1);
}

bool ATRS232Channel850::GetByte(uint8& c) {
	if (mConfig.mbDisableThrottling)
		PollDevice();

	if (!mInputLevel)
		return false;

	if (mInputBufAddr)
		c = mpMemory->ReadByte(mInputBufAddr + mInputReadOffset);
	else
		c = mInputBuffer[mInputReadOffset];

	if (++mInputReadOffset >= mInputBufferSize)
		mInputReadOffset = 0;

	--mInputLevel;

	c &= mInputParityMask;

	if (mbTranslationEnabled) {
		// strip high bit
		c &= 0x7f;

		// convert CR to EOL
		if (c == 0x0D)
			c = 0x9B;
		else if (mbTranslationHeavy && (uint8)(c - 0x20) > 0x5C)
			c = mWontTranslateChar;
	}

	return true;
}

bool ATRS232Channel850::PutByte(uint8 c) {
	if (mbLFPending)
		c = 0x0A;

	if (mbTranslationEnabled) {
		// convert EOL to CR
		if (c == 0x9B)
			c = 0x0D;

		if (mbTranslationHeavy) {
			// heavy translation -- drop bytes <$20 or >$7C
			if ((uint8)(c - 0x20) > 0x5C && c != 0x0D)
				return true;
		} else {
			// light translation - strip high bit
			c &= 0x7f;
		}
	}

	for(;;) {
		if (mOutputLevel >= sizeof mOutputBuffer)
			return false;
		
		uint8 d = c;

		static const uint8 kParityTable[16]={
			0x00, 0x80, 0x80, 0x00,
			0x80, 0x00, 0x00, 0x80,
			0x80, 0x00, 0x00, 0x80,
			0x00, 0x80, 0x80, 0x00,
		};

		switch(mOutputParityMode) {
			case kOutputParityNone:
			default:
				break;

			case kOutputParityEven:
				d ^= kParityTable[(d & 0x0F) ^ (d >> 4)];
				break;

			case kOutputParityOdd:
				d ^= kParityTable[(d & 0x0F) ^ (d >> 4)];
				d ^= 0x80;
				break;

			case kOutputParitySet:
				d |= 0x80;
				break;
		}

		mOutputBuffer[mOutputWriteOffset] = d;
		if (++mOutputWriteOffset >= sizeof mOutputBuffer)
			mOutputWriteOffset = 0;

		++mOutputLevel;

		if (mConfig.mbDisableThrottling)
			PollDevice();

		// If we just pushed out a CR byte and add-LF is enabled, we need to
		// loop around and push out another LF byte.
		if (c != 0x0D || !mbAddLFAfterEOL)
			break;

		mbLFPending = true;
		c = 0x0A;
	}

	mbLFPending = false;
	return true;
}

void ATRS232Channel850::ExecuteDeviceCommand(uint8 c) {
}

void ATRS232Channel850::SetTerminalState(uint8 c) {
	bool changed = false;

	if (c & 0x80) {
		bool dtr = (c & 0x40) != 0;

		if (mTerminalState.mbDataTerminalReady != dtr) {
			mTerminalState.mbDataTerminalReady = dtr;

			changed = true;
		}
	}

	if (c & 0x20) {
		bool rts = (c & 0x10) != 0;

		if (mTerminalState.mbRequestToSend != rts) {
			mTerminalState.mbRequestToSend = rts;

			changed = true;
		}
	}

	if (changed && mpDevice)
		mpDevice->SetTerminalState(mTerminalState);
}

void ATRS232Channel850::FlushInputBuffer() {
	mInputLevel = 0;
	mInputReadOffset = 0;
	mInputWriteOffset = 0;

	if (mpDevice)
		mpDevice->FlushOutputBuffer();
}

void ATRS232Channel850::OnControlStateChanged(const ATRS232ControlState& status) {
	// Line state transitions:
	//
	//	Prev				State	Next
	//	00 (always off)		off		00 (always off)
	//	01 (currently off)	off		01 (currently off)
	//	10 (currently on)	off		01 (currently off)
	//	11 (always on)		off		01 (currently off)
	//	00 (always off)		on		10 (currently on)
	//	01 (currently off)	on		10 (currently on)
	//	10 (currently on)	on		10 (currently on)
	//	11 (always on)		on		11 (always on)
	if (status.mbCarrierDetect) {
		if (!(mControlState & 0x08))
			mControlState = (mControlState & 0xf3) + 0x08;
	} else {
		if (mControlState & 0x08)
			mControlState = (mControlState & 0xf3) + 0x04;
	}

	if (status.mbClearToSend) {
		if (!(mControlState & 0x20))
			mControlState = (mControlState & 0xcf) + 0x20;
	} else {
		if (mControlState & 0x20)
			mControlState = (mControlState & 0xcf) + 0x10;
	}

	if (status.mbDataSetReady) {
		if (!(mControlState & 0x80))
			mControlState = (mControlState & 0x3f) + 0x80;
	} else {
		if (mControlState & 0x80)
			mControlState = (mControlState & 0x3f) + 0x40;
	}
}

void ATRS232Channel850::OnScheduledEvent(uint32 id) {
	mpEvent = mpScheduler->AddEvent(mCyclesPerByteDevice, this, 1);

	PollDevice();
}

void ATRS232Channel850::PollDevice() {
	if (mOutputLevel) {
		--mOutputLevel;

		if (mpDevice) {
			const uint8 c = mOutputBuffer[mOutputReadOffset];
			g_ATLCModemData("Sending byte to modem: $%02X\n", c);
			mpDevice->Write(mBaudRate, c);
		}

		if (++mOutputReadOffset >= sizeof mOutputBuffer)
			mOutputReadOffset = 0;
	}

	if (mpDevice) {
		uint8 c;
		bool framingError;

		if (mInputLevel < mInputBufferSize && mpDevice->Read(mBaudRate, c, framingError)) {
			g_ATLCModemData("Receiving byte from modem: $%02X\n", c);

			if (framingError)
				mErrorFlags |= kErrorFlag850_FramingError;

			EnqueueReceivedByte(c);
		}
	}
}

void ATRS232Channel850::EnqueueReceivedByte(uint8 c) {
	if (mInputBufAddr)
		mpMemory->WriteByte(mInputBufAddr + mInputWriteOffset, c);
	else
		mInputBuffer[mInputWriteOffset] = c;

	if (++mInputWriteOffset >= mInputBufferSize)
		mInputWriteOffset = 0;

	++mInputLevel;

	if (mpCB)
		mpCB->OnChannelReceiveReady(mIndex);
}

//////////////////////////////////////////////////////////////////////////

class ATRS232Channel1030 : public ATRS232Channel {
public:
	ATRS232Channel1030();

	void Init(int index, IATRS232ChannelCallback *cb, ATCPUEmulatorMemory *mem, ATScheduler *sched, ATScheduler *slowsched, IATUIRenderer *uir, ATPokeyEmulator *pokey, ATPIAEmulator *pia);
	void Shutdown();

	void ColdReset();

	uint8 Open(uint8 aux1, uint8 aux2, bool sioBased);
	void Close();

	uint8 GetCIOPermissions() const { return mOpenPerms; }
	uint32 GetOutputLevel() const { return mOutputLevel; }

	bool SetConcurrentMode();

	void ReconfigureDataRate(uint8 aux1, uint8 aux2, bool sioBased);
	void ReconfigureTranslation(uint8 aux1, uint8 aux2);

	void SetConfig(const ATRS232Config& config);

	void GetStatus();
	bool GetByte(uint8& c);
	bool PutByte(uint8 c);

	bool IsSuspended() const { return mbSuspended; }
	void ExecuteDeviceCommand(uint8 c);

	void SetTerminalState(uint8 c);

protected:
	void FlushInputBuffer();
	void OnControlStateChanged(const ATRS232ControlState& status);
	void OnScheduledEvent(uint32 id);
	void PollDevice();
	void EnqueueReceivedByte(uint8 c);

	ATScheduler	*mpScheduler;
	ATEvent	*mpEvent;
	IATRS232Device *mpDevice;
	ATCPUEmulatorMemory *mpMemory;
	ATPokeyEmulator *mpPokey;
	ATPIAEmulator *mpPIA;

	bool	mbAddLFAfterEOL;
	bool	mbTranslationEnabled;
	bool	mbTranslationHeavy;
	bool	mbLFPending;
	bool	mbConcurrentMode;
	bool	mbSuspended;
	uint8	mCommandState;
	uint8	mWontTranslateChar;

	uint8	mInputParityMask;

	uint8	mOpenPerms;		// Mirror of ICAX1 at open time.
	uint8	mControlState;

	// These must match the error flags returned by STATUS.
	enum {
		kErrorFlag1030_FramingError = 0x80,
		kErrorFlag1030_Parity = 0x20,
		kErrorFlag1030_Wraparound = 0x10,
		kErrorFlag1030_IllegalCommand = 0x01
	};

	uint8	mErrorFlags;

	uint32	mBaudRate;

	// These must match bits 0-1 of AUX1 as passed to XIO 38.
	enum OutputParityMode {
		kOutputParityNone	= 0x00,
		kOutputParityOdd	= 0x01,
		kOutputParityEven	= 0x02,
		kOutputParitySet	= 0x03
	};

	OutputParityMode	mOutputParityMode;

	ATRS232TerminalState	mTerminalState;

	int		mInputReadOffset;
	int		mInputWriteOffset;
	int		mInputLevel;
	int		mOutputReadOffset;
	int		mOutputWriteOffset;
	int		mOutputLevel;

	// This buffer is 256 bytes internally for T:.
	uint8	mInputBuffer[256];
	uint8	mOutputBuffer[32];

	ATRS232Config mConfig;
};

ATRS232Channel1030::ATRS232Channel1030()
	: mpScheduler(NULL)
	, mpEvent(NULL)
	, mpDevice(new ATModemEmulator)
	, mpMemory(NULL)
	, mpPokey(NULL)
	, mpPIA(NULL)
	, mbAddLFAfterEOL(false)
	, mbTranslationEnabled(false)
	, mbTranslationHeavy(false)
	, mbSuspended(true)
	, mCommandState(0)
	, mWontTranslateChar(0)
	, mInputParityMask(0)
	, mOpenPerms(0)
	, mControlState(0)
	, mErrorFlags(0)
	, mBaudRate(300)
	, mOutputParityMode(kOutputParityNone)
	, mInputReadOffset(0)
	, mInputWriteOffset(0)
	, mInputLevel(0)
	, mOutputReadOffset(0)
	, mOutputWriteOffset(0)
	, mOutputLevel(0)
{
}

void ATRS232Channel1030::Init(int index, IATRS232ChannelCallback *cb, ATCPUEmulatorMemory *mem, ATScheduler *sched, ATScheduler *slowsched, IATUIRenderer *uir, ATPokeyEmulator *pokey, ATPIAEmulator *pia) {
	mpMemory = mem;
	mpScheduler = sched;
	mpPokey = pokey;
	mpPIA = pia;

	mpDevice->SetCallback(this);
	static_cast<ATModemEmulator *>(mpDevice)->Init(sched, slowsched, uir);

	// compute initial control state
	ATRS232ControlState cstate(mpDevice->GetControlState());

	mControlState = 0;
	
	if (cstate.mbCarrierDetect)
		mControlState += 0x80;
}

void ATRS232Channel1030::Shutdown() {
	if (mpDevice) {
		delete mpDevice;
		mpDevice = NULL;
	}

	if (mpScheduler) {
		if (mpEvent) {
			mpScheduler->RemoveEvent(mpEvent);
			mpEvent = NULL;
		}

		mpScheduler = NULL;
	}

	mpPIA = NULL;
	mpPokey = NULL;
}

void ATRS232Channel1030::ColdReset() {
	Close();

	if (mpDevice)
		mpDevice->ColdReset();
}

uint8 ATRS232Channel1030::Open(uint8 aux1, uint8 aux2, bool sioBased) {
	return ATCIOSymbols::CIOStatSuccess;
}

void ATRS232Channel1030::Close() {
}

bool ATRS232Channel1030::SetConcurrentMode() {
	return true;
}

void ATRS232Channel1030::ReconfigureDataRate(uint8 aux1, uint8 aux2, bool sioBased) {
}

void ATRS232Channel1030::ReconfigureTranslation(uint8 aux1, uint8 aux2) {
}

void ATRS232Channel1030::SetConfig(const ATRS232Config& config) {
	mConfig = config;
	mpDevice->SetConfig(config);
}

void ATRS232Channel1030::GetStatus() {
}

bool ATRS232Channel1030::GetByte(uint8& c) {
	if (mConfig.mbDisableThrottling)
		PollDevice();

	if (!mInputLevel)
		return false;

	c = mInputBuffer[mInputReadOffset];

	if (++mInputReadOffset >= 256)
		mInputReadOffset = 0;

	--mInputLevel;

	c &= mInputParityMask;

	if (mbTranslationEnabled) {
		// strip high bit
		c &= 0x7f;

		// convert CR to EOL
		if (c == 0x0D)
			c = 0x9B;
		else if (mbTranslationHeavy && (uint8)(c - 0x20) > 0x5C)
			c = mWontTranslateChar;
	}

	return true;
}

bool ATRS232Channel1030::PutByte(uint8 c) {
	switch(mCommandState) {
		case 0:		// waiting for ESC
			// check CMCMD
			if (c == 0x1B && mpMemory->ReadByte(7)) {
				mCommandState = 1;
				return true;
			}

			g_ATLCModemData("Sending byte to modem: $%02X\n", c);
			mpDevice->Write(300, c);
			break;

		case 1:		// waiting for command letter
			mCommandState = 0;
			mErrorFlags &= ~kErrorFlag1030_IllegalCommand;

			switch(c) {
				case 'A':	// Set Translation [p1 p2]
					mCommandState = 2;
					break;

				case 'C':	// Set Parity [p1]
					mCommandState = 4;
					break;

				case 'E':	// End of commands
					// clear CMCMD
					mpMemory->WriteByte(7, 0);
					break;

				case 'F':	// Status
					GetStatus();
					break;

				case 'N':	// Set Pulse Dialing
					// currently ignored
					break;

				case 'O':	// Set Tone Dialing
					// currently ignored
					break;

				case 'H':	// Send Break Signal
				case 'I':	// Set Originate Mode
				case 'J':	// Set Answer Mode
				case 'K':	// Dial
				case 'L':	// Pick up phone
				case 'M':	// Put phone on hook (hang up)
				case 'P':	// Start 30 second timeout
				case 'Q':	// Reset Modem
				case 'W':	// Set Analog Loopback Test
				case 'X':	// Clear Analog Loopback Test
					ExecuteDeviceCommand(c);
					break;

				case 'Y':	// Resume Modem
					ExecuteDeviceCommand(c);
					break;

				case 'Z':	// Suspend Modem
					ExecuteDeviceCommand(c);
					break;

				default:
					mErrorFlags |= kErrorFlag1030_IllegalCommand;
					break;
			}
			break;

		case 2:		// Set Translation, first byte
			mbAddLFAfterEOL = (c & 0x40) != 0;
			mbTranslationEnabled = !(c & 0x20);
			mbTranslationHeavy = (c & 0x10) != 0;

			mCommandState = 3;
			break;

		case 3:		// Set Translation, second byte
			mCommandState = 0;
			mWontTranslateChar = c;
			break;

		case 4:		// Set Parity, first byte
			switch(c & 0x0c) {
				case 0x00:	// no parity checking
					mInputParityMask = 0xff;
					break;

				case 0x04:	// check even parity and strip bit 7 (currently not checked)
				case 0x08:	// check odd parity and strip bit 7 (currently not checked)
				case 0x0c:	// no parity checking and strip bit 7
					mInputParityMask = 0x7f;
					break;
			}

			switch(c & 0x03) {
				case 0x00:	// no output parity
					mOutputParityMode = kOutputParityNone;
					break;

				case 0x01:	// odd output parity
					mOutputParityMode = kOutputParityOdd;
					break;

				case 0x02:	// even output parity
					mOutputParityMode = kOutputParityEven;
					break;

				case 0x03:	// mark output parity
					mOutputParityMode = kOutputParitySet;
					break;
			}

			mCommandState = 0;
			break;
	}

	return true;
}

void ATRS232Channel1030::ExecuteDeviceCommand(uint8 c) {
	switch(c) {
		case 'H':	// Send Break Signal
			// currently ignored
			break;

		case 'I':	// Set Originate Mode
			// currently ignored
			break;

		case 'J':	// Set Answer Mode
			mpDevice->Answer();
			break;

		case 'L':	// Pick up phone
			break;

		case 'M':	// Put phone on hook (hang up)
			mpDevice->HangUp();
			break;

		case 'P':	// Start 30 second timeout
			if (!mConfig.mDialAddress.empty() && !mConfig.mDialService.empty())
				mpDevice->Dial(mConfig.mDialAddress.c_str(), mConfig.mDialService.c_str());
			break;

		case 'Q':	// Reset Modem
			break;

		case 'W':	// Set Analog Loopback Test
			// currently ignored
			break;

		case 'X':	// Clear Analog Loopback Test
			// currently ignored
			break;

		case 'Y':	// Resume Modem
			mbSuspended = false;
			if (!mpEvent)
				mpEvent = mpScheduler->AddEvent(59659, this, 1);
			break;

		case 'Z':	// Suspend Modem
			mbSuspended = true;
			mpScheduler->UnsetEvent(mpEvent);
			break;
	}
}

void ATRS232Channel1030::SetTerminalState(uint8 c) {
}

void ATRS232Channel1030::FlushInputBuffer() {
	mInputLevel = 0;
	mInputReadOffset = 0;
	mInputWriteOffset = 0;

	if (mpDevice)
		mpDevice->FlushOutputBuffer();
}

void ATRS232Channel1030::OnControlStateChanged(const ATRS232ControlState& status) {
	uint32 oldState = mControlState;

	if (status.mbCarrierDetect)
		mControlState |= 0x80;
	else
		mControlState &= ~0x80;

	if ((oldState ^ mControlState) & 0x80)
		mpPIA->AssertInterrupt();
}

void ATRS232Channel1030::OnScheduledEvent(uint32 id) {
	mpEvent = mpScheduler->AddEvent(59659, this, 1);

	PollDevice();
}

void ATRS232Channel1030::PollDevice() {
	if (mOutputLevel) {
		--mOutputLevel;

		if (mpDevice) {
			const uint8 c = mOutputBuffer[mOutputReadOffset];
			g_ATLCModemData("Sending byte to modem: $%02X\n", c);
			mpDevice->Write(mBaudRate, c);
		}

		if (++mOutputReadOffset >= sizeof mOutputBuffer)
			mOutputReadOffset = 0;
	}

	if (mpDevice) {
		uint8 c;
		bool framingError;

		if (mInputLevel < 256 && mpDevice->Read(mBaudRate, c, framingError)) {
			g_ATLCModemData("Receiving byte from modem: $%02X\n", c);

			if (framingError)
				mErrorFlags |= kErrorFlag1030_FramingError;

			EnqueueReceivedByte(c);
		}
	}

	if (mInputLevel) {
		const uint8 c = mInputBuffer[mInputReadOffset];

		if (++mInputReadOffset >= 256)
			mInputReadOffset = 0;

		--mInputLevel;

		mpPokey->ReceiveSIOByte(c, 5966, true);
	}
}

void ATRS232Channel1030::EnqueueReceivedByte(uint8 c) {
	mInputBuffer[mInputWriteOffset] = c;

	if (++mInputWriteOffset >= 256)
		mInputWriteOffset = 0;

	++mInputLevel;
}

//////////////////////////////////////////////////////////////////////////

class ATRS232Emulator : public IATRS232Emulator
					, public IATPokeySIODevice
					, public IATSchedulerCallback
					, public IATRS232ChannelCallback
{
	ATRS232Emulator(const ATRS232Emulator&);
	ATRS232Emulator& operator=(const ATRS232Emulator&);
public:
	ATRS232Emulator();
	~ATRS232Emulator();

	void Init(ATCPUEmulatorMemory *mem, ATScheduler *sched, ATScheduler *slowsched, IATUIRenderer *uir, ATPokeyEmulator *pokey, ATPIAEmulator *pia);
	void Shutdown();

	void LoadFirmware(const void *relocator, uint32 rellen, const void *handler, uint32 hlen);
	void ColdReset();

	void GetConfig(ATRS232Config& config);
	void SetConfig(const ATRS232Config& config);

	uint8 GetCIODeviceName() const;
	void OnCIOVector(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, int offset);

public:
	virtual void OnChannelReceiveReady(int index);

public:
	void PokeyAttachDevice(ATPokeyEmulator *pokey);
	void PokeyWriteSIO(uint8 c, bool command, uint32 cyclesPerBit);
	void PokeyBeginCommand();
	void PokeyEndCommand();
	void PokeySerInReady();

public:
	void BeginTransfer(uint32 length, bool write);
	void OnScheduledEvent(uint32 id);

protected:
	void OnCommandReceived();
	void InitChannels();
	void ShutdownChannels();

	ATScheduler *mpScheduler;
	ATScheduler *mpSlowScheduler;
	ATEvent		*mpTransferEvent;
	ATPIAEmulator	*mpPIA;
	ATCPUEmulatorMemory *mpMemory;
	IATUIRenderer *mpUIRenderer;

	ATRS232Channel *mpChannels[4];
	ATRS232Config mConfig;

	ATPokeyEmulator *mpPokey;
	bool	mbCommandState;
	sint8	mActiveConcurrentIndex;
	uint8	mActiveCommand;
	uint8	mActiveCommandUnit;
	uint8	mActiveCommandState;
	uint8	mActiveCommandData;

	sint8	mPollCounter;
	sint8	mDiskCounter;

	uint32	mTransferIndex;
	uint32	mTransferLength;
	uint8	mTransferBuffer[2048];

	vdblock<uint8> mRelocator;
	vdblock<uint8> mHandler;
};

IATRS232Emulator *ATCreateRS232Emulator() {
	return new ATRS232Emulator;
}

ATRS232Emulator::ATRS232Emulator()
	: mpScheduler(NULL)
	, mpSlowScheduler(NULL)
	, mpTransferEvent(NULL)
	, mpPokey(NULL)
	, mpPIA(NULL)
	, mpMemory(NULL)
	, mpUIRenderer(NULL)
	, mbCommandState(false)
	, mActiveConcurrentIndex(-1)
	, mActiveCommand(0)
	, mActiveCommandUnit(0)
	, mActiveCommandState(0)
	, mActiveCommandData(0)
	, mTransferIndex(0)
	, mTransferLength(0)
{
	mConfig.mbTelnetEmulation = true;

	for(int i=0; i<4; ++i)
		mpChannels[i] = NULL;
}

ATRS232Emulator::~ATRS232Emulator() {
	Shutdown();
}

void ATRS232Emulator::Init(ATCPUEmulatorMemory *mem, ATScheduler *sched, ATScheduler *slowsched, IATUIRenderer *uir, ATPokeyEmulator *pokey, ATPIAEmulator *pia) {
	mpMemory = mem;
	mpScheduler = sched;
	mpSlowScheduler = slowsched;
	mpPokey = pokey;
	mpPIA = pia;
	mpUIRenderer = uir;
	pokey->AddSIODevice(this);
}

void ATRS232Emulator::Shutdown() {
	ShutdownChannels();
	if (mpScheduler) {
		mpScheduler->UnsetEvent(mpTransferEvent);
		mpScheduler = NULL;
	}

	mpSlowScheduler = NULL;

	if (mpPokey) {
		mpPokey->RemoveSIODevice(this);
		mpPokey = NULL;
	}

	mpPIA = NULL;
	mpUIRenderer = NULL;
	mpMemory = NULL;
}

void ATRS232Emulator::LoadFirmware(const void *relocator, uint32 rellen, const void *handler, uint32 hlen) {
	if (rellen > sizeof(mTransferBuffer) - 32)
		rellen = sizeof(mTransferBuffer) - 32;

	if (hlen > sizeof(mTransferBuffer) - 32)
		hlen = sizeof(mTransferBuffer) - 32;

	mRelocator.resize(rellen);
	memcpy(mRelocator.data(), relocator, rellen);

	mHandler.resize(hlen);
	memcpy(mHandler.data(), handler, hlen);
}

void ATRS232Emulator::ColdReset() {
	for(int i=0; i<4; ++i) {
		if (mpChannels[i])
			mpChannels[i]->ColdReset();
	}

	mbCommandState = false;
	mTransferIndex = 0;
	mTransferLength = 0;
	mActiveConcurrentIndex = -1;
	mActiveCommand = 0;

	mPollCounter = 26;
	mDiskCounter = 26;

	mpScheduler->UnsetEvent(mpTransferEvent);
}

void ATRS232Emulator::GetConfig(ATRS232Config& config) {
	config = mConfig;
}

void ATRS232Emulator::SetConfig(const ATRS232Config& config) {
	const bool changedDevice = mConfig.mDeviceMode != config.mDeviceMode;

	if (changedDevice)
		ShutdownChannels();

	mConfig = config;

	if (changedDevice || !mpChannels[0])
		InitChannels();

	for(int i=0; i<4; ++i) {
		ATRS232Config chanConfig(config);

		if (i)
			chanConfig.mListenPort = 0;

		if (mpChannels[i])
			mpChannels[i]->SetConfig(chanConfig);
	}
}

uint8 ATRS232Emulator::GetCIODeviceName() const {
	if (mConfig.mDeviceMode == kATRS232DeviceMode_1030)
		return 'T';

	if (mConfig.m850SIOLevel == kAT850SIOEmulationLevel_Full)
		return 0;

	return 'R';
}

void ATRS232Emulator::OnCIOVector(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, int offset) {
	// We must read from the originating IOCB instead of from the zero-page IOCB for the
	// PUT BYTE command, as BASIC jumps directly to the put vector instead of going through
	// CIO. However, we must read from ICDNOZ for Ice-T to work as it does the same for
	// the STATUS command.
	uint32 idx = mem->ReadByte(offset == 6 ? ATKernelSymbols::ICDNO + cpu->GetX() : ATKernelSymbols::ICDNOZ) - 1;

	if (idx >= 4) {
		// For XIO and Open commands, we map R: to R1:. This is required by FoReM XEP's START module.
		if ((offset == 0 || offset == 10) && idx == (uint32)-1)
			idx = 0;
		else {
			cpu->Ldy(ATCIOSymbols::CIOStatUnkDevice);
			return;
		}
	}

	if (!mpChannels[idx]) {
		if (offset == 2)
			cpu->Ldy(ATCIOSymbols::CIOStatSuccess);
		else
			cpu->Ldy(ATCIOSymbols::CIOStatSystemError);

		return;
	}


	ATRS232Channel& ch = *mpChannels[idx];

	switch(offset) {
		case 0:		// open
			cpu->Ldy(ch.Open(mem->ReadByte(ATKernelSymbols::ICAX1Z), mem->ReadByte(ATKernelSymbols::ICAX2Z), false));
			break;

		case 2:		// close
			// wait for output buffer to drain (requires assist)
			if (ch.GetOutputLevel()) {
				cpu->Push(cpu->GetPC() >> 8);
				cpu->Push((cpu->GetPC() - 1) & 0xFF);
				cpu->Push(0xE4);
				cpu->Push(0xBF);
				cpu->Push(0xE4);
				cpu->Push(0xBF);
				return;
			}

			ch.Close();
			cpu->Ldy(ATCIOSymbols::CIOStatSuccess);
			break;

		case 4:		// get byte (requires assist)
			{
				if (mem->ReadByte(ATKernelSymbols::BRKKEY) == 0) {
					cpu->SetA(0);
					cpu->Ldy(ATCIOSymbols::CIOStatBreak);
					return;
				}

				uint8 c;
				if (!ch.GetByte(c)) {
					cpu->Push(cpu->GetPC() >> 8);
					cpu->Push((cpu->GetPC() - 1) & 0xFF);
					cpu->Push(0xE4);
					cpu->Push(0xBF);
					cpu->Push(0xE4);
					cpu->Push(0xBF);
					return;
				}

				cpu->SetA(c);
				cpu->Ldy(ATCIOSymbols::CIOStatSuccess);
			}
			break;

		case 6:		// put byte
			if (mem->ReadByte(ATKernelSymbols::BRKKEY) == 0) {
				cpu->SetA(0);
				cpu->Ldy(ATCIOSymbols::CIOStatBreak);
				return;
			}

			if (!ch.PutByte(cpu->GetA())) {
				cpu->Push(cpu->GetPC() >> 8);
				cpu->Push((cpu->GetPC() - 1) & 0xFF);
				cpu->Push(0xE4);
				cpu->Push(0xBF);
				cpu->Push(0xE4);
				cpu->Push(0xBF);
				return;
			}

			cpu->Ldy(ATCIOSymbols::CIOStatSuccess);
			break;

		case 8:		// get status
			ch.GetStatus();
			cpu->Ldy(ATCIOSymbols::CIOStatSuccess);
			break;

		case 10:	// special
			if (mConfig.mDeviceMode == kATRS232DeviceMode_1030) {
				cpu->Ldy(ATCIOSymbols::CIOStatNotSupported);
			} else {
				{
					uint8 cmd = mem->ReadByte(ATKernelSymbols::ICCOMZ);

					switch(cmd) {
						case 32:	// XIO 32 Force short block (silently ignored)
							break;

						case 34:	// XIO 34 control DTR, RTS, XMT (silently ignored)
							ch.SetTerminalState(mem->ReadByte(ATKernelSymbols::ICAX1Z));
							break;

						case 36:	// XIO 36 configure baud rate (partial support)
							ch.ReconfigureDataRate(mem->ReadByte(ATKernelSymbols::ICAX1Z), mem->ReadByte(ATKernelSymbols::ICAX2Z), false);
							break;

						case 38:	// XIO 38 configure translation mode (silently ignored)
							ch.ReconfigureTranslation(mem->ReadByte(ATKernelSymbols::ICAX1Z), mem->ReadByte(ATKernelSymbols::ICAX2Z));
							break;

						case 40:	// XIO 40 concurrent mode (silently ignored)
							ch.SetConcurrentMode();
							break;

						default:
							cpu->Ldy(ATCIOSymbols::CIOStatNotSupported);
							return;
					}
				}

				// Page 41 of the OS Manual says:
				// "You should not alter ICAX1 once the device/file is open."
				//
				// ...which of course means that Atari BASIC does it: XIO commands stomp the
				// AUX1 and AUX2 bytes. The former then causes GET BYTE operations to break as
				// they check the permission bits in AUX1. To work around this, the SPECIAL
				// routine of R: handlers have to restore the permissions byte.
				mem->WriteByte(ATKernelSymbols::ICAX1Z, ch.GetCIOPermissions());

				cpu->Ldy(ATCIOSymbols::CIOStatSuccess);
			}

			break;
	}
}

void ATRS232Emulator::OnChannelReceiveReady(int index) {
	if (index == mActiveConcurrentIndex) {
		ATRS232Channel850& ch = *static_cast<ATRS232Channel850 *>(mpChannels[mActiveConcurrentIndex]);
		
		uint8 c;
		if (ch.GetByte(c)) {
			mpPokey->ReceiveSIOByte(c, (ch.GetCyclesPerByteDevice() + 5) / 10, true);
		}
	}
}

void ATRS232Emulator::PokeyAttachDevice(ATPokeyEmulator *pokey) {
}

void ATRS232Emulator::PokeyWriteSIO(uint8 c, bool command, uint32 cyclesPerBit) {
	if (mConfig.mDeviceMode == kATRS232DeviceMode_1030) {
		// check for proper 300 baud operation (divisor = 2982, 5% tolerance)
		if (cyclesPerBit > 5666 && cyclesPerBit < 6266 && mpChannels[0]) {
			if (command) {
				mpChannels[0]->ExecuteDeviceCommand(c);

				mpPIA->AssertProceed();
			} else if (!mpChannels[0]->IsSuspended())
				mpChannels[0]->PutByte(c);
		}

		return;
	}

	if (mActiveConcurrentIndex >= 0) {
		ATRS232Channel850& ch = *static_cast<ATRS232Channel850 *>(mpChannels[mActiveConcurrentIndex]);

		ch.PutByte(c);
		ch.SetCyclesPerByteDevice(cyclesPerBit * 10);
	} else if (mTransferIndex < mTransferLength) {
		mTransferBuffer[mTransferIndex++] = c;

		if (mTransferIndex >= mTransferLength) {
			if (mbCommandState)
				OnCommandReceived();
			else switch(mActiveCommand) {
				case 0x57:	// write
					if (mActiveCommandState == 1) {
						ATRS232Channel850& ch = *static_cast<ATRS232Channel850 *>(mpChannels[mActiveCommandUnit]);

						// end command
						mActiveCommand = 0;

						// check checksum
						if (ATComputeSIOChecksum(mTransferBuffer, 64) == mTransferBuffer[64]) {
							// push bytes into the device
							for(uint8 i=0; i<mActiveCommandData; ++i)
								ch.PutByte(mTransferBuffer[i]);

							mTransferBuffer[0] = 0x41;
							mTransferBuffer[1] = 0x43;
							BeginTransfer(2, true);
						} else {
							mTransferBuffer[0] = 0x4E;
							BeginTransfer(1, true);
						}
					}
					break;
			}
		}
	}
}

void ATRS232Emulator::PokeyBeginCommand() {
	mpScheduler->UnsetEvent(mpTransferEvent);

	mbCommandState = true;
	mTransferIndex = 0;
	mTransferLength = 5;
	mActiveCommand = 0;

	if (mConfig.m850SIOLevel == kAT850SIOEmulationLevel_Full) {
		if (mConfig.mDeviceMode == kATRS232DeviceMode_850) {
			for(int i=0; i<4; ++i)
				static_cast<ATRS232Channel850 *>(mpChannels[i])->ExitConcurrentMode();

			mActiveConcurrentIndex = -1;
		}
	}
}

void ATRS232Emulator::PokeyEndCommand() {
	mbCommandState = false;
}

void ATRS232Emulator::PokeySerInReady() {
}

void ATRS232Emulator::BeginTransfer(uint32 length, bool write) {
	mTransferIndex = 0;
	mTransferLength = length;

	mpScheduler->SetEvent(2000, this, 1, mpTransferEvent);
}

void ATRS232Emulator::OnScheduledEvent(uint32 id) {
	mpTransferEvent = NULL;

	if (mTransferIndex < mTransferLength) {
		mpPokey->ReceiveSIOByte(mTransferBuffer[mTransferIndex++], 93);

		mpTransferEvent = mpScheduler->AddEvent(mTransferIndex == 1 && mTransferLength > 1? 5000 : 930, this, 1);
	} else {
		// check if we have an active command
		switch(mActiveCommand) {
			case 0x57:	// Write
				if (mActiveCommandState == 0) {
					// Prepare to receive data frame - always 64 bytes regardless of write length
					mTransferIndex = 0;
					mTransferLength = 65;
					mActiveCommandState = 1;
				}
				break;
		}
	}
}

void ATRS232Emulator::OnCommandReceived() {
	if (!mConfig.m850SIOLevel)
		return;

	// check checksum
	if (ATComputeSIOChecksum(mTransferBuffer, 4) != mTransferBuffer[4])
		return;

	// check device ID
	const uint32 index = mTransferBuffer[0] - 0x50;
	const uint8 cmd = mTransferBuffer[1];

	// non-poll commands reset poll counter, if poll hasn't already been answered
	if (cmd != 0x3F && mPollCounter >= 0)
		mPollCounter = 26;

	if (mConfig.m850SIOLevel == kAT850SIOEmulationLevel_Full && mTransferBuffer[0] == 0x31) {
		if (cmd == 0x53) {				// status
			// The 850 only answers the 26th status request. Once this arrives, it unlocks
			// that status request and any subsequent disk requests. Another status request
			// turns off disk emulation.
			if (mDiskCounter > 0 && !--mDiskCounter) {
				mTransferBuffer[0] = 0x41;
				mTransferBuffer[1] = 0x43;
				mTransferBuffer[2] = 0x00;
				mTransferBuffer[3] = 0xFF;
				mTransferBuffer[4] = 0xFE;
				mTransferBuffer[5] = 0x00;
				mTransferBuffer[6] = ATComputeSIOChecksum(mTransferBuffer+2, 4);
				BeginTransfer(7, true);
			}
			return;
		} else if (cmd == 0x52) {		// read
			if (mDiskCounter == 0) {
				uint32 sector = mTransferBuffer[2] + 256*mTransferBuffer[3];

				if (sector < 1 || sector > 3) {
					// out of range -- send NAK
					mTransferBuffer[0] = 0x4E;
					BeginTransfer(1, true);
					return;
				}

				uint32 rellen = (uint32)mRelocator.size();
				uint32 offset = (sector-1) * 128;

				memset(mTransferBuffer+2, 0, 128);

				if (offset < rellen)
					memcpy(mTransferBuffer+2, mRelocator.data() + offset, std::min<uint32>(128, rellen - offset));

				mTransferBuffer[0] = 0x41;
				mTransferBuffer[1] = 0x43;
				mTransferBuffer[130] = ATComputeSIOChecksum(mTransferBuffer+2, 128);
				BeginTransfer(131, true);
			}
			return;
		}
	}

	// Non-poll and non-D1: commands kill disk emulation.
	if (cmd != 0x3F && mTransferBuffer[0] != 0x31)
		mDiskCounter = -1;

	if (cmd != 0x3F && index >= 4)
		return;

	if (cmd == 0x3F || index < 4)
		g_ATLC850SIO("Unit %d | Command %02x %02x %02x\n", index + 1, mTransferBuffer[1], mTransferBuffer[2], mTransferBuffer[3]);

	if (cmd == 0x3F) {
		// Poll command -- send back SIO command for booting. This
		// is an 12 byte + chk block that is meant to be written to
		// the SIO parameter block starting at DDEVIC ($0300).
		//
		// The boot block MUST start at $0500. There are both BASIC-
		// based and cart-based loaders that use JSR $0506 to run the
		// loader.

		if (mPollCounter > 0 && !--mPollCounter) {
			static const uint8 kBootBlock[12]={
				0x50,		// DDEVIC
				0x01,		// DUNIT
				0x21,		// DCOMND = '!' (boot)
				0x40,		// DSTATS
				0x00, 0x05,	// DBUFLO, DBUFHI == $0500
				0x08,		// DTIMLO = 8 vblanks
				0x00,		// not used
				0x00, 0x00,	// DBYTLO, DBYTHI
				0x00,		// DAUX1
				0x00,		// DAUX2
			};

			mTransferBuffer[0] = 0x41;
			mTransferBuffer[1] = 0x43;
			memcpy(mTransferBuffer+2, kBootBlock, 12);

			uint32 relsize = (uint32)mRelocator.size();
			mTransferBuffer[10] = (uint8)relsize;
			mTransferBuffer[11] = (uint8)(relsize >> 8);
			mTransferBuffer[14] = ATComputeSIOChecksum(mTransferBuffer+2, 12);
			BeginTransfer(15, true);

			// Once the poll is answered, we don't answer it again until power cycle.
			mPollCounter = -1;
		}
		return;
	}

	if (index >= 4)
		return;
	
	if (cmd == 0x21) {
		// Boot command -- send back boot loader.
		static const uint8 kLoaderBlock[7]={
			0x00,		// flags
			0x01,		// sector count
			0x00, 0x05,	// load address
			0x06, 0x05,	// init address
			0x60
		};

		mTransferBuffer[0] = 0x41;
		mTransferBuffer[1] = 0x43;

		uint32 relsize;
		if (mConfig.m850SIOLevel == kAT850SIOEmulationLevel_StubLoader) {
			relsize = 7;
			memcpy(mTransferBuffer+2, kLoaderBlock, 7);
		} else {
			relsize = (uint32)mRelocator.size();
			memcpy(mTransferBuffer+2, mRelocator.data(), relsize);
		}

		mTransferBuffer[relsize+2] = ATComputeSIOChecksum(mTransferBuffer+2, relsize);
		BeginTransfer(relsize+3, true);
		return;
	}
	
	if (mConfig.m850SIOLevel != kAT850SIOEmulationLevel_Full)
		return;

	if (mConfig.mDeviceMode == kATRS232DeviceMode_850) {
		ATRS232Channel850& ch = *static_cast<ATRS232Channel850 *>(mpChannels[index]);

		if (cmd == 0x53) {	// 'S' / status
			mTransferBuffer[0] = 0x41;
			mTransferBuffer[1] = 0x43;
			ch.ReadControlStatus(mTransferBuffer[2], mTransferBuffer[3]);
			mTransferBuffer[4] = ATComputeSIOChecksum(mTransferBuffer+2, 2);
			BeginTransfer(5, true);
			return;
		} else if (cmd == 0x57) {	// 'W' / write

			// check data length
			uint8 dataLen = mTransferBuffer[2];

			if (!dataLen) {
				// 0 is a special case with no data frame
				mTransferBuffer[0] = 0x41;
				mTransferBuffer[1] = 0x43;
				BeginTransfer(2, true);
			} else {
				// send ACK
				mTransferBuffer[0] = 0x41;
				BeginTransfer(1, true);

				// set active command to write, with buffer length
				mActiveCommand = cmd;
				mActiveCommandUnit = (uint8)index;
				mActiveCommandData = dataLen;
				mActiveCommandState = 0;
			}
		} else if (cmd == 0x41) {	// 'A' / control
			ch.SetTerminalState(mTransferBuffer[2]);

			mTransferBuffer[0] = 0x41;
			mTransferBuffer[1] = 0x43;
			BeginTransfer(2, true);
		} else if (cmd == 0x58) {	// 'X' / stream
			if (!ch.CheckSerialFormatConcurrentOK(mTransferBuffer[2])) {
				// Invalid option configuration -- NAK it
				mTransferBuffer[0] = 0x41;
				mTransferBuffer[1] = 0x4E;
				BeginTransfer(2, true);
				return;
			}

			ch.Open(0x0C, 0, true);
			ch.ReconfigureTranslation(0x20, 0);

			if (!ch.SetConcurrentMode()) {
				// Hmm, seems a monitored control line was not active. NAK it.
				mTransferBuffer[0] = 0x41;
				mTransferBuffer[1] = 0x4E;
				BeginTransfer(2, true);
				return;
			}

			mActiveConcurrentIndex = index;

			mTransferBuffer[0] = 0x41;
			mTransferBuffer[1] = 0x43;

			// The data payload from the stream command is 9 bytes to set
			// to POKEY.
			uint32 cyclesPerHalfBit = (ch.GetCyclesPerByte() + 10) / 20;
			uint8 divlo = (uint8)cyclesPerHalfBit;
			uint8 divhi = (uint8)(cyclesPerHalfBit >> 8);

			mTransferBuffer[ 2] = divlo;
			mTransferBuffer[ 3] = 0xA0;		// AUDC1
			mTransferBuffer[ 4] = divhi;
			mTransferBuffer[ 5] = 0xA0;		// AUDC2
			mTransferBuffer[ 6] = divlo;
			mTransferBuffer[ 7] = 0xA0;		// AUDC3
			mTransferBuffer[ 8] = divhi;
			mTransferBuffer[ 9] = 0xA0;		// AUDC4
			mTransferBuffer[10] = 0x78;		// AUDCTL
			mTransferBuffer[11] = ATComputeSIOChecksum(mTransferBuffer+2, 9);
			BeginTransfer(12, true);
		} else if (cmd == 0x42) {	// 'B' / configure
			mTransferBuffer[0] = 0x41;
			mTransferBuffer[1] = 0x43;
			ch.ReconfigureDataRate(mTransferBuffer[2], mTransferBuffer[3], true);
			mTransferBuffer[4] = ATComputeSIOChecksum(mTransferBuffer+2, 2);
			BeginTransfer(5, true);
		} else if (cmd == 0x26) {	// '&' / load handler
			mTransferBuffer[0] = 0x41;
			mTransferBuffer[1] = 0x43;

			uint32 hsize = (uint32)mHandler.size();
			memcpy(mTransferBuffer+2, mHandler.data(), hsize);
			mTransferBuffer[hsize+2] = ATComputeSIOChecksum(mTransferBuffer+2, hsize);
			BeginTransfer(hsize+3, true);
		} else {
			// don't know this one - NAK it
			mTransferBuffer[0] = 0x4E;
			BeginTransfer(1, true);
		}
	}
}

void ATRS232Emulator::InitChannels() {
	for(int i=0; i<4; ++i) {
		if (mConfig.mDeviceMode == kATRS232DeviceMode_1030)
			mpChannels[i] = new ATRS232Channel1030;
		else
			mpChannels[i] = new ATRS232Channel850;

		mpChannels[i]->Init(i, this, mpMemory, mpScheduler, mpSlowScheduler, i ? NULL : mpUIRenderer, mpPokey, mpPIA);
	}
}

void ATRS232Emulator::ShutdownChannels() {
	for(int i=0; i<4; ++i) {
		if (mpChannels[i]) {
			mpChannels[i]->Shutdown();
			delete mpChannels[i];
			mpChannels[i] = NULL;
		}
	}
}
