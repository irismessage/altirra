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

#ifndef f_AT_ATCORE_DEVICESTORAGEIMPL_H
#define f_AT_ATCORE_DEVICESTORAGEIMPL_H

#include <vd2/system/function.h>
#include <at/atcore/devicestorage.h>

class ATDeviceVirtualStorage : public IATDeviceStorage {
	ATDeviceVirtualStorage(const ATDeviceVirtualStorage&) = delete;
	ATDeviceVirtualStorage& operator=(const ATDeviceVirtualStorage&) = delete;
public:
	ATDeviceVirtualStorage() = default;
	virtual ~ATDeviceVirtualStorage();

	void Init(IATDeviceStorageManager& mgr, vdfunction<void(IATDeviceStorageManager&)> loadfn, vdfunction<void(IATDeviceStorageManager&)> savefn);
	void Shutdown();

	void LoadStorage(IATDeviceStorageManager& mgr);
	void SaveStorage(IATDeviceStorageManager& mgr);

private:
	vdfunction<void(IATDeviceStorageManager&)> mpOnLoadFn;
	vdfunction<void(IATDeviceStorageManager&)> mpOnSaveFn;
	IATDeviceStorageManager *mpStorageMgr = nullptr;
};

#endif
