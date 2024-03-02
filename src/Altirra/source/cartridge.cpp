//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008-2009 Avery Lee
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
//	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

#include "stdafx.h"
#include <vd2/system/binary.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include "cartridge.h"
#include "savestate.h"
#include "oshelper.h"
#include "resource.h"
#include "simulator.h"
#include "uirender.h"

int ATGetCartridgeModeForMapper(int mapper) {
	switch(mapper) {
		case 1 : return kATCartridgeMode_8K;
		case 2 : return kATCartridgeMode_16K;
		case 3 : return kATCartridgeMode_OSS_034M;
		case 4 : return kATCartridgeMode_5200_32K;
		case 5 : return kATCartridgeMode_DB_32K;
		case 6 : return kATCartridgeMode_5200_16K_TwoChip;
		case 7 : return kATCartridgeMode_BountyBob5200;
		case 8 : return kATCartridgeMode_Williams_64K;
		case 9 : return kATCartridgeMode_Express_64K;
		case 10: return kATCartridgeMode_Diamond_64K;
		case 11: return kATCartridgeMode_SpartaDosX_64K;
		case 12: return kATCartridgeMode_XEGS_32K;
		case 13: return kATCartridgeMode_XEGS_64K;
		case 14: return kATCartridgeMode_XEGS_128K;
		case 15: return kATCartridgeMode_OSS_M091;
		case 16: return kATCartridgeMode_5200_16K_OneChip;
		case 17: return kATCartridgeMode_Atrax_128K;
		case 18: return kATCartridgeMode_BountyBob800;
		case 19: return kATCartridgeMode_5200_8K;
		case 20: return kATCartridgeMode_5200_4K;
		case 21: return kATCartridgeMode_RightSlot_8K;
		case 22: return kATCartridgeMode_Williams_32K;
		case 23: return kATCartridgeMode_XEGS_256K;
		case 24: return kATCartridgeMode_XEGS_512K;
		case 25: return kATCartridgeMode_XEGS_1M;
		case 26: return kATCartridgeMode_MegaCart_16K;
		case 27: return kATCartridgeMode_MegaCart_32K;
		case 28: return kATCartridgeMode_MegaCart_64K;
		case 29: return kATCartridgeMode_MegaCart_128K;
		case 30: return kATCartridgeMode_MegaCart_256K;
		case 31: return kATCartridgeMode_MegaCart_512K;
		case 32: return kATCartridgeMode_MegaCart_1M;
		case 33: return kATCartridgeMode_Switchable_XEGS_32K;
		case 34: return kATCartridgeMode_Switchable_XEGS_64K;
		case 35: return kATCartridgeMode_Switchable_XEGS_128K;
		case 36: return kATCartridgeMode_Switchable_XEGS_256K;
		case 37: return kATCartridgeMode_Switchable_XEGS_512K;
		case 38: return kATCartridgeMode_Switchable_XEGS_1M;
		case 39: return kATCartridgeMode_Phoenix_8K;
		case 40: return kATCartridgeMode_Blizzard_16K;
		case 41: return kATCartridgeMode_MaxFlash_128K;
		case 42: return kATCartridgeMode_MaxFlash_1024K;
		case 43: return kATCartridgeMode_SpartaDosX_128K;
		default:
			return kATCartridgeMode_None;
	}
}

int ATGetCartridgeMapperForMode(int mode) {
	switch(mode) {
		case kATCartridgeMode_8K: return 1;
		case kATCartridgeMode_16K: return 2;
		case kATCartridgeMode_OSS_034M: return 3;
		case kATCartridgeMode_5200_32K: return 4;
		case kATCartridgeMode_DB_32K: return 5;
		case kATCartridgeMode_5200_16K_TwoChip: return 6;
		case kATCartridgeMode_BountyBob5200: return 7;
		case kATCartridgeMode_Williams_64K: return 8;
		case kATCartridgeMode_Express_64K: return 9;
		case kATCartridgeMode_Diamond_64K: return 10;
		case kATCartridgeMode_SpartaDosX_64K: return 11;
		case kATCartridgeMode_XEGS_32K: return 12;
		case kATCartridgeMode_XEGS_64K: return 13;
		case kATCartridgeMode_XEGS_128K: return 14;
		case kATCartridgeMode_OSS_M091: return 15;
		case kATCartridgeMode_5200_16K_OneChip: return 16;
		case kATCartridgeMode_Atrax_128K: return 17;
		case kATCartridgeMode_BountyBob800: return 18;
		case kATCartridgeMode_5200_8K: return 19;
		case kATCartridgeMode_5200_4K: return 20;
		case kATCartridgeMode_RightSlot_8K: return 21;
		case kATCartridgeMode_Williams_32K: return 22;
		case kATCartridgeMode_XEGS_256K: return 23;
		case kATCartridgeMode_XEGS_512K: return 24;
		case kATCartridgeMode_XEGS_1M: return 25;
		case kATCartridgeMode_MegaCart_16K: return 26;
		case kATCartridgeMode_MegaCart_32K: return 27;
		case kATCartridgeMode_MegaCart_64K: return 28;
		case kATCartridgeMode_MegaCart_128K: return 29;
		case kATCartridgeMode_MegaCart_256K: return 30;
		case kATCartridgeMode_MegaCart_512K: return 31;
		case kATCartridgeMode_MegaCart_1M: return 32;
		case kATCartridgeMode_Switchable_XEGS_32K: return 33;
		case kATCartridgeMode_Switchable_XEGS_64K: return 34;
		case kATCartridgeMode_Switchable_XEGS_128K: return 35;
		case kATCartridgeMode_Switchable_XEGS_256K: return 36;
		case kATCartridgeMode_Switchable_XEGS_512K: return 37;
		case kATCartridgeMode_Switchable_XEGS_1M: return 38;
		case kATCartridgeMode_Phoenix_8K: return 39;
		case kATCartridgeMode_Blizzard_16K: return 40;
		case kATCartridgeMode_MaxFlash_128K: return 41;
		case kATCartridgeMode_MaxFlash_1024K: return 42;
		case kATCartridgeMode_SpartaDosX_128K: return 43;
		default:
			return 0;
	}
}

bool ATIsCartridgeModeHWCompatible(ATCartridgeMode cartmode, int hwmode) {
	bool cartIs5200 = false;

	switch(cartmode) {
		case kATCartridgeMode_5200_32K:
		case kATCartridgeMode_5200_16K_TwoChip:
		case kATCartridgeMode_5200_16K_OneChip:
		case kATCartridgeMode_5200_8K:
		case kATCartridgeMode_5200_4K:
			cartIs5200 = true;
			break;
	}

	bool modeIs5200 = (hwmode == kATHardwareMode_5200);

	return cartIs5200 == modeIs5200;
}

///////////////////////////////////////////////////////////////////////////

ATCartridgeEmulator::ATCartridgeEmulator()
	: mCartMode(kATCartridgeMode_None)
	, mCartBank(-1)
	, mCartBank2(-1)
	, mInitialCartBank(-1)
	, mInitialCartBank2(-1)
	, mbDirty(false)
	, mpUIRenderer(NULL)
	, mpCB(NULL)
	, mpMemMan(NULL)
	, mpMemLayerFixedBank1(NULL)
	, mpMemLayerFixedBank2(NULL)
	, mpMemLayerVarBank1(NULL)
	, mpMemLayerVarBank2(NULL)
	, mpMemLayerSpec1(NULL)
	, mpMemLayerSpec2(NULL)
	, mpMemLayerControl(NULL)
{
}

ATCartridgeEmulator::~ATCartridgeEmulator() {
	Shutdown();
}

void ATCartridgeEmulator::Init(ATMemoryManager *memman, int basePri) {
	mpMemMan = memman;
	mBasePriority = basePri;
}

void ATCartridgeEmulator::Shutdown() {
	ShutdownMemoryLayers();
	mpMemMan = NULL;
}

void ATCartridgeEmulator::SetUIRenderer(IATUIRenderer *r) {
	mpUIRenderer = r;
}

bool ATCartridgeEmulator::IsABxxMapped() const {
	if (mCartMode == kATCartridgeMode_SIC)
		return !(mCartBank & 0x40);

	return mCartMode && mCartBank >= 0;
}

bool ATCartridgeEmulator::IsBASICDisableAllowed() const {
	switch(mCartMode) {
		case kATCartridgeMode_MaxFlash_128K:
		case kATCartridgeMode_MaxFlash_1024K:
			return false;
	}

	return true;
}

