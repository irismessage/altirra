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

#include "stdafx.h"
#include <vd2/system/binary.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/math.h>
#include <vd2/Riza/audioformat.h>
#include <math.h>
#include "cassette.h"
#include "cpu.h"
#include "console.h"

using namespace nsVDWinFormats;

///////////////////////////////////////////////////////////////////////////////

namespace {
	enum {
		kClockCyclesPerAudioSample = 56,
		kAudioSamplesPerDataBit = 8,
		kClockCyclesPerDataBit = kClockCyclesPerAudioSample * kAudioSamplesPerDataBit
	};

	const float kAudioFrequency = (7159090.0f / 4.0f) / (float)kClockCyclesPerAudioSample;
	const float kDataFrequency = (7159090.0f / 4.0f) / (float)kClockCyclesPerDataBit;
}

///////////////////////////////////////////////////////////////////////////////

class ATCassetteDecoderFSK {
public:
	ATCassetteDecoderFSK();

	void Reset();
	bool Advance(float x);

protected:
	float mHistory[64];
	float mHistorySum3[64];
	float mHistorySum4[64];
	int mIndex;
};

ATCassetteDecoderFSK::ATCassetteDecoderFSK() {
	Reset();
}

void ATCassetteDecoderFSK::Reset() {
	mIndex = 0;
	memset(mHistory, 0, sizeof mHistory);
	memset(mHistorySum3, 0, sizeof mHistorySum3);
	memset(mHistorySum4, 0, sizeof mHistorySum4);
}

bool ATCassetteDecoderFSK::Advance(float x) {
	int base = mIndex++ & 31;
	int write = (base + 31) & 31;

	mHistory[write] = mHistory[write + 32] = x;

	// 3995Hz filter has an 8 cycle period.
	// 5327Hz filter has a 6 cycle period.

#if 0
	const float sin0_12 = 0.0f;
	const float sin1_12 = 0.5f;
	const float sin2_12 = 0.86602540378443864676372317075294f;
	const float sin3_12 = 1.0f;
	const float sin4_12 = 0.86602540378443864676372317075294f;
	const float sin5_12 = 0.5f;
	const float sin6_12 = 0.0f;
	const float sin7_12 = -0.5f;
	const float sin8_12 = -0.86602540378443864676372317075294f;
	const float sin9_12 = -1.0f;
	const float sin10_12 = -0.86602540378443864676372317075294f;
	const float sin11_12 = -0.5f;

	const float sin0_8 = 0.0f;
	const float sin1_8 = 0.707107f;
	const float sin2_8 = 1.0f;
	const float sin3_8 = 0.707107f;
	const float sin4_8 = 0.0f;
	const float sin5_8 = -0.707107f;
	const float sin6_8 = -1.0f;
	const float sin7_8 = -0.707107f;

	const float *xs = &mHistory[base];
	float x0 = xs[0] + xs[ 8] + xs[16];
	float x1 = xs[1] + xs[ 9] + xs[17];
	float x2 = xs[2] + xs[10] + xs[18];
	float x3 = xs[3] + xs[11] + xs[19];
	float x4 = xs[4] + xs[12] + xs[20];
	float x5 = xs[5] + xs[13] + xs[21];
	float x6 = xs[6] + xs[14] + xs[22];
	float x7 = xs[7] + xs[15] + xs[23];

	float zero_s = x0*sin0_8 + x1*sin1_8 + x2*sin2_8 + x3*sin3_8 + x4*sin4_8 + x5*sin5_8 + x6*sin6_8 + x7*sin7_8;
	float zero_c = x0*sin2_8 + x1*sin3_8 + x2*sin4_8 + x3*sin5_8 + x4*sin6_8 + x5*sin7_8 + x6*sin0_8 + x7*sin1_8;

	float y1 = xs[0] + xs[ 6] + xs[12] + xs[18];
	float y2 = xs[1] + xs[ 7] + xs[13] + xs[19];
	float y3 = xs[2] + xs[ 8] + xs[14] + xs[20];
	float y4 = xs[3] + xs[ 9] + xs[15] + xs[21];
	float y5 = xs[4] + xs[10] + xs[16] + xs[22];
	float y6 = xs[5] + xs[11] + xs[17] + xs[23];

	float one_s = y1*sin0_12 + y2*sin2_12 + y3*sin4_12 + y4*sin6_12 + y5*sin8_12 + y6*sin10_12;
	float one_c = y1*sin3_12 + y2*sin5_12 + y3*sin7_12 + y4*sin9_12 + y5*sin11_12 + y6*sin1_12;
#else
	const float *xs = &mHistory[base + 8];
	mHistorySum3[write] = mHistorySum3[write + 32] = xs[7] + xs[15] + xs[23];
	mHistorySum4[write] = mHistorySum4[write + 32] = xs[5] + xs[11] + xs[17] + xs[23];

	const float *s3 = &mHistorySum3[base + 32 - 8];
	const float *s4 = &mHistorySum4[base + 32 - 6];

	const float x0 = s3[0];
	const float x1 = s3[1];
	const float x2 = s3[2];
	const float x3 = s3[3];
	const float x4 = s3[4];
	const float x5 = s3[5];
	const float x6 = s3[6];
	const float x7 = s3[7];

	const float sqrt2 = 0.707107f;
	float x1mx5 = x1 - x5;
	float x3mx7 = x3 - x7;
	float zero_s = (x2 - x6) + (x1mx5 + x3mx7)*sqrt2;
	float zero_c = (x0 - x4) + (x1mx5 - x3mx7)*sqrt2;

	const float y1 = s4[0];
	const float y2 = s4[1];
	const float y3 = s4[2];
	const float y4 = s4[3];
	const float y5 = s4[4];
	const float y6 = s4[5];

	const float half = 0.5f;
	const float sin2_12 = 0.86602540378443864676372317075294f;

	float y2my5 = y2 - y5;
	float y3my6 = y3 - y6;
	float one_s = (y2my5 + y3my6)*sin2_12;
	float one_c = (y1 - y4) + (y2my5 - y3my6)*half;
#endif

	float one = (one_s * one_s + one_c * one_c) * ((8.0f / 6.0f) * (8.0f / 6.0f));
	float zero = (zero_s * zero_s + zero_c * zero_c);

	return one > zero;
}

