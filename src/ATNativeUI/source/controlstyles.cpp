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
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Kasumi/region.h>
#include <at/atnativeui/theme.h>
#include <at/atnativeui/theme_win32.h>
#include <at/atnativeui/controlstyles.h>

class ATUICheckboxStyleW32 final : public vdrefcounted<IATUICheckboxStyleW32> {
	ATUICheckboxStyleW32(const ATUICheckboxStyleW32&) = delete;
	ATUICheckboxStyleW32& operator=(const ATUICheckboxStyleW32&) = delete;

public:
	ATUICheckboxStyleW32() = default;
	~ATUICheckboxStyleW32();

	void Init(HDC hdc, int w, int h);

	bool IsMatch(HDC, int w, int h) const override {
		return mWidth == w && mHeight == h;
	}

	void Draw(HDC hdc, int dx, int dy, bool radio, bool disabled, bool pushed, bool highlighted, bool checked, bool indeterminate) override;

private:
	HDC mhdc = nullptr;
	HBITMAP mhbm = nullptr;
	HGDIOBJ mhbmOld = nullptr;
	int mWidth = 0;
	int mHeight = 0;
};

///////////////////////////////////////////////////////////////////////////

ATUICheckboxStyleW32::~ATUICheckboxStyleW32() {
	if (mhbmOld)
		SelectObject(mhdc, mhbmOld);

	if (mhbm)
		DeleteObject(mhbm);

	if (mhdc)
		DeleteDC(mhdc);
}

void ATUICheckboxStyleW32::Init(HDC hdc, int w, int h) {
	mWidth = w;
	mHeight = h;

	const ATUIThemeColors& tc = ATUIGetThemeColors();

	const int atlasW = w * 12;
	const int atlasH = h * 2;

	mhdc = CreateCompatibleDC(hdc);
	if (!mhdc)
		return;

	mhbm = CreateCompatibleBitmap(hdc, atlasW, atlasH);
	if (!mhbm)
		return;

	mhbmOld = SelectObject(mhdc, mhbm);
	if (!mhbmOld)
		return;

	for(int typeIndex = 0; typeIndex < 2; ++typeIndex) {
		const bool isRadio = typeIndex > 0;

		for(int stateIndex = 0; stateIndex < 12; ++stateIndex) {
			// states: unchecked/checked/indet { disabled, normal, highlighted, pushed }
			const bool checked = (stateIndex % 3) != 0;
			const bool indeterminate = (stateIndex % 3) == 2;
			const bool disabled = (stateIndex / 3) == 0;
			const bool highlighted = (stateIndex / 3) == 2;
			const bool pushed = (stateIndex / 3) == 3;

			VDPixmapLayout layout;
			VDPixmapCreateLinearLayout(layout, nsVDPixmap::kPixFormat_XRGB8888, w*4, h*4, 4);
			VDPixmapLayoutFlipV(layout);
			VDPixmapBuffer px(layout);

			const uint32 iconBg = disabled ? tc.mStaticBg : tc.mContentBg;

			VDPixmapPathRasterizer prast;
			VDPixmapRegion rg;

			const float cwf = (float)w * 4;
			const float chf = (float)h * 4;

			if (isRadio) {
				const float radius = std::min(cwf, chf) * 0.5f;
							
				VDMemset32Rect(px.data, px.pitch, tc.mStaticBg, w*4, h*4);
				prast.CircleF(vdfloat2{cwf, chf}*0.5f, radius, false);
				prast.ScanConvert(rg);
				VDPixmapFillRegion(px, rg, 0, 0, iconBg);

				prast.Clear();

				prast.CircleF(vdfloat2{cwf, chf}*0.5f, radius, false);
				prast.CircleF(vdfloat2{cwf, chf}*0.5f, radius - 4.0f, true);

				prast.ScanConvert(rg);

				const uint32 circleColor = disabled ? tc.mDisabledFg : highlighted ? tc.mHighlightedBg : tc.mHardPosEdge;
				VDPixmapFillRegion(px, rg, 0, 0, circleColor);

				if (checked || pushed) {
					prast.CircleF(vdfloat2{cwf, chf}*0.5f, radius - 12.0f, true);

					prast.ScanConvert(rg);
					VDPixmapFillRegion(px, rg, 0, 0, pushed ? tc.mStaticFg : circleColor);
				}
			} else {
				VDMemset32Rect(px.data, px.pitch, iconBg, w*4, h*4);
				prast.RectangleF(vdfloat2{0, 0}, vdfloat2{cwf, chf}, false);
				prast.RectangleF(vdfloat2{4, 4}, vdfloat2{cwf-4, chf-4}, true);

				prast.ScanConvert(rg);

				const uint32 circleColor = disabled ? tc.mDisabledFg : highlighted ? tc.mHighlightedBg : tc.mHardPosEdge;
				VDPixmapFillRegion(px, rg, 0, 0, circleColor);

				if (checked || pushed || indeterminate) {
					vdfloat2 area1{8.0f, 8.0f};
					vdfloat2 area2{cwf-8.0f, chf-8.0f};

					if (indeterminate) {
						prast.RectangleF(area1, area2, false);
					} else {
						vdfloat2 checkpts[] {
							vdfloat2{1, 0.10f},
							vdfloat2{0.5f, 0.90f},
							vdfloat2{0, 0.50f},
						};

						vdfloat2 area1{8.0f, 8.0f};
						vdfloat2 area2{cwf-8.0f, chf-8.0f};

						for(vdfloat2& v : checkpts)
							v = area1 + (area2 - area1)*v;

						prast.StrokeF(checkpts, std::size(checkpts), 6.0f);
					}

					prast.ScanConvert(rg);
					VDPixmapFillRegion(px, rg, 0, 0, pushed ? tc.mStaticFg : circleColor);
				}
			}

			VDPixmapLayout layout2;
			VDPixmapCreateLinearLayout(layout2, nsVDPixmap::kPixFormat_XRGB8888, w, h, 4);
			VDPixmapLayoutFlipV(layout2);
			VDPixmapBuffer px2(layout2);

			VDPixmapResolve4x(px2, px);

			BITMAPINFO biTile {
				{
					sizeof(BITMAPINFOHEADER),
					w,
					h,
					1,
					32,
					BI_RGB,
					w*h*4U,
					0,
					0,
					0,
					0
				}
			};

			SetDIBitsToDevice(mhdc, stateIndex * w, typeIndex * h, w, h, 0, 0, 0, h, px2.base(), &biTile, DIB_RGB_COLORS);
		}
	}
}

