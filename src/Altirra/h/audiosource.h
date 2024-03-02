//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2015 Avery Lee
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
//
///////////////////////////////////////////////////////////////////////////
//
// Synchronous audio mixer system
//
// The main emulation audio path in Altirra runs synchronously at 1/28th
// of machine rate, or 63920.4Hz NTSC / 63337.4Hz PAL. The mixer can run
// in either mono or stereo mode, and switch between them dynamically;
// this is to reduce the filtering load.
//
// After mixing, the outputs go through a high-pass filter with a long
// time constant to remove DC bias. The raw 63KHz output is tapped off
// for the audio recorder, and then it goes through a polyphase
// low-pass / resampling filter to get reduced down to 44.1KHz. This then
// goes through a crude time stretcher if necessary to deal with temporary
// deltas between audio and video.
//

#ifndef f_AT_AUDIOSOURCE_H
#define f_AT_AUDIOSOURCE_H

enum {
	kATCyclesPerSyncSample = 28
};

struct ATSyncAudioMixInfo {
	// Start time in machine cycles (NOT samples).
	uint32 mStartTime;

	// Number of samples to mix.
	uint32 mCount;

	// Mix buffers.
	float *mpLeft;
	float *mpRight;
};

class IATSyncAudioSource {
public:
	virtual bool SupportsStereoMixing() const = 0;
	virtual bool RequiresStereoMixingNow() const = 0;
	virtual void WriteAudio(const ATSyncAudioMixInfo& mixInfo) = 0;
};

#endif
