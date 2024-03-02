//	Altirra - Atari 800/800XL/5200 emulator
//	Core library -- block device interface
//	Copyright (C) 2009-2016 Avery Lee
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
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#ifndef f_AT_ATCORE_BLOCKDEVICE_H
#define f_AT_ATCORE_BLOCKDEVICE_H

#include <vd2/system/unknown.h>

struct ATBlockDeviceGeometry {
	uint32 mSectorsPerTrack;
	uint32 mHeads;
	uint32 mCylinders;
	bool mbSolidState;
};

class IATBlockDevice : public IVDRefUnknown {
public:
	static constexpr uint32 kTypeID = "IATBlockDevice"_vdtypeid;

	virtual bool IsReadOnly() const = 0;
	virtual uint32 GetSectorCount() const = 0;
	virtual ATBlockDeviceGeometry GetGeometry() const = 0;
	virtual uint32 GetSerialNumber() const = 0;

	virtual void Flush() = 0;

	virtual void ReadSectors(void *data, uint32 lba, uint32 n) = 0;
	virtual void WriteSectors(const void *data, uint32 lba, uint32 n) = 0;
};

class IATBlockDeviceDirectAccess : public IVDRefUnknown {
public:
	static constexpr uint32 kTypeID = "IATBlockDeviceDirectAccess"_vdtypeid;

	virtual VDStringW GetVHDDirectAccessPath() const = 0;
};

class IATBlockDeviceDynamic : public IVDRefUnknown {
public:
	static constexpr uint32 kTypeID = "IATBlockDeviceDynamic"_vdtypeid;

	virtual void RescanDynamicDisk() = 0;
};

#endif
