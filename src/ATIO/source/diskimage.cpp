//	Altirra - Atari 800/800XL/5200 emulator
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

#include "stdafx.h"
#include <vd2/system/binary.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/math.h>
#include <vd2/system/strutil.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/logging.h>
#include <at/atio/diskimage.h>
#include <at/atio/diskfs.h>

ATLogChannel g_ATLCDiskImage(false, false, "DISKIMAGE", "Disk image load details");

namespace {
	// The bit cell rate is 1MHz.
	static const int kBytesPerTrack = 26042;

	static const int kTrackInterleave18[18]={
		0, 9, 1, 10, 2, 11, 3, 12, 4, 13, 5, 14, 6, 15, 7, 16, 8, 17
	};

	static const int kTrackInterleaveDD[18]={
		15, 12, 9, 6, 3, 0, 16, 13, 10, 7, 4, 1, 17, 14, 11, 8, 5, 2
	};

	static const int kTrackInterleave26[26]={
		0, 13, 1, 14, 2, 15, 3, 16, 4, 17, 5, 18, 6, 19, 7, 20, 8, 21, 9, 22, 10, 23, 11, 24, 12, 25
	};

	struct ATDCMPassHeader {
		uint8 mArchiveType;
		uint8 mPassInfo;
		uint8 mSectorLo;
		uint8 mSectorHi;
	};

	enum ATDCMDiskType {
		kATDCMDiskType_SD,
		kATDCMDiskType_DD,
		kATDCMDiskType_ED
	};

	struct ATXHeader {
		uint8	mSignature[4];			// AT8X
		uint16	mVersionMajor;			// 1
		uint16	mVersionMinor;			// 1
		uint16	mFlags;					// 0x0002
		uint16	mMysteryDataCount;
		uint8	mFill1[16];
		uint32	mTrackDataOffset;
		uint32	mTotalSize;
		uint8	mFill2[12];
	};

	struct ATXTrackHeader {
		uint32	mSize;
		uint16	mType;
		uint16	mReserved06;
		uint8	mTrackNum;
		uint8	mReserved09;
		uint16	mNumSectors;
		uint8	mFill2[5];
		uint8	mMysteryIndex;
		uint8	mFill3[2];
		uint32	mDataOffset;
		uint8	mFill4[8];
	};

	struct ATXSectorHeader {
		uint8	mIndex;
		uint8	mFDCStatus;		// not inverted
		uint16	mTimingOffset;
		uint32	mDataOffset;
	};

	struct ATXTrackChunkHeader {
		enum {
			kTypeSectorData = 0x00,
			kTypeSectorList = 0x01,
			kTypeWeakBits = 0x10
		};

		uint32	mSize;
		uint8	mType;
		uint8	mNum;
		uint16	mData;
	};
}

class ATDiskImage : public IATDiskImage {
public:
	ATDiskImage();

	void Init(uint32 sectorCount, uint32 bootSectorCount, uint32 sectorSize);
	void Load(const wchar_t *s);
	void Load(const wchar_t *origPath, const wchar_t *imagePath, IVDRandomAccessStream& stream);

	ATDiskTimingMode GetTimingMode() const;

	bool IsDirty() const;
	bool IsUpdatable() const;
	bool IsDynamic() const { return false; }
	ATDiskImageFormat GetImageFormat() const override { return mImageFormat; }

	bool Flush();

	void SetPath(const wchar_t *path);
	void Save(const wchar_t *path, ATDiskImageFormat format);

	ATDiskGeometryInfo GetGeometry() const { return mGeometry; }
	uint32 GetSectorSize() const { return mSectorSize; }
	uint32 GetSectorSize(uint32 virtIndex) const { return virtIndex < mBootSectorCount ? 128 : mSectorSize; }
	uint32 GetBootSectorCount() const { return mBootSectorCount; }

	uint32 GetPhysicalSectorCount() const;
	void GetPhysicalSectorInfo(uint32 index, ATDiskPhysicalSectorInfo& info) const;

	void ReadPhysicalSector(uint32 index, void *data, uint32 len);
	void WritePhysicalSector(uint32 index, const void *data, uint32 len);

	uint32 GetVirtualSectorCount() const;
	void GetVirtualSectorInfo(uint32 index, ATDiskVirtualSectorInfo& info) const;

	uint32 ReadVirtualSector(uint32 index, void *data, uint32 len);
	bool WriteVirtualSector(uint32 index, const void *data, uint32 len);

protected:
	typedef ATDiskVirtualSectorInfo VirtSectorInfo;
	typedef ATDiskPhysicalSectorInfo PhysSectorInfo;
	typedef vdfastvector<PhysSectorInfo> PhysSectors;
	typedef vdfastvector<VirtSectorInfo> VirtSectors;

	void LoadXFD(IVDRandomAccessStream& stream, sint64 fileSize);
	void LoadDCM(IVDRandomAccessStream& stream, uint32 len, const wchar_t *origPath, const uint8 *header);
	void LoadATX(IVDRandomAccessStream& stream, uint32 len, const uint8 *header);
	void LoadP2(IVDRandomAccessStream& stream, uint32 len, const uint8 *header);
	void LoadP3(IVDRandomAccessStream& stream, uint32 len, const uint8 *header);
	void LoadATR(IVDRandomAccessStream& stream, uint32 len, const wchar_t *origPath, const uint8 *header);
	void LoadARC(IVDRandomAccessStream& stream, const wchar_t *origPath);
	void ComputeGeometry();

	void SaveATR(VDFile& f, PhysSectors& phySecs);
	void SaveXFD(VDFile& f, PhysSectors& phySecs);
	void SaveP2(VDFile& f, PhysSectors& phySecs);
	void SaveP3(VDFile& f, PhysSectors& phySecs);
	void SaveDCM(VDFile& f, PhysSectors& phySecs);
	void SaveATX(VDFile& f, PhysSectors& phySecs);

	uint32	mBootSectorCount;
	uint32	mSectorSize;
	uint32	mSectorsPerTrack;

	ATDiskImageFormat mImageFormat;
	bool	mbDirty;
	bool	mbDiskFormatDirty;
	bool	mbHasDiskSource;
	ATDiskTimingMode	mTimingMode;
	ATDiskGeometryInfo	mGeometry;

	VDStringW	mPath;

	PhysSectors mPhysSectors;
	VirtSectors mVirtSectors;
	vdfastvector<uint8>		mImage;
};

ATDiskImage::ATDiskImage()
	: mBootSectorCount()
	, mSectorSize()
	, mSectorsPerTrack()
	, mImageFormat()
	, mbDirty()
	, mbDiskFormatDirty()
	, mbHasDiskSource()
	, mTimingMode()
	, mGeometry()
	, mPath()
	, mPhysSectors()
	, mVirtSectors()
	, mImage()
{
}

void ATDiskImage::Init(uint32 sectorCount, uint32 bootSectorCount, uint32 sectorSize) {
	mBootSectorCount = bootSectorCount;
	mSectorSize = sectorSize;

	mImage.clear();
	mImage.resize(128 * bootSectorCount + sectorSize * (sectorCount - bootSectorCount), 0);

	mPhysSectors.resize(sectorCount);
	mVirtSectors.resize(sectorCount);

	ComputeGeometry();

	for(uint32 i=0; i<sectorCount; ++i) {
		PhysSectorInfo& psi = mPhysSectors[i];
		VirtSectorInfo& vsi = mVirtSectors[i];

		vsi.mStartPhysSector = i;
		vsi.mNumPhysSectors = 1;

		psi.mOffset		= i < mBootSectorCount ? 128*i : 128*mBootSectorCount + mSectorSize*(i-mBootSectorCount);
		psi.mDiskOffset= -1;
		psi.mSize		= i < mBootSectorCount ? 128 : + mSectorSize;
		psi.mbDirty		= true;
		psi.mRotPos		= mSectorSize >= 256 ? (float)kTrackInterleaveDD[i % 18] / 18.0f
			: mSectorsPerTrack >= 26 ? (float)kTrackInterleave26[i % 26] / 26.0f
			: (float)kTrackInterleave18[i % 18] / 18.0f;
		psi.mFDCStatus	= 0xFF;
		psi.mWeakDataOffset = -1;
	}

	mbDirty = true;
	mbDiskFormatDirty = true;

	mPath = L"(New disk)";
	mbHasDiskSource = false;
}

void ATDiskImage::Load(const wchar_t *s) {
	VDFileStream f(s);

	Load(s, s, f);
}

void ATDiskImage::Load(const wchar_t *origPath, const wchar_t *imagePath, IVDRandomAccessStream& stream) {
	sint64 fileSize = stream.Length();

	if (!vdwcsicmp(VDFileSplitExt(imagePath), L".arc")) {
		LoadARC(stream, origPath);
	} else if (fileSize <= 65535 * 128 && imagePath && !vdwcsicmp(VDFileSplitExt(imagePath), L".xfd")) {
		LoadXFD(stream, fileSize);
	} else {
		
		uint8 header[16];
		stream.Read(header, 16);

		sint32 len = VDClampToSint32(stream.Length()) - 16;
		mImage.resize(len);
		stream.Read(mImage.data(), len);

		mTimingMode = kATDiskTimingMode_Any;

		if (header[0] == 0xF9 || header[0] == 0xFA) {
			LoadDCM(stream, len, origPath, header);
		} else if (header[0] == 'A' && header[1] == 'T' && header[2] == '8' && header[3] == 'X') {
			LoadATX(stream, len, header);
		} else if (header[2] == 'P' && header[3] == '2') {
			LoadP2(stream, len, header);
		} else if (header[2] == 'P' && header[3] == '3') {
			LoadP3(stream, len, header);
		} else if (header[0] == 0x96 && header[1] == 0x02) {
			LoadATR(stream, len, origPath, header);
		} else {
			// Hmm. Okay, we don't recognize the signature on this file. Let's
			// check if it is a multiple of 128 bytes. If so, assume it's a headerless
			// XFD image. There are ".ATR" files stored this way. :-/
			if (!(fileSize & 127)) {
				stream.Seek(0);
				LoadXFD(stream, fileSize);
			} else if (origPath)
				throw MyError("Disk image \"%ls\" is corrupt or uses an unsupported format.", VDFileSplitPath(origPath));
			else
				throw MyError("Disk image is corrupt or uses an unsupported format.");
		}
	}

	if (origPath)
		mPath = origPath;
	else
		mPath.clear();

	ComputeGeometry();
	mbDirty = false;
	mbDiskFormatDirty = false;
	mbHasDiskSource = true;
}