///////////////////////////////////////////////////////////////////////////////

class ATCassetteDecoderDirect {
public:
	ATCassetteDecoderDirect();

	void Reset();
	bool Advance(float x);

protected:
	float mZeroLevel;
	float mOneLevel;
	bool mbCurrentState;
};

ATCassetteDecoderDirect::ATCassetteDecoderDirect() {
	Reset();
}

void ATCassetteDecoderDirect::Reset() {
	mZeroLevel = 0.0f;
	mOneLevel = 1.0f;
}

bool ATCassetteDecoderDirect::Advance(float x) {
	float range = mOneLevel - mZeroLevel;
	float oneThird = mZeroLevel + range * (1.0f / 3.0f);
	float twoThirds = mZeroLevel + range * (2.0f / 3.0f);

	if (x < oneThird) {
		mbCurrentState = false;

		mZeroLevel += (x - mZeroLevel) * 0.95f;
	} else if (x > twoThirds) {
		mbCurrentState = true;

		mOneLevel += (x - mOneLevel) * 0.95f;
	}

	return mbCurrentState;
}

///////////////////////////////////////////////////////////////////////////////

namespace {
	enum {
		kATCassetteEventId_ProcessBit = 1,
		kATCassetteEventId_UpdateAudio = 2
	};
}

ATCassetteEmulator::ATCassetteEmulator()
	: mpPlayEvent(NULL)
	, mpAudioEvent(NULL)
	, mbLogData(false)
	, mbLoadDataAsAudio(false)
{
	mAudioPosition = 0;
	mAudioLength = 0;
	mPosition = 0;
	mLength = 0;
}

ATCassetteEmulator::~ATCassetteEmulator() {
}

float ATCassetteEmulator::GetLength() const {
	return mLength / kDataFrequency;
}

float ATCassetteEmulator::GetPosition() const {
	return mPosition / kDataFrequency;
}

void ATCassetteEmulator::Init(ATPokeyEmulator *pokey, ATScheduler *sched) {
	mpPokey = pokey;
	mpScheduler = sched;

	ColdReset();
}

void ATCassetteEmulator::ColdReset() {
	mbOutputBit = false;
	mbMotorEnable = false;
	mbPlayEnable = false;
	mbDataLineState = false;
	mSIOPhase = 0;
	mDataByte = 0;
	mDataBitCounter = 0;

	RewindToStart();
	Play();
}

namespace {
	void ReadMono8(sint16 *dst, VDFile& src, uint32 count) {
		uint8 buf[1024];

		while(count) {
			uint32 tc = count > 1024 ? 1024 : count;
			count -= tc;

			src.read(buf, tc);

			for(uint32 i=0; i<tc; ++i) {
				dst[0] = dst[1] = (buf[i] << 8) - 0x8000;
				dst += 2;
			}
		}
	}

	void ReadMono16(sint16 *dst, VDFile& src, uint32 count) {
		sint16 buf[1024];

		while(count) {
			uint32 tc = count > 1024 ? 1024 : count;
			count -= tc;

			src.read(buf, tc*2);

			for(uint32 i=0; i<tc; ++i) {
				dst[0] = dst[1] = buf[i];
				dst += 2;
			}
		}
	}

	void ReadStereo8(sint16 *dst, VDFile& src, uint32 count) {
		uint8 buf[1024][2];

		while(count) {
			uint32 tc = count > 1024 ? 1024 : count;
			count -= tc;

			src.read(buf, tc*2);

			for(uint32 i=0; i<tc; ++i) {
				dst[0] = (buf[i][0] << 8) - 0x8000;
				dst[1] = (buf[i][1] << 8) - 0x8000;
				dst += 2;
			}
		}
	}

	void ReadStereo16(sint16 *dst, VDFile& src, uint32 count) {
		src.read(dst, count*4);
	}

