#include <stdafx.h>
#include <vd2/system/binary.h>
#include <vd2/system/math.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/filewatcher.h>
#include <vd2/system/strutil.h>
#include <vd2/system/time.h>
#include <at/atio/diskimage.h>
#include <at/atio/diskfsdos2util.h>
#include "debuggerlog.h"
#include "hostdeviceutils.h"

ATDebuggerLogChannel g_ATLCVDisk(false, false, "VDISK", "Virtual disk activity");

namespace {
	static const uint8 kTrackInterleave18[18]={
		0, 9, 1, 10, 2, 11, 3, 12, 4, 13, 5, 14, 6, 15, 7, 16, 8, 17
	};
}

class ATDiskImageVirtualFolder final : public IATDiskImage, public IVDTimerCallback {
public:
	ATDiskImageVirtualFolder();

	void Init(const wchar_t *path);

	ATDiskTimingMode GetTimingMode() const override { return kATDiskTimingMode_Any; }

	bool IsDirty() const override { return false; }
	bool IsUpdatable() const override { return false; }
	bool IsDynamic() const override { return true; }
	ATDiskImageFormat GetImageFormat() const override { return kATDiskImageFormat_None; }

	bool Flush() override { return true; }

	void SetPath(const wchar_t *path) override;
	void Save(const wchar_t *path, ATDiskImageFormat format) override;

	ATDiskGeometryInfo GetGeometry() const override;
	uint32 GetSectorSize() const override;
	uint32 GetSectorSize(uint32 virtIndex) const override;
	uint32 GetBootSectorCount() const override;

	uint32 GetPhysicalSectorCount() const override;
	void GetPhysicalSectorInfo(uint32 index, ATDiskPhysicalSectorInfo& info) const override;

	void ReadPhysicalSector(uint32 index, void *data, uint32 len) override;
	void WritePhysicalSector(uint32 index, const void *data, uint32 len) override;

	uint32 GetVirtualSectorCount() const override;
	void GetVirtualSectorInfo(uint32 index, ATDiskVirtualSectorInfo& info) const override;

	uint32 ReadVirtualSector(uint32 index, void *data, uint32 len) override;
	bool WriteVirtualSector(uint32 index, const void *data, uint32 len) override;

	void Resize(uint32 sectors) override;

public:
	void TimerCallback() override;

protected:
	void UpdateDirectory(bool reportNewFiles);

	struct DirEnt {
		enum {
			kFlagDeleted	= 0x80,
			kFlagInUse		= 0x40,
			kFlagLocked		= 0x20,
			kFlagDOS2		= 0x02,
			kFlagOpenWrite	= 0x01
		};

		uint8	mFlags;
		uint8	mSectorCount[2];
		uint8	mFirstSector[2];
		uint8	mName[11];
	};

	struct XDirBaseEnt {
		VDStringW mPath;
		uint32	mSize;
		uint32	mSectorCount;
		uint32	mLockedSector;

		XDirBaseEnt() : mSize(0), mSectorCount(0), mLockedSector(0) {}
	};

	struct XDirEnt : public XDirBaseEnt {
		VDFile	mFile;
		bool	mbValid;

		XDirEnt() : mbValid(false) {}
	};

	struct SectorEnt {
		bool	mbLinked;
		sint8	mFileIndex;
		uint16	mSectorIndex;
		uint16	mLRUPrev;
		uint16	mLRUNext;
	};

	void PromoteDataSector(uint32 sector);
	uint32 FindDataSector(sint8 fileIndex, uint16 sectorIndex) const;
	void UnlinkDataSector(uint32 sector);
	void LinkDataSector(uint32 sector);

	VDStringW mPath;
	uint32	mSectorCount;
	bool mbBootFilePresent;
	VDDate mBootFileLastDate;
	int mDosEntry;

	VDLazyTimer mCloseTimer;
	VDFileWatcher mFileWatcher;

	DirEnt	mDirEnt[64];
	XDirEnt	mXDirEnt[64];

	SectorEnt mSectorMap[720];

	uint8 mBootSectors[384];
};

ATDiskImageVirtualFolder::ATDiskImageVirtualFolder()
	: mSectorCount(720)
{
}

