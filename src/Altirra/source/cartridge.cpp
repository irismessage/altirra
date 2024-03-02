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
{
}

ATCartridgeEmulator::~ATCartridgeEmulator() {
}

void ATCartridgeEmulator::SetUIRenderer(IATUIRenderer *r) {
	mpUIRenderer = r;
}

bool ATCartridgeEmulator::IsABxxMapped() const {
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
}

void ATCartridgeEmulator::LoadFlash1Mb(bool altbank) {
	mCartMode = altbank ? kATCartridgeMode_MaxFlash_128K_MyIDE : kATCartridgeMode_MaxFlash_128K;
	mCartBank = 0;
	mInitialCartBank = 0;
	mInitialCartBank2 = -1;
	mImagePath.clear();
	vdfastvector<uint8>().swap(mCARTROM);
	vdfastvector<uint8>().swap(mCARTRAM);
	mCARTROM.resize(0x20000, 0xFF);
	mbDirty = false;
}

void ATCartridgeEmulator::LoadFlash8Mb() {
	mCartMode = kATCartridgeMode_MaxFlash_1024K;
	mCartBank = 127;
	mInitialCartBank = 127;
	mInitialCartBank2 = -1;
	mImagePath.clear();
	vdfastvector<uint8>().swap(mCARTROM);
	vdfastvector<uint8>().swap(mCARTRAM);
	mCARTROM.resize(0x100000, 0xFF);
	mbDirty = false;
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
		case kATCartridgeMode_TelelinkII:
		case kATCartridgeMode_RightSlot_8K:
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
			allocSize = 16384;
			break;

		case kATCartridgeMode_OSS_M091:
			mInitialCartBank = 3;
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
	}

	mCartBank = mInitialCartBank;
	mCartBank2 = mInitialCartBank2;

	if (mCartMode == kATCartridgeMode_TelelinkII)
		mCARTROM.resize(allocSize, 0xFF);
	else
		mCARTROM.resize(allocSize, 0);

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

	return true;
}

void ATCartridgeEmulator::Unload() {
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
	mCommandPhase = 0;
	mFlashReadMode = kFlashReadMode_Normal;
}

bool ATCartridgeEmulator::WriteMemoryMap5200(const uint8 **readMap, uint8 **writeMap, const uint8 **anticMap, const uint8 *dummyReadPage, uint8 *dummyWritePage) {
	if (mCartBank < 0)
		return false;

	const uint8 *cartbase = mCARTROM.data();

	switch(mCartMode) {
		case kATCartridgeMode_5200_4K:
			anticMap += 0x70;
			readMap += 0x70;
			for(int i=0; i<16; ++i) {
				anticMap[i] = readMap[i] = cartbase;
				cartbase += 0x100;
			}
			return true;

		case kATCartridgeMode_5200_8K:
			anticMap += 0x60;
			readMap += 0x60;
			for(int i=0; i<32; ++i) {
				anticMap[i] = readMap[i] = cartbase;
				cartbase += 0x100;
			}
			return true;

		case kATCartridgeMode_5200_16K_TwoChip:
			for(int i=0; i<32; ++i) {
				anticMap[i     ] = readMap[i     ] = cartbase;
				anticMap[i+0x20] = readMap[i+0x20] = cartbase;
				anticMap[i+0x40] = readMap[i+0x40] = cartbase + 0x2000;
				anticMap[i+0x60] = readMap[i+0x60] = cartbase + 0x2000;
				cartbase += 0x100;
			}
			return true;

		case kATCartridgeMode_5200_16K_OneChip:
			anticMap += 0x40;
			readMap += 0x40;
			for(int i=0; i<64; ++i) {
				anticMap[i] = readMap[i] = cartbase;
				cartbase += 0x100;
			}
			return true;

		case kATCartridgeMode_5200_32K:
			for(int i=0; i<128; ++i) {
				anticMap[i] = readMap[i] = cartbase;
				cartbase += 0x100;
			}
			return true;

		case kATCartridgeMode_BountyBob5200:
			{
				const uint8 *cartbase1 = mCARTROM.data() + 0x1000 * mCartBank;
				const uint8 *cartbase2 = mCARTROM.data() + 0x4000 + 0x1000 * mCartBank2;

				for(int i=0; i<15; ++i) {
					anticMap[i] = readMap[i] = cartbase1;
					cartbase1 += 0x100;

					anticMap[i+16] = readMap[i+16] = cartbase2;
					cartbase2 += 0x100;
				}

				const uint8 *cartbase3 = mCARTROM.data() + 0x8000;
				for(int i=0; i<32; ++i) {
					anticMap[i+64] = readMap[i+64] = cartbase3;
					anticMap[i+96] = readMap[i+96] = cartbase3;
					cartbase3 += 0x100;
				}

				anticMap[15] = NULL;
				anticMap[31] = NULL;
				readMap[15] = NULL;
				readMap[31] = NULL;
				writeMap[15] = NULL;
				writeMap[31] = NULL;
			}
			return true;
	}

	return false;
}


