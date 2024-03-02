#include "stdafx.h"
#include <vd2/system/binary.h>
#include <vd2/system/strutil.h>
#include "diskfs.h"
#include "diskimage.h"

class ATDiskFSDOS2 : public IATDiskFS {
public:
	ATDiskFSDOS2();
	~ATDiskFSDOS2();

public:
	void Init(IATDiskImage *image, bool readOnly);
	void GetInfo(ATDiskFSInfo& info);

	bool IsReadOnly() { return mbReadOnly; }
	void SetReadOnly(bool readOnly);

	bool Validate(ATDiskFSValidationReport& report);
	void Flush();

	uintptr FindFirst(uintptr key, ATDiskFSEntryInfo& info);
	bool FindNext(uintptr searchKey, ATDiskFSEntryInfo& info);
	void FindEnd(uintptr searchKey);

	void GetFileInfo(uintptr key, ATDiskFSEntryInfo& info);
	uintptr GetParentDirectory(uintptr dirKey);

	uintptr LookupFile(uintptr parentKey, const char *filename);

	void DeleteFile(uintptr key);
	void ReadFile(uintptr key, vdfastvector<uint8>& dst);
	void WriteFile(uintptr parentKey, const char *filename, const void *src, uint32 len);
	void RenameFile(uintptr key, const char *newFileName);

protected:
	struct DirEnt;

	bool IsSectorAllocated(uint32 sector) const;
	void AllocateSector(uint32 sector);
	void FreeSector(uint32 sector);

	static void WriteFileName(DirEnt& de, const char *filename);
	static bool IsValidFileName(const char *filename);

	IATDiskImage *mpImage;
	bool mbDirty;
	bool mbReadOnly;

	struct DirEnt {
		enum {
			kFlagDeleted	= 0x80,
			kFlagInUse		= 0x40,
			kFlagLocked		= 0x20,
			kFlagDOS2		= 0x02,
			kFlagOpenWrite	= 0x01
		};

		uint16	mSectorCount;
		uint16	mFirstSector;
		uint32	mBytes;
		uint8	mFlags;
		char	mName[12];
	};

	struct FindHandle {
		uint32	mPos;
	};

	DirEnt	mDirectory[64];
	uint8	mSectorBuffer[256];
	uint8	mVTOCSector[256];

	vdfastvector<uint8> mTempSectorMap;
};

ATDiskFSDOS2::ATDiskFSDOS2() {
}

ATDiskFSDOS2::~ATDiskFSDOS2() {
}

void ATDiskFSDOS2::Init(IATDiskImage *image, bool readOnly) {
	mpImage = image;
	mbDirty = false;
	mbReadOnly = readOnly;

	uint32 sectorSize = image->GetSectorSize();
	if (sectorSize != 128 && sectorSize != 256)
		throw MyError("Unsupported sector size for DOS 2.x image: %d bytes.", sectorSize);

	// a 256 sector disk is MyDOS, so mount it read-only since we don't support it
	// fully correctly yet
	if (sectorSize == 256)
		mbReadOnly = true;

	// read VTOC
	mpImage->ReadVirtualSector(359, mVTOCSector, sectorSize);

	const uint32 sectorCount = mpImage->GetVirtualSectorCount();
	mTempSectorMap.resize(sectorCount, 0);

	// initialize directory
	memset(mDirectory, 0, sizeof mDirectory);

	uint32 fileId = 0;
	uint8 secBuf2[512];
	for(uint32 dirSector = 360; dirSector < 368; ++dirSector) {
		if (dirSector >= 364 && sectorSize > 128)
			break;

		if (sectorSize != mpImage->ReadVirtualSector(dirSector, mSectorBuffer, sectorSize)) {
			fileId += 8;
			continue;
		}

		const uint32 dirEntsPerSector = (sectorSize > 128) ? 16 : 8;
		for(uint32 i = 0; i < dirEntsPerSector; ++i, ++fileId) {
			const uint8 *dirent = mSectorBuffer + 16*i;
			const uint8 flags = dirent[0];

			// MyDOS hack
			if (sectorSize >= 256 && !flags)
				goto directory_end;

			DirEnt& de = mDirectory[fileId];

			de.mFlags = flags;
			de.mFirstSector = VDReadUnalignedLEU32(dirent + 3);

			const uint8 *fnstart = dirent + 5;
			const uint8 *fnend = dirent + 13;

			while(fnend != fnstart && fnend[-1] == 0x20)
				--fnend;

			char *namedst = de.mName;
			while(fnstart != fnend)
				*namedst++ = *fnstart++;

			const uint8 *extstart = dirent + 13;
			const uint8 *extend = dirent + 16;

			while(extend != extstart && extend[-1] == 0x20)
				--extend;

			if (extstart != extend) {
				*namedst++ = '.';

				while(extstart != extend)
					*namedst++ = *extstart++;
			}

			*namedst = 0;

			// The sector count in the directory can be WRONG, so we recompute it
			// from the sector chain.
			de.mSectorCount = 0;
			de.mBytes = 0;

			if (!(flags & 0x40))
				continue;

			std::fill(mTempSectorMap.begin(), mTempSectorMap.end(), 0);

			uint32 sector = de.mFirstSector;
			uint32 sectorSize = mpImage->GetSectorSize();
			for(;;) {
				if (!sector || sector > sectorCount)
					break;

				if (mTempSectorMap[sector - 1])
					break;

				mTempSectorMap[sector - 1] = 1;

				if (sectorSize != mpImage->ReadVirtualSector(sector - 1, secBuf2, sectorSize))
					break;

				const uint8 sectorDataBytes = sectorSize > 128 ? secBuf2[sectorSize - 1] : secBuf2[sectorSize - 1] & 127;
				const uint8 sectorFileId = secBuf2[sectorSize - 3] >> 2;

				if (fileId != sectorFileId || sectorDataBytes > (sectorSize - 3))
					break;

				++de.mSectorCount;
				de.mBytes += sectorDataBytes;
				sector = ((uint32)(secBuf2[sectorSize - 3] & 3) << 8) + secBuf2[sectorSize - 2];
			}
		}
	}

directory_end:
	;
}

