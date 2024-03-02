//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2014 Avery Lee
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

#include <stdafx.h>
#include "uifilefilters.h"

extern const wchar_t g_ATUIFileFilter_Disk[] =
			L"All supported images\0*.atr;*.xfd;*.dcm;*.pro;*.atx;*.arc\0"
			L"Atari disk image (*.atr,*.xfd,*.dcm)\0*.atr;*.xfd;*.dcm\0"
			L"Protected disk image (*.pro)\0*.pro\0"
			L"VAPI disk image (*.atx)\0*.atx\0"
			L"Compressed archive (*.arc)\0*.arc\0"
			L"All files\0*.*\0";

extern const wchar_t g_ATUIFileFilter_DiskWithArchives[] =
			L"All supported types\0*.atr;*.pro;*.atx;*.xfd;*.dcm;*.zip;*.gz;*.atz;*.arc\0"
			L"Atari disk image (*.atr, *.xfd)\0*.atr;*.xfd;*.dcm\0"
			L"Protected disk image (*.pro)\0*.pro\0"
			L"VAPI disk image (*.atx)\0*.atx\0"
			L"Zip archive (*.zip)\0*.zip\0"
			L"Gzip archive (*.gz;*.atz)\0*.gz;*.atz\0"
			L".ARC archive (*.arc)\0*.arc\0"
			L"All files\0*.*\0";

extern const wchar_t g_ATUIFileFilter_Cheats[]=
			L"All supported files\0*.atcheats;*.a8t\0"
			L"Altirra cheat file (*.atcheats)\0*.atcheats\0"
			L"Atari800WinPLus cheat file (*.a8t)\0*.a8t\0"
			L"All files\0*.*\0";

extern const wchar_t g_ATUIFileFilter_LoadState[]=
			L"All supported types\0*.altstate\0"
			L"Altirra save state\0*.altstate\0"
			L"All files\0*.*\0";

extern const wchar_t g_ATUIFileFilter_SaveState[]=
			L"Altirra save state (*.altstate)\0*.altstate\0";

extern const wchar_t g_ATUIFileFilter_LoadCartridge[]=
			L"All supported types\0*.bin;*.car;*.rom;*.a52;*.zip\0"
			L"Cartridge image (*.bin,*.car,*.rom,*.a52)\0*.bin;*.car;*.rom;*.a52\0"
			L"Zip archive (*.zip)\0*.zip\0"
			L"All files\0*.*\0";

extern const wchar_t g_ATUIFileFilter_LoadTape[]=
			L"All supported types\0*.wav;*.cas\0"
			L"Atari cassette image (*.cas)\0"
			L"*.cas\0"
			L"Waveform audio (*.wav)\0*.wav\0"
			L"All files\0*.*\0";
