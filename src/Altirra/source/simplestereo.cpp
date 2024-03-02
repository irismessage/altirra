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

#include "stdafx.h"
#include <vd2/system/binary.h>
#include "simplestereo.h"
#include "scheduler.h"
#include "audiooutput.h"
#include "memorymanager.h"

ATSimpleStereoEmulator::ATSimpleStereoEmulator()
	: mpMemLayerControl(NULL)
	, mpScheduler(NULL)
	, mpMemMan(NULL)
	, mpAudioOut(NULL)
	, mMemBase(0xD2C0)
{
	// Initialize noise LFSR.
	uint32 lfsr = 0x7FFFF8;
	for(uint32 i=0; i<((1<<23) - 1); ++i) {
		lfsr = lfsr + lfsr + (((lfsr >> 17) ^ (lfsr >> 22)) & 1);
		mLFSR[i] = (uint8)(lfsr >> 8);
	}
}

ATSimpleStereoEmulator::~ATSimpleStereoEmulator() {
	Shutdown();
}

void ATSimpleStereoEmulator::SetMemBase(uint32 membase) {
	if (mMemBase == membase)
		return;

	mMemBase = membase;

	if (mpMemMan)
		UpdateControlLayer();
}

void ATSimpleStereoEmulator::Init(ATMemoryManager *memMan, ATScheduler *sch, IATAudioOutput *audioOut) {
	mpMemMan = memMan;
	mpScheduler = sch;
	mpAudioOut = audioOut;

	audioOut->AddSyncAudioSource(this);

	ColdReset();
}

void ATSimpleStereoEmulator::Shutdown() {
	if (mpMemMan) {
		if (mpMemLayerControl) {
			mpMemMan->DeleteLayer(mpMemLayerControl);
			mpMemLayerControl = NULL;
		}

		mpMemMan = NULL;
	}

	if (mpAudioOut) {
		mpAudioOut->RemoveSyncAudioSource(this);
		mpAudioOut = NULL;
	}
}

void ATSimpleStereoEmulator::ColdReset() {
	mLoadAddress = 0;
	mpCurChan = &mChannels[0];

	mAccumLevel = 0;
	mAccumOffset = 0;
	mGeneratedCycles = 0;
	mLastUpdate = ATSCHEDULER_GETTIME(mpScheduler);
	mCycleAccum = 0;

	UpdateControlLayer();

	WarmReset();
}

void ATSimpleStereoEmulator::WarmReset() {
	memset(mChannels, 0, sizeof mChannels);

	memset(mAccumBufferLeft, 0, sizeof mAccumBufferLeft);
	memset(mAccumBufferRight, 0, sizeof mAccumBufferRight);

	mLFSROffset = 0;
}

void ATSimpleStereoEmulator::WriteControl(uint8 addr, uint8 value) {
	addr ^= 0x10;

	if (addr < 0x10 || addr == 0x14)
		Flush();

	switch(addr) {
		case 0x00:	// waveform
			mpCurChan->mControl = value;

			if (!(value & 0x80))
				mpCurChan->mWaveformMode = kWaveformMode_Sample;
			else switch(value & 0x0f) {
				case 0x00:
					mpCurChan->mWaveformMode = kWaveformMode_Pulse;
					break;
				case 0x01:
					mpCurChan->mWaveformMode = kWaveformMode_Sawtooth;
					break;
				case 0x02:
					mpCurChan->mWaveformMode = kWaveformMode_Triangle;
					break;
				case 0x03:
					mpCurChan->mWaveformMode = kWaveformMode_Noise;
					break;
				default:
					mpCurChan->mWaveformMode = kWaveformMode_Disabled;
					break;
			}
			break;

		case 0x01:	// freq low
			mpCurChan->mFreq = (mpCurChan->mFreq & 0xff00) + value;
			break;

		case 0x02:	// freq high
			mpCurChan->mFreq = (mpCurChan->mFreq & 0x00ff) + ((uint32)value << 8);
			break;

		case 0x03:	// length
			mpCurChan->mLength = value;
			break;

		case 0x04:	// repeat
			mpCurChan->mRepeat = value;
			break;

		case 0x05:	// attack/decay
			mpCurChan->mAttack = value & 15;
			mpCurChan->mDecay = value >> 4;
			break;

		case 0x06:	// sustain/release
			mpCurChan->mSustain = value & 15;
			mpCurChan->mRelease = value >> 4;
			break;

		case 0x07:	// volume
			mpCurChan->mVolume = value & 31;
			break;

		case 0x08:	// pan
			mpCurChan->mPan = value & 31;
			break;

		case 0x10:	// load byte
			mMemory[mLoadAddress] = value;
			break;

		case 0x11:	// load address low
			mLoadAddress = (mLoadAddress & 0x0f00) + value;
			break;

		case 0x12:	// load address high
			mLoadAddress = (mLoadAddress & 0x00ff) + ((uint32)(value & 0x0f) << 8);
			break;

		case 0x13:	// channel select
			mpCurChan = &mChannels[value & 15];
			break;

		case 0x14:	// reset
			WarmReset();
			break;
	}
}