void ATCartridgeEmulator::LoadSuperCharger3D() {
	mCartMode = kATCartridgeMode_SuperCharger3D;
	mCartBank = -1;
	mInitialCartBank = -1;
	mImagePath.clear();
	vdfastvector<uint8>().swap(mCARTROM);
	vdfastvector<uint8>().swap(mCARTRAM);
	mbDirty = false;

	InitMemoryLayers();
}

void ATCartridgeEmulator::Load5200Default() {
	mCartMode = kATCartridgeMode_5200_4K;
	mCartBank = 0;
	mInitialCartBank = 0;
	mImagePath.clear();
	vdfastvector<uint8>().swap(mCARTROM);
	vdfastvector<uint8>().swap(mCARTRAM);
	mCARTROM.resize(0x1000, 0xFF);
	ATLoadKernelResource(IDR_NOCARTRIDGE, mCARTROM.data(), 0, 4096);
	mbDirty = false;

	InitMemoryLayers();
}

void ATCartridgeEmulator::LoadFlash1Mb(bool altbank) {
	mCartMode = altbank ? kATCartridgeMode_MaxFlash_128K_MyIDE : kATCartridgeMode_MaxFlash_128K;
	mCartBank = 0;
	mInitialCartBank = 0;
	mInitialCartBank2 = -1;
	mFlashReadMode = kFlashReadMode_Normal;
	mImagePath.clear();
	vdfastvector<uint8>().swap(mCARTROM);
	vdfastvector<uint8>().swap(mCARTRAM);
	mCARTROM.resize(0x20000, 0xFF);
	mbDirty = false;

	InitMemoryLayers();
}

void ATCartridgeEmulator::LoadFlash8Mb() {
	mCartMode = kATCartridgeMode_MaxFlash_1024K;
	mCartBank = 127;
	mInitialCartBank = 127;
	mInitialCartBank2 = -1;
	mFlashReadMode = kFlashReadMode_Normal;
	mImagePath.clear();
	vdfastvector<uint8>().swap(mCARTROM);
	vdfastvector<uint8>().swap(mCARTRAM);
	mCARTROM.resize(0x100000, 0xFF);
	mbDirty = false;

	InitMemoryLayers();
}

void ATCartridgeEmulator::LoadFlashSIC() {
	mCartMode = kATCartridgeMode_SIC;
	mCartBank = 0;
	mInitialCartBank = 0;
	mInitialCartBank2 = -1;
	mFlashReadMode = kFlashReadMode_Normal;
	mImagePath.clear();
	vdfastvector<uint8>().swap(mCARTROM);
	vdfastvector<uint8>().swap(mCARTRAM);
	mCARTROM.resize(0x80000, 0xFF);
	mbDirty = false;

	InitMemoryLayers();
}

bool ATCartridgeEmulator::Load(const wchar_t *s, ATCartLoadContext *loadCtx) {
	VDFileStream f(s);

	return Load(s, s, f, loadCtx);
}

bool ATCartridgeEmulator::Load(const wchar_t *origPath, const wchar_t *imagePath, IVDRandomAccessStream& f, ATCartLoadContext *loadCtx) {
	sint64 size = f.Length();

	if (size < 1024 || size > 1048576 + 16 + 8192)
		throw MyError("Unsupported cartridge size.");

	// check for header
	char buf[16];
	f.Read(buf, 16);

	bool validHeader = false;
	uint32 size32 = (uint32)size;

	if (!memcmp(buf, "CART", 4)) {
		uint32 type = VDReadUnalignedBEU32(buf + 4);
		uint32 checksum = VDReadUnalignedBEU32(buf + 8);

		size32 -= 16;
		mCARTROM.resize(size32);
		f.Read(mCARTROM.data(), size32);

		uint32 csum = 0;
		for(uint32 i=0; i<size32; ++i)
			csum += mCARTROM[i];

		if (csum == checksum) {
			validHeader = true;

			int mode = ATGetCartridgeModeForMapper(type);

			if (!mode)
				throw MyError("The selected cartridge cannot be loaded as it uses unsupported mapper mode %d.", type);

			mCartMode = (ATCartridgeMode)mode;
		}
	}

	if (loadCtx)
		loadCtx->mCartSize = size32;

	if (!validHeader) {
		if (loadCtx && loadCtx->mbReturnOnUnknownMapper) {
			loadCtx->mLoadStatus = kATCartLoadStatus_UnknownMapper;
			loadCtx->mbMayBe2600 = false;

			// Check if we see what looks like NMI, RESET, and IRQ handler addresses
			// in the Fxxx range. That highly likely indicates a 2600 cartridge.
			if (size32 == 2048 || size32 == 4096) {
				vdfastvector<uint8> data(size32);

				try {
					f.Seek(0);
					f.Read(data.data(), size32);

					const uint8 *tail = data.data() + size32 - 6;

					if (tail[1] >= 0xF0 && tail[3] >= 0xF0 && tail[5] >= 0xF0)
						loadCtx->mbMayBe2600 = true;
				} catch(const MyError&) {}
			}

			return false;
		}

		mCARTROM.resize(size32);
		f.Seek(0);
		f.Read(mCARTROM.data(), size32);

		if (loadCtx && loadCtx->mCartMapper) {
			mCartMode = (ATCartridgeMode)loadCtx->mCartMapper;
		} else {
			if (size32 <= 8192)
				mCartMode = kATCartridgeMode_8K;
			else if (size32 == 16384)
				mCartMode = kATCartridgeMode_16K;
			else if (size32 == 0x8000)
				mCartMode = kATCartridgeMode_XEGS_32K;
			else if (size32 == 0xA000)
				mCartMode = kATCartridgeMode_BountyBob800;
			else if (size32 == 0x10000)
				mCartMode = kATCartridgeMode_XEGS_64K;
			else if (size32 == 131072)
				mCartMode = kATCartridgeMode_MaxFlash_128K;
			else if (size32 == 524288)
				mCartMode = kATCartridgeMode_MegaCart_512K;
			else if (size32 == 1048576)
				mCartMode = kATCartridgeMode_MaxFlash_1024K;
			else
				throw MyError("Unsupported cartridge size.");
		}
	}

	uint32 allocSize = size32;

	// set initial bank and alloc size
	switch(mCartMode) {
		case kATCartridgeMode_5200_4K:
			mInitialCartBank = 0;
			allocSize = 4096;
			break;

		case kATCartridgeMode_8K:
		case kATCartridgeMode_5200_8K:
		case kATCartridgeMode_RightSlot_8K:
			mInitialCartBank = 0;
			allocSize = 8192;
			break;

		case kATCartridgeMode_TelelinkII:
			mInitialCartBank = 0;
			allocSize = 8192 + 256;
			break;

		case kATCartridgeMode_16K:
		case kATCartridgeMode_5200_16K_TwoChip:
		case kATCartridgeMode_5200_16K_OneChip:
			mInitialCartBank = 0;
			allocSize = 16384;
			break;

		case kATCartridgeMode_5200_32K:
			mInitialCartBank = 0;
			allocSize = 32768;
			break;

		case kATCartridgeMode_XEGS_32K:
		case kATCartridgeMode_Switchable_XEGS_32K:
			mInitialCartBank = 3;
			allocSize = 32768;
			break;

		case kATCartridgeMode_XEGS_64K:
		case kATCartridgeMode_Switchable_XEGS_64K:
			mInitialCartBank = 7;
			allocSize = 0x10000;
			break;

		case kATCartridgeMode_XEGS_128K:
		case kATCartridgeMode_Switchable_XEGS_128K:
			mInitialCartBank = 15;
			allocSize = 0x20000;
			break;

		case kATCartridgeMode_XEGS_256K:
		case kATCartridgeMode_Switchable_XEGS_256K:
			mInitialCartBank = 31;
			allocSize = 0x40000;
			break;

		case kATCartridgeMode_XEGS_512K:
		case kATCartridgeMode_Switchable_XEGS_512K:
			mInitialCartBank = 63;
			allocSize = 0x80000;
			break;

		case kATCartridgeMode_XEGS_1M:
		case kATCartridgeMode_Switchable_XEGS_1M:
			mInitialCartBank = 127;
			allocSize = 0x100000;
			break;

		case kATCartridgeMode_MaxFlash_128K:
		case kATCartridgeMode_MaxFlash_128K_MyIDE:
			mInitialCartBank = 0;
			allocSize = 0x20000;
			break;

		case kATCartridgeMode_MegaCart_16K:
			mInitialCartBank = 0;
			allocSize = 0x4000;
			break;

		case kATCartridgeMode_MegaCart_32K:
			mInitialCartBank = 0;
			allocSize = 0x8000;
			break;

		case kATCartridgeMode_MegaCart_64K:
			mInitialCartBank = 0;
			allocSize = 0x10000;
			break;

		case kATCartridgeMode_MegaCart_128K:
			mInitialCartBank = 0;
			allocSize = 0x20000;
			break;

		case kATCartridgeMode_MegaCart_256K:
			mInitialCartBank = 0;
			allocSize = 0x40000;
			break;

		case kATCartridgeMode_MegaCart_512K:
			mInitialCartBank = 0;
			allocSize = 0x80000;
			break;

		case kATCartridgeMode_MegaCart_1M:
			mInitialCartBank = 0;
			allocSize = 0x100000;
			break;

		case kATCartridgeMode_MaxFlash_1024K:
			mInitialCartBank = 127;
			allocSize = 1048576;
			break;

		case kATCartridgeMode_BountyBob800:
		case kATCartridgeMode_BountyBob5200:
			mInitialCartBank = 0;
			mInitialCartBank2 = 0;
			allocSize = 40960;
			break;

		case kATCartridgeMode_OSS_034M:
			mInitialCartBank = 2;
			mInitialCartBank2 = 3;
			allocSize = 16384;
			break;

		case kATCartridgeMode_OSS_M091:
			mInitialCartBank = 3;
			mInitialCartBank2 = 0;
			allocSize = 16384;
			break;

		case kATCartridgeMode_Corina_1M_EEPROM:
			mInitialCartBank = 0;
			allocSize = 1048576 + 8192;
			break;

		case kATCartridgeMode_Corina_512K_SRAM_EEPROM:
			mInitialCartBank = 0;
			allocSize = 524288 + 8192;
			break;

		case kATCartridgeMode_SpartaDosX_128K:
			mInitialCartBank = 0;
			allocSize = 131072;
			break;

		case kATCartridgeMode_Williams_64K:
		case kATCartridgeMode_Diamond_64K:
		case kATCartridgeMode_Express_64K:
		case kATCartridgeMode_SpartaDosX_64K:
			mInitialCartBank = 0;
			allocSize = 65536;
			break;

		case kATCartridgeMode_DB_32K:
			mInitialCartBank = 0;
			allocSize = 32768;
			break;

		case kATCartridgeMode_Atrax_128K:
			mInitialCartBank = 0;
			allocSize = 131072;
			break;

		case kATCartridgeMode_Williams_32K:
			mInitialCartBank = 0;
			allocSize = 32768;
			break;

		case kATCartridgeMode_Phoenix_8K:
			mInitialCartBank = 0;
			allocSize = 8192;
			break;

		case kATCartridgeMode_Blizzard_16K:
			mInitialCartBank = 0;
			allocSize = 16384;
			break;

		case kATCartridgeMode_SIC:
			mInitialCartBank = 0;
			allocSize = 0x80000;
			break;
	}

	mCartBank = mInitialCartBank;
	mCartBank2 = mInitialCartBank2;

	if (mCartMode == kATCartridgeMode_TelelinkII)
		mCARTROM.resize(allocSize, 0xFF);
	else
		mCARTROM.resize(allocSize, 0);

	if (mCartMode == kATCartridgeMode_8K) {
		// For the 8K cart, we have a special case if the ROM is 2K or 4K -- in that case,
		// we mirror the existing ROM to fit.
		uint8 *p = mCARTROM.data();

		if (size32 == 2048) {
			for(int i=0; i<3; ++i)
				memcpy(p + 2048*(i + 1), p, 2048);
		} else if (size32 == 4096) {
			memcpy(p + 4096, p, 4096);
		}
	} else if (mCartMode == kATCartridgeMode_SIC) {
		uint8 *p = mCARTROM.data();

		if (size32 == 0x20000)
			memcpy(p + 0x20000, p, 0x20000);

		if (size32 == 0x20000 || size32 == 0x40000)
			memcpy(p + 0x40000, p, 0x40000);
	}

	if (mCartMode == kATCartridgeMode_Corina_512K_SRAM_EEPROM) {
		mCARTRAM.resize(524288, 0);
	} else if (mCartMode == kATCartridgeMode_TelelinkII) {
		mCARTRAM.clear();
		mCARTRAM.resize(256, 0xFF);
	}

	mImagePath = origPath;

	mCommandPhase = 0;
	mFlashReadMode = kFlashReadMode_Normal;

	if (loadCtx) {
		loadCtx->mCartMapper = mCartMode;
		loadCtx->mLoadStatus = kATCartLoadStatus_Ok;
	}

	mbDirty = false;
	InitMemoryLayers();

	return true;
}

