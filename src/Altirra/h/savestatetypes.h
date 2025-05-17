//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
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

#ifndef f_AT_SAVESTATETYPES_H
#define f_AT_SAVESTATETYPES_H

#include <vd2/Kasumi/pixmaputils.h>
#include <at/atcore/snapshotimpl.h>

struct ATSaveStateInfo final : public ATSnapExchangeObject<ATSaveStateInfo, "ATSaveStateInfo"> {
	template<ATExchanger T>
	void Exchange(T& ex);

	VDPixmapBuffer mImage;
	float mImageX1 = 0;
	float mImageY1 = 0;
	float mImageX2 = 0;
	float mImageY2 = 0;
	float mImagePAR = 0;

	double mSimRunTimeSeconds = 0;
	uint32 mColdStartId = 0;
};

#endif
