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

#include <stdafx.h>
#include "encode_png.h"
#include "savestatetypes.h"

template<ATExchanger T>
void ATSaveStateInfo::Exchange(T& ex) {
	vdrefptr<ATSaveStateMemoryBuffer> image;

	if constexpr (ex.IsReader) {
		ex.Transfer("thumbnail", &image);
	} else {
		vdautoptr pngEncoder { VDCreateImageEncoderPNG() };
		const void *pngData = nullptr;
		uint32 pngLen = 0;

		// We could set the PAR on the PNG encoder to encode the aspect ratio, but omit it for
		// now because no programs seem to really support it.
		pngEncoder->Encode(mImage, pngData, pngLen, false);

		image = new ATSaveStateMemoryBuffer;
		image->mpDirectName = L"thumbnail.png";
		image->GetWriteBuffer().assign((const uint8 *)pngData, (const uint8 *)pngData + pngLen);

		ex.Transfer("thumbnail", &image);
	}

	ex.Transfer("thumbnail_x1", &mImageX1);
	ex.Transfer("thumbnail_y1", &mImageY1);
	ex.Transfer("thumbnail_x2", &mImageX2);
	ex.Transfer("thumbnail_y2", &mImageY2);
	ex.Transfer("thumbnail_pixel_aspect", &mImagePAR);

	ex.Transfer("run_time_seconds", &mSimRunTimeSeconds);
	ex.Transfer("cold_start_id", &mColdStartId);
}

template void ATSaveStateInfo::Exchange(ATSerializer&);
template void ATSaveStateInfo::Exchange(ATDeserializer&);
