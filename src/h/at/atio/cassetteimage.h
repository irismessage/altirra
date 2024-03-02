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

#include <vd2/system/refcount.h>
#include <at/atio/image.h>
#include <at/atcore/enumparse.h>

class IVDRandomAccessStream;
class IATCassetteImage;

// Cassette internal storage is defined in terms of NTSC cycle timings.
//
// Master sync mixer rate: 1.79MHz / 28 = 64KHz
// Audio/data samples: sync mixer rate / 2 = 32KHz
//
// Note that we currently have a problem in that these are always defined
// in terms of NTSC timings, but the machine cycle rate and sync mixer
// run about 1% slower in PAL. We currently cheat and just run the tape
// 1% slower too....

constexpr int kATCassetteCyclesPerAudioSample = 56;
constexpr int kATCassetteCyclesPerDataSample = kATCassetteCyclesPerAudioSample;

// Cycle rate for cassette tape calculations (independent of actual clock
// speed).
constexpr double kATCassetteCyclesPerSecondD = 7159090.0 / 4.0;
constexpr float kATCassetteCyclesPerSecondF = (float)kATCassetteCyclesPerSecondD;

/// Sampling rate for data samples stored in memory.
constexpr float kATCassetteDataSampleRate = kATCassetteCyclesPerSecondF / (float)kATCassetteCyclesPerDataSample;
constexpr double kATCassetteDataSampleRateD = kATCassetteCyclesPerSecondD / (double)kATCassetteCyclesPerDataSample;

/// Sampling rate for audio samples stored in memory. Note that this is internal to
/// block storage; the blocks themselves resample up to sync mixer rate.
constexpr float kATCassetteImageAudioRate = kATCassetteCyclesPerSecondF / (float)kATCassetteCyclesPerAudioSample;

constexpr float kATCassetteSecondsPerDataSample = 1.0f / kATCassetteDataSampleRate;
constexpr float kATCassetteMSPerDataSample = 1000.0f / kATCassetteDataSampleRate;

/// Maximum number of data samples that we allow in a cassette image. The
/// code uses uint32, but we limit to 2^29 to give us plenty of buffer
/// room (and also to limit memory usage). At 32KHz, this is about 4.6
/// hours of tape.
const uint32 kATCassetteDataLimit = UINT32_C(0x1FFFFFFF);

/// How much room we require before the limit before we will write out a byte.
/// At 1 baud, it takes 10 seconds to write out a byte. 
const uint32 kATCassetteDataWriteByteBuffer = (uint32)(kATCassetteDataSampleRate * 12);

enum class ATCassetteTurboDecodeAlgorithm : uint8 {
	SlopeNoFilter,
	SlopeFilter,
	PeakFilter,
	PeakFilterBalanceLoHi,
	PeakFilterBalanceHiLo,
};

AT_DECLARE_ENUM_TABLE(ATCassetteTurboDecodeAlgorithm);

struct ATCassetteLoadContext {
	ATCassetteTurboDecodeAlgorithm mTurboDecodeAlgorithm = ATCassetteTurboDecodeAlgorithm::SlopeNoFilter;
	bool mbStoreWaveform = false;
};

enum ATCassetteRegionType : uint8 {
	Mark,
	Raw,
	DecodedData
};

struct ATCassetteRegionInfo {
	ATCassetteRegionType mRegionType;
	uint32 mRegionStart;
	uint32 mRegionLen;
};

struct ATCassetteWriteCursor {
	uint32 mPosition = 0;
	uint32 mCachedBlockIndex = 0;
};

struct ATTapeSampleCursor {
	uint32 mNextTransition {};	// sample position of next transition; 0 = uninitialized
	bool mCurrentValue {};		// current decoded bit, valid until next transition
	bool mNextValue {};			// next decoded bit (at next transition)

	void Reset() { mNextTransition = 0; }
};

