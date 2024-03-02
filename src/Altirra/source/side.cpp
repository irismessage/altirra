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
#include "side.h"
#include "memorymanager.h"
#include "ide.h"
#include "uirender.h"
#include "simulator.h"
#include "firmwaremanager.h"

ATSIDEEmulator::ATSIDEEmulator()
	: mpIDE(NULL)
	, mpUIRenderer(NULL)
	, mpMemMan(NULL)
	, mpSim(NULL)
	, mpMemLayerIDE(NULL)
	, mpMemLayerCart(NULL)
	, mpMemLayerCart2(NULL)
	, mpMemLayerCartControl(NULL)
	, mpMemLayerCartControl2(NULL)
	, mbExternalEnable(false)
	, mbSDXEnable(true)
	, mbTopEnable(false)
	, mbTopRightEnable(false)
	, mbIDEEnabled(true)
	, mbVersion2(false)
	, mSDXBankRegister(0)
	, mTopBankRegister(0)
	, mSDXBank(0)
	, mTopBank(0)
	, mBankOffset(0)
	, mBankOffset2(0)
{
	memset(mFlash, 0xFF, sizeof mFlash);

	mRTC.Init();

	LoadNVRAM();
}

ATSIDEEmulator::~ATSIDEEmulator() {
	SaveNVRAM();
}

void ATSIDEEmulator::LoadFirmware(const void *ptr, uint32 len) {
	if (len > sizeof mFlash)
		len = sizeof mFlash;

	memcpy(mFlash, ptr, len);
	mFlashCtrl.SetDirty(false);
}

void ATSIDEEmulator::LoadFirmware(ATFirmwareManager& fwmgr, uint64 id) {
	void *flash = mFlash;
	uint32 flashSize = sizeof mFlash;

	mFlashCtrl.SetDirty(false);

	memset(flash, 0xFF, flashSize);

	fwmgr.LoadFirmware(id, flash, 0, flashSize);
}

