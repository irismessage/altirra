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

void ATBiquadFilter::Init(float fc) {
	const float cfc = cosf(6.28318f * fc);
	const float R = 1.0f - 3*(0.02f);
	const float K = (1.0f - 2.0f*R*cfc + R*R) / (2.0f - 2.0f*cfc);

	a0 = 1.0f - K;
	a1 = 2.0f*(K - R) * cfc;
	a2 = R*R - K;
	b1 = 2.0f*R * cfc;
	b2 = -R*R;

	Reset();
}

void ATBiquadFilter::Reset() {
	w1 = 0.0f;
	w2 = 0.0f;
}

float ATBiquadFilter::Advance(float x) {
	float w0 = x + b1*w1 + b2*w2;
	float y = a0*w0 + a1*w1 + a2*w2;

	w2 = w1;
	w1 = w0;

	return y;
}

///////////////////////////////////////////////////////////////////////////////

ATCassetteEmulator::ATCassetteEmulator()
	: mpPlayEvent(NULL)
	, mpAudioEvent(NULL)
{
	mAudioPosition = 0;
	mAudioLength = 0;
	mPosition = 0;
	mLength = 0;
	mTargetCycle = 0;
}

ATCassetteEmulator::~ATCassetteEmulator() {
}

float ATCassetteEmulator::GetPosition() const {
	uint32 cyc = mTargetCycle;

	if (mpPlayEvent)
		cyc -= mpScheduler->GetTicksToEvent(mpPlayEvent);

	return cyc / 1789773.0f;
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
	mSIOPhase = 0;
	mDataByte = 0;

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
				dst[0] = (buf[i] << 8) - 0x8000;
				dst[1] = 0;
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
				dst[0] = buf[i];
				dst[1] = 0;
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
	mBitstream.clear();
	mAudioStream.clear();

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
	ATBiquadFilter	filter0;
	ATBiquadFilter	filter1;
	filter0.Init(3995.0f / 44744.3f);	// We resample to 1.7MHz / 40 = 44.7KHz.
	filter1.Init(5327.0f / 44744.3f);

	uint64	resampAccum = 0;
	uint64	resampStep = VDRoundToInt64(wf.mSamplesPerSec / 44744.3f * 4294967296.0f);

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
	int slewAccum = 0;
	float deltaAccum = 0;
	uint32 clockCycle = 0;
	uint32 lastClockCycle = 0;
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

		int ix = outputBuffer[outputBufferIdx][0];
		++outputBufferIdx;

		float x = (float)ix;
		float r0 = filter0.Advance(x);
		float r1 = filter1.Advance(x);
		float dresult = (r1 + fabsf(r1)) - (r0 + fabsf(r0));

		deltaAccum += (dresult - deltaAccum) * 0.1f;

		--slewAccum;
		if (deltaAccum > 0)
			slewAccum += 2;

		if (slewAccum < -25)
			slewAccum = -25;
		else if (slewAccum > 25)
			slewAccum = 25;

		if (slewAccum < -10)
			outputBit = false;
		else if (slewAccum > 10)
			outputBit = true;

		if (lastBit != outputBit) {
			lastBit = outputBit;
			bitTimer = 37;
			dataPhase = false;
		}

		if (!--bitTimer) {
			bitTimer = 37;
			dataPhase = !dataPhase;

			if (dataPhase) {
				mBitstream.push_back((clockCycle - lastClockCycle)*2 + outputBit);
				lastClockCycle = clockCycle;
			}
		}

		clockCycle += 40;

		mAudioStream.push_back((outputBuffer[outputBufferIdx][1] >> 8) + 0x80);
	}
}

namespace {
	class ATCassetteEncoder {
	public:
		typedef vdfastvector<uint32> Bitstream;

		ATCassetteEncoder(Bitstream& bs);

		void SetBaudRate(uint32 baud);
		void EncodeTone(bool isMark, float seconds);
		void EncodeByte(uint8 c);
		void EncodeBytes(const uint8 *p, uint32 len);

	protected:
		Bitstream& mBitstream;
		uint32	mCurrentCycle;
		uint32	mLastCycle;
		uint32	mCyclesPerBitF8;
		uint32	mBitCycleAccumF8;
	};