struct ATTapeBitSumNextInfo {
	uint32 mSum;				// number of 1 bits within range
	bool mNextBit;				// polarity of next bit after range
};

struct ATTapeNextBitInfo {
	uint32 mPos;
	bool mBit;
};

struct ATTapeSlidingWindowCursor {
	ATTapeSampleCursor mHeadCursor;	// cursor for sample at window end (first sample after window)
	ATTapeSampleCursor mTailCursor;	// cursor for sample at window start (first sample within window)
	uint32 mNextCount {};			// 1-bit sum within window at next transition
	uint32 mNextTransition {};		// sample position of next transition (0 = uninitialized)
	bool mCurrentValue {};			// current decoded bit, valid until next transition
	bool mbFSKBypass {};			// true if FSK decoder should be bypassed (turbo enabled)
	uint32 mThresholdLo {};			// current sample switches to 0 when count < lo
	uint32 mThresholdHi {};			// current sample switches to 1 when count > hi
	uint32 mWindow {};				// number of samples in window
	uint32 mOffset {};				// offset from start of window to center sample

#ifdef _DEBUG
	uint32 mCurrentPos {};
#endif

	void Reset() {
		mHeadCursor.Reset();
		mTailCursor.Reset();
		mNextTransition = 0;

#ifdef _DEBUG
		mCurrentPos = 0;
#endif
	}

	void Update(IATCassetteImage& image, uint32 pos);
	bool GetBit(IATCassetteImage& image, uint32 pos);
	ATTapeBitSumNextInfo GetBitSumAndNext(IATCassetteImage& image, uint32 pos, uint32 end);
	ATTapeNextBitInfo FindNext(IATCassetteImage& image, uint32 pos, bool polarity, uint32 limit = ~(uint32)0);
};

class IATTapeImageClip : public IVDRefCount {
public:
	virtual uint32 GetLength() const = 0;
};

class IATCassetteImage : public IATImage {
public:
	enum : uint32 { kTypeID = 'csim' };

	// Returns length of data track, in data samples.
	virtual uint32 GetDataLength() const = 0;

	// Returns length of audio track, in audio samples.
	virtual uint32 GetAudioLength() const = 0;

	// Returns true if the audio track was loaded or created by editing; false
	// if it is a clone of the data track.
	virtual bool IsAudioPresent() const = 0;

	// Decodes a bit from the tape.
	//
	// pos: Center data sample position for decoding.
	// averagingPeriod: Number of data samples over which to extract a bit.
	// threshold: Threshold for 0/1 detection, relative to count (averaging period).
	// prevBit: Previous bit to reuse if sum doesn't exceed hysteresis threshold.
	//
	// Returns the decoded bit.
	//
	virtual bool GetBit(uint32 pos, uint32 averagingPeriod, uint32 threshold, bool prevBit, bool bypassFSK) const = 0;

	// Decodes a bit from the tape without averaging.
	virtual bool GetBit(uint32 pos, bool bypassFSK) const = 0;

	virtual uint32 GetBitSum(uint32 pos, uint32 period, bool bypassFSK) const = 0;

	using NextBitInfo = ATTapeNextBitInfo;

	virtual NextBitInfo FindNextBit(uint32 pos, uint32 limit, bool level, bool bypassFSK) const = 0;

	struct TransitionInfo {
		uint32 mTransitionBits;
		uint32 mSpaceBits;
		uint32 mMarkBits;
	};

	virtual TransitionInfo GetTransitionInfo(uint32 pos, uint32 n, bool bypassFSK) const = 0;

	// Read signal peaks.
	//
	// t0: First sample requested, in seconds.
	// dt: Time between samples, in seconds.
	// n: Number of samples requested.
	// data: Receives [n] min/max pairs for data track.
	// audio: Receives [n] min/max pairs for audio track.
	//
	// Peaks are returned as min/max pairs with values in [-1, 1] range.
	//
	virtual void ReadPeakMap(float t0, float dt, uint32 n, float *data, float *audio) = 0;

