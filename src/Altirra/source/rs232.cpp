#include "stdafx.h"
#include "rs232.h"
#include "cpu.h"
#include "cio.h"
#include "ksyms.h"
#include "scheduler.h"
#include "modem.h"

//////////////////////////////////////////////////////////////////////////

class ATRS232Channel : public IATSchedulerCallback {
public:
	ATRS232Channel();

	void Init(int index, ATCPUEmulatorMemory *mem, ATScheduler *sched, IATUIRenderer *uir);
	void Shutdown();

	uint8 Open(uint8 aux1, uint8 aux2);
	void Close();

	void ReconfigureDataRate(uint8 aux1, uint8 aux2);
	void ReconfigureTranslation(uint8 aux1, uint8 aux2);

	void SetConfig(const ATRS232Config& config);

	void GetStatus();
	bool GetByte(uint8& c);
	bool PutByte(uint8 c);

protected:
	void OnScheduledEvent(uint32 id);

	ATScheduler	*mpScheduler;
	ATEvent	*mpEvent;
	IATRS232Device *mpDevice;
	ATCPUEmulatorMemory *mpMemory;

	uint32	mCyclesPerByte;

	bool	mbAddLFAfterEOL;
	bool	mbTranslationEnabled;
	bool	mbTranslationHeavy;
	bool	mbLFPending;
	uint8	mWontTranslateChar;

	uint8	mInputParityMask;

	// These must match bits 0-1 of AUX1 as passed to XIO 38.
	enum OutputParityMode {
		kOutputParityNone	= 0x00,
		kOutputParityOdd	= 0x01,
		kOutputParityEven	= 0x02,
		kOutputParitySet	= 0x03
	};

