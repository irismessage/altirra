//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2012 Avery Lee
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
#include <vd2/system/hash.h>
#include <vd2/system/int128.h>
#include "myide.h"
#include "memorymanager.h"
#include "ide.h"
#include "uirender.h"
#include "simulator.h"
#include "firmwaremanager.h"

ATMyIDEEmulator::ATMyIDEEmulator()
	: mpMemMan(NULL)
	, mpMemLayerIDE(NULL)
	, mpMemLayerLeftCart(NULL)
	, mpMemLayerRightCart(NULL)
	, mpMemLayerLeftCartFlash(NULL)
	, mpMemLayerRightCartFlash(NULL)
	, mpIDE(NULL)
	, mpUIRenderer(NULL)
	, mpSim(NULL)
	, mbVersion2(false)
	, mbVersion2Ex(false)
	, mCartBank(0)
	, mCartBank2(0)
{
	memset(mFirmware, 0xFF, sizeof mFirmware);
}

ATMyIDEEmulator::~ATMyIDEEmulator() {
}

bool ATMyIDEEmulator::IsLeftCartEnabled() const {
	return mCartBank >= 0;
}

void ATMyIDEEmulator::Init(ATMemoryManager *memman, IATUIRenderer *uir, ATScheduler *sch, ATSimulator *sim, bool used5xx, bool v2, bool v2ex) {
	mpMemMan = memman;
	mpUIRenderer = uir;
	mbVersion2 = v2;
	mbVersion2Ex = v2ex;
	mpSim = sim;
	mbUseD5xx = used5xx;

	mFlash.SetDirty(false);

	ATMemoryHandlerTable handlerTable = {};

	handlerTable.mbPassAnticReads = true;
	handlerTable.mbPassReads = true;
	handlerTable.mbPassWrites = true;

	if (v2) {
		mFlash.Init(mFirmware, kATFlashType_Am29F040B, sch);

		handlerTable.mpThis = this;
		handlerTable.mpDebugReadHandler = OnDebugReadByte_CCTL_V2;
		handlerTable.mpReadHandler = OnReadByte_CCTL_V2;
		handlerTable.mpWriteHandler = OnWriteByte_CCTL_V2;

		mpMemLayerIDE = mpMemMan->CreateLayer(kATMemoryPri_CartridgeOverlay, handlerTable, 0xD5, 0x01);

		handlerTable.mpDebugReadHandler = ReadByte_Cart_V2;
		handlerTable.mpReadHandler = ReadByte_Cart_V2;
		handlerTable.mpWriteHandler = WriteByte_Cart_V2;

		mpMemLayerLeftCartFlash = mpMemMan->CreateLayer(kATMemoryPri_CartridgeOverlay+1, handlerTable, 0xA0, 0x20);
		mpMemLayerRightCartFlash = mpMemMan->CreateLayer(kATMemoryPri_CartridgeOverlay+1, handlerTable, 0x80, 0x20);

		mpMemLayerLeftCart = mpMemMan->CreateLayer(kATMemoryPri_CartridgeOverlay, mFirmware, 0xA0, 0x20, true);
		mpMemLayerRightCart = mpMemMan->CreateLayer(kATMemoryPri_CartridgeOverlay, mFirmware, 0x80, 0x20, true);

		mCartBank = 0;
		mCartBank2 = -1;

		UpdateCartBank();
		UpdateCartBank2();
	} else {
		handlerTable.mpThis = this;
		handlerTable.mpDebugReadHandler = OnDebugReadByte_CCTL;
		handlerTable.mpReadHandler = OnReadByte_CCTL;
		handlerTable.mpWriteHandler = OnWriteByte_CCTL;

		mpMemLayerIDE = mpMemMan->CreateLayer(kATMemoryPri_Cartridge1 - 1, handlerTable, used5xx ? 0xD5 : 0xD1, 0x01);

		mCartBank = -1;
		mCartBank2 = -1;
	}

	mpMemMan->EnableLayer(mpMemLayerIDE, true);

	ColdReset();
}