void ATUICheckboxStyleW32::Draw(HDC hdc, int dx, int dy, bool radio, bool disabled, bool pushed, bool highlighted, bool checked, bool indeterminate) {
	if (!mhdc)
		return;

	int sx = ((disabled ? 0 : pushed ? 3 : highlighted ? 2 : 1) * 3 + (indeterminate ? 2 : checked ? 1 : 0)) * mWidth;
	int sy = radio ? mHeight : 0;

	BitBlt(hdc, dx, dy, mWidth, mHeight, mhdc, sx, sy, SRCCOPY);
}

///////////////////////////////////////////////////////////////////////////

template<typename T, typename... T_Args>
class ATUIStyleCache {
public:
	~ATUIStyleCache();

	vdrefptr<T> CreateStyle(T_Args ...args);

private:
	vdfastvector<T *> mStyles;
};

template<typename T, typename... T_Args>
ATUIStyleCache<T, T_Args...>::~ATUIStyleCache() {
	while(!mStyles.empty()) {
		mStyles.back()->Release();
		mStyles.pop_back();
	}
}

template<typename T, typename... T_Args>
vdrefptr<T> ATUIStyleCache<T, T_Args...>::CreateStyle(T_Args ...args) {
	for(T *style : mStyles) {
		if (style->IsMatch(args...))
			return vdrefptr(style);
	}

	vdrefptr<T> newStyle(new T);
	newStyle->Init(args...);

	mStyles.push_back(newStyle);
	newStyle->AddRef();

	return newStyle;
}

struct ATUIGlobalStyleCache {
	ATUIStyleCache<ATUICheckboxStyleW32, HDC, int, int> mCheckboxStyles;
};

ATUIGlobalStyleCache *g_pATUIStyleCache;

void ATUIInitControlStylesW32() {
	if (!g_pATUIStyleCache)
		g_pATUIStyleCache = new ATUIGlobalStyleCache;
}

void ATUIShutdownControlStylesW32() {
	if (g_pATUIStyleCache) {
		delete g_pATUIStyleCache;
		g_pATUIStyleCache = nullptr;
	}
}

vdrefptr<IATUICheckboxStyleW32> ATUIGetCheckboxStyleW32(HDC hdc, int w, int h) {
	return g_pATUIStyleCache->mCheckboxStyles.CreateStyle(hdc, w, h);
}