class ATInvalidDiskFormatException : public MyError {
public:
	ATInvalidDiskFormatException(const wchar_t *path) {
		if (path)
			setf("Disk image \"%ls\" is corrupt or uses an unsupported format.", VDFileSplitPath(path));
		else
			setf("Disk image is corrupt or uses an unsupported format.");
	}
};

void ATDiskImage::LoadXFD(IVDRandomAccessStream& stream, sint64 fileSize) {
	sint32 len = (sint32)fileSize;

	mImage.resize(len);
	stream.Read(mImage.data(), len);

	mBootSectorCount = 3;
	mSectorSize = 128;
	mImageFormat = kATDiskImageFormat_XFD;

	const int sectorCount = len >> 7;
	mPhysSectors.resize(sectorCount);
	mVirtSectors.resize(sectorCount);

	for(int i=0; i<sectorCount; ++i) {
		PhysSectorInfo& psi = mPhysSectors[i];
		VirtSectorInfo& vsi = mVirtSectors[i];

		vsi.mStartPhysSector = i;
		vsi.mNumPhysSectors = 1;

		psi.mOffset		= 128*i;
		psi.mDiskOffset= -1;
		psi.mSize		= 128;
		psi.mFDCStatus	= 0xFF;
		psi.mRotPos		= (float)kTrackInterleave18[i % 18] / 18.0f;
		psi.mWeakDataOffset = -1;
		psi.mbDirty		= false;
	}
}

void ATDiskImage::LoadDCM(IVDRandomAccessStream& stream, uint32 len, const wchar_t *origPath, const uint8 *header) {
	stream.Seek(0);
	mImage.clear();

	// read passes
	uint8 sectorBuffer[256] = {0};

	VirtSectorInfo dummySector;
	dummySector.mNumPhysSectors = 0;
	dummySector.mStartPhysSector = 0;

	uint32 mainSectorSize = 128;
	uint32 mainSectorCount = 0;

	ATDCMDiskType diskType = kATDCMDiskType_SD;

	for(;;) {
		ATDCMPassHeader passHeader;

		stream.Read(&passHeader, sizeof(ATDCMPassHeader));

		if (passHeader.mArchiveType != 0xF9 && passHeader.mArchiveType != 0xFA)
			throw ATInvalidDiskFormatException(origPath);

		uint32 sectorSize = (passHeader.mPassInfo & 0x60) == 0x20 ? 256 : 128;
		uint32 sectorNum = passHeader.mSectorLo + 256*passHeader.mSectorHi;

		switch(passHeader.mPassInfo & 0x60) {
			case 0x00:
				mainSectorCount = 720;
				diskType = kATDCMDiskType_SD;
				break;
			case 0x40:
				mainSectorCount = 1040;
				diskType = kATDCMDiskType_ED;
				break;
			case 0x20:
				mainSectorCount = 720;
				diskType = kATDCMDiskType_DD;
				break;
		}

		mainSectorSize = sectorSize;

		for(;;) {
			uint8 contentType;
			stream.Read(&contentType, 1);

			if ((contentType & 0x7F) == 0x45)
				break;

			// This check has to be after the 0x45 token check, as it's legal to have a sector number of
			// 0 before it.
			if (!sectorNum)
				throw ATInvalidDiskFormatException(origPath);

			uint8 c;
			switch(contentType & 0x7F) {
				case 0x41:		// modify begin
					stream.Read(&c, 1);
					stream.Read(sectorBuffer, c + 1);
					for(uint32 i=0, j=c; i < j; ++i, --j) {
						std::swap(sectorBuffer[i], sectorBuffer[j]);
					}
					break;
				case 0x42:		// 128 byte DOS sector
					stream.Read(sectorBuffer + 123, 5);
					memset(sectorBuffer, sectorBuffer[123], 123);
					break;
				case 0x43:		// compressed sector
					{
						uint32 pos = 0;
						uint8 offset;
						bool compressed = false;
						bool first = true;
						while(pos < sectorSize) {
							stream.Read(&offset, 1);

							// offset cannot exceed sectorSize, ever.
							if (offset > sectorSize)
								throw ATInvalidDiskFormatException(origPath);

							// offset cannot go backwards, except in two specific cases:
							//  - offset is 0, pos is 0 (null span)
							//  - offset is 0, sectorSize is 256 (fill to end of DD sector)
							if (offset < pos && (offset || (pos && sectorSize != 256)))
								throw ATInvalidDiskFormatException(origPath);

							uint32 spanLen = (offset || first ? offset : sectorSize) - pos;
							if (compressed) {
								stream.Read(&c, 1);
								memset(sectorBuffer + pos, c, spanLen);
							} else if (spanLen) {
								stream.Read(sectorBuffer + pos, spanLen);
							}

							pos += spanLen;
							compressed = !compressed;
							first = false;
						}
					}
					break;
				case 0x44:		// modify end
					stream.Read(&c, 1);
					if (c >= sectorSize)
						throw ATInvalidDiskFormatException(origPath);
					stream.Read(sectorBuffer + c, sectorSize - c);
					break;
				case 0x46:		// repeat last sector
					break;
				case 0x47:		// uncompressed sector
					stream.Read(sectorBuffer, sectorSize);
					break;

				default:
					throw ATInvalidDiskFormatException(origPath);
			}

			// create entry for sector
			if (mVirtSectors.size() < sectorNum)
				mVirtSectors.resize(sectorNum, dummySector);

			VirtSectorInfo& vsi = mVirtSectors[sectorNum - 1];
			vsi.mNumPhysSectors = 1;
			vsi.mStartPhysSector = (uint32)mPhysSectors.size();

			PhysSectorInfo& psi =  mPhysSectors.push_back();
			psi.mOffset = (uint32)mImage.size();
			psi.mDiskOffset = -1;
			psi.mSize = sectorNum <= 3 ? 128 : sectorSize;
			psi.mRotPos = 0;
			psi.mFDCStatus = 0xFF;
			psi.mWeakDataOffset = -1;
			psi.mbDirty = false;

			switch(diskType) {
				case kATDCMDiskType_SD:
					psi.mRotPos = (float)kTrackInterleave18[(sectorNum - 1) % 18] / 18.0f;
					break;

				case kATDCMDiskType_DD:
					psi.mRotPos = (float)kTrackInterleaveDD[(sectorNum - 1) % 18] / 18.0f;
					break;

				case kATDCMDiskType_ED:
					psi.mRotPos = (float)kTrackInterleave26[(sectorNum - 1) % 26] / 26.0f;
					break;
			}

			mImage.insert(mImage.end(), sectorBuffer, sectorBuffer + psi.mSize);

			// increment sector number if sequential flag is set, else read new sector number
			if (contentType & 0x80) {
				++sectorNum;
			} else {
				uint8 newSec[2];
				stream.Read(newSec, 2);
				sectorNum = VDReadUnalignedLEU16(newSec);
			}
		}

		if (passHeader.mPassInfo & 0x80)
			break;
	}

	// fill in any missing sectors
	if (mVirtSectors.size() < mainSectorCount)
		mVirtSectors.resize(mainSectorCount, dummySector);

	for(VirtSectors::iterator it(mVirtSectors.begin()), itEnd(mVirtSectors.end()); it != itEnd; ++it) {
		VirtSectorInfo& vsi = *it;

		if (!vsi.mNumPhysSectors) {
			vsi.mNumPhysSectors = 1;
			vsi.mStartPhysSector = (uint32)mPhysSectors.size();

			uint32 secNum = (uint32)(it - mVirtSectors.begin()) + 1;

			PhysSectorInfo& psi =  mPhysSectors.push_back();
			psi.mOffset = (uint32)mImage.size();
			psi.mDiskOffset = -1;
			psi.mSize = secNum <= 3 ? 128 : mainSectorSize;
			psi.mFDCStatus = 0xFF;
			psi.mWeakDataOffset = -1;
			psi.mbDirty = false;

			switch(diskType) {
				case kATDCMDiskType_SD:
					psi.mRotPos = (float)kTrackInterleave18[(secNum - 1) % 18] / 18.0f;
					break;

				case kATDCMDiskType_DD:
					psi.mRotPos = (float)kTrackInterleaveDD[(secNum - 1) % 18] / 18.0f;
					break;

				case kATDCMDiskType_ED:
					psi.mRotPos = (float)kTrackInterleave26[(secNum - 1) % 26] / 26.0f;
					break;
			}

			mImage.resize(mImage.size() + psi.mSize, 0);
		}
	}

	mBootSectorCount = 3;
	mSectorSize = mainSectorSize;
	mImageFormat = kATDiskImageFormat_DCM;
}

