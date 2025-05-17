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
#include <vd2/VDDisplay/renderer.h>
#include <at/atuicontrols/uiimage.h>

void ATUIImage::SetImage() {
	mImageView.SetImage();
	mpImageOwner.clear();

	Invalidate();
}

void ATUIImage::SetImage(const VDPixmap& px, IVDRefUnknown *owner) {
	vdrefptr holder(owner);
	mImageView.SetImage(px, false);
	mpImageOwner = std::move(holder);

	Invalidate();
}

void ATUIImage::SetPixelAspectRatio(float aspectRatio) {
	if (mPixelAspectRatio != aspectRatio) {
		mPixelAspectRatio = aspectRatio;

		Invalidate();
	}
}

void ATUIImage::SetAlignX(float x) {
	if (mAlignX != x) {
		mAlignX = x;

		Invalidate();
	}
}

void ATUIImage::SetAlignY(float y) {
	if (mAlignY != y) {
		mAlignY = y;

		Invalidate();
	}
}

void ATUIImage::Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) {
	VDDisplayBltOptions bltOptions {};
	bltOptions.mFilterMode = VDDisplayBltOptions::kFilterMode_Bilinear;

	const VDPixmap& px = mImageView.GetImage();

	if (px.data && px.w > 0 && px.h > 0 && w > 0 && h > 0) {
		float imgw = mPixelAspectRatio * (float)px.w;
		float imgh = (float)px.h;
		float areaw = (float)w;
		float areah = (float)h;
		float scale = std::min<float>(imgw > 0 ? areaw / imgw : 0, areah / imgh);

		sint32 dstw = VDRoundToInt(imgw * scale);
		sint32 dsth = VDRoundToInt(imgh * scale);
		sint32 dstx = VDRoundToInt((float)(w - dstw) * mAlignX);
		sint32 dsty = VDRoundToInt((float)(h - dsth) * mAlignY);

		rdr.StretchBlt(dstx, dsty, dstw, dsth, mImageView, 0, 0, px.w, px.h, bltOptions);
	}
}
