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
#include <vd2/system/vdstl.h>
#include "cartridge.h"
#include "savestate.h"
#include "options.h"
#include "oshelper.h"
#include "resource.h"
#include "simulator.h"
#include "uirender.h"

namespace {
	void DeinterleaveAtraxSDX64K(uint8 *p) {
		// At -> Epr
		// D0 -> D4
		// D1 -> D0
		// D2 -> D5
		// D3 -> D1
		// D4 -> D7
		// D5 -> D6
		// D6 -> D3
		// D7 -> D2
		//
		// A0 -> A6
		// A1 -> A7
		// A2 -> A12
		// A3 -> A15
		// A4 -> A14
		// A5 -> A13
		// A6 -> A8
		// A7 -> A5
		// A8 -> A4
		// A9 -> A3
		// A10 -> A0
		// A11 -> A1
		// A12 -> A2
		// A13 -> A9
		// A14 -> A11
		// A15 -> A10
		// A16 -> A16

		vdblock<uint8> src(131072);
		memcpy(src.data(), p, 131072);

		uint8 dtab[256];
		uint16 alotab[256];
		uint16 ahitab[256];

		for(int i=0; i<256; ++i) {
			uint8 d = 0;
			uint16 alo = 0;
			uint16 ahi = 0;

			if (i & 0x01) { d += 0x02; alo += 0x0040; ahi += 0x0010; }
			if (i & 0x02) { d += 0x08; alo += 0x0080; ahi += 0x0008; }
			if (i & 0x04) { d += 0x80; alo += 0x1000; ahi += 0x0001; }
			if (i & 0x08) { d += 0x40; alo += 0x8000; ahi += 0x0002; }
			if (i & 0x10) { d += 0x01; alo += 0x4000; ahi += 0x0004; }
			if (i & 0x20) { d += 0x04; alo += 0x2000; ahi += 0x0200; }
			if (i & 0x40) { d += 0x20; alo += 0x0100; ahi += 0x0800; }
			if (i & 0x80) { d += 0x10; alo += 0x0020; ahi += 0x0400; }

			dtab[i] = d;
			alotab[i] = alo;
			ahitab[i] = ahi;
		}

		for(int i=0; i<65536; ++i) {
			p[i] = dtab[src[alotab[i & 0xff] + ahitab[i >> 8]]];
		}
	}
}

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
		case 44: return kATCartridgeMode_OSS_8K;
		case 45: return kATCartridgeMode_OSS_043M;
		case 46: return kATCartridgeMode_Blizzard_4K;
		case 47: return kATCartridgeMode_AST_32K;
		case 48: return kATCartridgeMode_Atrax_SDX_64K;
		case 49: return kATCartridgeMode_Atrax_SDX_128K;
		case 50: return kATCartridgeMode_Turbosoft_64K;
		case 51: return kATCartridgeMode_Turbosoft_128K;
		case 52: return kATCartridgeMode_MicroCalc;
		case 53: return kATCartridgeMode_RightSlot_8K;
		case 54: return kATCartridgeMode_SIC;
		case 55: return kATCartridgeMode_SIC;
		case 56: return kATCartridgeMode_SIC;
		case 57: return kATCartridgeMode_2K;
		case 58: return kATCartridgeMode_4K;
		case 59: return kATCartridgeMode_RightSlot_4K;
			// TODO: Get info for 60-62
		case 61: return kATCartridgeMode_MegaMax_2M;
		case 62: return kATCartridgeMode_TheCart_128M;
		case 63: return kATCartridgeMode_MegaCart_4M_3;
		case 64: return kATCartridgeMode_MegaCart_2M_3;
		case 65: return kATCartridgeMode_TheCart_32M;
		case 66: return kATCartridgeMode_TheCart_64M;
		default:
			return kATCartridgeMode_None;
	}
}

int ATGetCartridgeMapperForMode(int mode, uint32 size) {
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
		case kATCartridgeMode_OSS_8K: return 44;
		case kATCartridgeMode_OSS_043M: return 45;
		case kATCartridgeMode_Blizzard_4K: return 46;
		case kATCartridgeMode_AST_32K: return 47;
		case kATCartridgeMode_Atrax_SDX_64K: return 48;
		case kATCartridgeMode_Atrax_SDX_128K: return 49;
		case kATCartridgeMode_Turbosoft_64K: return 50;
		case kATCartridgeMode_Turbosoft_128K: return 51;
		case kATCartridgeMode_MicroCalc: return 52;

		case kATCartridgeMode_SIC:
			if (size > 256*1024)
				return 56;
			else if (size > 128*1024)
				return 55;

			return 54;

		case kATCartridgeMode_2K:
			return 57;

		case kATCartridgeMode_4K:
			return 58;

		case kATCartridgeMode_RightSlot_4K:
			return 59;

		case kATCartridgeMode_MegaMax_2M:		return 61;
		case kATCartridgeMode_TheCart_128M:		return 62;
		case kATCartridgeMode_MegaCart_4M_3:	return 63;
		case kATCartridgeMode_MegaCart_2M_3:	return 64;
		case kATCartridgeMode_TheCart_32M:		return 65;
		case kATCartridgeMode_TheCart_64M:		return 66;

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
		case kATCartridgeMode_5200_64K_32KBanks:
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
	, mpScheduler(NULL)
	, mpMemLayerFixedBank1(NULL)
	, mpMemLayerFixedBank2(NULL)
	, mpMemLayerVarBank1(NULL)
	, mpMemLayerVarBank2(NULL)
	, mpMemLayerSpec1(NULL)
	, mpMemLayerSpec2(NULL)
	, mpMemLayerControl(NULL)
	, mCartSize(0)
{
}

ATCartridgeEmulator::~ATCartridgeEmulator() {
	Shutdown();
}

void ATCartridgeEmulator::Init(ATMemoryManager *memman, ATScheduler *sch, int basePri, bool fastBus) {
	mpMemMan = memman;
	mpScheduler = sch;
	mBasePriority = basePri;
	mbFastBus = fastBus;
}

void ATCartridgeEmulator::Shutdown() {
	ShutdownMemoryLayers();
	mpScheduler = NULL;
	mpMemMan = NULL;
}

void ATCartridgeEmulator::SetUIRenderer(IATUIRenderer *r) {
	mpUIRenderer = r;
}

void ATCartridgeEmulator::SetFastBus(bool fastBus) {
	if (mbFastBus == fastBus)
		return;

	mbFastBus = fastBus;

	UpdateLayerBuses();
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
		case kATCartridgeMode_MaxFlash_1024K_Bank0:
			return false;
	}

	return true;
}

void ATCartridgeEmulator::Load5200Default() {
	mCartMode = kATCartridgeMode_5200_4K;
	mCartBank = 0;
	mInitialCartBank = 0;
	mImagePath.clear();
	mCARTROM.resize(0x1000, 0xFF);
	mCartSize = 0x1000;
	ATLoadKernelResource(IDR_NOCARTRIDGE, mCARTROM.data(), 0, 4096);
	mbDirty = false;

	InitMemoryLayers();
}

void ATCartridgeEmulator::LoadNewCartridge(ATCartridgeMode mode) {
	Unload();

	mCartMode = mode;
	mImagePath.clear();
	mbDirty = false;
	mInitialCartBank = -1;
	mInitialCartBank2 = -1;

	switch(mode) {
		case kATCartridgeMode_SuperCharger3D:
			mCartSize = 0;
			break;

		case kATCartridgeMode_MaxFlash_128K:
		case kATCartridgeMode_MaxFlash_128K_MyIDE:
			mInitialCartBank = 0;
			mCartSize = 0x20000;
			break;

		case kATCartridgeMode_MaxFlash_1024K:
		case kATCartridgeMode_MaxFlash_1024K_Bank0:
			mInitialCartBank = 127;
			mCartSize = 0x100000;
			break;

		case kATCartridgeMode_SIC:
			mInitialCartBank = 0;
			mCartSize = 0x80000;
			break;

		case kATCartridgeMode_MegaCart_512K_3:
			mCartSize = 0x80000;
			mInitialCartBank = 0;
			break;

		case kATCartridgeMode_MegaCart_2M_3:
			mCartSize = 0x200000;
			mInitialCartBank = 0;
			break;

		case kATCartridgeMode_MegaCart_4M_3:
			mCartSize = 0x400000;
			mInitialCartBank = 0xFE;
			break;

		case kATCartridgeMode_TheCart_32M:
			mCartSize = 0x2000000;
			mInitialCartBank = 0;
			break;

		case kATCartridgeMode_TheCart_64M:
			mCartSize = 0x4000000;
			mInitialCartBank = 0;
			break;

		case kATCartridgeMode_TheCart_128M:
			mCartSize = 0x8000000;
			mInitialCartBank = 0;
			break;
	}

	mCartBank = mInitialCartBank;
	mCartBank2 = mInitialCartBank2;

	mCARTROM.resize(mCartSize, 0xFF);

	InitMemoryLayers();
	ColdReset();
}

bool ATCartridgeEmulator::Load(const wchar_t *s, ATCartLoadContext *loadCtx) {
	VDFileStream f(s);

	return Load(s, f, loadCtx);
}

