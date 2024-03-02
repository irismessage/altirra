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

class IATAudioOutput;
class ATPokeyEmulator;
class ATSaveStateReader;
class ATSaveStateWriter;
class ATAudioFilter;
struct ATPokeyTables;
class ATPokeyRenderer;
class IATSoundBoardEmulator;

class IATPokeyEmulatorConnections {
public:
	virtual void PokeyAssertIRQ(bool cpuBased) = 0;
	virtual void PokeyNegateIRQ(bool cpuBased) = 0;
	virtual void PokeyBreak() = 0;
	virtual bool PokeyIsInInterrupt() const = 0;
	virtual bool PokeyIsKeyPushOK(uint8 c) const = 0;
	virtual uint32 PokeyGetTimestamp() const = 0;
};

class IATPokeySIODevice {
public:
	virtual void PokeyAttachDevice(ATPokeyEmulator *pokey) = 0;
	virtual void PokeyWriteSIO(uint8 c, bool command, uint32 cyclesPerBit) = 0;
	virtual void PokeyBeginCommand() = 0;
	virtual void PokeyEndCommand() = 0;
	virtual void PokeySerInReady() = 0;
};

class IATPokeyCassetteDevice {
public:
	virtual void PokeyChangeSerialRate(uint32 divisor) = 0;
	virtual void PokeyResetSerialInput() = 0;
};

struct ATPokeyRegisterState {
	uint8 mReg[0x20];
};

struct ATPokeyAudioState {
	int		mChannelOutputs[4];
};

struct ATPokeyAudioLog {
	ATPokeyAudioState	*mpStates;
	uint32	mRecordedCount;
	uint32	mMaxCount;
};

class ATPokeyEmulator : public IATSchedulerCallback {
public:
	ATPokeyEmulator(bool isSlave);
	~ATPokeyEmulator();

	void	Init(IATPokeyEmulatorConnections *mem, ATScheduler *sched, IATAudioOutput *output, ATPokeyTables *tables);
	void	ColdReset();

	void	SetSlave(ATPokeyEmulator *slave);
	void	SetCassette(IATPokeyCassetteDevice *dev);
	void	SetSoundBoard(IATSoundBoardEmulator *sbe);
	void	SetAudioLog(ATPokeyAudioLog *log);

	void	Set5200Mode(bool enable);

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
	void	RemoveSIODevice(IATPokeySIODevice *device);

	void	ReceiveSIOByte(uint8 byte, uint32 cyclesPerBit, bool simulateInputPort = false);

	void	SetAudioLine2(int v);		// used for audio from motor control line
	void	SetDataLine(bool newState);
	void	SetCommandLine(bool newState);
	void	SetSpeaker(bool newState);

	bool	IsChannelEnabled(uint32 channel) const;
	void	SetChannelEnabled(uint32 channel, bool enabled);

	bool	IsNonlinearMixingEnabled() const { return mbNonlinearMixingEnabled; }
	void	SetNonlinearMixingEnabled(bool enable);

	bool	GetShiftKeyState() const { return mbShiftKeyState; }
	void	SetShiftKeyState(bool down);
	bool	GetControlKeyState() const { return mbControlKeyState; }
	void	SetControlKeyState(bool down);
	void	PushKey(uint8 c, bool repeat, bool allowQueue = false, bool flushQueue = true, bool useCooldown = true);
	void	PushRawKey(uint8 c);
	void	ReleaseRawKey();
	void	SetBreakKeyState(bool down);
	void	PushBreak();

	void	UpdateKeyMatrix(int index, bool depressed);
	void	SetKeyMatrix(const bool matrix[64]);
	void	ClearKeyMatrix();

	int	GetPotPos(unsigned idx) const { return mPOT[idx]; }
	void	SetPotPos(unsigned idx, int pos) {
		if (pos > 228)
			pos = 228;

		if (pos < 0)
			pos = 0;

		mPOT[idx] = (uint8)pos;
	}

	void	AdvanceScanLine();
	void	AdvanceFrame(bool pushAudio);

	uint8	DebugReadByte(uint8 reg) const;
	uint8	ReadByte(uint8 reg);
	void	WriteByte(uint8 reg, uint8 value);

	void	DumpStatus();

	void	BeginLoadState(ATSaveStateReader& reader);
	void	LoadStateArch(ATSaveStateReader& reader);
	void	LoadStatePrivate(ATSaveStateReader& reader);
	void	LoadStateResetPrivate(ATSaveStateReader& reader);
	void	EndLoadState(ATSaveStateReader& reader);
	void	BeginSaveState(ATSaveStateWriter& writer);
	void	SaveStateArch(ATSaveStateWriter& writer);
	void	SaveStatePrivate(ATSaveStateWriter& writer);

