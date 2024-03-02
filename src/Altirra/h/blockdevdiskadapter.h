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

#ifndef f_AT_BLOCKDEVDISKADAPTER_H
#define f_AT_BLOCKDEVDISKADAPTER_H

#include <vd2/system/vdtypes.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/blockdevice.h>
#include <at/atcore/deviceparentimpl.h>
#include <at/atio/diskimage.h>

class ATBlockDeviceDiskAdapter : public ATDevice, public IATBlockDevice {
	ATBlockDeviceDiskAdapter(const ATBlockDeviceDiskAdapter&) = delete;
	ATBlockDeviceDiskAdapter& operator=(const ATBlockDeviceDiskAdapter&) = delete;

public:
	ATBlockDeviceDiskAdapter();
	~ATBlockDeviceDiskAdapter();

public:
	int AddRef() override;
	int Release() override;
	void *AsInterface(uint32 iid) override;

public:
	bool IsReadOnly() const override;
	uint32 GetSectorCount() const override;
	ATBlockDeviceGeometry GetGeometry() const override;
	uint32 GetSerialNumber() const override;

	void Init() override;
	void Shutdown() override;

	void Flush() override;

	void ReadSectors(void *data, uint32 lba, uint32 n) override;
	void WriteSectors(const void *data, uint32 lba, uint32 n) override;

protected:
	void ReadHeaderSector(void *data, uint32 lba);

	vdrefptr<IATDiskImage> mpDiskImage;
	uint32 mDefaultSectorCount {};

	static constexpr uint32 kHeaderSectorCount = 8;
};

class ATBlockDeviceVirtSDFS final : public ATBlockDeviceDiskAdapter {
public:
	ATBlockDeviceVirtSDFS();

	void GetDeviceInfo(ATDeviceInfo& info) override;
	void GetSettings(ATPropertySet& settings) override;
	bool SetSettings(const ATPropertySet& settings) override;
	void Init() override;

public:
	VDStringW mPath;
};

#endif
