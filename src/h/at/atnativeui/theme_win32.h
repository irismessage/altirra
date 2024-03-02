//	Altirra - Atari 800/800XL/5200 emulator
//	Native UI library - Win32-specific system theme support
//	Copyright (C) 2008-2019 Avery Lee
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

#ifndef f_AT_ATNATIVEUI_THEME_WIN32_H
#define f_AT_ATNATIVEUI_THEME_WIN32_H

#include <windows.h>

struct ATUIThemeColorsW32 {
	COLORREF mStaticBgCRef;
	COLORREF mStaticFgCRef;
	COLORREF mContentBgCRef;
	COLORREF mContentFgCRef;
	COLORREF mDisabledFgCRef;
	HBRUSH mStaticBgBrush;
	HBRUSH mStaticFgBrush;
	HBRUSH mContentBgBrush;
	HBRUSH mContentFgBrush;
	HBRUSH mDisabledFgBrush;
};

const ATUIThemeColorsW32& ATUIGetThemeColorsW32();

#endif
