//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2014 Avery Lee
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
#include <vd2/system/zip.h>
#include "firmwaremanager.h"

struct ATKnownFirmware {
	uint32 mCRC;
	uint32 mSize;
	ATFirmwareType mType;
	const wchar_t *mpDesc;
} kATKnownFirmwares[]={
	{ 0x4248d3e3,  2048, kATFirmwareType_Kernel5200, L"Atari 5200 OS (4-port)" },
	{ 0xc2ba2613,  2048, kATFirmwareType_Kernel5200, L"Atari 5200 OS (2-port)" },
	{ 0x4bec4de2,  8192, kATFirmwareType_Basic, L"Atari BASIC rev. A" },
	{ 0xf0202fb3,  8192, kATFirmwareType_Basic, L"Atari BASIC rev. B" },
	{ 0x7d684184,  8192, kATFirmwareType_Basic, L"Atari BASIC rev. C" },
	{ 0xc1b3bb02, 10240, kATFirmwareType_Kernel800_OSA, L"Atari 400/800 OS-A NTSC" },
	{ 0x72b3fed4, 10240, kATFirmwareType_Kernel800_OSA, L"Atari 400/800 OS-A PAL" },
	{ 0x0e86d61d, 10240, kATFirmwareType_Kernel800_OSB, L"Atari 400/800 OS-B NTSC" },
	{ 0x3e28a1fe, 10240, kATFirmwareType_Kernel800_OSB, L"Atari 400/800 OS-B NTSC (patched)" },
	{ 0x0c913dfc, 10240, kATFirmwareType_Kernel800_OSB, L"Atari 400/800 OS-B PAL" },
	{ 0xc5c11546, 16384, kATFirmwareType_Kernel1200XL, L"Atari 1200XL OS" },
	{ 0x643bcc98, 16384, kATFirmwareType_KernelXL, L"Atari XL/XE OS ver.1" },
	{ 0x1f9cd270, 16384, kATFirmwareType_KernelXL, L"Atari XL/XE OS ver.2" },
	{ 0x29f133f7, 16384, kATFirmwareType_KernelXL, L"Atari XL/XE OS ver.3" },
	{ 0x1eaf4002, 16384, kATFirmwareType_KernelXEGS, L"Atari XL/XE/XEGS OS ver.4" },
	{ 0xbdca01fb,  8192, kATFirmwareType_Game, L"Atari XEGS Missile Command" },
	{ 0xa8953874, 16384, kATFirmwareType_BlackBox, L"Black Box ver. 1.34" },
	{ 0x91175314, 16384, kATFirmwareType_BlackBox, L"Black Box ver. 1.41" },
	{ 0x7cafd9a8, 65536, kATFirmwareType_BlackBox, L"Black Box ver. 2.16" },
	{ 0xa6a9e3d6,  8192, kATFirmwareType_MIO, L"MIO ver. 1.41 (64Kbit)" },
	{ 0x1d400131, 16384, kATFirmwareType_MIO, L"MIO ver. 1.41 (128Kbit)" },
	{ 0xe2f4b3a8, 32768, kATFirmwareType_MIO, L"MIO ver. 1.41 (256Kbit)" },
};

bool ATFirmwareAutodetectCheckSize(uint64 fileSize) {
	uint32 fileSize32 = (uint32)fileSize;
	if (fileSize32 != fileSize)
		return false;

	switch(fileSize32) {
		case 2048:		// 5200
		case 8192:		// BASIC, MIO
		case 10240:		// 800
		case 16384:		// XL, XEGS, 1200XL, MIO
		case 32768:		// MIO
		case 65536:		// Black Box
			return true;

		default:
			return false;
	}
}

bool ATFirmwareAutodetect(const void *data, uint32 len, ATFirmwareInfo& info) {
	if (!ATFirmwareAutodetectCheckSize(len))
		return false;

	VDCRCChecker crcChecker(VDCRCChecker::kCRC32);
	crcChecker.Process(data, len);
	const uint32 crc32 = crcChecker.CRC();

	for(size_t i=0; i<vdcountof(kATKnownFirmwares); ++i) {
		ATKnownFirmware& kfw = kATKnownFirmwares[i];

		if (len == kfw.mSize && crc32 == kfw.mCRC) {
			info.mName = kfw.mpDesc;
			info.mType = kfw.mType;
			info.mbVisible = true;
			info.mFlags = 0;
			return true;
		}
	}

	return false;
}
