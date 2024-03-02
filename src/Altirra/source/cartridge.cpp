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

enum ATCartridgeMode {
	kATCartridgeMode_None,
	kATCartridgeMode_8K,
	kATCartridgeMode_16K,
	kATCartridgeMode_XEGS_32K,
	kATCartridgeMode_XEGS_64K,
	kATCartridgeMode_XEGS_128K,
	kATCartridgeMode_MaxFlash_128K,
	kATCartridgeMode_MaxFlash_1024K,
	kATCartridgeMode_MegaCart_512K,
	kATCartridgeMode_SuperCharger3D,
	kATCartridgeMode_BountyBob800
};

ATCartridgeEmulator::ATCartridgeEmulator()
	: mCartMode(kATCartridgeMode_None)
	, mCartBank(-1)
	, mCartBank2(-1)
	, mInitialCartBank(-1)
	, mInitialCartBank2(-1)
{
}

ATCartridgeEmulator::~ATCartridgeEmulator() {
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
}

void ATCartridgeEmulator::Load(const wchar_t *s) {
	VDFile f(s);

	sint64 size = f.size();

	if (size < 1024 || size > 1048576 + 16)
		throw MyError("Unsupported cartridge size.");

	// check for header
	char buf[16];
	f.read(buf, 16);

	bool validHeader = false;
	uint32 size32 = (uint32)size;

	if (!memcmp(buf, "CART", 4)) {
		uint32 type = VDReadUnalignedBEU32(buf + 4);
		uint32 checksum = VDReadUnalignedBEU32(buf + 8);

		size32 -= 16;
		mCARTROM.resize(size32);
		f.read(mCARTROM.data(), size32);

		uint32 csum = 0;
		for(uint32 i=0; i<size32; ++i)
			csum += mCARTROM[i];

		if (csum == checksum) {
			validHeader = true;

			switch(type) {
				case 0:
					mCartMode = kATCartridgeMode_8K;
					break;
				case 1:
					mCartMode = kATCartridgeMode_16K;
					break;
				case 12:
					mCartMode = kATCartridgeMode_XEGS_32K;
					break;
				case 13:
					mCartMode = kATCartridgeMode_XEGS_64K;
					break;
				case 14:
					mCartMode = kATCartridgeMode_XEGS_128K;
					break;
				case 18:
					mCartMode = kATCartridgeMode_BountyBob800;
					break;
				case 31:
					mCartMode = kATCartridgeMode_MegaCart_512K;
					break;
				case 41:
					mCartMode = kATCartridgeMode_MaxFlash_128K;
					break;
				case 42:
					mCartMode = kATCartridgeMode_MaxFlash_1024K;
					break;

				default:
					throw MyError("The selected cartridge cannot be loaded as it uses unsupported mapper mode %d.", type);
			}
		}
	}

	if (!validHeader) {
		mCARTROM.resize(size32);
		f.seek(0);
		f.read(mCARTROM.data(), size32);

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

	uint32 allocSize = size32;

	// set initial bank and alloc size
	switch(mCartMode) {
		case kATCartridgeMode_8K:
			mInitialCartBank = 0;
			allocSize = 8192;
			break;

		case kATCartridgeMode_16K:
			mInitialCartBank = 0;
			allocSize = 16384;
			break;

		case kATCartridgeMode_XEGS_32K:
			mInitialCartBank = 3;
			allocSize = 32768;
			break;

		case kATCartridgeMode_XEGS_64K:
			mInitialCartBank = 7;
			allocSize = 0x10000;
			break;

		case kATCartridgeMode_XEGS_128K:
			mInitialCartBank = 15;
			allocSize = 0x20000;
			break;

		case kATCartridgeMode_MaxFlash_128K:
			mInitialCartBank = 0;
			allocSize = 131072;
			break;

		case kATCartridgeMode_MegaCart_512K:
			mInitialCartBank = 0;
			allocSize = 0x80000;
			break;

		case kATCartridgeMode_MaxFlash_1024K:
			mInitialCartBank = 127;
			allocSize = 1048576;
			break;

		case kATCartridgeMode_BountyBob800:
			mInitialCartBank = 0;
			mInitialCartBank2 = 0;
			allocSize = 40960;
			break;
	}

	mCartBank = mInitialCartBank;
	mCartBank2 = mInitialCartBank2;

	mCARTROM.resize(allocSize, 0);
	mImagePath = s;
}

void ATCartridgeEmulator::Unload() {
	mInitialCartBank = 0;
	mCartBank = 0;
	mCartMode = kATCartridgeMode_None;

	mImagePath.clear();
}

void ATCartridgeEmulator::ColdReset() {
	mCartBank = mInitialCartBank;
}

bool ATCartridgeEmulator::WriteMemoryMap89(const uint8 **readMap, uint8 **writeMap, const uint8 **anticMap, const uint8 *dummyReadPage, uint8 *dummyWritePage) {
	if (mCartBank < 0)
		return false;

	switch(mCartMode) {
		case kATCartridgeMode_XEGS_32K:
		case kATCartridgeMode_XEGS_64K:
		case kATCartridgeMode_XEGS_128K:
		case kATCartridgeMode_16K:
			{
				const uint8 *cartbase = mCARTROM.data() + 0x2000 * mCartBank;
				for(int i=0; i<32; ++i) {
					anticMap[i] = readMap[i] = cartbase + (i << 8);
					writeMap[i] = dummyWritePage;
				}
			}
			return true;

		case kATCartridgeMode_MegaCart_512K:
			{
				const uint8 *cartbase = mCARTROM.data() + 0x4000 * mCartBank;
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

				readMap[15] = NULL;
				readMap[31] = NULL;
				writeMap[15] = NULL;
				writeMap[31] = NULL;
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
		case kATCartridgeMode_8K:
			break;

		case kATCartridgeMode_16K:
			cartbase += 0x2000;
			break;

		case kATCartridgeMode_XEGS_32K:
			cartbase += 0x6000;
			break;

		case kATCartridgeMode_XEGS_64K:
			cartbase += 0xE000;
			break;

		case kATCartridgeMode_XEGS_128K:
			cartbase += 0x1E000;
			break;

		case kATCartridgeMode_MaxFlash_128K:
		case kATCartridgeMode_MaxFlash_1024K:
			cartbase += 0x2000 * mCartBank;
			break;

		case kATCartridgeMode_MegaCart_512K:
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
	}

	for(int i=0; i<32; ++i) {
		anticMap[i] = readMap[i] = cartbase + (i << 8);
		writeMap[i] = dummyWritePage;
	}

	return true;
}

uint8 ATCartridgeEmulator::ReadByte89AB(uint16 address, bool& remapRequired) {
	if (mCartMode == kATCartridgeMode_BountyBob800) {
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

	return 0xFF;
}

bool ATCartridgeEmulator::WriteByte89AB(uint16 address, uint8 value) {
	if (mCartMode == kATCartridgeMode_BountyBob800) {
		uint32 index = (address & 0xEFFF) - 0x8FF6;

		if (index < 4) {
			if (address & 0x1000)
				mCartBank2 = index;
			else
				mCartBank = index;

			return true;
		}
	}

	return false;
}

uint8 ATCartridgeEmulator::ReadByteD5(uint16 address) {
	if (mCartMode == kATCartridgeMode_SuperCharger3D) {
		return mSC3D[address & 3];
	}

	return 0xFF;
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

		case kATCartridgeMode_MaxFlash_128K:
			if (addr < 0xD520) {
				if (addr < 0xD510)
					mCartBank = addr & 15;
				else
					mCartBank = -1;
			}
			return true;

		case kATCartridgeMode_MaxFlash_1024K:
			if (addr < 0xD580)
				mCartBank = addr & 127;
			else
				mCartBank = -1;
			return true;

		case kATCartridgeMode_MegaCart_512K:
			if (value & 0x80)
				mCartBank = -1;
			else
				mCartBank = value & 31;
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
