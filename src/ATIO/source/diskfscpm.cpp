//	Altirra - Atari 800/800XL/5200 emulator
//	I/O library - Indus GT CP/M 2.2 filesystem handler
//	Copyright (C) 2009-2021 Avery Lee
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
#include <bitset>
#include <vd2/system/bitmath.h>
#include <vd2/system/binary.h>
#include <vd2/system/strutil.h>
#include <vd2/system/vdalloc.h>
#include <at/atio/diskfs.h>
#include <at/atio/diskimage.h>

class ATDiskFSCPM final : public IATDiskFS {
public:
	ATDiskFSCPM();
	~ATDiskFSCPM();

public:
	void Init(IATDiskImage *image, bool readOnly);
	void GetInfo(ATDiskFSInfo& info) override;

	bool IsReadOnly() override { return mbReadOnly; }
	void SetReadOnly(bool readOnly) override;
	void SetAllowExtend(bool allow) override {}
	void SetStrictNameChecking(bool strict) override {}

	bool Validate(ATDiskFSValidationReport& report) override;
	void Flush() override;

	ATDiskFSFindHandle FindFirst(ATDiskFSKey key, ATDiskFSEntryInfo& info) override;
	bool FindNext(ATDiskFSFindHandle searchKey, ATDiskFSEntryInfo& info) override;
	void FindEnd(ATDiskFSFindHandle searchKey) override;

	void GetFileInfo(ATDiskFSKey key, ATDiskFSEntryInfo& info) override;
	ATDiskFSKey GetParentDirectory(ATDiskFSKey dirKey) override;

	ATDiskFSKey LookupFile(ATDiskFSKey parentKey, const char *filename) override;

	void DeleteFile(ATDiskFSKey key) override;
	void ReadFile(ATDiskFSKey key, vdfastvector<uint8>& dst) override;
	ATDiskFSKey WriteFile(ATDiskFSKey parentKey, const char *filename, const void *src, uint32 len) override;
	void RenameFile(ATDiskFSKey key, const char *newFileName) override;
	void SetFileTimestamp(ATDiskFSKey key, const VDExpandedDate& date) override {}

	ATDiskFSKey CreateDir(ATDiskFSKey parentKey, const char *filename) override;

protected:
	struct DecodedName {
		char s[13];
	};

	struct EncodedName {
		uint8 s[11];

		auto operator<=>(const EncodedName& other) const = default;
	};

	struct EncodedNameHashPred {
		bool operator()(const EncodedName& x, const EncodedName& y) const {
			return x == y;
		}

		size_t operator()(const EncodedName& x) const {
			constexpr size_t factor = sizeof(size_t) > 4 ? 57 : 7;
			size_t hash = 0;

			for(const uint8 c : x.s) {
				hash = hash * factor + (size_t)c;
			}

			return hash;
		}
	};

	struct DirEnt {
		uint8	mStatus;
		EncodedName mName;
		uint8	mExtentLow;
		uint8	mByteCount;
		uint8	mExtentHigh;
		uint8	mRecordCount;
		uint8	mBlocks[16];

		bool IsValidFile() const {
			return mStatus < 0x10;
		}

		bool IsInUse() const {
			return mStatus != 0xE5;
		}

		uint16 GetExtent() const {
			return (mExtentLow & 31) + ((mExtentHigh & 15) << 5);
		}

		void SetExtent(uint16 i) {
			mExtentLow = (uint8)(i & 31);
			mExtentHigh = (uint8)(i >> 5);
		}
	};

	static_assert(sizeof(DirEnt) == 32);

	struct FindHandle {
		uint32	mPos;
	};
	
	ATDiskFSKey LookupEntry(const char *filename) const;
	ATDiskFSKey LookupEntry(const EncodedName& en) const;

	static bool EncodeFileName(EncodedName& en, const char *filename);
	static DecodedName DecodeFileName(const EncodedName& en);
	bool IsValidFileName(const char *filename) const;
	void RecomputeBlockBitmap();

	vdrefptr<IATDiskImage> mpImage;
	bool mbDirty {};
	bool mbReadOnly {};
	bool mbBlockOverlapDetected {};
	bool mbInvalidBlockDetected {};
	uint32 mVolumeOffset {};
	uint32 mReservedBlocks {};
	uint32 mTotalBlocks {};
	uint32 mFreeBlocks {};
	uint32 mNextAlloc {};

	vdfastvector<bool> mBlockBitmap;

	bool	mbDirectoryDirty;
	DirEnt	mDirectory[64];
	sint32	mFileSizes[64];
	static_assert(sizeof(mDirectory) == 2048);

