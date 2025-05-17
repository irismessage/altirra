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
	{ 0xc5c11546, 16384, kATFirmwareType_Kernel1200XL, L"Atari 1200XL OS rev. 10" },
	{ 0x1A1D7B1B, 16384, kATFirmwareType_Kernel1200XL, L"Atari 1200XL OS rev. 11" },
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
	{ 0xF9A7AFB2,  4096, kATFirmwareType_1050, L"Atari 1050 firmware rev. E" },
	{ 0x6D9D589B,  4096, kATFirmwareType_1050, L"Atari 1050 firmware rev. H" },
	{ 0x91ba303d,  4096, kATFirmwareType_1050, L"Atari 1050 firmware rev. J" },
	{ 0x3abe7ef4,  4096, kATFirmwareType_1050, L"Atari 1050 firmware rev. K" },
	{ 0xfb4b8757,  4096, kATFirmwareType_1050, L"Atari 1050 firmware rev. L" },
	{ 0x371F6973,  6144, kATFirmwareType_Happy810, L"Happy 810 firmware (v7, 6K functional ROM, modified)" },
	{ 0x9CC7A207,  6144, kATFirmwareType_Happy810, L"Happy 810 firmware (v7, 6K functional ROM)" },
	{ 0x982D825D,  8192, kATFirmwareType_Happy810, L"Happy 810 firmware (v7, 8K full ROM)" },
	{ 0x19b6bfe5,  8192, kATFirmwareType_Happy1050, L"Happy 1050 firmware rev. 1" },
	{ 0xf76eae16,  8192, kATFirmwareType_Happy1050, L"Happy 1050 firmware rev. 2" },
	{ 0x739bab74,  4096, kATFirmwareType_ATR8000, L"ATR8000 firmware ver 3.02" },
	{ 0xd125caad,  4096, kATFirmwareType_IndusGT, L"Indus GT firmware ver. 1.1" },
	{ 0xd8504b4a,  4096, kATFirmwareType_IndusGT, L"Indus GT firmware ver. 1.2" },
	{ 0x605b7153,  4096, kATFirmwareType_USDoubler, L"US Doubler firmware rev. L" },
	{ 0x0126A511,  2048, kATFirmwareType_810Turbo, L"810 Turbo firmware V1.2" },
	{ 0x5A396459,  2048, kATFirmwareType_Percom, L"Percom RFD V1.00" },
	{ 0xE2D4A05C,  2048, kATFirmwareType_Percom, L"Percom RFD V1.10" },
	{ 0xC6C73D23,  2048, kATFirmwareType_Percom, L"Percom RFD V1.20" },
	{ 0xAC141045,  2048, kATFirmwareType_Percom, L"Percom RFD V2.10" },
	{ 0x2AB65122,  2048, kATFirmwareType_Percom, L"Astra 1001/1620 (based on Percom RFD V1.10)" },
	{ 0x2372FAE6,  2048, kATFirmwareType_PercomAT, L"Percom AT-88 V1.2 (460-0066-001)" },
	{ 0xFD13A674,  2048, kATFirmwareType_PercomAT, L"Percom AT-88 V1.2 (damaged) (460-0066-001)" },
	{ 0x518900E6,  4096, kATFirmwareType_PercomATSPD, L"Percom AT88-SPD V1.01 (0073)" },
	{ 0x87A0D7E7,  4096, kATFirmwareType_PercomATSPD, L"Percom AT88-SPD V1.11 (1-9-84)" },
	{ 0xFC6675F2,  4096, kATFirmwareType_PercomATSPD, L"Percom AT88-SPD V1.21 (5-4-84)" },
	{ 0x55DFC9AC,  8192, kATFirmwareType_1050TurboII, L"1050 Turbo II V3.4" },
	{ 0x4A69312B,  8192, kATFirmwareType_1050TurboII, L"1050 Turbo II V3.5" },
	{ 0x1527D542,  4096, kATFirmwareType_815, L"Atari 815 firmware" },
	{ 0x87809326,  2048, kATFirmwareType_1090Firmware, L"Atari 1090 80-column firmware rev. 9" },
	{ 0x8F07D9A0,  2048, kATFirmwareType_1090Charset, L"Atari 1090 80-column charset" },
	{ 0xB9D576DA,  4096, kATFirmwareType_Bit3Firmware, L"Bit 3 Full-View 80 firmware" },
	{ 0X738C6AC1,  2048, kATFirmwareType_Bit3Charset, L"Bit 3 Full-View 80 charset" },

	// overdump (only has 2K of unique data)
	{ 0xA4534CEA,  4096, kATFirmwareType_Bit3Charset, L"Bit 3 Full-View 80 charset (4K overdump)" },

	{ 0xFC02766B,  8192, kATFirmwareType_XF551, L"XF551 rev. 7.4" },
	{ 0x38B97AE3,  8192, kATFirmwareType_XF551, L"XF551 rev. 7.7" },
	{ 0x37DDA8B1,  4096, kATFirmwareType_XF551, L"XF551 rev. 7.7 (w/broken density hack)" },
	{ 0x0E117428,  8192, kATFirmwareType_XF551, L"XF551 rev. 7.7 (modified for 720K 3.5\")" },

	{ 0x2BA701DF,  4096, kATFirmwareType_1400XLHandler, L"1400XL V:/T: handler firmware (unknown v1)" },
	{ 0x5EC3EC7D,  4096, kATFirmwareType_1400XLHandler, L"1400XL V:/T: handler firmware (unknown v2)" },

	{ 0x7773B7DD,  4096, kATFirmwareType_1450XLDiskController, L"1450XLD disk controller firmware" },
	{ 0x03930886,  4096, kATFirmwareType_1450XLTONGDiskController, L"1450XLD \"TONG\" disk controller firmware" },
	{ 0xE8F9C8A7,  4096, kATFirmwareType_1450XLDiskHandler, L"1450XLD disk handler firmware (rev. E)" },
	{ 0x4BB7FA3A,  4096, kATFirmwareType_1450XLDiskHandler, L"1450XLD disk handler firmware (rev. F)" },
	{ 0x29359910,  4096, kATFirmwareType_1450XLDiskHandler, L"1450XLD disk handler firmware (post rev. F)" },

	{ 0xD6DD4F41,  1024, kATFirmwareType_835, L"Atari 835 internal 8048 firmware" },

	{ 0x329B1D5B,  4096, kATFirmwareType_1030InternalROM, L"Atari 1030 internal 8050 firmware" },
	{ 0x7ABDB8E7,  8192, kATFirmwareType_1030ExternalROM, L"Atari 1030 external ROM firmware" },

	{ 0x79E0FEA4,  2048, kATFirmwareType_820, L"Atari 820 firmware" },
	{ 0x46D194B4,  4096, kATFirmwareType_1025, L"Atari 1025 firmware" },
	{ 0x19C4F811,  4096, kATFirmwareType_1029, L"Atari 1029 firmware (English)" },
};

