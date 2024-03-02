//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2023 Avery Lee
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

#include <stdafx.h>
#include <vd2/system/constexpr.h>
#include <at/atemulation/phone.h>

ATPhonePulseDialDetector::ATPhonePulseDialDetector() {
}

ATPhonePulseDialDetector::~ATPhonePulseDialDetector() {
	Shutdown();
}

void ATPhonePulseDialDetector::Init(ATScheduler *sch) {
	mpScheduler = sch;
}

void ATPhonePulseDialDetector::Shutdown() {
	Reset();

	mpScheduler = nullptr;
}

void ATPhonePulseDialDetector::Reset() {
	mDigitCount = 0;
	mPulseCount = 0;
	mbInvalidNumber = false;
	mLastTransitionTime = 0;

	if (mpEventTimeout)
		mpScheduler->UnsetEvent(mpEventTimeout);
}

void ATPhonePulseDialDetector::OnHook() {
	if (!mbOffHook)
		return;

	mbOffHook = false;

	const uint64 t = mpScheduler->GetTick64();
	const float delay = t - std::exchange(mLastTransitionTime, t);

	if (mpEventTimeout)
		mpScheduler->UnsetEvent(mpEventTimeout);

	// The phone is going on-hook, which means the leading edge of a pulse. If
	// the min interdigit time has passed, then start a new digit. The max
	// interdigit time isn't checked here as that is done by event to trigger
	// dialing.

	if (delay > ATSecondsToDefaultCycles(ATPhoneConstants::kMinPulseInterdigitTime))
		EndDigit();
}

void ATPhonePulseDialDetector::OffHook() {
	if (mbOffHook)
		return;

	mbOffHook = true;

	const uint64 t = mpScheduler->GetTick64();
	const float delay = t - std::exchange(mLastTransitionTime, t);

	// If the phone has been on-hook for too long, treat it as a hang-up
	// instead of a pulse and clear the accumulated number.
	if (delay > ATSecondsToDefaultCycles(ATPhoneConstants::kMaxPulseTime)) {
		Reset();
		return;
	}

	// Otherwise, the phone is going off-hook as the trailing edge of a pulse.
	// Increment the pulse counter for the current digit, and arm the timeout
	// timer to check for dial timeout. The interdigit timeout is shorter and
	// is checked at on-hook.

	if (mPulseCount < 255)
		++mPulseCount;

	mpScheduler->SetEvent(ATSecondsToDefaultCycles(ATPhoneConstants::kMaxPulseInterdigitTime), this, 1, mpEventTimeout);
}

bool ATPhonePulseDialDetector::IsDialing() const {
	return mPulseCount > 0 || mDigitCount > 0;
}

void ATPhonePulseDialDetector::SetOnEvent(vdfunction<void(EventType, const char *)> fn) {
	mpOnEvent = std::move(fn);
}

void ATPhonePulseDialDetector::OnScheduledEvent(uint32 id) {
	mpEventTimeout = nullptr;

	EndDigit();

	auto digitCount = mDigitCount;
	auto invalid = mbInvalidNumber;
	Reset();

	if (digitCount && mpOnEvent) {
		if (invalid)
			mpOnEvent(EventType::InvalidDial, nullptr);
		else
			mpOnEvent(EventType::ValidDial, mDialNumber);
	}
}

void ATPhonePulseDialDetector::EndDigit() {
	if (!mPulseCount)
		return;

	if (mpOnEvent) {
		if (mPulseCount > 10) {
			mpOnEvent(EventType::InvalidDigit, nullptr);
			mbInvalidNumber = true;
		} else {
			const char ch[2] { (char)('0' + mPulseCount % 10), 0 };

			mpOnEvent(EventType::ValidDigit, ch);
		}
	}

	if (!mbInvalidNumber) {
		if (mDigitCount < kMaxDigits) {
			mDialNumber[mDigitCount++] = (char)('0' + mPulseCount % 10);
			mDialNumber[mDigitCount] = 0;
		} else {
			mbInvalidNumber = true;
		}
	}

	mPulseCount = 0;
}

////////////////////////////////////////////////////////////////////////////////

ATPhoneToneDialDetector::ATPhoneToneDialDetector() {
}

ATPhoneToneDialDetector::~ATPhoneToneDialDetector() {
	Shutdown();
}

void ATPhoneToneDialDetector::Init(ATScheduler *sch, IATAudioMixer *mixer) {
	mpSlowScheduler = sch;
	mpMixer = mixer;
}

void ATPhoneToneDialDetector::Shutdown() {
	SetEnabled(false);

	mpMixer = nullptr;

	if (mpSlowScheduler) {
		mpSlowScheduler->UnsetEvent(mpEventTimeout);
		mpSlowScheduler = nullptr;
	}
}

