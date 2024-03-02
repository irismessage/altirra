//	Altirra - Atari 800/800XL/5200 emulator
//	Test module
//	Copyright (C) 2009-2020 Avery Lee
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
#include <at/atcore/propertyset.h>
#include "blockdevvirtfat32.h"
#include "idevhdimage.h"
#include "test.h"

DEFINE_TEST_NONAUTO(IO_VirtFAT16) {
	vdrefptr<ATBlockDeviceVFAT32> vfImage(new ATBlockDeviceVFAT32(true));
	ATPropertySet pset;
	pset.SetString("path", L".");
	vfImage->SetSettings(pset);
	vfImage->Init();

	uint32 sectorCount = vfImage->GetSectorCount();
	printf("Creating VHD with %d sectors\n", sectorCount);

	vdrefptr<ATIDEVHDImage> vhdImage(new ATIDEVHDImage);
	vhdImage->InitNew(L"virtfat16.vhd", 255, 63, sectorCount, true, nullptr);

	uint8 buf[512 * 8];
	for(uint32 i = 0; i < sectorCount; i += 8) {
		vfImage->ReadSectors(buf, i, 8);
		vhdImage->WriteSectors(buf, i, 8);
	}

	vhdImage->Flush();

	return 0;
}

DEFINE_TEST_NONAUTO(IO_VirtFAT32) {
	vdrefptr<ATBlockDeviceVFAT32> vfImage(new ATBlockDeviceVFAT32(false));
	ATPropertySet pset;
	pset.SetString("path", L".");
	vfImage->SetSettings(pset);
	vfImage->Init();

	uint32 sectorCount = vfImage->GetSectorCount();
	printf("Creating VHD with %d sectors\n", sectorCount);

	vdrefptr<ATIDEVHDImage> vhdImage(new ATIDEVHDImage);
	vhdImage->InitNew(L"virtfat32.vhd", 255, 63, sectorCount, true, nullptr);

	uint8 buf[512 * 8];
	for(uint32 i = 0; i < sectorCount; i += 8) {
		vfImage->ReadSectors(buf, i, 8);
		vhdImage->WriteSectors(buf, i, 8);
	}
	
	vhdImage->Flush();

	return 0;
}
