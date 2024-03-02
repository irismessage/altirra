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

ATDebuggerLogChannel g_ATLC850SIO(false, false, "850", "850 interface SIO activity");

//////////////////////////////////////////////////////////////////////////

class ATRS232Channel : public IATSchedulerCallback, public IATRS232DeviceCallback {
public:
	ATRS232Channel();

	void Init(int index, ATCPUEmulatorMemory *mem, ATScheduler *sched, ATScheduler *slowsched, IATUIRenderer *uir);
	void Shutdown();

	void ColdReset();

	uint8 Open(uint8 aux1, uint8 aux2);
	void Close();

	uint8 GetCIOPermissions() const { return mOpenPerms; }
	uint32 GetOutputLevel() const { return mOutputLevel; }

	void SetConcurrentMode();

	void ReconfigureDataRate(uint8 aux1, uint8 aux2);
	void ReconfigureTranslation(uint8 aux1, uint8 aux2);

	void SetConfig(const ATRS232Config& config);

	void GetStatus();
	bool GetByte(uint8& c);
	bool PutByte(uint8 c);
	void SetTerminalState(uint8 c);

protected:
	void FlushInputBuffer();
	void OnControlStateChanged(const ATRS232ControlState& status);
	void OnScheduledEvent(uint32 id);
	void PollDevice();

	ATScheduler	*mpScheduler;
	ATEvent	*mpEvent;
	IATRS232Device *mpDevice;
	ATCPUEmulatorMemory *mpMemory;

	uint32	mCyclesPerByte;

	bool	mbAddLFAfterEOL;
	bool	mbTranslationEnabled;
	bool	mbTranslationHeavy;
	bool	mbLFPending;
	bool	mbConcurrentMode;
	uint8	mWontTranslateChar;

	uint8	mInputParityMask;

	uint8	mOpenPerms;		// Mirror of ICAX1 at open time.
	uint8	mControlState;

	// These must match the error flags returned by STATUS.
	enum {
		kErrorFlag_FramingError = 0x80
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
	int		mInputBufferSize;
	uint16	mInputBufAddr;
	int		mOutputReadOffset;
	int		mOutputWriteOffset;
	int		mOutputLevel;

	uint8	mInputBuffer[32];
	uint8	mOutputBuffer[32];