void ATDiskImage::LoadATX(IVDRandomAccessStream& stream, uint32 len, const uint8 *header) {
	ATXHeader atxhdr;
	stream.Seek(0);
	stream.Read(&atxhdr, sizeof atxhdr);

	stream.Seek(atxhdr.mTrackDataOffset);

	mImage.clear();
	mBootSectorCount = 3;
	mSectorSize = 128;

	sint64 imageSize = stream.Length();
	sint32 imageSize32 = (sint32)imageSize;

	if (imageSize != imageSize32)
		throw MyError("Invalid ATX image: file exceeds 2GB in size.");

	vdblock<uint8> trackBuf;
	vdfastvector<ATXSectorHeader> sectorHeaders;
	vdfastvector<int> phySectorLookup;
	for(uint32 i=0; i<40; ++i) {
		ATXTrackHeader trkhdr;
		sint32 trackBaseOffset = (sint32)stream.Pos();

		stream.Read(&trkhdr, sizeof trkhdr);

		// validate track
		if (trackBaseOffset + trkhdr.mSize > imageSize)
			throw MyError("Invalid ATX image: Chunk at %08x extends beyond end of file.", (uint32)trackBaseOffset);

		if (trkhdr.mSize < sizeof trkhdr)
			throw MyError("Invalid ATX image: Track header at %08x has invalid size.", (uint32)trackBaseOffset);

		if (trkhdr.mType != 0)
			throw MyError("Invalid ATX image: Track header at %08x has the wrong type.", (uint32)trackBaseOffset);

		// read in the entire track chunk
		if (trackBuf.size() < trkhdr.mSize)
			trackBuf.resize(trkhdr.mSize);

		memcpy(trackBuf.data(), &trkhdr, sizeof trkhdr);
		stream.Read(trackBuf.data() + sizeof trkhdr, trkhdr.mSize - sizeof trkhdr);

		// validate the chunk list and pull out the sector list
		uint32 sectorDataStart = 0;
		uint32 sectorDataLen = 0;

		sectorHeaders.resize(trkhdr.mNumSectors);
		bool foundSectorList = false;

		for(uint32 chunkOffset = trkhdr.mDataOffset;;) {
			if (trkhdr.mSize - chunkOffset < sizeof(ATXTrackChunkHeader))
				throw MyError("Invalid ATX image: Unterminated chunk chain.");

			uint32 chunkSize = VDReadUnalignedLEU32(&trackBuf[chunkOffset]);

			if (!chunkSize) {
				// If we don't have a sector data chunk, assume that all remaining data can be sector data.
				// The Wizard and the Princess has this issue.
				if (!sectorDataLen) {
					sectorDataStart = chunkOffset;
					sectorDataLen = trkhdr.mSize - chunkOffset;
				}
				break;
			}

			if (chunkOffset + chunkSize > trkhdr.mSize)
				throw MyError("Invalid ATX image: Chunk at %08x extends outside of track at %08x.", (uint32)chunkOffset + (uint32)trackBaseOffset, (uint32)trackBaseOffset);

			if (chunkSize < sizeof(ATXTrackChunkHeader))
				throw MyError("Invalid ATX image: Chunk at %08x has invalid size.", (uint32)chunkOffset + (uint32)trackBaseOffset);

			ATXTrackChunkHeader ch;
			memcpy(&ch, &trackBuf[chunkOffset], sizeof ch);

			if (ch.mType == ATXTrackChunkHeader::kTypeSectorData) {
				sectorDataStart = chunkOffset + sizeof(ATXTrackChunkHeader);
				sectorDataLen = chunkSize - sizeof(ATXTrackChunkHeader);
			} else if (ch.mType == ATXTrackChunkHeader::kTypeSectorList) {
				if (chunkSize < sizeof(ATXTrackChunkHeader) + sizeof(ATXSectorHeader) * trkhdr.mNumSectors)
					throw MyError("Invalid ATX image: Sector list at %08x has size %08x insufficient to hold %u sectors.", (uint32)chunkOffset + (uint32)trackBaseOffset, chunkSize, trkhdr.mNumSectors);

				foundSectorList = true;
				memcpy(sectorHeaders.data(), &trackBuf[chunkOffset] + sizeof(ATXTrackChunkHeader), sizeof(sectorHeaders[0])*sectorHeaders.size());
			}

			chunkOffset += chunkSize;
		}

		if (!foundSectorList)
			throw MyError("Invalid ATX image: Track at %08x does not have a sector list.", (uint32)trackBaseOffset);

		phySectorLookup.clear();
		phySectorLookup.resize(trkhdr.mNumSectors, -1);

		uint32 sectorsWithExtraData = 0;
		for(uint32 j=0; j<18; ++j) {
			mVirtSectors.push_back();
			VirtSectorInfo& vsi = mVirtSectors.back();

			vsi.mStartPhysSector = (uint32)mPhysSectors.size();
			vsi.mNumPhysSectors = 0;

			for(uint32 k=0; k<trkhdr.mNumSectors; ++k) {
				const ATXSectorHeader& sechdr = sectorHeaders[k];

				if (sechdr.mIndex != j + 1)
					continue;

				if (sechdr.mFDCStatus & 0x40)
					++sectorsWithExtraData;

				phySectorLookup[k] = (int)mPhysSectors.size();

				mPhysSectors.push_back();
				PhysSectorInfo& psi = mPhysSectors.back();

				psi.mFDCStatus = ~sechdr.mFDCStatus | 0xc0;		// purpose of bit 7 is unknown
				psi.mOffset = (sint32)mImage.size();
				psi.mDiskOffset = trackBaseOffset + sechdr.mDataOffset;
				psi.mSize = sechdr.mFDCStatus & 0x10 ? 0 : 128;
				psi.mRotPos = (float)sechdr.mTimingOffset / (float)kBytesPerTrack;
				psi.mWeakDataOffset = -1;
				psi.mbDirty = false;
				++vsi.mNumPhysSectors;

				g_ATLCDiskImage("Track %d, sector %d | pos %4.2f%s%s%s%s%s\n"
					, i
					, sechdr.mIndex
					, psi.mRotPos
					, psi.mFDCStatus & 0x20 ? "" : " deleted"
					, psi.mFDCStatus & 0x10 ? "" : " missing"
					, psi.mFDCStatus & 0x08 ? "" : " CRCerror"
					, psi.mFDCStatus & 0x04 ? "" : " long"
					, psi.mWeakDataOffset >= 0 ? " weak" : ""
					);

				// Missing sectors do not have data.
				if (!(sechdr.mFDCStatus & 0x10)) {
					uint32 dataEnd = sechdr.mDataOffset + 128;

					if (sechdr.mDataOffset < sectorDataStart || (dataEnd - sectorDataStart) > sectorDataLen)
						throw MyError("Invalid protected disk: sector extends outside of sector data region.");

					mImage.insert(mImage.end(), &trackBuf[sechdr.mDataOffset], &trackBuf[sechdr.mDataOffset] + 128);
				}
			}
		}

		// process extra sector data
		for(uint32 chunkOffset = trkhdr.mDataOffset;;) {
			uint32 chunkSize = VDReadUnalignedLEU32(&trackBuf[chunkOffset]);

			if (!chunkSize)
				break;

			ATXTrackChunkHeader ch;
			memcpy(&ch, &trackBuf[chunkOffset], sizeof ch);

			if (ch.mType == ATXTrackChunkHeader::kTypeWeakBits) {
				if (ch.mNum >= trkhdr.mNumSectors)
					throw MyError("Invalid ATX image: extra track data chunk at %08x has invalid sector number.", (uint32)(trackBaseOffset + chunkOffset));

				int phyIndex = phySectorLookup[ch.mNum];
				if (phyIndex < 0) {
					VDASSERT(phyIndex >= 0);
					continue;
				}

				PhysSectorInfo& psi = mPhysSectors[phyIndex];
				uint16 weakOffset = VDFromLE16(ch.mData);

				if (weakOffset < psi.mSize) {
					psi.mWeakDataOffset = weakOffset;

					g_ATLCDiskImage("Sector index %d -> weak offset %d\n"
						, phyIndex
						, weakOffset
						);
				}
			}

			chunkOffset += chunkSize;
		}
	}

	mTimingMode = kATDiskTimingMode_UsePrecise;
	mImageFormat = kATDiskImageFormat_ATX;
}

