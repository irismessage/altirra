//	Altirra - Atari 800/800XL/5200 emulator
//	I/O library - cassette FSK and direct decoders
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

#ifndef f_AT_ATIO_CASSETTEDECODER_H
#define f_AT_ATIO_CASSETTEDECODER_H

#include <at/atio/cassetteimage.h>

class VDBufferedWriteStream;

class ATCassetteDecoderFSK {
public:
	ATCassetteDecoderFSK();

	void Reset();

	template<bool T_DoAnalysis>
	void Process(const sint16 *samples, uint32 n, uint32 *bitfield, uint32 bitoffset, float *adest);

protected:
	struct Acc {
		sint32 m0R;
		sint32 m0I;
		sint32 m1R;
		sint32 m1I;
	} mAcc;
	uint32 mIndex;
	sint16 mHistory[24];
};

///////////////////////////////////////////////////////////////////////////////

class ATCassetteDecoderTurbo {
public:
	ATCassetteDecoderTurbo();

	void Init(ATCassetteTurboDecodeAlgorithm algorithm, bool enableAnalysis);

	void Reset();
	void Process(const sint16 *samples, uint32 n, float *adest);
	vdfastvector<uint32> Finalize();

private:
	enum class PreFilterType : uint8 {
		None,
		HP_IIR,
		HP_FIR
	};

	enum class DetectorType : uint8 {
		Slope,
		Level,
		Peak
	};

	template<bool T_DoAnalysis, DetectorType T_Detector, PreFilterType T_PreFilter>
	void Process(const sint16 *samples, uint32 n, float *adest);

	uint32 mBitAccum {};
	uint32 mBitCounter {};
	uint32 mWrittenBits {};

	float mHPFFactor {};
	bool mbCurrentState {};
	float mAGC {};
	float mPrevLevel {};
	float mPrefilterState {};

	// peak detector state
	uint32 mPostFilterWindowIdx {};
	bool mbLastStable {};
	bool mbLastPolarity {};
	sint32 mPeakSignCounter {};
	uint32 mPeakOffset {};
	uint32 mPeakWindowCount {};
	float mPeakValue {};
	float mPeakSign {};
	uint64 mShiftReg {};

	ATCassetteTurboDecodeAlgorithm mAlgorithm {};

	float mHPFWindow[16] {};

	float mPostFilterWindow[64] {};

	void (ATCassetteDecoderTurbo::*mpAlgorithm)(const sint16 *samples, uint32 n, float *adest);
	vdfastvector<uint32> mBitfield;
};

#endif
