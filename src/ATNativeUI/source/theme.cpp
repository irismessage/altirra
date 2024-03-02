//	Altirra - Atari 800/800XL/5200 emulator
//	Native UI library - system theme support
//	Copyright (C) 2008-2020 Avery Lee
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
#include <windows.h>
#include <vd2/system/binary.h>
#include <vd2/system/color.h>
#include <at/atcore/configvar.h>
#include <at/atcore/notifylist.h>
#include <at/atnativeui/theme.h>
#include <at/atnativeui/theme_win32.h>

namespace {
	struct ThemeColorToBrushMapping {
		uint32 ATUIThemeColors::*mpColor;
		HBRUSH ATUIThemeColorsW32::*mpBrush;
		COLORREF ATUIThemeColorsW32::*mpCRef;
	} kThemeColorToBrushMappings[] = {
		{ &ATUIThemeColors::mStaticFg,		&ATUIThemeColorsW32::mStaticFgBrush,	&ATUIThemeColorsW32::mStaticFgCRef },
		{ &ATUIThemeColors::mStaticBg,		&ATUIThemeColorsW32::mStaticBgBrush,	&ATUIThemeColorsW32::mStaticBgCRef },
		{ &ATUIThemeColors::mDisabledFg,	&ATUIThemeColorsW32::mDisabledFgBrush,	&ATUIThemeColorsW32::mDisabledFgCRef },
		{ &ATUIThemeColors::mContentFg,		&ATUIThemeColorsW32::mContentFgBrush,	&ATUIThemeColorsW32::mContentFgCRef },
		{ &ATUIThemeColors::mContentBg,		&ATUIThemeColorsW32::mContentBgBrush,	&ATUIThemeColorsW32::mContentBgCRef },
	};

	void ATUIUpdateDarkThemeColors() {
		if (ATUIIsDarkThemeActive()) {
			ATUIUpdateThemeColors();
			ATUINotifyThemeChanged();
		}
	}

	ATConfigVarRGBColor g_ATCVDarkThemeStaticBg("ui.theme.dark.static_bg", 0x303030, ATUIUpdateDarkThemeColors);
	ATConfigVarRGBColor g_ATCVDarkThemeStaticFg("ui.theme.dark.static_fg", 0xD8D8D8, ATUIUpdateDarkThemeColors);
	ATConfigVarRGBColor g_ATCVDarkThemeContentBg("ui.theme.dark.content_bg", 0x202020, ATUIUpdateDarkThemeColors);
	ATConfigVarRGBColor g_ATCVDarkThemeContentFg("ui.theme.dark.content_fg", 0xD8D8D8, ATUIUpdateDarkThemeColors);
}

bool g_ATUIDarkThemeEnabled;
ATNotifyList<const vdfunction<void()> *> g_ATUIThemeChangeNotifyList;
ATUIThemeColors g_ATUIThemeColors;
ATUIThemeColorsW32 g_ATUIThemeColorsW32;

void ATUIInitThemes() {
	ATUIUpdateThemeColors();
}

void ATUIShutdownThemes() {
	VDASSERT(g_ATUIThemeChangeNotifyList.IsEmpty());

	for (const auto& mapping : kThemeColorToBrushMappings) {
		if (g_ATUIThemeColorsW32.*(mapping.mpBrush)) {
			DeleteObject(g_ATUIThemeColorsW32.*(mapping.mpBrush));
			g_ATUIThemeColorsW32.*(mapping.mpBrush) = nullptr;
		}
	}
}

bool ATUIIsDarkThemeActive() {
	return g_ATUIDarkThemeEnabled;
}

void ATUISetDarkThemeActive(bool enabled) {
	if (g_ATUIDarkThemeEnabled != enabled) {
		g_ATUIDarkThemeEnabled = enabled;

		ATUIUpdateThemeColors();
		ATUINotifyThemeChanged();
	}
}

void ATUINotifyThemeChanged() {
	g_ATUIThemeChangeNotifyList.NotifyAll(
		[](const vdfunction<void()> *fn) {
			(*fn)();
		}
	);
}