	// Read audio.
	//
	// dst: Auto-incremented dest pointer to output channel.
	// posSample/posCycle: Auto-incremented integer/fractional audio sample position.
	// n: Number of samples requested.
	//
	// Returns number of samples provided. If the end of the audio track is hit,
	// fewer than requested samples may be returned.
	virtual void AccumulateAudio(float *&dst, uint32& posSample, uint32& posCycle, uint32 n, float volume) const = 0;

	virtual ATCassetteRegionInfo GetRegionInfo(uint32 pos) const = 0;

	// Write data to the image at the given cursor in one of three formats:
	//
	//	1) Blank - mark bit, constant space
	//	2) Std - cooked FSK data a specific baud rate
	//	3) Raw - FSK and turbo bitfield
	//
	// The data is written at the cursor, extending with blank tape if necessary,
	// and returns with the cursor after the written data. If the tape limit is
	// hit, the amount of data written is truncated.
	//
	// In overwrite mode, the region after the cursor is overwritten and the
	// tape is only extended when the write extends beyond the end of the tape.
	// In insert mode, the region after the cursor is pushed by the amount of
	// data inserted.
	//
	virtual void WriteBlankData(ATCassetteWriteCursor& cursor, uint32 len, bool insert) = 0;
	virtual void WriteStdData(ATCassetteWriteCursor& cursor, uint8 byte, uint32 baudRate, bool insert) = 0;
	virtual void WritePulse(ATCassetteWriteCursor& cursor, bool polarity, uint32 samples, bool insert, bool fsk) = 0;

	virtual uint32 EstimateWriteStdData(ATCassetteWriteCursor& cursor, uint32 numBytes, uint32 baudRate) const = 0;

	// Insert a clip into the tape starting at the given position. If the tape
	// would extend past the tape limit, the amount of inserted tape is
	// truncated (not the end of the tape). Returns the position at the end of
	// the inserted range.
	virtual uint32 InsertRange(uint32 start, const IATTapeImageClip& clip) = 0;

	// Replace a range [start, start+len) where len is the length of the supplied
	// clip or to the tape limit, whichever is shorter. If start is beyond the
	// current end of the tape, the tape is extended with blank tape first.
	// Returns the position at the end of the replaced range.
	virtual uint32 ReplaceRange(uint32 start, const IATTapeImageClip& clip) = 0;

	// Delete a range [start, end), shifting contents after end earlier. No-op
	// if start >= end, and any partial range extending beyond the end of the
	// tape is ignored.
	virtual void DeleteRange(uint32 start, uint32 end) = 0;

	// Copies the range of tape to a clip object, which holds that tape data
	// even if the tape is subsequently changed.
	virtual vdrefptr<IATTapeImageClip> CopyRange(uint32 start, uint32 len) = 0;

	virtual uint32 GetWaveformLength() const = 0;
	virtual uint32 ReadWaveform(uint8 *dst, uint32 pos, uint32 len, bool direct) const = 0;

	struct MinMax {
		uint8 mMin;
		uint8 mMax;
	};

	virtual MinMax ReadWaveformMinMax(uint32 pos, uint32 len, bool direct) const = 0;

	// Returns true if there are any standard data blocks that must be converted
	// to FSK blocks when saving to CAS, due to trimming.
	virtual bool HasCASIncompatibleStdBlocks() const = 0;
};

void ATCreateNewCassetteImage(IATCassetteImage **ppImage);
void ATLoadCassetteImage(IVDRandomAccessStream& file, IVDRandomAccessStream *analysisOutput, const ATCassetteLoadContext& ctx, IATCassetteImage **ppImage);
void ATSaveCassetteImageCAS(IVDRandomAccessStream& file, IATCassetteImage *image);
void ATSaveCassetteImageWAV(IVDRandomAccessStream& file, IATCassetteImage *image);

#endif