void ATDiskImageVirtualFolder::Init(const wchar_t *path) {
	mPath = path;
	mbBootFilePresent = false;
	mBootFileLastDate.mTicks = 0;
	mDosEntry = -1;
	memset(mDirEnt, 0, sizeof mDirEnt);

	UpdateDirectory(false);

	// Mark all sectors as in use.
	for(uint32 i=0; i<720; ++i) {
		mSectorMap[i].mbLinked = false;
		mSectorMap[i].mFileIndex = -1;
		mSectorMap[i].mLRUNext = i;
		mSectorMap[i].mLRUPrev = i;
		mSectorMap[i].mSectorIndex = 0;
	}

	// Sectors 3-66 are permanently dedicated to the first sector of each file.
	for(uint32 i=3; i<=66; ++i) {
		mSectorMap[i].mFileIndex = i-3;
		mSectorMap[i].mSectorIndex = 0;
	}

	// Free up sectors 67-358 and 368-718 for rotating data sector use.
	for(uint32 i=67; i<=358; ++i)
		LinkDataSector(i);

	for(uint32 i=368; i<=718; ++i)
		LinkDataSector(i);

	try {
		mFileWatcher.InitDir(path, false, NULL);
	} catch(const MyError&) {
	}
}

void ATDiskImageVirtualFolder::SetPath(const wchar_t *path) {
}

void ATDiskImageVirtualFolder::Save(const wchar_t *path, ATDiskImageFormat format) {
}

ATDiskGeometryInfo ATDiskImageVirtualFolder::GetGeometry() const {
	ATDiskGeometryInfo info;
	info.mSectorSize = 128;
	info.mBootSectorCount = 3;
	info.mTotalSectorCount = 720;
	info.mTrackCount = 40;
	info.mSectorsPerTrack = 18;
	info.mSideCount = 1;
	info.mbMFM = false;
	return info;
}

uint32 ATDiskImageVirtualFolder::GetSectorSize() const {
	return 128;
}

uint32 ATDiskImageVirtualFolder::GetSectorSize(uint32 virtIndex) const {
	return 128;
}

uint32 ATDiskImageVirtualFolder::GetBootSectorCount() const {
	return 3;
}

uint32 ATDiskImageVirtualFolder::GetPhysicalSectorCount() const {
	return 720;
}

void ATDiskImageVirtualFolder::GetPhysicalSectorInfo(uint32 index, ATDiskPhysicalSectorInfo& info) const {
	info.mOffset = 0;
	info.mSize = 128;
	info.mbDirty = false;
	info.mRotPos = (float)kTrackInterleave18[index % 18] / 18.0f;
	info.mFDCStatus = 0xFF;
	info.mWeakDataOffset = -1;
}