void ATSIDEEmulator::SaveFirmware(const wchar_t *path) {
	void *flash = mFlash;
	uint32 flashSize = sizeof mFlash;

	VDFile f;
	f.open(path, nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
	f.write(flash, flashSize);

	mFlashCtrl.SetDirty(false);
}

void ATSIDEEmulator::Init(ATScheduler *sch, IATUIRenderer *uir, ATMemoryManager *memman, ATSimulator *sim, bool version2) {
	mpUIRenderer = uir;
	mpMemMan = memman;
	mpSim = sim;
	mbVersion2 = version2;

	mFlashCtrl.Init(mFlash, kATFlashType_Am29F040B, sch);

	ATMemoryHandlerTable handlerTable = {};

	handlerTable.mpThis = this;
	handlerTable.mbPassAnticReads = true;
	handlerTable.mbPassReads = true;
	handlerTable.mbPassWrites = true;
	handlerTable.mpDebugReadHandler = OnDebugReadByte;
	handlerTable.mpReadHandler = OnReadByte;
	handlerTable.mpWriteHandler = OnWriteByte;
	mpMemLayerIDE = mpMemMan->CreateLayer(kATMemoryPri_CartridgeOverlay, handlerTable, 0xD5, 0x01);
	mpMemMan->SetLayerName(mpMemLayerIDE, "SIDE registers");
	mpMemMan->EnableLayer(mpMemLayerIDE, true);

	mpMemLayerCart = mpMemMan->CreateLayer(kATMemoryPri_CartridgeOverlay, mFlash, 0xA0, 0x20, true);
	mpMemMan->SetLayerName(mpMemLayerCart, "SIDE left cartridge window");

	if (mbVersion2) {
		mpMemLayerCart2 = mpMemMan->CreateLayer(kATMemoryPri_CartridgeOverlay, mFlash, 0x80, 0x20, true);
		mpMemMan->SetLayerName(mpMemLayerCart2, "SIDE right cartridge window");
	}

	handlerTable.mbPassReads = false;
	handlerTable.mbPassWrites = false;
	handlerTable.mbPassAnticReads = false;
	handlerTable.mpDebugReadHandler = OnCartRead;
	handlerTable.mpReadHandler = OnCartRead;
	handlerTable.mpWriteHandler = OnCartWrite;

	mpMemLayerCartControl = mpMemMan->CreateLayer(kATMemoryPri_CartridgeOverlay+1, handlerTable, 0xA0, 0x20);
	mpMemMan->SetLayerName(mpMemLayerCartControl, "SIDE flash control (left cart window)");

	handlerTable.mpDebugReadHandler = OnCartRead2;
	handlerTable.mpReadHandler = OnCartRead2;
	handlerTable.mpWriteHandler = OnCartWrite2;

	mpMemLayerCartControl2 = mpMemMan->CreateLayer(kATMemoryPri_CartridgeOverlay+1, handlerTable, 0x80, 0x20);
	mpMemMan->SetLayerName(mpMemLayerCartControl2, "SIDE flash control (right cart window)");

	mbExternalEnable = true;
}

void ATSIDEEmulator::Shutdown() {
	mFlashCtrl.Shutdown();

	if (mpMemLayerCartControl2) {
		mpMemMan->DeleteLayer(mpMemLayerCartControl2);
		mpMemLayerCartControl2 = NULL;
	}

	if (mpMemLayerCartControl) {
		mpMemMan->DeleteLayer(mpMemLayerCartControl);
		mpMemLayerCartControl = NULL;
	}

	if (mpMemLayerCart2) {
		mpMemMan->DeleteLayer(mpMemLayerCart2);
		mpMemLayerCart2 = NULL;
	}

	if (mpMemLayerCart) {
		mpMemMan->DeleteLayer(mpMemLayerCart);
		mpMemLayerCart = NULL;
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

void ATSIDEEmulator::SetIDEImage(ATIDEEmulator *ide) {
	mpIDE = ide;
}

void ATSIDEEmulator::SetExternalEnable(bool enable) {
	if (mbExternalEnable == enable)
		return;
	
	mbExternalEnable = enable;

	UpdateMemoryLayersCart();
}

void ATSIDEEmulator::SetSDXEnabled(bool enable) {
	if (mbSDXEnable == enable)
		return;

	mbSDXEnable = enable;
	UpdateMemoryLayersCart();
}

void ATSIDEEmulator::ResetCartBank() {
	mSDXBankRegister = 0x00;
	SetSDXBank(0, true);

	mTopBankRegister = 0x00;
	SetTopBank(0x20, false);
}

void ATSIDEEmulator::ColdReset() {
	mFlashCtrl.ColdReset();
	mRTC.ColdReset();

	ResetCartBank();

	mbIDEEnabled = true;
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

void ATSIDEEmulator::SetSDXBank(sint32 bank, bool topEnable) {
	if (mSDXBank == bank && mbTopEnable == topEnable)
		return;

	mSDXBank = bank;
	mbTopEnable = topEnable;

	UpdateMemoryLayersCart();
	mpSim->UpdateXLCartridgeLine();
}

void ATSIDEEmulator::SetTopBank(sint32 bank, bool topRightEnable) {
	// If the top cartridge is enabled in 16K mode, the LSB bank bit is ignored.
	// We force the LSB on in that case so the right cart window is in the right
	// place and the left cart window is 8K below that (mask LSB back off).
	if (topRightEnable)
		bank |= 0x01;

	if (mTopBank == bank && mbTopRightEnable == topRightEnable)
		return;

	mTopBank = bank;
	mbTopRightEnable = topRightEnable;

	UpdateMemoryLayersCart();
	mpSim->UpdateXLCartridgeLine();
}

sint32 ATSIDEEmulator::OnDebugReadByte(void *thisptr0, uint32 addr) {
	ATSIDEEmulator *thisptr = (ATSIDEEmulator *)thisptr0;

	if (addr < 0xD5E0)
		return -1;

	switch(addr) {
		case 0xD5F0:
		case 0xD5F1:
		case 0xD5F2:
		case 0xD5F3:
		case 0xD5F4:
		case 0xD5F5:
		case 0xD5F6:
		case 0xD5F7:
			if (thisptr->mbIDEEnabled && thisptr->mpIDE)
				return (uint8)thisptr->mpIDE->DebugReadByte((uint8)addr & 7);
			else
				return 0xFF;
	}

	return OnReadByte(thisptr0, addr);
}

sint32 ATSIDEEmulator::OnReadByte(void *thisptr0, uint32 addr) {
	ATSIDEEmulator *thisptr = (ATSIDEEmulator *)thisptr0;

	if (addr < 0xD5E0)
		return -1;

	switch(addr) {
		case 0xD5E1:	// SDX bank register
			if (thisptr->mbVersion2)
				return thisptr->mSDXBankRegister;

			break;

		case 0xD5E2:	// DS1305 RTC
			return thisptr->mRTC.ReadState() ? 0x08 : 0x00;

		case 0xD5E4:	// top cartridge bank switching
			if (thisptr->mbVersion2)
				return thisptr->mTopBankRegister;

			return 0xFF;

		case 0xD5F0:
		case 0xD5F1:
		case 0xD5F2:
		case 0xD5F3:
		case 0xD5F4:
		case 0xD5F5:
		case 0xD5F6:
		case 0xD5F7:
			return thisptr->mbIDEEnabled && thisptr->mpIDE ? (uint8)thisptr->mpIDE->ReadByte((uint8)addr & 7) : 0xFF;

		case 0xD5F8:
			if (thisptr->mbVersion2)
				return 0x32;

			break;

		case 0xD5F9:
			if (thisptr->mbVersion2) {
				// LSB=1 is currently card removed, which we don't support
				// yet.
				return 0x00;
			}

			break;

		case 0xD5FC:	return thisptr->mbSDXEnable ? 'S' : ' ';
		case 0xD5FD:	return 'I';
		case 0xD5FE:	return 'D';
		case 0xD5FF:	return 'E';
	}

	return -1;
}

bool ATSIDEEmulator::OnWriteByte(void *thisptr0, uint32 addr, uint8 value) {
	ATSIDEEmulator *thisptr = (ATSIDEEmulator *)thisptr0;

	if (addr < 0xD5E0)
		return false;

	switch(addr) {
		case 0xD5E0:
			if (!thisptr->mbVersion2 && thisptr->mSDXBankRegister != value) {
				thisptr->mSDXBankRegister = value;

				thisptr->SetSDXBank(value & 0x80 ? -1 : (value & 0x3f), !(value & 0x40));
			}
			break;

		case 0xD5E1:
			if (thisptr->mbVersion2 && thisptr->mSDXBankRegister != value) {
				thisptr->mSDXBankRegister = value;

				thisptr->SetSDXBank(value & 0x80 ? -1 : (value & 0x3f), !(value & 0x40));
			}
			break;

		case 0xD5E2:	// DS1305 RTC
			thisptr->mRTC.WriteState((value & 1) != 0, !(value & 2), (value & 4) != 0);
			break;

		case 0xD5E4:	// top cartridge bank switching
			if (thisptr->mTopBankRegister != value) {
				thisptr->mTopBankRegister = value;
				thisptr->SetTopBank(value & 0x80 ? -1 : (value & 0x3f) ^ 0x20, thisptr->mbVersion2 && ((value & 0x40) != 0));
			}
			break;

		case 0xD5F0:
		case 0xD5F1:
		case 0xD5F2:
		case 0xD5F3:
		case 0xD5F4:
		case 0xD5F5:
		case 0xD5F6:
		case 0xD5F7:
			if (thisptr->mbIDEEnabled && thisptr->mpIDE)
				thisptr->mpIDE->WriteByte((uint8)addr & 7, value);
			break;

		case 0xD5F8:	// F8-FB: D0 = /reset
		case 0xD5F9:
		case 0xD5FA:
		case 0xD5FB:
			if (thisptr->mbVersion2) {
				if (addr == 0xD5F9) {
					// Strobe to clear CARD_REMOVED (which we don't support yet).
				}

				thisptr->mbIDEEnabled = !(value & 0x80);
			}

			if (thisptr->mpIDE)
				thisptr->mpIDE->SetReset(!(value & 1));
			break;
	}

	return true;
}

sint32 ATSIDEEmulator::OnCartRead(void *thisptr0, uint32 addr) {
	ATSIDEEmulator *thisptr = (ATSIDEEmulator *)thisptr0;

	uint8 value;
	if (thisptr->mFlashCtrl.ReadByte(thisptr->mBankOffset + (addr - 0xA000), value)) {
		if (thisptr->mpUIRenderer) {
			if (thisptr->mFlashCtrl.CheckForWriteActivity())
				thisptr->mpUIRenderer->SetFlashWriteActivity();
		}

		thisptr->UpdateMemoryLayersCart();
	}

	return value;
}

sint32 ATSIDEEmulator::OnCartRead2(void *thisptr0, uint32 addr) {
	ATSIDEEmulator *thisptr = (ATSIDEEmulator *)thisptr0;

	uint8 value;
	if (thisptr->mFlashCtrl.ReadByte(thisptr->mBankOffset2 + (addr - 0x8000), value)) {
		if (thisptr->mpUIRenderer) {
			if (thisptr->mFlashCtrl.CheckForWriteActivity())
				thisptr->mpUIRenderer->SetFlashWriteActivity();
		}

		thisptr->UpdateMemoryLayersCart();
	}

	return value;
}

bool ATSIDEEmulator::OnCartWrite(void *thisptr0, uint32 addr, uint8 value) {
	ATSIDEEmulator *thisptr = (ATSIDEEmulator *)thisptr0;

	if (thisptr->mFlashCtrl.WriteByte(thisptr->mBankOffset + (addr - 0xA000), value)) {
		if (thisptr->mpUIRenderer) {
			if (thisptr->mFlashCtrl.CheckForWriteActivity())
				thisptr->mpUIRenderer->SetFlashWriteActivity();
		}

		thisptr->UpdateMemoryLayersCart();
	}

	return true;
}

bool ATSIDEEmulator::OnCartWrite2(void *thisptr0, uint32 addr, uint8 value) {
	ATSIDEEmulator *thisptr = (ATSIDEEmulator *)thisptr0;

	if (thisptr->mFlashCtrl.WriteByte(thisptr->mBankOffset2 + (addr - 0x8000), value)) {
		if (thisptr->mpUIRenderer) {
			if (thisptr->mFlashCtrl.CheckForWriteActivity())
				thisptr->mpUIRenderer->SetFlashWriteActivity();
		}

		thisptr->UpdateMemoryLayersCart();
	}

	return true;
}

void ATSIDEEmulator::UpdateMemoryLayersCart() {
	if (mSDXBank >= 0 && mbSDXEnable)
		mBankOffset = mSDXBank << 13;
	else if (mTopBank >= 0 && (mbTopEnable || !mbSDXEnable))
		mBankOffset = mTopBank << 13;

	mBankOffset2 = mBankOffset & ~0x2000;

	mpMemMan->SetLayerMemory(mpMemLayerCart, mFlash + mBankOffset);

	if (mbVersion2)
		mpMemMan->SetLayerMemory(mpMemLayerCart2, mFlash + mBankOffset2);

	// SDX disabled by switch => top cartridge enabled, SDX control bits ignored
	// else   SDX disabled, top cartridge disabled => no cartridge
	//        SDX disabled, top cartridge enabled => top cartridge
	//        other => SDX cartridge

	const bool sdxRead = (mbSDXEnable && mSDXBank >= 0);
	const bool topRead = ((mbTopEnable || !mbSDXEnable) && mTopBank >= 0);

	const bool flashRead = mbExternalEnable && (sdxRead || topRead);
	const bool controlRead = flashRead && mFlashCtrl.IsControlReadEnabled();

	mpMemMan->EnableLayer(mpMemLayerCartControl, kATMemoryAccessMode_AnticRead, controlRead);
	mpMemMan->EnableLayer(mpMemLayerCartControl, kATMemoryAccessMode_CPURead, controlRead);
	mpMemMan->EnableLayer(mpMemLayerCartControl, kATMemoryAccessMode_CPUWrite, flashRead);
	mpMemMan->EnableLayer(mpMemLayerCart, flashRead);

	if (mbVersion2) {
		const bool flashReadRight = mbExternalEnable && topRead && !sdxRead && mbTopRightEnable;
		const bool controlReadRight = flashReadRight && mFlashCtrl.IsControlReadEnabled();

		mpMemMan->EnableLayer(mpMemLayerCartControl2, kATMemoryAccessMode_AnticRead, controlReadRight);
		mpMemMan->EnableLayer(mpMemLayerCartControl2, kATMemoryAccessMode_CPURead, controlReadRight);
		mpMemMan->EnableLayer(mpMemLayerCartControl2, kATMemoryAccessMode_CPUWrite, flashReadRight);
		mpMemMan->EnableLayer(mpMemLayerCart2, flashReadRight);
	}
}