void ATDiskFSDOS2::GetInfo(ATDiskFSInfo& info) {
	info.mFSType = "Atari DOS 2";
	info.mFreeBlocks = VDReadUnalignedLEU16(mVTOCSector + 3);
	info.mBlockSize = 128;
}

void ATDiskFSDOS2::SetReadOnly(bool readOnly) {
	mbReadOnly = readOnly;
}

bool ATDiskFSDOS2::Validate(ATDiskFSValidationReport& report) {
	bool errorsFound = false;

	report.mbBrokenFiles = false;
	report.mbBitmapIncorrect = false;

	const uint32 sectorCount = mpImage->GetVirtualSectorCount();
	const uint32 sectorSize = mpImage->GetSectorSize();

	uint8 newVTOC[256];

	memcpy(newVTOC, mVTOCSector, sectorSize);

	uint32 sectorsAvailable = 707;

	// mark all sectors as free
	memset(newVTOC + 10, 0xFF, 90);

	// allocate sectors 0 (reserved) and 1-3 (boot)
	newVTOC[10] &= 0x0f;

	// allocate sectors 360 (VTOC), 361-367 (directory)
	newVTOC[55] = 0;
	newVTOC[56] &= 0x7f;

	for(uint32 i=0; i<64; ++i) {
		const DirEnt& de = mDirectory[i];

		if (!(de.mFlags & DirEnt::kFlagInUse))
			continue;

		uint32 sector = de.mFirstSector;
		while(sector) {
			if (sector > sectorCount) {
				report.mbBrokenFiles = true;
				errorsFound = true;
				break;
			}

			uint8& vtocByte = newVTOC[10 + (sector >> 3)];
			const uint8& vtocBit = 0x80 >> (sector & 7);
			if (!(vtocByte & vtocBit)) {
				report.mbBrokenFiles = true;
				errorsFound = true;
				break;
			}

			// allocate sector
			vtocByte &= ~vtocBit;
			--sectorsAvailable;

			if (sectorSize != mpImage->ReadVirtualSector(sector - 1, mSectorBuffer, sectorSize)) {
				report.mbBrokenFiles = true;
				errorsFound = true;
				break;
			}

			const uint8 sectorDataBytes = sectorSize > 128 ? mSectorBuffer[sectorSize - 1] : mSectorBuffer[sectorSize - 1] & 127;
			const uint8 sectorFileId = mSectorBuffer[sectorSize - 3] >> 2;

			if (i != sectorFileId || sectorDataBytes > sectorSize - 3) {
				report.mbBrokenFiles = true;
				errorsFound = true;
				break;
			}

			sector = ((uint32)(mSectorBuffer[sectorSize - 3] & 3) << 8) + mSectorBuffer[sectorSize - 2];
		}
	}

	VDWriteUnalignedLEU16(newVTOC + 3, sectorsAvailable);

	if (memcmp(newVTOC, mVTOCSector, sectorSize)) {
		report.mbBitmapIncorrect = true;
		errorsFound = true;
	}

	return !errorsFound;
}