	uint8	mSectorBuffer[256];
};

ATDiskFSCPM::ATDiskFSCPM() {
}

ATDiskFSCPM::~ATDiskFSCPM() {
}

void ATDiskFSCPM::Init(IATDiskImage *image, bool readOnly) {
	mpImage = image;
	mbDirty = false;
	mbReadOnly = readOnly;

	const uint32 sectorSize = image->GetSectorSize();
	if (sectorSize != 256)
		throw MyError("Unsupported sector size for CP/M image: %d bytes.", sectorSize);

	const uint32 sectorCount = image->GetVirtualSectorCount();
	if (sectorCount < 64 || sectorCount > 44 + 256*4)
		throw MyError("Unsupported sector count for CP/M image: %u sectors.", sectorCount);

	// directory is included in block indexing
	mVolumeOffset = 36;
	mReservedBlocks = 2;
	mTotalBlocks = (sectorCount - 36) >> 2;
	mNextAlloc = mReservedBlocks;

	// read directory
	for(sint32& v : mFileSizes)
		v = -1;

	uint8 secBuf[256];
	for(int i=0; i<8; ++i) {
		mpImage->ReadVirtualSector(mVolumeOffset + i, secBuf, 256);

		for(uint8& v : secBuf)
			v = ~v;

		memcpy(mDirectory + i*8, secBuf, 256);
	}

	RecomputeBlockBitmap();

	// compute file sizes
	vdhashmap<EncodedName, uint8, EncodedNameHashPred, EncodedNameHashPred> nameMap;
	for(int i=0; i<64; ++i) {
		const DirEnt& de = mDirectory[i];

		if (de.IsValidFile()) {
			// track the highest extent seen for each filename
			auto r = nameMap.insert(de.mName);
			if (r.second || de.GetExtent() > mDirectory[r.first->second].GetExtent())
				r.first->second = (uint8)i;
		}
	}

	for(const auto& nameEntry : nameMap) {
		uint8 idx = nameEntry.second;
		const DirEnt& de = mDirectory[idx];

		if (!de.IsValidFile())
			continue;

		uint32 extentOffset = de.GetExtent() * 16384;
		uint32 numRecords = std::min<uint32>(de.mRecordCount, 128);
		uint32 fileSize = extentOffset + numRecords * 128;

		if (numRecords) {
			const uint32 lastBlock = de.mBlocks[(numRecords - 1) >> 3];
			
			if (lastBlock >= mReservedBlocks && lastBlock < mTotalBlocks) {
				// read the sector containing the last record
				if (mpImage->ReadVirtualSector(mVolumeOffset + (lastBlock * 4) + (((numRecords - 1) & 7) >> 1), secBuf, 256) >= 256) {
					const uint8 *record = secBuf + (numRecords & 1 ? 0 : 128);
					uint32 lastRecordLen = 128;

					while(lastRecordLen && record[lastRecordLen - 1] == 0xE5)
						--lastRecordLen;

					fileSize = (fileSize - 128) + lastRecordLen;
				}
			}
		}

		mFileSizes[idx] = fileSize;
	}
}

void ATDiskFSCPM::GetInfo(ATDiskFSInfo& info) {
	info.mFSType = "CP/M 2.2";
	info.mFreeBlocks = mFreeBlocks;
	info.mBlockSize = 1024;
}

void ATDiskFSCPM::SetReadOnly(bool readOnly) {
	mbReadOnly = readOnly;
}

bool ATDiskFSCPM::Validate(ATDiskFSValidationReport& report) {
	bool errorsFound = false;

	report = {};

	if (mbInvalidBlockDetected) {
		report.mbBrokenFiles = true;
		errorsFound = true;
	}

	if (mbBlockOverlapDetected) {
		report.mbBrokenFiles = true;
		errorsFound = true;
	}

	return !errorsFound;
}

void ATDiskFSCPM::Flush() {
	if (!mbDirty)
		return;

	mbDirty = true;

	uint8 secBuf[256];

	for(int i=0; i<8; ++i) {
		memcpy(secBuf, &mDirectory[i * 8], 256);

		for(uint8& v : secBuf)
			v = ~v;

		mpImage->WriteVirtualSector(mVolumeOffset + i, secBuf, 256);
	}
}

ATDiskFSFindHandle ATDiskFSCPM::FindFirst(ATDiskFSKey key, ATDiskFSEntryInfo& info) {
	vdautoptr<FindHandle> h(new FindHandle);
	h->mPos = 0;

	if (!FindNext((ATDiskFSFindHandle)(uintptr)h.get(), info)) {
		return ATDiskFSFindHandle::Invalid;
	}

	return (ATDiskFSFindHandle)(uintptr)h.release();
}

