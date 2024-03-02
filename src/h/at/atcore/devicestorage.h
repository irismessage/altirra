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
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#ifndef f_AT_ATCORE_DEVICESTORAGE_H
#define f_AT_ATCORE_DEVICESTORAGE_H

#include <vd2/system/unknown.h>

class IATDeviceStorage;

class IATDeviceStorageManager {
public:
	static constexpr uint32 kTypeID = "IATDeviceSettingsManager"_vdtypeid;

	// Load NVRAM as a blob or integer; returns true if successful. Blobs should
	// be platform independent. If the length doesn't match, the common subset
	// of the persisted data and provided buffer is returned.
	virtual bool LoadNVRAM(const char *name, void *buf, size_t len) = 0;
	virtual bool LoadNVRAMInt(const char *name, sint32& val) = 0;

	// Save NVRAM from a blob or integer. Existing NVRAM of that name is replaced.
	virtual void SaveNVRAM(const char *name, const void *buf, size_t len) = 0;
	virtual void SaveNVRAMInt(const char *name, sint32 val) = 0;

	// Register device storage for visibility and load/save events. Typically
	// these happen at profile load/unload. Device storage is not automatically
	// loaded on register and must be manually triggered if desired.
	virtual void RegisterDeviceStorage(IATDeviceStorage& storage) = 0;
	virtual void UnregisterDeviceStorage(IATDeviceStorage& storage) = 0;
};

class IATDeviceStorage {
public:
	// Load storage from persistence.
	virtual void LoadStorage(IATDeviceStorageManager& mgr) = 0;

	// Save storage to persistence.
	virtual void SaveStorage(IATDeviceStorageManager& mgr) = 0;
};

#endif
