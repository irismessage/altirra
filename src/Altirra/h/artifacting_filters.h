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

#ifndef f_AT_ARTIFACTING_FILTERS_H
#define f_AT_ARTIFACTING_FILTERS_H

#include <vd2/system/vdstl.h>

struct ATFilterKernel {
	typedef vdfastvector<float> Coeffs;
	int mOffset;
	Coeffs mCoeffs;

	void Init(int off, std::initializer_list<float> vals) {
		mOffset = off;
		mCoeffs.insert(mCoeffs.end(), vals.begin(), vals.end());
	}

	ATFilterKernel operator-() const {
		ATFilterKernel r(*this);

		for(Coeffs::iterator it(r.mCoeffs.begin()), itEnd(r.mCoeffs.end()); it != itEnd; ++it)
			*it = -*it;

		return r;
	}

	ATFilterKernel operator<<(int offset) const {
		ATFilterKernel r(*this);

		r.mOffset -= offset;
		return r;
	}

	ATFilterKernel operator>>(int offset) const {
		ATFilterKernel r(*this);

		r.mOffset += offset;
		return r;
	}

	ATFilterKernel& operator+=(const ATFilterKernel& src);
	ATFilterKernel& operator-=(const ATFilterKernel& src);
	ATFilterKernel& operator*=(float scale);
	ATFilterKernel& operator*=(const ATFilterKernel& src);

	ATFilterKernel trim() const;
};

void ATFilterKernelSetBicubic(ATFilterKernel& k, float offset, float A);
void ATFilterKernelConvolve(ATFilterKernel& r, const ATFilterKernel& x, const ATFilterKernel& y);
void ATFilterKernelReverse(ATFilterKernel& r);
float ATFilterKernelEvaluate(const ATFilterKernel& k, const float *src);
void ATFilterKernelAccumulate(const ATFilterKernel& k, float *dst);

void ATFilterKernelAccumulateSub(const ATFilterKernel& k, float *dst);
void ATFilterKernelAccumulate(const ATFilterKernel& k, float *dst, float scale);
void ATFilterKernelAccumulateWindow(const ATFilterKernel& k, float *dst, int offset, int limit, float scale);

ATFilterKernel operator~(const ATFilterKernel& src);
ATFilterKernel operator*(const ATFilterKernel& x, const ATFilterKernel& y);
ATFilterKernel operator*(const ATFilterKernel& src, float scale);
ATFilterKernel operator+(const ATFilterKernel& x, const ATFilterKernel& y);
ATFilterKernel operator-(const ATFilterKernel& x, const ATFilterKernel& y);
ATFilterKernel operator^(const ATFilterKernel& x, const ATFilterKernel& y);

void ATFilterKernelEvalCubic4(float co[4], float offset, float A);

ATFilterKernel ATFilterKernelSampleBicubic(const ATFilterKernel& src, float offset, float step, float A);
ATFilterKernel ATFilterKernelSamplePoint(const ATFilterKernel& src, int offset, int step);

#endif
