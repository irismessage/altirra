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

#ifndef f_AT_CASSETTEIMAGE_H
#define f_AT_CASSETTEIMAGE_H

#include <vd2/system/refcount.h>
#include "audiosource.h"

class IVDRandomAccessStream;

namespace {
	enum {
		kClockCyclesPerAudioSample = 56,
		kAudioSamplesPerSyncSampleInt = kATCyclesPerSyncSample / kClockCyclesPerAudioSample,
		kAudioSamplesPerSyncSampleFrac = kATCyclesPerSyncSample % kClockCyclesPerAudioSample,

		kAudioSamplesPerDataBit = 8,
		kClockCyclesPerDataBit = kClockCyclesPerAudioSample * kAudioSamplesPerDataBit
	};

	const float kAudioFrequency = (7159090.0f / 4.0f) / (float)kClockCyclesPerAudioSample;
	const float kDataFrequency = (7159090.0f / 4.0f) / (float)kClockCyclesPerDataBit;
}

class IATCassetteImage : public IVDRefCount {
public:
	virtual ~IATCassetteImage() {}
	virtual uint32 GetDataLength() const = 0;
	virtual uint32 GetAudioLength() const = 0;

	virtual bool GetBit(uint32 pos, uint32 averagingPeriod, uint32 threshold, bool prevBit) const = 0;

	virtual void AccumulateAudio(float *&dstLeft, float *&dstRight, uint32& posSample, uint32& posCycle, uint32 n) const = 0;
};

void ATLoadCassetteImage(IVDRandomAccessStream& file, bool loadAudioAsData, IATCassetteImage **ppImage);

#endif	// f_AT_CASSETTEIMAGE_H
