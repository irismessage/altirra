//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2013 Avery Lee
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

#ifndef f_AT_SIMPLESTEREO_H
#define f_AT_SIMPLESTEREO_H

#include "audiosource.h"

class ATScheduler;
class ATMemoryManager;
class ATMemoryLayer;
class IATAudioOutput;

class ATSimpleStereoEmulator : public IATSyncAudioSource {
	ATSimpleStereoEmulator(const ATSimpleStereoEmulator&);
	ATSimpleStereoEmulator& operator=(const ATSimpleStereoEmulator&);
public:
	ATSimpleStereoEmulator();
	~ATSimpleStereoEmulator();

	void SetMemBase(uint32 membase);

	void Init(ATMemoryManager *memMan, ATScheduler *sch, IATAudioOutput *audioOut);
	void Shutdown();

	void ColdReset();
	void WarmReset();

	uint8 DebugReadControl(uint8 addr) const;
	uint8 ReadControl(uint8 addr);
	void WriteControl(uint8 addr, uint8 value);

	void Run(uint32 cycles);

	void WriteAudio(uint32 startTime, float *dstLeft, float *dstRightOpt, uint32 n);

protected:
	void Flush();
	void UpdateControlLayer();

	static sint32 StaticDebugReadD2xxControl(void *thisptr, uint32 addr);
	static sint32 StaticReadD2xxControl(void *thisptr, uint32 addr);
	static bool StaticWriteD2xxControl(void *thisptr, uint32 addr, uint8 value);

	static sint32 StaticDebugReadD5xxControl(void *thisptr, uint32 addr);
	static sint32 StaticReadD5xxControl(void *thisptr, uint32 addr);
	static bool StaticWriteD5xxControl(void *thisptr, uint32 addr, uint8 value);

	enum WaveformMode {
		kWaveformMode_Sample,
		kWaveformMode_Pulse,
		kWaveformMode_Sawtooth,
		kWaveformMode_Triangle,
		kWaveformMode_Noise,
		kWaveformMode_Disabled
	};

	struct Channel {
		WaveformMode	mWaveformMode;
		uint32	mPhase;
		uint32	mAddress;
		uint32	mLength;
		uint32	mRepeat;
		uint32	mFreq;
		uint8	mVolume;
		uint8	mPan;
		uint8	mAttack;
		uint8	mDecay;
		uint8	mSustain;
		uint8	mRelease;
		uint8	mControl;
		uint8	mLFSRLatch;
		sint16	mOverlapBuffer[16];
	};

	ATMemoryLayer *mpMemLayerControl;
	ATScheduler *mpScheduler;
	ATMemoryManager *mpMemMan;
	IATAudioOutput *mpAudioOut;
	uint32	mMemBase;
	uint32	mLoadAddress;
	Channel	*mpCurChan;
	uint32	mAccumLevel;
	uint32	mAccumOffset;
	uint32	mGeneratedCycles;

	uint32	mLastUpdate;
	uint32	mCycleAccum;
	uint32	mLFSROffset;

	Channel mChannels[16];

	enum {
		kSampleBufferSize = 512,
		kSampleBufferOverlap = 14,
		kAccumBufferSize = 1536
	};

	VDALIGN(16) sint16 mSampleBuffer[kSampleBufferSize + kSampleBufferOverlap];
	VDALIGN(16) float mAccumBufferLeft[kAccumBufferSize];
	VDALIGN(16) float mAccumBufferRight[kAccumBufferSize];
	VDALIGN(16) uint8 mMemory[4096];
	VDALIGN(16) uint8 mLFSR[1 << 23];		// 8MB!
};

#endif	// f_AT_SIMPLESTEREO_H