bool ATFirmwareAutodetectCheckSize(uint64 fileSize) {
	uint32 fileSize32 = (uint32)fileSize;
	if (fileSize32 != fileSize)
		return false;

	switch(fileSize32) {
		case 1024:		// 835
		case 2048:		// 5200, 810
		case 4096:		// 1050
		case 6144:		// Happy 810 functional dump
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

	// Special cases for Happy 810
	//
	// The pre-v7 4K firmware for the Happy 810 has a serial number at $7FE-7FF
	// that we want to exclude for matching purposes and then note. However, due
	// to dumping issues, there are a few configurations that we need to
	// handle:
	//
	//	- 3K CPU controller visible only dump ($400-FFF)
	//	- Full 4K dump including hidden $000-3FF region
	//	- 6K/8K overdumps

	uint32 happyLen = len;

	// check for 6K/8K overdumps and reinterpret as 3K/4K if so
	if (happyLen == 6144 && !memcmp(data, (const char *)data + 3072, 3072))
		happyLen = 3072;

	if (happyLen == 8192 && !memcmp(data, (const char *)data + 4096, 4096))
		happyLen = 4096;

	if (happyLen == 3072 || happyLen == 4096) {
		uint32 happyCrc32;
		const char *happyData = (const char *)data;
		const uint8 dummyChk[2] {};

		if (happyLen == 4096)
			happyData += 1024;
		
		// checksum everything but the inaccessible part ($000-3FF) and serial
		// ($7FE-7FF).
		happyCrc32 = UINT32_C(0xFFFFFFFF);
		happyCrc32 = VDCRCTable::CRC32.Process(happyCrc32, happyData, 0x3FE);
		happyCrc32 = VDCRCTable::CRC32.Process(happyCrc32, dummyChk, 2);
		happyCrc32 = VDCRCTable::CRC32.Process(happyCrc32, happyData + 0x400, 0x800);
		happyCrc32 = ~happyCrc32;

		if (happyCrc32 == 0xE52A98B2) {
			// Okay, we've confirmed that we have the pre-v7 Happy 810 ROM, so
			// we can report it along with the serial. If we had a 4K ROM, check
			// the hidden area to see if it is genuine.

			const uint16 serial = VDReadUnalignedLEU16(happyData + 0x3FE);
			if (happyLen == 4096) {
				if (VDCRCTable::CRC32.CRC(data, 1024) == 0x6AC58569)
					info.mName.sprintf(L"Happy 810 firmware (pre-v7 serial $%04X; 4K original)", serial);
				else
					info.mName.sprintf(L"Happy 810 firmware (pre-v7 serial $%04X; 4K non-original)", serial);
			} else
				info.mName.sprintf(L"Happy 810 firmware (pre-v7 serial $%04X; 3K functional-only)", serial);

			info.mType = kATFirmwareType_Happy810;
			info.mbVisible = true;
			info.mFlags = 0;
			specificType = {};
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