bool ATCartridgeEmulator::Load(const wchar_t *origPath, IVDRandomAccessStream& f, ATCartLoadContext *loadCtx) {
	sint64 size = f.Length();

	if (size < 1024 || size > 128*1024*1024 + 16)
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
		mCartSize = size32;
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

			// If the cartridge isn't too big, capture it.
			if (loadCtx->mpCaptureBuffer && size32 <= 1048576 + 8192) {
				loadCtx->mpCaptureBuffer->resize(size32);
				f.Seek(0);
				f.Read(loadCtx->mpCaptureBuffer->data(), size32);
			}
			return false;
		}

		mCARTROM.resize(size32);
		mCartSize = size32;
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

		case kATCartridgeMode_MaxFlash_1024K_Bank0:
			mInitialCartBank = 0;
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
			mInitialCartBank2 = 0;
			allocSize = 8192;
			break;

		case kATCartridgeMode_Blizzard_4K:
			mInitialCartBank = 0;
			allocSize = 4096;
			break;

		case kATCartridgeMode_Blizzard_16K:
			mInitialCartBank = 0;
			allocSize = 16384;
			break;

		case kATCartridgeMode_SIC:
			mInitialCartBank = 0;
			allocSize = 0x80000;
			break;

		case kATCartridgeMode_Atrax_SDX_64K:
			mInitialCartBank = 0;
			allocSize = 65536;
			break;

		case kATCartridgeMode_Atrax_SDX_128K:
			mInitialCartBank = 0;
			allocSize = 131072;
			break;

		case kATCartridgeMode_OSS_043M:
			mInitialCartBank = 2;
			mInitialCartBank2 = 3;
			allocSize = 16384;
			break;

		case kATCartridgeMode_OSS_8K:
			mInitialCartBank = 1;
			mInitialCartBank2 = 0;
			allocSize = 8192;
			break;

		case kATCartridgeMode_AST_32K:
			mInitialCartBank = 0;
			mInitialCartBank2 = 0;
			allocSize = 32768;
			break;

		case kATCartridgeMode_Turbosoft_64K:
			mInitialCartBank = 0;
			allocSize = 65536;
			break;

		case kATCartridgeMode_Turbosoft_128K:
			mInitialCartBank = 0;
			allocSize = 131072;
			break;

		case kATCartridgeMode_MegaCart_1M_2:
			mInitialCartBank = 0;
			allocSize = 0x100000;
			break;

		case kATCartridgeMode_MegaCart_512K_3:
			mInitialCartBank = 0;
			allocSize = 0x80000;
			break;

		case kATCartridgeMode_MegaCart_2M_3:
			mInitialCartBank = 0;
			allocSize = 0x200000;
			break;

		case kATCartridgeMode_MegaCart_4M_3:
			mInitialCartBank = 0xFE;
			allocSize = 0x400000;
			break;

		case kATCartridgeMode_5200_64K_32KBanks:
			mInitialCartBank = 0;
			allocSize = 0x10000;
			break;

		case kATCartridgeMode_MicroCalc:
			mInitialCartBank = 0;
			allocSize = 0x8000;
			break;

		case kATCartridgeMode_TheCart_32M:
			mInitialCartBank = 0;
			mInitialCartBank2 = -1;
			allocSize = 32*1024*1024;
			break;

		case kATCartridgeMode_TheCart_64M:
			mInitialCartBank = 0;
			mInitialCartBank2 = -1;
			allocSize = 64*1024*1024;
			break;

		case kATCartridgeMode_TheCart_128M:
			mInitialCartBank = 0;
			mInitialCartBank2 = -1;
			allocSize = 128*1024*1024;
			break;

		case kATCartridgeMode_MegaMax_2M:
			allocSize = 0x200000;
			mInitialCartBank = 0;
			break;
	}

	mCartBank = mInitialCartBank;
	mCartBank2 = mInitialCartBank2;

	if (mCartMode == kATCartridgeMode_TelelinkII)
		mCARTROM.resize(allocSize, 0xFF);
	else
		mCARTROM.resize(allocSize, 0);

	mCartSize = allocSize;

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
	} else if (mCartMode == kATCartridgeMode_Atrax_SDX_64K) {
		uint8 *p = mCARTROM.data();

		DeinterleaveAtraxSDX64K(p);
	} else if (mCartMode == kATCartridgeMode_Atrax_SDX_128K) {
		uint8 *p = mCARTROM.data();

		DeinterleaveAtraxSDX64K(p);
		DeinterleaveAtraxSDX64K(p+65536);
	} else if (mCartMode == kATCartridgeMode_OSS_034M) {
		mCARTROM.resize(0x7000);

		uint8 *p = mCARTROM.data();

		memset(p + 0x4000, 0xFF, 0x1000);

		for(int i=0; i<0x1000; ++i)
			p[i + 0x5000] = p[i] & p[i + 0x1000];

		for(int i=0; i<0x1000; ++i)
			p[i + 0x6000] = p[i + 0x2000] & p[i + 0x1000];
	} else if (mCartMode == kATCartridgeMode_OSS_043M) {
		mCARTROM.resize(0x7000);

		uint8 *p = mCARTROM.data();

		memset(p + 0x4000, 0xFF, 0x1000);

		for(int i=0; i<0x1000; ++i)
			p[i + 0x5000] = p[i] & p[i + 0x2000];

		for(int i=0; i<0x1000; ++i)
			p[i + 0x6000] = p[i + 0x2000] & p[i + 0x1000];
	}

	// shift/realign images
	switch(mCartMode) {
	case kATCartridgeMode_2K:
		// Shift the ROM image so that the bottom 6K is open ($FF) and the image
		// resides in the top 2K.
		mCARTROM.resize(8192);
		memcpy(&mCARTROM[6144], &mCARTROM[0], 2048);
		memset(&mCARTROM[0], 0xFF, 6144);
		break;
	case kATCartridgeMode_4K:
	case kATCartridgeMode_RightSlot_4K:
		// Shift the ROM image so that the bottom 4K is open ($FF) and the image
		// resides in the top 4K.
		mCARTROM.resize(8192);
		memcpy(&mCARTROM[4096], &mCARTROM[0], 4096);
		memset(&mCARTROM[0], 0xFF, 4096);
		break;
	}

	// initialize RAM
	switch(mCartMode) {
		case kATCartridgeMode_Corina_512K_SRAM_EEPROM:
			mCARTRAM.resize(524288, 0);
			break;
		case kATCartridgeMode_TelelinkII:
			mCARTRAM.clear();
			mCARTRAM.resize(256, 0xFF);
			break;

		case kATCartridgeMode_TheCart_32M:
		case kATCartridgeMode_TheCart_64M:
		case kATCartridgeMode_TheCart_128M:
			mCARTRAM.clear();
			mCARTRAM.resize(524288, 0);
			break;
	}

	if (origPath)
		mImagePath = origPath;
	else
		mImagePath.clear();

	if (loadCtx) {
		loadCtx->mCartMapper = mCartMode;
		loadCtx->mLoadStatus = kATCartLoadStatus_Ok;
	}

	mbDirty = false;
	InitMemoryLayers();
	ColdReset();

	return true;
}

void ATCartridgeEmulator::Unload() {
	ShutdownMemoryLayers();

	vdfastvector<uint8>().swap(mCARTROM);
	vdfastvector<uint8>().swap(mCARTRAM);

	mInitialCartBank = 0;
	mCartBank = 0;
	mCartMode = kATCartridgeMode_None;

	mImagePath.clear();
	mbDirty = false;
}

void ATCartridgeEmulator::Save(const wchar_t *fn, bool includeHeader) {
	int type = 0;

	if (includeHeader) {
		type = ATGetCartridgeMapperForMode(mCartMode, (uint32)mCARTROM.size());

		if (!type)
			throw MyError("This cartridge type is not supported in the .CAR file format and must be saved as a raw image.");
	}

	VDFile f(fn, nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);

	uint32 size = mCartSize;
	const uint8 *src = mCARTROM.data();

	// apply adjustment for cartridge types that we pad the ROM image for
	switch(mCartMode) {
		case kATCartridgeMode_2K:
			size = 2048;
			src += 6144;
			break;

		case kATCartridgeMode_4K:
		case kATCartridgeMode_RightSlot_4K:
			size = 4096;
			src += 4096;
			break;
	}

	// write header
	if (includeHeader) {
		char header[16] = { 'C', 'A', 'R', 'T' };

		VDWriteUnalignedBEU32(header + 4, type);

		uint32 checksum = 0;
		for(uint32 i=0; i<size; ++i)
			checksum += src[i];

		VDWriteUnalignedBEU32(header + 8, checksum);

		f.write(header, 16);
	}

	f.write(src, size);
	mbDirty = false;
}

void ATCartridgeEmulator::ColdReset() {
	mCartBank = mInitialCartBank;
	mCartBank2 = mInitialCartBank2;
	UpdateCartBank();

	if (mpMemLayerVarBank2)
		UpdateCartBank2();

	switch(mCartMode) {
		case kATCartridgeMode_SuperCharger3D:
			memset(mSC3D, 0xFF, sizeof mSC3D);
			break;

		case kATCartridgeMode_TheCart_32M:
		case kATCartridgeMode_TheCart_64M:
		case kATCartridgeMode_TheCart_128M:
			static const uint8 kTheCartInitData[]={
				0x00,
				0x00,
				0x01,
				0x00,
				0x00,
				0x00,
				0x01,
				0x00,
				0x82,	// /CS high, SI high, SCK low
			};

			memcpy(mTheCartRegs, kTheCartInitData, sizeof mTheCartRegs);
			mbTheCartConfigLock = false;
			mTheCartSICEnables = 0x40;		// Axxx only
			mTheCartOSSBank = 0;

			UpdateTheCartBanking();
			UpdateTheCart();
			break;
	}

	mFlashEmu.ColdReset();
	mFlashEmu2.ColdReset();
	mEEPROM.ColdReset();
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

sint32 ATCartridgeEmulator::ReadByte_SIC(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	const uint32 fullAddr = (thisptr->mCartBank & 0x1f) * 0x4000 + (address & 0x3fff);

	uint8 value;
	if (thisptr->mFlashEmu.ReadByte(fullAddr, value)) {
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, false);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_CPURead, false);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec2, kATMemoryAccessMode_AnticRead, false);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec2, kATMemoryAccessMode_CPURead, false);
	}

	return value;
}

