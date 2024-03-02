//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2020 Avery Lee
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
#include <vd2/system/color.h>
#include "palettesolver.h"
#include "palettegenerator.h"
#include "gtia.h"

class ATColorPaletteSolver final : public VDAlignedObject<16>, public IATColorPaletteSolver {
public:
	void Init(const ATColorParams& initialState, const uint32 palette[256], bool lockHueStart, bool lockGamma) override;
	void Reinit(const ATColorParams& initialState) override;
	Status Iterate() override;

	std::optional<uint32> GetCurrentError() const override { return mBestError < ~UINT32_C(0) ? mBestError : std::optional<uint32>(); }
	void GetCurrentSolution(ATColorParams& params) const override;

private:
	uint32 ComputeScore(const ATColorParams& params) const;
	uint8 FastRand() {
		// Marsaglia Xorshift32 generator
		mXorShift32State ^= mXorShift32State << 13;
		mXorShift32State ^= mXorShift32State >> 17;
		mXorShift32State ^= mXorShift32State << 5;

		return (uint8)mXorShift32State;
	}

	uint32 mXorShift32State = 1;

	uint32 mTargetPalette[256];

	float mDeltaScale;
	bool mbLockHueStart;
	bool mbLockGamma;
	ATColorParams mBestParams;
	uint32 mBestError = ~UINT32_C(0);
	uint32 mPatienceCounter = 0;

	uint8 mHeapIndices[32];
	uint32 mErrorHeap[32];
	ATColorParams mParamHeap[32];
};

IATColorPaletteSolver *ATCreateColorPaletteSolver() {
	return new ATColorPaletteSolver;
}

void ATColorPaletteSolver::Init(const ATColorParams& initialState, const uint32 palette[256], bool lockHueStart, bool lockGamma) {
	mbLockHueStart = lockHueStart;
	mbLockGamma = lockGamma;

	for(int i=0; i<256; ++i)
		mTargetPalette[i] = palette[i];

	Reinit(initialState);

	mBestError = ComputeScore(initialState);
}

void ATColorPaletteSolver::Reinit(const ATColorParams& initialState) {
	mBestParams = initialState;
	mDeltaScale = 1.0f;
	mPatienceCounter = 0;
	mXorShift32State = (uint32)((VDGetPreciseTick() * UINT64_C(0x100000001)) >> 32);

	std::fill(std::begin(mParamHeap), std::end(mParamHeap), initialState);
	for(int i=0; i<32; ++i)
		mHeapIndices[i] = i;
}

