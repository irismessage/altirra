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
class ATAudioFilter;

class IATPokeyEmulatorConnections {
public:
	virtual void PokeyAssertIRQ() = 0;
	virtual void PokeyNegateIRQ() = 0;
	virtual void PokeyBreak() = 0;
	virtual bool PokeyIsInInterrupt() const = 0;
	virtual bool PokeyIsKeyPushOK(uint8 c) const = 0;
	virtual uint32 PokeyGetTimestamp() const = 0;
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

class IATPokeyAudioTap {
public:
	virtual void WriteRawAudio(const float *left, const float *right, uint32 count, uint32 timestamp) = 0;
};

class ATPokeyEmulator : public IATSchedulerCallback {
public:
	ATPokeyEmulator(bool isSlave);
	~ATPokeyEmulator();

	void	Init(IATPokeyEmulatorConnections *mem, ATScheduler *sched);
	void	ColdReset();

	void	SetSlave(ATPokeyEmulator *slave);
	void	SetCassette(IATPokeyCassetteDevice *dev);
	void	SetAudioTap(IATPokeyAudioTap *tap);

	void	SetPal(bool pal) { mbPal = pal; }
	void	SetTurbo(bool enable) { mbTurbo = enable; }

	bool	IsTraceSIOEnabled() const { return mbTraceSIO; }
	void	SetTraceSIOEnabled(bool enable) { mbTraceSIO = enable; }

	enum SerialBurstMode {
		kSerialBurstMode_Disabled,
		kSerialBurstMode_Standard,
		kSerialBurstMode_Polled,
		kSerialBurstModeCount
	};

	SerialBurstMode GetSerialBurstMode() const { return mSerBurstMode; }
	void	SetSerialBurstMode(SerialBurstMode mode);

	void	AddSIODevice(IATPokeySIODevice *device);
	void	ReceiveSIOByte(uint8 byte);

	void	SetAudioLine(int v);		// used for audio from tape
	void	SetAudioLine2(int v);		// used for audio from motor control line
	void	SetDataLine(bool newState);
	void	SetCommandLine(bool newState);
	void	SetSpeaker(bool newState) {
		if (mbSpeakerState != newState) {
			mbSpeakerState = newState;
			mbSpeakerActive = true;
			UpdateOutput();
		}
	}

	float	GetVolume() const;
	void	SetVolume(float vol);

	bool	IsChannelEnabled(uint32 channel) const { return mbChannelEnabled[channel]; }
	void	SetChannelEnabled(uint32 channel, bool enabled);

	bool	IsNonlinearMixingEnabled() const { return mbNonlinearMixingEnabled; }
	void	SetNonlinearMixingEnabled(bool enable);

	void	SetShiftKeyState(bool down);
	void	PushKey(uint8 c, bool repeat, bool allowQueue = false, bool flushQueue = true, bool useCooldown = true);
	void	PushRawKey(uint8 c);
	void	ReleaseRawKey();
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

	uint8	DebugReadByte(uint8 reg) const;
	uint8	ReadByte(uint8 reg);
	void	WriteByte(uint8 reg, uint8 value);

	void	DumpStatus();

	void	LoadState(ATSaveStateReader& reader);
	void	SaveState(ATSaveStateWriter& writer);

protected:
	void	DoFullTick();
	void	OnScheduledEvent(uint32 id);

	void	GenerateSample(uint32 pos, uint32 t);
	void	UpdatePolynomialCounters() const;
	void	FireTimers(uint8 activeChannels);
	void	OnSerialOutputTick();
	void	UpdateOutput();
	void	FlushBlock();
	void	UpdateTimerCounters(uint8 channels);
	void	SetupTimers(uint8 channels);

	void	DumpStatus(bool isSlave);

	void	ResamplerReset();
	void	ResamplerShift();
	void	ResamplerSetRate(float rate);

	void	UpdateMixTable();