void ATMyIDEEmulator::Shutdown() {
	mFlash.Shutdown();

	if (mpMemLayerRightCartFlash) {
		mpMemMan->DeleteLayer(mpMemLayerRightCartFlash);
		mpMemLayerRightCartFlash = NULL;
	}

	if (mpMemLayerLeftCartFlash) {
		mpMemMan->DeleteLayer(mpMemLayerLeftCartFlash);
		mpMemLayerLeftCartFlash = NULL;
	}

	if (mpMemLayerRightCart) {
		mpMemMan->DeleteLayer(mpMemLayerRightCart);
		mpMemLayerRightCart = NULL;
	}

	if (mpMemLayerLeftCart) {
		mpMemMan->DeleteLayer(mpMemLayerLeftCart);
		mpMemLayerLeftCart = NULL;
	}

	if (mpMemLayerIDE) {
		mpMemMan->DeleteLayer(mpMemLayerIDE);
		mpMemLayerIDE = NULL;
	}

	mpUIRenderer = NULL;
	mpMemMan = NULL;
	mpIDE = NULL;
	mpSim = NULL;
}

void ATMyIDEEmulator::SetIDEImage(ATIDEEmulator *ide) {
	mpIDE = ide;

	UpdateIDEReset();
}

bool ATMyIDEEmulator::LoadFirmware(const void *ptr, uint32 len) {
	vduint128 oldHash = VDHash128(mFirmware, sizeof mFirmware);

	if (len > sizeof mFirmware)
		len = sizeof mFirmware;

	memset(mFirmware, 0xFF, sizeof mFirmware);
	memcpy(mFirmware, ptr, len);
	mFlash.SetDirty(false);

	return oldHash != VDHash128(mFirmware, sizeof mFirmware);
}

bool ATMyIDEEmulator::LoadFirmware(ATFirmwareManager& fwmgr, uint64 id) {
	void *flash = mFirmware;
	uint32 flashSize = sizeof mFirmware;

	vduint128 oldHash = VDHash128(flash, flashSize);

	mFlash.SetDirty(false);

	memset(flash, 0xFF, flashSize);

	fwmgr.LoadFirmware(id, flash, 0, flashSize);

	return oldHash != VDHash128(flash, flashSize);
}