bool ATCartridgeEmulator::WriteByte_SIC(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	const uint32 fullAddr = (thisptr->mCartBank & 0x1f) * 0x4000 + (address & 0x3fff);

	if (thisptr->mFlashEmu.WriteByte(fullAddr, value)) {
		if (thisptr->mFlashEmu.CheckForWriteActivity()) {
			thisptr->mpUIRenderer->SetFlashWriteActivity();

			if (!thisptr->mbDirty && thisptr->mFlashEmu.IsDirty())
				thisptr->mbDirty = true;
		}

		const bool flashRead = thisptr->mFlashEmu.IsControlReadEnabled();
		const bool flashReadBank1 = flashRead && (thisptr->mCartBank & 0x40);
		const bool flashReadBank2 = flashRead && (thisptr->mCartBank & 0x20);

		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, flashReadBank1);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_CPURead, flashReadBank1);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec2, kATMemoryAccessMode_AnticRead, flashReadBank2);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec2, kATMemoryAccessMode_CPURead, flashReadBank2);
	}

	return true;
}

sint32 ATCartridgeEmulator::ReadByte_TheCart(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	uint32 fullAddr = thisptr->mCartBank * 0x2000 + (address & 0x1fff);
	fullAddr &= thisptr->mCartSizeMask;

	uint8 value;
	if (thisptr->mFlashEmu.ReadByte(fullAddr, value)) {
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, false);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_CPURead, false);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec2, kATMemoryAccessMode_AnticRead, false);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec2, kATMemoryAccessMode_CPURead, false);
	}

	return value;
}

bool ATCartridgeEmulator::WriteByte_TheCart(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	uint32 fullAddr = (address & 0x2000 ? thisptr->mCartBank : thisptr->mCartBank2) * 0x2000 + (address & 0x1fff);
	fullAddr &= thisptr->mCartSizeMask;

	if (thisptr->mFlashEmu.WriteByte(fullAddr, value)) {
		if (thisptr->mFlashEmu.CheckForWriteActivity()) {
			thisptr->mpUIRenderer->SetFlashWriteActivity();

			if (!thisptr->mbDirty && thisptr->mFlashEmu.IsDirty())
				thisptr->mbDirty = true;
		}

		const bool flashRead = thisptr->mFlashEmu.IsControlReadEnabled();

		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, flashRead);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_CPURead, flashRead);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec2, kATMemoryAccessMode_AnticRead, flashRead);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec2, kATMemoryAccessMode_CPURead, flashRead);
	}

	return true;
}

sint32 ATCartridgeEmulator::ReadByte_MegaCart3(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	if (thisptr->mCartBank < 0)
		return -1;

	const uint32 fullAddr = (thisptr->mCartBank*0x4000 + (address & 0x3fff)) & ((uint32)thisptr->mCARTROM.size() - 1);

	uint8 value;
	if (thisptr->mFlashEmu.ReadByte(fullAddr, value)) {
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, false);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_CPURead, false);
	}

	return value;
}

bool ATCartridgeEmulator::WriteByte_MegaCart3(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	if (thisptr->mCartBank < 0)
		return false;

	const uint32 fullAddr = (thisptr->mCartBank*0x4000 + (address & 0x3fff)) & ((uint32)thisptr->mCARTROM.size() - 1);

	if (thisptr->mFlashEmu.WriteByte(fullAddr, value)) {
		if (thisptr->mFlashEmu.CheckForWriteActivity()) {
			thisptr->mpUIRenderer->SetFlashWriteActivity();

			if (!thisptr->mbDirty && thisptr->mFlashEmu.IsDirty())
				thisptr->mbDirty = true;
		}

		const bool flashRead = thisptr->mFlashEmu.IsControlReadEnabled();

		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, flashRead);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_CPURead, flashRead);
	}

	return true;
}

sint32 ATCartridgeEmulator::ReadByte_MaxFlash(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	if (thisptr->mCartBank < 0)
		return -1;

	const uint32 fullAddr = (thisptr->mCartBank*0x2000 + (address & 0x1fff)) & ((uint32)thisptr->mCARTROM.size() - 1);
	ATFlashEmulator& flashEmu = (fullAddr >= 0x80000 ? thisptr->mFlashEmu2 : thisptr->mFlashEmu);

	uint8 value;
	if (flashEmu.ReadByte(fullAddr & 0x7FFFF, value)) {
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, false);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_CPURead, false);
	}

	return value;
}

bool ATCartridgeEmulator::WriteByte_MaxFlash(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *thisptr = (ATCartridgeEmulator *)thisptr0;

	if (thisptr->mCartBank < 0)
		return false;

	const uint32 fullAddr = (thisptr->mCartBank*0x2000 + (address & 0x1fff)) & ((uint32)thisptr->mCARTROM.size() - 1);
	ATFlashEmulator& flashEmu = (fullAddr >= 0x80000 ? thisptr->mFlashEmu2 : thisptr->mFlashEmu);

	if (flashEmu.WriteByte(fullAddr & 0x7FFFF, value)) {
		if (flashEmu.CheckForWriteActivity()) {
			thisptr->mpUIRenderer->SetFlashWriteActivity();

			if (!thisptr->mbDirty && flashEmu.IsDirty())
				thisptr->mbDirty = true;
		}

		const bool flashRead = flashEmu.IsControlReadEnabled();

		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, flashRead);
		thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerSpec1, kATMemoryAccessMode_CPURead, flashRead);
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
sint32 ATCartridgeEmulator::ReadByte_CCTL_AddressToBank_Switchable(void *thisptr0, uint32 address) {
	((ATCartridgeEmulator *)thisptr0)->SetCartBank(address & 0x80 ? -1 : address & T_Mask);
	return 0xFF;
}