	extern "C" __declspec(align(16)) const sint16 kernel[32][8] = {
		{+0x0000,+0x0000,+0x0000,+0x4000,+0x0000,+0x0000,+0x0000,+0x0000 },
		{-0x000a,+0x0052,-0x0179,+0x3fe2,+0x019f,-0x005b,+0x000c,+0x0000 },
		{-0x0013,+0x009c,-0x02cc,+0x3f86,+0x0362,-0x00c0,+0x001a,+0x0000 },
		{-0x001a,+0x00dc,-0x03f9,+0x3eef,+0x054a,-0x012c,+0x002b,+0x0000 },
		{-0x001f,+0x0113,-0x0500,+0x3e1d,+0x0753,-0x01a0,+0x003d,+0x0000 },
		{-0x0023,+0x0141,-0x05e1,+0x3d12,+0x097c,-0x021a,+0x0050,-0x0001 },
		{-0x0026,+0x0166,-0x069e,+0x3bd0,+0x0bc4,-0x029a,+0x0066,-0x0001 },
		{-0x0027,+0x0182,-0x0738,+0x3a5a,+0x0e27,-0x031f,+0x007d,-0x0002 },
		{-0x0028,+0x0197,-0x07b0,+0x38b2,+0x10a2,-0x03a7,+0x0096,-0x0003 },
		{-0x0027,+0x01a5,-0x0807,+0x36dc,+0x1333,-0x0430,+0x00af,-0x0005 },
		{-0x0026,+0x01ab,-0x083f,+0x34db,+0x15d5,-0x04ba,+0x00ca,-0x0007 },
		{-0x0024,+0x01ac,-0x085b,+0x32b3,+0x1886,-0x0541,+0x00e5,-0x0008 },
		{-0x0022,+0x01a6,-0x085d,+0x3068,+0x1b40,-0x05c6,+0x0101,-0x000b },
		{-0x001f,+0x019c,-0x0846,+0x2dfe,+0x1e00,-0x0644,+0x011c,-0x000d },
		{-0x001c,+0x018e,-0x0819,+0x2b7a,+0x20c1,-0x06bb,+0x0136,-0x0010 },
		{-0x0019,+0x017c,-0x07d9,+0x28e1,+0x2380,-0x0727,+0x014f,-0x0013 },
		{-0x0016,+0x0167,-0x0788,+0x2637,+0x2637,-0x0788,+0x0167,-0x0016 },
		{-0x0013,+0x014f,-0x0727,+0x2380,+0x28e1,-0x07d9,+0x017c,-0x0019 },
		{-0x0010,+0x0136,-0x06bb,+0x20c1,+0x2b7a,-0x0819,+0x018e,-0x001c },
		{-0x000d,+0x011c,-0x0644,+0x1e00,+0x2dfe,-0x0846,+0x019c,-0x001f },
		{-0x000b,+0x0101,-0x05c6,+0x1b40,+0x3068,-0x085d,+0x01a6,-0x0022 },
		{-0x0008,+0x00e5,-0x0541,+0x1886,+0x32b3,-0x085b,+0x01ac,-0x0024 },
		{-0x0007,+0x00ca,-0x04ba,+0x15d5,+0x34db,-0x083f,+0x01ab,-0x0026 },
		{-0x0005,+0x00af,-0x0430,+0x1333,+0x36dc,-0x0807,+0x01a5,-0x0027 },
		{-0x0003,+0x0096,-0x03a7,+0x10a2,+0x38b2,-0x07b0,+0x0197,-0x0028 },
		{-0x0002,+0x007d,-0x031f,+0x0e27,+0x3a5a,-0x0738,+0x0182,-0x0027 },
		{-0x0001,+0x0066,-0x029a,+0x0bc4,+0x3bd0,-0x069e,+0x0166,-0x0026 },
		{-0x0001,+0x0050,-0x021a,+0x097c,+0x3d12,-0x05e1,+0x0141,-0x0023 },
		{+0x0000,+0x003d,-0x01a0,+0x0753,+0x3e1d,-0x0500,+0x0113,-0x001f },
		{+0x0000,+0x002b,-0x012c,+0x054a,+0x3eef,-0x03f9,+0x00dc,-0x001a },
		{+0x0000,+0x001a,-0x00c0,+0x0362,+0x3f86,-0x02cc,+0x009c,-0x0013 },
		{+0x0000,+0x000c,-0x005b,+0x019f,+0x3fe2,-0x0179,+0x0052,-0x000a },
	};

	uint64 resample16x2(sint16 *d, const sint16 *s, uint32 count, uint64 accum, sint64 inc) {
		do {
			const sint16 *s2 = s + (uint32)(accum >> 32)*2;
			const sint16 *f = kernel[(uint32)accum >> 27];

			accum += inc;

			uint32 l= (sint32)s2[ 0]*(sint32)f[0]
					+ (sint32)s2[ 2]*(sint32)f[1]
					+ (sint32)s2[ 4]*(sint32)f[2]
					+ (sint32)s2[ 6]*(sint32)f[3]
					+ (sint32)s2[ 8]*(sint32)f[4]
					+ (sint32)s2[10]*(sint32)f[5]
					+ (sint32)s2[12]*(sint32)f[6]
					+ (sint32)s2[14]*(sint32)f[7]
					+ 0x20002000;

			uint32 r= (sint32)s2[ 1]*(sint32)f[0]
					+ (sint32)s2[ 3]*(sint32)f[1]
					+ (sint32)s2[ 5]*(sint32)f[2]
					+ (sint32)s2[ 7]*(sint32)f[3]
					+ (sint32)s2[ 9]*(sint32)f[4]
					+ (sint32)s2[11]*(sint32)f[5]
					+ (sint32)s2[13]*(sint32)f[6]
					+ (sint32)s2[15]*(sint32)f[7]
					+ 0x20002000;

			l >>= 14;
			r >>= 14;

			if (l >= 0x10000)
				l = ~l >> 31;
			if (r >= 0x10000)
				r = ~r >> 31;

			d[0] = (sint16)(l - 0x8000);
			d[1] = (sint16)(r - 0x8000);
			d += 2;
		} while(--count);

		return accum;
	}
}