void ATMyIDEEmulator::SaveFirmware(const wchar_t *path) {
	void *flash = mFirmware;
	uint32 flashSize = sizeof mFirmware;

	VDFile f;
	f.open(path, nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
	f.write(flash, flashSize);

	mFlash.SetDirty(false);
}

void ATMyIDEEmulator::ColdReset() {
	if (mbVersion2) {
		mbCFPower = false;
		mbCFReset = true;
	} else {
		mbCFPower = true;
		mbCFReset = false;
	}

	mbCFAltReg = false;

	mLeftPage = 0;
	mRightPage = 0;
	mKeyHolePage = 0;
	mControl = 0x30;

	if (mbVersion2) {
		mCartBank = 0;
		mCartBank2 = -1;

		UpdateCartBank();
		UpdateCartBank2();

		memset(mRAM, 0xFF, sizeof mRAM);
	}

	UpdateIDEReset();
}

sint32 ATMyIDEEmulator::OnDebugReadByte_CCTL(void *thisptr0, uint32 addr) {
	ATMyIDEEmulator *thisptr = (ATMyIDEEmulator *)thisptr0;

	if (!thisptr->mpIDE)
		return 0xFF;

	return (uint8)thisptr->mpIDE->DebugReadByte((uint8)addr);
}

sint32 ATMyIDEEmulator::OnReadByte_CCTL(void *thisptr0, uint32 addr) {
	ATMyIDEEmulator *thisptr = (ATMyIDEEmulator *)thisptr0;

	if (!thisptr->mpIDE)
		return 0xFF;

	return (uint8)thisptr->mpIDE->ReadByte((uint8)addr);
}

bool ATMyIDEEmulator::OnWriteByte_CCTL(void *thisptr0, uint32 addr, uint8 value) {
	ATMyIDEEmulator *thisptr = (ATMyIDEEmulator *)thisptr0;

	if (thisptr->mpIDE)
		thisptr->mpIDE->WriteByte((uint8)addr, value);

	return true;
}

sint32 ATMyIDEEmulator::OnDebugReadByte_CCTL_V2(void *thisptr0, uint32 addr) {
	ATMyIDEEmulator *thisptr = (ATMyIDEEmulator *)thisptr0;

	// The updated V2 maps $D540-D57F to the data register.
	if (thisptr->mbVersion2Ex && addr >= 0xD540 && addr < 0xD580) {
		addr = 0xD500;
	}

	if (addr < 0xD508) {

		if (!thisptr->mbCFPower || !thisptr->mpIDE)
			return 0xFF;

		if (thisptr->mbCFAltReg)
			return (uint8)thisptr->mpIDE->ReadByteAlt((uint8)addr);
		else
			return (uint8)thisptr->mpIDE->DebugReadByte((uint8)addr);
	}

	return OnReadByte_CCTL_V2(thisptr0, addr);
}

sint32 ATMyIDEEmulator::OnReadByte_CCTL_V2(void *thisptr0, uint32 addr) {
	ATMyIDEEmulator *thisptr = (ATMyIDEEmulator *)thisptr0;

	// The updated V2 maps $D540-D57F to the data register.
	if (thisptr->mbVersion2Ex && addr >= 0xD540 && addr < 0xD580) {
		addr = 0xD500;
	}

	if (addr < 0xD510) {
		if (addr >= 0xD508) {
			// bit 7 = CF present
			// bit 6 = CF /RESET
			// bit 5 = CF powered

			uint8 status = 0x1F;

			if (thisptr->mpIDE)
				status |= 0x80;

			if (!thisptr->mbCFReset)
				status |= 0x40;

			if (thisptr->mbCFPower)
				status |= 0x20;

			return status;
		}

		if (!thisptr->mbCFPower || !thisptr->mpIDE)
			return 0xFF;

		if (thisptr->mbCFAltReg)
			return (uint8)thisptr->mpIDE->ReadByteAlt((uint8)addr);
		else
			return (uint8)thisptr->mpIDE->ReadByte((uint8)addr);
	} else if (addr >= 0xD580) {
		uint8 data;

		switch(thisptr->mControl & 0x0c) {
			case 0x00:		// R/W SRAM
			case 0x04:		// R/O SRAM
			default:
				return thisptr->mRAM[thisptr->mKeyHolePage + (addr - 0xD580)];

			case 0x08:
				thisptr->mFlash.ReadByte(thisptr->mKeyHolePage + (addr - 0xD580), data);
				return data;

			case 0x0c:		// disabled
				break;
		}
	}

	return -1;
}

bool ATMyIDEEmulator::OnWriteByte_CCTL_V2(void *thisptr0, uint32 addr, uint8 value) {
	ATMyIDEEmulator *thisptr = (ATMyIDEEmulator *)thisptr0;

	if (addr >= 0xD580) {
		switch(thisptr->mControl & 0x0c) {
			case 0x00:		// R/W SRAM
			default:
				thisptr->mRAM[thisptr->mKeyHolePage + (addr - 0xD580)] = value;
				break;

			case 0x04:		// R/O SRAM
			case 0x0c:		// disabled
				break;

			case 0x08:
				thisptr->mFlash.WriteByte(thisptr->mKeyHolePage + (addr - 0xD580), value);
				break;
		}
		return true;
	}

	// The updated V2 maps $D540-D57F to the data register.
	if (thisptr->mbVersion2Ex && addr >= 0xD540) {
		addr = 0xD500;
	}
	
	switch(addr) {
		case 0xD500:
		case 0xD501:
		case 0xD502:
		case 0xD503:
		case 0xD504:
		case 0xD505:
		case 0xD506:
		case 0xD507:
			if (thisptr->mbCFPower && thisptr->mpIDE) {
				if (thisptr->mbCFAltReg)
					thisptr->mpIDE->WriteByteAlt((uint8)addr, value);
				else
					thisptr->mpIDE->WriteByte((uint8)addr, value);
			}
			break;

		case 0xD508:
			value &= 0x3f;

			if (thisptr->mLeftPage != value) {
				thisptr->mLeftPage = value;

				if ((thisptr->mControl & 0xc0) < 0xc0)
					thisptr->SetCartBank(((thisptr->mControl & 0xc0) << 2) + value);
			}
			return true;

		case 0xD50A:
			value &= 0x3f;

			if (thisptr->mRightPage != value) {
				thisptr->mRightPage = value;

				if ((thisptr->mControl & 0x30) < 0x30)
					thisptr->SetCartBank2(((thisptr->mControl & 0x30) << 4) + value);
			}
			return true;

		case 0xD50C:
			thisptr->mKeyHolePage = (thisptr->mKeyHolePage & 0x7f8000) + ((uint32)value << 7);
			return true;

		case 0xD50D:
			thisptr->mKeyHolePage = (thisptr->mKeyHolePage & 0x007f80) + ((uint32)(value & 0x0f) << 15);
			return true;

		case 0xD50E:
			// bit 1 = CF power
			// bit 0 = CF reset unlock & alternate status

			thisptr->mbCFPower = (value & 2) != 0;

			if (!thisptr->mbCFPower)
				thisptr->mbCFReset = true;

			// 0 -> 1 transition on bit 0 when CF is powered blocks /RESET.
			if (thisptr->mbCFReset && (value & 1))
				thisptr->mbCFReset = false;

			thisptr->mbCFAltReg = !(value & 1);

			thisptr->UpdateIDEReset();
			break;

		case 0xD50F:
			value &= 0xfd;

			if (thisptr->mControl != value) {
				uint8 delta = thisptr->mControl ^ value;

				thisptr->mControl = value;

				if (delta & 0xc0) {
					switch(value & 0xc0) {
						case 0x00:
							thisptr->SetCartBank(thisptr->mLeftPage);
							break;
						case 0x40:
							thisptr->SetCartBank(thisptr->mLeftPage + 0x100);
							break;
						case 0x80:
							thisptr->SetCartBank(thisptr->mLeftPage + 0x200);
							break;
						case 0xc0:
							thisptr->SetCartBank(-1);
							break;
					}
				}

				if (delta & 0x30) {
					switch(value & 0x30) {
						case 0x00:
							thisptr->SetCartBank2(thisptr->mRightPage);
							break;
						case 0x10:
							thisptr->SetCartBank2(thisptr->mRightPage + 0x100);
							break;
						case 0x20:
							thisptr->SetCartBank2(thisptr->mRightPage + 0x200);
							break;
						case 0x30:
							thisptr->SetCartBank2(-1);
							break;
					}
				}
			}
			return true;
	}

	return false;
}

sint32 ATMyIDEEmulator::ReadByte_Cart_V2(void *thisptr0, uint32 address) {
	ATMyIDEEmulator *thisptr = (ATMyIDEEmulator *)thisptr0;

	// A tricky part here: it's possible that both left and right banks are pointing to the same
	// bank and therefore we may have to remap BOTH banks on a flash state change. Fortunately,
	// we don't have to worry about the keyhole as we always use memory routines for that. We
	// are a bit lazy here and just turn off the read layer for the window that was hit, and turn
	// of the other window if/when that one gets hit too.

	uint8 data;

	if (address < 0xA000) {
		if (thisptr->mFlash.ReadByte(address - 0x8000 + (thisptr->mCartBank2 << 13), data)) {
			thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerLeftCartFlash, kATMemoryAccessMode_CPURead, false);
			thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerLeftCartFlash, kATMemoryAccessMode_AnticRead, false);
		}
	} else {
		if (thisptr->mFlash.ReadByte(address - 0xA000 + (thisptr->mCartBank << 13), data)) {
			thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerRightCartFlash, kATMemoryAccessMode_CPURead, false);
			thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerRightCartFlash, kATMemoryAccessMode_AnticRead, false);
		}
	}

	return data;
}