	void	GetRegisterState(ATPokeyRegisterState& state) const;
	void	GetAudioState(ATPokeyAudioState& state) const;

	void	FlushAudio(bool pushAudio);

protected:
	void	DoFullTick();
	void	OnScheduledEvent(uint32 id);

	template<uint8 channel>
	void	FireTimer();

	void	OnSerialOutputTick();
	uint32	GetSerialCyclesPerBit() const;

	void	RecomputeAllowedDeferredTimers();

	template<int channel>
	void	RecomputeTimerPeriod();

	template<int channel>
	void	UpdateTimerCounter();

	void	SetupTimers(uint8 channels);
	void	FlushDeferredTimerEvents(int channel);
	void	SetupDeferredTimerEvents(int channel, uint32 t0, uint32 period);
	void	SetupDeferredTimerEventsLinked(int channel, uint32 t0, uint32 period, uint32 hit0, uint32 hiperiod, uint32 hilooffset);

	void	DumpStatus(bool isSlave);

	void	UpdateMixTable();

	void	TryPushNextKey();

protected:
	void	SetLast64KHzTime(uint32 t) { mLast64KHzTime = t; }
	void	SetLast15KHzTime(uint32 t) { mLast15KHzTime = t; }

protected:
	ATPokeyRenderer *mpRenderer;

	int		mTimerCounters[4];

	bool	mbCommandLineState;
	bool	mbPal;
	bool	mb5200Mode;
	bool	mbTraceSIO;
	bool	mbNonlinearMixingEnabled;

	uint8	mKBCODE;
	uint32	mKeyCodeTimer;
	uint32	mKeyCooldownTimer;
	bool	mbKeyboardIRQPending;
	bool	mbUseKeyCooldownTimer;
	bool	mbShiftKeyState;
	bool	mbControlKeyState;
	bool	mbBreakKeyState;

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

	ATPokeyRegisterState mState;

	// countdown timer values
	int		mAUDFP1[4];		// AUDF values, plus 1 (we use these everywhere)
	int		mCounter[4];
	int		mCounterBorrow[4];
	uint32	mTimerPeriod[4];
	uint32	mTimerFullPeriod[2];		// time for timer to count off 256 in linked mode (#1 and #3 only)

	mutable uint32	mLastPolyTime;
	mutable uint32	mPoly17Counter;
	mutable uint32	mPoly9Counter;

	uint8	mSerialInputShiftRegister;
	uint8	mSerialOutputShiftRegister;
	uint8	mSerialInputCounter;
	uint8	mSerialOutputCounter;
	bool	mbSerOutValid;
	bool	mbSerShiftValid;
	bool	mbSerialOutputState;
	bool	mbSpeakerActive;
	bool	mbSerialRateChanged;
	bool	mbSerialWaitingForStartBit;
	bool	mbSerInBurstPending;

	uint32	mSerialSimulateInputBaseTime;
	uint32	mSerialSimulateInputCyclesPerBit;
	uint32	mSerialSimulateInputData;
	bool	mbSerialSimulateInputPort;

	SerialBurstMode	mSerBurstMode;

	ATPokeyTables *mpTables;
	ATPokeyAudioLog	*mpAudioLog;

	// AUDCTL breakout
	bool	mbFastTimer1;
	bool	mbFastTimer3;
	bool	mbLinkedTimers12;
	bool	mbLinkedTimers34;
	bool	mbUse15KHzClock;

	bool	mbAllowDeferredTimer[4];

	uint32	mLast15KHzTime;
	uint32	mLast64KHzTime;

	uint8	mPOT[8];
	uint8	mALLPOT;
	uint32	mPotScanStartTime;

	ATEvent *mpPotScanEvent[8];
	ATEvent	*mp15KHzEvent;
	ATEvent	*mpAudioEvent;
	ATEvent	*mpStartBitEvent;
	ATEvent	*mpResetTimersEvent;
	ATEvent *mpEventSerialOutput;
	ATEvent	*mpTimerBorrowEvents[4];

	bool	mbDeferredTimerEvents[4];
	uint32	mDeferredTimerStarts[4];
	uint32	mDeferredTimerPeriods[4];

	ATScheduler *mpScheduler;

	IATPokeyEmulatorConnections *mpConn;
	ATPokeyEmulator	*mpSlave;
	const bool	mbIsSlave;

	IATAudioOutput *mpAudioOut;

	typedef vdfastvector<IATPokeySIODevice *> Devices;
	Devices	mDevices;

	IATPokeyCassetteDevice *mpCassette;
	IATSoundBoardEmulator *mpSoundBoard;

	vdfastdeque<uint8> mKeyQueue;

	bool	mbKeyMatrix[64];
	uint8	mKeyScanState;
	uint8	mKeyScanCode;
	uint8	mKeyScanLatch;
};

#endif
