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

#ifndef f_AT_ATEMULATION_DISKUTILS_H
#define f_AT_ATEMULATION_DISKUTILS_H

#include <vd2/system/vdtypes.h>

class ATLogChannel;
class IATDiskImage;

extern ATLogChannel g_ATLCFDC;
extern ATLogChannel g_ATLCFDCWTData;

void ATInsertTrackBits(uint8* dst, uint32 dstBitOffset, const uint8* src, uint32 srcBitOffset, uint32 numBits);
void ATWriteRawFMToTrack(uint8 *track, uint32 trackOffsetBits, uint32 trackLenBits, const uint8 *clockMaskBits, const uint8 *dataBits, uint32 numBytes, uint8 *fmBuffer);
void ATWriteRawMFMToTrack(uint8 *track, uint32 trackOffsetBits, uint32 trackLenBits, const uint8 *clockMaskBits, const uint8 *dataBits, uint32 numBytes, uint8 *mfmBuffer);

struct ATDiskRenderedSectorInfo {
	uint32 mSector;					// sector ID (normally 1-26)
	uint32 mVirtualSector;			// virtual sector index in disk image
	uint32 mPhysicalSector;			// physical sector index in disk image
	uint32 mStartBitPos;			// starting bit position in raw track for the sector (start of address field)
	uint32 mEndBitPos;				// ending bit position in raw track for the sector (after address/data fields)
	uint32 mStartDataFieldBitPos;	// starting bit position of the data field for the sector
	uint8 mSectorSizeCode;			// sector size encoding (0-3 for 128-1024).
};

// Render sectors to a raw disk track composed of interleaved clock and data bits.
//
// trackData:
//		Pointer to buffer to hold raw bits. This must hold at least enough space to contain the specified
//		number of bits, +4 bytes for wraparound. The first 32 bits are appended after the track to make
//		it easier to extract a bit window across the index.
//
// trackBitLen:
//		The length of the track, in raw bit cells, counting both data and clock cells. The track buffer
//		must be at least (trackBitLen + 7 + 32)/8 bytes.
//
// renderedSectors:
//		Receives information about which sectors were rendered where in the track, to make it easier
//		to correlate sector writes (optional). This is used by the 815 emulation to match a raw write of
//		sector data to the actual sector.
//
// diskImage:
//		Disk image to source sectors.
//
// track:
//		Track number (0-79).
//
// mfm:
//		True to include only MFM sectors, false to include only FM sectors.
//
// highDensity:
//		True to include only high density sectors, false to include normal density sectors.
//
// invertData:
//		True to invert sector data as standard Atari drives do, false to leave data uninverted (815).
//
void ATDiskRenderRawTrack(uint8 *trackData, size_t trackBitLen, vdfastvector<ATDiskRenderedSectorInfo> *renderedSectors, IATDiskImage& diskImage, uint32 track, bool mfm, bool highDensity, bool invertData);

#endif