void ATCassetteEmulator::Load(const wchar_t *fn) {
	Unload();

	VDFile file;
	file.open(fn);

	uint32 basehdr;
	if (file.readData(&basehdr, 4) != 4)
		basehdr = 0;

	file.seek(0);

	uint32 baseid = VDFromLE32(basehdr);
	if (baseid == VDMAKEFOURCC('R', 'I', 'F', 'F'))
		ParseWAVE(file);
	else if (baseid == VDMAKEFOURCC('F', 'U', 'J', 'I'))
		ParseCAS(file);
	else
		throw MyError("%ls is not in a recognizable Atari cassette format.", fn);

	mPosition = 0;
	mLength = mBitstream.size();
	mAudioPosition = 0;
	mAudioLength = mAudioStream.size();
}

void ATCassetteEmulator::Unload() {
	mBitstream.clear();
	mAudioStream.clear();
	mPosition = 0;
	mLength = 0;
	mAudioPosition = 0;
	mAudioLength = 0;

	mbMotorEnable = false;
	mbPlayEnable = false;
	UpdateMotorState();
}

void ATCassetteEmulator::ParseWAVE(VDFile& file) {
	WaveFormatEx wf = {0};
	sint64 limit = file.size();
	sint64 datapos = -1;
	uint32 datalen = 0;

	for(;;) {
		uint32 hdr[2];

		if (file.tell() >= limit)
			break;

		if (8 != file.readData(hdr, 8))
			break;

		uint32 fcc = hdr[0];
		uint32 len = VDFromLE32(hdr[1]);

		switch(fcc) {
		case VDMAKEFOURCC('R', 'I', 'F', 'F'):
			limit = file.tell() + len;
			if (len < 4)
				throw MyError("'%ls' is an invalid WAV file.", file.getFilenameForError());

			file.read(hdr, 4);
			if (hdr[0] != VDMAKEFOURCC('W', 'A', 'V', 'E'))
				throw MyError("'%ls' is not a WAV file.", file.getFilenameForError());

			len = 0;
			break;

		case VDMAKEFOURCC('f', 'm', 't', ' '):
			{
				uint32 toread = std::min<uint32>(sizeof(wf), len);

				file.read(&wf, toread);
				len -= toread;

				// validate format
				if (wf.mFormatTag != kWAVE_FORMAT_PCM
					|| (wf.mBitsPerSample != 8 && wf.mBitsPerSample != 16)
					|| (wf.mChannels != 1 && wf.mChannels != 2)
					|| (wf.mBlockAlign != wf.mBitsPerSample * wf.mChannels / 8)
					|| wf.mSamplesPerSec < 8000)
				{
					throw MyError("'%ls' uses an unsupported WAV format.", file.getFilenameForError());
				}
			}
			break;

		case VDMAKEFOURCC('d', 'a', 't', 'a'):
			datapos = file.tell();
			datalen = len;
			break;
		}

		if (len)
			file.skip(len + (len & 1));
	}

	if (!wf.mBlockAlign || datapos < 0)
		throw MyError("'%ls' is not a valid WAV file.", file.getFilenameForError());

	// These are hard-coded into the 410 hardware.
	ATCassetteDecoderFSK	decoder;
//	ATCassetteDecoderDirect	decoder;

	uint64	resampAccum = 0;
	uint64	resampStep = VDRoundToInt64(wf.mSamplesPerSec / kAudioFrequency * 4294967296.0f);

	sint16	inputBuffer[512][2] = {0};
	uint32	inputBufferLevel = 3;

	sint16	outputBuffer[4096][2] = {0};
	uint32	outputBufferIdx = 0;
	uint32	outputBufferLevel = 0;

	uint32	inputSamplesLeft = datalen / wf.mBlockAlign;

	file.seek(datapos);

	bool outputBit = false;
	bool lastBit = false;
	bool dataPhase = false;
	int bitTimer = 0;
	uint8 bitCounter = 0;
	int bitAccum = 0;

	for(;;) {
		if (outputBufferIdx >= outputBufferLevel) {
			uint32 toRead = 512 - inputBufferLevel;
			if (toRead > inputSamplesLeft)
				toRead = inputSamplesLeft;

			inputSamplesLeft -= toRead;

			if (wf.mBlockAlign == 1) {
				ReadMono8(inputBuffer[inputBufferLevel], file, toRead);
			} else if (wf.mBlockAlign == 2) {
				if (wf.mChannels == 1)
					ReadMono16(inputBuffer[inputBufferLevel], file, toRead);
				else
					ReadStereo8(inputBuffer[inputBufferLevel], file, toRead);
			} else if (wf.mBlockAlign == 4) {
				ReadStereo16(inputBuffer[inputBufferLevel], file, toRead);
			}

			inputBufferLevel += toRead;

			// resampAccum + resampStep*(count - 1) < ((inputBufferLevel - 7) << 32)
			// count <= (((inputBufferLevel - 7) << 32) - resampAccum) / resampStep
			sint32 resampCount = (sint32)((sint64)(((uint64)(inputBufferLevel - 7) << 32) - resampAccum) / resampStep);

			if (!resampCount)
				break;

			resampAccum = resample16x2(outputBuffer[0], inputBuffer[0], resampCount, resampAccum, resampStep);

			uint32 shift = (uint32)(resampAccum >> 32);
			if (shift) {
				memmove(inputBuffer[0], inputBuffer[shift], (inputBufferLevel - shift)*sizeof(inputBuffer[0]));
				inputBufferLevel -= shift;
				resampAccum -= (uint64)shift << 32;
			}

			outputBufferIdx = 0;
			outputBufferLevel = resampCount;
		}

		int ix = outputBuffer[outputBufferIdx][1];

		float x = (float)ix;
		const bool outputBit = decoder.Advance(x);

		bitAccum += outputBit;

		if (++bitTimer >= kAudioSamplesPerDataBit) {
			bitTimer = 0;
			bitCounter += (bitAccum >= 4);
			bitAccum = 0;

			mBitstream.push_back(bitCounter);
		}

		mAudioStream.push_back((outputBuffer[outputBufferIdx][0] >> 8) + 0x80);

		++outputBufferIdx;
	}
}

