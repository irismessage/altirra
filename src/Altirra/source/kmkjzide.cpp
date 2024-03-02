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
#include <vd2/system/file.h>
#include <vd2/system/registry.h>
#include "kmkjzide.h"
#include "memorymanager.h"
#include "ide.h"
#include "uirender.h"

ATKMKJZIDE::ATKMKJZIDE(bool version2)
	: mpIDE(NULL)
	, mpUIRenderer(NULL)
	, mpMemMan(NULL)
	, mpMemLayerControl(NULL)
	, mpMemLayerFlash(NULL)
	, mpMemLayerFlashControl(NULL)
	, mpMemLayerRAM(NULL)
	, mpMemLayerSDX(NULL)
	, mpMemLayerSDXControl(NULL)
	, mHighDataLatch(0xFF)
	, mbVersion2(version2)
{
	memset(mFlash, 0xFF, sizeof mFlash);
	memset(mSDX, 0xFF, sizeof mSDX);
	mRTC.Init();

	LoadNVRAM();
}

ATKMKJZIDE::~ATKMKJZIDE() {
	SaveNVRAM();

	Shutdown();
}

void ATKMKJZIDE::LoadFirmware(bool sdx, const void *ptr, uint32 len) {
	if (sdx) {
		if (len > sizeof mSDX)
			len = sizeof mSDX;

		memcpy(mSDX, ptr, len);
		mSDXCtrl.SetDirty(false);
	} else {
		uint32 flashSize = mbVersion2 ? sizeof mFlash : 0x600;

		if (len > flashSize)
			len = flashSize;

		memcpy(mFlash, ptr, len);
		mFlashCtrl.SetDirty(false);
	}
}

void ATKMKJZIDE::LoadFirmware(bool sdx, const wchar_t *path) {
	void *flash;
	uint32 flashSize;

	if (sdx) {
		flash = mSDX;
		flashSize = sizeof mSDX;
		mSDXCtrl.SetDirty(false);
	} else {
		flash = mFlash;
		flashSize = mbVersion2 ? sizeof mFlash : 0x600;
		mFlashCtrl.SetDirty(false);
	}

	memset(flash, 0xFF, flashSize);

	VDFile f;
	f.open(path);
	f.readData(flash, flashSize);
}

