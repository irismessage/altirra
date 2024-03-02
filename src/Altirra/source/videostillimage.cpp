//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2022 Avery Lee
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
#include <vd2/system/math.h>
#include <vd2/system/vectors.h>
#include <vd2/Kasumi/pixel.h>
#include <vd2/Kasumi/pixmapops.h>
#include <at/atcore/devicevideosource.h>
#include <at/atcore/propertyset.h>
#include "oshelper.h"
#include "videostillimage.h"

extern const ATDeviceDefinition g_ATDeviceDefVideoStillImage {
	"videostillimage",
	"videostillimage",
	L"Video still image",
	[](const ATPropertySet& pset, IATDevice** dev) {
		vdrefptr<ATDeviceVideoStillImage> devp(new ATDeviceVideoStillImage);
		
		*dev = devp.release();
	}
};

ATDeviceVideoStillImage::ATDeviceVideoStillImage() {
	SetSaveStateAgnostic();
}

void *ATDeviceVideoStillImage::AsInterface(uint32 id) {
	if (id == IATDeviceVideoSource::kTypeID)
		return static_cast<IATDeviceVideoSource *>(this);

	return ATDevice::AsInterface(id);
}

void ATDeviceVideoStillImage::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefVideoStillImage;
}

void ATDeviceVideoStillImage::GetSettingsBlurb(VDStringW& buf) {
	buf = mImagePath;
}

void ATDeviceVideoStillImage::GetSettings(ATPropertySet& settings) {
	settings.SetString("path", mImagePath.c_str());
}

bool ATDeviceVideoStillImage::SetSettings(const ATPropertySet& settings) {
	const wchar_t *path = settings.GetString("path", L"");

	if (mImagePath != path) {
		mImagePath = path;

		if (mbInited)
			LoadImageFile();
	}

	return true;
}

void ATDeviceVideoStillImage::Init() {
	mbInited = true;
	LoadImageFile();
}

bool ATDeviceVideoStillImage::GetErrorStatus(uint32 idx, VDStringW& error) {
	if (idx == 0 && mbImageLoadFailed) {
		error.sprintf(L"Unable to load still image: %ls", mImagePath.c_str());
		return true;
	}

	return false;
}

uint32 ATDeviceVideoStillImage::ReadVideoSample(float x, int y) const {
	if (y < 0 || y >= 480 || mbImageLoadFailed)
		return 0;

	int xf = (int)((x * mImageXScale + mImageXOffset) * 256.0f + 0.5f);
	int yf = (int)((y * mImageYScale + mImageYOffset) * 256.0f + 0.5f);

	// return blanking if completely outside of active region
	// we allow tapering off horizontally, but vertical is strict
	if (xf < 0x80 || xf >= (mImage.w << 8) + 0x180 || yf < 0 || yf >= (mImage.h << 8))
		return 0;

	return VDPixmapInterpolateSampleRGB24(mImage, xf, yf) & 0xFFFFFF;
}

void ATDeviceVideoStillImage::LoadImageFile() {
	mImage.clear();
	mbImageLoadFailed = true;

	if (mImagePath.empty())
		return;

	try {
		VDPixmapBuffer pxbuf;
		ATLoadFrame(pxbuf, mImagePath.c_str());

		sint32 w = pxbuf.w;
		sint32 h = pxbuf.h;

		mImage.init(w + 2, h, nsVDPixmap::kPixFormat_XRGB8888);
		memset(mImage.base(), 0, mImage.size());

		VDPixmapBlt(mImage, 1, 0, pxbuf, 0, 0, w, h);

		mbImageLoadFailed = false;

		// Compute image scaling.
		//
		// We inscribe the provided image within a 720x480 area at CCIR 601 sampling
		// rate (13.5MHz), but assume that the incoming image uses square pixels
		// (12 + 27/99 MHz). With square pixels, the area is 654.54 x 480.

		static constexpr float kPixelAspect = (12.0f + 27.0f/99.0f) / 13.5f;
		const float wf = (float)w;
		const float hf = (float)h;
		const float scale = std::min<float>(wf / 654.54f, hf / 480.0f);
		mImageXScale = kPixelAspect * scale;
		mImageYScale = scale;

		// +1 to account for left border
		mImageXOffset = -360.0f * mImageXScale + 0.5f * wf + 1.0f;
		mImageYOffset = -240.0f * mImageYScale + 0.5f * hf;
	} catch(const MyError&) {
	}
}