void ATPhoneToneDialDetector::Reset() {
	mDigitCount = 0;
	mbInvalidNumber = false;
	mbDialing = false;
	mLastTone = -1;

	mpSlowScheduler->UnsetEvent(mpEventTimeout);
}

void ATPhoneToneDialDetector::SetEnabled(bool enable) {
	if (mbEnabled != enable) {
		mbEnabled = enable;

		// we must not reset the detector here, as it is completely valid for
		// the interdigit timeout to occur after the computer has shut off its
		// audio tap

		if (enable)
			mpMixer->AddInternalAudioTap(this);
		else
			mpMixer->RemoveInternalAudioTap(this);
	}
}

void ATPhoneToneDialDetector::SetOnEvent(vdfunction<void(EventType, const char *)> fn) {
	mpOnEvent = std::move(fn);
}

void ATPhoneToneDialDetector::OnScheduledEvent(uint32) {
	mpEventTimeout = nullptr;

	// this may already be true if we ended immediately on #, but set it anyway
	mbDialing = true;

	if (mpOnEvent)
		mpOnEvent(mbInvalidNumber ? EventType::InvalidDial : EventType::ValidDial, mDialNumber);
}

void ATPhoneToneDialDetector::WriteInternalAudio(const float *samples, uint32 count, uint32 timestamp) {
	// If we have started dialing, we are done -- no need to process any more audio
	if (mbDialing)
		return;

	while(count) {
		uint32 tc = std::min<uint32>(count, kInputBlockSize - mSampleCount);

		memcpy(&mInputBuffer[mSampleCount], samples, sizeof(float) * tc);
		samples += tc;
		count -= tc;
		mSampleCount += tc;

		if (mSampleCount >= kInputBlockSize) {
			mSampleCount = kInputBlockSize / 2;

			ProcessBlock();

			memmove(mInputBuffer, mInputBuffer + (kInputBlockSize / 2), sizeof(float) * (kInputBlockSize / 2));
		}
	}
}