void ATKMKJZIDE::SaveFirmware(bool sdx, const wchar_t *path) {
	void *flash;
	uint32 flashSize;

	if (sdx) {
		flash = mSDX;
		flashSize = sizeof mSDX;
	} else {
		flash = mFlash;
		flashSize = mbVersion2 ? sizeof mFlash : 0x600;
	}

	VDFile f;
	f.open(path, nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
	f.write(flash, flashSize);

	if (sdx)
		mSDXCtrl.SetDirty(false);
	else
		mFlashCtrl.SetDirty(false);
}

void ATKMKJZIDE::Init(ATIDEEmulator *ide, ATScheduler *sch, IATUIRenderer *uir) {
	mpIDE = ide;
	mpUIRenderer = uir;
	mbSelected = false;

	mFlashCtrl.Init(mFlash, kATFlashType_Am29F010, sch);
	mSDXCtrl.Init(mSDX, kATFlashType_Am29F040B, sch);
}

void ATKMKJZIDE::Shutdown() {
	mpIDE = NULL;
	mpUIRenderer = NULL;
}

void ATKMKJZIDE::AttachDevice(ATMemoryManager *memman) {
	mpMemMan = memman;

	ATMemoryHandlerTable handlers = {};
	handlers.mpThis = this;
	handlers.mbPassReads = true;
	handlers.mbPassWrites = true;
	handlers.mbPassAnticReads = true;
	handlers.mpDebugReadHandler = OnControlDebugRead;
	handlers.mpReadHandler = OnControlRead;
	handlers.mpWriteHandler = OnControlWrite;

	mpMemLayerControl = mpMemMan->CreateLayer(kATMemoryPri_PBI, handlers, 0xD1, 0x01);
	mpMemMan->EnableLayer(mpMemLayerControl, true);

	mpMemLayerFlash = mpMemMan->CreateLayer(kATMemoryPri_PBI, mFlash, 0xD8, 0x06, true);

	handlers.mbPassReads = false;
	handlers.mbPassWrites = false;
	handlers.mbPassAnticReads = false;
	handlers.mpDebugReadHandler = OnFlashRead;
	handlers.mpReadHandler = OnFlashRead;
	handlers.mpWriteHandler = OnFlashWrite;

	mpMemLayerFlashControl = mpMemMan->CreateLayer(kATMemoryPri_PBI, handlers, 0xD8, 0x06);
	mpMemLayerRAM = mpMemMan->CreateLayer(kATMemoryPri_PBI, mRAM, 0xDE, 0x02, false);

	mpMemLayerSDX = mpMemMan->CreateLayer(kATMemoryPri_Extsel, mSDX, 0xA0, 0x20, true);

	handlers.mpDebugReadHandler = OnSDXRead;
	handlers.mpReadHandler = OnSDXRead;
	handlers.mpWriteHandler = OnSDXWrite;

	mpMemLayerSDXControl = mpMemMan->CreateLayer(kATMemoryPri_Extsel+1, handlers, 0xA0, 0x20);

	ColdReset();
}

void ATKMKJZIDE::DetachDevice() {
	if (mpMemLayerSDXControl) {
		mpMemMan->DeleteLayer(mpMemLayerSDXControl);
		mpMemLayerSDXControl = NULL;
	}

	if (mpMemLayerSDX) {
		mpMemMan->DeleteLayer(mpMemLayerSDX);
		mpMemLayerSDX = NULL;
	}

	if (mpMemLayerFlashControl) {
		mpMemMan->DeleteLayer(mpMemLayerFlashControl);
		mpMemLayerFlashControl = NULL;
	}

	if (mpMemLayerControl) {
		mpMemMan->DeleteLayer(mpMemLayerControl);
		mpMemLayerControl = NULL;
	}

	if (mpMemLayerFlash) {
		mpMemMan->DeleteLayer(mpMemLayerFlash);
		mpMemLayerFlash = NULL;
	}

	if (mpMemLayerRAM) {
		mpMemMan->DeleteLayer(mpMemLayerRAM);
		mpMemLayerRAM = NULL;
	}

	mpMemMan = NULL;
}

void ATKMKJZIDE::GetDeviceInfo(ATPBIDeviceInfo& devInfo) const {
	devInfo.mDeviceId = 0x01;
	devInfo.mpName = L"KMK/JZ IDE";
}

void ATKMKJZIDE::Select(bool enable) {
	mbSelected = enable;
	mpMemMan->EnableLayer(mpMemLayerFlash, enable);
	mpMemMan->EnableLayer(mpMemLayerFlashControl, kATMemoryAccessMode_CPUWrite, enable);
	mpMemMan->EnableLayer(mpMemLayerRAM, enable);
}

void ATKMKJZIDE::ColdReset() {
	memset(mRAM, 0xFF, sizeof mRAM);

	mFlashCtrl.ColdReset();
	mSDXCtrl.ColdReset();

	WarmReset();
}

void ATKMKJZIDE::WarmReset() {
	mpMemMan->SetLayerMemory(mpMemLayerFlash, mFlash);
	mpMemMan->SetLayerMemory(mpMemLayerRAM, mRAM);
	mpMemMan->SetLayerMemory(mpMemLayerSDX, mSDX);

	mFlashBankOffset = 0 - 0xD800;
	mSDXBankOffset = 0 - 0xA000;
	mbSDXEnabled = true;

	UpdateMemoryLayersFlash();
	UpdateMemoryLayersSDX();
}

void ATKMKJZIDE::LoadNVRAM() {
	VDRegistryAppKey key("Nonvolatile RAM");

	uint8 buf[10];
	memset(buf, 0xFF, sizeof buf);

	if (key.getBinary("IDEPlus clock", (char *)buf, 10))
		mRTC.Load(buf);
}

void ATKMKJZIDE::SaveNVRAM() {
	VDRegistryAppKey key("Nonvolatile RAM");

	uint8 buf[10];
	memset(buf, 0xFF, sizeof buf);

	mRTC.Save(buf);

	key.setBinary("IDEPlus clock", (const char *)buf, 10);
}

sint32 ATKMKJZIDE::OnControlDebugRead(void *thisptr0, uint32 addr) {
	ATKMKJZIDE *thisptr = (ATKMKJZIDE *)thisptr0;

	if (!thisptr->mbSelected)
		return -1;

	uint8 addr8 = (uint8)addr;

	switch(addr8) {
		case 0x00:
		case 0x01:
		case 0x02:
		case 0x03:
		case 0x04:
		case 0x05:
		case 0x06:
		case 0x07:
		case 0x08:
		case 0x09:
		case 0x0A:
		case 0x0B:
		case 0x0C:
		case 0x0D:
		case 0x0E:
		case 0x0F:
			return thisptr->mHighDataLatch;

		case 0x10:
		case 0x11:
		case 0x12:
		case 0x13:
		case 0x14:
		case 0x15:
		case 0x16:
		case 0x17:
			return thisptr->mpIDE->DebugReadByte(addr8 & 0x07);

		case 0x1E:	// alternate status register
			return thisptr->mpIDE->DebugReadByte(0x07);

		case 0x20:
			return thisptr->mRTC.DebugReadBit() ? 0xFF : 0x7F;
	}

	return 0xFF;
}

sint32 ATKMKJZIDE::OnControlRead(void *thisptr0, uint32 addr) {
	ATKMKJZIDE *thisptr = (ATKMKJZIDE *)thisptr0;
	if (!thisptr->mbSelected)
		return -1;

	uint8 addr8 = (uint8)addr;

	switch(addr8) {
		case 0x00:
		case 0x01:
		case 0x02:
		case 0x03:
		case 0x04:
		case 0x05:
		case 0x06:
		case 0x07:
		case 0x08:
		case 0x09:
		case 0x0A:
		case 0x0B:
		case 0x0C:
		case 0x0D:
		case 0x0E:
		case 0x0F:
			return thisptr->mHighDataLatch;

		case 0x10:
			{
				uint32 v = thisptr->mpIDE->ReadDataLatch(true);

				thisptr->mHighDataLatch = (uint8)(v >> 8);
				return (uint8)v;
			}

		case 0x11:
		case 0x12:
		case 0x13:
		case 0x14:
		case 0x15:
		case 0x16:
		case 0x17:
			return thisptr->mpIDE->ReadByte(addr8 & 0x07);

		case 0x1E:	// alternate status register
			return thisptr->mpIDE->ReadByte(0x07);

		case 0x20:
			return thisptr->mRTC.ReadBit() ? 0xFF : 0x7F;
	}

	return 0xFF;
}

bool ATKMKJZIDE::OnControlWrite(void *thisptr0, uint32 addr, uint8 value) {
	ATKMKJZIDE *thisptr = (ATKMKJZIDE *)thisptr0;
	if (!thisptr->mbSelected && addr != 0xD1FE)
		return false;

	uint8 addr8 = (uint8)addr;

	switch(addr8) {
		case 0x00:
		case 0x01:
		case 0x02:
		case 0x03:
		case 0x04:
		case 0x05:
		case 0x06:
		case 0x07:
		case 0x08:
		case 0x09:
		case 0x0A:
		case 0x0B:
		case 0x0C:
		case 0x0D:
		case 0x0E:
		case 0x0F:
			thisptr->mHighDataLatch = value;
			return true;

		case 0x10:
			thisptr->mpIDE->WriteDataLatch(value, thisptr->mHighDataLatch);
			return true;

		case 0x11:
		case 0x12:
		case 0x13:
		case 0x14:
		case 0x15:
		case 0x16:
		case 0x17:
			thisptr->mpIDE->WriteByte(addr8 & 0x07, value);
			return true;

		case 0x20:
			thisptr->mRTC.WriteBit((value & 0x80) != 0);
			return true;

		case 0xA0:
			if (!thisptr->mbVersion2) {
				thisptr->mFlashBankOffset = 0x300 - 0xD800;

				thisptr->mpMemMan->SetLayerMemory(thisptr->mpMemLayerFlash, thisptr->mFlash + 0x300);
				thisptr->mpMemMan->SetLayerMemory(thisptr->mpMemLayerRAM, thisptr->mRAM + 0x100);
			}
			return true;

		case 0xC0:
			if (!thisptr->mbVersion2) {
				thisptr->mFlashBankOffset = 0 - 0xD800;

				thisptr->mpMemMan->SetLayerMemory(thisptr->mpMemLayerFlash, thisptr->mFlash);
				thisptr->mpMemMan->SetLayerMemory(thisptr->mpMemLayerRAM, thisptr->mRAM);
			}
			return true;

		case 0xFC:
			if (thisptr->mbVersion2) {
				uint32 offset = ((uint32)(value & 0x3f) << 11);

				thisptr->mFlashBankOffset = offset - 0xD800;

				thisptr->mpMemMan->SetLayerMemory(thisptr->mpMemLayerFlash, thisptr->mFlash + offset);
			}
			return true;

		case 0xFD:
			if (thisptr->mbVersion2) {
				uint32 offset = ((uint32)(value & 0x3f) << 9);

				thisptr->mpMemMan->SetLayerMemory(thisptr->mpMemLayerRAM, thisptr->mRAM + offset);
			}
			return true;

		case 0xFE:
			if (thisptr->mbVersion2) {
				uint32 offset = ((uint32)(value & 0x3f) << 13);

				thisptr->mSDXBankOffset = offset - 0xA000;

				thisptr->mpMemMan->SetLayerMemory(thisptr->mpMemLayerSDX, thisptr->mSDX + offset);

				thisptr->mbSDXEnabled = !(value & 0x80);
				thisptr->UpdateMemoryLayersSDX();
			}
			return true;
	}

	return true;
}

sint32 ATKMKJZIDE::OnFlashRead(void *thisptr0, uint32 addr) {
	ATKMKJZIDE *thisptr = (ATKMKJZIDE *)thisptr0;

	uint8 value;
	if (thisptr->mFlashCtrl.ReadByte(thisptr->mFlashBankOffset + addr, value))
		thisptr->UpdateMemoryLayersFlash();

	return value;
}

bool ATKMKJZIDE::OnFlashWrite(void *thisptr0, uint32 addr, uint8 value) {
	ATKMKJZIDE *thisptr = (ATKMKJZIDE *)thisptr0;

	if (thisptr->mFlashCtrl.WriteByte(thisptr->mFlashBankOffset + addr, value)) {
		if (thisptr->mpUIRenderer) {
			if (thisptr->mFlashCtrl.CheckForWriteActivity())
				thisptr->mpUIRenderer->SetFlashWriteActivity();
		}

		thisptr->UpdateMemoryLayersFlash();
	}

	return true;
}

sint32 ATKMKJZIDE::OnSDXRead(void *thisptr0, uint32 addr) {
	ATKMKJZIDE *thisptr = (ATKMKJZIDE *)thisptr0;

	uint8 value;
	if (thisptr->mSDXCtrl.ReadByte(thisptr->mSDXBankOffset + addr, value)) {
		thisptr->UpdateMemoryLayersSDX();
	}

	return value;
}

bool ATKMKJZIDE::OnSDXWrite(void *thisptr0, uint32 addr, uint8 value) {
	ATKMKJZIDE *thisptr = (ATKMKJZIDE *)thisptr0;

	if (thisptr->mSDXCtrl.WriteByte(thisptr->mSDXBankOffset + addr, value)) {
		if (thisptr->mpUIRenderer) {
			if (thisptr->mSDXCtrl.CheckForWriteActivity())
				thisptr->mpUIRenderer->SetFlashWriteActivity();
		}

		thisptr->UpdateMemoryLayersSDX();
	}

	return true;
}

void ATKMKJZIDE::UpdateMemoryLayersFlash() {
	const bool controlRead = mFlashCtrl.IsControlReadEnabled();

	mpMemMan->EnableLayer(mpMemLayerFlashControl, kATMemoryAccessMode_AnticRead, controlRead);
	mpMemMan->EnableLayer(mpMemLayerFlashControl, kATMemoryAccessMode_CPURead, controlRead);
}

void ATKMKJZIDE::UpdateMemoryLayersSDX() {
	const bool controlRead = mbSDXEnabled && mSDXCtrl.IsControlReadEnabled();

	mpMemMan->EnableLayer(mpMemLayerSDXControl, kATMemoryAccessMode_AnticRead, controlRead);
	mpMemMan->EnableLayer(mpMemLayerSDXControl, kATMemoryAccessMode_CPURead, controlRead);
	mpMemMan->EnableLayer(mpMemLayerSDXControl, kATMemoryAccessMode_CPUWrite, mbSDXEnabled);
	mpMemMan->EnableLayer(mpMemLayerSDX, mbSDXEnabled);
}