bool ATCartridgeEmulator::WriteMemoryMap89(const uint8 **readMap, uint8 **writeMap, const uint8 **anticMap, const uint8 *dummyReadPage, uint8 *dummyWritePage) {
	if (mCartBank < 0)
		return false;

	const uint8 *cartbase = mCARTROM.data();

	switch(mCartMode) {
		case kATCartridgeMode_XEGS_32K:
		case kATCartridgeMode_XEGS_64K:
		case kATCartridgeMode_XEGS_128K:
		case kATCartridgeMode_XEGS_256K:
		case kATCartridgeMode_XEGS_512K:
		case kATCartridgeMode_XEGS_1M:
		case kATCartridgeMode_Switchable_XEGS_32K:
		case kATCartridgeMode_Switchable_XEGS_64K:
		case kATCartridgeMode_Switchable_XEGS_128K:
		case kATCartridgeMode_Switchable_XEGS_256K:
		case kATCartridgeMode_Switchable_XEGS_512K:
		case kATCartridgeMode_Switchable_XEGS_1M:
		case kATCartridgeMode_16K:
		case kATCartridgeMode_Blizzard_16K:
		case kATCartridgeMode_DB_32K:
			{
				cartbase += 0x2000 * mCartBank;
				for(int i=0; i<32; ++i) {
					anticMap[i] = readMap[i] = cartbase + (i << 8);
					writeMap[i] = dummyWritePage;
				}
			}
			return true;

		case kATCartridgeMode_MegaCart_16K:
		case kATCartridgeMode_MegaCart_32K:
		case kATCartridgeMode_MegaCart_64K:
		case kATCartridgeMode_MegaCart_128K:
		case kATCartridgeMode_MegaCart_256K:
		case kATCartridgeMode_MegaCart_512K:
		case kATCartridgeMode_MegaCart_1M:
			{
				cartbase += 0x4000 * mCartBank;
				for(int i=0; i<32; ++i) {
					anticMap[i] = readMap[i] = cartbase + (i << 8);
					writeMap[i] = dummyWritePage;
				}
			}
			return true;

		case kATCartridgeMode_BountyBob800:
			{
				const uint8 *cartbase1 = mCARTROM.data() + 0x1000 * mCartBank;
				const uint8 *cartbase2 = mCARTROM.data() + 0x4000 + 0x1000 * mCartBank2;

				for(int i=0; i<15; ++i) {
					anticMap[i] = readMap[i] = cartbase1 + (i << 8);
					anticMap[i+16] = readMap[i+16] = cartbase2 + (i << 8);
					writeMap[i] = dummyWritePage;
					writeMap[i+16] = dummyWritePage;
				}

				anticMap[15] = NULL;
				anticMap[31] = NULL;
				readMap[15] = NULL;
				readMap[31] = NULL;
				writeMap[15] = NULL;
				writeMap[31] = NULL;
			}
			return true;

		case kATCartridgeMode_Corina_1M_EEPROM:
			cartbase += mCartBank << 14;

			if (mCartBank == 64) {
				// EEPROM - 8K mirrored twice
				for(int i=0; i<32; ++i) {
					writeMap[i] = NULL;
					anticMap[i] = readMap[i] = cartbase;
					cartbase += 0x100;
				}
			} else {
				for(int i=0; i<32; ++i) {
					anticMap[i] = readMap[i] = cartbase;
					writeMap[i] = dummyWritePage;
					cartbase += 0x100;
				}
			}
			return true;

		case kATCartridgeMode_Corina_512K_SRAM_EEPROM:
			if (mCartBank == 64) {
				// EEPROM - 8K mirrored twice
				cartbase += (32 << 14);

				for(int i=0; i<32; ++i) {
					writeMap[i] = NULL;
					anticMap[i] = readMap[i] = cartbase;
					cartbase += 0x100;
				}
			} else if (mCartBank >= 32) {
				// SRAM
				uint8 *dst = mCARTRAM.data() + ((mCartBank - 32) << 14);
				for(int i=0; i<32; ++i) {
					anticMap[i] = readMap[i] = dst;
					writeMap[i] = dst;
					dst += 0x100;
				}
			} else {
				// ROM
				cartbase += mCartBank << 14;

				for(int i=0; i<32; ++i) {
					anticMap[i] = readMap[i] = cartbase;
					writeMap[i] = dummyWritePage;
					cartbase += 0x100;
				}
			}
			return true;
		case kATCartridgeMode_5200_4K:
		case kATCartridgeMode_5200_8K:
		case kATCartridgeMode_5200_16K_OneChip:
		case kATCartridgeMode_5200_16K_TwoChip:
		case kATCartridgeMode_5200_32K:
			return true;
		case kATCartridgeMode_TelelinkII:
			std::fill(readMap, readMap + 32, mCARTRAM.data());
			std::fill(anticMap, anticMap + 32, mCARTRAM.data());
			std::fill(writeMap, writeMap + 32, (uint8 *)NULL);
			return true;

		case kATCartridgeMode_RightSlot_8K:
			for(int i=0; i<32; ++i) {
				anticMap[i] = readMap[i] = cartbase + (i << 8);
				writeMap[i] = dummyWritePage;
			}
			return true;
	}

	return false;
}

