//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2018 Avery Lee
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
#include <at/atcore/enumparseimpl.h>
#include "constants.h"

AT_DEFINE_ENUM_TABLE_BEGIN(ATHLEProgramLoadMode)
	{ kATHLEProgramLoadMode_Default, "default" }, 
	{ kATHLEProgramLoadMode_Type3Poll, "type3poll" }, 
	{ kATHLEProgramLoadMode_Deferred, "deferred" }, 
	{ kATHLEProgramLoadMode_DiskBoot, "diskboot" }, 
AT_DEFINE_ENUM_TABLE_END(ATHLEProgramLoadMode, kATHLEProgramLoadMode_Default)

AT_DEFINE_ENUM_TABLE_BEGIN(ATMemoryMode)
	{ kATMemoryMode_48K, "48k" },
	{ kATMemoryMode_52K, "52k" },
	{ kATMemoryMode_64K, "64k" },
	{ kATMemoryMode_128K, "128k" },
	{ kATMemoryMode_320K, "320k" },
	{ kATMemoryMode_576K, "576k" },
	{ kATMemoryMode_1088K, "1088k" },
	{ kATMemoryMode_16K, "16k" },
	{ kATMemoryMode_8K, "8k" },
	{ kATMemoryMode_24K, "24k" },
	{ kATMemoryMode_32K, "32k" },
	{ kATMemoryMode_40K, "40k" },
	{ kATMemoryMode_320K_Compy, "320kcompy"},
	{ kATMemoryMode_576K_Compy, "576kcompy" },
	{ kATMemoryMode_256K, "256k" },
AT_DEFINE_ENUM_TABLE_END(ATMemoryMode, kATMemoryMode_320K)

AT_DEFINE_ENUM_TABLE_BEGIN(ATHardwareMode)
	{ kATHardwareMode_800, "800" },
	{ kATHardwareMode_800XL, "800xl" },
	{ kATHardwareMode_5200, "5200" },
	{ kATHardwareMode_XEGS, "XEGS" },
	{ kATHardwareMode_1200XL, "1200xl" },
	{ kATHardwareMode_130XE, "130xe" },
	{ kATHardwareMode_1400XL, "1400xl" },
AT_DEFINE_ENUM_TABLE_END(ATHardwareMode, kATHardwareMode_800XL)

AT_DEFINE_ENUM_TABLE_BEGIN(ATVideoStandard)
	{ kATVideoStandard_NTSC, "ntsc" },
	{ kATVideoStandard_PAL, "pal" },
	{ kATVideoStandard_SECAM, "secam" },
	{ kATVideoStandard_PAL60, "pal60" },
	{ kATVideoStandard_NTSC50, "ntsc50" },
AT_DEFINE_ENUM_TABLE_END(ATVideoStandard, kATVideoStandard_NTSC)