	ATCassetteEncoder::ATCassetteEncoder(Bitstream& bs)
		: mBitstream(bs)
		, mCurrentCycle(1)
		, mLastCycle(0)
		, mCyclesPerBitF8(0)
		, mBitCycleAccumF8(0)
	{
		SetBaudRate(600);
	}

	void ATCassetteEncoder::SetBaudRate(uint32 baud) {
		mCyclesPerBitF8 = (uint32)((458181760 + (baud >> 1)) / baud);
	}

	void ATCassetteEncoder::EncodeTone(bool isMark, float seconds) {
		sint32 delayTime = (sint32)(0.5f + seconds * 1789772.5f);

		while(delayTime > 0) {
			// compute cycle delta for this bit
			mBitCycleAccumF8 += mCyclesPerBitF8;
			uint32 cycleDelta = mBitCycleAccumF8 >> 8;
			mBitCycleAccumF8 &= 0xff;

			// encode a bit
			mBitstream.push_back((mCurrentCycle - mLastCycle) * 2 + (isMark ? 1 : 0));
			mLastCycle = mCurrentCycle;
			mCurrentCycle += cycleDelta;
			delayTime -= cycleDelta;
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
				mBitCycleAccumF8 += mCyclesPerBitF8;
				uint32 cycleDelta = mBitCycleAccumF8 >> 8;
				mBitCycleAccumF8 &= 0xff;

				// encode a bit
				mBitstream.push_back((mCurrentCycle - mLastCycle) * 2 + (bits & 1));
				bits >>= 1;
				mLastCycle = mCurrentCycle;
				mCurrentCycle += cycleDelta;
			}
		}
	}
}

void ATCassetteEmulator::ParseCAS(VDFile& file) {
	ATCassetteEncoder enc(mBitstream);
	uint32 baudRate = 0;
	uint32 currentCycle = 0;
	uint8 buf[128];

	// insert 2 second mark tone (normally 20s)
	enc.EncodeTone(true, 2.0f);

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
				break;

			case VDMAKEFOURCC('d', 'a', 't', 'a'):
				// encode inter-record gap
				enc.EncodeTone(true, (float)(hdr.aux1 + ((uint32)hdr.aux2 << 8)) / 1000.0f);

				// encode data bytes
				while(len > 0) {
					uint32 tc = sizeof(buf);
					if (tc > len)
						tc = len;
					len -= tc;

					file.read(buf, tc);
					enc.EncodeBytes(buf, tc);
				}
				break;
		}

		file.skip(len);
	}
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
	mTargetCycle = 0;
	mAudioPosition = 0;
}