void ATDiskImageVirtualFolder::ReadPhysicalSector(uint32 index, void *data, uint32 len) {
	memset(data, 0, len);

	if (len != 128 || index >= 720)
		return;

	// check for updates
	if (mFileWatcher.Wait(0))
		UpdateDirectory(true);

	// check for boot sector
	if (index < 3) {
		if (mbBootFilePresent) {
			memcpy(data, mBootSectors + 128*index, 128);

			// If this is the first sector, we need to patch it to include
			// info about where DOS.SYS is.
			if (!index) {
				uint8 *dst = (uint8 *)data;
				if (mDosEntry >= 0) {
					dst[0x0E] = 1;
					dst[0x0F] = mDirEnt[mDosEntry].mFirstSector[0];
					dst[0x10] = mDirEnt[mDosEntry].mFirstSector[1];
				} else {
					dst[0x0E] = 0;
				}
			}
		} else {
			unsigned offset = index * 128;

			if (offset < g_ATResDOSBootSectorLen)
				memcpy(data, g_ATResDOSBootSector + offset, std::min<size_t>(g_ATResDOSBootSectorLen - offset, 128));
		}
		return;
	}

	// check for VTOC
	if (index == 359) {
		static const uint8 kVTOCSector[]={
			0x02, 0xC3, 0x02
		};

		memcpy(data, kVTOCSector, sizeof kVTOCSector);
		return;
	}

	// check for directory
	if (index >= 360 && index < 368) {
		memcpy(data, &mDirEnt[(index - 360) << 3], 128);
		return;
	}

	// Must be data sector.
	//
	// We have a total of 707 data sectors to play with, 3-358 and 368-718 (we are zero-based
	// here, and DOS does not allocate the last sector). Sectors 3-66 are reserved as the first
	// sectors; we use the remaining sectors for rotating data sector use.

	// First, check if the sector is assigned to a file. If not, bail.
	SectorEnt& se = mSectorMap[index];
	if (se.mFileIndex < 0)
		return;

	// Promote this sector to LRU head.
	PromoteDataSector(index);

	// Retrieve the data corresponding to that file.
	XDirEnt& xd = mXDirEnt[se.mFileIndex];
	
	g_ATLCVDisk("Reading sector %u (sector %u of file %d / %ls)\n", index+1, se.mSectorIndex, se.mFileIndex, VDFileSplitPath(xd.mPath.c_str()));

	uint8 validLen = 0;
	uint32 link = (uint32)0 - 1;

	// Check if we are beyond the end of the file -- this can happen with an update. If
	// this happens, we report an empty terminator sector.
	if (se.mSectorIndex < xd.mSectorCount) {
		if (!xd.mFile.isOpen()) {
			g_ATLCVDisk("Opening file: %ls\n", xd.mPath.c_str());
			xd.mFile.open(xd.mPath.c_str());
		}

		const uint32 offset = (uint32)se.mSectorIndex * 125;
		const uint32 remainder = xd.mSize - offset;

		validLen = (uint8)std::min<uint32>(125, remainder);

		// Read in the file data.
		if (validLen) {
			xd.mFile.seek(offset);
			xd.mFile.read(data, validLen);

			mCloseTimer.SetOneShot(this, 3000);
		}

		// Check if there will be another sector. If so, we need to determine the sector link and
		// pre-allocate that sector if needed.
		if (remainder > 125) {
			link = xd.mLockedSector;

			if (!link || mSectorMap[link].mSectorIndex != se.mSectorIndex + 1) {
				// Release the locked sector back into the LRU list.
				if (link)
					LinkDataSector(link);

				// Check if the sector we need is still present.
				link = FindDataSector(se.mFileIndex, se.mSectorIndex + 1);

				if (link) {
					g_ATLCVDisk("Resurrecting cached link at sector %u\n", link);
				} else {
					// No -- allocate a fresh sector from the LRU tail.
					link = mSectorMap[0].mLRUPrev;
					VDASSERT(link);

					g_ATLCVDisk("Reassigning cached link at sector %u\n", link);

					// Reassign the sector.
					SectorEnt& le = mSectorMap[link];
					VDASSERT(le.mbLinked);

					le.mFileIndex = se.mFileIndex;
					le.mSectorIndex = se.mSectorIndex + 1;
				}

				UnlinkDataSector(link);
				xd.mLockedSector = link;
			} else {
				g_ATLCVDisk("Using cached link at sector %u\n", link);
			}
		}
	}

	// Byte 125 contains the file ID in bits 2-7 and the high two bits of the link in bits 0-1.
	// Byte 126 contains the low 8 bits of the link (note that we must make this one-based).
	// Byte 127 contains the number of valid data bytes.
	uint8 *dst = (uint8 *)data;

	dst[125] = (uint8)(((int)se.mFileIndex << 2) + ((link + 1) >> 8));
	dst[126] = (uint8)(link + 1);
	dst[127] = validLen;
}

void ATDiskImageVirtualFolder::WritePhysicalSector(uint32 index, const void *data, uint32 len) {
	throw MyError("Writes are not supported to a virtual disk.");
}

uint32 ATDiskImageVirtualFolder::GetVirtualSectorCount() const {
	return 720;
}

void ATDiskImageVirtualFolder::GetVirtualSectorInfo(uint32 index, ATDiskVirtualSectorInfo& info) const {
	info.mStartPhysSector = index;
	info.mNumPhysSectors = 1;
}

uint32 ATDiskImageVirtualFolder::ReadVirtualSector(uint32 index, void *data, uint32 len) {
	if (len < 128)
		return 0;

	ReadPhysicalSector(index, data, len > 128 ? 128 : len);
	return 128;
}

bool ATDiskImageVirtualFolder::WriteVirtualSector(uint32 index, const void *data, uint32 len) {
	return false;
}

void ATDiskImageVirtualFolder::Resize(uint32 sectors) {
	throw MyError("A virtual disk cannot be resized.");
}

void ATDiskImageVirtualFolder::TimerCallback() {
	for(size_t i=0; i<vdcountof(mXDirEnt); ++i) {
		XDirEnt& xd = mXDirEnt[i];

		if (xd.mFile.isOpen()) {
			g_ATLCVDisk("Closing file: %ls\n", xd.mPath.c_str());
			xd.mFile.closeNT();
		}
	}
}

void ATDiskImageVirtualFolder::PromoteDataSector(uint32 sector) {
	if (!mSectorMap[sector].mbLinked)
		return;

	UnlinkDataSector(sector);
	LinkDataSector(sector);
}

