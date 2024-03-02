//	Altirra - Atari 800/800XL/5200 emulator
//	SpartaDOS X filesystem utilities
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

extern constexpr uint8 kATSDFSBootSector0[128]={
	0x00,
	0x03,		// +1	number of boot sectors
	0x00,		// +2	load address low ($3000)
	0x30,		// +3	load address high
	0x40,		// +4	init address low ($0740)
	0x07,		// +5	init address high
	0x4C,		// +6	launch address (JMP) -- must be JMP $3080 or $0440 to be recognized by IDE+2 EXE loader
	0x80,		// +7	signature byte
	0x30,		// +8
	0x44,		// +9	root directory sector map low (vsec 68)
	0x00,		// +10	root directory sector map high
	0xfe,		// +11	total sector count low (65535)
	0xff,		// +12	total sector count high
	0x00,		// +13	free sector count low
	0x00,		// +14	free sector count high
	0x40,		// +15	bitmap sector count (64)
	0x04,		// +16	bitmap start sector low (4)
	0x00,		// +17	bitmap start sector high
	0x45,		// +18	next free data sector low (69)
	0x00,		// +19	next free data sector high
	0x45,		// +20	next free directory sector low (69)
	0x00,		// +21	next free directory sector high
	0x41,		// +22	volume name
	0x20,
	0x20,
	0x20,
	0x20,
	0x20,
	0x20,
	0x20,
	0x01,		// +30	track count (1)
	0x80,		// +31	sector size (128)
	0x21,		// +32	filesystem version (2.1)
	0x80,		// +33	sector size low (128)
	0x00,		// +34	sector size high
	0x3e,		// +35	sector references per sector map low (62)
	0x00,		// +36	sector references per sector map high
	0x01,		// +37	physical sectors per logical sector (1)
	0x00,		// +38	volume sequence number
	0xA5,		// +39	volume random ID
	0x00,		// +40	boot file starting sector low
	0x00,		// +41	boot file starting sector high
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,		// +50
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,		// +60
	0x00,
	0x00,
	0x00,

	0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,	// $4x (RTS)
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,	// $5x
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,	// $6x
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,	// $7x
};

extern constexpr uint8 kATSDFSBootSector0_256b[256]={
	0x00,
	0x03,		// +1	number of boot sectors
	0x00,		// +2	load address low ($3000)
	0x30,		// +3	load address high
	0x40,		// +4	init address low ($0740)
	0x07,		// +5	init address high
	0x4C,		// +6	launch address (JMP) -- must be JMP $3080 or $0440 to be recognized by IDE+2 EXE loader
	0x80,		// +7	signature byte
	0x30,		// +8
	0x24,		// +9	root directory sector map low (vsec 36)
	0x00,		// +10	root directory sector map high
	0xfe,		// +11	total sector count low (65535)
	0xff,		// +12	total sector count high
	0x00,		// +13	free sector count low
	0x00,		// +14	free sector count high
	0x20,		// +15	bitmap sector count (32)
	0x04,		// +16	bitmap start sector low (4)
	0x00,		// +17	bitmap start sector high
	0x25,		// +18	next free data sector low (37)
	0x00,		// +19	next free data sector high
	0x25,		// +20	next free directory sector low (37)
	0x00,		// +21	next free directory sector high
	0x41,		// +22	volume name
	0x20,
	0x20,
	0x20,
	0x20,
	0x20,
	0x20,
	0x20,
	0x01,		// +30	track count (1)
	0x00,		// +31	sector size (256)
	0x21,		// +32	filesystem version (2.1)
	0x00,		// +33	sector size low (256)
	0x01,		// +34	sector size high
	0x7e,		// +35	sector references per sector map low (126)
	0x00,		// +36	sector references per sector map high
	0x01,		// +37	physical sectors per logical sector (1)
	0x00,		// +38	volume sequence number
	0xA5,		// +39	volume random ID
	0x00,		// +40	boot file starting sector low
	0x00,		// +41	boot file starting sector high
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,		// +50
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,		// +60
	0x00,
	0x00,
	0x00,

	0x60 // $4x (RTS)
};

extern constexpr uint8 kATSDFSBootSector0_512b[256]={
	0x00,
	0x01,		// +1	number of boot sectors
	0x00,		// +2	load address low ($0400)
	0x04,		// +3	load address high
	0xE0,		// +4	init address low ($07E0)
	0x07,		// +5	init address high
	0x4C,		// +6	launch address (JMP) -- must be JMP $3080 or $0440 to be recognized by IDE+2 EXE loader
	0x40,		// +7	signature byte
	0x04,		// +8
	0x12,		// +9	root directory sector map low (vsec 18)
	0x00,		// +10	root directory sector map high
	0xfe,		// +11	total sector count low (65535)
	0xff,		// +12	total sector count high
	0x00,		// +13	free sector count low
	0x00,		// +14	free sector count high
	0x10,		// +15	bitmap sector count (16)
	0x02,		// +16	bitmap start sector low (2)
	0x00,		// +17	bitmap start sector high
	0x13,		// +18	next free data sector low (19)
	0x00,		// +19	next free data sector high
	0x13,		// +20	next free directory sector low (19)
	0x00,		// +21	next free directory sector high
	0x41,		// +22	volume name
	0x20,
	0x20,
	0x20,
	0x20,
	0x20,
	0x20,
	0x20,
	0x01,		// +30	track count (1)
	0x01,		// +31	sector size (1 = 512)
	0x21,		// +32	filesystem version (2.1)
	0x00,		// +33	sector size low (512)
	0x02,		// +34	sector size high
	0xFE,		// +35	sector references per sector map low (254)
	0x00,		// +36	sector references per sector map high
	0x01,		// +37	physical sectors per logical sector (1)
	0x00,		// +38	volume sequence number
	0xA5,		// +39	volume random ID
	0x00,		// +40	boot file starting sector low
	0x00,		// +41	boot file starting sector high
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,		// +50
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,		// +60
	0x00,
	0x00,
	0x00,

	0x60 // $4x (RTS)
};

extern constexpr uint8 kATSDFSBootSector1[128]={
	0xA9,0x60,		// LDA #$60
	0x8D,0x40,0x07,	// STA $0740
	0x38,			// SEC
	0x60			// RTS
};