void ATDiskFSDOS2::Flush() {
	if (!mbDirty || mbReadOnly)
		return;

	// Reform and rewrite directory sectors.
	uint32 sectorSize = mpImage->GetSectorSize();
	uint32 dirSecCount = sectorSize >= 256 ? 4 : 8;
	uint32 dirEntsPerSec = sectorSize >= 256 ? 16 : 8;
	const DirEnt *pde = mDirectory;
	for(uint32 i=0; i<dirSecCount; ++i) {
		memset(mSectorBuffer, 0, sizeof mSectorBuffer);

		for(uint32 j=0; j<dirEntsPerSec; ++j) {
			const DirEnt& de = *pde++;

			if (!(de.mFlags & DirEnt::kFlagInUse))
				continue;

			uint8 *rawde = &mSectorBuffer[16*j];

			rawde[0] = de.mFlags;
			VDWriteUnalignedLEU16(rawde + 1, de.mSectorCount);
			VDWriteUnalignedLEU16(rawde + 3, de.mFirstSector);

			const char *ext = strchr(de.mName, '.');
			uint32 fnLen = ext ? (uint32)(ext - de.mName) : 8;

			memset(rawde + 5, 0x20, 11);
			memcpy(rawde + 5, de.mName, fnLen);

			if (ext)
				memcpy(rawde + 13, ext + 1, strlen(ext + 1));
		}

		mpImage->WriteVirtualSector(360 + i, mSectorBuffer, sectorSize);
	}

	// Update VTOC
	mpImage->WriteVirtualSector(359, mVTOCSector, sectorSize);

	mbDirty = false;
}

uintptr ATDiskFSDOS2::FindFirst(uintptr key, ATDiskFSEntryInfo& info) {
	if (key)
		return 0;

	FindHandle *h = new FindHandle;
	h->mPos = 0;

	if (!FindNext((uintptr)h, info)) {
		delete h;
		return 0;
	}

	return (uintptr)h;
}

bool ATDiskFSDOS2::FindNext(uintptr searchKey, ATDiskFSEntryInfo& info) {
	FindHandle *h = (FindHandle *)searchKey;

	while(h->mPos < 64) {
		const DirEnt& de = mDirectory[h->mPos++];

		if (!(de.mFlags & DirEnt::kFlagInUse))
			continue;

		info.mFileName	= VDTextAToW(de.mName);
		info.mSectors	= de.mSectorCount;
		info.mBytes		= de.mBytes;
		info.mKey		= h->mPos;
		info.mbIsDirectory = false;
		info.mbDateValid = false;	
		return true;
	}

	return false;
}

void ATDiskFSDOS2::FindEnd(uintptr searchKey) {
	delete (FindHandle *)searchKey;
}

void ATDiskFSDOS2::GetFileInfo(uintptr key, ATDiskFSEntryInfo& info) {
	const DirEnt& de = mDirectory[key - 1];

	info.mFileName	= VDTextAToW(de.mName);
	info.mSectors	= de.mSectorCount;
	info.mBytes		= de.mBytes;
	info.mKey		= key;
	info.mbIsDirectory = false;
	info.mbDateValid = false;
}

uintptr ATDiskFSDOS2::GetParentDirectory(uintptr dirKey) {
	return 0;
}

uintptr ATDiskFSDOS2::LookupFile(uintptr parentKey, const char *filename) {
	if (parentKey)
		return 0;

	for(uint32 i=0; i<64; ++i) {
		const DirEnt& de = mDirectory[i];

		if (!(de.mFlags & DirEnt::kFlagInUse))
			continue;

		if (!vdstricmp(de.mName, filename))
			return (uintptr)(i + 1);
	}

	return 0;
}