bool ATCartridgeEmulator::WriteMemoryMapAB(const uint8 **readMap, uint8 **writeMap, const uint8 **anticMap, const uint8 *dummyReadPage, uint8 *dummyWritePage) {
	if (!mCartMode || mCartBank < 0)
		return false;

	const uint8 *cartbase = mCARTROM.data();

	switch(mCartMode) {
		case kATCartridgeMode_RightSlot_8K:
		case kATCartridgeMode_8K:
		case kATCartridgeMode_TelelinkII:
		case kATCartridgeMode_Phoenix_8K:
			break;

		case kATCartridgeMode_Blizzard_16K:
		case kATCartridgeMode_16K:
			cartbase += 0x2000;
			break;

		case kATCartridgeMode_DB_32K:
		case kATCartridgeMode_XEGS_32K:
		case kATCartridgeMode_Switchable_XEGS_32K:
			cartbase += 0x6000;
			break;

		case kATCartridgeMode_XEGS_64K:
		case kATCartridgeMode_Switchable_XEGS_64K:
			cartbase += 0xE000;
			break;

		case kATCartridgeMode_XEGS_128K:
		case kATCartridgeMode_Switchable_XEGS_128K:
			cartbase += 0x1E000;
			break;

		case kATCartridgeMode_MaxFlash_128K:
		case kATCartridgeMode_MaxFlash_128K_MyIDE:
		case kATCartridgeMode_MaxFlash_1024K:
			if (mFlashReadMode) {
				for(int i=0; i<32; ++i) {
					anticMap[i] = readMap[i] = NULL;
					writeMap[i] = NULL;
				}
				return true;
			}

			cartbase += 0x2000 * mCartBank;
			dummyWritePage = NULL;		// trap writes to detect flash commands
			break;

		case kATCartridgeMode_MegaCart_16K:
		case kATCartridgeMode_MegaCart_32K:
		case kATCartridgeMode_MegaCart_64K:
		case kATCartridgeMode_MegaCart_128K:
		case kATCartridgeMode_MegaCart_256K:
		case kATCartridgeMode_MegaCart_512K:
		case kATCartridgeMode_MegaCart_1M:
			cartbase += 0x4000 * mCartBank + 0x2000;
			break;

		case kATCartridgeMode_BountyBob800:
			{
				const uint8 *cartbase = mCARTROM.data() + 0x8000;

				for(int i=0; i<32; ++i) {
					anticMap[i] = readMap[i] = cartbase + (i << 8);
					writeMap[i] = dummyWritePage;
				}
			}
			return true;

		case kATCartridgeMode_OSS_034M:
			{
				if (mCartBank >= 4) {
					for(int i=0; i<16; ++i) {
						anticMap[i+16] = readMap[i+16] = dummyReadPage;
					}
				} else {
					for(int i=0; i<16; ++i) {
						anticMap[i+16] = readMap[i+16] = cartbase + (i << 8) + 0x3000;
					}
				}

				cartbase += 0x1000 * mCartBank;

				for(int i=0; i<16; ++i) {
					anticMap[i] = readMap[i] = cartbase + (i << 8);
					writeMap[i] = dummyWritePage;
					writeMap[i+16] = dummyWritePage;
				}
			}

			return true;

		case kATCartridgeMode_OSS_M091:
			{
				for(int i=0; i<16; ++i) {
					anticMap[i+16] = readMap[i+16] = cartbase + (i << 8);
				}

				cartbase += 0x1000 * mCartBank;

				for(int i=0; i<16; ++i) {
					anticMap[i] = readMap[i] = cartbase + (i << 8);
					writeMap[i] = dummyWritePage;
					writeMap[i+16] = dummyWritePage;
				}
			}

			return true;

		case kATCartridgeMode_Corina_1M_EEPROM:
			cartbase += mCartBank << 14;

			if (mCartBank == 64) {
				// EEPROM - 8K mirrored twice
				for(int i=0; i<32; ++i) {
					writeMap[i] = NULL;
					anticMap[i] = readMap[i] = cartbase;
					cartbase += 0x100;
				}
			} else {
				cartbase += 0x2000;

				for(int i=0; i<32; ++i) {
					anticMap[i] = readMap[i] = cartbase;
					writeMap[i] = dummyWritePage;
					cartbase += 0x100;
				}
			}
			return true;

		case kATCartridgeMode_Corina_512K_SRAM_EEPROM:
			if (mCartBank == 64) {
				// EEPROM - 8K mirrored twice
				cartbase += 32 << 14;
				for(int i=0; i<32; ++i) {
					writeMap[i] = NULL;
					anticMap[i] = readMap[i] = cartbase;
					cartbase += 0x100;
				}
			} else {
				if (mCartBank >= 32) {
					uint8 *dst = mCARTRAM.data() + ((mCartBank - 32) << 14) + 0x2000;

					for(int i=0; i<32; ++i) {
						anticMap[i] = readMap[i] = dst;
						writeMap[i] = dst;
						dst += 0x100;
					}
				} else {
					cartbase += mCartBank << 14;
					cartbase += 0x2000;

					for(int i=0; i<32; ++i) {
						anticMap[i] = readMap[i] = cartbase;
						writeMap[i] = dummyWritePage;
						cartbase += 0x100;
					}
				}
			}
			return true;

		case kATCartridgeMode_SpartaDosX_128K:
		case kATCartridgeMode_SpartaDosX_64K:
		case kATCartridgeMode_Atrax_128K:
		case kATCartridgeMode_Williams_64K:
		case kATCartridgeMode_Williams_32K:
		case kATCartridgeMode_Diamond_64K:
		case kATCartridgeMode_Express_64K:
			cartbase += mCartBank << 13;
			break;

		case kATCartridgeMode_5200_4K:
		case kATCartridgeMode_5200_8K:
		case kATCartridgeMode_5200_16K_OneChip:
		case kATCartridgeMode_5200_16K_TwoChip:
		case kATCartridgeMode_5200_32K:
			return true;
	}

	for(int i=0; i<32; ++i) {
		anticMap[i] = readMap[i] = cartbase + (i << 8);
		writeMap[i] = dummyWritePage;
	}

	return true;
}