void ATDiskImage::LoadP2(IVDRandomAccessStream& stream, uint32 len, const uint8 *header) {
	mBootSectorCount = 3;
	mSectorSize = 128;
	mImageFormat = kATDiskImageFormat_P2;
	mTimingMode = kATDiskTimingMode_UseOrdered;

	int sectorCount = VDReadUnalignedBEU16(&header[0]);

	g_ATLCDiskImage("PRO header: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n"
		, header[0]
		, header[1]
		, header[2]
		, header[3]
		, header[4]
		, header[5]
		, header[6]
		, header[7]
		, header[8]
		, header[9]
		, header[10]
		, header[11]
		, header[12]
		, header[13]
		, header[14]
		, header[15]);

	// read sector headers
	for(int i=0; i<sectorCount; ++i) {
		const uint8 *sectorhdr = &mImage[(128+12) * i];

		g_ATLCDiskImage("Sector %03d: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n"
			, i + 1
			, sectorhdr[0]
			, sectorhdr[1]
			, sectorhdr[2]
			, sectorhdr[3]
			, sectorhdr[4]
			, sectorhdr[5]
			, sectorhdr[6]
			, sectorhdr[7]
			, sectorhdr[8]
			, sectorhdr[9]
			, sectorhdr[10]
			, sectorhdr[11]);

		mPhysSectors.push_back();
		PhysSectorInfo& psi = mPhysSectors.back();

		psi.mOffset		= (128+12)*i+12;
		psi.mDiskOffset = psi.mOffset + 16;
		psi.mSize		= 128;
		psi.mFDCStatus	= sectorhdr[1];
		psi.mRotPos		= (float)kTrackInterleave18[i % 18] / 18.0f;
		psi.mWeakDataOffset = -1;
		psi.mbDirty		= false;

		if (!(psi.mFDCStatus & 0x10)) {
			psi.mSize = 0;
		}

		mVirtSectors.push_back();
		VirtSectorInfo& vsi = mVirtSectors.back();

		vsi.mStartPhysSector = (uint32)mPhysSectors.size() - 1;
		vsi.mNumPhysSectors = 1;

		uint16 phantomSectorCount = sectorhdr[5];
		if (phantomSectorCount) {
			vsi.mNumPhysSectors = phantomSectorCount + 1;

			sectorCount -= phantomSectorCount;

			for(uint32 j=0; j<phantomSectorCount; ++j) {
				uint32 phantomSectorOffset = sectorCount + sectorhdr[7 + j] - 1;
				uint32 phantomSectorByteOffset = (128+12) * phantomSectorOffset;
				if (phantomSectorByteOffset + 128 > mImage.size())
					throw MyError("Invalid protected disk.");
				const uint8 *sectorhdr2 = &mImage[phantomSectorByteOffset];

				mPhysSectors.push_back();
				PhysSectorInfo& psi = mPhysSectors.back();

				psi.mOffset		= phantomSectorByteOffset+12;
				psi.mDiskOffset = psi.mOffset + 16;
				psi.mSize		= 128;
				psi.mFDCStatus	= sectorhdr2[1];
				psi.mRotPos		= (float)kTrackInterleave18[i % 18] / 18.0f + (1.0f / ((float)phantomSectorCount + 1)) * (j+1);
				psi.mWeakDataOffset = -1;
				psi.mbDirty		= false;

				if (!(psi.mFDCStatus & 0x10)) {
					psi.mSize = 0;
				}
			}
		}
	}
}

void ATDiskImage::LoadP3(IVDRandomAccessStream& stream, uint32 len, const uint8 *header) {
	g_ATLCDiskImage("PRO header: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n"
		, header[0]
		, header[1]
		, header[2]
		, header[3]
		, header[4]
		, header[5]
		, header[6]
		, header[7]
		, header[8]
		, header[9]
		, header[10]
		, header[11]
		, header[12]
		, header[13]
		, header[14]
		, header[15]);

	mBootSectorCount = 3;
	mSectorSize = 128;
	mImageFormat = kATDiskImageFormat_P3;
	mTimingMode = kATDiskTimingMode_UseOrdered;

	uint32 sectorCount = VDReadUnalignedBEU16(&header[6]);

	// read sector headers
	for(uint32 i=0; i<sectorCount; ++i) {
		const uint8 *sectorhdr = &mImage[(128+12) * i];

		g_ATLCDiskImage("Sector %03d: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n"
			, i + 1
			, sectorhdr[0]
			, sectorhdr[1]
			, sectorhdr[2]
			, sectorhdr[3]
			, sectorhdr[4]
			, sectorhdr[5]
			, sectorhdr[6]
			, sectorhdr[7]
			, sectorhdr[8]
			, sectorhdr[9]
			, sectorhdr[10]
			, sectorhdr[11]);

		uint32 phantomSectorCount = sectorhdr[5];

		float rotationalPosition = (float)kTrackInterleave18[i % 18] / 18.0f; 
		float rotationalIncrement = phantomSectorCount ? (1.0f / (int)phantomSectorCount) : 0.0f;

		mVirtSectors.push_back();
		VirtSectorInfo& vsi = mVirtSectors.back();

		vsi.mStartPhysSector = (uint32)mPhysSectors.size();
		vsi.mNumPhysSectors = phantomSectorCount + 1;

		for(uint32 j=0; j<=phantomSectorCount; ++j) {
			uint8 idx = sectorhdr[6 + j];

			uint32 phantomSectorOffset = idx ? sectorCount + idx - 1 : i;
			uint32 phantomSectorByteOffset = (128+12) * phantomSectorOffset;
			if (phantomSectorByteOffset + 128 > mImage.size())
				throw MyError("Invalid protected disk.");
			const uint8 *sectorhdr2 = &mImage[phantomSectorByteOffset];

			mPhysSectors.push_back();
			PhysSectorInfo& psi = mPhysSectors.back();

			psi.mOffset		= phantomSectorByteOffset+12;
			psi.mDiskOffset = psi.mOffset + 16;
			psi.mSize		= 128;
			psi.mFDCStatus	= sectorhdr2[1];
			psi.mRotPos		= rotationalPosition;
			psi.mWeakDataOffset = -1;
			psi.mbDirty		= false;

			if (!(psi.mFDCStatus & 0x10)) {
				psi.mSize = 0;
			}

			rotationalPosition += rotationalIncrement;
		}
	}
}

void ATDiskImage::LoadATR(IVDRandomAccessStream& stream, uint32 len, const wchar_t *origPath, const uint8 *header) {
	mSectorSize = header[4] + 256*header[5];

	uint32 imageBootSectorCount = 0;
	bool packedBootSectors = false;

	if (mSectorSize <= 256) {
		mBootSectorCount = 3;
		imageBootSectorCount = 3;

		// Check if this is a FUBARed DD disk where the boot sectors are 256 bytes.
		// We assume this is the case if the paragraphs count works out for that.
		if (mSectorSize == 256) {
			uint32 headerParas = header[2] + 256*header[3];

			if (!(headerParas & 0x0f)) {
				imageBootSectorCount = 0;

				// Okay, now we need to check for REALLY screwed up images where the
				// first three sectors are stored back to back, followed by a 192 byte
				// section of nulls.
				bool slotTwoEmpty = true;

				for(int i=0; i<128; ++i) {
					if (mImage[16 + 128 + i]) {
						slotTwoEmpty = false;
						break;
					}
				}

				bool slotFiveEmpty = true;

				for(int i=0; i<128; ++i) {
					if (mImage[16 + 128*4 + i]) {
						slotFiveEmpty = false;
						break;
					}
				}

				if (!slotTwoEmpty && slotFiveEmpty)
					packedBootSectors = true;
			}
		}
	} else
		mBootSectorCount = 0;

	if (mSectorSize > 8192) {
		if (origPath)
			throw MyError("Disk image \"%ls\" uses an unsupported sector size of %u bytes.", VDFileSplitPath(origPath), mSectorSize);
		else
			throw MyError("Disk image uses an unsupported sector size of %u bytes.", mSectorSize);
	}

	mImageFormat = kATDiskImageFormat_ATR;
	
	if (len < 128*imageBootSectorCount) {
		imageBootSectorCount = len >> 7;
		if (mBootSectorCount > imageBootSectorCount)
			mBootSectorCount = imageBootSectorCount;
	}

	uint32 sectorCount = (len - 128*imageBootSectorCount) / mSectorSize + imageBootSectorCount;

	mPhysSectors.resize(sectorCount);
	mVirtSectors.resize(sectorCount);

	ComputeGeometry();	// needed earlier for interleave

	for(uint32 i=0; i<sectorCount; ++i) {
		PhysSectorInfo& psi = mPhysSectors[i];
		VirtSectorInfo& vsi = mVirtSectors[i];

		vsi.mStartPhysSector = i;
		vsi.mNumPhysSectors = 1;

		if (packedBootSectors && i < 3) {
			psi.mOffset		= 128*i;
		} else {
			psi.mOffset		= i < imageBootSectorCount ? 128*i : 128*imageBootSectorCount + mSectorSize*(i-imageBootSectorCount);
		}

		psi.mDiskOffset = psi.mOffset + 16;
		psi.mSize		= i < mBootSectorCount ? 128 : mSectorSize;
		psi.mFDCStatus	= 0xFF;
		psi.mRotPos		= mSectorSize >= 256 ? (float)kTrackInterleaveDD[i % 18] / 18.0f
						: mSectorsPerTrack >= 26 ? (float)kTrackInterleave26[i % 26] / 26.0f
						: (float)kTrackInterleave18[i % 18] / 18.0f;
		psi.mWeakDataOffset = -1;
		psi.mbDirty		= false;
	}
}