void ATCartridgeEmulator::Unload() {
	ShutdownMemoryLayers();

	mInitialCartBank = 0;
	mCartBank = 0;
	mCartMode = kATCartridgeMode_None;

	mImagePath.clear();
	mbDirty = false;
}

void ATCartridgeEmulator::Save(const wchar_t *fn, bool includeHeader) {
	VDFile f(fn, nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);

	// write header
	uint32 size = mCARTROM.size();

	if (includeHeader) {
		char header[16] = { 'C', 'A', 'R', 'T' };

		int type = ATGetCartridgeMapperForMode(mCartMode);

		VDWriteUnalignedBEU32(header + 4, type);

		uint32 checksum = 0;
		for(uint32 i=0; i<size; ++i)
			checksum += mCARTROM[i];

		VDWriteUnalignedBEU32(header + 8, checksum);

		f.write(header, 16);
	}

	f.write(mCARTROM.data(), size);
	mbDirty = false;
}

void ATCartridgeEmulator::ColdReset() {
	mCartBank = mInitialCartBank;
	mCartBank2 = mInitialCartBank2;
	UpdateCartBank();

	if (mpMemLayerVarBank2)
		UpdateCartBank2();

	mCommandPhase = 0;
	mFlashReadMode = kFlashReadMode_Normal;
}

sint32 ATCartridgeEmulator::ReadByte_BB5200_1(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;
	uint32 index = address - 0x4FF6;

	if (index < 4) {
		uint8 data = thisptr->mCARTROM[(thisptr->mCartBank << 12) + 0x0FF6 + index];
		thisptr->SetCartBank(index);
		return data;
	}

	return -1;
}

sint32 ATCartridgeEmulator::ReadByte_BB5200_2(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;
	uint32 index = address - 0x5FF6;

	if (index < 4) {
		uint8 data = thisptr->mCARTROM[(thisptr->mCartBank2 << 12) + 0x4FF6 + index];
		thisptr->SetCartBank2(index);
		return data;
	}

	return -1;
}

bool ATCartridgeEmulator::WriteByte_BB5200_1(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;
	uint32 index = address - 0x4FF6;

	if (index < 4)
		thisptr->SetCartBank(index);

	return true;
}

bool ATCartridgeEmulator::WriteByte_BB5200_2(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;
	uint32 index = address - 0x5FF6;

	if (index < 4)
		thisptr->SetCartBank2(index);

	return true;
}

sint32 ATCartridgeEmulator::ReadByte_BB800_1(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;
	uint32 index = address - 0x8FF6;

	if (index < 4) {
		uint8 data = thisptr->mCARTROM[(thisptr->mCartBank << 12) + 0x0FF6 + index];
		thisptr->SetCartBank(index);
		return data;
	}

	return -1;
}

sint32 ATCartridgeEmulator::ReadByte_BB800_2(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;
	uint32 index = address - 0x9FF6;

	if (index < 4) {
		uint8 data = thisptr->mCARTROM[(thisptr->mCartBank2 << 12) + 0x4FF6 + index];
		thisptr->SetCartBank2(index);
		return data;
	}

	return -1;
}

bool ATCartridgeEmulator::WriteByte_BB800_1(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	uint32 index = address - 0x8FF6;

	if (index < 4)
		thisptr->SetCartBank(index);

	return true;
}

bool ATCartridgeEmulator::WriteByte_BB800_2(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	uint32 index = address - 0x9FF6;

	if (index < 4)
		thisptr->SetCartBank2(index);

	return true;
}