	ATRS232Config mConfig;
};

ATRS232Channel::ATRS232Channel()
	: mpScheduler(NULL)
	, mpEvent(NULL)
	, mpDevice(new ATModemEmulator)
	, mpMemory(NULL)
	, mbAddLFAfterEOL(false)
	, mbTranslationEnabled(false)
	, mbTranslationHeavy(false)
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
	, mInputBufferSize(0)
	, mInputBufAddr(0)
	, mOutputReadOffset(0)
	, mOutputWriteOffset(0)
	, mOutputLevel(0)
{
}

void ATRS232Channel::Init(int index, ATCPUEmulatorMemory *mem, ATScheduler *sched, ATScheduler *slowsched, IATUIRenderer *uir) {
	mpMemory = mem;
	mpScheduler = sched;

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
	ReconfigureDataRate(0, 0);
	ReconfigureTranslation(0x10, 0);
}

void ATRS232Channel::Shutdown() {
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
}

void ATRS232Channel::ColdReset() {
	Close();

	if (mpDevice)
		mpDevice->ColdReset();
}

uint8 ATRS232Channel::Open(uint8 aux1, uint8 aux2) {
	if (!mpEvent)
		mpEvent = mpScheduler->AddEvent(mCyclesPerByte, this, 1);

	mOpenPerms = aux1;
	mInputReadOffset = 0;
	mInputWriteOffset = 0;
	mInputLevel = 0;
	mInputBufferSize = sizeof mInputBuffer;
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
	mbConcurrentMode = false;

	// ICD Multi I/O Manual Chapter 6: Open sets RTS and DTR to high.
	mTerminalState.mbDataTerminalReady = true;
	mTerminalState.mbRequestToSend = true;

	if (mpDevice)
		mpDevice->SetTerminalState(mTerminalState);

	FlushInputBuffer();

	return ATCIOSymbols::CIOStatSuccess;
}

void ATRS232Channel::Close() {
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
}

void ATRS232Channel::SetConcurrentMode() {
	mbConcurrentMode = true;

	FlushInputBuffer();
}

void ATRS232Channel::ReconfigureDataRate(uint8 aux1, uint8 aux2) {
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

	mErrorFlags = 0;
}

void ATRS232Channel::ReconfigureTranslation(uint8 aux1, uint8 aux2) {
	mbTranslationEnabled = (aux1 & 0x20) == 0;
	mbTranslationHeavy = (aux1 & 0x10) != 0;

	// EOL -> CR+LF is only available if translation is enabled.
	mbAddLFAfterEOL = mbTranslationEnabled && (aux1 & 0x40);

	mWontTranslateChar = aux2;

	mInputParityMask = (aux1 & 0x0c) ? 0x7F : 0xFF;
	mOutputParityMode = (OutputParityMode)(aux1 & 0x03);

	mErrorFlags = 0;
}

void ATRS232Channel::SetConfig(const ATRS232Config& config) {
	mConfig = config;
	mpDevice->SetConfig(config);
}

void ATRS232Channel::GetStatus() {
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

bool ATRS232Channel::GetByte(uint8& c) {
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

bool ATRS232Channel::PutByte(uint8 c) {
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

void ATRS232Channel::SetTerminalState(uint8 c) {
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

void ATRS232Channel::FlushInputBuffer() {
	mInputLevel = 0;
	mInputReadOffset = 0;
	mInputWriteOffset = 0;

	if (mpDevice)
		mpDevice->FlushOutputBuffer();
}

void ATRS232Channel::OnControlStateChanged(const ATRS232ControlState& status) {
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

void ATRS232Channel::OnScheduledEvent(uint32 id) {
	mpEvent = mpScheduler->AddEvent(mCyclesPerByte, this, 1);

	PollDevice();
}

void ATRS232Channel::PollDevice() {
	if (mOutputLevel) {
		--mOutputLevel;

		if (mpDevice)
			mpDevice->Write(mBaudRate, mOutputBuffer[mOutputReadOffset]);

		if (++mOutputReadOffset >= sizeof mOutputBuffer)
			mOutputReadOffset = 0;
	}

	if (mpDevice) {
		uint8 c;
		bool framingError;

		if (mInputLevel < mInputBufferSize && mpDevice->Read(mBaudRate, c, framingError)) {
			if (framingError)
				mErrorFlags |= kErrorFlag_FramingError;

			if (mInputBufAddr)
				mpMemory->WriteByte(mInputBufAddr + mInputWriteOffset, c);
			else
				mInputBuffer[mInputWriteOffset] = c;

			if (++mInputWriteOffset >= mInputBufferSize)
				mInputWriteOffset = 0;

			++mInputLevel;
		}
	}
}

//////////////////////////////////////////////////////////////////////////

class ATRS232Emulator : public IATRS232Emulator
					, public IATPokeySIODevice
					, public IATSchedulerCallback
{
	ATRS232Emulator(const ATRS232Emulator&);
	ATRS232Emulator& operator=(const ATRS232Emulator&);
public:
	ATRS232Emulator();
	~ATRS232Emulator();

	void Init(ATCPUEmulatorMemory *mem, ATScheduler *sched, ATScheduler *slowsched, IATUIRenderer *uir, ATPokeyEmulator *pokey);
	void Shutdown();

	void ColdReset();

	void GetConfig(ATRS232Config& config);
	void SetConfig(const ATRS232Config& config);

	void OnCIOVector(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, int offset);

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

	ATScheduler *mpScheduler;
	ATEvent		*mpTransferEvent;

	ATRS232Channel mChannels[4];
	ATRS232Config mConfig;

	ATPokeyEmulator *mpPokey;
	bool	mbCommandState;
	uint32	mTransferIndex;
	uint32	mTransferLength;
	uint8	mTransferBuffer[16];
};

IATRS232Emulator *ATCreateRS232Emulator() {
	return new ATRS232Emulator;
}

ATRS232Emulator::ATRS232Emulator()
	: mpScheduler(NULL)
	, mpTransferEvent(NULL)
	, mpPokey(NULL)
	, mbCommandState(false)
	, mTransferIndex(0)
	, mTransferLength(0)
{
	mConfig.mbTelnetEmulation = true;
}

ATRS232Emulator::~ATRS232Emulator() {
	Shutdown();
}

void ATRS232Emulator::Init(ATCPUEmulatorMemory *mem, ATScheduler *sched, ATScheduler *slowsched, IATUIRenderer *uir, ATPokeyEmulator *pokey) {
	for(int i=0; i<4; ++i) {
		mChannels[i].Init(i, mem, sched, slowsched, i ? NULL : uir);
	}

	mpScheduler = sched;
	mpPokey = pokey;
	pokey->AddSIODevice(this);
}

void ATRS232Emulator::Shutdown() {
	if (mpScheduler) {
		mpScheduler->UnsetEvent(mpTransferEvent);
		mpScheduler = NULL;
	}

	if (mpPokey) {
		mpPokey->RemoveSIODevice(this);
		mpPokey = NULL;
	}

	for(int i=0; i<4; ++i) {
		mChannels[i].Shutdown();
	}
}

void ATRS232Emulator::ColdReset() {
	for(int i=0; i<4; ++i) {
		mChannels[i].ColdReset();
	}

	mbCommandState = false;
	mTransferIndex = 0;
	mTransferLength = 0;

	mpScheduler->UnsetEvent(mpTransferEvent);
}

void ATRS232Emulator::GetConfig(ATRS232Config& config) {
	config = mConfig;
}

void ATRS232Emulator::SetConfig(const ATRS232Config& config) {
	mConfig = config;

	for(int i=0; i<4; ++i) {
		ATRS232Config chanConfig(config);

		if (i)
			chanConfig.mListenPort = 0;

		mChannels[i].SetConfig(chanConfig);
	}
}

void ATRS232Emulator::OnCIOVector(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, int offset) {
	// We must read from the originating IOCB instead of from the zero-page IOCB.
	// BASIC jumps directly to the put vector instead of going through CIO.
	uint32 idx = mem->ReadByte(ATKernelSymbols::ICDNO + cpu->GetX()) - 1;

	if (idx >= 4) {
		// For XIO and Open commands, we map R: to R1:. This is required by FoReM XEP's START module.
		if ((offset == 0 || offset == 10) && idx == (uint32)-1)
			idx = 0;
		else {
			cpu->Ldy(ATCIOSymbols::CIOStatUnkDevice);
			return;
		}
	}

	ATRS232Channel& ch = mChannels[idx];

	switch(offset) {
		case 0:		// open
			cpu->Ldy(ch.Open(mem->ReadByte(ATKernelSymbols::ICAX1Z), mem->ReadByte(ATKernelSymbols::ICAX2Z)));
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
			{
				uint8 cmd = mem->ReadByte(ATKernelSymbols::ICCOMZ);

				switch(cmd) {
					case 32:	// XIO 32 Force short block (silently ignored)
						break;

					case 34:	// XIO 34 control DTR, RTS, XMT (silently ignored)
						ch.SetTerminalState(mem->ReadByte(ATKernelSymbols::ICAX1Z));
						break;

					case 36:	// XIO 36 configure baud rate (partial support)
						ch.ReconfigureDataRate(mem->ReadByte(ATKernelSymbols::ICAX1Z), mem->ReadByte(ATKernelSymbols::ICAX2Z));
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
			return;
	}
}

void ATRS232Emulator::PokeyAttachDevice(ATPokeyEmulator *pokey) {
}

void ATRS232Emulator::PokeyWriteSIO(uint8 c, bool command, uint32 cyclesPerBit) {
	if (mTransferIndex < mTransferLength) {
		mTransferBuffer[mTransferIndex++] = c;

		if (mTransferIndex >= mTransferLength) {
			if (mbCommandState)
				OnCommandReceived();
		}
	}
}

void ATRS232Emulator::PokeyBeginCommand() {
	mbCommandState = true;
	mTransferIndex = 0;
	mTransferLength = 5;
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

		if (mTransferIndex < mTransferLength)
			mpTransferEvent = mpScheduler->AddEvent(mTransferIndex == 1 ? 5000 : 930, this, 1);
	}
}

void ATRS232Emulator::OnCommandReceived() {
	if (!mConfig.m850SIOLevel)
		return;

	// check device ID
	const uint32 index = mTransferBuffer[0] - 0x50;
	const uint8 cmd = mTransferBuffer[1];
	if (cmd != 0x3F && index >= 4)
		return;

	g_ATLC850SIO("Unit %d | Command %02x %02x %02x\n", index + 1, mTransferBuffer[1], mTransferBuffer[2], mTransferBuffer[3]);

	if (cmd == 0x3F) {
		// Poll command -- send back SIO command for booting. This
		// is an 12 byte + chk block that is meant to be written to
		// the SIO parameter block starting at DDEVIC ($0300).
		//
		// The boot block MUST start at $0500. There are both BASIC-
		// based and cart-based loaders that use JSR $0506 to run the
		// loader.

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
		mTransferBuffer[10] = 7;
		mTransferBuffer[11] = 0;
		mTransferBuffer[14] = ATComputeSIOChecksum(mTransferBuffer+2, 12);
		BeginTransfer(15, true);
		return;
	} else if (cmd == 0x21) {
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
		memcpy(mTransferBuffer+2, kLoaderBlock, 7);
		mTransferBuffer[9] = ATComputeSIOChecksum(mTransferBuffer+2, 7);
		BeginTransfer(10, true);
		return;
	}
}