void ATUIUpdateThemeColors() {
	auto& tc = g_ATUIThemeColors;

	tc.mActiveCaption1		= VDSwizzleU32(GetSysColor(COLOR_ACTIVECAPTION)) >> 8;
	tc.mActiveCaption2		= VDSwizzleU32(GetSysColor(COLOR_GRADIENTACTIVECAPTION)) >> 8;
	tc.mInactiveCaption1	= VDSwizzleU32(GetSysColor(COLOR_INACTIVECAPTION)) >> 8;
	tc.mInactiveCaption2	= VDSwizzleU32(GetSysColor(COLOR_GRADIENTINACTIVECAPTION)) >> 8;

	if (g_ATUIDarkThemeEnabled) {
		auto darken = [](uint32 c, float lumaFactor, float chromaFactor) {
			auto lc = VDColorRGB::FromBGR8(c).SRGBToLinear();
			auto luma = lc.Luma();
			auto chroma = lc - luma;
				
			lc = chroma * chromaFactor + luma * lumaFactor;

			return lc.LinearToSRGB().ToBGR8();
		};

		tc.mActiveCaption1 = darken(tc.mActiveCaption1, 0.3f, 1.0f);
		tc.mActiveCaption2 = darken(tc.mActiveCaption2, 0.3f, 1.0f);
		tc.mInactiveCaption1 = darken(tc.mInactiveCaption1, 0.3f, 0.3f);
		tc.mInactiveCaption2 = darken(tc.mInactiveCaption2, 0.3f, 0.3f);
		tc.mActiveCaptionText = 0xE0E0E0;
		tc.mInactiveCaptionText = 0xD0D0D0;
		tc.mCaptionIcon		= 0xE0E0E0;

		tc.mStaticBg		= g_ATCVDarkThemeStaticBg;
		tc.mStaticFg		= g_ATCVDarkThemeStaticFg;
		tc.mDisabledFg		= 0x808080;
		tc.mContentBg		= g_ATCVDarkThemeContentBg;
		tc.mContentFg		= g_ATCVDarkThemeContentFg;
		tc.mHighlightedBg	= VDSwizzleU32(GetSysColor(COLOR_HIGHLIGHT)) >> 8;
		tc.mHighlightedFg	= 0xFFFFFF;
		tc.mInactiveHiBg	= 0x909090;
		tc.mInactiveHiFg	= 0x000000;
		tc.mInactiveTextBg	= 0x303030;
		tc.mHyperlinkText	= 0x50B0FF;
		tc.mCommentText		= 0x00D800;
		tc.mHeadingText		= 0x40A0FF;
		tc.mDirectLocationBg = 0xC0C060;
		tc.mDirectLocationFg = 0x000000;
		tc.mIndirectLocationBg = 0x60C060;
		tc.mIndirectLocationFg = 0x000000;

		tc.mHardPosEdge		= 0x909090;
		tc.mHardNegEdge		= 0x909090;
		tc.mSoftPosEdge		= 0x383838;
		tc.mSoftNegEdge		= 0x383838;
		tc.mButtonBg		= 0x383838;

		tc.mButtonFg		= tc.mStaticFg;

		tc.mFocusedBg		= 0x505050;
		tc.mFocusedRect		= 0x808080;
	} else {
		tc.mActiveCaptionText	= VDSwizzleU32(GetSysColor(COLOR_CAPTIONTEXT)) >> 8;
		tc.mInactiveCaptionText	= VDSwizzleU32(GetSysColor(COLOR_INACTIVECAPTIONTEXT)) >> 8;
		tc.mCaptionIcon		= 0x000000;

		tc.mStaticBg		= VDSwizzleU32(GetSysColor(COLOR_3DFACE)) >> 8;
		tc.mStaticFg		= VDSwizzleU32(GetSysColor(COLOR_WINDOWTEXT)) >> 8;
		tc.mDisabledFg		= VDSwizzleU32(GetSysColor(COLOR_GRAYTEXT)) >> 8;
		tc.mContentBg		= VDSwizzleU32(GetSysColor(COLOR_WINDOW)) >> 8;
		tc.mContentFg		= VDSwizzleU32(GetSysColor(COLOR_WINDOWTEXT)) >> 8;
		tc.mHighlightedBg	= VDSwizzleU32(GetSysColor(COLOR_HIGHLIGHT)) >> 8;
		tc.mHighlightedFg	= VDSwizzleU32(GetSysColor(COLOR_HIGHLIGHTTEXT)) >> 8;
		tc.mInactiveHiBg	= VDSwizzleU32(GetSysColor(COLOR_3DFACE)) >> 8;
		tc.mInactiveHiFg	= VDSwizzleU32(GetSysColor(COLOR_WINDOWTEXT)) >> 8;
		tc.mHyperlinkText	= 0x0000FF;
		tc.mCommentText		= 0x008000;
		tc.mKeywordText		= 0x0000FF;
		tc.mInactiveTextBg	= 0xF0F0F0;

		const uint32 c1 = tc.mHighlightedBg;
		const uint32 c2 = tc.mContentFg;
		tc.mHeadingText		= (c1 | c2) - (((c1 ^ c2) & 0xfefefe) >> 1);

		tc.mDirectLocationBg = 0xFFFF80;
		tc.mDirectLocationFg = 0x000000;
		tc.mIndirectLocationBg = 0x80FF80;
		tc.mIndirectLocationFg = 0x000000;
		tc.mHiMarkedFg = 0x000000;
		tc.mHiMarkedBg = 0xFF8080;
		tc.mPendingHiMarkedFg = 0x000000;
		tc.mPendingHiMarkedBg = 0xFFA880;

		tc.mHardPosEdge = VDSwizzleU32(GetSysColor(COLOR_3DHIGHLIGHT)) >> 8;
		tc.mSoftPosEdge = VDSwizzleU32(GetSysColor(COLOR_BTNHIGHLIGHT)) >> 8;
		tc.mSoftNegEdge = VDSwizzleU32(GetSysColor(COLOR_BTNSHADOW)) >> 8;
		tc.mHardNegEdge = VDSwizzleU32(GetSysColor(COLOR_3DSHADOW)) >> 8;
		tc.mButtonBg = VDSwizzleU32(GetSysColor(COLOR_BTNFACE)) >> 8;
		tc.mButtonFg = VDSwizzleU32(GetSysColor(COLOR_BTNTEXT)) >> 8;

		tc.mFocusedBg = tc.mHighlightedBg;
	}
	
	tc.mButtonPushedBg	= (tc.mStaticFg | tc.mButtonBg) - (((tc.mStaticFg ^ tc.mButtonBg) & 0xfefefe) >> 1);
	tc.mButtonCheckedBg = (tc.mButtonBg | tc.mButtonPushedBg) - (((tc.mButtonBg ^ tc.mButtonPushedBg) & 0xfefefe) >> 1);

	BOOL gradientsEnabled = FALSE;
	SystemParametersInfo(SPI_GETGRADIENTCAPTIONS, 0, &gradientsEnabled, FALSE);

	if (!gradientsEnabled) {
		tc.mActiveCaption2 = tc.mActiveCaption1;
		tc.mInactiveCaption2 = tc.mInactiveCaption1;
	}

	// update COLORREFs in Win32-specific theme info, and recreate brushes that are missing
	// or for which the color has changed
	for (const auto& mapping : kThemeColorToBrushMappings) {
		const uint32 color = tc.*(mapping.mpColor);
		const uint32 cref = VDSwizzleU32(color) >> 8;
		COLORREF& brushcref = g_ATUIThemeColorsW32.*(mapping.mpCRef);
		HBRUSH& brush = g_ATUIThemeColorsW32.*(mapping.mpBrush);

		if (!brush || brushcref != cref) {
			brushcref = cref;

			HBRUSH oldBrush = brush;

			brush = CreateSolidBrush(cref);
			if (!brush) {
				// ugh, we couldn't create the brush -- pick stock black/white so hopefully
				// things aren't _totally_ broken

				brush = (HBRUSH)GetStockObject((color & 0xff00) > 0x8000 ? WHITE_BRUSH : BLACK_BRUSH);
			}

			if (oldBrush)
				DeleteObject(oldBrush);
		}
	}
}

void ATUIRegisterThemeChangeNotification(const vdfunction<void()> *fn) {
	g_ATUIThemeChangeNotifyList.Add(fn);
}

void ATUIUnregisterThemeChangeNotification(const vdfunction<void()> *fn) {
	g_ATUIThemeChangeNotifyList.Remove(fn);
}

const ATUIThemeColors& ATUIGetThemeColors() {
	return g_ATUIThemeColors;
}

const ATUIThemeColorsW32& ATUIGetThemeColorsW32() {
	return g_ATUIThemeColorsW32;
}
