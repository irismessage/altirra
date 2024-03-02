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
#include <at/atio/cassetteimage.h>

class IVDRandomAccessStream;

class IATCassetteImage : public IVDRefCount {
public:
	virtual ~IATCassetteImage() = default;

	/// Returns length of data track, in data samples.
	virtual uint32 GetDataLength() const = 0;

	/// Returns length of audio track, in audio samples.
	virtual uint32 GetAudioLength() const = 0;

	/// Decodes a bit from the tape.
	///
	/// pos: Center data sample position for decoding.
	/// averagingPeriod: Number of data samples over which to extract a bit.
	/// threshold: Threshold for 0/1 detection, relative to count (averaging period).
	/// prevBit: Previous bit to reuse if sum doesn't exceed hysteresis threshold.
	///
	/// Returns the decoded bit.
	///
	virtual bool GetBit(uint32 pos, uint32 averagingPeriod, uint32 threshold, bool prevBit) const = 0;

	/// Read signal peaks.
	///
	/// t0: First sample requested, in seconds.
	/// dt: Time between samples, in seconds.
	/// n: Number of samples requested.
	/// data: Receives [n] min/max pairs for data track.
	/// audio: Receives [n] min/max pairs for audio track.
	///
	/// Peaks are returned as min/max pairs with values in [-1, 1] range.
	///
	virtual void ReadPeakMap(float t0, float dt, uint32 n, float *data, float *audio) = 0;

	/// Read audio.
	///
	/// dstLeft: Auto-incremented dest pointer to left channel.
	/// dstRight: Auto-incremented dest pointer to right channel.
	/// posSample/posCycle: Auto-incremented integer/fractional audio sample position.
	/// n: Number of samples requested.
	///
	/// Returns number of samples provided. If the end of the audio track is hit,
	/// fewer than requested samples may be returned.
	virtual void AccumulateAudio(float *&dstLeft, float *&dstRight, uint32& posSample, uint32& posCycle, uint32 n) const = 0;

	virtual uint32 GetWriteCursor() const = 0;
	virtual void SetWriteCursor(uint32 pos) = 0;
	virtual void WriteBlankData(uint32 len) = 0;
	virtual void WriteStdData(uint8 byte, uint32 baudRate) = 0;
	virtual void WriteFSKPulse(bool polarity, uint32 samples) = 0;
};

void ATCreateNewCassetteImage(IATCassetteImage **ppImage);
void ATLoadCassetteImage(IVDRandomAccessStream& file, bool loadAudioAsData, IATCassetteImage **ppImage);
void ATSaveCassetteImageCAS(IVDRandomAccessStream& file, IATCassetteImage *image);

#endif	// f_AT_CASSETTEIMAGE_H
