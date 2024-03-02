//	Altirra - Atari 800/800XL/5200 emulator
//	I/O library - disk image definitions
//	Copyright (C) 2008-2015 Avery Lee
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

#ifndef f_AT_ATIO_DISKIMAGE_H
#define f_AT_ATIO_DISKIMAGE_H

class IVDRandomAccessStream;

enum ATDiskTimingMode {
	kATDiskTimingMode_Any,
	kATDiskTimingMode_UsePrecise,
	kATDiskTimingMode_UseOrdered
};

struct ATDiskVirtualSectorInfo {
	uint32	mStartPhysSector;
	uint32	mNumPhysSectors;
};

struct ATDiskPhysicalSectorInfo {
	uint32	mOffset;			// offset within memory image
	sint32	mDiskOffset;		// offset within disk image (for rewriting)
	uint16	mSize;
	bool	mbDirty;
	float	mRotPos;
	uint8	mFDCStatus;
	sint16	mWeakDataOffset;
};

struct ATDiskGeometryInfo {
	uint16	mSectorSize;
	uint8	mBootSectorCount;
	uint32	mTotalSectorCount;
	uint8	mTrackCount;
	uint32	mSectorsPerTrack;
	uint8	mSideCount;
	bool	mbMFM;
};

enum ATDiskImageFormat {
	kATDiskImageFormat_None,
	kATDiskImageFormat_ATR,
	kATDiskImageFormat_XFD,
	kATDiskImageFormat_P2,
	kATDiskImageFormat_P3,
	kATDiskImageFormat_ATX,
	kATDiskImageFormat_DCM
};

class VDINTERFACE IATDiskImage {
public:
	virtual ~IATDiskImage() = default;

	virtual ATDiskTimingMode GetTimingMode() const = 0;

	// Returns true if the disk image has been written to since the image was created
	// or loaded. It is unspecified whether writing a sector with contents identical to
	// the existing sector sets the dirty flag (other factors like copy-on-write, timestamp,
	// and sector error flags may require dirty to be set). Flush() is a no-op if the
	// image is not dirty.
	virtual bool IsDirty() const = 0;

	// Returns true if the disk image has a persistent backing store that can
	// be updated from the active image.
	virtual bool IsUpdatable() const = 0;

	// Returns true if the disk image has dynamic contents based on access pattern
	// or external updates, and false if the image is statically established on
	// load.
	virtual bool IsDynamic() const = 0;

	virtual ATDiskImageFormat GetImageFormat() const = 0;

	// Flush any changes back to the persistent store. Returns true on success or no-op;
	// false if the image is not updatable. I/O exceptions may be thrown. A dirty image
	// becomes clean after a successful flush.
	virtual bool Flush() = 0;

	virtual void SetPath(const wchar_t *path) = 0;
	virtual void Save(const wchar_t *path, ATDiskImageFormat format) = 0;

	virtual ATDiskGeometryInfo GetGeometry() const = 0;
	virtual uint32 GetSectorSize() const = 0;
	virtual uint32 GetSectorSize(uint32 virtIndex) const = 0;
	virtual uint32 GetBootSectorCount() const = 0;

	virtual uint32 GetPhysicalSectorCount() const = 0;
	virtual void GetPhysicalSectorInfo(uint32 index, ATDiskPhysicalSectorInfo& info) const = 0;

	virtual void ReadPhysicalSector(uint32 index, void *data, uint32 len) = 0;
	virtual void WritePhysicalSector(uint32 index, const void *data, uint32 len) = 0;

	virtual uint32 GetVirtualSectorCount() const = 0;
	virtual void GetVirtualSectorInfo(uint32 index, ATDiskVirtualSectorInfo& info) const = 0;

	virtual uint32 ReadVirtualSector(uint32 index, void *data, uint32 len) = 0;
	virtual bool WriteVirtualSector(uint32 index, const void *data, uint32 len) = 0;

	// Attempt to resize the disk image to the given number of virtual sectors.
	// In general, this should be <65536 sectors since that is the limit of the
	// standard SIO disk protocol, but this is not enforced. New sectors are
	// filled with $00 and the disk is marked as dirty. The disk geometry is
	// auto-computed according to inference rules. An exception is thrown if
	// the disk is dynamic.
	virtual void Resize(uint32 sectors) = 0;
};

IATDiskImage *ATLoadDiskImage(const wchar_t *path);
IATDiskImage *ATLoadDiskImage(const wchar_t *origPath, const wchar_t *imagePath, IVDRandomAccessStream& stream);
IATDiskImage *ATMountDiskImageVirtualFolder(const wchar_t *path, uint32 sectorCount);
IATDiskImage *ATMountDiskImageVirtualFolderSDFS(const wchar_t *path, uint32 sectorCount, uint64 uniquenessValue);
IATDiskImage *ATCreateDiskImage(uint32 sectorCount, uint32 bootSectorCount, uint32 sectorSize);

void ATDiskConvertGeometryToPERCOM(uint8 percom[12], const ATDiskGeometryInfo& geom);
void ATDiskConvertPERCOMToGeometry(ATDiskGeometryInfo& geom, const uint8 percom[12]);

#endif	// f_AT_ATIO_DISKIMAGE_H