uint8 ATCartridgeEmulator::ReadByte4567(uint16 address, bool& remapRequired) {
	switch(mCartMode) {
		case kATCartridgeMode_BountyBob5200:
			{
				uint32 index = (address & 0xEFFF) - 0x4FF6;

				if (index < 4) {
					remapRequired = true;

					if (address & 0x1000)
						mCartBank2 = index;
					else
						mCartBank = index;
				}

				return address < 0x5000 ? mCARTROM[(mCartBank << 12) + address - 0x4000]
					: address < 0x6000 ? mCARTROM[(mCartBank2 << 12) + address - 0x5000 + 0x4000]
					: mCARTROM[address - 0x6000 + 0x8000];
			}
			break;
	}

	return 0xFF;
}

bool ATCartridgeEmulator::WriteByte4567(uint16 address, uint8 value) {
	switch(mCartMode) {
		case kATCartridgeMode_BountyBob5200:
			{
				uint32 index = (address & 0xEFFF) - 0x4FF6;

				if (index < 4) {
					if (address & 0x1000)
						mCartBank2 = index;
					else
						mCartBank = index;
				}
			}
			return true;
	}

	return false;
}

uint8 ATCartridgeEmulator::ReadByte89AB(uint16 address, bool& remapRequired) {
	switch(mCartMode) {
		case kATCartridgeMode_BountyBob800:
			{
				uint32 index = (address & 0xEFFF) - 0x8FF6;

				if (index < 4) {
					remapRequired = true;

					if (address & 0x1000)
						mCartBank2 = index;
					else
						mCartBank = index;
				}

				return address < 0x9000 ? mCARTROM[(mCartBank << 12) + address - 0x8000]
					: address < 0xA000 ? mCARTROM[(mCartBank2 << 12) + address - 0x9000 + 0x4000]
					: mCARTROM[address - 0xA000 + 0x8000];
			}
			break;

		case kATCartridgeMode_MaxFlash_128K:
		case kATCartridgeMode_MaxFlash_128K_MyIDE:
		case kATCartridgeMode_MaxFlash_1024K:
			switch(mFlashReadMode) {
				case kFlashReadMode_Normal:
					VDASSERT(false);
					break;

				case kFlashReadMode_Autoselect:
					switch(address & 0xFF) {
						case 0x00:
							return 0x01;	// XX00 Manufacturer ID: AMD

						case 0x01:
							if (mCartMode == kATCartridgeMode_MaxFlash_128K)
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
			break;
	}

	return 0xFF;
}

bool ATCartridgeEmulator::WriteByte89AB(uint16 address, uint8 value) {
	switch(mCartMode) {
		case kATCartridgeMode_BountyBob800:
			{
				uint32 index = (address & 0xEFFF) - 0x8FF6;

				if (index < 4) {
					if (address & 0x1000)
						mCartBank2 = index;
					else
						mCartBank = index;
				}
			}
			return true;

		case kATCartridgeMode_MaxFlash_128K:
		case kATCartridgeMode_MaxFlash_128K_MyIDE:
		case kATCartridgeMode_MaxFlash_1024K:
			{
				uint32 fullAddr = mCartBank * 0x2000 + (address & 0x1fff);
				uint32 fullAddr16 = fullAddr & 0xffff;

				switch(mCommandPhase) {
					case 0:
						// $F0 written at phase 0 deactivates autoselect mode
						if (value == 0xF0) {
							if (!mFlashReadMode)
								return false;

							mFlashReadMode = kFlashReadMode_Normal;
							return true;
						}

						if (fullAddr16 == 0x5555 && value == 0xAA)
							mCommandPhase = 1;
						break;

					case 1:
						if (fullAddr16 == 0x2AAA && value == 0x55)
							mCommandPhase = 2;
						else
							mCommandPhase = 0;
						break;

					case 2:
						if (fullAddr16 != 0x5555) {
							mCommandPhase = 0;
							return false;
						}

						switch(value) {
							case 0x80:	// $80: sector or chip erase
								mCommandPhase = 3;
								break;

							case 0x90:	// $90: autoselect mode
								mFlashReadMode = kFlashReadMode_Autoselect;
								mCommandPhase = 0;
								return true;

							case 0xA0:	// $A0: program mode
								mCommandPhase = 6;
								break;

							case 0xF0:	// $F0: reset
								mCommandPhase = 0;
								mFlashReadMode = kFlashReadMode_Normal;
								return true;

							default:
								mCommandPhase = 0;
								break;
						}

						break;

					case 3:		// 5555[AA] 2AAA[55] 5555[80]
						if (fullAddr16 != 0x5555 || value != 0xAA) {
							mCommandPhase = 0;
							return false;
						}

						mCommandPhase = 4;
						break;

					case 4:		// 5555[AA] 2AAA[55] 5555[80] 5555[AA]
						if (fullAddr16 != 0x2AAA || value != 0x55) {
							mCommandPhase = 0;
							return false;
						}

						mCommandPhase = 5;
						break;

					case 5:		// 5555[AA] 2AAA[55] 5555[80] 5555[AA] 2AAA[55]
						if (fullAddr16 == 0x5555 && value == 0x10) {
							// full chip erase
							memset(mCARTROM.data(), 0xFF, mCARTROM.size());
						} else if (value == 0x30) {
							// sector erase
							if (mCartMode == kATCartridgeMode_MaxFlash_1024K) {
								fullAddr &= 0xF0000;
								memset(mCARTROM.data() + fullAddr, 0xFF, 0x10000);
							} else {
								fullAddr &= 0x1C000;
								memset(mCARTROM.data() + fullAddr, 0xFF, 0x4000);
							}
						}

						mbDirty = true;
						if (mpUIRenderer)
							mpUIRenderer->SetFlashWriteActivity();

						mCommandPhase = 0;
						//mFlashReadMode = kFlashReadMode_WriteStatus;
						mFlashReadMode = kFlashReadMode_Normal;
						return true;

					case 6:		// 5555[AA] 2AAA[55] 5555[A0]
						mCARTROM[fullAddr] &= value;
						mbDirty = true;
						if (mpUIRenderer)
							mpUIRenderer->SetFlashWriteActivity();

						mCommandPhase = 0;
						//mFlashReadMode = kFlashReadMode_WriteStatus;
						mFlashReadMode = kFlashReadMode_Normal;
						return true;
				}
			}
			break;

		case kATCartridgeMode_Corina_1M_EEPROM:
			// We don't emulate write times at the moment.
			if (mCartBank == 64) {
				mbDirty = true;
				if (mpUIRenderer)
					mpUIRenderer->SetFlashWriteActivity();

				mCARTROM[0x100000 + (address & 0x1fff)] = value;
				return true;
			}
			break;

		case kATCartridgeMode_Corina_512K_SRAM_EEPROM:
			// We don't emulate write times at the moment.
			if (mCartBank == 64) {
				mbDirty = true;
				if (mpUIRenderer)
					mpUIRenderer->SetFlashWriteActivity();

				mCARTROM[0x80000 + (address & 0x1fff)] = value;
				return true;
			}
			break;

		case kATCartridgeMode_TelelinkII:
			if (address < 0xA000)
				mCARTRAM[address & 0xFF] = value | 0xF0;
			break;
	}

	return false;
}

bool ATCartridgeEmulator::ReadByteD5(uint16 address, uint8& v) {
	v = 0xFF;

	switch(mCartMode) {
		case kATCartridgeMode_SuperCharger3D:
			v = mSC3D[address & 3];
			break;

		case kATCartridgeMode_TelelinkII:
			if (address & 1) {
				// initiate array load
				for(int i=0; i<256; ++i)
					mCARTRAM[i] = mCARTROM[8192 + i] | 0xF0;
			}
			break;

		case kATCartridgeMode_Williams_64K:
			{
				int bank = -1;

				if (address < 0xD508)
					bank = address & 7;

				if (mCartBank != bank) {
					mCartBank = bank;
					return true;
				}
			}
			break;

		case kATCartridgeMode_Williams_32K:
			{
				int bank = -1;

				if (address < 0xD508)
					bank = address & 3;

				if (mCartBank != bank) {
					mCartBank = bank;
					return true;
				}
			}
			break;

		case kATCartridgeMode_Diamond_64K:
			if ((uint32)(address - 0xD5D0) < 0x10){
				int bank = -1;

				if (address < 0xD5D8)
					bank = ~address & 7;

				if (mCartBank != bank) {
					mCartBank = bank;
					return true;
				}
			}
			break;

		case kATCartridgeMode_Express_64K:
			if ((uint32)(address - 0xD570) < 0x10){
				int bank = -1;

				if (address < 0xD578)
					bank = ~address & 7;

				if (mCartBank != bank) {
					mCartBank = bank;
					return true;
				}
			}
			break;

		case kATCartridgeMode_SpartaDosX_64K:
			if ((address & 0xFFF0) == 0xD5E0) {
				int bank = -1;
				if (!(address & 8))
					bank = ~address & 7;

				if (mCartBank != bank) {
					mCartBank = bank;
					return true;
				}
			}
			break;

		case kATCartridgeMode_SpartaDosX_128K:
			if ((address & 0xFFE0) == 0xD5E0) {
				int bank = -1;
				if (!(address & 8)) {
					bank = ~address & 7;

					if (!(address & 0x10))
						bank += 8;
				}

				if (mCartBank != bank) {
					mCartBank = bank;
					return true;
				}
			}
			break;

		case kATCartridgeMode_OSS_034M:
			{
				static const sint8 kBankLookup[16] = {0, 0, 4, 1, 2, 2, 4, 1, -1, -1, -1, -1, -1, -1, -1, -1};

				mCartBank = kBankLookup[(uint8)address & 15];
			}
			return true;

		case kATCartridgeMode_OSS_M091:
			switch((uint8)address & 9) {
				case 0:
					mCartBank = 1;
					break;
				case 1:
					mCartBank = 3;
					break;
				case 8:
					mCartBank = -1;
					break;
				case 9:
					mCartBank = 2;
					break;
			}
			return true;

		case kATCartridgeMode_DB_32K:
			{
				int bank = address & 3;

				if (mCartBank != bank) {
					mCartBank = bank;
					return true;
				}
			}
			break;

		case kATCartridgeMode_Phoenix_8K:
		case kATCartridgeMode_Blizzard_16K:
			if (mCartBank >= 0) {
				mCartBank = -1;
				return true;
			}
			break;
	}

	return false;
}

bool ATCartridgeEmulator::WriteByteD5(uint16 addr, uint8 value) {
	switch(mCartMode) {
		case kATCartridgeMode_8K:
		case kATCartridgeMode_16K:
			break;

		case kATCartridgeMode_XEGS_32K:
			mCartBank = value & 3;
			return true;

		case kATCartridgeMode_XEGS_64K:
			mCartBank = value & 7;
			return true;

		case kATCartridgeMode_XEGS_128K:
			mCartBank = value & 15;
			return true;

		case kATCartridgeMode_XEGS_256K:
			mCartBank = value & 31;
			return true;

		case kATCartridgeMode_XEGS_512K:
			mCartBank = value & 63;
			return true;

		case kATCartridgeMode_XEGS_1M:
			mCartBank = value & 127;
			return true;

		case kATCartridgeMode_Switchable_XEGS_32K:
			if (value & 0x80)
				mCartBank = -1;
			else
				mCartBank = value & 3;
			return true;

		case kATCartridgeMode_Switchable_XEGS_64K:
			if (value & 0x80)
				mCartBank = -1;
			else
				mCartBank = value & 7;
			return true;

		case kATCartridgeMode_Switchable_XEGS_128K:
			if (value & 0x80)
				mCartBank = -1;
			else
				mCartBank = value & 15;
			return true;

		case kATCartridgeMode_Switchable_XEGS_256K:
			if (value & 0x80)
				mCartBank = -1;
			else
				mCartBank = value & 31;
			return true;

		case kATCartridgeMode_Switchable_XEGS_512K:
			if (value & 0x80)
				mCartBank = -1;
			else
				mCartBank = value & 63;
			return true;

		case kATCartridgeMode_Switchable_XEGS_1M:
			if (value & 0x80)
				mCartBank = -1;
			else
				mCartBank = value;
			return true;

		case kATCartridgeMode_MaxFlash_128K:
			if (addr < 0xD520) {
				if (addr < 0xD510)
					mCartBank = addr & 15;
				else
					mCartBank = -1;
			}
			return true;

		case kATCartridgeMode_MaxFlash_128K_MyIDE:
			if (addr >= 0xD520 && addr < 0xD540) {
				if (addr & 0x10)
					mCartBank = -1;
				else
					mCartBank = addr & 15;
			}
			return true;

		case kATCartridgeMode_MaxFlash_1024K:
			if (addr < 0xD580)
				mCartBank = addr & 127;
			else
				mCartBank = -1;
			return true;

		case kATCartridgeMode_MegaCart_16K:
			if (value & 0x80)
				mCartBank = -1;
			else
				mCartBank = 0;
			return true;

		case kATCartridgeMode_MegaCart_32K:
			if (value & 0x80)
				mCartBank = -1;
			else
				mCartBank = value & 1;
			return true;

		case kATCartridgeMode_MegaCart_64K:
			if (value & 0x80)
				mCartBank = -1;
			else
				mCartBank = value & 3;
			return true;

		case kATCartridgeMode_MegaCart_128K:
			if (value & 0x80)
				mCartBank = -1;
			else
				mCartBank = value & 7;
			return true;

		case kATCartridgeMode_MegaCart_256K:
			if (value & 0x80)
				mCartBank = -1;
			else
				mCartBank = value & 15;
			return true;

		case kATCartridgeMode_MegaCart_512K:
			if (value & 0x80)
				mCartBank = -1;
			else
				mCartBank = value & 31;
			return true;

		case kATCartridgeMode_MegaCart_1M:
			if (value & 0x80)
				mCartBank = -1;
			else
				mCartBank = value & 63;
			return true;

		case kATCartridgeMode_SuperCharger3D:
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

			{
				int idx = (int)addr & 3;

				if (idx < 3)
					mSC3D[idx] = value;
				else {
					if (value == 1) {
						uint32 d = ((uint32)mSC3D[1] << 8) + (uint32)mSC3D[2];

						if (mSC3D[1] >= mSC3D[0]) {
							mSC3D[3] = 1;
						} else {
							mSC3D[2] = (uint8)(d / (uint32)mSC3D[0]);
							mSC3D[1] = (uint8)(d % (uint32)mSC3D[0]);
							mSC3D[3] = 0;
						}
					} else if (value == 2) {
						uint32 result = (uint32)mSC3D[2] * (uint32)mSC3D[0];

						mSC3D[1] = (uint8)(result >> 8);
						mSC3D[2] = (uint8)result;
						mSC3D[3] = 0;
					} else {
						mSC3D[3] = 1;
					}
				}
			}
			break;

		case kATCartridgeMode_OSS_034M:
			{
				static const sint8 kBankLookup[16] = {0, 0, 4, 1, 2, 2, 4, 1, -1, -1, -1, -1, -1, -1, -1, -1};

				mCartBank = kBankLookup[(uint8)addr & 15];
			}
			return true;

		case kATCartridgeMode_OSS_M091:
			switch((uint8)addr & 9) {
				case 0:
					mCartBank = 1;
					break;
				case 1:
					mCartBank = 3;
					break;
				case 8:
					mCartBank = -1;
					break;
				case 9:
					mCartBank = 2;
					break;
			}
			return true;

		case kATCartridgeMode_Corina_1M_EEPROM:
		case kATCartridgeMode_Corina_512K_SRAM_EEPROM:
			if (addr == 0xD500) {
				if (value & 0x80) {
					// D7=1 disables cartridge.
					mCartBank = -1;
				} else {
					// D7=0 enables cartridge.
					switch(value & 0x60) {
						case 0x00:	// 000xxxxx -> ROM (banks 0-31)
						case 0x20:	// 001xxxxx -> ROM/SRAM (banks 32-63)
							mCartBank = value;
							break;
						case 0x40:	// 10xxxxxx -> EEPROM
							mCartBank = 64;
							break;
						case 0x60:	// 11xxxxxx -> reserved
							mCartBank = -1;
							break;
					}
				}
				return true;
			}

			break;

		case kATCartridgeMode_SpartaDosX_64K:
			if ((addr & 0xFFF0) == 0xD5E0) {
				int bank = -1;
				if (!(addr & 8))
					bank = ~addr & 7;

				if (mCartBank != bank) {
					mCartBank = bank;
					return true;
				}
			}
			break;

		case kATCartridgeMode_SpartaDosX_128K:
			if ((addr & 0xFFE0) == 0xD5E0) {
				int bank = -1;
				if (!(addr & 8)) {
					bank = ~addr & 7;

					if (!(addr & 0x10))
						bank += 8;
				}

				if (mCartBank != bank) {
					mCartBank = bank;
					return true;
				}
			}
			break;

		case kATCartridgeMode_TelelinkII:
			// initiate NV store
			memcpy(mCARTROM.data() + 8192, mCARTRAM.data(), 256);
			mbDirty = true;
			if (mpUIRenderer)
				mpUIRenderer->SetFlashWriteActivity();
			break;

		case kATCartridgeMode_Williams_64K:
			{
				int bank = -1;

				if (addr < 0xD508)
					bank = addr & 7;

				if (mCartBank != bank) {
					mCartBank = bank;
					return true;
				}
			}
			break;

		case kATCartridgeMode_Williams_32K:
			{
				int bank = -1;

				if (addr < 0xD508)
					bank = addr & 3;

				if (mCartBank != bank) {
					mCartBank = bank;
					return true;
				}
			}
			break;

		case kATCartridgeMode_Diamond_64K:
			if ((uint32)(addr - 0xD5D0) < 0x10){
				int bank = -1;

				if (addr < 0xD5D8)
					bank = ~addr & 7;

				if (mCartBank != bank) {
					mCartBank = bank;
					return true;
				}
			}
			break;

		case kATCartridgeMode_Express_64K:
			if ((uint32)(addr - 0xD570) < 0x10){
				int bank = -1;

				if (addr < 0xD578)
					bank = ~addr & 7;

				if (mCartBank != bank) {
					mCartBank = bank;
					return true;
				}
			}
			break;

		case kATCartridgeMode_Atrax_128K:
			{
				int bank = -1;

				if (value < 0x80)
					bank = value & 15;

				if (mCartBank != bank) {
					mCartBank = bank;
					return true;
				}
			}
			break;

		case kATCartridgeMode_DB_32K:
			{
				int bank = addr & 3;

				if (mCartBank != bank) {
					mCartBank = bank;
					return true;
				}
			}
			break;

		case kATCartridgeMode_Phoenix_8K:
		case kATCartridgeMode_Blizzard_16K:
			if (mCartBank >= 0) {
				mCartBank = -1;
				return true;
			}
			break;
	}

	return false;
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