void ATPhoneToneDialDetector::ProcessBlock() {
	static constexpr int N = kProcessBlockSize;

	// run low pass filter and reduce sampling rate from 64Khz to 8KHz
	float buf[N];
	ProcessBlockReduceRate(buf);

	// run 16 Goertzel filters, 8 to detect DTMF tones and 8 to detect low
	// energy bands that we want to exclude (0-234Hz)
	static constexpr float fs = 7990.0558035714285714285714285714f;

	static constexpr float kGoertzelFrequencies[16] {
		697.0f, 770.0f, 852.0f, 941.0f,
		1209.0f, 1336.0f, 1477.0f, 1633.0f,
		0*(fs/(float)kProcessBlockSize),
		1*(fs/(float)kProcessBlockSize),
		2*(fs/(float)kProcessBlockSize),
		3*(fs/(float)kProcessBlockSize),
		4*(fs/(float)kProcessBlockSize),
		5*(fs/(float)kProcessBlockSize),
		6*(fs/(float)kProcessBlockSize),
		7*(fs/(float)kProcessBlockSize),
	};

	alignas(16) constexpr auto kGoertzelCoeffs = VDCxArray<float, 16>::transform(kGoertzelFrequencies,
		[](float freq) { return 2.0f * VDCxCosPi(2.0f * freq / fs); }
	);

	static constexpr float winCosInc = VDCxCosPi(1.0f / (float)(N - 1));
	static constexpr float winSinInc = VDCxSinPi(1.0f / (float)(N - 1));

	alignas(16) float s[2][16] {};
	float en = 0;

	float winCos = 1.0f;
	float winSin = 0.0f;

	for(int j = 0; j < N; ++j) {
		// Window in time domain with a sine window. Common raised cosine
		// based windows reduce spectral resolution too much, but a
		// rectangular window gives too much spectral leakage.
		float sample = buf[j] * winSin;

		// Run Goertzel filters.
		for(int k=0; k<16; ++k) {
			float t = kGoertzelCoeffs.v[k] * s[0][k] - s[1][k] + sample;
			s[1][k] = s[0][k];
			s[0][k] = t;
		}

		// Estimate total energy.
		en += sample * sample;

		// Advance window.
		float wc = winCos * winCosInc - winSin * winSinInc;
		float ws = winSin * winCosInc + winCos * winSinInc;

		winCos = wc;
		winSin = ws;
	}

	en *= 1.0f / (float)N;

	sint8 dtmfTone = kNoTone;

	static constexpr char kDTMFDecode[16] {
		'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', '*', '#'
	};

	// If there is insufficient energy within the 250Hz-4KHz band, suppress
	// detection.
	static constexpr float kEnergyThreshold = 0.0001f;

	if (en >= kEnergyThreshold) {
		// Compute squared magnitudes from Goertzel filters, doubling the magnitudes to
		// account for negative frequencies, and dividing by N to normalize
		// the DFT.
		float magSq[16] {};
		for(int i = 0; i < 16; ++i) {
			magSq[i] = std::max<float>(0.0f, (2.0f / (float)N / (float)N) * (s[0][i]*s[0][i] + s[1][i]*s[1][i] - s[0][i]*s[1][i]*kGoertzelCoeffs.v[i]));
		}

		// Remove doubling from DC component as it is not reflected.
		magSq[8] *= 0.5f;

		// Subtract ~0-234Hz from total energy estimate as these are excluded
		// from DTMF detection (0-250Hz, according to spec).
		for(int i = 0; i < 8; ++i) {
			en -= magSq[i+8];
		}

		// Compute normalized energy ratios for DTMF row/column tones.
		float dtmfRatios[8] {};
		const float magSqToRatio = 1.0f / std::max<float>(en, 0.001f);
		
		for(int i = 0; i < 8; ++i)
			dtmfRatios[i] = magSqToRatio * magSq[i];

		// Index sort the DTMF tones to find the top two tones.
		int rankIndices[8] {};
		for(int i = 0; i < 8; ++i)
			rankIndices[i] = i;

		std::sort(
			rankIndices, rankIndices + 8,
			[&dtmfRatios](int i, int j) {
				return dtmfRatios[i] > dtmfRatios[j];
			}
		);

		// Compute amount of relative power in the top two DTMF bands.
		const int rank1 = rankIndices[0];
		const int rank2 = rankIndices[1];
		const float relPower1 = dtmfRatios[rank1];
		const float relPower2 = dtmfRatios[rank2];
		const float relPower12 = relPower1 + relPower2;

		// Check that the top pair has at least 50% of the total energy, both
		// tones have at least 20% energy, and that one is a row tone while the
		// other is a column tone.
		if (relPower12 > 0.5f && relPower2 > 0.2f && ((rank1 ^ rank2) & 4) != 0) {
			// We have a valid tone. Decode the keypad value.
			static constexpr sint8 kDTMFDecodeTable[4][4] {
				{ 1, 2, 3, 10 },
				{ 4, 5, 6, 11 },
				{ 7, 8, 9, 12 },
				{ 14, 0, 15, 13 },
			};

			if (rank1 & 4)
				dtmfTone = kDTMFDecodeTable[rank2 & 3][rank1 & 3];
			else
				dtmfTone = kDTMFDecodeTable[rank1 & 3][rank2 & 3];
		}

#if 0
		VDDEBUG("Total energy in 250Hz-4KHz: %7.3f | %6.2f%%  %6.2f%%  %6.2f%%  %6.2f%% | %6.2f%%  %6.2f%%  %6.2f%%  %6.2f%% | Twist: %+5.2f dBm | Decoded: %c\n"
			, sqrtf(std::max<float>(0.0f, en))
			, 100.0f * dtmfRatios[0]
			, 100.0f * dtmfRatios[1]
			, 100.0f * dtmfRatios[2]
			, 100.0f * dtmfRatios[3]
			, 100.0f * dtmfRatios[4]
			, 100.0f * dtmfRatios[5]
			, 100.0f * dtmfRatios[6]
			, 100.0f * dtmfRatios[7]
			// twist is the ratio in power between the high (column) tone and low (column tone)
			, dtmfTone >= 0 ? log10f(dtmfRatios[rank1 & 4 ? rank1 : rank2] / dtmfRatios[rank1 & 4 ? rank2 : rank1]) * 10.0f : 0.0f
			, dtmfTone >= 0 ? kDTMFDecode[dtmfTone & 15] : '-'
		);
#endif
	}

	// Now we need to determine the start and end of each tone. The rules:
	//
	// - Tones need to persist for at least 40ms
	// - Gap between digits needs to be at least 40ms
	// - Minimum tone+gap duration combined is 85ms
	//
	// Note that we analyze half overlapping blocks, so while we use N=128 at
	// 8KHz, we actually have double the resolution at ~8ms/block. Being a
	// little conservative, we want to see three good blocks in a row before
	// we consider being in a tone.
	
	static constexpr float kBlockDuration = (fs / (float)N) / 2;

	if (dtmfTone != mLastTone) {
		if (mLastTone >= 0) {
			// end the current tone
			if (mLastToneRepeatCount >= 2) {
				if (mDigitCount < kMaxDigits) {
					mDialNumber[mDigitCount++] = kDTMFDecode[mLastTone & 15];
					mDialNumber[mDigitCount] = 0;

					if (mpOnEvent)
						mpOnEvent(EventType::ValidDigit, &mDialNumber[mDigitCount - 1]);

					// If we still have a valid number and dialed #, begin dialing ASAP
					if (dtmfTone == 15) {
						mbDialing = true;

						// remove # from the number to dial
						mDialNumber[--mDigitCount] = 0;

						mpSlowScheduler->SetEvent(1, this, 1, mpEventTimeout);
					}
				} else {
					mbInvalidNumber = true;
				}
			}

			// check if we transitioned directly from one tone to another --
			// if so, mark the tone as invalid and wait until all tones cease
			if (dtmfTone != kNoTone)
				dtmfTone = kInvalidTone;
		}

		mLastToneRepeatCount = 0;
	} else if (dtmfTone >= 0) {
		// we're in a tone and it's consistent -- start incrementing the repeat
		// count
		++mLastToneRepeatCount;
	}
	
	mLastTone = dtmfTone;

	if (dtmfTone != kNoTone) {
		// we have a tone, even if invalid -- disarm the timeout
		mpSlowScheduler->UnsetEvent(mpEventTimeout);
	} else {
		// we don't have a tone anymore -- set 1s timeout
		if (!mpEventTimeout && mDigitCount > 0)
			mpSlowScheduler->SetEvent(15700, this, 1, mpEventTimeout);
	}

}

