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

#ifndef f_AT_ATCORE_CRC_H
#define f_AT_ATCORE_CRC_H

#include <vd2/system/vdtypes.h>

struct ATCRC7Table {
	uint8 v[256];		// v[i] = ((i*x^8) mod (x^7 + x^3 + 1))*x
};

struct ATCRC16Table {
	uint16 v[256];		// v[i] = (i*x^16) mod (x^16 + x^12 + x^5 + 1)
};

extern const ATCRC7Table kATCRC7Table;
extern const ATCRC16Table kATCRC16Table;

inline uint8 ATAdvanceCRC7(uint8 crc, uint8 v) {
	return kATCRC7Table.v[crc ^ v];
}

inline uint16 ATAdvanceCRC16(uint16 crc, uint8 v) {
	return (crc << 8) ^ kATCRC16Table.v[(crc >> 8) ^ v];
}

// Compute a CRC-7 using the polynomial x^7 + x^3 + 1 (0x9), left-justified within
// the word in bits 1-7 and with a 0 in bit 0. This computes a CRC-7 compatible
// with SD card commands.
uint8 ATComputeCRC7(uint8 crc, const void *data, size_t len);

// Compute a CRC-16-CCITT with the polynomial x^16 + x^12 + x^5 + 1 (0x1021).
// This CRC-16 is compatible with SD card data transfers with initial value 0,
// and IBM disk formats with initial value 0xFFFF.
uint16 ATComputeCRC16(uint16 crc, const void *data, size_t len);

#endif
