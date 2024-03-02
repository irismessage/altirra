//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2010 Avery Lee
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "stdafx.h"
#include "console.h"
#include <windows.h>
#include <commdlg.h>

void ATUIShowDialogDebugFont(VDGUIHandle hParent) {
	CHOOSEFONTW cf = {sizeof(CHOOSEFONTW)};
	LOGFONTW lf;

	ATConsoleGetFont(lf);

	cf.hwndOwner	= (HWND)hParent;
	cf.hDC			= NULL;
	cf.lpLogFont	= &lf;
	cf.iPointSize	= 0;
	cf.Flags		= CF_FIXEDPITCHONLY | CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT;

	if (ChooseFontW(&cf)) {
		lf.lfWidth			= 0;
		lf.lfEscapement		= 0;
		lf.lfOrientation	= 0;
		lf.lfWeight			= 0;
		lf.lfItalic			= FALSE;
		lf.lfUnderline		= FALSE;
		lf.lfStrikeOut		= FALSE;
		lf.lfCharSet		= DEFAULT_CHARSET;
		lf.lfOutPrecision	= OUT_DEFAULT_PRECIS;
		lf.lfClipPrecision	= CLIP_DEFAULT_PRECIS;
		lf.lfQuality		= DEFAULT_QUALITY;
		lf.lfPitchAndFamily	= FF_DONTCARE | DEFAULT_PITCH;

		ATConsoleSetFont(lf);
	}
}
