//	Altirra - Atari 800/800XL/5200 emulator
//	I/O library - cassette tape image definitions
//	Copyright (C) 2009-2016 Avery Lee
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

#ifndef f_AT_ATIO_CASSETTEIMAGE_H
#define f_AT_ATIO_CASSETTEIMAGE_H

// Cassette internal storage is defined in terms of NTSC cycle timings.
//
// Master sync mixer rate: 1.79MHz / 28 = 64KHz
// Audio samples: sync mixer rate / 2 = 32KHz
// Data samples: audio sample rate / 8 = 4KHz
//
// Note that we currently have a problem in that these are always defined
// in terms of NTSC timings, but the machine cycle rate and sync mixer
// run about 1% slower in PAL. We currently cheat and just run the tape
// 1% slower too....

const int kATCassetteAudioSamplesPerDataSample = 8;
const int kATCassetteCyclesPerAudioSample = 56;
const int kATCassetteCyclesPerDataSample = kATCassetteCyclesPerAudioSample * kATCassetteAudioSamplesPerDataSample;

/// Sampling rate for data samples stored in memory.
const float kATCassetteDataSampleRate = (7159090.0f / 4.0f) / (float)kATCassetteCyclesPerDataSample;

/// Sampling rate for audio samples stored in memory. Note that this is internal to
/// block storage; the blocks themselves resample up to sync mixer rate.
const float kATCassetteImageAudioRate = (7159090.0f / 4.0f) / (float)kATCassetteCyclesPerAudioSample;

/// Maximum number of data samples that we allow in a cassette image. The
/// code uses uint32, but we limit to 2^31 to give us plenty of buffer
/// room (and also to limit memory usage). At 4KHz, this is about 37
/// hours of tape.
const uint32 kATCassetteDataLimit = UINT32_C(0x1FFFFFFF);

/// How much room we require before the limit before we will write out a byte.
/// At 1 baud, it takes 10 seconds to write out a byte. 
const uint32 kATCassetteDataWriteByteBuffer = (uint32)(kATCassetteDataSampleRate * 12);

#endif