void ATDiskImage::LoadARC(IVDRandomAccessStream& stream, const wchar_t *origPath) {
	// mount the ARC file system
	vdautoptr<IATDiskFS> arcfs(ATDiskMountImageARC(stream, origPath));

	// iterate over all files and get an estimate of how much disk space we need:
	//
	// - data sectors
	// - map sectors (1 map sector per 60 data sectors in SD)
	// - bitmap sectors (1 sector per 1024 total sectors)
	// - 3 superblock/bootblock sectors
	// - directory sectors (23 bytes for root entry and for each file)

	uint32 dataSectors = 0;
	uint32 mapSectors = 0;
	uint32 fileCount = 0;

	ATDiskFSEntryInfo entryInfo;
	uintptr fh = arcfs->FindFirst(0, entryInfo);
	try {
		do {
			uint32 size = entryInfo.mBytes;
			uint32 secs = ((size + 127) >> 7);

			dataSectors += secs;
			mapSectors += (secs + 59) / 60;
			++fileCount;
		} while(arcfs->FindNext(fh, entryInfo));

		arcfs->FindEnd(fh);
	} catch(...) {
		arcfs->FindEnd(fh);
		throw;
	}

	const uint32 bootSectors = 3;
	const uint32 dirSectors = ((fileCount + 1) * 23 + 127) >> 7;
	const uint32 dirMapSectors = (dirSectors + 59) / 60;
	const uint32 nonBitmapSectors = bootSectors + dirSectors + dirMapSectors + dataSectors + mapSectors;

	// computing the number of needed bitmap sectors is tricky as they are part
	// of the bitmap -- we do a loop here to handle the tricky case where the
	// addition of the bitmap itself requires adding bitmap sectors
	uint32 bitmapSectors = 0;
	for(;;) {
		uint32 t = ((nonBitmapSectors + bitmapSectors) >> 10) + 1;	// (!!) sector 0 bit is unused

		if (t <= bitmapSectors)
			break;

		bitmapSectors = t;
	}

	const uint32 totalSectors = bitmapSectors + nonBitmapSectors;

	// initialize a new disk image (on us!)
	Init(totalSectors, bootSectors, 128);
	
	// mount SDFS on us
	vdautoptr<IATDiskFS> sdfs(ATDiskFormatImageSDX2(this, origPath ? VDTextWToA(VDFileSplitPath(origPath)).c_str() : NULL));

	// copy over files
	uintptr fh2 = arcfs->FindFirst(0, entryInfo);
	try {
		vdfastvector<uint8> buf;
		do {
			arcfs->ReadFile(entryInfo.mKey, buf);
			uintptr fileKey = sdfs->WriteFile(0, entryInfo.mFileName.c_str(), buf.data(), (uint32)buf.size());
			sdfs->SetFileTimestamp(fileKey, entryInfo.mDate);
		} while(arcfs->FindNext(fh2, entryInfo));

		arcfs->FindEnd(fh2);
	} catch(...) {
		arcfs->FindEnd(fh2);
		throw;
	}

	sdfs->Flush();

	mPath = origPath;
	mbHasDiskSource = true;
	mImageFormat = kATDiskImageFormat_None;
}

ATDiskTimingMode ATDiskImage::GetTimingMode() const {
	return mTimingMode;
}

bool ATDiskImage::IsDirty() const {
	return mbDirty;
}

bool ATDiskImage::IsUpdatable() const {
	return mbHasDiskSource && mImageFormat != kATDiskImageFormat_None;
}

bool ATDiskImage::Flush() {
	if (!mbDirty)
		return true;

	if (!IsUpdatable())
		return false;

	if (mbDiskFormatDirty) {
		Save(VDStringW(mPath).c_str(), mImageFormat);
		return true;
	}

	// build a list of dirty sectors
	typedef vdfastvector<PhysSectorInfo *> DirtySectors;
	DirtySectors dirtySectors;

	for(PhysSectors::iterator it(mPhysSectors.begin()), itEnd(mPhysSectors.end()); it != itEnd; ++it) {
		PhysSectorInfo *psi = &*it;

		if (psi->mbDirty) {
			if (psi->mDiskOffset < 0) {
				// uh oh... this sector doesn't have a straightforward position on disk. force
				// a full write
				Save(VDStringW(mPath).c_str(), mImageFormat);
				return true;
			}

			dirtySectors.push_back(psi);
		}
	}

	// sort dirty sectors by on-disk position
	std::sort(dirtySectors.begin(), dirtySectors.end(),
		[](const PhysSectorInfo *x, const PhysSectorInfo *y) {
			return x->mDiskOffset < y->mDiskOffset;
		}
	);

	// open file for rewriting
	VDFile f(mPath.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kOpenExisting);

	// check if we have enough dirty sectors to bother coalescing
	if (dirtySectors.size() < 16) {
		// no - write individual sectors
		for(PhysSectorInfo* psi : dirtySectors) {
			f.seek(psi->mDiskOffset);
			f.write(&mImage[psi->mOffset], psi->mSize);
		}
	} else {
		// yes - allocate coalescing buffer
		const uint32 kWriteBufLen = 65536;
		vdblock<uint8> writeBuf(kWriteBufLen);
		uint32 wbLevel = 0;

		DirtySectors::const_iterator it(dirtySectors.begin()), itEnd(dirtySectors.end());
		while(it != itEnd) {
			PhysSectorInfo *psi = *it;
			sint32 writeOffset = psi->mDiskOffset;
			sint32 writeLen = psi->mSize;
			
			f.seek(writeOffset);

			for(;;) {
				if (wbLevel + psi->mSize > kWriteBufLen) {
					f.write(writeBuf.data(), wbLevel);
					wbLevel = 0;
				}

				memcpy(writeBuf.data() + wbLevel, &mImage[psi->mOffset], psi->mSize);
				wbLevel += psi->mSize;

				if (++it == itEnd)
					break;

				psi = *it;

				if (psi->mDiskOffset != writeOffset + writeLen)
					break;

				writeLen += psi->mSize;
			}

			if (wbLevel) {
				f.write(writeBuf.data(), wbLevel);
				wbLevel = 0;
			}
		}
	}

	// clear dirty flags on sectors
	for(PhysSectorInfo* psi : dirtySectors)
		psi->mbDirty = false;

	// clear global dirty flag
	mbDirty = false;

	// all done
	return true;
}

void ATDiskImage::SetPath(const wchar_t *path) {
	mbDirty = true;
	mbDiskFormatDirty = true;
	mbHasDiskSource = true;
	mPath = path;
}

void ATDiskImage::Save(const wchar_t *s, ATDiskImageFormat format) {
	// Image types:
	// ATR	Any sector size
	// XFD	128b sectors only
	// P2	128b sectors only
	// P3	128b sectors only
	// ATX	128b sectors only
	// DCM	128b or 256b sectors

	if (format == kATDiskImageFormat_None)
		throw MyError("Invalid disk image format type specified.");

	// scan for virtual sectors with errors or phantoms and check if it is supported
	bool supportPhantoms = false;
	uint32 maxPhantoms = 0;
	bool supportErrors = false;
	bool supportWeakSectors = false;
	bool supportSectorSize256 = false;
	bool supportSectorSizeOther = false;

	switch(format) {
		case kATDiskImageFormat_ATR:
			supportSectorSize256 = true;
			supportSectorSizeOther = true;
			break;

		case kATDiskImageFormat_P2:
		case kATDiskImageFormat_P3:
			supportPhantoms = true;
			supportErrors = true;
			supportWeakSectors = false;
			maxPhantoms = 7;
			break;

		case kATDiskImageFormat_ATX:
			supportPhantoms = true;
			supportErrors = true;
			supportWeakSectors = true;
			break;

		case kATDiskImageFormat_DCM:
			// DCM only supports three standard disk formats.
			if ((mSectorSize != 128 || mVirtSectors.size() != 720)
				&& (mSectorSize != 128 || mVirtSectors.size() != 1040)
				&& (mSectorSize != 256 || mVirtSectors.size() != 720))
			{
				throw MyError("Cannot save disk image: disk geometry is not supported.");
			}

			supportSectorSize256 = true;
			break;
	}

	if (!supportPhantoms) {
		for(const VirtSectorInfo& vsi : mVirtSectors) {
			if (vsi.mNumPhysSectors != 1)
				throw MyError("Cannot save disk image: disk contains phantom or missing sectors.");
		}
	} else if (maxPhantoms) {
		for(const VirtSectorInfo& vsi : mVirtSectors) {
			if (vsi.mNumPhysSectors > maxPhantoms)
				throw MyError("Cannot save disk image: disk contains too many phantom sectors.");
		}
	}

	for(const PhysSectorInfo& psi : mPhysSectors) {
		if (!supportErrors) {
			if (psi.mFDCStatus != 0xFF)
				throw MyError("Cannot save disk image: disk contains sector errors.");
		}

		if (!supportWeakSectors) {
			if (psi.mWeakDataOffset >= 0)
				throw MyError("Cannot save disk image: disk contains weak data sectors.");
		}

		if (psi.mSize == 256 ? !supportSectorSize256 : (psi.mSize != 128 && !supportSectorSizeOther))
			throw MyError("Cannot save disk image: disk contains an unsupported sector size.");
	}

	// copy the temp physical sector array as we will need to rewrite the disk offsets,
	// but don't want to leave them trashed if the save fails midway
	PhysSectors tempPhysSectors(mPhysSectors);

	VDFile f(s, nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);

	switch(format) {
		case kATDiskImageFormat_ATR:
			SaveATR(f, tempPhysSectors);
			break;

		case kATDiskImageFormat_XFD:
			SaveXFD(f, tempPhysSectors);
			break;

		case kATDiskImageFormat_P2:
			SaveP2(f, tempPhysSectors);
			break;

		case kATDiskImageFormat_P3:
			SaveP3(f, tempPhysSectors);
			break;

		case kATDiskImageFormat_ATX:
			SaveATX(f, tempPhysSectors);
			break;

		case kATDiskImageFormat_DCM:
			SaveDCM(f, tempPhysSectors);
			break;
	}

	// swap the new physical sector array in and clear the dirty flags
	mPhysSectors.swap(tempPhysSectors);
	for(PhysSectors::iterator it(mPhysSectors.begin()), itEnd(mPhysSectors.end());
		it != itEnd;
		++it)
	{
		PhysSectorInfo& psi = *it;
		psi.mbDirty = false;
	}

	mPath = s;
	mbDirty = false;
	mbDiskFormatDirty = false;
	mbHasDiskSource = true;
	mImageFormat = format;
}

uint32 ATDiskImage::GetPhysicalSectorCount() const {
	return (uint32)mPhysSectors.size();
}

void ATDiskImage::GetPhysicalSectorInfo(uint32 index, ATDiskPhysicalSectorInfo& info) const {
	info = mPhysSectors[index];
}