void ATSimpleStereoEmulator::Run(uint32 cycles) {
	// SimpleStereo runs on an 88.63MHz clock with 83 states (10 states per channel + 3 global states),
	// which gives a sample rate of 1.0678MHz. 
	cycles += mCycleAccum;

	while(cycles) {
		// compute samples we can generate based on time
		uint32 samplesToGenerate = cycles / 2;

		// don't generate more than a sample buffer at a time
		if (samplesToGenerate > kSampleBufferSize)
			samplesToGenerate = kSampleBufferSize;

		// if we can't generate anything, we're done
		if (!samplesToGenerate)
			break;

		// subtract cycles from budget
		cycles -= samplesToGenerate * 2;

		// determine how many samples we can accumulate at 1/28 rate
		mGeneratedCycles += samplesToGenerate * 2;

		const uint32 samplesToAccum = mGeneratedCycles / 28;

		mGeneratedCycles -= samplesToAccum * 28;

		VDASSERT(mAccumLevel + samplesToAccum <= kAccumBufferSize);

		const uint32 samplesEatenByAccum = 14 * samplesToAccum;

		for(int chidx = 0; chidx < 8; ++chidx) {
			Channel *__restrict ch = &mChannels[chidx];
			const uint32 vol = ch->mVolume;

			// Internal stats:
			//
			// - Phase accumulator is 24-bit.
			// - Frequency is 16-bit, right-normalized to bits 0-15.
			// - Length is 8-bit, left-normalized to bits 16-23.
			// - Repeat position is 8-bit, left-normalized to bits 16-23.

			uint32 phase = ch->mPhase;
			const uint32 freq = ch->mFreq << 8;
			const uint32 length = ch->mLength << 24;
			const uint32 repeat = ch->mRepeat << 24;

			if (!vol /*|| !(ch->mControl & 0x40)*/) {
#if 0
				// If the channel has DMA enabled but is shut off, we still have to update
				// the phase.
				if (ch->mControl & 0x40) {
					// this is pretty lame, but it correctly handles the lost phase on
					// the repeat
					if (ch->mWaveformMode == kWaveformMode_Sample && length != 0) {
						for(uint32 i = 0; i < samplesToGenerate; ++i) {
							phase += freq;

							if (phase >= length)
								phase = repeat;
						}
					} else {
						phase += freq * samplesToGenerate;
					}

					ch->mPhase = phase;
				}
#endif

				// Shift the overlap buffer.
				if (samplesEatenByAccum) {
					if (samplesEatenByAccum >= kSampleBufferOverlap)
						memset(ch->mOverlapBuffer, 0, sizeof ch->mOverlapBuffer);
					else {
						memmove(ch->mOverlapBuffer, ch->mOverlapBuffer + samplesEatenByAccum, sizeof(ch->mOverlapBuffer[0]) * (kSampleBufferOverlap - samplesEatenByAccum));
						memset(ch->mOverlapBuffer + (kSampleBufferOverlap - samplesEatenByAccum), 0, sizeof(ch->mOverlapBuffer[0]) * samplesEatenByAccum);
					}
				}

				continue;
			}

			const uint32 panleft = 31 - ch->mPan;
			const uint32 panright = ch->mPan;
			const uint32 baseAddr = ch->mAddress;
			sint16 *sdst = mSampleBuffer;
			uint8 *const mem = mMemory;
			uint32 lfsrOffset = (mLFSROffset + 10 * chidx) % 0x7FFFFF;

			memcpy(sdst, ch->mOverlapBuffer, kSampleBufferOverlap*sizeof(sdst[0]));
			sdst += kSampleBufferOverlap;

			if (ch->mControl & 0x20) {	// enabled
				switch(ch->mWaveformMode) {
					case kWaveformMode_Pulse:
						{
							const uint32 pulseThreshold = (ch->mLength << 24) + 0xFFFFFF;

							for(uint32 i = 0; i < samplesToGenerate; ++i) {
								const uint8 sample = phase > pulseThreshold ? (uint8)0xFF : (uint8)0x00;
								phase += freq;

								*sdst++ = (sint16)((uint32)sample - 0x80);
							}
						}
						break;

					case kWaveformMode_Sawtooth:
						for(uint32 i = 0; i < samplesToGenerate; ++i) {
							const uint8 sample = (uint8)(phase >> 24);
							phase += freq;

							*sdst++ = (sint16)((uint32)sample - 0x80);
						}
						break;

					case kWaveformMode_Triangle:
						for(uint32 i = 0; i < samplesToGenerate; ++i) {
							const uint8 sample = (uint8)(((phase >> 23) ^ (uint32)((sint32)phase >> 31)) & 0xfe);
							phase += freq;

							*sdst++ = (sint16)((uint32)sample - 0x80);
						}
						break;

					case kWaveformMode_Disabled:
						for(uint32 i = 0; i < samplesToGenerate; ++i) {
							const uint8 sample = 0;
							phase += freq;

							*sdst++ = (sint16)((uint32)sample - 0x80);
						}

						phase += freq * samplesToGenerate;
						break;

					case kWaveformMode_Noise:
						for(uint32 i = 0; i < samplesToGenerate; ++i) {
							uint32 phase2 = phase + freq;

							if ((sint32)(phase ^ phase2) < 0)
								ch->mLFSRLatch = mLFSR[lfsrOffset % 0x7FFFFF];

							phase = phase2;

							lfsrOffset += 83;

							const uint8 sample = ch->mLFSRLatch;
							*sdst++ = (sint16)((uint32)sample - 0x80);
						}
						break;

					case kWaveformMode_Sample:
						for(uint32 i = 0; i < samplesToGenerate; ++i) {
							const uint8 sample = mem[(baseAddr + (phase >> 16)) & 0x7ffff];

							if (phase < length)
								phase += freq;
							else
								phase = repeat;

							*sdst++ = (sint16)((uint32)sample - 0x80);
						}
						break;
				}
			} else {
				switch(ch->mWaveformMode) {
					case kWaveformMode_Pulse:
					case kWaveformMode_Sawtooth:
					case kWaveformMode_Triangle:
					case kWaveformMode_Disabled:
						for(uint32 i = 0; i < samplesToGenerate; ++i) {
							const uint8 sample = 0;
							phase += freq;

							*sdst++ = (sint16)((uint32)sample - 0x80);
						}

						phase += freq * samplesToGenerate;
						break;

					case kWaveformMode_Noise:
						for(uint32 i = 0; i < samplesToGenerate; ++i) {
							uint32 phase2 = phase + freq;

							if ((sint32)(phase ^ phase2) < 0)
								ch->mLFSRLatch = mLFSR[lfsrOffset % 0x7FFFFF];

							lfsrOffset += 83;

							const uint8 sample = 0;
							*sdst++ = (sint16)((uint32)sample - 0x80);
						}
						break;

					case kWaveformMode_Sample:
						for(uint32 i = 0; i < samplesToGenerate; ++i) {
							const uint8 sample = mem[baseAddr];
							phase += freq;

							if (phase < length)
								phase += freq;
							else
								phase = repeat;

							*sdst++ = (sint16)((uint32)sample - 0x80);
						}
						break;
				}
			}

			ch->mPhase = phase;

			const sint16 *ssrc = mSampleBuffer;
			float *dstLeft = mAccumBufferLeft + mAccumLevel;
			float *dstRight = mAccumBufferRight + mAccumLevel;
			float volf = (float)vol * (1.0f / 255.0f / 31.0f);
			float volPanLeft = volf * (float)panleft;
			float volPanRight = volf * (float)panright;

			uint32 accumCounter = samplesToAccum;

			if (accumCounter) {
				// 6 6 6 6 4
				//         2 6 6 6 6 2
				//                   4 6 6 6 6
				//
				// We consume 14 samples in 84 cycles, or one sample every 6 cycles.
				do {
					float sample;

					sample = (float)ssrc[0]
						+ (float)ssrc[1]
						+ (float)ssrc[2]
						+ (float)ssrc[3]
						+ (float)ssrc[4]
						+ (float)ssrc[5]
						+ (float)ssrc[6]
						+ (float)ssrc[7]
						+ (float)ssrc[8]
						+ (float)ssrc[9]
						+ (float)ssrc[10]
						+ (float)ssrc[11]
						+ (float)ssrc[12]
						+ (float)ssrc[13]
						;

					*dstLeft++ += sample * volPanLeft;
					*dstRight++ += sample * volPanRight;
					ssrc += 14;

				} while(--accumCounter);
			}

			memcpy(ch->mOverlapBuffer, sdst - kSampleBufferOverlap, sizeof ch->mOverlapBuffer);
		}

		// update LFSR offset
		mLFSROffset = (mLFSROffset + samplesToGenerate) % 0x7FFFFF;

		// update accumulator phase and offset
		mAccumOffset += samplesToGenerate - samplesEatenByAccum;

		VDASSERT(mAccumOffset < kSampleBufferOverlap);

		// update accumulator level
		mAccumLevel += samplesToAccum;
		VDASSERT(mAccumLevel <= kAccumBufferSize);
	}

	mCycleAccum = cycles;
}