	void	UnpackAUDCx(int index);
	void	TryPushNextKey();

protected:
	void	SetLast64KHzTime(uint32 t) { mLast64KHzTime = t; }
	void	SetLast15KHzTime(uint32 t) { mLast15KHzTime = t; }

protected:
	float	mAccum;
	int		mSampleCounter;
	float	mOutputLevel;
	int		mLastOutputTime;
	int		mAudioInput;
	int		mAudioInput2;
	int		mExternalInput;

	bool	mbResampleWaitForLatencyDrain;
	uint64	mResampleAccum;
	uint64	mResampleRate;
	float	mResampleRateF;
	uint32	mResampleSamplesFiltered;
	uint32	mResampleSamplesPresent;
	uint32	mResampleSamplesNeeded;
	int		mResampleRestabilizeCounter;

	ATAudioFilter	*mpFilter;

	int		mTicksAccumulated;

	int		mTimerCounters[4];

	int		mOutputs[4];
	int		mChannelVolume[4];
	bool	mNoiseFF[4];
	bool	mHighPassFF[2];

	bool	mbChannelEnabled[4];

	bool	mbCommandLineState;
	bool	mbPal;
	bool	mbTurbo;
	bool	mbTraceSIO;
	bool	mbNonlinearMixingEnabled;

	uint8	mKBCODE;
	uint32	mKeyCodeTimer;
	uint32	mKeyCooldownTimer;
	bool	mbUseKeyCooldownTimer;

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

	mutable uint32	mLastPolyTime;
	mutable uint32	mPoly17Counter;
	mutable uint32	mPoly9Counter;
	mutable uint32	mPoly5Counter;
	mutable uint32	mPoly4Counter;

	uint8	mSerialInputShiftRegister;
	uint8	mSerialOutputShiftRegister;
	uint8	mSerialInputCounter;
	uint8	mSerialOutputCounter;
	bool	mbSerOutValid;
	bool	mbSerialOutputState;
	bool	mbSpeakerState;
	bool	mbSpeakerActive;
	bool	mbSerialRateChanged;
	bool	mbSerialWaitingForStartBit;
	bool	mbSerInBurstPending;
	SerialBurstMode	mSerBurstMode;

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
	int		mSamplingRate;

	uint8	mPOT[8];
	uint8	mALLPOT;
	uint32	mPotScanStartTime;

	ATEvent *mpPotScanEvent[8];
	ATEvent	*mp64KHzEvent;
	ATEvent	*mp15KHzEvent;
	ATEvent	*mpAudioEvent;
	ATEvent	*mpStartBitEvent;
	ATEvent	*mpResetTimersEvent;
	ATEvent	*mpTimerEvents[4];
	ATEvent	*mpTimerBorrowEvents[4];
	ATScheduler *mpScheduler;

	IATPokeyEmulatorConnections *mpConn;
	ATPokeyEmulator	*mpSlave;
	const bool	mbIsSlave;

	vdautoptr<IVDAudioOutput>	mpAudioOut;

	typedef vdfastvector<IATPokeySIODevice *> Devices;
	Devices	mDevices;

	IATPokeyCassetteDevice *mpCassette;
	IATPokeyAudioTap *mpAudioTap;

	enum {
		kSamplesPerBlock = 256,
		kBlockSize = kSamplesPerBlock * 4,
		kBlockCount = 32,
		kBufferSize = kBlockSize * kBlockCount,
		kLatency = 2048 * 4 * 2,
		kRawBlockSize = kSamplesPerBlock * 16
	};

	vdfastdeque<uint8> mKeyQueue;

	float	mMixTable[61];
	float	mRawOutputBuffer[kRawBlockSize];
	float	mFilteredOutputBuffer[kRawBlockSize];
	sint16	mOutputBuffer[kBlockSize];
	uint8	mPoly4Buffer[15];
	uint8	mPoly5Buffer[31];
	uint8	mPoly9Buffer[511];
	uint8	mPoly17Buffer[131071];
};

#endif