	OutputParityMode	mOutputParityMode;

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

void ATRS232Channel::Init(int index, ATCPUEmulatorMemory *mem, ATScheduler *sched, IATUIRenderer *uir) {
	mpMemory = mem;
	mpScheduler = sched;

	static_cast<ATModemEmulator *>(mpDevice)->Init(sched, uir);
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

uint8 ATRS232Channel::Open(uint8 aux1, uint8 aux2) {
	// Default to 300 baud, 8 data bits, 1 stop bit, no input parity, space output parity,
	// no CR after LF, light translation
	ReconfigureDataRate(0, 0);
	ReconfigureTranslation(0x10, 0);

	if (!mpEvent)
		mpEvent = mpScheduler->AddEvent(mCyclesPerByte, this, 1);

	mInputReadOffset = 0;
	mInputWriteOffset = 0;
	mInputLevel = 0;
	mInputBufferSize = sizeof mInputBuffer;
	mInputBufAddr =0;

	mOutputReadOffset = 0;
	mOutputWriteOffset = 0;
	mOutputLevel = 0;

	mbLFPending = false;

	return ATCIOSymbols::CIOStatSuccess;
}

void ATRS232Channel::Close() {
	if (mpEvent) {
		mpScheduler->RemoveEvent(mpEvent);
		mpEvent = NULL;
	}
}

void ATRS232Channel::ReconfigureDataRate(uint8 aux1, uint8 aux2) {
	static const uint32 kBaudTable[16]={
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

	mCyclesPerByte = kBaudTable[aux1 & 15];
}

void ATRS232Channel::ReconfigureTranslation(uint8 aux1, uint8 aux2) {
	mbTranslationEnabled = (aux1 & 0x20) == 0;
	mbTranslationHeavy = (aux1 & 0x10) != 0;

	// EOL -> CR+LF is only available if translation is enabled.
	mbAddLFAfterEOL = (aux1 & 0x30) && (aux1 & 0x40);

	mWontTranslateChar = aux2;

	mInputParityMask = (aux1 & 0x0c) ? 0x7F : 0xFF;
	mOutputParityMode = (OutputParityMode)(aux1 & 0x03);
}

void ATRS232Channel::SetConfig(const ATRS232Config& config) {
	mpDevice->SetConfig(config);
}

void ATRS232Channel::GetStatus() {
	mpMemory->WriteByte(ATKernelSymbols::DVSTAT, 0);
	mpMemory->WriteByte(ATKernelSymbols::DVSTAT+1, mInputLevel & 0xFF);
	mpMemory->WriteByte(ATKernelSymbols::DVSTAT+2, mInputLevel >> 8);
	mpMemory->WriteByte(ATKernelSymbols::DVSTAT+3, mOutputLevel);
}

bool ATRS232Channel::GetByte(uint8& c) {
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
			if ((uint8)(c - 0x20) > 0x5C)
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

void ATRS232Channel::OnScheduledEvent(uint32 id) {
	mpEvent = mpScheduler->AddEvent(mCyclesPerByte, this, 1);

	if (mOutputLevel) {
		--mOutputLevel;

		if (mpDevice)
			mpDevice->Write(mOutputBuffer[mOutputReadOffset]);

		if (++mOutputReadOffset >= sizeof mOutputBuffer)
			mOutputReadOffset = 0;
	}

	if (mpDevice) {
		uint8 c;

		if (mInputLevel < mInputBufferSize && mpDevice->Read(c)) {
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

class ATRS232Emulator : public IATRS232Emulator {
	ATRS232Emulator(const ATRS232Emulator&);
	ATRS232Emulator& operator=(const ATRS232Emulator&);
public:
	ATRS232Emulator();
	~ATRS232Emulator();

	void Init(ATCPUEmulatorMemory *mem, ATScheduler *sched, IATUIRenderer *uir);
	void Shutdown();

	void GetConfig(ATRS232Config& config);
	void SetConfig(const ATRS232Config& config);

	void OnCIOVector(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, int offset);

protected:
	ATRS232Channel mChannels[4];
	ATRS232Config mConfig;
};

IATRS232Emulator *ATCreateRS232Emulator() {
	return new ATRS232Emulator;
}

ATRS232Emulator::ATRS232Emulator() {
	mConfig.mbTelnetEmulation = true;
}

ATRS232Emulator::~ATRS232Emulator() {
	Shutdown();
}

void ATRS232Emulator::Init(ATCPUEmulatorMemory *mem, ATScheduler *sched, IATUIRenderer *uir) {
	for(int i=0; i<4; ++i) {
		mChannels[i].Init(i, mem, sched, uir);
	}
}

void ATRS232Emulator::Shutdown() {
	for(int i=0; i<4; ++i) {
		mChannels[i].Shutdown();
	}
}

void ATRS232Emulator::GetConfig(ATRS232Config& config) {
	config = mConfig;
}

void ATRS232Emulator::SetConfig(const ATRS232Config& config) {
	mConfig = config;

	for(int i=0; i<4; ++i) {
		mChannels[i].SetConfig(config);
	}
}

void ATRS232Emulator::OnCIOVector(ATCPUEmulator *cpu, ATCPUEmulatorMemory *mem, int offset) {
	uint32 idx = mem->ReadByte(ATKernelSymbols::ICDNOZ) - 1;

	if (idx >= 4) {
		cpu->Ldy(ATCIOSymbols::CIOStatUnkDevice);
		return;
	}

	ATRS232Channel& ch = mChannels[idx];

	switch(offset) {
		case 0:		// open
			cpu->Ldy(ch.Open(mem->ReadByte(ATKernelSymbols::ICAX1Z), mem->ReadByte(ATKernelSymbols::ICAX2Z)));
			break;

		case 2:		// close
			ch.Close();
			cpu->Ldy(ATCIOSymbols::CIOStatSuccess);
			break;

		case 4:		// get byte (requires assist)
			{
				uint8 c;
				if (!ch.GetByte(c)) {
					cpu->Push(cpu->GetPC() >> 8);
					cpu->Push((cpu->GetPC() - 1) & 0xFF);
					cpu->Push(0xBF);
					cpu->Push(0xE4);
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
				cpu->Push(0xBF);
				cpu->Push(0xE4);
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
						cpu->Ldy(ATCIOSymbols::CIOStatSuccess);
						return;

					case 34:	// XIO 34 control DTR, RTS, XMT (silently ignored)
						cpu->Ldy(ATCIOSymbols::CIOStatSuccess);
						return;

					case 36:	// XIO 36 configure baud rate (partial support)
						ch.ReconfigureDataRate(mem->ReadByte(ATKernelSymbols::ICAX1Z), mem->ReadByte(ATKernelSymbols::ICAX2Z));
						cpu->Ldy(ATCIOSymbols::CIOStatSuccess);
						return;

					case 38:	// XIO 38 configure translation mode (silently ignored)
						ch.ReconfigureTranslation(mem->ReadByte(ATKernelSymbols::ICAX1Z), mem->ReadByte(ATKernelSymbols::ICAX2Z));
						cpu->Ldy(ATCIOSymbols::CIOStatSuccess);
						return;

					case 40:	// XIO 40 concurrent mode (silently ignored)
						cpu->Ldy(ATCIOSymbols::CIOStatSuccess);
						return;
				}
			}
			cpu->Ldy(ATCIOSymbols::CIOStatNotSupported);
			break;
	}
}
