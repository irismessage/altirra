//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2024 Avery Lee
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
#include <vd2/system/binary.h>
#include <vd2/system/constexpr.h>
#include <vd2/system/error.h>
#include <vd2/system/math.h>
#include <at/atcore/crc.h>
#include <at/atcore/logging.h>
#include <at/atemulation/diskutils.h>
#include <at/atio/diskimage.h>

ATLogChannel g_ATLCFDC(false, false, "FDC", "Floppy drive controller");
ATLogChannel g_ATLCFDCWTData(false, false, "FDCWTDATA", "Floppy drive controller write track data");

static constexpr auto kATMFMBitSpreadTable = [] {
	VDCxArray<uint16, 256> table{};

	for(int i = 0; i < 256; ++i) {
		uint16 v = 0;

		if (i & 0x01) v += 0x0001;
		if (i & 0x02) v += 0x0004;
		if (i & 0x04) v += 0x0010;
		if (i & 0x08) v += 0x0040;
		if (i & 0x10) v += 0x0100;
		if (i & 0x20) v += 0x0400;
		if (i & 0x40) v += 0x1000;
		if (i & 0x80) v += 0x4000;

		table[i] = v;
	}

	return table;
}();

void ATInsertTrackBits(uint8 *dst, uint32 dstBitOffset, const uint8 *src, uint32 srcBitOffset, uint32 numBits) {
	if (!numBits)
		return;

	// byte align the destination offset
	uint8 firstByteMask = 0xFF;
	uint32 startShift = dstBitOffset & 7;
	if (startShift) {
		dstBitOffset -= startShift;
		srcBitOffset -= startShift;
		firstByteMask >>= startShift;

		numBits += startShift;
	}

	// check if we have a partial write on the end
	uint8 lastByteMask = 0xFF;
	uint32 endBits = numBits & 7;

	if (endBits)
		lastByteMask = 0xFF ^ (lastByteMask >> endBits);

	// compute number of bytes to write, including a masked first byte but not a masked last byte
	uint32 numBytes = numBits >> 3;

	// compute bit shift
	int bitShift = 8 - (srcBitOffset & 7);

	// adjust pointers
	dst += dstBitOffset >> 3;
	src += srcBitOffset >> 3;

	// if we have a masked first byte, do it now
	if (firstByteMask != 0xFF) {
		uint8 v = (uint8)(VDReadUnalignedBEU16(src++) >> bitShift);

		if (--numBytes == 0) {
			// special case -- first and last byte masks overlap
			*dst = *dst ^ ((*dst ^ v) & firstByteMask & lastByteMask);
			return;
		}

		*dst = *dst ^ ((*dst ^ v) & firstByteMask);
		++dst;
	}

	// copy over whole bytes
	if (bitShift == 8) {
		memcpy(dst, src, numBytes);
		dst += numBytes;
		src += numBytes;
	} else {
		while(numBytes--)
			*dst++ = (uint8)(VDReadUnalignedBEU16(src++) >> bitShift);
	}

	// do final partial byte
	if (endBits) {
		const uint8 v = (uint8)(VDReadUnalignedBEU16(src) >> bitShift);

		*dst = *dst ^ ((*dst ^ v) & lastByteMask);
	}
}

void ATWriteRawFMToTrack(uint8 *track, uint32 trackOffsetBits, uint32 trackLenBits, const uint8 *clockMaskBits, const uint8 *dataBits, uint32 numBytes, uint8 *fmBuffer) {
	fmBuffer[0] = 0;
	fmBuffer[1] = 0;
	fmBuffer[numBytes * 2 + 2] = 0;
	fmBuffer[numBytes * 2 + 3] = 0;

	// encode to MFM
	uint8 lastByte = 0xFF;

	for(uint32 i = 0; i < numBytes; ++i) {
		const uint8 d = dataBits[i];
		const uint8 m = clockMaskBits[i];

		const uint16 fmDataBits = kATMFMBitSpreadTable[d];
		const uint16 fmClockBits = kATMFMBitSpreadTable[m];

		VDWriteUnalignedBEU16(&fmBuffer[2 + i*2], fmDataBits + (fmClockBits << 1));

		lastByte = d;
	}

	// check if the write would wrap around the track, and thus we need a split write
	const uint32 numBits = numBytes * 16;
	if (trackLenBits - trackOffsetBits < numBits) {
		// yes - split
		uint32 numBits1 = trackLenBits - trackOffsetBits;
		uint32 numBits2 = numBits - numBits1;

		ATInsertTrackBits(track, trackOffsetBits, fmBuffer, 16, numBits1);
		ATInsertTrackBits(track, 0, fmBuffer, 16 + numBits1, numBits2);
	} else {
		// no - single write
		ATInsertTrackBits(track, trackOffsetBits, fmBuffer, 16, numBits); 
	}
}