bool ATMyIDEEmulator::WriteByte_Cart_V2(void *thisptr0, uint32 address, uint8 value) {
	ATMyIDEEmulator *thisptr = (ATMyIDEEmulator *)thisptr0;

	// A tricky part here: it's possible that both left and right banks are pointing to the same
	// bank and therefore we may have to remap BOTH banks on a flash state change. Fortunately,
	// we don't have to worry about the keyhole as we always use memory routines for that. Unlike
	// the read routine, we cannot ignore it here as it may involve turning ON the read mapping.

	bool remap;

	if (address < 0xA000)
		remap = thisptr->mFlash.WriteByte(address - 0x8000 + (thisptr->mCartBank2 << 13), value);
	else
		remap = thisptr->mFlash.WriteByte(address - 0xA000 + (thisptr->mCartBank << 13), value);

	if (thisptr->mFlash.CheckForWriteActivity()) {
		if (thisptr->mpUIRenderer)
			thisptr->mpUIRenderer->SetFlashWriteActivity();
	}

	if (remap) {
		bool enabled = thisptr->mFlash.IsControlReadEnabled();

		if (!(thisptr->mCartBank & 0xf00)) {
			thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerLeftCartFlash, kATMemoryAccessMode_CPURead, enabled);
			thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerLeftCartFlash, kATMemoryAccessMode_AnticRead, enabled);
		}

		if (!(thisptr->mCartBank2 & 0xf00)) {
			thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerRightCartFlash, kATMemoryAccessMode_CPURead, enabled);
			thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerRightCartFlash, kATMemoryAccessMode_AnticRead, enabled);
		}
	}

	return true;
}

void ATMyIDEEmulator::UpdateIDEReset() {
	if (mpIDE)
		mpIDE->SetReset(!mbCFPower || mbCFReset);
}