uint32 ATDiskImageVirtualFolder::FindDataSector(sint8 fileIndex, uint16 sectorIndex) const {
	for(uint32 i=1; i<720; ++i) {
		if (mSectorMap[i].mFileIndex == fileIndex && mSectorMap[i].mSectorIndex == sectorIndex)
			return i;
	}

	return 0;
}

void ATDiskImageVirtualFolder::UnlinkDataSector(uint32 sector) {
	VDASSERT(sector != 0);

	// unlink sector
	SectorEnt& se = mSectorMap[sector];
	SectorEnt& pe = mSectorMap[se.mLRUPrev];
	SectorEnt& ne = mSectorMap[se.mLRUNext];

	ne.mLRUPrev = se.mLRUPrev;
	pe.mLRUNext = se.mLRUNext;
	se.mLRUPrev = sector;
	se.mLRUNext = sector;
	se.mbLinked = false;
}

void ATDiskImageVirtualFolder::LinkDataSector(uint32 sector) {
	// relink sector at head
	SectorEnt& se = mSectorMap[sector];
	SectorEnt& re = mSectorMap[0];
	SectorEnt& he = mSectorMap[re.mLRUNext];
	se.mLRUPrev = 0;
	se.mLRUNext = re.mLRUNext;

	he.mLRUPrev = sector;
	re.mLRUNext = sector;

	se.mbLinked = true;
}

