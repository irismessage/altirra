﻿//	Altirra - Atari 800/800XL/5200 emulator
//	Core library - audio mixing source definitions
//	Copyright (C) 2008-2016 Avery Lee
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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#ifndef f_AT_ATCORE_AUDIOSOURCE_H
#define f_AT_ATCORE_AUDIOSOURCE_H

//=========================================================================
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
// Levels are relative to the output of the POKEY renderer. This is
// because POKEY is the most active and CPU intensive source to
// generate, so it sets the scale and all of the other sources just get
// this factor folded into their gain factors. As of Altirra 3.90, POKEY's
// output is normalized -- the (28*60) factor is gone and all mix levels
// are scaled to compensate for any inherent scales so that mixing sources
// do not have to account for them.
//
// Two global filters are applied at the end of the mixbus. One is a DC
// filter to remove bias. It is OK for audio sources to produce biased
// output, although it is best if the initial cold start value is zero
// to avoid clicking and unnecessary mix overhead. The second is a low-
// pass filter at about 15KHz.
//

enum {
	kATCyclesPerSyncSample = 28
};

struct ATSyncAudioMixInfo {
	// Start time in machine cycles (NOT samples). This time is guaranteed
	// to be monotonic but not necessarily continuous -- the mixer is
	// allowed to drop samples. This is considered a glitch and sources
	// do not need to produce perfect audio across such discontinuities.
	//
	// Note that this is the start time for the sample window, and not the
	// frame. Mixing frames are close to sampling frames but not quite the
	// same since the frame time is not evenly divisible in samples. The
	// mixing window always contains the start of the frame but may end
	// slightly short. Sources need to be prepared to carry audio info
	// into the next frame to accommodate this.
	uint64 mStartTime;

	// Number of samples to mix. This may vary but is normally about a
	// frame's worth of samples (~1050-1300).
	uint32 mCount;

	// Number of cycles covered by the mixing window. This is provided for
	// convenience; it's always equal to mCount * kATCyclesPerSyncSample.
	uint32 mNumCycles;

	// Mixing sample frequency in Hz. Note that this is related to actual
	// playback frequency and is NOT equal to the system clock rate divided
	// by kATCyclesPerSyncSample. Depending on the frame rate mode it may
	// be tweaked up and down by a small amount. However, it is not
	// affected by warp or slow-mo as we handle that by a time shifter.
	// The mixing rate is nominally around 63.9KHz.
	float mMixingRate;

	// Mix buffers. Audio must be *added* into these buffers. mpRight is
	// null if mono mixing is occurring. Because the mix operation is
	// additive, a source is not required to write to all of or even any
	// of the buffer if it does not have signal to add over the whole
	// duration.
	float *mpLeft;
	float *mpRight;

	// DC mix levels. Any DC that needs to be added to the left or right
	// channels can be added to these values instead; this is equivalent
	// to and faster than adding the value to each entry in the mixing
	// buffer. mDCRight is active only if mpRight is also active.
	float *mpDCLeft;
	float *mpDCRight;

	// Mix levels (see ATAudioMix). Audio sources are expected to always
	// use a mix level from this table, Other if nothing else.
	const float *mpMixLevels;
};

class IATSyncAudioSource {
public:
	// Returns true if the audio source requires stereo mixing for the
	// next mix operation, i.e. its L/R output will be different. This allows
	// the mixbus to dynamically switch between mono and stereo mixing not
	// only on whether a source _can_ produce stereo, but also whether it is
	// _actually_ producing stereo. The mixer calls this on all sources before
	// calling WriteAudio() on all of them.
	//
	// Starting with Altirra 3.10, audio sources are sorted so that mono
	// sources are always asked to mix mono and stereo sources are always
	// asked to mix stereo. A mono source is no longer required to support
	// mixing into stereo buffers.
	virtual bool RequiresStereoMixingNow() const = 0;

	// Mix audio into the mixbus.
	virtual void WriteAudio(const ATSyncAudioMixInfo& mixInfo) = 0;
};

#endif