void ATDiskFSDOS2::DeleteFile(uintptr key) {
	if (mbReadOnly)
		throw ATDiskFSException(kATDiskFSError_ReadOnly);

	VDASSERT(key >= 1 && key <= 64);

	uint8 fileId = (uint8)key - 1;
	DirEnt& de = mDirectory[fileId];

	if (!(de.mFlags & DirEnt::kFlagInUse))
		return;

	vdfastvector<uint32> sectorsToFree;
	std::fill(mTempSectorMap.begin(), mTempSectorMap.end(), 0);

	const uint32 sectorSize = mpImage->GetSectorSize();
	uint32 sector = de.mFirstSector;
	while(sector) {
		if (sector > mTempSectorMap.size())
			throw ATDiskFSException(kATDiskFSError_CorruptedFileSystem);

		if (!IsSectorAllocated(sector))
			throw ATDiskFSException(kATDiskFSError_CorruptedFileSystem);

		if (mTempSectorMap[sector - 1])
			throw ATDiskFSException(kATDiskFSError_CorruptedFileSystem);

		mTempSectorMap[sector - 1] = 1;

		if (128 != mpImage->ReadVirtualSector(sector - 1, mSectorBuffer, sectorSize))
			throw ATDiskFSException(kATDiskFSError_CorruptedFileSystem);

		const uint8 sectorDataBytes = sectorSize > 128 ? mSectorBuffer[sectorSize - 1] : mSectorBuffer[sectorSize - 1] & 127;
		const uint8 sectorFileId = mSectorBuffer[sectorSize - 3] >> 2;

		if (fileId != sectorFileId || sectorDataBytes > sectorSize - 3)
			throw ATDiskFSException(kATDiskFSError_CorruptedFileSystem);

		sectorsToFree.push_back(sector);

		sector = ((uint32)(mSectorBuffer[sectorSize - 3] & 3) << 8) + mSectorBuffer[sectorSize - 2];
	}

	// free sectors
	while(!sectorsToFree.empty()) {
		uint32 sec = sectorsToFree.back();
		sectorsToFree.pop_back();

		FreeSector(sec);
	}

	// free directory entry
	de.mFlags &= ~DirEnt::kFlagInUse;
	de.mFlags |= DirEnt::kFlagDeleted;

	mbDirty = true;
}

void ATDiskFSDOS2::ReadFile(uintptr key, vdfastvector<uint8>& dst) {
	VDASSERT(key >= 1 && key <= 64);

	const uint8 fileId = (uint8)key - 1;
	const DirEnt& de = mDirectory[fileId];

	uint32 sector = de.mFirstSector;

	std::fill(mTempSectorMap.begin(), mTempSectorMap.end(), 0);

	dst.clear();

	uint32 sectorSize = mpImage->GetSectorSize();
	while(sector) {
		if (sector > mTempSectorMap.size())
			throw ATDiskFSException(kATDiskFSError_CorruptedFileSystem);

		if (mTempSectorMap[sector - 1] || !IsSectorAllocated(sector))
			throw ATDiskFSException(kATDiskFSError_CorruptedFileSystem);

		mTempSectorMap[sector - 1] = 1;

		if (sectorSize != mpImage->ReadVirtualSector(sector - 1, mSectorBuffer, sectorSize))
			throw ATDiskFSException(kATDiskFSError_CorruptedFileSystem);

		const uint8 sectorDataBytes = sectorSize > 128 ? mSectorBuffer[sectorSize - 1] : mSectorBuffer[sectorSize - 1] & 127;
		const uint8 sectorFileId = mSectorBuffer[sectorSize - 3] >> 2;

		if (fileId != sectorFileId || sectorDataBytes > sectorSize - 3)
			throw ATDiskFSException(kATDiskFSError_CorruptedFileSystem);

		dst.insert(dst.end(), mSectorBuffer, mSectorBuffer + sectorDataBytes);

		sector = ((uint32)(mSectorBuffer[sectorSize - 3] & 3) << 8) + mSectorBuffer[sectorSize - 2];
	}
}

