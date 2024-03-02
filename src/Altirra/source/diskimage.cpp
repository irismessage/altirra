#include "stdafx.h"
#include <vd2/system/binary.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/math.h>
#include <vd2/system/strutil.h>
#include <vd2/system/vdstl.h>
#include "diskimage.h"

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
}

class ATDiskImage : public IATDiskImage {
public:
	void Init(uint32 sectorCount, uint32 bootSectorCount, uint32 sectorSize);
	void Load(const wchar_t *s);
	void Load(const wchar_t *origPath, const wchar_t *imagePath, IVDRandomAccessStream& stream);

	ATDiskTimingMode GetTimingMode() const;

	bool IsDirty() const;
	bool IsUpdatable() const;
	bool IsDynamic() const { return false; }
	bool Flush();

	void SetPathATR(const wchar_t *path);
	void SaveATR(const wchar_t *path);

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
	void LoadDCM(IVDRandomAccessStream& stream, uint32 len, const wchar_t *origPath, const uint8 *header);
	void LoadATX(IVDRandomAccessStream& stream, uint32 len, const uint8 *header);
	void LoadP2(IVDRandomAccessStream& stream, uint32 len, const uint8 *header);
	void LoadP3(IVDRandomAccessStream& stream, uint32 len, const uint8 *header);
	void LoadATR(IVDRandomAccessStream& stream, uint32 len, const wchar_t *origPath, const uint8 *header);
	void ComputeSectorsPerTrack();

	typedef ATDiskVirtualSectorInfo VirtSectorInfo;
	typedef ATDiskPhysicalSectorInfo PhysSectorInfo;
	typedef vdfastvector<PhysSectorInfo> PhysSectors;
	typedef vdfastvector<VirtSectorInfo> VirtSectors;

	struct SortDirtySectors {
		bool operator()(const PhysSectorInfo *x, const PhysSectorInfo *y) const {
			return x->mOffset < y->mOffset;
		}
	};

	uint32	mBootSectorCount;
	uint32	mSectorSize;
	uint32	mSectorsPerTrack;
	uint32	mReWriteOffset;
	bool	mbDirty;
	bool	mbDiskFormatDirty;
	bool	mbHasDiskSource;
	ATDiskTimingMode	mTimingMode;

	VDStringW	mPath;

	PhysSectors mPhysSectors;
	VirtSectors mVirtSectors;
	vdfastvector<uint8>		mImage;
};