sint32 ATCartridgeEmulator::ReadByte_MaxFlash(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	switch(thisptr->mFlashReadMode) {
		case kFlashReadMode_Normal:
			VDASSERT(false);
			break;

		case kFlashReadMode_Autoselect:
			switch(address & 0xFF) {
				case 0x00:
					return 0x01;	// XX00 Manufacturer ID: AMD

				case 0x01:
					if (thisptr->mCartMode == kATCartridgeMode_MaxFlash_128K)
						return 0x21;	// XX01 Device ID: Am29F010 128K x 8-bit flash
					else
						return 0xA4;	// XX01 Device ID: Am29F040B 512K x 8-bit flash or ??? 1M x 8-bit

				default:
					return 0x00;	// XX02 Sector Protect Verify: 00 not protected
			}
			break;

		case kFlashReadMode_WriteStatus:
			return 0xFF;	// operation complete
	}

	return 0xFF;
}

bool ATCartridgeEmulator::WriteByte_MaxFlash(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;
	uint32 fullAddr = (thisptr->mCartBank & 0x1f) * 0x4000 + (address & 0x3fff);
	uint32 fullAddr16 = fullAddr & 0xffff;

	switch(thisptr->mCommandPhase) {
		case 0:
			// $F0 written at phase 0 deactivates autoselect mode
			if (value == 0xF0) {
				if (!thisptr->mFlashReadMode)
					break;

				thisptr->mFlashReadMode = kFlashReadMode_Normal;
				thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, false);
				break;
			}

			if (fullAddr16 == 0x5555 && value == 0xAA)
				thisptr->mCommandPhase = 1;
			break;

		case 1:
			if (fullAddr16 == 0x2AAA && value == 0x55)
				thisptr->mCommandPhase = 2;
			else {
				thisptr->mCommandPhase = 0;
			}
			break;

		case 2:
			if (fullAddr16 != 0x5555) {
				thisptr->mCommandPhase = 0;
				break;
			}

			switch(value) {
				case 0x80:	// $80: sector or chip erase
					thisptr->mCommandPhase = 3;
					break;

				case 0x90:	// $90: autoselect mode
					thisptr->mFlashReadMode = kFlashReadMode_Autoselect;
					thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, true);
					thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_CPURead, true);
					thisptr->mCommandPhase = 0;
					break;

				case 0xA0:	// $A0: program mode
					thisptr->mCommandPhase = 6;
					break;

				case 0xF0:	// $F0: reset
					thisptr->mCommandPhase = 0;
					thisptr->mFlashReadMode = kFlashReadMode_Normal;
					thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, false);
					thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_CPURead, false);
					break;

				default:
					thisptr->mCommandPhase = 0;
					break;
			}

			break;

		case 3:		// 5555[AA] 2AAA[55] 5555[80]
			if (fullAddr16 != 0x5555 || value != 0xAA) {
				thisptr->mCommandPhase = 0;
				break;
			}

			thisptr->mCommandPhase = 4;
			break;

		case 4:		// 5555[AA] 2AAA[55] 5555[80] 5555[AA]
			if (fullAddr16 != 0x2AAA || value != 0x55) {
				thisptr->mCommandPhase = 0;
				break;
			}

			thisptr->mCommandPhase = 5;
			break;

		case 5:		// 5555[AA] 2AAA[55] 5555[80] 5555[AA] 2AAA[55]
			if (fullAddr16 == 0x5555 && value == 0x10) {
				// full chip erase
				memset(thisptr->mCARTROM.data(), 0xFF, thisptr->mCARTROM.size());
			} else if (value == 0x30) {
				// sector erase
				if (thisptr->mCartMode == kATCartridgeMode_SIC) {
					fullAddr &= 0x70000;
					memset(thisptr->mCARTROM.data() + fullAddr, 0xFF, 0x10000);
				} else if (thisptr->mCartMode == kATCartridgeMode_MaxFlash_1024K) {
					fullAddr &= 0xF0000;
					memset(thisptr->mCARTROM.data() + fullAddr, 0xFF, 0x10000);
				} else {
					fullAddr &= 0x1C000;
					memset(thisptr->mCARTROM.data() + fullAddr, 0xFF, 0x4000);
				}
			}

			thisptr->mbDirty = true;
			if (thisptr->mpUIRenderer)
				thisptr->mpUIRenderer->SetFlashWriteActivity();

			thisptr->mCommandPhase = 0;
			//mFlashReadMode = kFlashReadMode_WriteStatus;
			thisptr->mFlashReadMode = kFlashReadMode_Normal;
			thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, false);
			thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_CPURead, false);
			break;

		case 6:		// 5555[AA] 2AAA[55] 5555[A0]
			thisptr->mCARTROM[fullAddr] &= value;
			thisptr->mbDirty = true;
			if (thisptr->mpUIRenderer)
				thisptr->mpUIRenderer->SetFlashWriteActivity();

			thisptr->mCommandPhase = 0;
			//mFlashReadMode = kFlashReadMode_WriteStatus;
			thisptr->mFlashReadMode = kFlashReadMode_Normal;
			thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, false);
			thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_CPURead, false);
			break;
	}

	return true;
}

bool ATCartridgeEmulator::WriteByte_Corina1M(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	// We don't emulate write times at the moment.
	if (thisptr->mCartBank == 64) {
		thisptr->mbDirty = true;
		if (thisptr->mpUIRenderer)
			thisptr->mpUIRenderer->SetFlashWriteActivity();

		thisptr->mCARTROM[0x100000 + (address & 0x1fff)] = value;
	}

	return true;
}

bool ATCartridgeEmulator::WriteByte_Corina512K(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	// We don't emulate write times at the moment.
	if (thisptr->mCartBank == 64) {
		thisptr->mbDirty = true;
		if (thisptr->mpUIRenderer)
			thisptr->mpUIRenderer->SetFlashWriteActivity();

		thisptr->mCARTROM[0x80000 + (address & 0x1fff)] = value;
	}

	return true;
}

bool ATCartridgeEmulator::WriteByte_TelelinkII(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->mCARTRAM[address & 0xFF] = value | 0xF0;

	return true;
}

bool ATCartridgeEmulator::WriteByte_CCTL_Phoenix(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->SetCartBank(-1);
	return true;
}

template<uint8 T_Mask>
bool ATCartridgeEmulator::WriteByte_CCTL_AddressToBank(void *thisptr0, uint32 address, uint8 value) {
	((ATCartridgeEmulator *)thisptr0)->SetCartBank(address & T_Mask);
	return true;
}

template<uint8 T_Mask>
bool ATCartridgeEmulator::WriteByte_CCTL_DataToBank(void *thisptr0, uint32 address, uint8 value) {
	((ATCartridgeEmulator *)thisptr0)->SetCartBank(value & T_Mask);
	return true;
}

template<uint8 T_Mask>
bool ATCartridgeEmulator::WriteByte_CCTL_DataToBank_Switchable(void *thisptr0, uint32 address, uint8 value) {
	((ATCartridgeEmulator *)thisptr0)->SetCartBank(value & 0x80 ? -1 : value & T_Mask);
	return true;
}

template<uint8 T_Mask>
sint32 ATCartridgeEmulator::ReadByte_CCTL_Williams(void *thisptr0, uint32 address) {
	((ATCartridgeEmulator *)thisptr0)->SetCartBank(address & 8 ? -1 : address & T_Mask);
	return 0xFF;
}

template<uint8 T_Mask>
bool ATCartridgeEmulator::WriteByte_CCTL_Williams(void *thisptr0, uint32 address, uint8 value) {
	((ATCartridgeEmulator *)thisptr0)->SetCartBank(address & 8 ? -1 : address & T_Mask);
	return true;
}

template<uint8 T_Address>
sint32 ATCartridgeEmulator::ReadByte_CCTL_SDX64(void *thisptr0, uint32 address) {
	if (((uint8)address & 0xF0) == T_Address) {
		((ATCartridgeEmulator *)thisptr0)->SetCartBank(address & 8 ? -1 : ~address & 7);
		return 0xFF;
	}

	return -1;
}