void ATDiskFSDOS2::WriteFile(uintptr parentKey, const char *filename, const void *src, uint32 len) {
	if (mbReadOnly)
		throw ATDiskFSException(kATDiskFSError_ReadOnly);

	if (!IsValidFileName(filename))
		throw ATDiskFSException(kATDiskFSError_InvalidFileName);

	if (LookupFile(parentKey, filename))
		throw ATDiskFSException(kATDiskFSError_FileExists);

	// find an empty directory entry
	uint32 dirIdx = 0;
	for(;;) {
		if (!(mDirectory[dirIdx].mFlags & DirEnt::kFlagInUse))
			break;

		++dirIdx;
		if (dirIdx >= 64)
			throw ATDiskFSException(kATDiskFSError_DirectoryFull);
	}

	// find free sectors
	const uint32 sectorSize = mpImage->GetSectorSize();
	const uint32 dataBytesPerSector = sectorSize - 3;

	vdfastvector<uint32> sectorsToUse;
	uint32 sectorCount = (len + dataBytesPerSector - 1) / dataBytesPerSector;
	uint32 sectorsToAllocate = sectorCount;
	for(uint32 i = 1; i < 720; ++i) {
		if (mVTOCSector[10 + (i >> 3)] & (0x80 >> (i & 7))) {
			sectorsToUse.push_back(i);

			if (!--sectorsToAllocate)
				break;
		}
	}

	if (sectorsToAllocate)
		throw ATDiskFSException(kATDiskFSError_DiskFull);

	// write data sectors
	for(uint32 i=0; i<sectorCount; ++i) {
		uint32 offset = dataBytesPerSector*i;
		uint32 dataBytes = len - offset;

		if (dataBytes > dataBytesPerSector)
			dataBytes = dataBytesPerSector;

		memcpy(mSectorBuffer, (const char *)src + offset, dataBytes);
		memset(mSectorBuffer + dataBytes, 0, (sectorSize - 3) - dataBytes);

		uint32 nextSector = (i == sectorCount - 1) ? 0 : sectorsToUse[i + 1];

		mSectorBuffer[sectorSize - 3] = (dirIdx << 2) + (nextSector >> 8);
		mSectorBuffer[sectorSize - 2] = (uint8)nextSector;
		mSectorBuffer[sectorSize - 1] = sectorSize > 128 ? dataBytes : dataBytes < 125 ? 0x80 + dataBytes : 125;

		uint32 sector = sectorsToUse[i];
		mpImage->WriteVirtualSector(sector - 1, mSectorBuffer, sectorSize);

		AllocateSector(sector);
	}

	// write directory entry
	DirEnt& de = mDirectory[dirIdx];
	de.mFlags = DirEnt::kFlagDOS2 | DirEnt::kFlagInUse;
	de.mBytes = len;
	de.mSectorCount = sectorCount;
	de.mFirstSector = sectorCount ? sectorsToUse.front() : 0;

	WriteFileName(de, filename);

	mbDirty = true;
}

void ATDiskFSDOS2::RenameFile(uintptr key, const char *filename) {
	if (mbReadOnly)
		throw ATDiskFSException(kATDiskFSError_ReadOnly);

	if (!IsValidFileName(filename))
		throw ATDiskFSException(kATDiskFSError_InvalidFileName);

	uintptr conflictingKey = LookupFile(0, filename);

	if (conflictingKey == key)
		return;

	if (conflictingKey)
		throw ATDiskFSException(kATDiskFSError_FileExists);

	WriteFileName(mDirectory[key - 1], filename);
	mbDirty = true;
}

bool ATDiskFSDOS2::IsSectorAllocated(uint32 sector) const {
	const uint32 mask = mVTOCSector[10 + (sector >> 3)];
	const uint8 bit = (0x80 >> (sector & 7));
	return !(mask & bit);
}

void ATDiskFSDOS2::AllocateSector(uint32 sector) {
	mVTOCSector[10 + (sector >> 3)] &= ~(0x80 >> (sector & 7));

	VDWriteUnalignedLEU16(mVTOCSector + 3, VDReadUnalignedLEU16(mVTOCSector + 3) - 1);
}

void ATDiskFSDOS2::FreeSector(uint32 sector) {
	mVTOCSector[10 + (sector >> 3)] |= (0x80 >> (sector & 7));

	VDWriteUnalignedLEU16(mVTOCSector + 3, VDReadUnalignedLEU16(mVTOCSector + 3) + 1);
}

void ATDiskFSDOS2::WriteFileName(DirEnt& de, const char *filename) {
	for(int i=0; i<12; ++i) {
		uint8 c = filename[i];

		if ((uint8)(c - 0x61) < 26)
			c &= 0xdf;

		de.mName[i] = c;
		if (!c)
			break;
	}

	de.mName[12] = 0;
}

bool ATDiskFSDOS2::IsValidFileName(const char *filename) {

	// first character must be alphanumeric
	if ((uint8)(((uint8)*filename++ & 0xdf) - 0x41) >= 26)
		return false;

	// up to 7 alphanumeric characters may follow
	int count = 0;

	for(;;) {
		uint8 c = *filename;

		if ((uint8)(c - 0x30) >= 10 && (uint8)((c & 0xdf) - 0x41) >= 26)
			break;

		++filename;

		if (++count > 7)
			return false;
	}

	// next needs to be EOS or a period
	if (!*filename)
		return true;

	if (*filename++ != '.')
		return false;

	// up to 3 alphanumeric characters may follow
	count = 0;

	for(;;) {
		uint8 c = *filename++;

		if ((uint8)(c - 0x30) >= 10 && (uint8)((c & 0xdf) - 0x41) >= 26)
			break;

		if (++count > 3)
			return false;
	}

	// looks OK
	return true;
}

///////////////////////////////////////////////////////////////////////////

IATDiskFS *ATDiskMountImageDOS2(IATDiskImage *image, bool readOnly) {
	ATDiskFSDOS2 *fs = new ATDiskFSDOS2;

	fs->Init(image, readOnly);

	return fs;
}
