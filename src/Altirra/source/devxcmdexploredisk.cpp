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

void ATUIShowDialogDiskExplorer(VDGUIHandle h, IATBlockDevice *dev, const wchar_t *devName);

class ATDeviceXCmdExploreDisk final : public IATDeviceXCmd {
public:
	int AddRef() override { return 2; }
	int Release() override { return 1; }

	bool IsSupported(IATDevice& dev) const override;
	ATDeviceXCmdInfo GetInfo() const override;
	void Invoke(ATDeviceManager& devMgr, IATDevice& dev) override;
} g_ATDeviceXCmdExploreDisk;

bool ATDeviceXCmdExploreDisk::IsSupported(IATDevice& dev) const {
	return vdpoly_cast<IATBlockDevice *>(&dev) != nullptr;
}

ATDeviceXCmdInfo ATDeviceXCmdExploreDisk::GetInfo() const {
	ATDeviceXCmdInfo info;

	info.mbRequiresElevation = false;
	info.mDisplayName = L"Explore Disk";
	return info;
}

void ATDeviceXCmdExploreDisk::Invoke(ATDeviceManager& devmgr, IATDevice& dev) {
	IATBlockDevice *bd = vdpoly_cast<IATBlockDevice *>(&dev);

	if (!bd)
		throw MyError("This device cannot be explored as a disk.");

	ATDeviceInfo di;
	dev.GetDeviceInfo(di);
	ATUIShowDialogDiskExplorer(ATUIGetNewPopupOwner(), bd, di.mpDef->mpName);
}

void ATDeviceInitXCmdExploreDisk(ATDeviceManager& dm) {
	dm.RegisterExtendedCommand(g_ATDeviceXCmdExploreDisk);
}