void ATSimpleStereoEmulator::WriteAudio(uint32 startTime, float *dstLeft, float *dstRightOpt, uint32 n) {
	Flush();

	VDASSERT(n <= kAccumBufferSize);

	// if we don't have enough samples, pad out; eventually we'll catch up enough
	if (mAccumLevel < n) {
		memset(mAccumBufferLeft + mAccumLevel, 0, sizeof(mAccumBufferLeft[0]) * (n - mAccumLevel));
		memset(mAccumBufferRight + mAccumLevel, 0, sizeof(mAccumBufferRight[0]) * (n - mAccumLevel));

		mAccumLevel = n;
	}

	// add output to output buffers
	if (dstRightOpt) {
		for(uint32 i=0; i<n; ++i) {
			dstLeft[i] += mAccumBufferLeft[i];
			dstRightOpt[i] += mAccumBufferRight[i];
		}
	} else {
		for(uint32 i=0; i<n; ++i) {
			dstLeft[i] += mAccumBufferLeft[i] + mAccumBufferRight[i];
		}
	}

	// shift down accumulation buffers
	uint32 samplesLeft = mAccumLevel - n;

	if (samplesLeft) {
		memmove(mAccumBufferLeft, mAccumBufferLeft + n, samplesLeft * sizeof(mAccumBufferLeft[0]));
		memmove(mAccumBufferRight, mAccumBufferRight + n, samplesLeft * sizeof(mAccumBufferRight[0]));
	}

	memset(mAccumBufferLeft + samplesLeft, 0, sizeof(mAccumBufferLeft[0]) * n);
	memset(mAccumBufferRight + samplesLeft, 0, sizeof(mAccumBufferRight[0]) * n);

	mAccumLevel = samplesLeft;
}