void ATDiskImageVirtualFolder::UpdateDirectory(bool reportNewFiles) {
	// Build a hash table of the existing entries.
	typedef vdhashmap<VDStringW, int> ExistingLookup;
	ExistingLookup existingLookup;

	for(size_t i=0; i<vdcountof(mXDirEnt); ++i) {
		if (mXDirEnt[i].mbValid)
			existingLookup[mXDirEnt[i].mPath] = (int)i;
	}

	// Clear valid flags for all entries.
	for(size_t i=0; i<vdcountof(mXDirEnt); ++i)
		mXDirEnt[i].mbValid = false;

	// Iterate over the directory. Collect them separately so we don't disturb existing entries.
	XDirBaseEnt newList[vdcountof(mXDirEnt)];

	uint32 totalEntries = 0;
	uint32 newEntries = 0;

	bool bootPresent = false;

	for(VDDirectoryIterator it(VDMakePath(mPath.c_str(), L"*.*").c_str()); it.Next();) {
		if (it.IsDirectory())
			continue;

		// Skip hidden files.
		if (it.GetAttributes() & kVDFileAttr_Hidden)
			continue;

		// Check for the magic boot sector.
		if (!vdwcsicmp(it.GetName(), L"$dosboot.bin")) {
			if (it.GetSize() == 384) {
				bootPresent = true;

				VDDate lastWriteDate = it.GetLastWriteDate();
				if (mBootFileLastDate != lastWriteDate) {
					mBootFileLastDate = lastWriteDate;

					// Try to read in the boot sector.
					try {
						VDFile f(it.GetFullPath().c_str());

						f.read(mBootSectors, sizeof mBootSectors);
					} catch(const MyError& e) {
						g_ATLCVDisk("Unable to read boot file: %s\n", e.gets());
						bootPresent = false;
					}
				}
			}

			continue;
		}

		// If we still have room in the emulated directory, add the file. Note that we
		// must continue to scan the host directory in case the boot sector is present.
		if (totalEntries < vdcountof(mDirEnt)) {
			++totalEntries;

			// Check if we had this entry previously; if so we want to preserve it.
			XDirBaseEnt *xdst;

			const VDStringW& filePath = it.GetFullPath();
			ExistingLookup::const_iterator it2(existingLookup.find(filePath));
			if (it2 == existingLookup.end()) {
				xdst = &newList[newEntries++];
				xdst->mPath = filePath;

				if (reportNewFiles)
					g_ATLCVDisk("Adding new file: %ls\n", it.GetName());
			} else {
				mXDirEnt[it2->second].mbValid = true;
				xdst = &mXDirEnt[it2->second];
			}

			uint32 size = VDClampToUint32(it.GetSize());
			uint32 sectors = VDClampToUint16((size + 124) / 125);
			xdst->mSize = size;
			xdst->mSectorCount = sectors;
		}
	}

	mbBootFilePresent = bootPresent;

	// Delete all directory entries that are no longer valid.
	for(size_t i=0; i<vdcountof(mDirEnt); ++i) {
		DirEnt& de = mDirEnt[i];
		XDirEnt& xde = mXDirEnt[i];

		if (!xde.mbValid) {
			memset(&de, 0, sizeof de);
			xde.mFile.closeNT();

			if (xde.mLockedSector) {
				LinkDataSector(xde.mLockedSector);
				xde.mLockedSector = 0;
			}

			xde.mSectorCount = 0;
			xde.mSize = 0;
		}
	}

	// Assign new slots for all new entries.
	uint32 freeNext = 0;
	for(uint32 i=0; i<newEntries; ++i) {
		// Copy base info into new entry.
		while(mXDirEnt[freeNext].mbValid)
			++freeNext;

		XDirEnt& xe = mXDirEnt[freeNext];
		DirEnt& de = mDirEnt[freeNext];

		static_cast<XDirBaseEnt&>(xe) = newList[i];
		xe.mbValid = true;

		// Assign a non-conflicting name.
		const VDStringA& name = VDTextWToA(VDFileSplitPath(xe.mPath.c_str()));
		size_t len1 = std::min<size_t>(name.size(), name.find('.'));
		const char *s = name.c_str();

		memset(de.mName, 0x20, sizeof de.mName);

		for(size_t j=0, k=0; k<8 && j<len1; ++j) {
			unsigned char c = toupper(s[j]);

			if ((c>='A' && c<='Z') || (c>='0' && c<='9'))
				de.mName[k++] = c;
		}

		if (len1 < name.size()) {
			size_t j = len1 + 1;
			size_t k = 8;
			
			while(k < 11) {
				unsigned char c = toupper((unsigned char)s[j++]);

				if (!c)
					break;

				if ((c>='A' && c<='Z') || (c>='0' && c<='9'))
					de.mName[k++] = c;
			}
		}

		// check for conflicts (a bit slow)
		for(;;) {
			bool clear = true;

			for(size_t j=0; j<vdcountof(mDirEnt); ++j) {
				if (freeNext != j && !memcmp(mDirEnt[j].mName, de.mName, 11)) {
					clear = false;
					break;
				}
			}

			if (clear)
				break;

			// increment the name
			for(int i=7; i>=0; --i) {
				if ((unsigned)(de.mName[i]-'0') >= 10) {
					de.mName[i] = '1';
					break;
				} else if (de.mName[i] == '9') {
					de.mName[i] = '0';
				} else {
					++de.mName[i];
					break;
				}
			}
		}
	}

	// Fill in all holes in the directory with the deleted flag.
	bool foundValidEntry = false;
	for(size_t i=vdcountof(mDirEnt); i; --i) {
		if (mXDirEnt[i-1].mbValid)
			foundValidEntry = true;
		else if (foundValidEntry)
			mDirEnt[i-1].mFlags = DirEnt::kFlagDeleted;
	}

	// Recompute directory entry metadata.
	mDosEntry = -1;

	for(size_t i=0; i<vdcountof(mDirEnt); ++i) {
		DirEnt& de = mDirEnt[i];
		const XDirEnt& xde = mXDirEnt[i];

		if (xde.mbValid) {
			// Check if this is DOS.SYS so we can update the boot sector.
			if (!memcmp(de.mName, "DOS     SYS", 11))
				mDosEntry = (int)i;

			// There is an anomaly we have to deal with here regarding zero byte files.
			// Specifically, there is no provision in the disk format for a file with
			// no data sectors attached to it, so in the case of an zero byte file we
			// must point to a valid but empty data sector and put a sector count of 1
			// in the directory entry. DOS 2.0S and MyDOS 4.5D both do this. SpartaDOS X
			// also creates the empty data sector but puts a sector count of zero into
			// the directory entry, which makes VTOCFIX complain.

			VDWriteUnalignedLEU16(de.mSectorCount, std::max<uint16>(1, (uint16)std::min<uint32>(999, xde.mSectorCount)));
			de.mFirstSector[0] = (uint8)(4 + i);
			de.mFirstSector[1] = 0;
			de.mFlags = DirEnt::kFlagDOS2 | DirEnt::kFlagInUse;
		}
	}
}

///////////////////////////////////////////////////////////////////////////

IATDiskImage *ATMountDiskImageVirtualFolder(const wchar_t *path, uint32 sectorCount) {
	vdautoptr<ATDiskImageVirtualFolder> p(new ATDiskImageVirtualFolder);
	
	p->Init(path);
	return p.release();
}
