//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2020 Avery Lee
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
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#include <stdafx.h>
#include <at/atcore/crc.h>

extern constexpr ATCRC7Table kATCRC7Table = []() constexpr -> ATCRC7Table {
	ATCRC7Table table {};

	for(uint32 i = 0; i < 256; ++i) {
		uint8 v = i;

		for(uint32 j = 0; j < 8; ++j) {
			if (v >= 0x80)
				v ^= 0x89;

			v += v;
		}

		table.v[i] = v;
	}

	return table;
}();

extern constexpr ATCRC16Table kATCRC16Table = []() constexpr -> ATCRC16Table {
	ATCRC16Table table {};

	for(int i=0; i<256; ++i) {
		uint16 crc = i << 8;

		for(int j=0; j<8; ++j)
			crc = (crc + crc) ^ (crc & 0x8000 ? 0x1021 : 0);

		table.v[i] = crc;
	}

	return table;
}();

uint8 ATComputeCRC7(uint8 crc, const void *data, size_t len) {
	const uint8 *data8 = (const uint8 *)data;

	while(len--)
		crc = kATCRC7Table.v[crc ^ *data8++];

	return crc;
}

uint16 ATComputeCRC16(uint16 crc, const void *data, size_t len) {
	const uint8 *data8 = (const uint8 *)data;

	while(len--)
		crc = (crc << 8) ^ kATCRC16Table.v[(crc >> 8) ^ *data8++];

	return crc;
}
