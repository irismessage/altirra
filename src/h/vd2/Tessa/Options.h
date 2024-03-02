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

#ifndef f_VD2_TESSA_OPTIONS_H
#define f_VD2_TESSA_OPTIONS_H

struct VDTInternalOptions {
	static bool sbEnableDXFlipMode;
	static bool sbEnableDXFlipDiscard;
	static bool sbEnableDXWaitableObject;
	static bool sbEnableDXDoNotWait;
	static bool sbD3D11Force9_1;
	static bool sbD3D11Force9_3;
	static bool sbD3D11Force10_0;
};

#endif
