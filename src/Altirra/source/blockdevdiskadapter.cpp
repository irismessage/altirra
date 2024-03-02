//	Altirra - Atari 800/800XL/5200 emulator
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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#include <stdafx.h>
#include <vd2/system/binary.h>
#include <vd2/system/vdalloc.h>
#include <at/atcore/propertyset.h>
#include "blockdevdiskadapter.h"

ATBlockDeviceDiskAdapter::ATBlockDeviceDiskAdapter() {
}

ATBlockDeviceDiskAdapter::~ATBlockDeviceDiskAdapter() {
	Shutdown();
}

int ATBlockDeviceDiskAdapter::AddRef() {
	return ATDevice::AddRef();
}

int ATBlockDeviceDiskAdapter::Release() {
	return ATDevice::Release();
}

void *ATBlockDeviceDiskAdapter::AsInterface(uint32 iid) {
	switch(iid) {
		case IATBlockDevice::kTypeID: return static_cast<IATBlockDevice *>(this);
		default:
			return ATDevice::AsInterface(iid);
	}
}

bool ATBlockDeviceDiskAdapter::IsReadOnly() const {
	return true;
}

uint32 ATBlockDeviceDiskAdapter::GetSectorCount() const {
	return (mpDiskImage ? mpDiskImage->GetVirtualSectorCount() : mDefaultSectorCount) + kHeaderSectorCount;
}

ATBlockDeviceGeometry ATBlockDeviceDiskAdapter::GetGeometry() const {
	// it's not useful to reflect floppy disk geometry to a hard disk, so we just
	// return default (unspecified).
	return ATBlockDeviceGeometry{};
}

uint32 ATBlockDeviceDiskAdapter::GetSerialNumber() const {
	return 0;
}

void ATBlockDeviceDiskAdapter::Init() {
}

void ATBlockDeviceDiskAdapter::Shutdown() {
	mpDiskImage = nullptr;
}

void ATBlockDeviceDiskAdapter::Flush() {
}

void ATBlockDeviceDiskAdapter::ReadSectors(void *data, uint32 lba, uint32 n) {
	memset(data, 0, 512*n);

	if (!mpDiskImage)
		return;

	const uint32 imageSectorSize = mpDiskImage->GetSectorSize();

	uint32 numSecs = mpDiskImage->GetVirtualSectorCount();

	while(n--) {
		if (lba < kHeaderSectorCount) {
			ReadHeaderSector(data, lba);
		} else if (lba < numSecs + kHeaderSectorCount) {
			mpDiskImage->ReadVirtualSector(lba - kHeaderSectorCount, data, 512);

			if (imageSectorSize < 256) {
				uint8 *data8 = (uint8 *)data;

				for(int i = 255; i > 0; --i) {
					data8[i*2] = data8[i];
					data8[i*2-1] = 0;
				}
			}
		}

		data = (char *)data + 512;
		++lba;
	}
}

void ATBlockDeviceDiskAdapter::WriteSectors(const void *data, uint32 lba, uint32 n) {
	throw MyError("Image is read-only.");
}

void ATBlockDeviceDiskAdapter::ReadHeaderSector(void *data, uint32 lba) {
	uint8 *dst = (uint8 *)data;

	if (lba == 0) {
		// protective MBR APT partition
		dst[0x1BE] = 0x80;
		dst[0x1C2] = 0x7F;		// partition type (unknown / foreign)
		VDWriteUnalignedLEU32(&dst[0x1C6], 1);
		VDWriteUnalignedLEU32(&dst[0x1CA], GetSectorCount() - 1);

		// MBR signature
		dst[0x1FE] = 0x55;
		dst[0x1FF] = 0xAA;
	} else if (lba == 1) {
		// APT partition table header
		dst[0x000] = 0x00;		// partition table uses full words, rev 0, no global metadata, no extra mapping slots
		dst[0x001] = 0x41;		// 'A'
		dst[0x002] = 0x50;		// 'P'
		dst[0x003] = 0x54;		// 'T'
		dst[0x004] = 0;			// boot drive unspecified
		dst[0x005] = 2;			// two entries (header + mapping slot)
		dst[0x006] = 0;			// next sector entry offset (N/A)
		dst[0x007] = 0;			// prev sector entry offset (N/A)
		VDWriteUnalignedLEU32(&dst[0x008], 0);		// next sector (0)
		VDWriteUnalignedLEU32(&dst[0x00C], 0);		// prev sector (0)

		// APT mapping slot entry
		dst[0x010] = 0x03;		// 512 byte/sector
		dst[0x011] = 0x00;		// DOS partition
		VDWriteUnalignedLEU32(&dst[0x012], kHeaderSectorCount);							// absolute starting LBA
		VDWriteUnalignedLEU32(&dst[0x016], GetSectorCount() - kHeaderSectorCount);		// sector count
		VDWriteUnalignedLEU16(&dst[0x01A], 1);		// partition ID
		dst[0x01C] = 0xC0;		// write protected, automount
		dst[0x01E] = 0x00;		// no reserved area
	}
}

///////////////////////////////////////////////////////////////////////////

void ATCreateDeviceBlockDevVirtSDFS(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATBlockDeviceVirtSDFS> vdev(new ATBlockDeviceVirtSDFS);

	vdev->SetSettings(pset);

	*dev = vdev.release();
}

extern constexpr ATDeviceDefinition g_ATDeviceDefBlockDevVSDFS = {
	"hdvirtsdfs",
	"hdvirtsdfs",
	L"Virtual SDFS hard disk",
	ATCreateDeviceBlockDevVirtSDFS
};

ATBlockDeviceVirtSDFS::ATBlockDeviceVirtSDFS() {
	// Because of the way that device settings work, we have to support temporarily
	// initializing without a disk image. Set the default sector count so the HD
	// size is stable until the disk image is inited and swapped in.
	mDefaultSectorCount = 65535;

	SetSaveStateAgnostic();
}

void ATBlockDeviceVirtSDFS::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefBlockDevVSDFS;
}

void ATBlockDeviceVirtSDFS::GetSettings(ATPropertySet& settings) {
	settings.SetString("path", mPath.c_str());
}

bool ATBlockDeviceVirtSDFS::SetSettings(const ATPropertySet& settings) {
	const wchar_t *newPath = settings.GetString("path");

	if (!newPath)
		newPath = L"";

	if (mPath != newPath) {
		mPath = newPath;
		return false;
	}

	return true;
}

void ATBlockDeviceVirtSDFS::Init() {
	if (!mPath.empty())
		ATMountDiskImageVirtualFolderSDFS(mPath.c_str(), 512, 0, ~mpDiskImage);
}
