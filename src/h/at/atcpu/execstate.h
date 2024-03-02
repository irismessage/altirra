//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2015 Avery Lee
//	CPU emulation library - execution state
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

#ifndef AT_ATCPU_EXECSTATE_H
#define AT_ATCPU_EXECSTATE_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <vd2/system/vdtypes.h>

struct ATCPUExecState {
	// 6502/65C02/65802/65816
	uint16	mPC;
	uint8	mA;
	uint8	mX;
	uint8	mY;
	uint8	mS;
	uint8	mP;

	// 65802/65816 only
	uint8	mAH;
	uint8	mXH;
	uint8	mYH;
	uint8	mSH;
	uint8	mB;
	uint8	mK;
	uint16	mDP;
	bool	mbEmulationFlag;
};

#endif