void ATDiskImage::Init(uint32 sectorCount, uint32 bootSectorCount, uint32 sectorSize) {
	mBootSectorCount = bootSectorCount;
	mSectorSize = sectorSize;

	mImage.clear();
	mImage.resize(128 * bootSectorCount + sectorSize * (sectorCount - bootSectorCount), 0);

	mPhysSectors.resize(sectorCount);
	mVirtSectors.resize(sectorCount);

	ComputeSectorsPerTrack();

	for(uint32 i=0; i<sectorCount; ++i) {
		PhysSectorInfo& psi = mPhysSectors[i];
		VirtSectorInfo& vsi = mVirtSectors[i];

		vsi.mStartPhysSector = i;
		vsi.mNumPhysSectors = 1;

		psi.mOffset		= i < mBootSectorCount ? 128*i : 128*mBootSectorCount + mSectorSize*(i-mBootSectorCount);
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

	if (fileSize <= 65535 * 128 && imagePath && !vdwcsicmp(VDFileSplitExt(imagePath), L".xfd")) {
		sint32 len = (sint32)fileSize;

		mImage.resize(len);
		stream.Read(mImage.data(), len);

		mBootSectorCount = 3;
		mSectorSize = 128;
		mReWriteOffset = 0;

		const int sectorCount = len >> 7;
		mPhysSectors.resize(sectorCount);
		mVirtSectors.resize(sectorCount);

		for(int i=0; i<sectorCount; ++i) {
			PhysSectorInfo& psi = mPhysSectors[i];
			VirtSectorInfo& vsi = mVirtSectors[i];

			vsi.mStartPhysSector = i;
			vsi.mNumPhysSectors = 1;

			psi.mOffset		= 128*i;
			psi.mSize		= 128;
			psi.mFDCStatus	= 0xFF;
			psi.mRotPos		= (float)kTrackInterleave18[i % 18] / 18.0f;
			psi.mWeakDataOffset = -1;
			psi.mbDirty		= false;
		}
	} else {
		sint32 len = VDClampToSint32(stream.Length()) - 16;
		
		uint8 header[16];

		mImage.resize(len);
		stream.Read(header, 16);
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
			if (origPath)
				throw MyError("Disk image is corrupt or uses an unsupported format.");
			else
				throw MyError("Disk image \"%ls\" is corrupt or uses an unsupported format.", VDFileSplitPath(origPath));
		}
	}

	if (origPath)
		mPath = origPath;
	else
		mPath.clear();

	ComputeSectorsPerTrack();
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

	enum {
		kInterleaveSD,
		kInterleaveDD,
		kInterleaveED
	};

	uint8 interleaveMode = kInterleaveSD;

	for(;;) {
		struct PassHeader {
			uint8 mArchiveType;
			uint8 mPassInfo;
			uint8 mSectorLo;
			uint8 mSectorHi;
		} passHeader;

		stream.Read(&passHeader, sizeof(PassHeader));

		if (passHeader.mArchiveType != 0xF9 && passHeader.mArchiveType != 0xFA)
			throw ATInvalidDiskFormatException(origPath);

		uint32 sectorSize = (passHeader.mPassInfo & 0x60) == 0x20 ? 256 : 128;
		uint32 sectorNum = passHeader.mSectorLo + 256*passHeader.mSectorHi;

		switch(passHeader.mPassInfo & 0x60) {
			case 0x00:
				mainSectorCount = 720;
				interleaveMode = kInterleaveSD;
				break;
			case 0x40:
				mainSectorCount = 1040;
				interleaveMode = kInterleaveED;
				break;
			case 0x20:
				mainSectorCount = 720;
				interleaveMode = kInterleaveDD;
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
			psi.mSize = sectorNum <= 3 ? 128 : sectorSize;
			psi.mRotPos = 0;
			psi.mFDCStatus = 0xFF;
			psi.mWeakDataOffset = -1;
			psi.mbDirty = false;

			switch(interleaveMode) {
				case kInterleaveSD:
					psi.mRotPos = (float)kTrackInterleave18[(sectorNum - 1) % 18] / 18.0f;
					break;

				case kInterleaveDD:
					psi.mRotPos = (float)kTrackInterleaveDD[(sectorNum - 1) % 18] / 18.0f;
					break;

				case kInterleaveED:
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
			psi.mSize = secNum <= 3 ? 128 : mainSectorSize;
			psi.mFDCStatus = 0xFF;
			psi.mWeakDataOffset = -1;
			psi.mbDirty = false;

			switch(interleaveMode) {
				case kInterleaveSD:
					psi.mRotPos = (float)kTrackInterleave18[(secNum - 1) % 18] / 18.0f;
					break;

				case kInterleaveDD:
					psi.mRotPos = (float)kTrackInterleaveDD[(secNum - 1) % 18] / 18.0f;
					break;

				case kInterleaveED:
					psi.mRotPos = (float)kTrackInterleave26[(secNum - 1) % 26] / 26.0f;
					break;
			}

			mImage.resize(mImage.size() + psi.mSize, 0);
		}
	}

	mBootSectorCount = 3;
	mSectorSize = mainSectorSize;
	mReWriteOffset = -1;
}

namespace {
	struct ATXHeader {
		uint8	mSignature[4];
		uint16	mVersionMajor;
		uint16	mVersionMinor;
		uint16	mFlags;
		uint16	mMysteryDataCount;
		uint8	mFill1[16];
		uint32	mTrackDataOffset;
		uint32	mMysteryDataOffset;
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

	struct ATXSectorListHeader {
		uint32	mSize;
		uint8	mType;
		uint8	mReserved05[3];
	};

	struct ATXSectorHeader {
		uint8	mIndex;
		uint8	mFDCStatus;		// not inverted
		uint16	mTimingOffset;
		uint32	mDataOffset;
	};

	struct ATXSectorExtraData {
		enum {
			kFlagWeakBits = 0x00000008
		};

		uint32	mFlags;
		uint8	mUnknown2;
		uint8	mSectorIndex;
		uint16	mWeakDataOffset;	// starting byte at which to inject garbage
	};
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

	vdfastvector<ATXSectorHeader> sectorHeaders;
	vdfastvector<int> phySectorLookup;
	for(uint32 i=0; i<40; ++i) {
		ATXTrackHeader trkhdr;
		sint64 trackBaseOffset = stream.Pos();

		stream.Read(&trkhdr, sizeof trkhdr);

		// validate track
		if (trackBaseOffset + trkhdr.mSize > imageSize)
			throw MyError("Invalid ATX image: Track header at %08x extends beyond end of file.", (uint32)trackBaseOffset);

		if (trkhdr.mType != 0)
			throw MyError("Invalid ATX image: Track header at %08x has the wrong type.", (uint32)trackBaseOffset);

		// read sector list header
		const sint64 sectorListOffset = trackBaseOffset + trkhdr.mDataOffset;
		stream.Seek(sectorListOffset);

		ATXSectorListHeader sectorListHeader;
		stream.Read(&sectorListHeader, sizeof sectorListHeader);

		if (sectorListHeader.mType != 1)
			throw MyError("Invalid ATX image: Sector list header at %08x has the wrong type.", (uint32)sectorListOffset);

		// read sector headers
		sectorHeaders.resize(trkhdr.mNumSectors);
		stream.Read(sectorHeaders.data(), sizeof(sectorHeaders[0])*trkhdr.mNumSectors);

		phySectorLookup.clear();
		phySectorLookup.resize(trkhdr.mNumSectors, -1);

		uint32 secDataOffset = (uint32)mImage.size();
		uint32 sectorsWithExtraData = 0;
		uint32 highestEnd = 0;
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

				// Missing sectors do not have data.
				if (!(sechdr.mFDCStatus & 0x10)) {
					uint32 dataEnd = sechdr.mDataOffset + 128;
					if (dataEnd > trkhdr.mSize)
						throw MyError("Invalid protected disk: sector extends outside of track.");

					if (highestEnd < dataEnd)
						highestEnd = dataEnd;
				}

				phySectorLookup[k] = (int)mPhysSectors.size();

				mPhysSectors.push_back();
				PhysSectorInfo& psi = mPhysSectors.back();

				psi.mFDCStatus = ~sechdr.mFDCStatus | 0xc0;		// purpose of bit 7 is unknown
				psi.mOffset = secDataOffset + sechdr.mDataOffset;
				psi.mSize = sechdr.mFDCStatus & 0x10 ? 0 : 128;
				psi.mRotPos = (float)sechdr.mTimingOffset / (float)kBytesPerTrack;
				psi.mWeakDataOffset = -1;
				psi.mbDirty = false;
				++vsi.mNumPhysSectors;
			}
		}

		// read sector data
		stream.Seek(trackBaseOffset);
		size_t offset = mImage.size();
		mImage.resize(offset + highestEnd);
		stream.Read(mImage.data() + offset, highestEnd);

		// read extra sector data
		ATXSectorExtraData extraData;
		for(uint32 j=0; j<sectorsWithExtraData; ++j) {
			stream.Read(&extraData, sizeof extraData);

			if (extraData.mSectorIndex >= trkhdr.mNumSectors)
				continue;

			int phyIndex = phySectorLookup[extraData.mSectorIndex];
			if (phyIndex < 0) {
				VDASSERT(phyIndex >= 0);
				continue;
			}

			PhysSectorInfo& psi = mPhysSectors[phyIndex];
			if (extraData.mFlags & ATXSectorExtraData::kFlagWeakBits) {
				if (extraData.mWeakDataOffset < psi.mSize)
					psi.mWeakDataOffset = extraData.mWeakDataOffset;
			}
		}

		stream.Seek(trackBaseOffset + trkhdr.mSize);
	}

	mTimingMode = kATDiskTimingMode_UsePrecise;
	mReWriteOffset = -1;
}

