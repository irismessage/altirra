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

#include <stdafx.h>
#include <unordered_set>
#include <vd2/system/binary.h>
#include <at/atcore/blockdevice.h>
#include <at/atio/partitiontable.h>

bool ATIsAPTPartitionTable(const uint8 *dat, uint32 prevLBA, uint8 prevHeaderIndex, uint32 totalBlocks) {
	// validate signature (APT)
	if (dat[1] != 0x41 || dat[2] != 0x50 || dat[3] != 0x54)
		return false;

	// validate entry count
	if (dat[5] == 0 || dat[5] > 32)
		return false;

	// validate prev/next sector header offsets
	if (dat[6] >= 32 || dat[7] >= 32)
		return false;

	// validate previous LBA
	if (prevLBA != VDReadUnalignedLEU32(&dat[12]))
		return false;

	// validate previous header index, if we had a previous sector
	if (prevLBA && dat[7] != prevHeaderIndex)
		return false;

	// validate next LBA
	if (VDReadUnalignedLEU32(&dat[8]) >= totalBlocks)
		return false;

	// looks good
	return true;
}

void ATDecodePartitionTable(IATBlockDevice& bdev, vdvector<ATPartitionInfo>& partitions) {
	const auto n = bdev.GetSectorCount();
	uint8 buf[512];

	// read LBA 0
	bdev.ReadSectors(buf, 0, 1);

	// check for MBR -- if there is a protective MBR it will point to the APT partition
	uint32 aptLBA = 0;

	if (buf[510] == 0x55 && buf[511] == 0xAA) {
		// check MBR partitions for the APT partition
		for(int offset = 0x1BE; offset < 0x1FE; offset += 16) {
			if (buf[offset + 4] == 0x7F) {
				aptLBA = VDReadUnalignedLEU32(&buf[offset + 8]);
				break;
			}
		}

		// exit if no APT partition found
		if (!aptLBA || aptLBA >= n)
			return;

		// read in the first sector of the APT partition table
		bdev.ReadSectors(buf, aptLBA, 1);
	}

	// see if there is a valid APT partition table
	if (ATIsAPTPartitionTable(buf, 0, 0, n)) {
		// parse APT partition table
		const int numMappingSlots = (buf[0] & 0x0F) + 15;
		uint8 headerIndex = 0;
		int slotOffset = 0;

		std::unordered_set<uint32> aptBlocks;
		std::unordered_set<uint32> seenPartitionIds;
		aptBlocks.emplace(aptLBA);

		uint32 prevLBA = aptLBA;

		for(;;) {
			const int headerOffset = headerIndex*16;
			int numEntries = buf[headerOffset + 5];
			bool tableEnded = false;

			for(int i=0; i<numEntries; ++i) {
				if (i == headerIndex)
					continue;

				const uint8 *entry = &buf[i*16];

				// check if end entry
				if (!(entry[0] & 0x03)) {
					// skip if mapping slot
					if (slotOffset + i <= numMappingSlots)
						continue;

					// terminate table
					tableEnded = true;
					break;
				}

				// check if reserved entry
				if (entry[0] & 0x80)
					continue;

				// check if partition start and length is valid
				const uint32 partitionStart = VDReadUnalignedLEU32(&entry[2]);
				const uint32 partitionLength = VDReadUnalignedLEU32(&entry[6]);

				if (!partitionStart || partitionStart >= n)
					continue;

				if (!partitionLength || n - partitionStart < partitionLength)
					continue;

				// check for reserved partition IDs
				const uint16 partitionId = VDReadUnalignedLEU16(&entry[10]);
				if (partitionId == 0x0000 || partitionId == 0xFFFF)
					continue;

				// check if partition has already been seen (important to avoid
				// duplicating mapped partitions)
				if (!seenPartitionIds.insert(partitionId).second)
					continue;

				// check if DOS or external DOS partition
				enum class APTPartitionType : uint8 {
					DOS = 0,
					ExternalDOS = 3
				};

				const APTPartitionType partitionType = (APTPartitionType)entry[1];
				if (partitionType != APTPartitionType::DOS && partitionType != APTPartitionType::ExternalDOS)
					continue;

				// create partition entry
				partitions.emplace_back();
				ATPartitionInfo& ptInfo = partitions.back();

				ptInfo.mBlockStart = partitionStart;
				ptInfo.mBlockCount = partitionLength;
				ptInfo.mbWriteProtected = (entry[12] & 0x80) != 0;

				ptInfo.mName = L"APT Partition";

				if (slotOffset + i <= numMappingSlots)
					ptInfo.mName.append_sprintf(L" (D%d:)", slotOffset + i);

				// set partition block to disk sector mapping based on sector size
				// and mapping mode
				static constexpr ATPartitionSectorMode kSectorModes128[4] = {
					ATPartitionSectorMode::Direct4x,
					ATPartitionSectorMode::Direct2x,
					ATPartitionSectorMode::InterleavedSectorBytes2x,
					ATPartitionSectorMode::PackedSectorsByteToWord,
				};

				static constexpr ATPartitionSectorMode kSectorModes256[4] = {
					ATPartitionSectorMode::Direct2x,
					ATPartitionSectorMode::Direct,
					ATPartitionSectorMode::InterleavedSectorBytes,
					ATPartitionSectorMode::PackedSectors,
				};

				switch(entry[0] & 0x03) {
					case 0x01:
						ptInfo.mSectorSize = 128;
						ptInfo.mSectorMode = kSectorModes128[(entry[0] >> 2) & 3];
						break;

					case 0x02:
						ptInfo.mSectorSize = 256;
						ptInfo.mSectorMode = kSectorModes256[(entry[0] >> 2) & 3];
						break;

					case 0x03:
					default:
						ptInfo.mSectorSize = 512;
						ptInfo.mSectorMode = ATPartitionSectorMode::Direct;
						break;
				}

				ptInfo.mSectorCount = ptInfo.mBlockCount;

				switch(ptInfo.mSectorMode) {
					default:
						break;

					case ATPartitionSectorMode::InterleavedSectorBytes:
					case ATPartitionSectorMode::InterleavedSectorBytes2x:
					case ATPartitionSectorMode::PackedSectors:
					case ATPartitionSectorMode::PackedSectorsByteToWord:
						ptInfo.mSectorCount *= 2;
						break;
				}
			}

			// cache off the next header index and bump slot offset
			const uint8 nextHeaderIndex = buf[6];

			// check if there is a next sector
			const uint32 nextLBA = VDReadUnalignedLEU32(&buf[headerOffset + 8]);
			if (!nextLBA || nextLBA >= n)
				break;

			// check if sector has already been seen
			if (!aptBlocks.emplace(nextLBA).second)
				break;

			// read next sector
			bdev.ReadSectors(buf, nextLBA, n);

			// validate
			if (!ATIsAPTPartitionTable(&buf[16 * nextHeaderIndex], prevLBA, headerIndex, n))
				break;

			// shift vars before processing next block
			headerIndex = nextHeaderIndex;
			prevLBA = nextLBA;
			slotOffset += numEntries;
		}
	}
}
