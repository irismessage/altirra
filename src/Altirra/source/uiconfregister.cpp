//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2014 Avery Lee
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

#include <stdafx.h>
#include "devicemanager.h"

bool ATUIConfDevHardDisk(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevBlackBox(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevModem(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevDragonCart(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevPCLink(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevHostFS(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDev1030(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDev850(VDGUIHandle hParent, ATPropertySet& props);
bool ATUIConfDevSX212(VDGUIHandle hParent, ATPropertySet& props);

void ATRegisterDeviceConfigurers(ATDeviceManager& dev) {
	dev.AddDeviceConfigurer("harddisk", ATUIConfDevHardDisk);
	dev.AddDeviceConfigurer("blackbox", ATUIConfDevBlackBox);
	dev.AddDeviceConfigurer("modem", ATUIConfDevModem);
	dev.AddDeviceConfigurer("dragoncart", ATUIConfDevDragonCart);
	dev.AddDeviceConfigurer("pclink", ATUIConfDevPCLink);
	dev.AddDeviceConfigurer("hostfs", ATUIConfDevHostFS);
	dev.AddDeviceConfigurer("1030", ATUIConfDev1030);
	dev.AddDeviceConfigurer("850", ATUIConfDev850);
	dev.AddDeviceConfigurer("sx212", ATUIConfDevSX212);
}
