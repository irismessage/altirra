//	VirtualDub - Video processing and capture application
//	Graphics support library
//	Copyright (C) 1998-2019 Avery Lee
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

#ifndef f_VD2_KASUMI_UBERBLIT_YCBCR_SSE2_INTRIN_H
#define f_VD2_KASUMI_UBERBLIT_YCBCR_SSE2_INTRIN_H

#if VD_CPU_X86 || VD_CPU_X64

#include "uberblit.h"
#include "uberblit_ycbcr.h"

class VDPixmapGenRGB32ToYCbCr709_SSE2 final : public VDPixmapGenWindowBasedOneSource {
public:
	void Init(IVDPixmapGen *src, uint32 srcindex);
	void Start() override;
	const void *GetRow(sint32 y, uint32 index) override;
	uint32 GetType(uint32 output) const override;

private:
	void Compute(void *dst0, sint32 y) override;
};

#endif

#endif
