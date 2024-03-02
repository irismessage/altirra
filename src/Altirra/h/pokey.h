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

#ifndef AT_POKEY_H
#define AT_POKEY_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <vd2/system/vdalloc.h>
#include <vd2/system/vdstl.h>
#include "scheduler.h"

class IVDAudioOutput;
class ATPokeyEmulator;
class ATSaveStateReader;
class ATSaveStateWriter;

class IATPokeyEmulatorConnections {
public:
	virtual void PokeyAssertIRQ() = 0;
	virtual void PokeyNegateIRQ() = 0;
	virtual void PokeyBreak() = 0;
};

class IATPokeySIODevice {
public:
	virtual void PokeyAttachDevice(ATPokeyEmulator *pokey) = 0;
	virtual void PokeyWriteSIO(uint8 c) = 0;
	virtual void PokeyBeginCommand() = 0;
	virtual void PokeyEndCommand() = 0;
	virtual void PokeySerInReady() = 0;
};

class IATPokeyCassetteDevice {
public:
	virtual void PokeyChangeSerialRate(uint32 divisor) = 0;
	virtual void PokeyResetSerialInput() = 0;
};

struct ATAudioFilter {
	float	mHiPassAccum;
	float	mLoPassPrev1;
	float	mLoPassPrev2;
	float	mLoPassPrev3;
	float	mLoPassPrev4;

	ATAudioFilter();

	void Filter(float * VDRESTRICT dst, const float * VDRESTRICT src, uint32 count, float loCoeff, float hiCoeff);
};


class ATPokeyEmulator : public IATSchedulerCallback {
public:
	ATPokeyEmulator(bool isSlave);
	~ATPokeyEmulator();

	void	Init(IATPokeyEmulatorConnections *mem, ATScheduler *sched);
	void	ColdReset();

	void	SetSlave(ATPokeyEmulator *slave);
	void	SetCassette(IATPokeyCassetteDevice *dev);

	void	SetPal(bool pal) { mbPal = pal; }
	void	SetTurbo(bool enable) { mbTurbo = enable; }

	bool	IsTraceSIOEnabled() const { return mbTraceSIO; }
	void	SetTraceSIOEnabled(bool enable) { mbTraceSIO = enable; }

	void	AddSIODevice(IATPokeySIODevice *device);
	void	ReceiveSIOByte(uint8 byte);

	void	SetAudioLine(int v);
	void	SetDataLine(bool newState);
	void	SetCommandLine(bool newState);
	void	SetSpeaker(bool newState) { mbSpeakerState = newState; }

	void	SetShiftKeyState(bool down);
	void	PushKey(uint8 c, bool repeat);
	void	PushBreak();

	int	GetPotPos(unsigned idx) const { return mPOT[idx]; }
	void	SetPotPos(unsigned idx, int pos) {
		if (pos > 228)
			pos = 228;

		if (pos < 0)
			pos = 0;

		mPOT[idx] = (uint8)pos;
	}

	void	AdvanceScanLine();
	void	AdvanceFrame();

	uint8	DebugReadByte(uint8 reg);
	uint8	ReadByte(uint8 reg);
	void	WriteByte(uint8 reg, uint8 value);

	void	DumpStatus();

	void	LoadState(ATSaveStateReader& reader);
	void	SaveState(ATSaveStateWriter& writer);

protected:
	void	DoFullTick();
	void	OnScheduledEvent(uint32 id);

	void	GenerateSample(uint32 pos, uint32 t);
	void	UpdatePolynomialCounters();
	void	FireTimers(uint8 activeChannels);
	void	UpdateOutput();
	void	FlushBlock();
	void	UpdateTimerCounters(uint8 channels);
	void	SetupTimers(uint8 channels);

	void	DumpStatus(bool isSlave);

	void	ResamplerReset();
	void	ResamplerShift();
	void	ResamplerSetRate(float rate);

protected:
	void	SetLast64KHzTime(uint32 t) { mLast64KHzTime = t; }
	void	SetLast15KHzTime(uint32 t) { mLast15KHzTime = t; }

protected:
	int		mAccum;
	int		mSampleCounter;
	int		mOutputLevel;
	int		mLastOutputTime;
	int		mExternalInput;