bool ATDiskFSCPM::FindNext(ATDiskFSFindHandle searchKey, ATDiskFSEntryInfo& info) {
	FindHandle *h = (FindHandle *)searchKey;

	while(h->mPos < 64) {
		if (mFileSizes[h->mPos++] >= 0) {
			GetFileInfo((ATDiskFSKey)(h->mPos + 63), info);
			return true;
		}
	}

	return false;
}

void ATDiskFSCPM::FindEnd(ATDiskFSFindHandle searchKey) {
	delete (FindHandle *)searchKey;
}

void ATDiskFSCPM::GetFileInfo(ATDiskFSKey key, ATDiskFSEntryInfo& info) {
	const uint32 idx = (uint32)key & 63;
	const DirEnt& de = mDirectory[idx];

	info.mFileName	= DecodeFileName(de.mName).s;
	info.mSectors	= (mFileSizes[idx] + 1023) >> 10;
	info.mBytes		= mFileSizes[idx];
	info.mKey		= key;
	info.mbIsDirectory = false;
	info.mbDateValid = false;
}

ATDiskFSKey ATDiskFSCPM::GetParentDirectory(ATDiskFSKey dirKey) {
	return ATDiskFSKey::None;
}

ATDiskFSKey ATDiskFSCPM::LookupFile(ATDiskFSKey parentKey, const char *filename) {
	return LookupEntry(filename);
}

ATDiskFSKey ATDiskFSCPM::LookupEntry(const char *filename) const {
	EncodedName en;

	if (!EncodeFileName(en, filename))
		return ATDiskFSKey::None;

	return LookupEntry(en);
}

ATDiskFSKey ATDiskFSCPM::LookupEntry(const EncodedName& en) const {
	for(uint32 i=0; i<64; ++i) {
		const DirEnt& de = mDirectory[i];

		if (de.mStatus >= 0x10)
			continue;

		if (mFileSizes[i] < 0)
			continue;

		// do case insensitive compare
		bool match = true;

		for(int i=0; i<11; ++i) {
			uint8 c = en.s[i];
			uint8 d = de.mName.s[i];
			uint8 err = c ^ d;
			uint8 mask = ((c - 0x41) & 0x5F) < 26 ? 0x5F : 0x7F;

			if (err ^ mask) {
				match = false;
				break;
			}
		}

		if (match)
			return (ATDiskFSKey)(i + 64);
	}

	return ATDiskFSKey::None;
}

void ATDiskFSCPM::DeleteFile(ATDiskFSKey key) {
	if (mbReadOnly)
		throw ATDiskFSException(kATDiskFSError_ReadOnly);

	const uint8 fileId = (uint8)((uint32)key & 63);
	const DirEnt& de = mDirectory[fileId];
	const EncodedName en = de.mName;

	for(DirEnt& de : mDirectory) {
		if (de.mName == en)
			memset(&de, 0xE5, sizeof de);
	}
	
	mbDirty = true;
	mFileSizes[fileId] = -1;

	RecomputeBlockBitmap();
}

void ATDiskFSCPM::ReadFile(ATDiskFSKey key, vdfastvector<uint8>& dst) {
	VDASSERT(key != ATDiskFSKey::None);

	const uint8 fileId = (uint8)((uint32)key & 63);
	const DirEnt& de = mDirectory[fileId];

	vdfastvector<const DirEnt *> extentMap;

	for(const DirEnt& de2 : mDirectory) {
		if (!memcmp(de.mName.s, de2.mName.s, 11)) {
			uint16 extNum = de.GetExtent();

			if (extentMap.size() <= extNum)
				extentMap.resize(extNum + 1, nullptr);

			extentMap[extNum] = &de2;
		}
	}

	uint32 extentOffset = 0;

	for(const DirEnt *extent : extentMap) {
		const uint32 numRecords = std::min<uint32>(extent->mRecordCount, 128);
		const uint32 numBlocks = (numRecords + 7) >> 3;

		if (!numBlocks)
			continue;

		uint32 extentEnd = extentOffset + 1024 * numBlocks;
		dst.resize(extentEnd, 0x1A);

		for(uint32 i = 0; i < 16; ++i) {
			if (extent->mBlocks[i]) {
				for(uint32 j = 0; j < 4; ++j) {
					if (256 != mpImage->ReadVirtualSector(mVolumeOffset + extent->mBlocks[i] * 4 + j, &dst[extentOffset + 1024 * i + 256 * j], 256))
						throw ATDiskFSException(kATDiskFSError_CorruptedFileSystem);
				}
			}
		}

		dst.resize(extentOffset + 128 * numRecords);

		for(uint32 i = 0; i < 128; ++i) {
			if (dst.back() != 0xE5)
				break;

			dst.pop_back();
		}
	}

	for(uint8& v : dst)
		v = ~v;
}

