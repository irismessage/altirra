//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2023 Avery Lee
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

#include "stdafx.h"
#include <vd2/system/constexpr.h>
#include <vd2/system/zip.h>
#include <at/attest/test.h>

AT_DEFINE_TEST(System_CRC) {
	uint8 buf[1024];

	for(int i=0; i<1024; ++i)
		buf[i] = i+1;

	static constexpr auto kCRCTab =
		VDCxArray<uint32, 256>::from_index(
			[](uint8 c) {
				uint32 v = c;

				for(int i=0; i<8; ++i)
					v = (v >> 1) ^ (v&1 ? 0xEDB88320 : 0);

				return v;
			}
		);

	static constexpr auto kReferenceCRCs =
		[] {
			VDCxArray<uint32, 1025> r {};

			uint32 crc = 0xFFFFFFFF;

			// CRC bytes 01..nn
			for(uint32 i = 0; i < 1025; ++i) {
				r.v[i] = ~crc;

				uint8 c = (i+1) & 0xFF;

				crc = (crc >> 8) ^ kCRCTab.v[(crc ^ c) & 0xFF];
			}

			return r;
		}();
	/*
	static constexpr uint32 kReferenceCRCs[65] {
		0x00000000,0xD202EF8D,0x36DE2269,0x0854897F,
		0x8BB98613,0x515AD3CC,0x30EBCF4A,0xAD5809F9,
		0x88AA689F,0xBCE14302,0x456CD746,0xAD2D8EE1,
		0x9270C965,0xE6FE46B8,0x69EF56C8,0xA06C675E,
		0xCECEE288,0x2C183A19,0xDCF57F85,0xBCB51C15,
		0x3BDDFFA4,0x195881FE,0xE5C38CFC,0x92382767,
		0x8295A696,0xD880D40C,0xBF078BB2,0x0AB0C3DC,
		0xD708085D,0xD30E9683,0xC5665F58,0x4D786D77,
		0x91267E8A,0xE4908305,0xEEE59BDF,0x11E084B7,
		0x25715854,0x8222EFE9,0x405243F9,0xC42E728B,
		0x0DA62E3C,0xC8D59DDE,0xF1135DAF,0x4F218B7F,
		0xBE4B5BEB,0xD7BCF3C5,0x7C043934,0x2F1C12CE,
		0x05202171,0xD3DCBEAA,0xB50C79FF,0x37625DF9,
		0xA984A67E,0x44A2C3A5,0x2249DE0A,0xFD4FDAD4,
		0xEBFC1395,0x7A8ECCCB,0x81CBF271,0x338DBC67,
		0xB0EC7FEE,0xBA6FB00A,0x6A052532,0xDBDEA683,
		0x100ECE8C,
	};
	*/

	for(int i=0; i<=1024; ++i) {
		VDCRCChecker crc(VDCRCTable::CRC32);
		crc.Init();
		crc.Process(buf, i);
		uint32 v = crc.CRC();

		crc.Init();
		for(int j=0; j<i; ++j)
			crc.Process(buf+j, 1);
		uint32 v2 = crc.CRC();

		uint32 ref = kReferenceCRCs.v[i];

		AT_TEST_TRACEF("%3d: %08X %08X != %08X", i, v, v2, ref);

		AT_TEST_ASSERTF(v == ref && v2 == ref, "FAIL %3d: %08X %08X != %08X\n", i, v, v2, ref);
	}

	return 0;
}
