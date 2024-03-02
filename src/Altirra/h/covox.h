//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2012 Avery Lee
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

#ifndef f_AT_COVOX_H
#define f_AT_COVOX_H

#include "audiosource.h"

class ATScheduler;
class ATMemoryManager;
class ATMemoryLayer;
class IATAudioOutput;

class ATCovoxEmulator : public IATSyncAudioSource {
	ATCovoxEmulator(const ATCovoxEmulator&);
	ATCovoxEmulator& operator=(const ATCovoxEmulator&);
public:
	ATCovoxEmulator();
	~ATCovoxEmulator();

	void Init(ATMemoryManager *memMan, ATScheduler *sch, IATAudioOutput *audioOut);
	void Shutdown();

	void ColdReset();
	void WarmReset();

	void DumpStatus();

	void WriteControl(uint8 addr, uint8 value);

	void Run(int cycles);

public:
	void WriteAudio(uint32 startTime, float *dstLeft, float *dstRightOpt, uint32 n);

protected:
	void Flush();

	static sint32 StaticReadControl(void *thisptr, uint32 addr);
	static bool StaticWriteControl(void *thisptr, uint32 addr, uint8 value);

	ATMemoryLayer *mpMemLayerControl;
	ATScheduler *mpScheduler;
	ATMemoryManager *mpMemMan;
	IATAudioOutput *mpAudioOut;

	uint8	mVolume[4];

	float	mOutputAccumLeft;
	float	mOutputAccumRight;
	uint32	mOutputCount;
	uint32	mOutputLevel;

	uint32	mLastUpdate;

	enum {
		kAccumBufferSize = 1536
	};

	VDALIGN(16) float mAccumBufferLeft[kAccumBufferSize];
	VDALIGN(16) float mAccumBufferRight[kAccumBufferSize];
};

#endif
