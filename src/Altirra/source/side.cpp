//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2011 Avery Lee
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
#include <vd2/system/registry.h>
#include "side.h"
#include "memorymanager.h"
#include "ide.h"
#include "uirender.h"
#include "simulator.h"

ATSIDEEmulator::ATSIDEEmulator()
	: mpIDE(NULL)
	, mpUIRenderer(NULL)
	, mpMemMan(NULL)
	, mpSim(NULL)
	, mpMemLayerIDE(NULL)
	, mpMemLayerSDX(NULL)
	, mpMemLayerSDXControl(NULL)
	, mbSDXEnabled(false)
{
	memset(mSDX, 0xFF, sizeof mSDX);

	mRTC.Init();

	LoadNVRAM();
}

ATSIDEEmulator::~ATSIDEEmulator() {
	SaveNVRAM();
}

void ATSIDEEmulator::LoadFirmware(const void *ptr, uint32 len) {
	if (len > sizeof mSDX)
		len = sizeof mSDX;

	memcpy(mSDX, ptr, len);
	mSDXCtrl.SetDirty(false);
}

void ATSIDEEmulator::LoadFirmware(const wchar_t *path) {
	void *flash = mSDX;
	uint32 flashSize = sizeof mSDX;

	mSDXCtrl.SetDirty(false);

	memset(flash, 0xFF, flashSize);

	VDFile f;
	f.open(path);
	f.readData(flash, flashSize);
}

