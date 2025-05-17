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

#ifndef f_AT_UIIMAGE_H
#define f_AT_UIIMAGE_H

#include <vd2/VDDisplay/renderer.h>
#include <at/atui/uiwidget.h>

class ATUIImage : public ATUIWidget {
public:
	void SetImage();
	void SetImage(const VDPixmap& px, IVDRefUnknown *owner = nullptr);

	float GetPixelAspectRatio() const { return mPixelAspectRatio; }
	void SetPixelAspectRatio(float aspectRatio);

	float GetAlignX() const { return mAlignX; }
	void SetAlignX(float x);
	float GetAlignY() const { return mAlignY; }
	void SetAlignY(float y);

	void Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) override;

private:
	VDDisplayImageView mImageView;
	vdrefptr<IVDRefUnknown> mpImageOwner;

	float mPixelAspectRatio = 1.0f;
	float mAlignX = 0.5f;
	float mAlignY = 0.5f;
};

#endif