uint32 ATDiskImage::GetVirtualSectorCount() const {
	return (uint32)mVirtSectors.size();
}

void ATDiskImage::GetVirtualSectorInfo(uint32 index, ATDiskVirtualSectorInfo& info) const {
	info = mVirtSectors[index];
}

void ATDiskImage::ReadPhysicalSector(uint32 index, void *data, uint32 len) {
	const PhysSectorInfo& psec = mPhysSectors[index];

	memcpy(data, mImage.data() + psec.mOffset, std::min<uint32>(len, psec.mSize));
}

void ATDiskImage::WritePhysicalSector(uint32 index, const void *data, uint32 len) {
	PhysSectorInfo& psi = mPhysSectors[index];

	memcpy(mImage.data() + psi.mOffset, data, std::min<uint32>(len, psi.mSize));
	psi.mbDirty = true;
	mbDirty = true;
}

uint32 ATDiskImage::ReadVirtualSector(uint32 index, void *data, uint32 len) {
	if (index >= (uint32)mVirtSectors.size())
		return 0;

	const VirtSectorInfo& vsi = mVirtSectors[index];

	if (!vsi.mNumPhysSectors)
		return 0;

	const PhysSectorInfo& psi = mPhysSectors[vsi.mStartPhysSector];

	if (len > psi.mSize)
		len = psi.mSize;

	if (len)
		memcpy(data, mImage.data() + psi.mOffset, len);

	return len;
}

bool ATDiskImage::WriteVirtualSector(uint32 index, const void *data, uint32 len) {
	if (index >= (uint32)mVirtSectors.size())
		return false;

	const VirtSectorInfo& vsi = mVirtSectors[index];

	if (!vsi.mNumPhysSectors)
		return false;

	PhysSectorInfo& psi = mPhysSectors[vsi.mStartPhysSector];

	if (len != psi.mSize)
		return false;

	memcpy(mImage.data() + psi.mOffset, data, len);
	psi.mbDirty = true;
	mbDirty = true;
	return true;
}

void ATDiskImage::ComputeGeometry() {
	uint32 sectorCount = (uint32)mVirtSectors.size();
	mSectorsPerTrack = mSectorSize >= 256 ? 18 : sectorCount > 720 && !(sectorCount % 26) ? 26 : 18;

	mGeometry.mTrackCount = 1;
	mGeometry.mSideCount = 1;
	mGeometry.mbMFM = false;
	mGeometry.mSectorsPerTrack = sectorCount;
	mGeometry.mBootSectorCount = mBootSectorCount;
	mGeometry.mSectorSize = mSectorSize;
	mGeometry.mTotalSectorCount = sectorCount;

	if (mGeometry.mBootSectorCount > 0) {
		if (mGeometry.mSectorSize == 128) {
			switch(mGeometry.mTotalSectorCount) {
				default:
					if (mGeometry.mTotalSectorCount > 720)
						break;

					// fall through
				case 720:
					mGeometry.mSectorsPerTrack = 18;
					mGeometry.mSideCount = 1;
					break;

				case 1440:
				case 2880:
					mGeometry.mSectorsPerTrack = 18;
					mGeometry.mSideCount = 2;
					break;

				case 1040:
					mGeometry.mSectorsPerTrack = 26;
					mGeometry.mSideCount = 1;
					mGeometry.mbMFM = true;
					break;
			}
		} else if (mGeometry.mSectorSize == 256) {
			switch(mGeometry.mTotalSectorCount) {
				case 720:
					mGeometry.mSectorsPerTrack = 18;
					mGeometry.mSideCount = 1;
					mGeometry.mbMFM = true;
					break;

				case 1440:
				case 2880:
					mGeometry.mSectorsPerTrack = 18;
					mGeometry.mSideCount = 2;
					mGeometry.mbMFM = true;
					break;
			}
		}
	}

	if (mGeometry.mSectorsPerTrack > 0)
		mGeometry.mTrackCount = (mGeometry.mTotalSectorCount + mGeometry.mSectorsPerTrack - 1) / mGeometry.mSectorsPerTrack;

	if (mGeometry.mSideCount > 1)
		mGeometry.mTrackCount = (mGeometry.mTrackCount + 1) >> 1;
}

void ATDiskImage::SaveATR(VDFile& f, PhysSectors& phySecs) {
	// compute total sector sizes
	uint32 totalSize = 0;
	for(const VirtSectorInfo& vsi : mVirtSectors) {
		const PhysSectorInfo& psi = phySecs[vsi.mStartPhysSector];
		totalSize += psi.mSize;
	}

	// create ATR header
	uint8 header[16] = {0};
	uint32 paras = totalSize >> 4;
	VDWriteUnalignedLEU16(header+0, 0x0296);
	VDWriteUnalignedLEU16(header+2, (uint16)paras);
	VDWriteUnalignedLEU16(header+4, mSectorSize);
	header[6] = (uint8)(paras >> 16);

	f.write(header, 16);

	uint32 diskOffset = 16;

	for(const VirtSectorInfo& vsi : mVirtSectors) {
		PhysSectorInfo& psi = phySecs[vsi.mStartPhysSector];

		f.write(&mImage[psi.mOffset], psi.mSize);

		psi.mDiskOffset = diskOffset;
		diskOffset += psi.mSize;
	}
}

void ATDiskImage::SaveXFD(VDFile& f, PhysSectors& phySecs) {
	uint32 diskOffset = 0;

	for(const VirtSectorInfo& vsi : mVirtSectors) {
		PhysSectorInfo& psi = phySecs[vsi.mStartPhysSector];

		f.write(&mImage[psi.mOffset], psi.mSize);

		psi.mDiskOffset = diskOffset;
		diskOffset += psi.mSize;
	}
}

void ATDiskImage::SaveP2(VDFile& f, PhysSectors& phySecs) {
	// select emulation mode
	uint8 mode = 0x00;

	for(const VirtSectorInfo& vsi : mVirtSectors) {
		if (vsi.mNumPhysSectors > 1) {
			// select flip/flop if there are any phantom sectors
			mode = 0x02;
			break;
		}
	}

	// write file header
	const uint32 numSectors = (uint32)mVirtSectors.size();
	uint8 header[16] = {0};

	VDWriteUnalignedBEU16(&header[0], (uint16)mPhysSectors.size());
	header[2] = (uint8)'P';
	header[3] = (uint8)'2';
	header[4] = mode;

	f.write(header, 16);

	vdfastvector<uint8> phantomSectorCounts(numSectors, 0);
	vdfastvector<const PhysSectorInfo *> sectorOrdering(numSectors, nullptr);

	for(uint32 i=0; i<numSectors; ++i) {
		const VirtSectorInfo& vsi = mVirtSectors[i];

		if (!vsi.mNumPhysSectors)
			continue;

		PhysSectorInfo *psi = &phySecs[vsi.mStartPhysSector];
		psi->mDiskOffset = 16 + 12 + (12+128) * i;
		sectorOrdering[i] = psi++;
		phantomSectorCounts[i] = (uint8)(vsi.mNumPhysSectors - 1);

		for(uint32 j=1; j<vsi.mNumPhysSectors; ++j) {
			psi->mDiskOffset = 16 + 12 + (12+128) * (uint32)sectorOrdering.size();
			sectorOrdering.push_back(psi++);
			phantomSectorCounts.push_back(0);
		}
	}

	// write sector headers
	struct PROSectorData {
		uint8 mStatus[5];
		uint8 mPhantomCount;
		uint8 mPhantoms[6];
		uint8 mData[128];
	} sectorData;

	// set time out
	sectorData.mStatus[2] = 0xE0;
	sectorData.mStatus[3] = 0x00;

	const uint32 totalSectors = (uint32)sectorOrdering.size();
	uint32 phantomIndex = 1;
	for(uint32 i=0; i<totalSectors; ++i) {
		const PhysSectorInfo *psi = sectorOrdering[i];
		memset(sectorData.mPhantoms, 0, sizeof sectorData.mPhantoms);

		// set drive and FDC status and copy sector data
		if (psi) {
			memcpy(sectorData.mData, &mImage[psi->mOffset], 128);

			sectorData.mStatus[1] = psi->mFDCStatus;
		} else {
			// missing sector data
			sectorData.mStatus[1] = 0xF7;
		}

		sectorData.mStatus[0] = (sectorData.mStatus[1] != 0xFF) ? 0x1C : 0x18;

		// compute checksum
		uint32 checkSum = sectorData.mStatus[0] + sectorData.mStatus[1] + sectorData.mStatus[2] + sectorData.mStatus[3];

		while(checkSum >= 0x100)
			checkSum = (checkSum & 0xFF) + (checkSum >> 8);

		sectorData.mStatus[4] = checkSum;
		sectorData.mPhantomCount = phantomSectorCounts[i];

		for(uint32 i = 1; i <= sectorData.mPhantomCount; ++i)
			sectorData.mPhantoms[i] = phantomIndex++;

		f.write(&sectorData, sizeof sectorData);
	}
}