void ATSIDEEmulator::SaveFirmware(const wchar_t *path) {
	void *flash = mSDX;
	uint32 flashSize = sizeof mSDX;

	VDFile f;
	f.open(path, nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
	f.write(flash, flashSize);

	mSDXCtrl.SetDirty(false);
}

void ATSIDEEmulator::Init(ATIDEEmulator *ide, ATScheduler *sch, IATUIRenderer *uir, ATMemoryManager *memman, ATSimulator *sim) {
	mpIDE = ide;
	mpUIRenderer = uir;
	mpMemMan = memman;
	mpSim = sim;

	mSDXCtrl.Init(mSDX, kATFlashType_Am29F040B, sch);

	ATMemoryHandlerTable handlerTable = {};

	handlerTable.mpThis = this;
	handlerTable.mbPassAnticReads = true;
	handlerTable.mbPassReads = true;
	handlerTable.mbPassWrites = true;
	handlerTable.mpDebugReadHandler = OnDebugReadByte;
	handlerTable.mpReadHandler = OnReadByte;
	handlerTable.mpWriteHandler = OnWriteByte;
	mpMemLayerIDE = mpMemMan->CreateLayer(kATMemoryPri_CartridgeOverlay, handlerTable, 0xD5, 0x01);
	mpMemMan->EnableLayer(mpMemLayerIDE, true);

	mpMemLayerSDX = mpMemMan->CreateLayer(kATMemoryPri_CartridgeOverlay, mSDX, 0xA0, 0x20, true);

	handlerTable.mbPassReads = false;
	handlerTable.mbPassWrites = false;
	handlerTable.mbPassAnticReads = false;
	handlerTable.mpDebugReadHandler = OnSDXRead;
	handlerTable.mpReadHandler = OnSDXRead;
	handlerTable.mpWriteHandler = OnSDXWrite;

	mpMemLayerSDXControl = mpMemMan->CreateLayer(kATMemoryPri_CartridgeOverlay+1, handlerTable, 0xA0, 0x20);
}

void ATSIDEEmulator::Shutdown() {
	mSDXCtrl.Shutdown();

	if (mpMemLayerSDXControl) {
		mpMemMan->DeleteLayer(mpMemLayerSDXControl);
		mpMemLayerSDXControl = NULL;
	}

	if (mpMemLayerSDX) {
		mpMemMan->DeleteLayer(mpMemLayerSDX);
		mpMemLayerSDX = NULL;
	}

	if (mpMemLayerIDE) {
		mpMemMan->DeleteLayer(mpMemLayerIDE);
		mpMemLayerIDE = NULL;
	}

	mpMemMan = NULL;
	mpUIRenderer = NULL;
	mpIDE = NULL;
	mpSim = NULL;
}

void ATSIDEEmulator::ColdReset() {
	mSDXCtrl.ColdReset();
	mRTC.ColdReset();

	mpMemMan->SetLayerMemory(mpMemLayerSDX, mSDX + (63 << 13));

	mSDXBankOffset = (63 << 13) - 0xA000;
	mbSDXEnabled = true;

	UpdateMemoryLayersSDX();
}

void ATSIDEEmulator::LoadNVRAM() {
	VDRegistryAppKey key("Nonvolatile RAM");

	uint8 buf[0x72];
	memset(buf, 0, sizeof buf);

	if (key.getBinary("SIDE clock", (char *)buf, 0x72))
		mRTC.Load(buf);
}

void ATSIDEEmulator::SaveNVRAM() {
	VDRegistryAppKey key("Nonvolatile RAM");

	uint8 buf[0x72];
	memset(buf, 0, sizeof buf);

	mRTC.Save(buf);

	key.setBinary("SIDE clock", (const char *)buf, 0x72);
}

void ATSIDEEmulator::DumpRTCStatus() {
	mRTC.DumpStatus();
}

sint32 ATSIDEEmulator::OnDebugReadByte(void *thisptr0, uint32 addr) {
	ATSIDEEmulator *thisptr = (ATSIDEEmulator *)thisptr0;

	if (addr < 0xD5E0)
		return -1;

	switch(addr) {
		case 0xD5E2:	// DS1305 RTC
			return thisptr->mRTC.ReadState() ? 0x08 : 0x00;

		case 0xD5E4:	// top cartridge bank switching
			return 0xFF;

		case 0xD5F0:
		case 0xD5F1:
		case 0xD5F2:
		case 0xD5F3:
		case 0xD5F4:
		case 0xD5F5:
		case 0xD5F6:
		case 0xD5F7:
			return (uint8)thisptr->mpIDE->DebugReadByte((uint8)addr & 7);

		case 0xD5FC:	return 'S';
		case 0xD5FD:	return 'I';
		case 0xD5FE:	return 'D';
		case 0xD5FF:	return 'E';

		default:
			return 0xFF;
	}
}

sint32 ATSIDEEmulator::OnReadByte(void *thisptr0, uint32 addr) {
	ATSIDEEmulator *thisptr = (ATSIDEEmulator *)thisptr0;

	if (addr < 0xD5E0)
		return -1;

	switch(addr) {
		case 0xD5E2:	// DS1305 RTC
			return thisptr->mRTC.ReadState() ? 0x08 : 0x00;

		case 0xD5E4:	// top cartridge bank switching
			return 0xFF;

		case 0xD5F0:
		case 0xD5F1:
		case 0xD5F2:
		case 0xD5F3:
		case 0xD5F4:
		case 0xD5F5:
		case 0xD5F6:
		case 0xD5F7:
			return (uint8)thisptr->mpIDE->ReadByte((uint8)addr & 7);

		case 0xD5FC:	return 'S';
		case 0xD5FD:	return 'I';
		case 0xD5FE:	return 'D';
		case 0xD5FF:	return 'E';

		default:
			return 0xFF;
	}
}

bool ATSIDEEmulator::OnWriteByte(void *thisptr0, uint32 addr, uint8 value) {
	ATSIDEEmulator *thisptr = (ATSIDEEmulator *)thisptr0;

	if (addr < 0xD5E0)
		return false;

	switch(addr) {
		case 0xD5E0:
			{
				uint32 offset = ((uint32)(~value & 0x3f) << 13);

				thisptr->mSDXBankOffset = offset - 0xA000;

				thisptr->mpMemMan->SetLayerMemory(thisptr->mpMemLayerSDX, thisptr->mSDX + offset);

				bool en = !(value & 0x80);
				
				if (thisptr->mbSDXEnabled != en) {
					thisptr->mbSDXEnabled = en;
					thisptr->UpdateMemoryLayersSDX();
					thisptr->mpSim->UpdateXLCartridgeLine();
				}
			}
			break;

		case 0xD5E2:	// DS1305 RTC
			thisptr->mRTC.WriteState((value & 1) != 0, !(value & 2), (value & 4) != 0);
			break;

		case 0xD5E4:	// top cartridge bank switching
			break;

		case 0xD5F0:
		case 0xD5F1:
		case 0xD5F2:
		case 0xD5F3:
		case 0xD5F4:
		case 0xD5F5:
		case 0xD5F6:
		case 0xD5F7:
			thisptr->mpIDE->WriteByte((uint8)addr & 7, value);
			break;

		case 0xD5F8:	// F8-FB: D0 = /reset
		case 0xD5F9:
		case 0xD5FA:
		case 0xD5FB:
			thisptr->mpIDE->SetReset(!(value & 1));
			break;
	}

	return true;
}

sint32 ATSIDEEmulator::OnSDXRead(void *thisptr0, uint32 addr) {
	ATSIDEEmulator *thisptr = (ATSIDEEmulator *)thisptr0;

	uint8 value;
	if (thisptr->mSDXCtrl.ReadByte(thisptr->mSDXBankOffset + addr, value)) {
		if (thisptr->mpUIRenderer) {
			if (thisptr->mSDXCtrl.CheckForWriteActivity())
				thisptr->mpUIRenderer->SetFlashWriteActivity();
		}

		thisptr->UpdateMemoryLayersSDX();
	}

	return value;
}

bool ATSIDEEmulator::OnSDXWrite(void *thisptr0, uint32 addr, uint8 value) {
	ATSIDEEmulator *thisptr = (ATSIDEEmulator *)thisptr0;

	if (thisptr->mSDXCtrl.WriteByte(thisptr->mSDXBankOffset + addr, value)) {
		if (thisptr->mpUIRenderer) {
			if (thisptr->mSDXCtrl.CheckForWriteActivity())
				thisptr->mpUIRenderer->SetFlashWriteActivity();
		}

		thisptr->UpdateMemoryLayersSDX();
	}

	return true;
}

void ATSIDEEmulator::UpdateMemoryLayersSDX() {
	const bool controlRead = mbSDXEnabled && mSDXCtrl.IsControlReadEnabled();

	mpMemMan->EnableLayer(mpMemLayerSDXControl, kATMemoryAccessMode_AnticRead, controlRead);
	mpMemMan->EnableLayer(mpMemLayerSDXControl, kATMemoryAccessMode_CPURead, controlRead);
	mpMemMan->EnableLayer(mpMemLayerSDXControl, kATMemoryAccessMode_CPUWrite, mbSDXEnabled);
	mpMemMan->EnableLayer(mpMemLayerSDX, mbSDXEnabled);
}