ATDiskFSKey ATDiskFSCPM::WriteFile(ATDiskFSKey parentKey, const char *filename, const void *src, uint32 len) {
	if (mbReadOnly)
		throw ATDiskFSException(kATDiskFSError_ReadOnly);

	EncodedName en;
	if (!EncodeFileName(en, filename))
		throw ATDiskFSException(kATDiskFSError_InvalidFileName);

	if (LookupEntry(en) != ATDiskFSKey::None)
		throw ATDiskFSException(kATDiskFSError_FileExists);

	// compute the total number of records, blocks, and extents in the file
	const uint32 totalRecords = (len + 127) >> 7;
	const uint32 totalBlocks = (totalRecords + 7) >> 3;
	const uint32 totalExtents = (totalBlocks + 15) >> 4;

	// create the block and extent maps for all block/extent maps that are non-empty
	vdfastvector<uint32> blockMap(totalExtents*16, 0);
	vdfastvector<sint32> extentMap(totalExtents, -1);
	const uint8 *src8 = (const uint8 *)src;
	uint32 nextAlloc = mNextAlloc;
	uint32 nextExtent = 0;

	for(uint32 i=0; i<totalBlocks; ++i) {
		uint32 blockStart = i * 1024;
		uint32 blockEnd = std::min<uint32>(blockStart + 1024, len);
		bool isBlockEmpty = true;

		for(uint32 i = blockStart; i < blockEnd; ++i) {
			if (src8[i] != 0x1A) {
				isBlockEmpty = false;
				break;
			}
		}

		if (!isBlockEmpty) {
			for(;;) {
				uint32 block = nextAlloc;
				if (++nextAlloc >= mTotalBlocks)
					nextAlloc = mReservedBlocks;

				if (!mBlockBitmap[block]) {
					blockMap[i] = block;
					break;
				}

				if (nextAlloc == mNextAlloc)
					throw ATDiskFSException(kATDiskFSError_DiskFull);
			}
		}

		// allocate extent if needed -- we always need at least one
		if (!isBlockEmpty || (i == totalBlocks - 1)) {
			uint32 extentIdx = i >> 4;
			if (extentMap[extentIdx] < 0) {
				while(mDirectory[nextExtent].IsInUse()) {
					if (++nextExtent >= 64)
						throw ATDiskFSException(kATDiskFSError_DirectoryFull);
				}

				extentMap[extentIdx] = nextExtent;
			}
		}
	}

	// now that we know we can fit everything, write non-empty blocks to disk
	uint8 secBuf[256];
	for(uint32 i=0; i<totalBlocks; ++i) {
		const uint32 block = blockMap[i];

		if (!block)
			continue;

		uint32 sectorOffset = i * 1024;
		for(uint32 j=0; j<4; ++j, sectorOffset += 256) {
			uint32 validLen = sectorOffset < len ? std::min<uint32>(256, len - sectorOffset) : 0;

			if (validLen)
				memcpy(secBuf, src8 + sectorOffset, validLen);

			if (validLen < 256)
				memset(secBuf + validLen, 0x1A, 256 - validLen);

			for(uint8& v : secBuf)
				v = ~v;

			mpImage->WriteVirtualSector(mVolumeOffset + block * 4 + j, secBuf, 256);
		}
	}

	// write extents to the directory
	for(uint32 i = 0; i < totalExtents; ++i) {
		sint32 extentIdx = extentMap[i];
		if (extentIdx < 0)
			continue;

		DirEnt& de = mDirectory[extentIdx];

		de.mStatus = 0;
		de.mName = en;
		de.mByteCount = 0;
		de.mRecordCount = std::min<uint8>(totalRecords - i*128, 128);
		de.SetExtent(i);
		for(int j=0; j<16; ++j)
			de.mBlocks[j] = (uint8)blockMap[i * 16 + j];
	}

	// allocate blocks from the bitmap
	for(uint32& idx : blockMap) {
		if (idx) {
			mBlockBitmap[idx] = true;
			--mFreeBlocks;
		}
	}

	mbDirty = true;

	if (extentMap.back() >= 0) {
		mFileSizes[extentMap.back()] = len;
	} else {
		VDFAIL("Last extent was not allocated (should have been forced in block scan)");
	}

	return ATDiskFSKey::None;
}