namespace {
	class ATCassetteEncoder {
	public:
		typedef vdfastvector<uint8> Bitstream;

		ATCassetteEncoder(Bitstream& bs);

		void SetBaudRate(uint32 baud);
		void EncodeTone(bool isMark, float seconds);
		void EncodeByte(uint8 c);
		void EncodeBytes(const uint8 *p, uint32 len);

	protected:
		Bitstream& mBitstream;
		uint32	mCyclesPerBitF12;
		uint32	mBitCycleAccumF12;
		uint8 mCounter;
	};

	ATCassetteEncoder::ATCassetteEncoder(Bitstream& bs)
		: mBitstream(bs)
		, mCyclesPerBitF12(0)
		, mBitCycleAccumF12(0)
	{
		SetBaudRate(600);
	}

	void ATCassetteEncoder::SetBaudRate(uint32 baud) {
		mCyclesPerBitF12 = (uint32)((16661155 + (baud >> 1)) / baud);
	}

	void ATCassetteEncoder::EncodeTone(bool isMark, float seconds) {
		sint32 delaySamples = VDRoundToInt32(seconds * kDataFrequency);

		while(delaySamples > 0) {
			mBitstream.push_back(mCounter);
			mCounter += isMark;
			--delaySamples;
		}
	}

	void ATCassetteEncoder::EncodeByte(uint8 c) {
		EncodeBytes(&c, 1);
	}

	void ATCassetteEncoder::EncodeBytes(const uint8 *p, uint32 len) {
		while(len--) {
			// data bytes consist of a space, followed by bits from LSB to MSB, followed by a mark
			uint32 bits = ((uint32)*p++ << 1) | 0x200;

			// encode 10 bits starting from LSB
			for(uint32 i=0; i<10; ++i) {
				// compute cycle delta for this bit
				mBitCycleAccumF12 += mCyclesPerBitF12;
				uint32 bitCount = mBitCycleAccumF12 >> 12;
				mBitCycleAccumF12 &= 0xfff;

				// encode a bit
				const uint8 addend = (bits & 1);
				bits >>= 1;

				while(bitCount--) {
					mBitstream.push_back(mCounter);
					mCounter += addend;
				}
			}
		}
	}
}

void ATCassetteEmulator::ParseCAS(VDFile& file) {
	ATCassetteEncoder enc(mBitstream);
	uint32 baudRate = 0;
	uint32 currentCycle = 0;
	uint8 buf[128];

	// insert 10 second mark tone (normally 20s)
	enc.EncodeTone(true, 10.0f);

	for(;;) {
		struct {
			uint32 id;
			uint16 len;
			uint8 aux1;
			uint8 aux2;
		} hdr;

		if (file.readData(&hdr, 8) != 8)
			break;

		uint32 len = VDFromLE16(hdr.len);

		switch(hdr.id) {
			case VDMAKEFOURCC('b', 'a', 'u', 'd'):
				baudRate = hdr.aux1 + ((uint32)hdr.aux2 << 8);
				enc.SetBaudRate(baudRate);
				break;

			case VDMAKEFOURCC('d', 'a', 't', 'a'):
				// encode inter-record gap
				//VDDEBUG("Starting IRG at position %u\n", (unsigned)mBitstream.size());
				enc.EncodeTone(true, (float)(hdr.aux1 + ((uint32)hdr.aux2 << 8)) / 1000.0f);

				//VDDEBUG("Starting data at position %u\n", (unsigned)mBitstream.size());

				// encode data bytes
				bool firstBlock = true;
				while(len > 0) {
					uint32 tc = sizeof(buf);
					if (tc > len)
						tc = len;
					len -= tc;

					// Check if this is the first block and if we have an FF byte prior to
					// the sync mark. We must remove this, because otherwise the start bit
					// will foul up the Atari kernel's baud rate determination.
					int offset = 0;
					if (firstBlock && tc >= 2 && buf[0] == (uint8)0xFF && buf[1] == (uint8)0x55) {
						offset = 1;
						tc -= 1;
					}

					file.read(buf + offset, tc);
					enc.EncodeBytes(buf, tc);

					firstBlock = false;
				}
				break;
		}

		file.skip(len);
	}

	// add two second footer
	enc.EncodeTone(true, 2.0f);

	if (mbLoadDataAsAudio)
		ConvertDataToAudio();
}

void ATCassetteEmulator::SetLogDataEnable(bool enable) {
	mbLogData = enable;
}