void ATDiskImage::SaveP3(VDFile& f, PhysSectors& phySecs) {
	// select emulation mode
	uint8 mode = 0x00;

	for(const VirtSectorInfo& vsi : mVirtSectors) {
		if (vsi.mNumPhysSectors > 1) {
			// select flip/flop if there are any phantom sectors
			mode = 0x02;
			break;
		}
	}

	// write file header
	const uint32 numSectors = (uint32)mVirtSectors.size();
	uint8 header[16] = {0};

	VDWriteUnalignedBEU16(&header[0], (uint16)mPhysSectors.size());
	header[2] = (uint8)'P';
	header[3] = (uint8)'3';
	header[4] = mode;
	VDWriteUnalignedBEU16(&header[6], (uint16)numSectors);

	f.write(header, 16);

	vdfastvector<uint8> phantomSectorCounts(numSectors, 0);
	vdfastvector<const PhysSectorInfo *> sectorOrdering(numSectors, nullptr);

	for(uint32 i=0; i<numSectors; ++i) {
		const VirtSectorInfo& vsi = mVirtSectors[i];

		if (!vsi.mNumPhysSectors)
			continue;

		PhysSectorInfo *psi = &phySecs[vsi.mStartPhysSector];
		psi->mDiskOffset = 16 + 12 + (12+128) * i;
		sectorOrdering[i] = psi++;
		phantomSectorCounts[i] = (uint8)(vsi.mNumPhysSectors - 1);

		for(uint32 j=1; j<vsi.mNumPhysSectors; ++j) {
			psi->mDiskOffset = 16 + 12 + (12+128) * (uint32)sectorOrdering.size();
			sectorOrdering.push_back(psi++);
			phantomSectorCounts.push_back(0);
		}
	}

	// write sector headers
	struct PROSectorData {
		uint8 mStatus[5];
		uint8 mPhantomCount;
		uint8 mPhantoms[6];
		uint8 mData[128];
	} sectorData;

	// set time out
	sectorData.mStatus[2] = 0xE0;
	sectorData.mStatus[3] = 0x00;

	const uint32 totalSectors = (uint32)sectorOrdering.size();
	uint32 phantomIndex = 1;
	for(uint32 i=0; i<totalSectors; ++i) {
		const PhysSectorInfo *psi = sectorOrdering[i];
		memset(sectorData.mPhantoms, 0, sizeof sectorData.mPhantoms);

		// set drive and FDC status and copy sector data
		if (psi) {
			memcpy(sectorData.mData, &mImage[psi->mOffset], 128);

			sectorData.mStatus[1] = psi->mFDCStatus;
		} else {
			// missing sector data
			sectorData.mStatus[1] = 0xF7;
		}

		sectorData.mStatus[0] = (sectorData.mStatus[1] != 0xFF) ? 0x1C : 0x18;

		// compute checksum
		uint32 checkSum = sectorData.mStatus[0] + sectorData.mStatus[1] + sectorData.mStatus[2] + sectorData.mStatus[3];

		while(checkSum >= 0x100)
			checkSum = (checkSum & 0xFF) + (checkSum >> 8);

		sectorData.mStatus[4] = checkSum;
		sectorData.mPhantomCount = phantomSectorCounts[i];

		for(uint32 i = 1; i <= sectorData.mPhantomCount; ++i)
			sectorData.mPhantoms[i] = phantomIndex++;

		f.write(&sectorData, sizeof sectorData);
	}
}

void ATDiskImage::SaveDCM(VDFile& f, PhysSectors& phySecs) {
	uint8 packBuf[0x6200];
	uint32 packBufLevel = 0;

	// Initialize pass header at the base of the pack buffer
	packBuf[0] = 0xFA;		// single file

	if (mSectorSize == 128) {
		if (mVirtSectors.size() == 720)
			packBuf[1] = 0x80;	// pass 0, last pass, single density
		else
			packBuf[1] = 0xA0;	// pass 0, last pass, enhanced density
	} else {
		packBuf[1] = 0xC0;	// pass 0, last pass, double density
	}

	packBuf[2] = 0;
	packBuf[3] = 0;
	packBufLevel = 4;

	uint8 buf1[256] = {0};
	uint8 buf2[256];

	uint8 *prevBuf = buf1;
	uint8 *nextBuf = buf2;
	uint32 prevSector = 0;
	sint32 prevSectorFlagOffset = -1;

	uint8 rleBuf[512];

	const uint32 numSecs = (uint32)mVirtSectors.size();
	for(uint32 sector=1; sector<=numSecs; ++sector) {
		const VirtSectorInfo& vsi = mVirtSectors[sector - 1];
		PhysSectorInfo& psi = phySecs[vsi.mStartPhysSector];

		// invalidate the image offset since DCMs cannot be rewritten in place
		psi.mDiskOffset = -1;

		// copy sector data into sector buffer, since we need to pad 128b
		// to 256b in DD mode
		memcpy(nextBuf, &mImage[psi.mOffset], psi.mSize);
		memset(nextBuf + psi.mSize, 0, sizeof(buf2) - psi.mSize);

		// check for an all zero sector
		bool allZero = true;
		for(uint32 i=0; i<mSectorSize; ++i) {
			if (nextBuf[i]) {
				allZero = false;
				break;
			}
		}

		// if the sector is all zero, skip -- do not update prev/next buffers
		if (allZero)
			continue;

		// update header if this is the first sector in the pass
		if (prevSectorFlagOffset < 0)
			VDWriteUnalignedLEU16(&packBuf[2], sector);

		uint32 packBufSectorStart = packBufLevel;

		// compute common prefix
		uint32 prefixLen = 0;
		while(prefixLen < mSectorSize && prevBuf[prefixLen] == nextBuf[prefixLen])
			++prefixLen;

		// check if the entire sector is the same as prev
		if (prefixLen == mSectorSize) {
			// repeat last sector
			packBuf[packBufLevel++] = 0x46;
		} else {
			// compute common suffix
			uint32 suffixLen = 0;
			while(suffixLen < mSectorSize && prevBuf[mSectorSize - 1 - suffixLen] == nextBuf[mSectorSize - 1 - suffixLen])
				++suffixLen;

			// attempt RLE compression
			uint32 runStart = 0;
			uint32 rleLen = 0;

			while(runStart < mSectorSize) {
				VDASSERT(rleLen <= sizeof(rleBuf));
				// compute uncompressed length
				uint32 runEnd = runStart;
				for(;;) {
					// If we have three bytes or less, just include them in the current
					// uncompressed run and then exit.
					if (runEnd >= mSectorSize - 3) {
						runEnd = mSectorSize;
						break;
					}

					// Check if we can start a run.
					if (nextBuf[runEnd] == nextBuf[runEnd + 1]
						&& nextBuf[runEnd] == nextBuf[runEnd + 2])
					{
						// yes -- terminate the uncompressed run
						break;
					}

					// include this byte in the uncompressed run
					++runEnd;
				}

				// check if we are encoding a 256 byte uncompressed run -- this is
				// a special case that the encoding doesn't support. fortunately,
				// it's also suboptimal, so we should never use it.
				if (runStart == 0 && runEnd == 256) {
					// mark the RLE encoding as max sector size and bail
					rleLen = mSectorSize;
					break;
				}

				// copy uncompressed run to RLE buffer
				rleBuf[rleLen++] = runEnd;
				memcpy(&rleBuf[rleLen], &nextBuf[runStart], runEnd - runStart);
				rleLen += runEnd - runStart;

				// check if we're done
				if (runEnd >= mSectorSize)
					break;

				// begin encoding RLE segment
				const uint8 runVal = nextBuf[runEnd];
				runStart = runEnd;
			
				do {
					++runEnd;
				} while(runEnd < mSectorSize && nextBuf[runEnd] == runVal);

				// write compressed run
				rleBuf[rleLen++] = runEnd;
				rleBuf[rleLen++] = runVal;

				// loop back for next uncompressed run
				runStart = runEnd;
			}

			// compare sizes
			const uint32 prefixEncodingSize = prefixLen > 1 ? mSectorSize - prefixLen + 1 : ~(uint32)0;
			const uint32 suffixEncodingSize = suffixLen > 1 ? mSectorSize - suffixLen + 1 : ~(uint32)0;

			// pick smallest encoding -- note that RLE is set up to win
			// over prefix/suffix whenever none are advantageous
			if (rleLen < prefixEncodingSize && rleLen < suffixEncodingSize) {
				if (rleLen >= mSectorSize) {
					// write uncompressed sector
					packBuf[packBufLevel++] = 0x47;
					memcpy(&packBuf[packBufLevel], nextBuf, mSectorSize);
					packBufLevel += mSectorSize;
				} else {
					// write RLE sector
					packBuf[packBufLevel++] = 0x43;
					memcpy(&packBuf[packBufLevel], rleBuf, rleLen);
					packBufLevel += rleLen;
				}
			} else if (prefixEncodingSize < suffixEncodingSize) {
				// write modify end
				packBuf[packBufLevel++] = 0x44;
				packBuf[packBufLevel++] = prefixLen;
				memcpy(&packBuf[packBufLevel], nextBuf + prefixLen, mSectorSize - prefixLen);
				packBufLevel += mSectorSize - (uint8)prefixLen;
			} else {
				// write modify begin
				packBuf[packBufLevel++] = 0x41;
				packBuf[packBufLevel++] = (uint8)(mSectorSize - 1 - suffixLen);

				for(uint32 i = mSectorSize - suffixLen; i; --i)
					packBuf[packBufLevel++] = nextBuf[i - 1];
			}
		}

		// check if the pack buffer is full -- must not exceed 0x6000 bytes
		if (packBufLevel >= 0x5FFF) {
			// set sequential flag on last sector
			if (prevSectorFlagOffset >= 0)
				packBuf[prevSectorFlagOffset] |= 0x80;

			// temporarily clear the final pass flag in the header
			packBuf[1] &= 0x7F;

			// write out the previous pass, NOT including this sector
			f.write(packBuf, packBufSectorStart);

			// write out pass end
			const uint8 passEnd = 0x45;
			f.write(&passEnd, 1);

			// move new data down
			uint32_t newDataLen = packBufLevel - packBufSectorStart;
			memmove(&packBuf[4], &packBuf[packBufSectorStart], newDataLen);
			packBufLevel = 4 + newDataLen;

			// set final pass flag again and increment pass number
			packBuf[1] = ((packBuf[1] + 1) & 0x1F) + (packBuf[1] & 0x60) + 0x80;

			// update header starting sector
			VDWriteUnalignedLEU16(&packBuf[2], sector);

			prevSectorFlagOffset = 4;
		} else {
			// pack buffer not full -- set the previous sector pointer
			if (prevSectorFlagOffset >= 0) {
				if (prevSector + 1 == sector) {
					// set sequential flag on previous sector
					packBuf[prevSectorFlagOffset] |= 0x80;
				} else {
					// insert new sector number
					memmove(&packBuf[packBufSectorStart + 2], &packBuf[packBufSectorStart], packBufLevel - packBufSectorStart);
					VDWriteUnalignedLEU16(&packBuf[packBufSectorStart], sector);
					packBufLevel += 2;
					packBufSectorStart += 2;
				}
			}

			prevSectorFlagOffset = packBufSectorStart;
		}

		// swap the sector buffers and go on
		std::swap(nextBuf, prevBuf);
		prevSector = sector;
	}

	// terminate last sector number
	if (prevSectorFlagOffset >= 0)
		packBuf[prevSectorFlagOffset] |= 0x80;

	// write out final pass
	f.write(packBuf, packBufLevel);
	const uint8 passEnd = 0x45;
	f.write(&passEnd, 1);
}