template<uint8 T_Mask>
bool ATCartridgeEmulator::WriteByte_CCTL_AddressToBank_Switchable(void *thisptr0, uint32 address, uint8 value) {
	((ATCartridgeEmulator *)thisptr0)->SetCartBank(address & 0x80 ? -1 : address & T_Mask);
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

// The!Cart registers:
//
// $D5A0: primary bank register low byte (0-255, default: 0)
// $D5A1: primary bank register high byte (0-63, default: 0)
// $D5A2: primary bank enable (1=enable, 0=disable, default: 1)
// $D5A3: secondary bank register low byte (0-255, default: 0)
// $D5A4: secondary bank register high byte (0-63, default: 0)
// $D5A5: secondary bank enable (1=enable, 0=disable, default: 0)
// $D5A6: cart mode select (see section on cartridge modes, default: 1 / 8k)
// $D5A7: flash/ram selection and write enable control (0-15, default: 0)
//   bit 0: primary bank write enable (0 = write protect, 1 = write enable)
//   bit 1: primary bank source (0 = flash, 1 = RAM)
//   bit 2: secondary bank write enable (0 = write protect, 1 = write enable)
//   bit 3: secondary bank source (0 = flash, 1 = RAM)
// 
// $D5A8: SPI interface to EEPROM
//   bit 0: SPI CLK
//   bit 1: SPI CS
//   bit 7: SPI data in (on reads), SPI data out (on writes)

sint32 ATCartridgeEmulator::ReadByte_CCTL_TheCart(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;
	address &= 0xff;

	if (address >= 0xA0 && address <= 0xAF && !thisptr->mbTheCartConfigLock) {
		if (address <= 0xA7)
			return thisptr->mTheCartRegs[address - 0xA0];
		else if (address == 0xA8)
			return (thisptr->mTheCartRegs[8] & 0x03) + (thisptr->mEEPROM.ReadState() ? 0x80 : 0x00);
		else
			return 0xFF;
	} else if (thisptr->mTheCartBankMode == kTheCartBankMode_SIC && address < 0x20) {
		return ((thisptr->mTheCartRegs[0] >> 1) & 0x1F) + (thisptr->mTheCartRegs[2] ? 0 : 0x40) + (thisptr->mTheCartRegs[5] ? 0x20 : 0);
	} else if (thisptr->mbTheCartBankByAddress) {
		WriteByte_CCTL_TheCart(thisptr0, address, 0);
		return 0xFF;
	}

	return -1;
}

bool ATCartridgeEmulator::WriteByte_CCTL_TheCart(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;
	address &= 0xff;

	if (address >= 0xA0 && address <= 0xAF && !thisptr->mbTheCartConfigLock) {
		if (address <= 0xA7) {
			static const uint8 kRegWriteMasks[]={
				0xFF,
				0x3F,
				0x01,
				0xFF,
				0x3F,
				0x01,
				0x3F,
				0x0F,
			};

			const int index = address - 0xA0;
			uint8& reg = thisptr->mTheCartRegs[index];

			value &= kRegWriteMasks[index];

			bool forceUpdate = false;

			switch(index) {
				case 0:
				case 1:
					// accessing the primary bank registers also enables the primary window
					if (!thisptr->mTheCartRegs[2]) {
						thisptr->mTheCartRegs[2] = 1;
						forceUpdate = true;
					}
					break;

				case 3:
				case 4:
					// accessing the secondary bank registers also enables the secondary window
					if (!thisptr->mTheCartRegs[5]) {
						thisptr->mTheCartRegs[5] = 1;
						forceUpdate = true;
					}
					break;
			}

			if (forceUpdate || reg != value) {
				reg = value;

				// check if we updated the banking mode register -- this is particularly expensive
				if (index == 6)
					thisptr->UpdateTheCartBanking();

				// must come after banking update
				thisptr->UpdateTheCart();
			}
		} else if (address == 0xA8) {
			value &= 0x83;
			
			if (thisptr->mTheCartRegs[8] != value) {
				thisptr->mTheCartRegs[8] = value;

				thisptr->mEEPROM.WriteState((value & 2) == 0, (value & 1) != 0, (value & 0x80) != 0);
			}
		} else if (address == 0xAF) {
			thisptr->mbTheCartConfigLock = true;
		}

		return true;
	} else {
		const sint16 bankInfo = thisptr->mTheCartBankInfo[thisptr->mbTheCartBankByAddress ? address : value];

		if (bankInfo >= 0) {
			// modify primary bank register
			const uint16 oldBank = VDReadUnalignedLEU16(thisptr->mTheCartRegs);
			const uint16 newBank = oldBank ^ ((oldBank ^ (uint16)bankInfo) & thisptr->mTheCartBankMask);
			const uint8 newEnable = bankInfo & 0x4000 ? 1 : 0;

			// argh... we could almost get away with a generic routine here, if it weren't
			// for SIC! and OSS. :(

			switch(thisptr->mTheCartBankMode) {
				case kTheCartBankMode_SIC:
					if (address < 0x20) {
						const uint8 newEnables = (bankInfo & 0x6000) >> 8;
						if (oldBank != newBank || thisptr->mTheCartSICEnables != newEnables) {
							VDWriteUnalignedLEU16(thisptr->mTheCartRegs, newBank);
							thisptr->mTheCartSICEnables = newEnables;

							thisptr->UpdateTheCart();
						}
					}
					break;

				case kTheCartBankMode_OSS:
					if (thisptr->mTheCartOSSBank != (uint8)bankInfo) {
						thisptr->mTheCartOSSBank = (uint8)bankInfo;

						thisptr->UpdateTheCart();
					}
					break;

				default:
					if (oldBank != newBank || thisptr->mTheCartRegs[2] != newEnable) {
						VDWriteUnalignedLEU16(thisptr->mTheCartRegs, newBank);
						thisptr->mTheCartRegs[2] = newEnable;

						thisptr->UpdateTheCart();
					}
					break;
			}

			return true;
		}
	}

	return false;
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

	static const sint8 kBankLookup[16] = {0, 5, 4, 1, 2, 6, 4, 1, -1, -1, -1, -1, -1, -1, -1, -1};
	thisptr->SetCartBank(kBankLookup[address]);
	thisptr->SetCartBank2(kBankLookup[address] >> 31);

	return -1;
}

bool ATCartridgeEmulator::WriteByte_CCTL_OSS_034M(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	address &= 15;

	static const sint8 kBankLookup[16] = {0, 5, 4, 1, 2, 6, 4, 1, -1, -1, -1, -1, -1, -1, -1, -1};
	thisptr->SetCartBank(kBankLookup[address]);
	thisptr->SetCartBank2(kBankLookup[address] >> 31);

	return true;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_OSS_043M(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	address &= 15;

	static const sint8 kBankLookup[16] = {0, 5, 4, 2, 1, 6, 4, 2, -1, -1, -1, -1, -1, -1, -1, -1};
	thisptr->SetCartBank(kBankLookup[address]);
	thisptr->SetCartBank2(kBankLookup[address] >> 31);

	return -1;
}

bool ATCartridgeEmulator::WriteByte_CCTL_OSS_043M(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	address &= 15;

	static const sint8 kBankLookup[16] = {0, 5, 4, 2, 1, 6, 4, 2, -1, -1, -1, -1, -1, -1, -1, -1};
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

sint32 ATCartridgeEmulator::ReadByte_CCTL_OSS_8K(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	address &= 9;

	static const sint8 kBankLookup[10] = {1, 1, 1, 1, 1, 1, 1, 1, -1, 0};
	thisptr->SetCartBank(kBankLookup[address]);
	thisptr->SetCartBank2(kBankLookup[address] >> 31);

	return -1;
}

bool ATCartridgeEmulator::WriteByte_CCTL_OSS_8K(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	address &= 9;

	static const sint8 kBankLookup[10] = {1, 1, 1, 1, 1, 1, 1, 1, -1, 0};
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

bool ATCartridgeEmulator::WriteByte_CCTL_AST_32K(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->SetCartBank(-1);
	thisptr->SetCartBank2((thisptr->mCartBank2 + 1) & 127);
	return true;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_Turbosoft_64K(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->SetCartBank((address & 16) ? -1 : (address & 7));
	return 0xFF;
}

bool ATCartridgeEmulator::WriteByte_CCTL_Turbosoft_64K(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->SetCartBank((address & 16) ? -1 : (address & 7));
	return true;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_Turbosoft_128K(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->SetCartBank((address & 16) ? -1 : (address & 15));
	return 0xFF;
}

bool ATCartridgeEmulator::WriteByte_CCTL_Turbosoft_128K(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->SetCartBank((address & 16) ? -1 : (address & 15));
	return true;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_5200_64K_32KBanks(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	uint8 addr8 = (uint8)address;

	if (addr8 >= 0xE0)
		thisptr->SetCartBank(1);
	else if (addr8 >= 0xD0)
		thisptr->SetCartBank((address >> 2) & 1);

	return thisptr->mCARTROM[address - 0x4000 + (thisptr->mCartBank << 15)];
}

bool ATCartridgeEmulator::WriteByte_CCTL_5200_64K_32KBanks(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	uint8 addr8 = (uint8)address;

	if (addr8 >= 0xE0)
		thisptr->SetCartBank(1);
	else if (addr8 >= 0xD0)
		thisptr->SetCartBank((address >> 2) & 1);

	return true;
}

namespace {
	static const sint8 kMicroCalcTab[]={ 0, 1, 2, 3, -1 };
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_MicroCalc(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->SetCartBank(kMicroCalcTab[thisptr->mCartBank + 1]);

	return 0xFF;
}

bool ATCartridgeEmulator::WriteByte_CCTL_MicroCalc(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->SetCartBank(kMicroCalcTab[thisptr->mCartBank + 1]);

	return true;
}

sint32 ATCartridgeEmulator::ReadByte_CCTL_MegaCart3(void *thisptr0, uint32 address) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	return (uint8)thisptr->mCartBank;
}

bool ATCartridgeEmulator::WriteByte_CCTL_MegaCart3(void *thisptr0, uint32 address, uint8 value) {
	ATCartridgeEmulator *const thisptr = (ATCartridgeEmulator *)thisptr0;

	thisptr->SetCartBank(value == 0xFF ? -1 : value);

	return true;
}

///////////////////////////////////////////////////////////////////////////

void ATCartridgeEmulator::BeginLoadState(ATSaveStateReader& reader) {
	reader.RegisterHandlerMethod(kATSaveStateSection_Private, VDMAKEFOURCC('C', 'A', 'R', 'T'), this, &ATCartridgeEmulator::LoadStatePrivate);
}

void ATCartridgeEmulator::LoadStatePrivate(ATSaveStateReader& reader) {
	ExchangeState(reader);
}

void ATCartridgeEmulator::EndLoadState(ATSaveStateReader& reader) {
	UpdateCartBank();
	UpdateCartBank2();
}

void ATCartridgeEmulator::BeginSaveState(ATSaveStateWriter& writer) {
	writer.RegisterHandlerMethod(kATSaveStateSection_Private, this, &ATCartridgeEmulator::SaveStatePrivate);	
}

void ATCartridgeEmulator::SaveStatePrivate(ATSaveStateWriter& writer) {
	writer.BeginChunk(VDMAKEFOURCC('C', 'A', 'R', 'T'));
	ExchangeState(writer);
	writer.EndChunk();
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
	sint32 bank1Mask = -1;
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
			fixedBase	= 0x40;
			fixedSize	= 0x80;
			fixedMask	= 0x0F;
			break;

		case kATCartridgeMode_5200_8K:
			fixedBase	= 0x40;
			fixedSize	= 0x80;
			fixedMask	= 0x1F;
			break;

		case kATCartridgeMode_5200_16K_TwoChip:
			fixedBase	= 0x40;
			fixedSize	= 0x40;
			fixedMask	= 0x1F;
			fixed2Base	= 0x80;
			fixed2Size	= 0x40;
			fixed2Offset= 0x2000;
			fixed2Mask	= 0x1F;
			break;

		case kATCartridgeMode_5200_16K_OneChip:
			fixedBase	= 0x40;
			fixedSize	= 0x80;
			fixedMask	= 0x3F;
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

		case kATCartridgeMode_2K:
		case kATCartridgeMode_4K:
		case kATCartridgeMode_8K:
			fixedBase	= 0xA0;
			fixedSize	= 0x20;
			break;

		case kATCartridgeMode_16K:
			fixedBase	= 0x80;
			fixedSize	= 0x40;
			break;

		case kATCartridgeMode_RightSlot_4K:
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
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_DataToBank_Switchable<0x03>;
			break;

		case kATCartridgeMode_Switchable_XEGS_64K:
			bank1Base	= 0x80;
			bank1Size	= 0x20;
			fixedBase	= 0xA0;
			fixedSize	= 0x20;
			fixedOffset	= 0x00E000;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_DataToBank_Switchable<0x07>;
			break;

		case kATCartridgeMode_Switchable_XEGS_128K:
			bank1Base	= 0x80;
			bank1Size	= 0x20;
			fixedBase	= 0xA0;
			fixedSize	= 0x20;
			fixedOffset	= 0x01E000;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_DataToBank_Switchable<0x0F>;
			break;

		case kATCartridgeMode_Switchable_XEGS_256K:
			bank1Base	= 0x80;
			bank1Size	= 0x20;
			fixedBase	= 0xA0;
			fixedSize	= 0x20;
			fixedOffset	= 0x03E000;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_DataToBank_Switchable<0x1F>;
			break;

		case kATCartridgeMode_Switchable_XEGS_512K:
			bank1Base	= 0x80;
			bank1Size	= 0x20;
			fixedBase	= 0xA0;
			fixedSize	= 0x20;
			fixedOffset	= 0x07E000;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_DataToBank_Switchable<0x3F>;
			break;

		case kATCartridgeMode_Switchable_XEGS_1M:
			bank1Base	= 0x80;
			bank1Size	= 0x20;
			fixedBase	= 0xA0;
			fixedSize	= 0x20;
			fixedOffset	= 0x0FE000;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_DataToBank_Switchable<0x7F>;
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
		case kATCartridgeMode_Atrax_SDX_128K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_SDX128;
			cctlhd.mpWriteHandler = WriteByte_CCTL_SDX128;
			break;

		case kATCartridgeMode_SpartaDosX_64K:
		case kATCartridgeMode_Atrax_SDX_64K:
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
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_MaxFlash_128K;
			cctlhd.mpWriteHandler = WriteByte_CCTL_MaxFlash_128K;
			spec1Base	= 0xA0;
			spec1Size	= 0x20;
			spec1hd.mpReadHandler = ReadByte_MaxFlash;
			spec1hd.mpWriteHandler = WriteByte_MaxFlash;
			spec1WriteEnabled = true;

			// 04072012 flasher expects mfr|device == 0x20 or 0x21
			mFlashEmu.Init(mCARTROM.data(), kATFlashType_Am29F010, mpScheduler);
			break;

		case kATCartridgeMode_MaxFlash_128K_MyIDE:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_MaxFlash_128K_MyIDE;
			cctlhd.mpWriteHandler = WriteByte_CCTL_MaxFlash_128K_MyIDE;
			spec1Base	= 0xA0;
			spec1Size	= 0x20;
			spec1hd.mpReadHandler = ReadByte_MaxFlash;
			spec1hd.mpWriteHandler = WriteByte_MaxFlash;
			spec1WriteEnabled = true;

			// 04072012 flasher expects mfr|device == 0x20 or 0x21
			mFlashEmu.Init(mCARTROM.data(), kATFlashType_M29F010B, mpScheduler);
			break;

		case kATCartridgeMode_MaxFlash_1024K:
		case kATCartridgeMode_MaxFlash_1024K_Bank0:
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
			mFlashEmu.Init(mCARTROM.data(), kATFlashType_Am29F040B, mpScheduler);
			mFlashEmu2.Init(mCARTROM.data() + 512*1024, kATFlashType_Am29F040B, mpScheduler);
			break;

		case kATCartridgeMode_Phoenix_8K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_Phoenix;
			break;

		case kATCartridgeMode_Blizzard_4K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			bank1Mask	= 0x0F;
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
			spec1hd.mpReadHandler = ReadByte_SIC;
			spec1hd.mpWriteHandler = WriteByte_SIC;
			spec1WriteEnabled = true;
			spec2Base	= 0x80;
			spec2Size	= 0x20;
			spec2hd.mpReadHandler = ReadByte_SIC;
			spec2hd.mpWriteHandler = WriteByte_SIC;

			if (g_ATOptions.mSICFlashChip == "SST39SF040")
				mFlashEmu.Init(mCARTROM.data(), kATFlashType_SST39SF040, mpScheduler);
			else
				mFlashEmu.Init(mCARTROM.data(), kATFlashType_Am29F040B, mpScheduler);
			break;

		case kATCartridgeMode_OSS_8K:
			bank1Base	= 0xA0;
			bank1Size	= 0x10;
			bank2Base	= 0xB0;
			bank2Size	= 0x10;
			usecctl		= true;
			usecctlread	= true;
			usecctlwrite= true;
			cctlhd.mpReadHandler = ReadByte_CCTL_OSS_8K;
			cctlhd.mpWriteHandler = WriteByte_CCTL_OSS_8K;
			break;

		case kATCartridgeMode_OSS_043M:
			bank1Base	= 0xA0;
			bank1Size	= 0x10;
			bank2Base	= 0xB0;
			bank2Size	= 0x10;
			usecctl		= true;
			usecctlread	= true;
			usecctlwrite= true;
			cctlhd.mpReadHandler = ReadByte_CCTL_OSS_043M;
			cctlhd.mpWriteHandler = WriteByte_CCTL_OSS_043M;
			break;

		case kATCartridgeMode_AST_32K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			bank1Mask	= 0x00;
			bank2Base	= 0xD5;
			bank2Size	= 0x01;
			usecctl		= true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_AST_32K;
			break;

		case kATCartridgeMode_Turbosoft_64K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_Turbosoft_64K;
			cctlhd.mpWriteHandler = WriteByte_CCTL_Turbosoft_64K;
			break;

		case kATCartridgeMode_Turbosoft_128K:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_Turbosoft_128K;
			cctlhd.mpWriteHandler = WriteByte_CCTL_Turbosoft_128K;
			break;

		case kATCartridgeMode_MegaCart_1M_2:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpWriteHandler = WriteByte_CCTL_DataToBank_Switchable<0x3F>;
			break;

		case kATCartridgeMode_MegaCart_512K_3:
			bank1Base	= 0x80;
			bank1Size	= 0x40;
			spec1Base	= 0x80;
			spec1Size	= 0x40;
			spec1hd.mpDebugReadHandler = ReadByte_MegaCart3;
			spec1hd.mpReadHandler = ReadByte_MegaCart3;
			spec1hd.mpWriteHandler = WriteByte_MegaCart3;
			spec1WriteEnabled = true;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpDebugReadHandler = ReadByte_CCTL_MegaCart3;
			cctlhd.mpReadHandler = ReadByte_CCTL_MegaCart3;
			cctlhd.mpWriteHandler = WriteByte_CCTL_MegaCart3;
			mFlashEmu.Init(mCARTROM.data(), kATFlashType_Am29F040B, mpScheduler);
			break;

		case kATCartridgeMode_MegaCart_2M_3:
			bank1Base	= 0x80;
			bank1Size	= 0x40;
			spec1Base	= 0x80;
			spec1Size	= 0x40;
			spec1hd.mpDebugReadHandler = ReadByte_MegaCart3;
			spec1hd.mpReadHandler = ReadByte_MegaCart3;
			spec1hd.mpWriteHandler = WriteByte_MegaCart3;
			spec1WriteEnabled = true;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpDebugReadHandler = ReadByte_CCTL_MegaCart3;
			cctlhd.mpReadHandler = ReadByte_CCTL_MegaCart3;
			cctlhd.mpWriteHandler = WriteByte_CCTL_MegaCart3;
			mFlashEmu.Init(mCARTROM.data(), kATFlashType_Am29F016D, mpScheduler);
			break;

		case kATCartridgeMode_MegaCart_4M_3:
			bank1Base	= 0x80;
			bank1Size	= 0x40;
			spec1Base	= 0x80;
			spec1Size	= 0x40;
			spec1hd.mpDebugReadHandler = ReadByte_MegaCart3;
			spec1hd.mpReadHandler = ReadByte_MegaCart3;
			spec1hd.mpWriteHandler = WriteByte_MegaCart3;
			spec1WriteEnabled = true;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpDebugReadHandler = ReadByte_CCTL_MegaCart3;
			cctlhd.mpReadHandler = ReadByte_CCTL_MegaCart3;
			cctlhd.mpWriteHandler = WriteByte_CCTL_MegaCart3;
			mFlashEmu.Init(mCARTROM.data(), kATFlashType_Am29F032B, mpScheduler);
			break;

		case kATCartridgeMode_5200_64K_32KBanks:
			bank1Base	= 0x40;
			bank1Size	= 0x80;
			spec1Base	= 0xBF;
			spec1Size	= 0x01;
			spec1ReadEnabled = true;
			spec1WriteEnabled = true;
			spec1hd.mpReadHandler = ReadByte_CCTL_5200_64K_32KBanks;
			spec1hd.mpWriteHandler = WriteByte_CCTL_5200_64K_32KBanks;
			break;

		case kATCartridgeMode_MicroCalc:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_MicroCalc;
			cctlhd.mpWriteHandler = WriteByte_CCTL_MicroCalc;
			break;

		case kATCartridgeMode_TheCart_32M:
		case kATCartridgeMode_TheCart_64M:
		case kATCartridgeMode_TheCart_128M:
			bank1Base	= 0xA0;
			bank1Size	= 0x20;
			bank2Base	= 0x80;
			bank2Size	= 0x20;
			usecctl = true;
			usecctlread = true;
			usecctlwrite = true;
			cctlhd.mpDebugReadHandler = ReadByte_CCTL_TheCart;
			cctlhd.mpReadHandler = ReadByte_CCTL_TheCart;
			cctlhd.mpWriteHandler = WriteByte_CCTL_TheCart;
			spec1Base	= 0xA0;
			spec1Size	= 0x20;
			spec1hd.mpReadHandler = ReadByte_TheCart;
			spec1hd.mpWriteHandler = WriteByte_TheCart;
			spec1WriteEnabled = true;
			spec2Base	= 0x80;
			spec2Size	= 0x20;
			spec2hd.mpReadHandler = ReadByte_TheCart;
			spec2hd.mpWriteHandler = WriteByte_TheCart;

			switch(mCartMode) {
				case kATCartridgeMode_TheCart_32M:
					mFlashEmu.Init(mCARTROM.data(), kATFlashType_S29GL256P, mpScheduler);
					break;
				case kATCartridgeMode_TheCart_64M:
					mFlashEmu.Init(mCARTROM.data(), kATFlashType_S29GL512P, mpScheduler);
					break;
				case kATCartridgeMode_TheCart_128M:
					mFlashEmu.Init(mCARTROM.data(), kATFlashType_S29GL01P, mpScheduler);
					break;
			}

			mEEPROM.Init();
			break;

		case kATCartridgeMode_MegaMax_2M:
			bank1Base	= 0x80;
			bank1Size	= 0x40;
			usecctl = true;
			usecctlwrite = true;
			cctlhd.mpReadHandler = ReadByte_CCTL_AddressToBank_Switchable<0x7F>;
			cctlhd.mpWriteHandler = WriteByte_CCTL_AddressToBank_Switchable<0x7F>;
			break;
	}

	if (fixedSize) {
		mpMemLayerFixedBank1 = mpMemMan->CreateLayer(mBasePriority, mCARTROM.data() + fixedOffset, fixedBase, fixedSize, true);
		mpMemMan->SetLayerName(mpMemLayerFixedBank1, "Cartridge fixed window 1");

		if (fixedMask)
			mpMemMan->SetLayerMemory(mpMemLayerFixedBank1, mCARTROM.data() + fixedOffset, fixedBase, fixedSize, fixedMask);

		mpMemMan->EnableLayer(mpMemLayerFixedBank1, true);
	}

	if (fixed2Size) {
		uint8 *mem = fixed2RAM ? mCARTRAM.data() : mCARTROM.data();
		mpMemLayerFixedBank2 = mpMemMan->CreateLayer(mBasePriority, mem + fixed2Offset, fixed2Base, fixedSize, true);
		mpMemMan->SetLayerName(mpMemLayerFixedBank2, "Cartridge fixed window 2");

		if (fixed2Mask >= 0)
			mpMemMan->SetLayerMemory(mpMemLayerFixedBank2, mem + fixed2Offset, fixed2Base, fixed2Size, fixed2Mask);

		mpMemMan->EnableLayer(mpMemLayerFixedBank2, true);
	}

	if (bank1Size) {
		mpMemLayerVarBank1 = mpMemMan->CreateLayer(mBasePriority+1, mCARTROM.data(), bank1Base, bank1Size, true);
		mpMemMan->SetLayerName(mpMemLayerVarBank1, "Cartridge variable window 1");

		if (bank1Mask >= 0)
			mpMemMan->SetLayerMemory(mpMemLayerVarBank1, mCARTROM.data(), bank1Base, bank1Size, bank1Mask);
	}

	if (bank2Size) {
		mpMemLayerVarBank2 = mpMemMan->CreateLayer(mBasePriority+2, mCARTROM.data(), bank2Base, bank2Size, true);
		mpMemMan->SetLayerName(mpMemLayerVarBank2, "Cartridge variable window 2");
	}

	if (spec1Size) {
		mpMemLayerSpec1 = mpMemMan->CreateLayer(mBasePriority+3, spec1hd, spec1Base, spec1Size);
		mpMemMan->SetLayerName(mpMemLayerSpec1, "Cartridge special window 1");

		if (spec1ReadEnabled) {
			mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, true);
			mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_CPURead, true);
		}

		if (spec1WriteEnabled)
			mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_CPUWrite, true);
	}

	if (spec2Size) {
		mpMemLayerSpec2 = mpMemMan->CreateLayer(mBasePriority+4, spec2hd, spec2Base, spec2Size);
		mpMemMan->SetLayerName(mpMemLayerSpec2, "Cartridge special window 2");

		if (spec2Enabled)
			mpMemMan->EnableLayer(mpMemLayerSpec2, true);
	}

	if (usecctl) {
		mpMemLayerControl = mpMemMan->CreateLayer(mBasePriority+5, cctlhd, 0xD5, 0x01);
		mpMemMan->SetLayerName(mpMemLayerControl, "Cartridge control window 1");

		mpMemMan->EnableLayer(mpMemLayerControl, kATMemoryAccessMode_AnticRead, usecctlread);
		mpMemMan->EnableLayer(mpMemLayerControl, kATMemoryAccessMode_CPURead, usecctlread);
		mpMemMan->EnableLayer(mpMemLayerControl, kATMemoryAccessMode_CPUWrite, usecctlwrite);
	}

	// This doesn't make sense for non-pow2 carts, of course, but currently we only use it for pow2.
	mCartSizeMask = (uint32)mCARTROM.size() - 1;

	UpdateLayerBuses();

	switch(mCartMode) {
		case kATCartridgeMode_TheCart_32M:
		case kATCartridgeMode_TheCart_64M:
		case kATCartridgeMode_TheCart_128M:
			UpdateTheCartBanking();
			UpdateTheCart();
			break;
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

		const bool flashRead = mFlashEmu.IsControlReadEnabled();

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

	if (mCartMode == kATCartridgeMode_TheCart_32M ||
		mCartMode == kATCartridgeMode_TheCart_64M ||
		mCartMode == kATCartridgeMode_TheCart_128M)
	{
		if (mpCB)
			mpCB->CartSetAxxxMapped(mCartBank >= 0);

		if (mCartBank < 0) {
			// primary bank disabled
			mpMemMan->EnableLayer(mpMemLayerVarBank1, false);
			mpMemMan->EnableLayer(mpMemLayerSpec1, false);
		} else {
			uint8 base = 0xA0;
			uint8 size = 0x20;

			if (mCartBank & 0x20000) {
				base = 0xB0;
				size = 0x10;
			}

			if (!(mCartBank & 0x10000)) {
				// primary bank set to flash
				mpMemMan->SetLayerMemory(mpMemLayerVarBank1, mCARTROM.data() + ((mCartBank << 13) & mCartSizeMask), base, size, (uint32)0-1, true);
				mpMemMan->EnableLayer(mpMemLayerVarBank1, true);

				const bool flashRead = mFlashEmu.IsControlReadEnabled();
				mpMemMan->SetLayerAddressRange(mpMemLayerSpec1, base, size);
				mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_AnticRead, flashRead);
				mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_CPURead, flashRead);
				mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_CPUWrite, (mTheCartRegs[7] & 0x01) != 0);
			} else {
				// primary bank set to RAM
				mpMemMan->SetLayerMemory(mpMemLayerVarBank1, mCARTRAM.data() + ((mCartBank << 13) & 0x7ffff), base, size, (uint32)0-1, !(mTheCartRegs[7] & 0x01));
				mpMemMan->EnableLayer(mpMemLayerVarBank1, true);

				// disable flash layer
				mpMemMan->EnableLayer(mpMemLayerSpec1, false);
			}
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
			case kATCartridgeMode_MaxFlash_1024K_Bank0:
				mpMemMan->EnableLayer(mpMemLayerSpec1, false);
				break;

			case kATCartridgeMode_Switchable_XEGS_32K:
			case kATCartridgeMode_Switchable_XEGS_64K:
			case kATCartridgeMode_Switchable_XEGS_128K:
			case kATCartridgeMode_Switchable_XEGS_256K:
			case kATCartridgeMode_Switchable_XEGS_512K:
			case kATCartridgeMode_Switchable_XEGS_1M:
				mpMemMan->EnableLayer(mpMemLayerFixedBank1, false);
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
		case kATCartridgeMode_MaxFlash_1024K_Bank0:
			// 8K banks
			mpMemMan->SetLayerMemory(mpMemLayerVarBank1, cartbase + (mCartBank << 13));
			mpMemMan->EnableLayer(mpMemLayerSpec1, kATMemoryAccessMode_CPUWrite, true);
			break;

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
		case kATCartridgeMode_Atrax_SDX_64K:
		case kATCartridgeMode_Atrax_SDX_128K:
		case kATCartridgeMode_AST_32K:
		case kATCartridgeMode_Turbosoft_64K:
		case kATCartridgeMode_Turbosoft_128K:
		case kATCartridgeMode_MegaCart_1M_2:
		case kATCartridgeMode_MicroCalc:
			// 8K banks
			mpMemMan->SetLayerMemory(mpMemLayerVarBank1, cartbase + (mCartBank << 13));
			break;

		case kATCartridgeMode_MegaCart_512K_3:
			// 8K banks, masked to 512K
			mpMemMan->SetLayerMemory(mpMemLayerVarBank1, cartbase + ((mCartBank & 0x1f) << 14));
			break;

		case kATCartridgeMode_MegaCart_2M_3:
			// 8K banks, masked to 2M
			mpMemMan->SetLayerMemory(mpMemLayerVarBank1, cartbase + ((mCartBank & 0x7f) << 14));
			break;

		case kATCartridgeMode_Switchable_XEGS_32K:
		case kATCartridgeMode_Switchable_XEGS_64K:
		case kATCartridgeMode_Switchable_XEGS_128K:
		case kATCartridgeMode_Switchable_XEGS_256K:
		case kATCartridgeMode_Switchable_XEGS_512K:
		case kATCartridgeMode_Switchable_XEGS_1M:
			// 8K banks
			mpMemMan->EnableLayer(mpMemLayerFixedBank1, true);
			mpMemMan->SetLayerMemory(mpMemLayerVarBank1, cartbase + (mCartBank << 13));
			break;

		case kATCartridgeMode_MegaCart_16K:
		case kATCartridgeMode_MegaCart_32K:
		case kATCartridgeMode_MegaCart_64K:
		case kATCartridgeMode_MegaCart_128K:
		case kATCartridgeMode_MegaCart_256K:
		case kATCartridgeMode_MegaCart_512K:
		case kATCartridgeMode_MegaCart_1M:
		case kATCartridgeMode_MegaCart_4M_3:
		case kATCartridgeMode_MegaMax_2M:
			// 16K banks
			mpMemMan->SetLayerMemory(mpMemLayerVarBank1, cartbase + (mCartBank << 14));
			break;

		case kATCartridgeMode_5200_64K_32KBanks:
			// 32K banks
			mpMemMan->SetLayerMemory(mpMemLayerVarBank1, cartbase + (mCartBank << 15));
			break;

		case kATCartridgeMode_OSS_034M:
		case kATCartridgeMode_OSS_043M:
		case kATCartridgeMode_OSS_M091:
		case kATCartridgeMode_OSS_8K:
		case kATCartridgeMode_BountyBob5200:
		case kATCartridgeMode_BountyBob800:
			// 4K banks
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
				mpMemMan->SetLayerMemory(mpMemLayerVarBank1, cartbase + (32 << 14), 0x80, 0x40, 0x1F);
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
	switch(mCartMode) {
		case kATCartridgeMode_SIC:
			return;

		case kATCartridgeMode_TheCart_32M:
		case kATCartridgeMode_TheCart_64M:
		case kATCartridgeMode_TheCart_128M:
			if (mCartBank2 < 0) {
				// secondary bank disabled
				mpMemMan->EnableLayer(mpMemLayerVarBank2, false);
				mpMemMan->EnableLayer(mpMemLayerSpec2, false);
			} else {
				uint8 base = 0x80;
				uint8 size = 0x20;
				uint32 extraOffset = 0;
				uint32 bank2 = mCartBank2;

				if (bank2 & 0x20000) {
					bank2 -= 0x20000;

					base = 0xA0;
					size = 0x10;

					if (bank2 & 1)
						extraOffset = 0x1000;

					bank2 += bank2 & 0x10000;
					bank2 >>= 1;
				}

				if (!(bank2 & 0x10000)) {
					// secondary bank set to flash
					mpMemMan->SetLayerMemory(mpMemLayerVarBank2, mCARTROM.data() + (((bank2 << 13) + extraOffset) & mCartSizeMask), base, size, (uint32)0-1, true);
					mpMemMan->EnableLayer(mpMemLayerVarBank2, true);

					const bool flashRead = mFlashEmu.IsControlReadEnabled();
					mpMemMan->SetLayerAddressRange(mpMemLayerSpec2, base, size);
					mpMemMan->EnableLayer(mpMemLayerSpec2, kATMemoryAccessMode_AnticRead, flashRead);
					mpMemMan->EnableLayer(mpMemLayerSpec2, kATMemoryAccessMode_CPURead, flashRead);
					mpMemMan->EnableLayer(mpMemLayerSpec2, kATMemoryAccessMode_CPUWrite, (mTheCartRegs[7] & 0x04) != 0);
				} else {
					// secondary bank set to RAM
					mpMemMan->SetLayerMemory(mpMemLayerVarBank2, mCARTRAM.data() + (((bank2 << 13) + extraOffset) & 0x7ffff), base, size, (uint32)0-1, !(mTheCartRegs[7] & 0x04));
					mpMemMan->EnableLayer(mpMemLayerVarBank2, true);

					// disable flash layer
					mpMemMan->EnableLayer(mpMemLayerSpec2, false);
				}
			}
			return;
	}

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
		case kATCartridgeMode_OSS_043M:
			mpMemMan->SetLayerMemory(mpMemLayerVarBank2, cartbase + 0x3000);
			break;

		case kATCartridgeMode_OSS_M091:
			mpMemMan->SetLayerMemory(mpMemLayerVarBank2, cartbase);
			break;

		case kATCartridgeMode_AST_32K:
			mpMemMan->SetLayerMemory(mpMemLayerVarBank2, cartbase + (mCartBank2 << 8));
			break;
	}
}

void ATCartridgeEmulator::UpdateLayerBuses() {
	if (mpMemLayerFixedBank1) mpMemMan->SetLayerFastBus(mpMemLayerFixedBank1, mbFastBus);
	if (mpMemLayerFixedBank2) mpMemMan->SetLayerFastBus(mpMemLayerFixedBank2, mbFastBus);
	if (mpMemLayerVarBank1	) mpMemMan->SetLayerFastBus(mpMemLayerVarBank1, mbFastBus);
	if (mpMemLayerVarBank2	) mpMemMan->SetLayerFastBus(mpMemLayerVarBank2, mbFastBus);
	if (mpMemLayerSpec1		) mpMemMan->SetLayerFastBus(mpMemLayerSpec1, mbFastBus);
	if (mpMemLayerSpec2		) mpMemMan->SetLayerFastBus(mpMemLayerSpec2, mbFastBus);
	if (mpMemLayerControl	) mpMemMan->SetLayerFastBus(mpMemLayerControl, mbFastBus);
}

void ATCartridgeEmulator::UpdateTheCartBanking() {
	mTheCartBankInfo.clear();
	mTheCartBankInfo.resize(256, -1);

	// Supported modes:
	//
	// $00: off, cartridge disabled
	// $01: 8k banks at $A000
	// $02: AtariMax 1MBit / 128k
	// $03: Atarimax 8MBit / 1MB
	// $04: OSS M091
	// $08: SDX 64k cart, $D5Ex banking
	// $09: Diamond GOS 64k cart, $D5Dx banking
	// $0A: Express 64k cart, $D57x banking
	// $0C: Atrax 128k cart
	// $0D: Williams 64k cart
	// $20: flexi mode (separate 8k banks at $A000 and $8000)
	// $21: standard 16k cart at $8000-$BFFF
	// $22: MegaMax 16k mode (up to 2MB), AtariMax 8Mbit banking
	// $23: Blizzard 16k
	// $24: Sic!Cart 512k
	// $28: 16k Mega cart
	// $29: 32k Mega cart
	// $2A: 64k Mega cart
	// $2B: 128k Mega cart
	// $2C: 256k Mega cart
	// $2D: 512k Mega cart
	// $2E: 1024k Mega cart
	// $2F: 2048k Mega cart
	// $30: 32k XEGS cart
	// $31: 64k XEGS cart
	// $32: 128k XEGS cart
	// $33: 256k XEGS cart
	// $34: 512k XEGS cart
	// $35: 1024k XEGS cart
	// $38: 32k SWXEGS cart
	// $39: 64k SWXEGS cart
	// $3A: 128k SWXEGS cart
	// $3B: 256k SWXEGS cart
	// $3C: 512k SWXEGS cart
	// $3D: 1024k SWXEGS cart

	const uint8 mode = mTheCartRegs[6] & 0x3f;

	mTheCartBankMask = 0;
	mbTheCartBankByAddress = false;

	// validate mode and determine if we need the bank info
	switch(mode) {
		case 0x00:	// $00: off, cartridge disabled
			mTheCartBankMode = kTheCartBankMode_Disabled;
			break;

		case 0x01:	// $01: 8k banks at $A000
			mTheCartBankMode = kTheCartBankMode_8K;
			break;

		case 0x20:	// $20: flexi mode (separate 8k banks at $A000 and $8000)
			mTheCartBankMode = kTheCartBankMode_Flexi;
			break;

		case 0x21:	// $21: standard 16k cart at $8000-$BFFF
			mTheCartBankMode = kTheCartBankMode_16K;
			break;

		case 0x02:	// $02: AtariMax 1MBit / 128k
			mTheCartBankMode = kTheCartBankMode_8K;
			mbTheCartBankByAddress = true;
			mTheCartBankMask = 0x0F;

			for(int i=0; i<0x10; ++i)
				mTheCartBankInfo[i] = 0x4000 + i;

			for(int i=0x10; i<0x20; ++i)
				mTheCartBankInfo[i] = i;
			break;

		case 0x03:	// $03: Atarimax 8MBit / 1MB
			mTheCartBankMode = kTheCartBankMode_8K;
			mbTheCartBankByAddress = true;
			mTheCartBankMask = 0x7F;

			for(int i=0; i<0x80; ++i)
				mTheCartBankInfo[i] = 0x4000 + i;

			// The!Cart docs say that only $D58x disables in AtariMax 8Mbit emulation mode,
			// even though $D580-FF do so in the real cart.
			for(int i=0x80; i<0x90; ++i)
				mTheCartBankInfo[i] = i;
			break;

		case 0x04:	// $04: OSS M091
			{
				static const sint8 kBankLookup[16] = {1, 3, 1, 3, 1, 3, 1, 3, -1, 2};

				mTheCartBankMode = kTheCartBankMode_OSS;
				mbTheCartBankByAddress = true;
				mTheCartBankMask = 0x01;

				for(int i=0; i<256; ++i) {
					sint8 v = kBankLookup[i & 9];
					mTheCartBankInfo[i] = v;
				}
			}
			break;

		case 0x08:	// $08: SDX 64k cart, $D5Ex banking
			mTheCartBankMode = kTheCartBankMode_8K;
			mbTheCartBankByAddress = true;
			mTheCartBankMask = 0x07;

			for(int i=0; i<8; ++i)
				mTheCartBankInfo[0xE0+i] = 0x4007 - i;

			for(int i=0; i<8; ++i)
				mTheCartBankInfo[0xE8+i] = 7 - i;			
			break;

		case 0x09:	// $09: Diamond GOS 64k cart, $D5Dx banking
			mTheCartBankMode = kTheCartBankMode_8K;
			mbTheCartBankByAddress = true;
			mTheCartBankMask = 0x07;

			for(int i=0; i<8; ++i)
				mTheCartBankInfo[0xD0+i] = 0x4007 - i;

			for(int i=0; i<8; ++i)
				mTheCartBankInfo[0xD8+i] = 7 - i;			
			break;

		case 0x0A:	// $0A: Express 64k cart, $D57x banking
			mTheCartBankMode = kTheCartBankMode_8K;
			mbTheCartBankByAddress = true;
			mTheCartBankMask = 0x07;

			for(int i=0; i<8; ++i)
				mTheCartBankInfo[0x70+i] = 0x4007 - i;

			for(int i=0; i<8; ++i)
				mTheCartBankInfo[0x78+i] = 7 - i;			
			break;

		case 0x0C:	// $0C: Atrax 128k cart
			mTheCartBankMode = kTheCartBankMode_8K;
			mTheCartBankMask = 0x0F;
			for(int i=0; i<128; ++i)
				mTheCartBankInfo[i] = 0x4000 + i;

			for(int i=0; i<128; ++i)
				mTheCartBankInfo[0x80+i] = i;			
			break;

		case 0x0D:	// $0D: Williams 64k cart
			mTheCartBankMode = kTheCartBankMode_8K;
			mbTheCartBankByAddress = true;
			mTheCartBankMask = 0x07;

			for(int i=0; i<0x100; ++i)
				mTheCartBankInfo[i] = i & 8 ? i : 0x4000 + i;
			break;

		case 0x22:	// $22: MegaMax 16k mode (up to 2MB), AtariMax 8Mbit banking
			mTheCartBankMode = kTheCartBankMode_16K;
			mbTheCartBankByAddress = true;
			mTheCartBankMask = 0xFE;

			for(int i=0; i<128; ++i)
				mTheCartBankInfo[i] = 0x4000 + i*2;

			for(int i=0; i<128; ++i)
				mTheCartBankInfo[i+128] = i*2;
			break;

		case 0x23:	// $23: Blizzard 16k
			mTheCartBankMode = kTheCartBankMode_16K;
			mTheCartBankMask = 0;
			for(int i=0; i<0x100; ++i)
				mTheCartBankInfo[i] = 0;			
			break;

		case 0x24:	// $24: Sic!Cart 512k
			mTheCartBankMode = kTheCartBankMode_SIC;
			mTheCartBankMask = 0x3E;

			for(int i=0; i<0x100; ++i) {
				// Bit 7 controls flash -- which we ignore here.
				// Bit 6 disables $A000-BFFF.
				// Bit 5 enables $8000-9FFF.
				// SIC! uses 16K banks, so we have to go evens.
				mTheCartBankInfo[i] = ((i & 0x40) ? 0 : 0x4000) + ((i & 0x20) ? 0x2000 : 0) + (i & 0x1f)*2;
			}
			break;

		case 0x28:	// $28: 16k Mega cart
		case 0x29:	// $29: 32k Mega cart
		case 0x2A:	// $2A: 64k Mega cart
		case 0x2B:	// $2B: 128k Mega cart
		case 0x2C:	// $2C: 256k Mega cart
		case 0x2D:	// $2D: 512k Mega cart
		case 0x2E:	// $2E: 1024k Mega cart
		case 0x2F:	// $2F: 2048k Mega cart
			// 16K banks - data written to $D5xx selects bank, D7=1 disables
			mTheCartBankMode = kTheCartBankMode_16K;
			mTheCartBankMask = (2 << (mode - 0x28)) - 2;
			mbTheCartBankByAddress = false;

			for(int i=0; i<128; ++i)
				mTheCartBankInfo[i] = 0x4000 + i * 2;

			for(int i=0; i<128; ++i)
				mTheCartBankInfo[i + 128] = i * 2;
			break;

		case 0x30:	// $30: 32k XEGS cart
		case 0x31:	// $31: 64k XEGS cart
		case 0x32:	// $32: 128k XEGS cart
		case 0x33:	// $33: 256k XEGS cart
		case 0x34:	// $34: 512k XEGS cart
		case 0x35:	// $35: 1024k XEGS cart
			// fixed upper 8K bank, data written to $D5xx selects lower 8K bank
			mTheCartBankMode = kTheCartBankMode_8KFixed_8K;
			mTheCartBankMask = (4 << (mode - 0x30)) - 1;
			mbTheCartBankByAddress = false;

			for(int i=0; i<256; ++i)
				mTheCartBankInfo[i] = 0x4000 + i;
			break;

		case 0x38:	// $38: 32k SWXEGS cart
		case 0x39:	// $39: 64k SWXEGS cart
		case 0x3A:	// $3A: 128k SWXEGS cart
		case 0x3B:	// $3B: 256k SWXEGS cart
		case 0x3C:	// $3C: 512k SWXEGS cart
		case 0x3D:	// $3D: 1024k SWXEGS cart
			// fixed upper 8K bank, data written to $D5xx selects lower 8K bank, D7=1 disables
			mTheCartBankMode = kTheCartBankMode_8KFixed_8K;
			mTheCartBankMask = (4 << (mode - 0x38)) - 1;
			mbTheCartBankByAddress = false;

			for(int i=0; i<256; ++i)
				mTheCartBankInfo[i] = ((i & 0x80) ? 0 : 0x4000) + i;
			break;
	}
}

void ATCartridgeEmulator::UpdateTheCart() {
	// compute primary bank
	sint32 bank1 = -1;

	if (mTheCartRegs[2] & 0x01) {		// check if enabled
		bank1 = VDReadUnalignedLEU16(mTheCartRegs + 0) & 0x3FFF;

		if (mTheCartRegs[7] & 0x02)		// check if RAM
			bank1 += 0x10000;
	}

	// compute secondary bank
	sint32 bank2 = -1;

	if (mTheCartRegs[5] & 0x01) {		// check if enabled
		bank2 = VDReadUnalignedLEU16(mTheCartRegs + 3) & 0x3FFF;

		if (mTheCartRegs[7] & 0x08)		// check if RAM
			bank2 += 0x10000;
	}

	switch(mTheCartBankMode) {
		case kTheCartBankMode_Disabled:
		default:
			SetCartBank(bank1);
			SetCartBank2(bank2);
			break;

		case kTheCartBankMode_8K:
			SetCartBank(bank1);
			SetCartBank2(-1);
			break;

		case kTheCartBankMode_16K:
			bank1 &= ~1;
			SetCartBank(bank1 + 1);
			SetCartBank2(bank1);
			break;

		case kTheCartBankMode_SIC:
			if (mTheCartRegs[2]) {
				// recompute primary bank since there are independent enables (arg)
				bank1 = VDReadUnalignedLEU16(mTheCartRegs + 0) & 0x3FFF;

				if (mTheCartRegs[7] & 0x02)		// check if RAM
					bank1 += 0x10000;

				bank1 &= ~1;
				SetCartBank(mTheCartSICEnables & 0x40 ? bank1+1 : -1);
				SetCartBank2(mTheCartSICEnables & 0x20 ? bank1 : -1);
			} else {
				SetCartBank(-1);
				SetCartBank2(-1);
			}
			break;

		case kTheCartBankMode_Flexi:
			SetCartBank(bank1);
			SetCartBank2(bank2);
			break;

		case kTheCartBankMode_8KFixed_8K:
			SetCartBank(bank1 | mTheCartBankMask);
			SetCartBank2(bank1);
			break;

		case kTheCartBankMode_OSS:
			SetCartBank(bank1 | 0x20000);
			SetCartBank2(((bank1 & ~1) * 2) | 0x20000 | mTheCartOSSBank);
			break;
	}
}