uint8 ATCassetteEmulator::ReadBlock(uint16 bufadr, uint16 len, ATCPUEmulatorMemory *mpMem) {
	if (!mbPlayEnable)
		return 0x8A;	// timeout

	mbMotorEnable = false;
	UpdateMotorState();

	uint32 offset = 0;
	uint32 sum = 0;
	uint8 status = 0x43;	// complete

	if (mPosition >= mLength)
		return 0x8A;	// timeout

	uint32 irgDelay = 0;
	uint32 syncMarkTimeout = 0;

	while(offset <= len) {
		mbOutputBit = mBitstream[mPosition++] & 1;
		mpPokey->SetAudioLine(mbOutputBit ? -32 : 32);
		mpPokey->SetDataLine(mbOutputBit);

		if (irgDelay) {
			--irgDelay;
		} else if (mSIOPhase == 0) {
			if (!mbOutputBit)
				mSIOPhase = 1;
			else if (offset == 1 && ++syncMarkTimeout >= 50) {
				offset = 0;
				syncMarkTimeout = 0;
			}
		} else {
			++mSIOPhase;
			if (mSIOPhase > 9) {
				mSIOPhase = 0;

				if (!mbOutputBit) {
					// framing error
					if (offset < 2) {
						ATConsolePrintf("CAS: Framing error detected during sync -- restarting.\n");
						offset = 0;
					}
				} else if (offset < 2) {
					ATConsolePrintf("CAS: Receiving sync mark[%d] = %02x\n", offset, mDataByte);
					if (mDataByte == 0x55) {
						++offset;
						if (offset == 2) {
							sum += 0x55 * 2;
							mpMem->WriteByte(bufadr++, 0x55);
							mpMem->WriteByte(bufadr++, 0x55);
						}
					} else
						mSIOPhase = 0;
				} else if (offset < len) {
					ATConsolePrintf("CAS: Reading block[%02x] = %02x\n", offset, mDataByte);

					mpMem->WriteByte(bufadr++, mDataByte);
					sum += mDataByte;
					++offset;
				} else {
					sum = (sum & 0xff) + ((sum >> 8) & 0xff) + ((sum >> 16) & 0xff);
					uint8 actualChecksum = (uint8)((sum & 0xff) + ((sum >> 8) & 0xff));
					uint8 readChecksum = mDataByte;

					mpMem->WriteByte(0x0031, readChecksum);

					ATConsolePrintf("CAS: Reading checksum %02x (expected %02x)\n", readChecksum, actualChecksum);

					if (actualChecksum != readChecksum)
						status = 0x8F;		// checksum error

					++offset;
				}
			} else
				mDataByte = (mDataByte >> 1) + (mbOutputBit ? 0x80 : 0x00);
		}

		if (mPosition >= mLength)
			return 0x8A;	// timeout

		uint32 nextPos = mBitstream[mPosition] >> 1;
		mTargetCycle += nextPos;
	}

	ATConsolePrintf("CAS: Completed read with status %02x; control=%02x, position=%.2fs (cycle %u)\n", status, mpMem->ReadByte(bufadr - len + 2), mTargetCycle / 1789772.5f, mTargetCycle);

	// check if short inter-record gaps (IRGs) are enabled
	uint8 daux2 = mpMem->ReadByte(0x030B);
	if (daux2 < 0) {
		mbMotorEnable = true;
		UpdateMotorState();
	}

	return status;
}

void ATCassetteEmulator::OnScheduledEvent(uint32 id) {
	if (id == 0) {
		mpPlayEvent = NULL;

		mbOutputBit = mBitstream[mPosition++] & 1;
//		mpPokey->SetAudioLine(mbOutputBit ? -32 : 32);
		mpPokey->SetDataLine(mbOutputBit);

		if (mSIOPhase == 0) {
			if (!mbOutputBit)
				mSIOPhase = 1;
		} else {
			++mSIOPhase;
			if (mSIOPhase > 9 && mbOutputBit) {
				mSIOPhase = 0;
				mpPokey->ReceiveSIOByte(mDataByte);
			} else
				mDataByte = (mDataByte >> 1) + (mbOutputBit ? 0x80 : 0x00);
		}

		if (mPosition < mLength) {
			uint32 nextPos = mBitstream[mPosition] >> 1;
			mTargetCycle += nextPos;
			mpPlayEvent = mpScheduler->AddEvent(nextPos, this, 0);
		}
	} else if (id == 1) {
		mpAudioEvent = NULL;

		if (mAudioPosition < mAudioLength) {
			mpPokey->SetAudioLine(((int)mAudioStream[mAudioPosition++] - 0x80) >> 3);

			mpAudioEvent = mpScheduler->AddEvent(40, this, 1);
		}
	}
}

void ATCassetteEmulator::UpdateMotorState() {
	if (mbMotorEnable && mbPlayEnable) {
		if (!mpPlayEvent && mPosition < mLength) {
			uint32 nextPos = mBitstream[mPosition] >> 1;
			mTargetCycle += nextPos;
			mpPlayEvent = mpScheduler->AddEvent(nextPos, this, 0);
		}

		if (!mpAudioEvent && mAudioPosition < mAudioLength) {
			mpAudioEvent = mpScheduler->AddEvent(40, this, 1);
		}
	} else {
		if (mpPlayEvent) {
			mTargetCycle -= mpScheduler->GetTicksToEvent(mpPlayEvent);
			mpScheduler->RemoveEvent(mpPlayEvent);
			mpPlayEvent = NULL;
		}

		if (mpAudioEvent) {
			mpScheduler->RemoveEvent(mpAudioEvent);
			mpAudioEvent = NULL;
		}
	}
}