void ATDiskImage::SaveATX(VDFile& f, PhysSectors& phySecs) {
	ATXHeader hdr = {0};
	memcpy(hdr.mSignature, "AT8X", 4);
	hdr.mVersionMajor = 1;
	hdr.mVersionMinor = 1;
	hdr.mFlags = 2;
	hdr.mTrackDataOffset = 48;

	f.write(&hdr, sizeof hdr);

	// write one track at a time
	const uint32 numSectors = (uint32)mVirtSectors.size();
	const uint32 numTracks = (numSectors + 17) / 18;

	vdfastvector<ATXSectorHeader> sechdrs;
	vdfastvector<ATXTrackChunkHeader> xhdrs;
	vdfastvector<PhysSectorInfo *> psecs;
	vdfastvector<int> secorder;
	for(uint32 track=0; track<numTracks; ++track) {
		sechdrs.clear();
		xhdrs.clear();
		psecs.clear();

		uint32 vsecIndex = track * 18;
		uint32 vsecCount = std::min<uint32>(numSectors - vsecIndex, 18);
		uint32 dataOffset = 0;

		for(uint32 i = 0; i < vsecCount; ++i) {
			const VirtSectorInfo& vsi = mVirtSectors[vsecIndex + i];

			for(uint32 j = 0; j < vsi.mNumPhysSectors; ++j) {
				PhysSectorInfo& psi = phySecs[vsi.mStartPhysSector + j];
				ATXSectorHeader& sechdr = sechdrs.push_back();

				sechdr.mIndex = i + 1;
				sechdr.mFDCStatus = ~psi.mFDCStatus & 0x3F;
				sechdr.mTimingOffset = (uint32)((psi.mRotPos - floorf(psi.mRotPos)) * kBytesPerTrack);
				if (sechdr.mTimingOffset >= kBytesPerTrack)
					sechdr.mTimingOffset -= kBytesPerTrack;

				sechdr.mDataOffset = dataOffset;
				dataOffset += psi.mSize;

				if (psi.mWeakDataOffset >= 0) {
					sechdr.mFDCStatus |= 0x40;

					ATXTrackChunkHeader& xhdr = xhdrs.push_back();

					xhdr.mSize = sizeof(xhdr);
					xhdr.mType = xhdr.kTypeWeakBits;
					xhdr.mNum = (uint8)(sechdrs.size() - 1);
					xhdr.mData = (uint16)psi.mWeakDataOffset;
				}

				psecs.push_back(&psi);
			}
		}

		// now that we know the physical sector count for this track,
		// adjust the data offsets to be relative to the start of the track chunk
		const uint32 psecCount = (uint32)sechdrs.size();
		const uint32 preDataSize = sizeof(ATXTrackHeader) + sizeof(ATXTrackChunkHeader) + sizeof(ATXSectorHeader)*psecCount + sizeof(ATXTrackChunkHeader);

		for(ATXSectorHeader& secHdr : sechdrs) {
			secHdr.mDataOffset += preDataSize;
		}

		// resort physical sectors by disk position
		secorder.resize(psecCount);
		for(uint32 i = 0; i < psecCount; ++i)
			secorder[i] = i;

		std::sort(secorder.begin(), secorder.end(),
			[&](int x, int y) {
				return sechdrs[x].mTimingOffset < sechdrs[y].mTimingOffset;
			}
		);

		// write track header
		const sint32 trackChunkBase = (sint32)f.tell();

		ATXTrackHeader trkhdr = {0};
		trkhdr.mSize = preDataSize + dataOffset + sizeof(ATXTrackChunkHeader) * ((uint32)xhdrs.size() + 1);
		trkhdr.mType = 0;
		trkhdr.mTrackNum = track;
		trkhdr.mNumSectors = psecCount;
		trkhdr.mDataOffset = sizeof(ATXTrackHeader);

		f.write(&trkhdr, sizeof trkhdr);

		// write sector list header
		ATXTrackChunkHeader slhdr = {0};
		slhdr.mSize = sizeof(ATXTrackChunkHeader) + sizeof(ATXSectorHeader) * psecCount;
		slhdr.mType = 1;

		f.write(&slhdr, sizeof slhdr);

		// write sector list
		for(uint32 i = 0; i < psecCount; ++i) {
			f.write(&sechdrs[secorder[i]], sizeof(ATXSectorHeader));
		}

		// write sector data header
		ATXTrackChunkHeader sdhdr = {0};
		sdhdr.mSize = sizeof(ATXTrackChunkHeader) + dataOffset;
		sdhdr.mType = ATXTrackChunkHeader::kTypeSectorData;

		f.write(&sdhdr, sizeof sdhdr);

		// write sector data
		for(uint32 i = 0; i < psecCount; ++i) {
			const ATXSectorHeader& sechdr = sechdrs[i];
			PhysSectorInfo& psi = *psecs[i];

			psi.mDiskOffset = trackChunkBase + sechdr.mDataOffset;

			if (psi.mSize)
				f.write(&mImage[psi.mOffset], psi.mSize);
		}

		// write extra sector data
		if (!xhdrs.empty()) {
			// adjust indices for sorting
			for(auto& xhdr : xhdrs) {
				auto it = std::find(secorder.begin(), secorder.end(), xhdr.mNum);

				VDASSERT(it != secorder.end());
				xhdr.mNum = (uint8)(it - secorder.begin());
			}

			f.write(xhdrs.data(), (uint32)xhdrs.size() * sizeof(xhdrs[0]));
		}

		// write sentinel
		ATXTrackChunkHeader endhdr = {0};
		f.write(&endhdr, sizeof endhdr);
	}

	// backpatch size
	uint32 totalSize = (uint32)f.size();

	f.seek(32);
	f.write(&totalSize, 4);
}

///////////////////////////////////////////////////////////////////////////

IATDiskImage *ATLoadDiskImage(const wchar_t *path) {
	vdautoptr<ATDiskImage> diskImage(new ATDiskImage);

	diskImage->Load(path);

	return diskImage.release();
}

IATDiskImage *ATLoadDiskImage(const wchar_t *origPath, const wchar_t *imagePath, IVDRandomAccessStream& stream) {
	vdautoptr<ATDiskImage> diskImage(new ATDiskImage);

	diskImage->Load(origPath, imagePath, stream);

	return diskImage.release();
}

IATDiskImage *ATCreateDiskImage(uint32 sectorCount, uint32 bootSectorCount, uint32 sectorSize) {
	vdautoptr<ATDiskImage> diskImage(new ATDiskImage);

	diskImage->Init(sectorCount, bootSectorCount, sectorSize);

	return diskImage.release();
}

void ATDiskConvertGeometryToPERCOM(uint8 percom[12], const ATDiskGeometryInfo& geom) {
	// track count
	percom[0] = (uint8)geom.mTrackCount;

	// step rate
	percom[1] = 0x01;

	// sectors per track
	percom[2] = (uint8)(geom.mSectorsPerTrack >> 8);
	percom[3] = (uint8)(geom.mSectorsPerTrack);

	// sides minus one
	percom[4] = geom.mSideCount - 1;

	// record method
	percom[5] = geom.mbMFM ? 4 : 0;

	// bytes per sector
	percom[6] = (uint8)(geom.mSectorSize >> 8);
	percom[7] = (uint8)geom.mSectorSize;

	// drive online
	percom[8] = 0xFF;

	// unused
	percom[9] = 0;
	percom[10] = 0;
	percom[11] = 0;
}

void ATDiskConvertPERCOMToGeometry(ATDiskGeometryInfo& geom, const uint8 percom[12]) {
	geom.mTrackCount = percom[0];
	geom.mSectorSize = VDReadUnalignedBEU16(&percom[6]);
	geom.mSectorsPerTrack = VDReadUnalignedBEU16(&percom[2]);
	geom.mSideCount = percom[4] + 1;
	geom.mbMFM = (percom[5] & 4) != 0;
	geom.mBootSectorCount = geom.mSectorSize < 512 ? 3 : 0;
	geom.mTotalSectorCount = geom.mSectorsPerTrack * geom.mSideCount * geom.mTrackCount;
}
