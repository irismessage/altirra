//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2022 Avery Lee
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
#include <vd2/system/filesys.h>
#include <at/atcore/blockdevice.h>
#include <at/atcore/deviceparent.h>
#include "devicemanager.h"
#include "uiaccessors.h"

bool ATUIIsElevationRequiredForMountVHDImage();
void ATUITemporarilyMountVHDImageW32(VDGUIHandle hwnd, const wchar_t *path, bool readOnly);

class ATDeviceXCmdMountVHD final : public IATDeviceXCmd {
public:
	int AddRef() override { return 2; }
	int Release() override { return 1; }

	bool IsSupported(IATDevice *dev, int busIndex) const override;
	ATDeviceXCmdInfo GetInfo() const override;
	void Invoke(ATDeviceManager& devMgr, IATDevice *dev, int busIndex) override;
} g_ATDeviceXCmdMountVHD;

bool ATDeviceXCmdMountVHD::IsSupported(IATDevice *dev, int busIndex) const {
	return vdpoly_cast<IATBlockDeviceDirectAccess *>(dev) != nullptr && busIndex < 0;
}

ATDeviceXCmdInfo ATDeviceXCmdMountVHD::GetInfo() const {
	ATDeviceXCmdInfo info;

	info.mbRequiresElevation = ATUIIsElevationRequiredForMountVHDImage();
	info.mDisplayName = L"Mount VHD disk image in Windows";
	return info;
}

void ATDeviceXCmdMountVHD::Invoke(ATDeviceManager& devmgr, IATDevice *dev, int busIndex) {
	IATBlockDevice *bd = vdpoly_cast<IATBlockDevice *>(dev);
	IATBlockDeviceDirectAccess *da = vdpoly_cast<IATBlockDeviceDirectAccess *>(dev);

	if (!bd || !da)
		throw MyError("This device cannot be mounted as a disk in Windows.");

	VDStringW directPath = da->GetVHDDirectAccessPath();
	if (directPath.empty())
		throw MyError("This device cannot be mounted as a disk in Windows.");

	const bool readOnly = bd->IsReadOnly();

	IATDeviceParent *const devParent = dev->GetParent();
	const auto devParentIndex = dev->GetParentBusIndex();

	VDStringW devStr;
	devmgr.SerializeDevice(dev, devStr, true, true);
	devmgr.RemoveDevice(dev);

	bd = nullptr;
	da = nullptr;

	auto finalize = [&] {
		devmgr.DeserializeDevices(devParent, devParent ? devParent->GetDeviceBus(devParentIndex) : nullptr, devStr.c_str());
	};

	try {
		ATUITemporarilyMountVHDImageW32(ATUIGetNewPopupOwner(), directPath.c_str(), readOnly);
	} catch(const MyError&) {
		try {
			finalize();
		} catch(const MyError&) {
			// eat
		}
		throw;
	}

	finalize();
}

void ATDeviceInitXCmdMountVHD(ATDeviceManager& dm) {
	dm.RegisterExtendedCommand(g_ATDeviceXCmdMountVHD);
}