void ATDiskFSCPM::RenameFile(ATDiskFSKey key, const char *filename) {
	if (mbReadOnly)
		throw ATDiskFSException(kATDiskFSError_ReadOnly);

	EncodedName newName;
	if (!EncodeFileName(newName, filename))
		throw ATDiskFSException(kATDiskFSError_InvalidFileName);

	EncodedName oldName = mDirectory[(uint32)key & 63].mName;

	for(DirEnt& de : mDirectory) {
		if (de.mName == oldName)
			de.mName = newName;
	}

	mbDirty = true;
}

ATDiskFSKey ATDiskFSCPM::CreateDir(ATDiskFSKey parentKey, const char *filename) {
	throw ATDiskFSException(kATDiskFSError_NotSupported);
}

bool ATDiskFSCPM::EncodeFileName(EncodedName& en, const char *filename) {
	static constexpr struct CharMask {
		uint32 mask[4];

		constexpr void set() {
			for(uint32& v : mask)
				v = ~UINT32_C(0);
		}

		constexpr void reset(uint32 i) {
			mask[i >> 5] &= ~(UINT32_C(1) << (i & 31));
		}

		constexpr bool operator[](uint32 i) const {
			return (mask[i >> 5] & (UINT32_C(1) << (i & 31))) != 0;
		}
	} kValidChars = []() constexpr -> CharMask {
		CharMask validSet {};

		validSet.set();

		for(char c : "<>.,;:=?*[]")
			validSet.reset((uint8)c);

		for(int i=0; i<32; ++i)
			validSet.reset(i);

		validSet.reset(127);

		return validSet;
	}();

	memset(en.s, 0x20, sizeof en.s);

	// can't be empty
	if (!*filename)
		return false;

	// up to 8 characters are allowed in the filename
	int count = 0;

	for(;;) {
		uint8 c = (uint8)*filename;

		if (c >= 0x80 || !kValidChars[c])
			break;
		
		++filename;

		if (count >= 8)
			return false;

		if (c >= 0x61 && c <= 0x7A)
			c -= 0x20;

		en.s[count++] = (uint8)c;
	}

	// next needs to be EOS or a period
	if (!*filename)
		return true;

	if (*filename++ != '.')
		return false;

	// up to 3 alphanumeric characters may follow
	count = 8;

	for(;;) {
		uint8 c = *filename++;

		if (!c)
			break;

		if (c >= 0x80 || !kValidChars[c])
			return false;

		if (count >= 11)
			return false;

		if (c >= 0x61 && c <= 0x7A)
			c -= 0x20;

		en.s[count++] = c;
	}

	// looks OK
	return true;
}

ATDiskFSCPM::DecodedName ATDiskFSCPM::DecodeFileName(const EncodedName& en) {
	DecodedName dn;

	int fnlen = 8;
	while(fnlen && en.s[fnlen - 1] == 0x20)
		--fnlen;

	int extlen = 3;
	while(extlen && en.s[extlen + 7] == 0x20)
		--extlen;

	memcpy(dn.s, en.s, fnlen);
	if (extlen) {
		dn.s[fnlen] = '.';
		memcpy(dn.s + fnlen + 1, en.s + 8, extlen);
		dn.s[fnlen + extlen + 1] = 0;
	} else {
		dn.s[fnlen] = 0;
	}

	return dn;
}

bool ATDiskFSCPM::IsValidFileName(const char *filename) const {
	EncodedName en;

	return EncodeFileName(en, filename);
}

void ATDiskFSCPM::RecomputeBlockBitmap() {
	// initialize the block bitmap
	mBlockBitmap.resize(mTotalBlocks, false);

	for(uint32 i = 0; i < mReservedBlocks; ++i)
		mBlockBitmap[i] = true;

	mbBlockOverlapDetected = false;
	mbInvalidBlockDetected = false;

	mFreeBlocks = mTotalBlocks - mReservedBlocks;

	for(const DirEnt& de : mDirectory) {
		if (de.mStatus != 0xE5) {
			for(uint32 blockIdx : de.mBlocks) {
				if (!blockIdx)
					continue;

				if (blockIdx < mReservedBlocks || blockIdx >= mTotalBlocks)
					mbInvalidBlockDetected = true;
				else if (mBlockBitmap[blockIdx])
					mbBlockOverlapDetected = true;
				else {
					mBlockBitmap[blockIdx] = true;
					--mFreeBlocks;
				}
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////

IATDiskFS *ATDiskMountImageCPM(IATDiskImage *image, bool readOnly) {
	vdautoptr<ATDiskFSCPM> fs(new ATDiskFSCPM);

	fs->Init(image, readOnly);

	return fs.release();
}