void ATSimpleStereoEmulator::Flush() {
	uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
	uint32 dt = t - mLastUpdate;
	mLastUpdate = t;

	Run(dt);
}

void ATSimpleStereoEmulator::UpdateControlLayer() {
	if (mpMemLayerControl) {
		mpMemMan->DeleteLayer(mpMemLayerControl);
		mpMemLayerControl = NULL;
	}

	if (!mMemBase)
		return;

	ATMemoryHandlerTable handlers = {};
	handlers.mpThis = this;

	switch(mMemBase) {
		case 0xD280:
		default:
			handlers.mbPassAnticReads = true;
			handlers.mbPassReads = true;
			handlers.mbPassWrites = true;
			handlers.mpDebugReadHandler = NULL;
			handlers.mpReadHandler = NULL;
			handlers.mpWriteHandler = StaticWriteD2xxControl;
			mpMemLayerControl = mpMemMan->CreateLayer(kATMemoryPri_HardwareOverlay, handlers, 0xD2, 0x01);
			break;

		case 0xD500:
			handlers.mbPassAnticReads = false;
			handlers.mbPassReads = true;
			handlers.mbPassWrites = true;
			handlers.mpDebugReadHandler = NULL;
			handlers.mpReadHandler = NULL;
			handlers.mpWriteHandler = StaticWriteD5xxControl;
			mpMemLayerControl = mpMemMan->CreateLayer(kATMemoryPri_HardwareOverlay, handlers, 0xD5, 0x01);
			break;

		case 0xD600:
			handlers.mbPassAnticReads = false;
			handlers.mbPassReads = true;
			handlers.mbPassWrites = true;
			handlers.mpDebugReadHandler = NULL;
			handlers.mpReadHandler = NULL;
			handlers.mpWriteHandler = StaticWriteD5xxControl;
			mpMemLayerControl = mpMemMan->CreateLayer(kATMemoryPri_HardwareOverlay, handlers, 0xD6, 0x01);
			break;
	}

	mpMemMan->EnableLayer(mpMemLayerControl, kATMemoryAccessMode_CPUWrite, true);
}

bool ATSimpleStereoEmulator::StaticWriteD2xxControl(void *thisptr, uint32 addr, uint8 value) {
	uint8 addr8 = (uint8)addr;
	if (addr8 < 0x80 || addr8 >= 0xC0)
		return false;

	((ATSimpleStereoEmulator *)thisptr)->WriteControl(addr8 & 0x3f, value);
	return true;
}

bool ATSimpleStereoEmulator::StaticWriteD5xxControl(void *thisptr, uint32 addr, uint8 value) {
	uint8 addr8 = (uint8)addr;
	if (addr8 >= 0x40)
		return false;

	((ATSimpleStereoEmulator *)thisptr)->WriteControl(addr8, value);
	return true;
}
