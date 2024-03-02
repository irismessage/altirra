//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2011 Avery Lee
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

#ifndef f_AT_SOUNDBOARD_H
#define f_AT_SOUNDBOARD_H

#include <vd2/system/memory.h>

class ATScheduler;
class ATMemoryManager;
class ATMemoryLayer;
class IATAudioOutput;

class IATSoundBoardEmulator {
public:
	virtual ~IATSoundBoardEmulator() {}

	virtual void WriteAudio(const float *left, const float *right, uint32 count, bool pushAudio, uint32 timestamp) = 0;
};

class ATSoundBoardEmulator : public VDAlignedObject<16>, public IATSoundBoardEmulator {
	ATSoundBoardEmulator(const ATSoundBoardEmulator&);
	ATSoundBoardEmulator& operator=(const ATSoundBoardEmulator&);
public:
	ATSoundBoardEmulator();
	~ATSoundBoardEmulator();

	void SetMemBase(uint32 membase);

	void Init(ATMemoryManager *memMan, ATScheduler *sch, IATAudioOutput *audioOut);
	void Shutdown();

	void ColdReset();
	void WarmReset();

	uint8 DebugReadControl(uint8 addr) const;
	uint8 ReadControl(uint8 addr);
	void WriteControl(uint8 addr, uint8 value);

	void Run(uint32 cycles);

	void WriteAudio(const float *left, const float *right, uint32 count, bool pushAudio, uint32 timestamp);

protected:
	void Flush();
	void UpdateControlLayer();

	static sint32 StaticDebugReadD2xxControl(void *thisptr, uint32 addr);
	static sint32 StaticReadD2xxControl(void *thisptr, uint32 addr);
	static bool StaticWriteD2xxControl(void *thisptr, uint32 addr, uint8 value);

	static sint32 StaticDebugReadD5xxControl(void *thisptr, uint32 addr);
	static sint32 StaticReadD5xxControl(void *thisptr, uint32 addr);
	static bool StaticWriteD5xxControl(void *thisptr, uint32 addr, uint8 value);

	struct Channel {
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
		sint16	mOverlapBuffer[8];
	};

	uint8	*mpMemory;
	ATMemoryLayer *mpMemLayerControl;
	ATScheduler *mpScheduler;
	ATMemoryManager *mpMemMan;
	IATAudioOutput *mpAudioOut;
	uint32	mMemBase;
	uint32	mLoadAddress;
	Channel	*mpCurChan;
	uint32	mAccumLevel;
	uint32	mAccumPhase;
	uint32	mAccumOffset;
	uint32	mGeneratedCycles;

	uint32	mLastUpdate;
	uint32	mCycleAccum;

	uint8	mMultiplierMode;
	uint8	mMultiplierArg1[2];
	uint8	mMultiplierArg2[2];
	uint8	mMultiplierResult[4];

	Channel mChannels[8];

	enum {
		kSampleBufferSize = 512,
		kSampleBufferOverlap = 8,
		kAccumBufferSize = 1536
	};

	VDALIGN(16) sint16 mSampleBuffer[kSampleBufferSize + kSampleBufferOverlap];
	VDALIGN(16) float mAccumBufferLeft[kAccumBufferSize];
	VDALIGN(16) float mAccumBufferRight[kAccumBufferSize];
};

#endif	// f_AT_SOUNDBOARD_H