template<uint8 T_Address>
bool ATCartridgeEmulator::WriteByte_CCTL_SDX64(void *thisptr0, uint32 address, uint8 value) {
	if (((uint8)address & 0xF0) == T_Address) {
		((ATCartridgeEmulator *)thisptr0)->SetCartBank(address & 8 ? -1 : ~address & 7);
		return true;
	}

	return false;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_SDX128(void *thisptr0, uint32 address) {
	if (((uint8)address & 0xE0) == 0xE0) {
		((ATCartridgeEmulator *)thisptr0)->SetCartBank(address & 8 ? -1 : (~address & 7) + (address & 0x10 ? 0 : 8));
		return 0xFF;
	}

	return -1;
}

bool ATCartridgeEmulator::WriteByte_CCTL_SDX128(void *thisptr0, uint32 address, uint8 value) {
	if (((uint8)address & 0xE0) == 0xE0) {
		((ATCartridgeEmulator *)thisptr0)->SetCartBank(address & 8 ? -1 : (~address & 7) + (address & 0x10 ? 0 : 8));
		return true;
	}

	return false;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_MaxFlash_128K(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	if (address < 0xD520) {
		if (address < 0xD510)
			thisptr->SetCartBank(address & 15);
		else
			thisptr->SetCartBank(-1);

		return 0xFF;
	}

	return -1;
}

bool ATCartridgeEmulator::WriteByte_CCTL_MaxFlash_128K(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	if (address < 0xD520) {
		if (address < 0xD510)
			thisptr->SetCartBank(address & 15);
		else
			thisptr->SetCartBank(-1);

		return true;
	}

	return false;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_MaxFlash_128K_MyIDE(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	if (address >= 0xD520 && address < 0xD540) {
		if (address & 0x10)
			thisptr->SetCartBank(-1);
		else
			thisptr->SetCartBank(address & 15);

		return 0xFF;
	}

	return -1;
}

bool ATCartridgeEmulator::WriteByte_CCTL_MaxFlash_128K_MyIDE(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	if (address >= 0xD520 && address < 0xD540) {
		if (address & 0x10)
			thisptr->SetCartBank(-1);
		else
			thisptr->SetCartBank(address & 15);

		return true;
	}

	return false;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_MaxFlash_1024K(void *thisptr0, uint32 address) {
	((ATCartridgeEmulator *)thisptr0)->SetCartBank(address & 0x80 ? -1 : (uint8)address & 0x7F);
	return 0xFF;
}

bool ATCartridgeEmulator::WriteByte_CCTL_MaxFlash_1024K(void *thisptr0, uint32 address, uint8 value) {
	((ATCartridgeEmulator *)thisptr0)->SetCartBank(address & 0x80 ? -1 : (uint8)address & 0x7F);
	return true;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_SIC(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;
	uint8 addr8 = (uint8)address;

	if (addr8 >= 0x20)
		return -1;

	return (uint8)thisptr->mCartBank;
}

bool ATCartridgeEmulator::WriteByte_CCTL_SIC(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	if ((uint8)address < 0x20)
		thisptr->SetCartBank(value);

	return true;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_SC3D(void *thisptr0, uint32 address) {
	return ((ATCartridgeEmulator *)thisptr0)->mSC3D[address & 3];
}

bool ATCartridgeEmulator::WriteByte_CCTL_SC3D(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	// Information on how the SuperCharger 3D cart works comes from jindroush, by way of
	// HiassofT:
	//
	//	0,1,2 are data regs, 3 is command/status.
	//
	//	Command 1, division:
	//	reg3 = 1
	//	reg1 (hi) reg2 (lo) / reg0 = reg2 (res), reg1 (remainder).
	//
	//	If there's error, status is 1, otherwise status is 0.
	//
	//	Command 2, multiplication:
	//	reg3 = 2
	//	reg2 * reg0 = reg1 (hi), reg2 (lo).

	int idx = (int)address & 3;

	if (idx < 3)
		thisptr->mSC3D[idx] = value;
	else {
		if (value == 1) {
			uint32 d = ((uint32)thisptr->mSC3D[1] << 8) + (uint32)thisptr->mSC3D[2];

			if (thisptr->mSC3D[1] >= thisptr->mSC3D[0]) {
				thisptr->mSC3D[3] = 1;
			} else {
				thisptr->mSC3D[2] = (uint8)(d / (uint32)thisptr->mSC3D[0]);
				thisptr->mSC3D[1] = (uint8)(d % (uint32)thisptr->mSC3D[0]);
				thisptr->mSC3D[3] = 0;
			}
		} else if (value == 2) {
			uint32 result = (uint32)thisptr->mSC3D[2] * (uint32)thisptr->mSC3D[0];

			thisptr->mSC3D[1] = (uint8)(result >> 8);
			thisptr->mSC3D[2] = (uint8)result;
			thisptr->mSC3D[3] = 0;
		} else {
			thisptr->mSC3D[3] = 1;
		}
	}

	return true;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_TelelinkII(void *thisptr0, uint32 address) {
	if (address & 1) {
		ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

		// initiate array load
		for(int i=0; i<256; ++i)
			thisptr->mCARTRAM[i] = thisptr->mCARTROM[8192 + i] | 0xF0;
	}

	return -1;
}

bool ATCartridgeEmulator::WriteByte_CCTL_TelelinkII(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	// initiate NV store
	memcpy(thisptr->mCARTROM.data() + 8192, thisptr->mCARTRAM.data(), 256);
	thisptr->mbDirty = true;
	if (thisptr->mpUIRenderer)
		thisptr->mpUIRenderer->SetFlashWriteActivity();

	return true;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_OSS_034M(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	address &= 15;

	static const sint8 kBankLookup[16] = {0, 0, 4, 2, 1, 1, 4, 1, -1, -1, -1, -1, -1, -1, -1, -1};
	thisptr->SetCartBank(kBankLookup[address]);
	thisptr->SetCartBank2(kBankLookup[address] >> 31);

	return -1;
}

bool ATCartridgeEmulator::WriteByte_CCTL_OSS_034M(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	address &= 15;

	static const sint8 kBankLookup[16] = {0, 0, 4, 2, 1, 1, 4, 1, -1, -1, -1, -1, -1, -1, -1, -1};
	thisptr->SetCartBank(kBankLookup[address]);
	thisptr->SetCartBank2(kBankLookup[address] >> 31);

	return true;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_OSS_M091(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	address &= 9;

	static const sint8 kBankLookup[16] = {1, 3, 1, 3, 1, 3, 1, 3, -1, 2};
	thisptr->SetCartBank(kBankLookup[address]);
	thisptr->SetCartBank2(kBankLookup[address] >> 31);

	return -1;
}

bool ATCartridgeEmulator::WriteByte_CCTL_OSS_M091(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	address &= 9;

	static const sint8 kBankLookup[16] = {1, 3, 1, 3, 1, 3, 1, 3, -1, 2};
	thisptr->SetCartBank(kBankLookup[address]);
	thisptr->SetCartBank2(kBankLookup[address] >> 31);

	return true;
}

bool ATCartridgeEmulator::WriteByte_CCTL_Corina(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	if (address != 0xD500)
		return false;

	int bank = -1;

	// D7=1 disables cartridge.
	// D7=0 enables cartridge.
	if (!(value & 0x80)) {
		switch(value & 0x60) {
			case 0x00:	// 000xxxxx -> ROM (banks 0-31)
			case 0x20:	// 001xxxxx -> ROM/SRAM (banks 32-63)
				bank = value;
				break;
			case 0x40:	// 10xxxxxx -> EEPROM
				bank = 64;
				break;
			case 0x60:	// 11xxxxxx -> reserved
				bank = -1;
				break;
		}
	}

	thisptr->SetCartBank(bank);
	return true;
}

void ATCartridgeEmulator::LoadState(ATSaveStateReader& reader) {
	ExchangeState(reader);
}

void ATCartridgeEmulator::SaveState(ATSaveStateWriter& writer) {
	ExchangeState(writer);
}

template<class T>
void ATCartridgeEmulator::ExchangeState(T& io) {
	io != mCartBank;
	io != mCartBank2;
}

void ATCartridgeEmulator::InitMemoryLayers() {
	uint32 fixedBase = 0;
	uint32 fixedSize = 0;
	uint32 fixedOffset = 0;
	uint32 fixedMask = 0;
	uint32 fixed2Base = 0;
	uint32 fixed2Size = 0;
	uint32 fixed2Offset = 0;
	int fixed2Mask = -1;
	bool fixed2RAM = false;
	uint32 bank1Base = 0;
	uint32 bank1Size = 0;
	uint32 bank2Base = 0;
	uint32 bank2Size = 0;
	uint32 spec1Base = 0;
	uint32 spec1Size = 0;
	bool spec1ReadEnabled = false;
	bool spec1WriteEnabled = false;
	bool spec2Enabled = false;
	uint32 spec2Base = 0;
	uint32 spec2Size = 0;
	bool usecctl = false;
	bool usecctlread = false;
	bool usecctlwrite = false;

	ATMemoryHandlerTable spec1hd = {};
	spec1hd.mbPassAnticReads = true;
	spec1hd.mbPassReads = true;
	spec1hd.mbPassWrites = true;
	spec1hd.mpThis = this;

	ATMemoryHandlerTable spec2hd = {};
	spec2hd.mbPassAnticReads = true;
	spec2hd.mbPassReads = true;
	spec2hd.mbPassWrites = true;
	spec2hd.mpThis = this;

	ATMemoryHandlerTable cctlhd = {};
	cctlhd.mbPassAnticReads = true;
	cctlhd.mbPassReads = true;
	cctlhd.mbPassWrites = true;
	cctlhd.mpThis = this;

	switch(mCartMode) {
		case kATCartridgeMode_SuperCharger3D:
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpDebugReadHandler = ReadByte_CCTL_SC3D;
			cctlhd.mpReadHandler = ReadByte_CCTL_SC3D;
			cctlhd.mpWriteHandler = WriteByte_CCTL_SC3D;
			break;

		case kATCartridgeMode_5200_4K:
			fixedBase	= 0xB0;
			fixedSize	= 0x10;
			break;

		case kATCartridgeMode_5200_8K:
			fixedBase	= 0xA0;
			fixedSize	= 0x20;
			break;

		case kATCartridgeMode_5200_16K_OneChip:
			fixedBase	= 0x40;
			fixedSize	= 0x40;
			fixedMask	= 0x1F;
			fixed2Base	= 0x80;
			fixed2Size	= 0x40;
			fixed2Offset= 0x2000;
			fixed2Mask	= 0x1F;
			break;

		case kATCartridgeMode_5200_16K_TwoChip:
			fixedBase	= 0x80;
			fixedSize	= 0x40;
			break;

		case kATCartridgeMode_5200_32K:
			fixedBase	= 0x40;
			fixedSize	= 0x80;
			break;

		case kATCartridgeMode_BountyBob5200:
			bank1Base	= 0x40;
			bank1Size	= 0x10;
			bank2Base	= 0x50;
			bank2Size	= 0x10;
			fixedBase	= 0x80;
			fixedSize	= 0x40;
			fixedOffset	= 0x8000;
			fixedMask	= 0x1F;
			spec1Base	= 0x4F;
			spec1Size	= 0x01;
			spec2Base	= 0x5F;
			spec2Size	= 0x01;
			spec1hd.mpDebugReadHandler = NULL;
			spec1hd.mpReadHandler = ReadByte_BB5200_1;
			spec1hd.mpWriteHandler = WriteByte_BB5200_1;
			spec1ReadEnabled = true;
			spec1WriteEnabled = true;
			spec2hd.mpDebugReadHandler = NULL;
			spec2hd.mpReadHandler = ReadByte_BB5200_2;
			spec2hd.mpWriteHandler = WriteByte_BB5200_2;
			spec2Enabled = true;
			break;

		case kATCartridgeMode_BountyBob800:
			bank1Base	= 0x80;
			bank1Size	= 0x10;
			bank2Base	= 0x90;
			bank2Size	= 0x10;
			fixedBase	= 0xA0;
			fixedSize	= 0x20;
			fixedOffset	= 0x8000;
			spec1Base	= 0x8F;
			spec1Size	= 0x01;
			spec2Base	= 0x9F;
			spec2Size	= 0x01;
			spec1hd.mpDebugReadHandler = NULL;
			spec1hd.mpReadHandler = ReadByte_BB800_1;
			spec1hd.mpWriteHandler = WriteByte_BB800_1;
			spec1ReadEnabled = true;
			spec1WriteEnabled = true;
			spec2hd.mpDebugReadHandler = NULL;
			spec2hd.mpReadHandler = ReadByte_BB800_2;
			spec2hd.mpWriteHandler = WriteByte_BB800_2;
			spec2Enabled = true;
			break;

		case kATCartridgeMode_8K:
			fixedBase	= 0xA0;
			fixedSize	= 0x20;
			break;

		case kATCartridgeMode_16K:
			fixedBase	= 0x80;
			fixedSize	= 0x40;
			break;

		case kATCartridgeMode_RightSlot_8K:
			fixedBase	= 0x80;
			fixedSize	= 0x20;
			break;

		case kATCartridgeMode_XEGS_32K:
			bank1Base	= 0x80;
			bank1Size	= 0x20;
			fixedBase	= 0xA0;
			fixedSize	= 0x20;
			fixedOffset	= 0x006000;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_DataToBank<0x03>;
			break;

		case kATCartridgeMode_XEGS_64K:
			bank1Base	= 0x80;
			bank1Size	= 0x20;
			fixedBase	= 0xA0;
			fixedSize	= 0x20;
			fixedOffset	= 0x00E000;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_DataToBank<0x07>;
			break;

		case kATCartridgeMode_XEGS_128K:
			bank1Base	= 0x80;
			bank1Size	= 0x20;
			fixedBase	= 0xA0;
			fixedSize	= 0x20;
			fixedOffset	= 0x01E000;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_DataToBank<0x0F>;
			break;

		case kATCartridgeMode_XEGS_256K:
			bank1Base	= 0x80;
			bank1Size	= 0x20;
			fixedBase	= 0xA0;
			fixedSize	= 0x20;
			fixedOffset	= 0x03E000;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_DataToBank<0x1F>;
			break;

		case kATCartridgeMode_XEGS_512K:
			bank1Base	= 0x80;
			bank1Size	= 0x20;
			fixedBase	= 0xA0;
			fixedSize	= 0x20;
			fixedOffset	= 0x07E000;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_DataToBank<0x3F>;
			break;

		case kATCartridgeMode_XEGS_1M:
			bank1Base	= 0x80;
			bank1Size	= 0x20;
			fixedBase	= 0xA0;
			fixedSize	= 0x20;
			fixedOffset	= 0x0FE000;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_DataToBank<0x7F>;
			break;

		case kATCartridgeMode_Switchable_XEGS_32K:
			bank1Base	= 0x80;
			bank1Size	= 0x20;
			fixedBase	= 0xA0;
			fixedSize	= 0x20;
			fixedOffset	= 0x006000;
			break;

		case kATCartridgeMode_Switchable_XEGS_64K:
			bank1Base	= 0x80;
			bank1Size	= 0x20;
			fixedBase	= 0xA0;
			fixedSize	= 0x20;
			fixedOffset	= 0x00E000;
			break;

		case kATCartridgeMode_Switchable_XEGS_128K:
			bank1Base	= 0x80;
			bank1Size	= 0x20;
			fixedBase	= 0xA0;
			fixedSize	= 0x20;
			fixedOffset	= 0x01E000;
			break;

		case kATCartridgeMode_Switchable_XEGS_256K:
			bank1Base	= 0x80;
			bank1Size	= 0x20;
			fixedBase	= 0xA0;
			fixedSize	= 0x20;
			fixedOffset	= 0x03E000;
			break;

		case kATCartridgeMode_Switchable_XEGS_512K:
			bank1Base	= 0x80;
			bank1Size	= 0x20;
			fixedBase	= 0xA0;
			fixedSize	= 0x20;
			fixedOffset	= 0x07E000;
			break;

		case kATCartridgeMode_Switchable_XEGS_1M:
			bank1Base	= 0x80;
			bank1Size	= 0x20;
			fixedBase	= 0xA0;
			fixedSize	= 0x20;
			fixedOffset	= 0x0FE000;
			break;

		case kATCartridgeMode_DB_32K:
			bank1Base	= 0x80;
			bank1Size	= 0x20;
			fixedBase	= 0xA0;
			fixedSize	= 0x20;
			fixedOffset	= 0x6000;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_AddressToBank<0x03>;
			break;

		case kATCartridgeMode_MegaCart_16K:
			bank1Base	= 0x80;
			bank1Size	= 0x40;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_DataToBank_Switchable<0x00>;
			break;

		case kATCartridgeMode_MegaCart_32K:
			bank1Base	= 0x80;
			bank1Size	= 0x40;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_DataToBank_Switchable<0x01>;
			break;

		case kATCartridgeMode_MegaCart_64K:
			bank1Base	= 0x80;
			bank1Size	= 0x40;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_DataToBank_Switchable<0x03>;
			break;

		case kATCartridgeMode_MegaCart_128K:
			bank1Base	= 0x80;
			bank1Size	= 0x40;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_DataToBank_Switchable<0x07>;
			break;

		case kATCartridgeMode_MegaCart_256K:
			bank1Base	= 0x80;
			bank1Size	= 0x40;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_DataToBank_Switchable<0x0F>;
			break;

		case kATCartridgeMode_MegaCart_512K:
			bank1Base	= 0x80;
			bank1Size	= 0x40;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_DataToBank_Switchable<0x1F>;
			break;

		case kATCartridgeMode_MegaCart_1M:
			bank1Base	= 0x80;
			bank1Size	= 0x40;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_DataToBank_Switchable<0x3F>;
			break;

		case kATCartridgeMode_SpartaDosX_128K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_SDX128;
			cctlhd.mpWriteHandler = WriteByte_CCTL_SDX128;
			break;

		case kATCartridgeMode_SpartaDosX_64K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_SDX64<0xE0>;
			cctlhd.mpWriteHandler = WriteByte_CCTL_SDX64<0xE0>;
			break;

		case kATCartridgeMode_Atrax_128K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_DataToBank_Switchable<0x0F>;
			break;

		case kATCartridgeMode_Williams_64K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_Williams<7>;
			cctlhd.mpWriteHandler = WriteByte_CCTL_Williams<7>;
			break;

		case kATCartridgeMode_Williams_32K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_Williams<3>;
			cctlhd.mpWriteHandler = WriteByte_CCTL_Williams<3>;
			break;

		case kATCartridgeMode_Diamond_64K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_SDX64<0xD0>;
			cctlhd.mpWriteHandler = WriteByte_CCTL_SDX64<0xD0>;
			break;

		case kATCartridgeMode_Express_64K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_SDX64<0x70>;
			cctlhd.mpWriteHandler = WriteByte_CCTL_SDX64<0x70>;
			break;

		case kATCartridgeMode_TelelinkII:
			fixedBase	= 0xA0;
			fixedSize	= 0x20;
			fixed2Base	= 0x80;
			fixed2Size	= 0x20;
			fixed2Offset = 0;
			fixed2Mask	= 0;
			fixed2RAM	= true;
			spec1hd.mpWriteHandler = WriteByte_TelelinkII;
			spec1Base	= 0x80;
			spec1Size	= 0x20;
			spec1WriteEnabled = true;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_TelelinkII;
			cctlhd.mpWriteHandler = WriteByte_CCTL_TelelinkII;
			break;

		case kATCartridgeMode_MaxFlash_128K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_MaxFlash_128K;
			cctlhd.mpWriteHandler = WriteByte_CCTL_MaxFlash_128K;
			spec1Base	= 0xA0;
			spec1Size	= 0x20;
			spec1hd.mpReadHandler = ReadByte_MaxFlash;
			spec1hd.mpWriteHandler = WriteByte_MaxFlash;
			spec1WriteEnabled = true;
			break;

		case kATCartridgeMode_MaxFlash_128K_MyIDE:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_MaxFlash_128K_MyIDE;
			cctlhd.mpWriteHandler = WriteByte_CCTL_MaxFlash_128K_MyIDE;
			spec1Base	= 0xA0;
			spec1Size	= 0x20;
			spec1hd.mpReadHandler = ReadByte_MaxFlash;
			spec1hd.mpWriteHandler = WriteByte_MaxFlash;
			spec1WriteEnabled = true;
			break;

		case kATCartridgeMode_MaxFlash_1024K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_MaxFlash_1024K;
			cctlhd.mpWriteHandler = WriteByte_CCTL_MaxFlash_1024K;
			spec1Base	= 0xA0;
			spec1Size	= 0x20;
			spec1hd.mpReadHandler = ReadByte_MaxFlash;
			spec1hd.mpWriteHandler = WriteByte_MaxFlash;
			spec1WriteEnabled = true;
			break;

		case kATCartridgeMode_Phoenix_8K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_Phoenix;
			break;

		case kATCartridgeMode_Blizzard_16K:
			bank1Base	= 0x80;
			bank1Size	= 0x40;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_Phoenix;
			break;

		case kATCartridgeMode_OSS_034M:
			bank1Base	= 0xA0;
			bank1Size	= 0x10;
			bank2Base	= 0xB0;
			bank2Size	= 0x10;
			usecctl		= true;
			usecctlread	= true;
			usecctlwrite= true;
			cctlhd.mpReadHandler = ReadByte_CCTL_OSS_034M;
			cctlhd.mpWriteHandler = WriteByte_CCTL_OSS_034M;
			break;

		case kATCartridgeMode_OSS_M091:
			bank1Base	= 0xA0;
			bank1Size	= 0x10;
			bank2Base	= 0xB0;
			bank2Size	= 0x10;
			usecctl		= true;
			usecctlread	= true;
			usecctlwrite= true;
			cctlhd.mpReadHandler = ReadByte_CCTL_OSS_M091;
			cctlhd.mpWriteHandler = WriteByte_CCTL_OSS_M091;
			break;

		case kATCartridgeMode_Corina_1M_EEPROM:
			bank1Base	= 0x80;
			bank1Size	= 0x40;
			spec1Base	= 0x80;
			spec1Size	= 0x40;
			usecctl		= true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_Corina;
			spec1hd.mpWriteHandler = WriteByte_Corina1M;
			break;

		case kATCartridgeMode_Corina_512K_SRAM_EEPROM:
			bank1Base	= 0x80;
			bank1Size	= 0x40;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_Corina;
			spec1Base	= 0x80;
			spec1Size	= 0x40;
			spec1hd.mpWriteHandler = WriteByte_Corina512K;
			break;

		case kATCartridgeMode_SIC:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			bank2Base	= 0x80;
			bank2Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_SIC;
			cctlhd.mpWriteHandler = WriteByte_CCTL_SIC;
			spec1Base	= 0xA0;
			spec1Size	= 0x20;
			spec1hd.mpReadHandler = ReadByte_MaxFlash;
			spec1hd.mpWriteHandler = WriteByte_MaxFlash;
			spec1WriteEnabled = true;
			spec2Base	= 0x80;
			spec2Size	= 0x20;
			spec2hd.mpReadHandler = ReadByte_MaxFlash;
			spec2hd.mpWriteHandler = WriteByte_MaxFlash;
			break;
	}

	if (fixedSize) {
		mpMemLayerFixedBank1 = mpMemMan->CreateLayer(mBasePriority, mCARTROM.data() + fixedOffset, fixedBase, fixedSize, true);

		if (fixedMask)
			mpMemMan->SetLayerMemory(mpMemLayerFixedBank1, mCARTROM.data() + fixedOffset, fixedBase, fixedSize, fixedMask);

		mpMemMan->EnableLayer(mpMemLayerFixedBank1, true);
	}

	if (fixed2Size) {
		uint8 *mem = fixed2RAM ? mCARTRAM.data() : mCARTROM.data();
		mpMemLayerFixedBank2 = mpMemMan->CreateLayer(mBasePriority, mem + fixed2Offset, fixed2Base, fixedSize, true);

		if (fixed2Mask >= 0)
			mpMemMan->SetLayerMemory(mpMemLayerFixedBank2, mem + fixed2Offset, fixed2Base, fixed2Size, fixed2Mask);

		mpMemMan->EnableLayer(mpMemLayerFixedBank2, true);
	}

	if (bank1Size)
		mpMemLayerVarBank1 = mpMemMan->CreateLayer(mBasePriority+1, mCARTROM.data(), bank1Base, bank1Size, true);

	if (bank2Size)
		mpMemLayerVarBank2 = mpMemMan->CreateLayer(mBasePriority+2, mCARTROM.data(), bank2Base, bank2Size, true);

	if (spec1Size) {
		mpMemLayerSpec1 = mpMemMan->CreateLayer(mBasePriority+3, spec1hd, spec1Base, spec1Size);

		if (spec1ReadEnabled) {
			mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, true);
			mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_CPURead, true);
		}

		if (spec1WriteEnabled)
			mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_CPUWrite, true);
	}

	if (spec2Size) {
		mpMemLayerSpec2 = mpMemMan->CreateLayer(mBasePriority+4, spec2hd, spec2Base, spec2Size);

		if (spec2Enabled)
			mpMemMan->EnableLayer(mpMemLayerSpec2, true);
	}

	if (usecctl) {
		mpMemLayerControl = mpMemMan->CreateLayer(mBasePriority+5, cctlhd, 0xD5, 0x01);

		mpMemMan->EnableLayer(mpMemLayerControl, kATMemoryAccessMode_AnticRead, usecctlread);
		mpMemMan->EnableLayer(mpMemLayerControl, kATMemoryAccessMode_CPURead, usecctlread);
		mpMemMan->EnableLayer(mpMemLayerControl, kATMemoryAccessMode_CPUWrite, usecctlwrite);
	}

	UpdateCartBank();

	if (mpMemLayerVarBank2)
		UpdateCartBank2();
}

void ATCartridgeEmulator::ShutdownMemoryLayers() {
	if (mpMemMan)
	{
#define X(layer) if (layer) { mpMemMan->DeleteLayer(layer); layer = NULL; }
		X(mpMemLayerFixedBank1)
		X(mpMemLayerFixedBank2)
		X(mpMemLayerVarBank1)
		X(mpMemLayerVarBank2)
		X(mpMemLayerSpec1)
		X(mpMemLayerSpec2)
		X(mpMemLayerControl)
#undef X

		mpMemMan = NULL;
	}
}

void ATCartridgeEmulator::SetCartBank(int bank) {
	if (mCartBank == bank)
		return;

	mCartBank = bank;
	UpdateCartBank();
}

void ATCartridgeEmulator::SetCartBank2(int bank) {
	if (mCartBank2 == bank)
		return;

	mCartBank2 = bank;
	UpdateCartBank2();
}

void ATCartridgeEmulator::UpdateCartBank() {
	if (mCartMode == kATCartridgeMode_SIC) {
		if (mpCB)
			mpCB->CartSetAxxxMapped((mCartBank & 0x40) == 0);

		const bool flashWrite = (mCartBank & 0x80) != 0;
		mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_CPUWrite, flashWrite);
		mpMemMan->EnableLayer(mpMemLayerSpec2, kATMemoryAccessMode_CPUWrite, flashWrite);

		const bool flashRead = (mFlashReadMode != kFlashReadMode_Normal);

		if (mCartBank & 0x40) {
			mpMemMan->EnableLayer(mpMemLayerVarBank1, false);
			mpMemMan->EnableLayer(mpMemLayerSpec1, false);
		} else {
			mpMemMan->EnableLayer(mpMemLayerVarBank1, true);
			mpMemMan->SetLayerMemory(mpMemLayerVarBank1, mCARTROM.data() + ((uint32)(mCartBank & 0x1f) << 14) + 0x2000);
			mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, flashRead);
			mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_CPURead, flashRead);
		}

		if (mCartBank & 0x20) {
			mpMemMan->EnableLayer(mpMemLayerVarBank2, true);
			mpMemMan->SetLayerMemory(mpMemLayerVarBank2, mCARTROM.data() + ((uint32)(mCartBank & 0x1f) << 14));
			mpMemMan->EnableLayer(mpMemLayerSpec2, kATMemoryAccessMode_AnticRead, flashRead);
			mpMemMan->EnableLayer(mpMemLayerSpec2, kATMemoryAccessMode_CPURead, flashRead);
		} else {
			mpMemMan->EnableLayer(mpMemLayerVarBank2, false);
			mpMemMan->EnableLayer(mpMemLayerSpec2, false);
		}
		return;
	}

	if (mCartBank < 0) {
		if (mpCB)
			mpCB->CartSetAxxxMapped(mCartMode == kATCartridgeMode_BountyBob800);

		switch(mCartMode) {
			case kATCartridgeMode_Corina_1M_EEPROM:
			case kATCartridgeMode_Corina_512K_SRAM_EEPROM:
			case kATCartridgeMode_MaxFlash_128K:
			case kATCartridgeMode_MaxFlash_128K_MyIDE:
			case kATCartridgeMode_MaxFlash_1024K:
				mpMemMan->EnableLayer(mpMemLayerSpec1, false);
				break;
		}

		if (mpMemLayerVarBank1)
			mpMemMan->EnableLayer(mpMemLayerVarBank1, false);
		return;
	}

	if (mpMemLayerVarBank1)
		mpMemMan->EnableLayer(mpMemLayerVarBank1, true);

	if (mpCB)
		mpCB->CartSetAxxxMapped(mCartMode != kATCartridgeMode_RightSlot_8K);

	const uint8 *cartbase = mCARTROM.data();
	switch(mCartMode) {
		case kATCartridgeMode_MaxFlash_128K:
		case kATCartridgeMode_MaxFlash_128K_MyIDE:
		case kATCartridgeMode_MaxFlash_1024K:
		case kATCartridgeMode_XEGS_32K:
		case kATCartridgeMode_XEGS_64K:
		case kATCartridgeMode_XEGS_128K:
		case kATCartridgeMode_XEGS_256K:
		case kATCartridgeMode_XEGS_512K:
		case kATCartridgeMode_XEGS_1M:
		case kATCartridgeMode_SpartaDosX_64K:
		case kATCartridgeMode_SpartaDosX_128K:
		case kATCartridgeMode_Williams_64K:
		case kATCartridgeMode_Williams_32K:
		case kATCartridgeMode_Express_64K:
		case kATCartridgeMode_Diamond_64K:
		case kATCartridgeMode_Atrax_128K:
			mpMemMan->SetLayerMemory(mpMemLayerVarBank1, cartbase + (mCartBank << 13));
			break;

		case kATCartridgeMode_MegaCart_16K:
		case kATCartridgeMode_MegaCart_32K:
		case kATCartridgeMode_MegaCart_64K:
		case kATCartridgeMode_MegaCart_128K:
		case kATCartridgeMode_MegaCart_256K:
		case kATCartridgeMode_MegaCart_512K:
		case kATCartridgeMode_MegaCart_1M:
			mpMemMan->SetLayerMemory(mpMemLayerVarBank1, cartbase + (mCartBank << 14));
			break;

		case kATCartridgeMode_OSS_034M:
		case kATCartridgeMode_OSS_M091:
		case kATCartridgeMode_BountyBob5200:
		case kATCartridgeMode_BountyBob800:
			mpMemMan->SetLayerMemory(mpMemLayerVarBank1, cartbase + (mCartBank << 12));
			break;

		case kATCartridgeMode_Corina_1M_EEPROM:
			if (mCartBank == 64) {
				// EEPROM - 8K mirrored twice
				mpMemMan->SetLayerMemory(mpMemLayerVarBank1, cartbase + (64 << 14), 0x80, 0x40, 0x1F);
				mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_CPUWrite, true);
			} else {
				mpMemMan->SetLayerMemory(mpMemLayerVarBank1, cartbase + (mCartBank << 14), 0x80, 0x40);
				mpMemMan->EnableLayer(mpMemLayerSpec1, false);
			}
			break;

		case kATCartridgeMode_Corina_512K_SRAM_EEPROM:
			if (mCartBank == 64) {
				// EEPROM - 8K mirrored twice
				mpMemMan->SetLayerMemory(mpMemLayerVarBank1, cartbase + (64 << 14), 0x80, 0x40, 0x1F);
				mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_CPUWrite, true);
			} else if (mCartBank >= 32) {
				mpMemMan->SetLayerMemory(mpMemLayerVarBank1, mCARTRAM.data() + ((mCartBank - 32) << 14), 0x80, 0x40, 0xFFFFFFFFU, false);
				mpMemMan->EnableLayer(mpMemLayerSpec1, false);
			} else {
				mpMemMan->SetLayerMemory(mpMemLayerVarBank1, cartbase + (mCartBank << 14), 0x80, 0x40, 0xFFFFFFFFU, true);
				mpMemMan->EnableLayer(mpMemLayerSpec1, false);
			}
			break;
	}
}

void ATCartridgeEmulator::UpdateCartBank2() {
	if (mCartMode == kATCartridgeMode_SIC)
		return;

	if (mCartBank2 < 0) {
		mpMemMan->EnableLayer(mpMemLayerVarBank2, false);
		return;
	}

	mpMemMan->EnableLayer(mpMemLayerVarBank2, true);

	const uint8 *cartbase = mCARTROM.data();
	switch(mCartMode) {
		case kATCartridgeMode_BountyBob5200:
			mpMemMan->SetLayerMemory(mpMemLayerVarBank2, cartbase + (mCartBank2 << 12) + 0x4000);
			break;

		case kATCartridgeMode_BountyBob800:
			mpMemMan->SetLayerMemory(mpMemLayerVarBank2, cartbase + (mCartBank2 << 12) + 0x4000);
			break;

		case kATCartridgeMode_OSS_034M:
			mpMemMan->SetLayerMemory(mpMemLayerVarBank2, cartbase + 0x3000);
			break;

		case kATCartridgeMode_OSS_M091:
			mpMemMan->SetLayerMemory(mpMemLayerVarBank2, cartbase);
			break;
	}
}
