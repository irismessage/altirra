//	Altirra - Atari 800/800XL/5200 emulator
//	Device emulation library - joystick port dongle emulation
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <stdafx.h>
#include <vd2/system/binary.h>
#include <at/atcore/deviceport.h>
#include <at/atcore/propertyset.h>
#include <at/atdevices/dongle.h>

void ATCreateDeviceDongle(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceDongle> p(new ATDeviceDongle);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefDongle = { "dongle", "dongle", L"Joystick port dongle", ATCreateDeviceDongle };

///////////////////////////////////////////////////////////////////////////

ATDeviceDongle::ATDeviceDongle() {
}

ATDeviceDongle::~ATDeviceDongle() {
}

void ATDeviceDongle::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefDongle;
}

void ATDeviceDongle::GetSettings(ATPropertySet& pset) {
	pset.SetUint32("port", mPortIndex);
	
	wchar_t mappingStr[17] = {};

	for(int i=0; i<16; ++i) {
		mappingStr[i] = L"0123456789ABCDEF"[mResponseTable[i]];
	}

	pset.SetString("mapping", mappingStr);
}

bool ATDeviceDongle::SetSettings(const ATPropertySet& pset) {
	uint32 port = pset.GetUint32("port");

	if (port >= 4)
		port = 0;

	if (mPortIndex != port) {
		mPortIndex = port;

		ReinitPort();
	}

	memset(mResponseTable, 0x0F, sizeof mResponseTable);

	const wchar_t *mapping = pset.GetString("mapping");
	if (mapping) {
		for(int i=0; i<16; ++i) {
			wchar_t c = mapping[i];

			if (c >= L'0' && c <= L'9')
				mResponseTable[i] = (uint8)(c - L'0');
			else if (c >= L'A' && c <= L'F')
				mResponseTable[i] = (uint8)((c - L'A') + 10);
			else
				break;
		}

		UpdatePortOutput();
	}

	return true;
}

void ATDeviceDongle::Init() {
	mpPortManager = GetService<IATDevicePortManager>();

	ReinitPort();
}

void ATDeviceDongle::Shutdown() {
	mpPort = nullptr;
	mpPortManager = nullptr;
}

void ATDeviceDongle::ReinitPort() {
	if (mpPortManager) {
		mpPortManager->AllocControllerPort(mPortIndex, ~mpPort);
		mpPort->SetOnDirOutputChanged(15, [this] { UpdatePortOutput(); }, true);
	}
}

void ATDeviceDongle::UpdatePortOutput() {
	if (mpPort)
		mpPort->SetDirInput(mResponseTable[mpPort->GetCurrentDirOutput() & 15]);
}
