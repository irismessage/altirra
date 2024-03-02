//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2016 Avery Lee
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
#include <vd2/system/binary.h>
#include <vd2/system/zip.h>
#include "firmwaremanager.h"
#include "firmwaredetect.h"

struct ATKnownFirmware {
	uint32 mCRC;
	uint32 mSize;
	ATFirmwareType mType;
	const wchar_t *mpDesc;
	ATSpecificFirmwareType mSpecificType;
} kATKnownFirmwares[]={
	{ 0x4248d3e3,  2048, kATFirmwareType_Kernel5200, L"Atari 5200 OS (4-port)" },
	{ 0xc2ba2613,  2048, kATFirmwareType_Kernel5200, L"Atari 5200 OS (2-port)" },
	{ 0x4bec4de2,  8192, kATFirmwareType_Basic, L"Atari BASIC rev. A", kATSpecificFirmwareType_BASICRevA },
	{ 0xf0202fb3,  8192, kATFirmwareType_Basic, L"Atari BASIC rev. B", kATSpecificFirmwareType_BASICRevB },
	{ 0x7d684184,  8192, kATFirmwareType_Basic, L"Atari BASIC rev. C", kATSpecificFirmwareType_BASICRevC },
	{ 0xc1b3bb02, 10240, kATFirmwareType_Kernel800_OSA, L"Atari 400/800 OS-A NTSC", kATSpecificFirmwareType_OSA },
	{ 0x72b3fed4, 10240, kATFirmwareType_Kernel800_OSA, L"Atari 400/800 OS-A PAL" },
	{ 0x0e86d61d, 10240, kATFirmwareType_Kernel800_OSB, L"Atari 400/800 OS-B NTSC", kATSpecificFirmwareType_OSB },
	{ 0x3e28a1fe, 10240, kATFirmwareType_Kernel800_OSB, L"Atari 400/800 OS-B NTSC (patched)", kATSpecificFirmwareType_OSB },
	{ 0x0c913dfc, 10240, kATFirmwareType_Kernel800_OSB, L"Atari 400/800 OS-B PAL" },
	{ 0xc5c11546, 16384, kATFirmwareType_Kernel1200XL, L"Atari 1200XL OS" },
	{ 0x643bcc98, 16384, kATFirmwareType_KernelXL, L"Atari XL/XE OS ver.1" },
	{ 0x1f9cd270, 16384, kATFirmwareType_KernelXL, L"Atari XL/XE OS ver.2", kATSpecificFirmwareType_XLOSr2 },
	{ 0x29f133f7, 16384, kATFirmwareType_KernelXL, L"Atari XL/XE OS ver.3" },
	{ 0x1eaf4002, 16384, kATFirmwareType_KernelXEGS, L"Atari XL/XE/XEGS OS ver.4", kATSpecificFirmwareType_XLOSr4 },
	{ 0xbdca01fb,  8192, kATFirmwareType_Game, L"Atari XEGS Missile Command" },
	{ 0xa8953874, 16384, kATFirmwareType_BlackBox, L"Black Box ver. 1.34" },
	{ 0x91175314, 16384, kATFirmwareType_BlackBox, L"Black Box ver. 1.41" },
	{ 0x7cafd9a8, 65536, kATFirmwareType_BlackBox, L"Black Box ver. 2.16" },
	{ 0x7d68f07b, 16384, kATFirmwareType_MIO, L"MIO ver. 1.1k (128Kbit)" },
	{ 0x00694A74, 16384, kATFirmwareType_MIO, L"MIO ver. 1.1m (128Kbit)" },
	{ 0xa6a9e3d6,  8192, kATFirmwareType_MIO, L"MIO ver. 1.41 (64Kbit)" },
	{ 0x1d400131, 16384, kATFirmwareType_MIO, L"MIO ver. 1.41 (128Kbit)" },
	{ 0xe2f4b3a8, 32768, kATFirmwareType_MIO, L"MIO ver. 1.41 (256Kbit)" },
	{ 0x19227d33,  2048, kATFirmwareType_810, L"Atari 810 firmware rev. B" },
	{ 0x0896f03d,  2048, kATFirmwareType_810, L"Atari 810 firmware rev. C" },
	{ 0xaad220f4,  2048, kATFirmwareType_810, L"Atari 810 firmware rev. E" },
	{ 0x91ba303d,  4096, kATFirmwareType_1050, L"Atari 1050 firmware rev. J" },
	{ 0x3abe7ef4,  4096, kATFirmwareType_1050, L"Atari 1050 firmware rev. K" },
	{ 0xfb4b8757,  4096, kATFirmwareType_1050, L"Atari 1050 firmware rev. L" },
	{ 0x942ec3d5,  4096, kATFirmwareType_Happy810, L"Happy 810 firmware (pre-v7)" },
	{ 0x19b6bfe5,  8192, kATFirmwareType_Happy1050, L"Happy 1050 firmware rev. 1" },
	{ 0xf76eae16,  8192, kATFirmwareType_Happy1050, L"Happy 1050 firmware rev. 2" },
	{ 0x739bab74,  4096, kATFirmwareType_ATR8000, L"ATR8000 firmware ver 3.02" },
	{ 0xd125caad,  4096, kATFirmwareType_IndusGT, L"Indus GT firmware ver. 1.1" },
	{ 0xd8504b4a,  4096, kATFirmwareType_IndusGT, L"Indus GT firmware ver. 1.2" },
	{ 0x605b7153,  4096, kATFirmwareType_USDoubler, L"US Doubler firmware" },
	{ 0x0126A511,  2048, kATFirmwareType_810Turbo, L"810 Turbo firmware V1.2" },
	{ 0x5A396459,  2048, kATFirmwareType_Percom, L"Percom RFD V1.00" },
	{ 0xE2D4A05C,  2048, kATFirmwareType_Percom, L"Percom RFD V1.10" },
	{ 0xC6C73D23,  2048, kATFirmwareType_Percom, L"Percom RFD V1.20" },
	{ 0x2AB65122,  2048, kATFirmwareType_Percom, L"Astra 1001/1620 (based on Percom RFD V1.10)" },
	{ 0x2372FAE6,  2048, kATFirmwareType_PercomAT, L"Percom AT-88 V1.2 (460-0066-001)" },
	{ 0xFD13A674,  2048, kATFirmwareType_PercomAT, L"Percom AT-88 V1.2 (damaged) (460-0066-001)" },
};