void ATCassetteEmulator::SetLoadDataAsAudioEnable(bool enable) {
	mbLoadDataAsAudio = enable;
}

void ATCassetteEmulator::SetMotorEnable(bool enable) {
	mbMotorEnable = enable;
	UpdateMotorState();
}

void ATCassetteEmulator::Stop() {
	mbPlayEnable = false;
	UpdateMotorState();
}

void ATCassetteEmulator::Play() {
	mbPlayEnable = true;
	UpdateMotorState();
}

void ATCassetteEmulator::RewindToStart() {
	mPosition = 0;
	mAudioPosition = 0;
}

void ATCassetteEmulator::SeekToTime(float seconds) {
	if (seconds < 0.0f)
		seconds = 0.0f;

	uint32 pos = VDRoundToInt(seconds * kDataFrequency);

	SeekToBitPos(pos);
}

void ATCassetteEmulator::SeekToBitPos(uint32 bitPos) {
	if (mPosition == bitPos)
		return;

	mPosition = bitPos;

	// compute new audio position from data position
	mAudioPosition = VDRoundToInt((float)mPosition * (kAudioFrequency / kDataFrequency));

	// clamp positions and kill or recreate events as appropriate
	if (mPosition >= mLength) {
		mPosition = mLength;

		if (mpPlayEvent) {
			mpScheduler->RemoveEvent(mpPlayEvent);
			mpPlayEvent = NULL;
		}
	}

	if (mAudioPosition >= mAudioLength) {
		mAudioPosition = mAudioLength;

		if (mpAudioEvent) {
			mpScheduler->RemoveEvent(mpAudioEvent);
			mpAudioEvent = NULL;
		}
	}

	UpdateMotorState();
}

void ATCassetteEmulator::SkipForward(float seconds) {
	// compute tape offset
	sint32 bitsToSkip = VDRoundToInt(seconds * kDataFrequency);

	SeekToBitPos(mPosition + bitsToSkip);
}

uint8 ATCassetteEmulator::ReadBlock(uint16 bufadr, uint16 len, ATCPUEmulatorMemory *mpMem) {
	if (!mbPlayEnable)
		return 0x8A;	// timeout

	mbMotorEnable = false;
	UpdateMotorState();

	uint32 offset = 0;
	uint32 sum = 0;
	uint8 status = 0x01;	// complete

	uint32 syncGapTimeout = 80;
	uint32 syncGapTimeLeft = syncGapTimeout;
	uint32 syncMarkTimeout = 30;
	int syncBitsLeft = 20;
	uint32 syncStart = 0;
	float idealBaudRate = 0.0f;
	int framingErrors = 0;
	uint32 firstFramingError = 0;

	//VDDEBUG("CAS: Beginning accelerated read.\n");

	while(offset <= len) {
		if (mPosition >= mLength)
			return 0x8A;	// timeout

		BitResult r = ProcessBit();

		if (syncGapTimeLeft > 0) {
			if (!mbDataLineState)
				syncGapTimeLeft = syncGapTimeout;
			else
				--syncGapTimeLeft;

			continue;
		}

		if (syncBitsLeft > 0) {
			bool expected = (syncBitsLeft & 1) != 0;

			if (expected != mbDataLineState) {
				if (--syncMarkTimeout <= 0) {
					syncMarkTimeout = 30;

					if (syncBitsLeft < 15) {
						//VDDEBUG("CAS: Sync timeout; restarting.\n");
						syncGapTimeLeft = syncGapTimeout;
					}

					syncBitsLeft = 20;
				}
			} else {
				if (syncBitsLeft == 20) {
					syncStart = mPosition;
				}

				--syncBitsLeft;
				syncMarkTimeout = 30;

				if (syncBitsLeft == 0) {
					uint32 bitDelta = mPosition - syncStart;

					// compute baud rate divisor
					//
					// bitDelta / 19 = ticks_per_bit
					// divisor = cycles_per_bit = 440 * ticks_per_bit
					//
					// baud = bits_per_second = cycles_per_second / cycles_per_bit
					//		= 7159090 / 4 / (440 * ticks_per_bit)
					//		= 7159090 / (1760 * ticks_per_bit)
					//
					// Note that we have to halve the divisor since you're supposed to set the
					// timer such that the frequency is the intended baud rate, so that it
					// rolls over TWICE for each bit.

					PokeyChangeSerialRate(VDRoundToInt32(kClockCyclesPerDataBit / 19.0f * 0.5f * (float)bitDelta));

					mSIOPhase = 0;
					idealBaudRate = 7159090.0f / 1760.0f * 19.0f / (float)bitDelta;
					VDDEBUG("CAS: Sync mark found. Computed baud rate = %.2f baud\n", idealBaudRate);

					mpMem->WriteByte(bufadr++, 0x55);
					mpMem->WriteByte(bufadr++, 0x55);
					sum = 0x55*2;
					offset = 2;
					framingErrors = 0;
					firstFramingError = 0;
				}
			}

			continue;
		}

		if (r == kBR_NoOutput)
			continue;

		if (r == kBR_FramingError) {
			++framingErrors;
			firstFramingError = mPosition;
			continue;
		}

		VDASSERT(r == kBR_ByteReceived);

		if (offset < len) {
			if (mbLogData)
				ATConsolePrintf("CAS: Reading block[%02x] = %02x (pos = %.3fs)\n", offset, mDataByte, (float)mPosition / (float)kDataFrequency);

			mpMem->WriteByte(bufadr++, mDataByte);
			sum += mDataByte;
			++offset;
		} else {
			sum = (sum & 0xff) + ((sum >> 8) & 0xff) + ((sum >> 16) & 0xff);
			sum = (sum & 0xff) + ((sum >> 8) & 0xff);
			uint8 actualChecksum = (uint8)((sum & 0xff) + ((sum >> 8) & 0xff));
			uint8 readChecksum = mDataByte;

			mpMem->WriteByte(0x0031, readChecksum);

			if (actualChecksum != readChecksum) {
				status = 0x8F;		// checksum error

				ATConsolePrintf("CAS: Checksum error encountered (got %02x, expected %02x).\n", readChecksum, actualChecksum);
				ATConsolePrintf("CAS: Sector sync pos: %.3f s | End pos: %.3f s | Baud rate: %.2f baud | Framing errors: %d (first at %.02f)\n"
					, (float)syncStart / (float)kDataFrequency
					, (float)mPosition / (float)kDataFrequency
					, idealBaudRate
					, framingErrors
					, (float)firstFramingError / (float)kDataFrequency
					);
			}

			++offset;
		}
	}

	// resync audio position
	mAudioPosition = VDRoundToInt((float)mPosition * (kAudioFrequency / kDataFrequency));

	if (mAudioPosition >= mAudioLength) {
		mAudioPosition = mAudioLength;

		if (mpAudioEvent) {
			mpScheduler->RemoveEvent(mpAudioEvent);
			mpAudioEvent = NULL;
		}
	}

	ATConsolePrintf("CAS: Completed read with status %02x; control=%02x, position=%.2fs (cycle %u), baud=%.2fs\n", status, mpMem->ReadByte(bufadr - len + 2), mPosition / kDataFrequency, mPosition, idealBaudRate);

	// check if short inter-record gaps (IRGs) are enabled
	uint8 daux2 = mpMem->ReadByte(0x030B);
	if (daux2 < 0) {
		mbMotorEnable = true;
		UpdateMotorState();
	}

	return status;
}