void ATDiskImage::LoadP2(IVDRandomAccessStream& stream, uint32 len, const uint8 *header) {
	mBootSectorCount = 3;
	mSectorSize = 128;
	mReWriteOffset = -1;
	mTimingMode = kATDiskTimingMode_UseOrdered;

	int sectorCount = VDReadUnalignedBEU16(&header[0]);

	// read sector headers
	for(int i=0; i<sectorCount; ++i) {
		const uint8 *sectorhdr = &mImage[(128+12) * i];

		mPhysSectors.push_back();
		PhysSectorInfo& psi = mPhysSectors.back();

		psi.mOffset		= (128+12)*i+12;
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
	mBootSectorCount = 3;
	mSectorSize = 128;
	mReWriteOffset = -1;
	mTimingMode = kATDiskTimingMode_UseOrdered;

	uint32 sectorCount = VDReadUnalignedBEU16(&header[6]);

	// read sector headers
	for(uint32 i=0; i<sectorCount; ++i) {
		const uint8 *sectorhdr = &mImage[(128+12) * i];
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

	if (mSectorSize > 512) {
		if (origPath)
			throw MyError("Disk image \"%ls\" uses an unsupported sector size of %u bytes.", VDFileSplitPath(origPath), mSectorSize);
		else
			throw MyError("Disk image uses an unsupported sector size of %u bytes.", mSectorSize);
	}

	mReWriteOffset = 16;

	if (len < 128*imageBootSectorCount) {
		imageBootSectorCount = len >> 7;
		if (mBootSectorCount > imageBootSectorCount)
			mBootSectorCount = imageBootSectorCount;
	}

	uint32 sectorCount = (len - 128*imageBootSectorCount) / mSectorSize + imageBootSectorCount;

	ComputeSectorsPerTrack();	// needed earlier for interleave

	mPhysSectors.resize(sectorCount);
	mVirtSectors.resize(sectorCount);

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
		psi.mSize		= i < mBootSectorCount ? 128 : mSectorSize;
		psi.mFDCStatus	= 0xFF;
		psi.mRotPos		= mSectorSize >= 256 ? (float)kTrackInterleaveDD[i % 18] / 18.0f
						: mSectorsPerTrack >= 26 ? (float)kTrackInterleave26[i % 26] / 26.0f
						: (float)kTrackInterleave18[i % 18] / 18.0f;
		psi.mWeakDataOffset = -1;
		psi.mbDirty		= false;
	}
}

ATDiskTimingMode ATDiskImage::GetTimingMode() const {
	return mTimingMode;
}

bool ATDiskImage::IsDirty() const {
	return mbDirty;
}

bool ATDiskImage::IsUpdatable() const {
	return mbHasDiskSource && mReWriteOffset >= 0;
}

bool ATDiskImage::Flush() {
	if (!mbDirty)
		return true;

	if (!IsUpdatable())
		return false;

	if (mbDiskFormatDirty) {
		SaveATR(VDStringW(mPath).c_str());
		return true;
	}

	// build a list of dirty sectors
	typedef vdfastvector<PhysSectorInfo *> DirtySectors;
	DirtySectors dirtySectors;

	for(PhysSectors::iterator it(mPhysSectors.begin()), itEnd(mPhysSectors.end()); it != itEnd; ++it) {
		PhysSectorInfo *psi = &*it;

		if (psi->mbDirty)
			dirtySectors.push_back(psi);
	}

	std::sort(dirtySectors.begin(), dirtySectors.end(), SortDirtySectors());

	VDFile f(mPath.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kOpenExisting);

	DirtySectors::const_iterator it(dirtySectors.begin()), itEnd(dirtySectors.end());
	while(it != itEnd) {
		PhysSectorInfo *psi = *it;
		uint32 writeOffset = psi->mOffset;
		uint32 writeLen = psi->mSize;

		while(++it != itEnd) {
			psi = *it;

			if (psi->mOffset != writeOffset + writeLen)
				break;

			writeLen += psi->mSize;
		}

		f.seek(writeOffset + mReWriteOffset);
		f.write(&mImage[writeOffset], writeLen);
	}

	for(it = dirtySectors.begin(); it != itEnd; ++it) {
		PhysSectorInfo *psi = *it;

		psi->mbDirty = false;
	}

	mbDirty = false;
	return true;
}

void ATDiskImage::SetPathATR(const wchar_t *path) {
	mbDirty = true;
	mbDiskFormatDirty = true;
	mbHasDiskSource = true;
	mPath = path;
}

void ATDiskImage::SaveATR(const wchar_t *s) {
	// scan for virtual sectors with errors or phantoms -- we can't handle that.
	for(VirtSectors::const_iterator it(mVirtSectors.begin()), itEnd(mVirtSectors.end());
		it != itEnd;
		++it)
	{
		const VirtSectorInfo& vsi = *it;

		if (vsi.mNumPhysSectors != 1)
			throw MyError("Cannot save a disk which contains phantom or missing sectors.");
	}

	for(PhysSectors::const_iterator it(mPhysSectors.begin()), itEnd(mPhysSectors.end());
		it != itEnd;
		++it)
	{
		const PhysSectorInfo& psi = *it;

		if (psi.mFDCStatus != 0xFF)
			throw MyError("Cannot save a disk which contains deliberate sector errors.");
	}

	// create ATR header
	uint8 header[16] = {0};
	uint32 paras = mImage.size() >> 4;
	VDWriteUnalignedLEU16(header+0, 0x0296);
	VDWriteUnalignedLEU16(header+2, (uint16)paras);
	VDWriteUnalignedLEU16(header+4, mSectorSize);
	header[6] = (uint8)(paras >> 16);

	VDFile f(s, nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
	f.write(header, 16);

	for(VirtSectors::const_iterator it(mVirtSectors.begin()), itEnd(mVirtSectors.end());
		it != itEnd;
		++it)
	{
		const VirtSectorInfo& vsi = *it;
		const PhysSectorInfo& psi = mPhysSectors[vsi.mStartPhysSector];

		f.write(&mImage[psi.mOffset], psi.mSize);
	}

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
	mReWriteOffset = 16;
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

void ATDiskImage::ComputeSectorsPerTrack() {
	uint32 sectorCount = (uint32)mVirtSectors.size();
	mSectorsPerTrack = mSectorSize >= 256 ? 18 : sectorCount > 720 && !(sectorCount % 26) ? 26 : 18;
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