// 63-point LPF at 6KHz with Hamming window, padded to 64-point and transposed 8x8
//
// Scilab: w = 0.54 - 0.46*cos(%pi*2*[0:1:30]/30); f = w.*sinc(([0:1:30]-15)*%pi*6000/63920); f = f/sum(f); plot([0:1:30]*63920/31, abs(fft(f)));
//
alignas(16) constexpr float ATPhoneToneDialDetector::kLPFCoeffs[8][8] {
	{ 0.0018031f, -0.0102586f,  0.0239900f, 0.0936789f, 0.0239900f, -0.0102586f,  0.0018031f, 0.0000000f },
	{ 0.0018294f, -0.0083945f,  0.0135850f, 0.0921092f, 0.0355324f, -0.0114349f,  0.0014754f, 0.0002290f },
	{ 0.0016544f, -0.0062043f,  0.0047646f, 0.0875098f, 0.0476254f, -0.0115417f,  0.0007577f, 0.0004765f },
	{ 0.0013718f, -0.0039986f, -0.0021885f, 0.0802002f, 0.0595843f, -0.0102178f, -0.0004053f, 0.0007505f },
	{ 0.0010551f, -0.0020144f, -0.0071646f, 0.0706802f, 0.0706802f, -0.0071646f, -0.0020144f, 0.0010551f },
	{ 0.0007505f, -0.0004053f, -0.0102178f, 0.0595843f, 0.0802002f, -0.0021885f, -0.0039986f, 0.0013718f },
	{ 0.0004765f,  0.0007577f, -0.0115417f, 0.0476254f, 0.0875098f,  0.0047646f, -0.0062043f, 0.0016544f },
	{ 0.0002290f,  0.0014754f, -0.0114349f, 0.0355324f, 0.0921092f,  0.0135850f, -0.0083945f, 0.0018294f },
};

void VDNOINLINE ATPhoneToneDialDetector::ProcessBlockReduceRate(float *dst0) {
	alignas(16) float pipe[8] {};
	const float *VDRESTRICT src = mInputBuffer;
	float *VDRESTRICT dst = dst0;

	// preroll pipeline with 4 samples, assuming zero-padding before
	for(int i=0; i<4; ++i) {
		for(int j=0; j<8; ++j) {
			const float *VDRESTRICT coeff = kLPFCoeffs[j];
			float s = *src++;

			for(int k=0; k<8; ++k)
				pipe[k] += coeff[k] * s;
		}

		for(int j=0; j<7; ++j)
			pipe[j] = pipe[j+1];

		pipe[7] = 0;
	}

	// Process bulk of samples.
	//
	// We are downsampling to 1/8th rate while filtering, so within each group of 8 samples, each
	// sample uses a different slice of the FIR filter, which is itself sampled every 8 samples.
	// Every 8 samples we shift out an output sample and shift a new 0 into the pipeline.
	//
	for(size_t i=4; i<kProcessBlockSize; ++i) {
		for(int j=0; j<8; ++j) {
			const float *VDRESTRICT coeff = kLPFCoeffs[j];
			float s = *src++;

			for(int k=0; k<8; ++k)
				pipe[k] += coeff[k] * s;
		}

		*dst++ = pipe[0];

		for(int j=0; j<7; ++j)
			pipe[j] = pipe[j+1];

		pipe[7] = 0;
	}

	// write final four samples, assuming zero-padding after
	for(int i=0; i<4; ++i)
		*dst++ = pipe[i];
}