void ATCassetteEmulator::OnScheduledEvent(uint32 id) {
	if (id == kATCassetteEventId_ProcessBit) {
		mpPlayEvent = NULL;

		if (kBR_ByteReceived == ProcessBit()) {
			mpPokey->ReceiveSIOByte(mDataByte);
			//VDDEBUG("Receiving byte: %02x\n", mDataByte);
		}

		if (mPosition < mLength) {
			mpPlayEvent = mpScheduler->AddEvent(kClockCyclesPerDataBit, this, kATCassetteEventId_ProcessBit);
		}
	} else if (id == kATCassetteEventId_UpdateAudio) {
		mpAudioEvent = NULL;

		if (mAudioPosition < mAudioLength) {
			mpPokey->SetAudioLine(((int)mAudioStream[mAudioPosition++] - 0x80) >> 3);

			mpAudioEvent = mpScheduler->AddEvent(kClockCyclesPerAudioSample, this, kATCassetteEventId_UpdateAudio);
		}
	}
}

void ATCassetteEmulator::PokeyChangeSerialRate(uint32 divisor) {
	mAveragingPeriod = (divisor + (kClockCyclesPerDataBit >> 1)) / kClockCyclesPerDataBit;
	if (mAveragingPeriod < 1)
		mAveragingPeriod = 1;

	mThresholdZeroBit = VDFloorToInt(mAveragingPeriod * 0.45f);
	if (mThresholdZeroBit < 1)
		mThresholdZeroBit = 1;

	mThresholdOneBit = mAveragingPeriod - mThresholdZeroBit;

	if (mDataBitHalfPeriod != divisor) {
		mDataBitHalfPeriod = divisor;

		if (mbMotorEnable) {
			VDDEBUG("Setting divisor to %d (avper = %d, thresholds = %d,%d)\n", divisor, mAveragingPeriod, mThresholdZeroBit, mThresholdOneBit);
		}
	}
}

void ATCassetteEmulator::PokeyResetSerialInput() {
	if (mbMotorEnable) {
		VDDEBUG("Reset received\n");
	}
	mSIOPhase = 0;
}

void ATCassetteEmulator::UpdateMotorState() {
	if (mbMotorEnable && mbPlayEnable) {
		if (!mpPlayEvent && mPosition < mLength)
			mpPlayEvent = mpScheduler->AddEvent(kClockCyclesPerDataBit, this, kATCassetteEventId_ProcessBit);

		if (!mpAudioEvent && mAudioPosition < mAudioLength) {
			mpAudioEvent = mpScheduler->AddEvent(kClockCyclesPerAudioSample, this, kATCassetteEventId_UpdateAudio);
		}
	} else {
		if (mpPlayEvent) {
			mpScheduler->RemoveEvent(mpPlayEvent);
			mpPlayEvent = NULL;
		}

		if (mpAudioEvent) {
			mpScheduler->RemoveEvent(mpAudioEvent);
			mpAudioEvent = NULL;
		}
	}
}