bool ATFirmwareAutodetectCheckSize(uint64 fileSize) {
	uint32 fileSize32 = (uint32)fileSize;
	if (fileSize32 != fileSize)
		return false;

	switch(fileSize32) {
		case 2048:		// 5200, 810
		case 4096:		// 1050
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

ATFirmwareDetection ATFirmwareAutodetect(const void *data, uint32 len, ATFirmwareInfo& info, ATSpecificFirmwareType& specificType) {
	specificType = kATSpecificFirmwareType_None;

	if (!ATFirmwareAutodetectCheckSize(len))
		return ATFirmwareDetection::None;

	const uint32 crc32 = VDCRCTable::CRC32.CRC(data, len);

	// try to match a specific known firmware
	for(const ATKnownFirmware& kfw : kATKnownFirmwares) {
		if (len == kfw.mSize && crc32 == kfw.mCRC) {
			info.mName = kfw.mpDesc;
			info.mType = kfw.mType;
			info.mbVisible = true;
			info.mFlags = 0;
			specificType = kfw.mSpecificType;
			return ATFirmwareDetection::SpecificImage;
		}
	}

	// we weren't able to match a specific one -- check for specific types
	const auto isPossibleOSROM = [](const uint8 *data, uint16 baseAddr) {
		const auto isWithinROM = [=](uint16 addr) {
			return addr >= baseAddr && (addr < 0xD000 || addr >= 0xD800);
		};

		// Reset vectors must be within the ROM.
		for(int hwVecOffset = 0; hwVecOffset < 6; hwVecOffset += 2) {
			const uint16 hwVec = VDReadUnalignedLEU16(&data[0xFFFA - 0xD800 + hwVecOffset]);

			if (!isWithinROM(hwVec))
				return false;
		}

		// The device handlers at $E400-E44F must all have six valid OS vectors
		// within the OS ROM.
		for(int deviceOffset=0; deviceOffset<0x50; deviceOffset += 0x10) {
			for(int handlerOffset = 0; handlerOffset < 12; handlerOffset += 2) {
				// The CIO device table holds an RTS return address, so we must add 1 to it.
				const uint16 addr = VDReadUnalignedLEU16(data + (0xE400 - 0xD800) + deviceOffset + handlerOffset) + 1;

				// reject handler if not in OS ROM
				if (!isWithinROM(addr))
					return false;
			}
		}

		// Starting at $E450, there must be at least 16 JMP vectors within the ROM.
		for(int vectorAddr = 0xE450; vectorAddr < 0xE480; vectorAddr += 3) {
			const uint8 *vec = &data[vectorAddr - 0xD800];

			if (vec[0] != 0x4C)
				return false;

			const uint16 jmpTarget = VDReadUnalignedLEU16(vec + 1);
			if (!isWithinROM(jmpTarget))
				return false;
		}

		// We don't check the font exactly to allow for variant fonts, but we do
		// check that the space is correct (ATASCII $20, internal $00).
		const uint8 *font = &data[0xE000 - 0xD800];
		if (VDReadUnalignedU64(font))
			return false;

		// Verdict: Plausible
		return true;
	};

	if (len == 10*1024) {
		// 10K -- possible 800 OS ROM
		if (isPossibleOSROM((const uint8 *)data, 0xD800)) {
			info.mType = kATFirmwareType_Kernel800_OSB;
			return ATFirmwareDetection::TypeOnly;
		}
	} else if (len == 16*1024) {
		// 16K -- possible XL/XE OS ROM
		if (isPossibleOSROM((const uint8 *)data + 6 * 1024, 0xC000)) {
			info.mType = kATFirmwareType_KernelXL;
			return ATFirmwareDetection::TypeOnly;
		}
	}

	return ATFirmwareDetection::None;
}