ATColorPaletteSolver::Status ATColorPaletteSolver::Iterate() {
	if (++mPatienceCounter >= 1000) {
		mPatienceCounter = 0;

		mDeltaScale -= 0.02f;

		if (mDeltaScale < 0.01f)
			return Status::Finished;
	}

	uint8 dstIndex = mHeapIndices[0];
	ATColorParams& dstParams = mParamHeap[dstIndex];

	if (!(FastRand() & 1)) {
		// crossover
		const ATColorParams& srcParams1 = mParamHeap[mHeapIndices[((FastRand() * 31) >> 8) + 1]];
		const ATColorParams& srcParams2 = mParamHeap[mHeapIndices[((FastRand() * 31) >> 8) + 1]];

#if 1
		uint8 mask = FastRand();
		dstParams.mHueStart = (mask & 0x01 ? srcParams1 : srcParams2).mHueStart;
		dstParams.mHueRange = (mask & 0x02 ? srcParams1 : srcParams2).mHueRange;
		dstParams.mBrightness = (mask & 0x04 ? srcParams1 : srcParams2).mBrightness;
		dstParams.mContrast = (mask & 0x08 ? srcParams1 : srcParams2).mContrast;
		dstParams.mSaturation = (mask & 0x10 ? srcParams1 : srcParams2).mSaturation;
		dstParams.mGammaCorrect = (mask & 0x20 ? srcParams1 : srcParams2).mGammaCorrect;
		dstParams.mColorMatchingMode = (mask & 0x40 ? srcParams1 : srcParams2).mColorMatchingMode;
#else
		uint8 mask = FastRand();
		dstParams.mHueStart		= (srcParams1.mHueStart		+ srcParams2.mHueStart		) * 0.5f;
		dstParams.mHueRange		= (srcParams1.mHueRange		+ srcParams2.mHueRange		) * 0.5f;
		dstParams.mBrightness	= (srcParams1.mBrightness	+ srcParams2.mBrightness	) * 0.5f;
		dstParams.mContrast		= (srcParams1.mContrast		+ srcParams2.mContrast		) * 0.5f;
		dstParams.mSaturation	= (srcParams1.mSaturation	+ srcParams2.mSaturation	) * 0.5f;
		dstParams.mGammaCorrect	= (srcParams1.mGammaCorrect	+ srcParams2.mGammaCorrect	) * 0.5f;
		dstParams.mColorMatchingMode = (mask & 0x40 ? srcParams1 : srcParams2).mColorMatchingMode;
#endif
	} else {
		// mutate
		const ATColorParams& srcParams1 = mParamHeap[mHeapIndices[((FastRand() * 31) >> 8) + 1]];
		dstParams = srcParams1;

		if (FastRand() & 1) {
			float delta = mDeltaScale * ((sint8)FastRand() / 128.0f);

			switch((FastRand() * 6) >> 8) {
				case 0:
					dstParams.mHueStart	+= delta * 10.0f;
					break;
				case 1:
					dstParams.mHueStart	-= delta * 5.0f;
					dstParams.mHueRange	+= delta * 10.0f;
					break;
				case 2:
					dstParams.mBrightness += delta * 1.0f;
					break;
				case 3:
					dstParams.mBrightness -= delta * 0.5f;
					dstParams.mContrast += delta * 1.0f;
					break;
				case 4:		dstParams.mSaturation		+= delta * 1.0f;		break;
				case 5:
					if (!mbLockGamma)
						dstParams.mGammaCorrect		+= delta * 1.0f;
					break;

				case 6:
					dstParams.mColorMatchingMode = srcParams1.mColorMatchingMode == ATColorMatchingMode::SRGB ? ATColorMatchingMode::None : ATColorMatchingMode::SRGB;
					break;
			}
		} else {
			dstParams.mHueStart		+= mDeltaScale * ((sint8)FastRand() / 128.0f) * 10.0f;

			float hueRangeDelta = mDeltaScale * ((sint8)FastRand() / 128.0f) * 10.0f;
			dstParams.mHueStart		-= hueRangeDelta * 0.5f;
			dstParams.mHueRange		+= hueRangeDelta;

			dstParams.mBrightness	+= mDeltaScale * ((sint8)FastRand() / 128.0f) * 1.0f;
			dstParams.mContrast		+= mDeltaScale * ((sint8)FastRand() / 128.0f) * 1.0f;
			dstParams.mSaturation	+= mDeltaScale * ((sint8)FastRand() / 128.0f) * 1.0f;

			if (!mbLockGamma)
				dstParams.mGammaCorrect	+= mDeltaScale * ((sint8)FastRand() / 128.0f) * 1.0f;

			if (FastRand() & 1)
				dstParams.mColorMatchingMode = srcParams1.mColorMatchingMode == ATColorMatchingMode::SRGB ? ATColorMatchingMode::None : ATColorMatchingMode::SRGB;
		}
	}

	dstParams.mHueStart		= dstParams.mHueStart + 360.0f * truncf((dstParams.mHueStart - 60.0f) / 360.0f);
	dstParams.mHueRange		= std::clamp(dstParams.mHueRange,		0.0f, 540.0f);
	dstParams.mBrightness	= std::clamp(dstParams.mBrightness,		-0.20f, 0.20f);
	dstParams.mContrast		= std::clamp(dstParams.mContrast,		0.01f, 1.5f);
	dstParams.mSaturation	= std::clamp(dstParams.mSaturation,		0.01f, 0.75f);

	if (!mbLockGamma)
		dstParams.mGammaCorrect	= std::clamp(dstParams.mGammaCorrect,	0.5f, 2.0f);

	uint32 error = ComputeScore(dstParams);

	const auto minHeapPred = [this](uint8 i, uint8 j) {
		return mErrorHeap[i] < mErrorHeap[j];
	};

	std::pop_heap(std::begin(mHeapIndices), std::end(mHeapIndices), minHeapPred);

	VDASSERT(std::end(mHeapIndices)[-1] == dstIndex);
	mErrorHeap[dstIndex] = error;

	std::push_heap(std::begin(mHeapIndices), std::end(mHeapIndices), minHeapPred);

	Status status = Status::RunningNoImprovement;

	if (error < mBestError) {
		mBestParams = dstParams;
		mBestError = error;
		mPatienceCounter = 0;
		status = Status::RunningImproved;
	}

	return status;
}

void ATColorPaletteSolver::GetCurrentSolution(ATColorParams& params) const {
	params = mBestParams;
}

uint32 ATColorPaletteSolver::ComputeScore(const ATColorParams& params) const {
	ATColorPaletteGenerator gen;
	gen.Generate(params, ATMonitorMode::Color);

	uint32 error = 0;
	for(int i=0; i<256; ++i) {
		if (!(i & 15))
			++i;

		uint32 c = mTargetPalette[i];
		uint32 d = gen.mPalette[i];

		sint32 dr = (sint32)((c >> 16) & 0xFF) - (sint32)((d >> 16) & 0xFF);
		sint32 dg = (sint32)((c >>  8) & 0xFF) - (sint32)((d >>  8) & 0xFF);
		sint32 db = (sint32)((c >>  0) & 0xFF) - (sint32)((d >>  0) & 0xFF);

		error += (uint32)(dr * dr + dg * dg + db * db);
	}

	return error;
}