ATCassetteEmulator::BitResult ATCassetteEmulator::ProcessBit() {
	// The sync mark has to be read before the baud rate is set, so we force the averaging
	// period to ~2000 baud.
	const uint32 prevPosDirect = (mPosition < 2) ? 0 : mPosition - 2;
	const uint8 counterDirect = mBitstream[mPosition] - mBitstream[prevPosDirect];

	if (counterDirect == 0) {
		if (mbDataLineState) {
			mbDataLineState = false;
			mpPokey->SetDataLine(false);
//			VDDEBUG("[%.1f | %d] Setting data line to low\n", (float)mPosition / 6.77944f, ATSCHEDULER_GETTIME(mpScheduler));
		}
	} else if (counterDirect >= 2) {
		if (!mbDataLineState) {
			mbDataLineState = true;
			mpPokey->SetDataLine(true);
//			VDDEBUG("[%.1f | %d] Setting data line to high\n", (float)mPosition / 6.77944f, ATSCHEDULER_GETTIME(mpScheduler));
		}
	}

	const uint32 prevPos = (mPosition < mAveragingPeriod) ? 0 : mPosition - mAveragingPeriod;
	const uint8 counter = mBitstream[mPosition] - mBitstream[prevPos];
	++mPosition;

	bool dataBit = mbOutputBit;
	if (counter < mThresholdZeroBit)
		dataBit = false;
	else if (counter > mThresholdOneBit)
		dataBit = true;

	if (dataBit != mbOutputBit) {
		mbOutputBit = dataBit;

		//VDDEBUG("[%.1f] Data bit is now %d\n", (float)mPosition / 6.77944f, dataBit);

		// Alright, we've seen a transition, so this must be the start of a data bit.
		// Set ourselves up to sample.
		if (mbDataBitEdge) {
			//VDDEBUG("[%.1f] Starting data bit\n", (float)mPosition / 6.77944f);
			mbDataBitEdge = false;
		}

		mDataBitCounter = 0;
		return kBR_NoOutput;
	}

	mDataBitCounter += kClockCyclesPerDataBit;
	if (mDataBitCounter < mDataBitHalfPeriod)
		return kBR_NoOutput;

	mDataBitCounter -= mDataBitHalfPeriod;

	if (mbDataBitEdge) {
		// We were expecting the leading edge of a bit and didn't see a transition.
		// Assume there is an invisible bit boundary and set ourselves up to sample
		// the data bit one half bit period from now.
		mbDataBitEdge = false;
//		VDDEBUG("[%.1f] Starting data bit (implicit)\n", (float)mPosition / 6.77944f);
		return kBR_NoOutput;
	}

	// Set ourselves up to look for another edge transition.
	mbDataBitEdge = true;
//	VDDEBUG("[%.1f] Sampling data bit %d\n", (float)mPosition / 6.77944f, mbOutputBit);

	// Time to sample the data bit.
	//
	// We are looking for:
	//
	//     ______________________________________________
	//     |    |    |    |    |    |    |    |    |
	//     | 0  | 1  | 2  | 3  | 4  | 5  | 6  | 7  |
	// ____|____|____|____|____|____|____|____|____|
	// start                                         stop

	if (mSIOPhase == 0) {
		// Check for start bit.
		if (!mbOutputBit) {
			mSIOPhase = 1;
//			VDDEBUG("[%.1f] Start bit detected\n", (float)mPosition / 6.77944f);
		}
	} else {
		++mSIOPhase;
		if (mSIOPhase > 9) {
			mSIOPhase = 0;

			// Check for stop bit.
			if (mbOutputBit) {
				// We got a mark -- send the byte on.
				VDDEBUG("[%.1f] Stop bit detected; receiving byte %02x\n", (float)mPosition / 6.77944f, mDataByte);
				return kBR_ByteReceived;
			} else {
				// Framing error -- drop the byte.
				VDDEBUG("[%.1f] Framing error detected (baud rate = %.2f)\n", (float)mPosition / 6.77944f, 7159090.0f / 8.0f / (float)mDataBitHalfPeriod);
				return kBR_FramingError;
			}
		} else {
			mDataByte = (mDataByte >> 1) + (mbOutputBit ? 0x80 : 0x00);
		}
	}

	return kBR_NoOutput;
}

void ATCassetteEmulator::ConvertDataToAudio() {
	vdblock<uint8> phaseTable(1024);

	for(int i=0; i<1024; ++i) {
		float t = (float)i * (nsVDMath::kfTwoPi / 1024.0f);
		phaseTable[i] = VDClampedRoundFixedToUint8Fast(0.5f + 0.25f * sinf(t));
	}

	uint32 dataLength = mBitstream.size();

	if (dataLength < 2)
		return;

	const uint32 kPhaseIncZero = (uint32)(0.5 + 5327.0f / kAudioFrequency * 1024.0f);
	const uint32 kPhaseIncOne = (uint32)(0.5 + 3995.0f / kAudioFrequency * 1024.0f);

	mAudioStream.resize((dataLength - 1) * kAudioSamplesPerDataBit);
	mAudioLength = mAudioStream.size();
	mAudioPosition = 0;

	uint8 *dst = mAudioStream.data();

	uint32 phaseAccum = 0;
	for(uint32 i=1; i<dataLength; ++i) {
		const bool bit = (mBitstream[i - 1] != mBitstream[i]);
		const uint32 phaseInc = bit ? kPhaseIncOne : kPhaseIncZero;

		for(uint32 j=0; j<kAudioSamplesPerDataBit; ++j) {
			dst[j] = phaseTable[phaseAccum & 1023];
			phaseAccum += phaseInc;
		}

		dst += kAudioSamplesPerDataBit;
	}
}