	bool	mbResampleWaitForLatencyDrain;
	uint64	mResampleAccum;
	uint64	mResampleRate;
	float	mResampleRateF;
	uint32	mResampleSamplesFiltered;
	uint32	mResampleSamplesPresent;
	uint32	mResampleSamplesNeeded;
	int		mResampleRestabilizeCounter;

	ATAudioFilter	mFilter;

	int		mTicksAccumulated;

	int		mTimerCounters[4];

	int		mOutputs[4];
	bool	mNoiseFF[4];
	bool	mHighPassFF[2];

	bool	mbCommandLineState;
	bool	mbPal;
	bool	mbTurbo;
	bool	mbTraceSIO;

	uint8	mKBCODE;
	uint32	mKeyCodeTimer;

	uint8	mIRQEN;
	uint8	mIRQST;
	uint8	mAUDF[4];		// $D200/2/4/6: audio frequency, channel 1/2/3/4
	uint8	mAUDC[4];		// $D201/3/5/7: audio control, channel 1/2/3/4
	uint8	mAUDCTL;		// $D208
							// bit 7: use 9-bit poly instead of 17-bit poly
							// bit 6: clock channel 1 with 1.79MHz instead of 64KHz
							// bit 5: clock channel 3 with 1.79MHz instead of 64KHz
							// bit 4: clock channel 2 with channel 1 instead of 64KHz
							// bit 3: clock channel 4 with channel 3 instead of 64KHz
							// bit 2: apply high pass filter to channel 1 using channel 3
							// bit 1: apply high pass filter to channel 2 using channel 4
							// bit 0: change 64KHz frequency to 15KHz
	uint8	mSERIN;			// $D20D: SERIN
	uint8	mSEROUT;		// $D20D: SEROUT
	uint8	mSKSTAT;		// $D20F: SKSTAT
							// bit 3: shift key depressed
							// bit 2: key depressed
	uint8	mSKCTL;			// $D20F: SKCTL
							// bit 3: shift key depressed
							// bit 2: key depressed

	int		mCounter[4];

	uint32	mLastPolyTime;
	uint32	mPoly17Counter;
	uint32	mPoly9Counter;
	uint32	mPoly5Counter;
	uint32	mPoly4Counter;

	uint8	mSerialInputShiftRegister;
	uint8	mSerialOutputShiftRegister;
	uint8	mSerialOutputCounter;
	bool	mbSerInValid;
	bool	mbSerShiftInValid;
	bool	mbSerOutValid;
	bool	mbSerialOutputState;
	bool	mbSpeakerState;
	bool	mbSerialRateChanged;
	bool	mbSerialStartBitActive;

	// AUDCTL breakout
	bool	mbFastTimer1;
	bool	mbFastTimer3;
	bool	mbLinkedTimers12;
	bool	mbLinkedTimers34;
	bool	mbUse15KHzClock;

	uint32	mLast15KHzTime;
	uint32	mLast64KHzTime;
	uint32	mAudioRate;
	float	mAudioDampedError;

	uint8	mPOT[8];
	uint8	mALLPOT;
	uint32	mPotScanStartTime;

	ATEvent *mpPotScanEvent[8];
	ATEvent	*mp64KHzEvent;
	ATEvent	*mp15KHzEvent;
	ATEvent	*mpStartBitEvent;
	ATEvent	*mpTimerEvents[4];
	ATScheduler *mpScheduler;

	IATPokeyEmulatorConnections *mpConn;
	ATPokeyEmulator	*mpSlave;
	const bool	mbIsSlave;

	vdautoptr<IVDAudioOutput>	mpAudioOut;

	typedef vdfastvector<IATPokeySIODevice *> Devices;
	Devices	mDevices;

	IATPokeyCassetteDevice *mpCassette;

	enum {
		kSamplesPerBlock = 128,
		kBlockSize = kSamplesPerBlock * 4,
		kBlockCount = 32,
		kBufferSize = kBlockSize * kBlockCount,
		kLatency = 2048 * 4,
		kRawBlockSize = kSamplesPerBlock * 16
	};

	float	mRawOutputBuffer[kRawBlockSize];
	float	mFilteredOutputBuffer[kRawBlockSize];
	sint16	mOutputBuffer[kBlockSize];
	bool	mPoly4Buffer[15];
	bool	mPoly5Buffer[31];
	uint8	mPoly9Buffer[511];
	uint8	mPoly17Buffer[131071];
};

#endif