void ATMyIDEEmulator::SetCartBank(int bank) {
	if (mCartBank == bank)
		return;

	mCartBank = bank;
	UpdateCartBank();
}

void ATMyIDEEmulator::SetCartBank2(int bank) {
	if (mCartBank2 == bank)
		return;

	mCartBank2 = bank;
	UpdateCartBank2();
}

void ATMyIDEEmulator::UpdateCartBank() {
	mpSim->UpdateXLCartridgeLine();

	const bool flashControlRead = !(mCartBank & 0xf00) && mFlash.IsControlReadEnabled();
	mpMemMan->EnableLayer(mpMemLayerLeftCartFlash, kATMemoryAccessMode_CPURead, flashControlRead);
	mpMemMan->EnableLayer(mpMemLayerLeftCartFlash, kATMemoryAccessMode_AnticRead, flashControlRead);

	switch(mCartBank & 0xf00) {
		case 0x000:
			mpMemMan->SetLayerMemory(mpMemLayerLeftCart, mFirmware + (mCartBank << 13), 0xA0, 0x20, (uint32)-1, true);
			mpMemMan->EnableLayer(mpMemLayerLeftCart, true);
			mpMemMan->EnableLayer(mpMemLayerLeftCartFlash, kATMemoryAccessMode_CPUWrite, true);
			break;

		case 0x100:
			mpMemMan->SetLayerMemory(mpMemLayerLeftCart, mRAM + ((mCartBank - 0x100) << 13), 0xA0, 0x20, (uint32)-1, false);
			mpMemMan->EnableLayer(mpMemLayerLeftCart, true);
			mpMemMan->EnableLayer(mpMemLayerLeftCartFlash, kATMemoryAccessMode_CPUWrite, false);
			break;

		case 0x200:
			mpMemMan->SetLayerMemory(mpMemLayerLeftCart, mRAM + ((mCartBank - 0x200) << 13), 0xA0, 0x20, (uint32)-1, true);
			mpMemMan->EnableLayer(mpMemLayerLeftCart, true);
			mpMemMan->EnableLayer(mpMemLayerLeftCartFlash, kATMemoryAccessMode_CPUWrite, false);
			break;

		default:
			mpMemMan->EnableLayer(mpMemLayerLeftCart, false);
			mpMemMan->EnableLayer(mpMemLayerLeftCartFlash, kATMemoryAccessMode_CPUWrite, false);
			break;
	}
}

void ATMyIDEEmulator::UpdateCartBank2() {
	const bool flashControlRead = !(mCartBank2 & 0xf00) && mFlash.IsControlReadEnabled();
	mpMemMan->EnableLayer(mpMemLayerRightCartFlash, kATMemoryAccessMode_CPURead, flashControlRead);
	mpMemMan->EnableLayer(mpMemLayerRightCartFlash, kATMemoryAccessMode_AnticRead, flashControlRead);

	switch(mCartBank2 & 0xf00) {
		case 0x000:
			mpMemMan->SetLayerMemory(mpMemLayerRightCart, mFirmware + (mCartBank2 << 13), 0x80, 0x20, (uint32)-1, true);
			mpMemMan->EnableLayer(mpMemLayerRightCart, true);
			mpMemMan->EnableLayer(mpMemLayerRightCartFlash, kATMemoryAccessMode_CPUWrite, true);
			break;

		case 0x100:
			mpMemMan->SetLayerMemory(mpMemLayerRightCart, mRAM + ((mCartBank2 - 0x100) << 13), 0x80, 0x20, (uint32)-1, false);
			mpMemMan->EnableLayer(mpMemLayerRightCart, true);
			mpMemMan->EnableLayer(mpMemLayerRightCartFlash, kATMemoryAccessMode_CPUWrite, false);
			break;

		case 0x200:
			mpMemMan->SetLayerMemory(mpMemLayerRightCart, mRAM + ((mCartBank2 - 0x200) << 13), 0x80, 0x20, (uint32)-1, true);
			mpMemMan->EnableLayer(mpMemLayerRightCart, true);
			mpMemMan->EnableLayer(mpMemLayerRightCartFlash, kATMemoryAccessMode_CPUWrite, false);
			break;

		default:
			mpMemMan->EnableLayer(mpMemLayerRightCart, false);
			mpMemMan->EnableLayer(mpMemLayerRightCartFlash, kATMemoryAccessMode_CPUWrite, false);
			break;
	}
}
