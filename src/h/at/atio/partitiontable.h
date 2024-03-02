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

#ifndef f_AT_ATUI_PARTITIONTABLE_H
#define f_AT_ATUI_PARTITIONTABLE_H

class IATBlockDevice;

enum class ATPartitionSectorMode : uint8 {
	// Sector is left padded within each block, with 0-padding to end
	// of block.
	Direct,

	// Each byte in the sector is padded to a word with 0 bytes.
	Direct2x,

	// Each byte in the sector is padded to a dword with 0 bytes.
	Direct4x,

	// Bytes from each pair of sectors are interleaved into words, with
	// the lower sector's bytes coming before each of the higher sector's
	// bytes.
	InterleavedSectorBytes,

	// Same as InterleavedSectorBytes, but with $0000 words in between
	// each byte pair. Used to interleave 128b sectors.
	InterleavedSectorBytes2x,

	// Each block contains an even sector followed by an odd sector.
	PackedSectors,

	// Each block contains an even sector followed by an odd sector,
	// with the bytes of each sector padded to words with $00 after
	// each byte. Used to pack 128b sectors.
	PackedSectorsByteToWord,
};

struct ATPartitionInfo {
	ATPartitionSectorMode mSectorMode {};
	uint32 mBlockStart = 0;
	uint32 mBlockCount = 0;
	uint32 mSectorSize = 0;
	uint32 mSectorCount = 0;
	bool mbWriteProtected = false;
	uint32 mMetadataSector = 0;
	VDStringW mName;
};

void ATDecodePartitionTable(IATBlockDevice& bdev, vdvector<ATPartitionInfo>& partitions);

#endif
