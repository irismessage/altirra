//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2012 Avery Lee
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
#include <at/atcore/propertyset.h>
#include <vd2/system/error.h>
#include <vd2/system/filesys.h>
#include "iderawimage.h"

void ATCreateDeviceHardDiskRawImage(const ATPropertySet& pset, IATDevice **dev);

extern const ATDeviceDefinition g_ATDeviceDefIDERawImage = { "hdrawimage", "harddisk", L"Hard disk image (raw file)", ATCreateDeviceHardDiskRawImage };

ATIDERawImage::ATIDERawImage()
	: mSectorCount(0)
	, mSectorCountLimit(0)
	, mbReadOnly(false)
{
}

ATIDERawImage::~ATIDERawImage() {
	Shutdown();
}

int ATIDERawImage::AddRef() {
	return ATDevice::AddRef();
}

int ATIDERawImage::Release() {
	return ATDevice::Release();
}

void *ATIDERawImage::AsInterface(uint32 iid) {
	switch(iid) {
		case IATIDEDisk::kTypeID: return static_cast<IATIDEDisk *>(this);
		default:
			return ATDevice::AsInterface(iid);
	}
}

void ATIDERawImage::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefIDERawImage;
}

void ATIDERawImage::GetSettings(ATPropertySet& settings) {
	settings.SetString("path", mPath.c_str());
	settings.SetUint32("sectors", mSectorCountLimit);
	settings.SetBool("write_enabled", !mbReadOnly);
}

bool ATIDERawImage::SetSettings(const ATPropertySet& settings) {
	const wchar_t *path = settings.GetString("path");
	if (path && !VDFileIsPathEqual(path, mPath.c_str()))
		return false;

	bool we;
	if (settings.TryGetBool("write_enabled", we)) {
		if (mbReadOnly != !we)
			return false;
	}

	settings.TryGetUint32("sectors", mSectorCountLimit);


	return true;
}

bool ATIDERawImage::IsReadOnly() const {
	return mbReadOnly;
}

uint32 ATIDERawImage::GetSectorCount() const {
	return std::max(mSectorCount, mSectorCountLimit);
}

void ATIDERawImage::Init(const wchar_t *path, bool write) {
	Shutdown();

	mPath = path;
	mFile.open(path, write ? nsVDFile::kReadWrite | nsVDFile::kDenyAll | nsVDFile::kOpenAlways : nsVDFile::kRead | nsVDFile::kDenyWrite | nsVDFile::kOpenExisting);
	mbReadOnly = !write;

	uint64 sectors = (uint64)mFile.size() >> 9;
	mSectorCount = sectors > 0xFFFFFFFFU ? 0xFFFFFFFFU : (uint32)sectors;
}

void ATIDERawImage::Shutdown() {
	mFile.closeNT();
}

void ATIDERawImage::Flush() {
}

void ATIDERawImage::RequestUpdate() {
}

void ATIDERawImage::ReadSectors(void *data, uint32 lba, uint32 n) {
	mFile.seek((sint64)lba << 9);

	uint32 requested = n << 9;
	uint32 actual = mFile.readData(data, requested);

	if (requested < actual)
		memset((char *)data + actual, 0, requested - actual);
}

void ATIDERawImage::WriteSectors(const void *data, uint32 lba, uint32 n) {
	mFile.seek((sint64)lba << 9);
	mFile.write(data, 512 * n);

	if (lba + n > mSectorCount)
		mSectorCount = lba + n;
}