void ATWriteRawMFMToTrack(uint8 *track, uint32 trackOffsetBits, uint32 trackLenBits, const uint8 *clockMaskBits, const uint8 *dataBits, uint32 numBytes, uint8 *mfmBuffer) {
	mfmBuffer[0] = 0;
	mfmBuffer[1] = 0;
	mfmBuffer[numBytes * 2 + 2] = 0;
	mfmBuffer[numBytes * 2 + 3] = 0;

	// encode to MFM
	uint8 lastByte = 0xFF;

	for(uint32 i = 0; i < numBytes; ++i) {
		const uint8 d = dataBits[i];
		const uint8 m = clockMaskBits[i];

		const uint16 mfmDataBits = kATMFMBitSpreadTable[d];
		const uint16 mfmClockBits = kATMFMBitSpreadTable[(uint8)(~(d | ((d >> 1) + (lastByte << 7))) & m)];

		VDWriteUnalignedBEU16(&mfmBuffer[2 + i*2], mfmDataBits + (mfmClockBits << 1));

		lastByte = d;
	}

	// check if the write would wrap around the track, and thus we need a split write
	const uint32 numBits = numBytes * 16;
	if (trackLenBits - trackOffsetBits < numBits) {
		// yes - split
		uint32 numBits1 = trackLenBits - trackOffsetBits;
		uint32 numBits2 = numBits - numBits1;

		ATInsertTrackBits(track, trackOffsetBits, mfmBuffer, 16, numBits1);
		ATInsertTrackBits(track, 0, mfmBuffer, 16 + numBits1, numBits2);
	} else {
		// no - single write
		ATInsertTrackBits(track, trackOffsetBits, mfmBuffer, 16, numBits); 
	}
}

void ATDiskRenderRawTrack(uint8 *trackData, size_t trackBitLen, vdfastvector<ATDiskRenderedSectorInfo> *renderedSectors, IATDiskImage& diskImage, uint32 track, bool mfm, bool highDensity, bool invertData) {
	if (renderedSectors)
		renderedSectors->clear();

	// Preformat the entire track with $4E bytes.
	//
	// Data bits:		%00101110
	// MFM encoded:		%10100100 01010100
	// 
	// We're guaranteed to have enough padding at the end of the track for wraparound that we
	// can run over by a byte.
	const size_t trackFullByteLen = (trackBitLen + 7) >> 3;
	const size_t trackRawByteLen = trackBitLen >> 3;

	if (mfm) {
		for(size_t i=0; i<trackFullByteLen; i += 2) {
			trackData[i+0] = 0xA4;
			trackData[i+1] = 0x54;
		}
	} else {
		for(size_t i=0; i<trackFullByteLen; i += 2) {
			trackData[i+0] = 0xFF;
			trackData[i+1] = 0xFF;
		}
	}

	static const auto bitSpread = [](uint8 v) -> uint16 {
		static constexpr auto kBitSpreadTable = [] {
			VDCxArray<uint16, 256> table {};

			for(int i=0; i<256; ++i) {
				uint16 v = 0;
				if (i & 0x01) v += 0x0001;
				if (i & 0x02) v += 0x0004;
				if (i & 0x04) v += 0x0010;
				if (i & 0x08) v += 0x0040;
				if (i & 0x10) v += 0x0100;
				if (i & 0x20) v += 0x0400;
				if (i & 0x40) v += 0x1000;
				if (i & 0x80) v += 0x4000;
				table[i] = v;
			}

			return table;
		}();

		return kBitSpreadTable[v];
	};

	// For each sector, we emit (MFM):
	//	8 x $00
	//	3 x A1_sync
	//	$FE			IDAM
	//	$xx			Track number
	//	$00			Side number
	//	$xx			Sector number
	//	$01			Sector size
	//	$xx			Address CRC 1
	//	$xx			Address CRC 2
	//	19 x $4E
	//		-- write splice here --
	//	12 x $00
	//	3 x A1_sync
	//	$FB			DAM
	//	256 x xx	Data
	//	2 x xx		Data CRC 1/2
	//	$FF
	//		-- write splice here --
	//
	// FM:
	//	11 x $00
	//	$FE($C7)	IDAM
	//	$xx			Track number
	//	$00			Side number
	//	$xx			Sector number
	//	$01			Sector size
	//	$xx			Address CRC 1
	//	$xx			Address CRC 2
	//	$00
	//		-- write splice here --
	//	16 x $00
	//	$FB($C7)	DAM
	//	256 x xx	Data
	//	2 x xx		Data CRC 1/2
	//	$FF
	//		-- write splice here --
	//
	// As the address field is normally laid down as part of a single Write Track
	// operation, it should be byte aligned, although the FDC does not require
	// this. The data field can be skewed by any arbitrary offset as it is
	// re-laid down each time the sector is written.

	const ATDiskGeometryInfo& geometry = diskImage.GetGeometry();
	uint32 vsecStart = geometry.mSectorsPerTrack * track;
	uint32 vsecCount = diskImage.GetVirtualSectorCount();

	// check geometry and skip all sectors if high density flag doesn't match
	const uint32 spt = geometry.mbHighDensity == highDensity ? geometry.mSectorsPerTrack : 0;

	// prescan all virtual sectors and build a list of physical sectors
	struct SectorInfo : public ATDiskPhysicalSectorInfo {
		uint32 mVirtIndex = 0;
		uint32 mPhysIndex = 0;
		uint8 mSectorId = 0;
		uint32 mRecordedSize = 0;
		uint32 mAddressFieldBytes = 0;
		uint32 mDataFieldBytes = 0;
		uint32 mTotalBytes = 0;
		uint32 mBytePos = 0;
		bool mbLinkedToNext = false;

		bool Overlaps(const SectorInfo& other, uint32 trackLen) const {
			if (mBytePos < other.mBytePos) {
				if (mBytePos + mTotalBytes > other.mBytePos)
					return true;

				if (other.mBytePos + other.mTotalBytes > mBytePos + trackLen)
					return true;
			} else {
				if (other.mBytePos + other.mTotalBytes > mBytePos)
					return true;

				if (mBytePos + mTotalBytes > other.mBytePos + trackLen)
					return true;
			}

			return false;
		}
	};

	vdfastvector<SectorInfo> sectors;
	uint32 totalRequiredBytes = 0;
	const uint32 trackByteLen = trackRawByteLen / 2;

	for(uint32 i = 0; i < spt; ++i) {
		if (vsecStart + i >= vsecCount)
			break;

		ATDiskVirtualSectorInfo vsi;
		diskImage.GetVirtualSectorInfo(vsecStart + i, vsi);

		for(uint32 j = 0; j < vsi.mNumPhysSectors; ++j) {
			SectorInfo info;
			info.mVirtIndex = vsecStart + i;
			info.mPhysIndex = vsi.mStartPhysSector + j;
			info.mSectorId = i + 1;
			diskImage.GetPhysicalSectorInfo(info.mPhysIndex, info);

			// toss sectors of the wrong density
			if (info.mbMFM != mfm)
				continue;

			// if the sector is a long sector and has a CRC error, trim it to 128 bytes
			info.mRecordedSize = 0;

			if (info.mFDCStatus & 0x10) {
				info.mRecordedSize = info.mPhysicalSize;

				const bool hasCRC = !(info.mFDCStatus & 0x08);
				const bool hasLong = !(info.mFDCStatus & 0x04) || (!mfm && info.mRecordedSize > 128);
				if (hasLong && hasCRC)
					info.mRecordedSize = 128;
			}

			// minimal address field bytes:
			//	MFM: presync(1) + sync(3) + IDAM + address field(6)
			//	FM: presync(1) + IDAM + address field(6)
			//
			info.mAddressFieldBytes = mfm ? 11 : 8;
			info.mDataFieldBytes = 0;

			if (info.mFDCStatus & 0x10) {
				// Minimal data field bytes:
				// FM: gapII(17) + DAM(1) + data(128-1024) + CRC(2) + postbyte(1)
				// MFM: gapII(34) + sync(3) + DAM(1) + data(128-1024) + CRC(2) + postbyte(1)
				//
				// We don't try to vary gap II because there is little leeway for it in practice -- testing
				// on a real 1050 shows that it can't be reduced low about 16 bytes in FM before reading
				// becomes marginal or fails, as there is not much margin for the FDC to detect the DAM in
				// time.
				info.mDataFieldBytes = info.mRecordedSize + (mfm ? 41 : 21);
			}

			info.mTotalBytes = info.mAddressFieldBytes + info.mDataFieldBytes;
			totalRequiredBytes += info.mTotalBytes;

			// Set initial position in encoded bytes (raw words). This is aligned to a byte boundary
			// as that will naturally occur when formatting tracks with Write Track.
			info.mBytePos = VDRoundToInt32((info.mRotPos - floorf(info.mRotPos)) * (float)trackByteLen) % trackByteLen;

			sectors.push_back(info);
		}
	}

	// Check the total number of required bytes, which will tell us how many bytes we can afford
	// to spend on gap III.

	if (totalRequiredBytes > trackByteLen) {
		g_ATLCFDC("Cannot fit track -- needs %u raw bytes, only %u available\n", totalRequiredBytes, trackByteLen);
	} else {
		g_ATLCFDC("Track needs %u raw bytes, %u available\n", totalRequiredBytes, trackByteLen);
	}

	// Gap III practice:
	//	810: 17 bytes
	//	1050: 18 bytes (FM), 62 bytes (MFM)
	//	XF551: 17 bytes (FM), 36 bytes (MFM)
	//	FDC docs: minimum 14 byte (FM), 32 bytes (MFM)
	//
	uint32 gapIII = mfm ? 32 : 14;

	if (!sectors.empty() && totalRequiredBytes < trackByteLen)
		gapIII = std::min(gapIII, (trackByteLen - totalRequiredBytes) / (uint32)sectors.size());

	// Sort all the sectors by position.
	std::sort(sectors.begin(), sectors.end(),
		[](const SectorInfo& a, const SectorInfo& b) {
			return a.mBytePos < b.mBytePos;
		}
	);
	
	// Loop over the sectors and try to resolve all overlaps. Whenever two sectors overlap, move them
	// apart by half the distance to establish the recommended gap III, then link the two sectors so
	// they move together in any subsequent shifts. Stop when either all overlaps are resolved or we
	// run out of space.
	const uint32 numSectors = (uint32)sectors.size();
	uint32 sectorGroups = numSectors;

	while(sectorGroups > 1) {
		bool foundOverlaps = false;

		for(uint32 i = 0; i < numSectors; ++i) {
			uint32 prevIdx = i ? i-1 : numSectors-1;
			uint32 nextIdx = i;
			SectorInfo& prev = sectors[prevIdx];
			SectorInfo& next = sectors[nextIdx];

			// if these sectors are linked, don't check for overlaps (they shouldn't)
			if (prev.mbLinkedToNext)
				continue;

			if (!prev.Overlaps(next, trackByteLen))
				continue;

			// These two sectors overlap. Determine the midpoint between them and then the displacement
			// to apply.
			const uint32 distance = ((prev.mBytePos + prev.mTotalBytes) + gapIII - next.mBytePos + trackByteLen) % trackByteLen;

			g_ATLCFDC("Sector %u overlaps sector %u, pushing apart by %u bytes\n", prev.mSectorId, next.mSectorId, distance);

			// Compute the needed displacements for prev and next. If the total distance needed is odd,
			// allocate the extra byte distance to next.
			const uint32 prevDisp = trackByteLen - (distance >> 1);
			const uint32 nextDisp = (distance + 1) >> 1;

			// Move all sectors in the next group. This can't loop indefinitely because we stop the process at N-1
			// joins for N sectors.
			do {
				sectors[prevIdx].mBytePos = (sectors[prevIdx].mBytePos + prevDisp) % trackByteLen;

				if (!prevIdx)
					prevIdx = numSectors;

				--prevIdx;
			} while(sectors[prevIdx].mbLinkedToNext);

			for(;;) {
				sectors[nextIdx].mBytePos = (sectors[nextIdx].mBytePos + nextDisp) % trackByteLen;

				if (!sectors[nextIdx].mbLinkedToNext)
					break;

				++nextIdx;
				if (nextIdx >= numSectors)
					nextIdx = 0;
			}

			// Mark the sectors as linked.
			prev.mbLinkedToNext = true;

			// Decrement the sector group count, as we have linked two sectors together.
			foundOverlaps = true;
			--sectorGroups;
			if (sectorGroups <= 1)
				break;
		}

		if (!foundOverlaps)
			break;
	}

	// Render all sectors.
	//
	// The maximum data we need per sector is as follows, in MFM (FM is smaller):
	//	- 8 x $00 bytes from Gap III
	//	- 3 x $A1 sync
	//	- 1 x $FE IDAM
	//	- 6 address field bytes
	//	- 24 x $4E from Gap II
	//	- 8 x $00 from Gap II
	//	- 3 x $A1 sync
	//	- 1 x $FB DAM
	//	- 1024 data bytes
	//	- 2 CRC bytes
	//	- 1 x $4E post-sync

	uint8 dataBuf[1081 + 3] {};
	uint8 clockMaskBuf[1081 + 3] {};
	uint8 mfmBuffer[sizeof(dataBuf)*2 + 4] {};

	for(const SectorInfo& psi : sectors) {
		uint8 *data = dataBuf;
		uint8 *clock = clockMaskBuf;

		// determine how much of a gap we have before the sector, and allocate up to 4 (FM) or 8 (MFM)
		// pre-sync bytes
		const SectorInfo& prev = &psi == sectors.data() ? sectors.back() : (&psi)[-1];
		const uint32 prevGapSpace = (psi.mBytePos + trackByteLen - (prev.mBytePos + prev.mTotalBytes)) % trackByteLen;
		const uint32 prevGap = std::max<uint32>(1, std::min<uint32>(prevGapSpace, mfm ? 8 : 4));

		for(uint32 i = 0; i < prevGap; ++i) {
			*data++ = 0x00;
			*clock++ = 0xFF;
		}

		// add sync (MFM only)
		if (mfm) {
			*data++ = 0xA1;
			*data++ = 0xA1;
			*data++ = 0xA1;
			*clock++ = 0xFB;
			*clock++ = 0xFB;
			*clock++ = 0xFB;
		}

		// convert sector size to code; truncate to 1K if needed
		uint32 sectorSizeCode;

		if (psi.mPhysicalSize <= 128)
			sectorSizeCode = 0;
		else if (psi.mPhysicalSize <= 256)
			sectorSizeCode = 1;
		else if (psi.mPhysicalSize <= 512)
			sectorSizeCode = 2;
		else
			sectorSizeCode = 3;

		// add IDAM + address field + CRC
		*data++ = 0xFE;
		*data++ = (uint8)track;
		*data++ = 0;
		*data++ = psi.mSectorId;
		*data++ = sectorSizeCode;

		*clock++ = mfm ? 0xFF : 0xC7;
		*clock++ = 0xFF;
		*clock++ = 0xFF;
		*clock++ = 0xFF;
		*clock++ = 0xFF;

		// update the CRC -- include sync bytes if in MFM
		uint16 addressCRC = mfm ? ATComputeCRC16(0xFFFF, data - 8, 8) : ATComputeCRC16(0xFFFF, data - 5, 5);

		// corrupt the CRC if the sector has an address CRC error (RNF + CRC bits set)
		if ((psi.mFDCStatus & 0x18) == 0x00)
			addressCRC = ~addressCRC;

		// write CRC
		VDWriteUnalignedBEU16(data, addressCRC);
		data += 2;
		*clock++ = 0xFF;
		*clock++ = 0xFF;

		// Create Gap II
		//
		// The first byte is critical for the 815 to ensure that the trailing clock bit after the CRC is correct.
		if (mfm) {
			*data++ = 0x4E;
			*clock++ = 0xFF;
		}

		uint32 damOffset = 0;
		if (psi.mRecordedSize) {
			if (mfm) {
				for(int i=0; i<21; ++i)
					*data++ = 0x4E;

				for(int i=0; i<12; ++i)
					*data++ = 0x00;

				for(int i=0; i<33; ++i)
					*clock++ = 0xFF;

				for(int i=0; i<3; ++i) {
					*data++ = 0xA1;
					*clock++ = 0xFB;
				}
			} else {
				for(int i=0; i<11; ++i)
					*data++ = 0xFF;

				for(int i=0; i<6; ++i)
					*data++ = 0x00;

				for(int i=0; i<17; ++i)
					*clock++ = 0xFF;
			}

			// add DAM
			damOffset = (uint32)(data - dataBuf);
			*data++ = 0xF8 + ((psi.mFDCStatus & 0x60) >> 5);
			*clock++ = mfm ? 0xFF : 0xC7;

			// add sector data
			// if the sector is short due to being a boot sector, we need to fill it with $B6, or else
			// 815 format verify will fail
			memset(data, 0xB6, psi.mRecordedSize);

			try {
				diskImage.ReadPhysicalSector(psi.mPhysIndex, data, psi.mRecordedSize);
			} catch(const VDException&) {
			}

			if (invertData) {
				const uint32 invertSize = std::min<uint32>(psi.mRecordedSize, diskImage.GetSectorSize(psi.mVirtIndex));
				for(uint32 i = 0; i < invertSize; ++i)
					data[i] = ~data[i];
			}

			// compute data CRC
			uint16 dataCRC = mfm ? ATComputeCRC16(0xFFFF, data - 4, psi.mRecordedSize + 4) : ATComputeCRC16(0xFFFF, data - 1, psi.mRecordedSize + 1);

			// corrupt data field CRC-16 if sector has CRC error
			if ((psi.mFDCStatus & 0x08) == 0x00)
				dataCRC = ~dataCRC;

			// finish sector data, CRC, and post-byte
			data += psi.mRecordedSize;

			VDWriteUnalignedBEU16(data, dataCRC);
			data += 2;

			*data++ = mfm ? 0x4E : 0xFF;
		}

		memset(clock, 0xFF, psi.mRecordedSize + 3);
		clock += psi.mRecordedSize + 3;

		VDASSERT(clock - clockMaskBuf == data - dataBuf);

		// start position, taking into account the part of Gap III that we added
		const uint32 startRawBitPos = (psi.mBytePos*16 + trackBitLen - (prevGap - 1)*16) % trackBitLen;

		// write the whole thing to the track
		const uint32 sectorByteLen = (uint32)(data - dataBuf);
		if (mfm)
			ATWriteRawMFMToTrack(trackData, startRawBitPos, trackBitLen, clockMaskBuf, dataBuf, sectorByteLen, mfmBuffer);
		else
			ATWriteRawFMToTrack(trackData, startRawBitPos, trackBitLen, clockMaskBuf, dataBuf, sectorByteLen, mfmBuffer);

		// write the data field
		const uint32 endRawBitPos = startRawBitPos + sectorByteLen * 16;

		if (renderedSectors) {
			ATDiskRenderedSectorInfo sri {};
			sri.mSector = psi.mSectorId;
			sri.mVirtualSector = psi.mVirtIndex;
			sri.mPhysicalSector = psi.mPhysIndex;
			sri.mStartBitPos = startRawBitPos;
			sri.mStartDataFieldBitPos = startRawBitPos + damOffset * 16;
			sri.mEndBitPos = endRawBitPos;
			sri.mSectorSizeCode = sectorSizeCode;
			renderedSectors->push_back(sri);
		}

		g_ATLCFDC("Rendered track %u, sector %2u to %u-%u\n", track, psi.mSectorId, startRawBitPos, endRawBitPos);
	}

	// repeat the start of the track after the end so we can easily extract words
	// overlapping the track start
	uint8 *tail = &trackData[trackRawByteLen];
	int tailBitShift = trackBitLen & 7;
	uint32 tailMask = 0xFFFFFFFFU >> tailBitShift;

	VDWriteUnalignedBEU32(tail, (VDReadUnalignedBEU32(tail) & ~tailMask) + (VDReadUnalignedBEU32(trackData) >> tailBitShift));
}
