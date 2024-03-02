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

#ifndef f_AT_CARTRIDGE_H
#define f_AT_CARTRIDGE_H

#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>

class ATSaveStateReader;
class ATSaveStateWriter;
class IVDRandomAccessStream;
class IATUIRenderer;

enum ATCartridgeMode {
	kATCartridgeMode_None,
	kATCartridgeMode_8K,
	kATCartridgeMode_16K,
	kATCartridgeMode_XEGS_32K,
	kATCartridgeMode_XEGS_64K,
	kATCartridgeMode_XEGS_128K,
	kATCartridgeMode_Switchable_XEGS_32K,
	kATCartridgeMode_Switchable_XEGS_64K,
	kATCartridgeMode_Switchable_XEGS_128K,
	kATCartridgeMode_Switchable_XEGS_256K,
	kATCartridgeMode_Switchable_XEGS_512K,
	kATCartridgeMode_Switchable_XEGS_1M,
	kATCartridgeMode_MaxFlash_128K,
	kATCartridgeMode_MaxFlash_128K_MyIDE,
	kATCartridgeMode_MaxFlash_1024K,
	kATCartridgeMode_MegaCart_16K,
	kATCartridgeMode_MegaCart_32K,
	kATCartridgeMode_MegaCart_64K,
	kATCartridgeMode_MegaCart_128K,
	kATCartridgeMode_MegaCart_256K,
	kATCartridgeMode_MegaCart_512K,
	kATCartridgeMode_MegaCart_1M,
	kATCartridgeMode_SuperCharger3D,
	kATCartridgeMode_BountyBob800,
	kATCartridgeMode_OSS_034M,
	kATCartridgeMode_OSS_M091,
	kATCartridgeMode_5200_32K,
	kATCartridgeMode_5200_16K_TwoChip,
	kATCartridgeMode_5200_16K_OneChip,
	kATCartridgeMode_5200_8K,
	kATCartridgeMode_5200_4K,
	kATCartridgeMode_Corina_1M_EEPROM,
	kATCartridgeMode_Corina_512K_SRAM_EEPROM,
	kATCartridgeMode_BountyBob5200,
	kATCartridgeMode_SpartaDosX_128K,
	kATCartridgeMode_TelelinkII,
	kATCartridgeMode_Williams_64K,
	kATCartridgeMode_Diamond_64K,
	kATCartridgeMode_Express_64K,
	kATCartridgeMode_SpartaDosX_64K,
	kATCartridgeMode_RightSlot_8K,
	kATCartridgeMode_XEGS_256K,
	kATCartridgeMode_XEGS_512K,
	kATCartridgeMode_XEGS_1M,
	kATCartridgeMode_DB_32K,
	kATCartridgeMode_Atrax_128K,
	kATCartridgeMode_Williams_32K,
	kATCartridgeMode_Phoenix_8K,
	kATCartridgeMode_Blizzard_16K,
	kATCartridgeModeCount
};

enum ATCartLoadStatus {
	kATCartLoadStatus_Ok,
	kATCartLoadStatus_UnknownMapper
};

struct ATCartLoadContext {
	bool mbReturnOnUnknownMapper;
	int mCartMapper;
	uint32 mCartSize;

	ATCartLoadStatus mLoadStatus;
	bool mbMayBe2600;
};

class ATCartridgeEmulator {
	ATCartridgeEmulator(const ATCartridgeEmulator&);
	ATCartridgeEmulator& operator=(const ATCartridgeEmulator&);

public:
	ATCartridgeEmulator();
	~ATCartridgeEmulator();

	void SetUIRenderer(IATUIRenderer *r);

	int GetCartBank() const { return mCartBank; }
	bool IsABxxMapped() const;
	bool IsBASICDisableAllowed() const;		// Cleared if we have a cart type that doesn't want OPTION pressed (AtariMax).
	bool IsDirty() const { return mbDirty; }
	
	const wchar_t *GetPath() const {
		return mCartMode ? mImagePath.c_str() : NULL;
	}

	ATCartridgeMode GetMode() const { return mCartMode; }

	void LoadSuperCharger3D();
	void Load5200Default();
	void LoadFlash1Mb(bool altbank);
	void LoadFlash8Mb();
	bool Load(const wchar_t *fn, ATCartLoadContext *loadCtx);
	bool Load(const wchar_t *origPath, const wchar_t *imagePath, IVDRandomAccessStream& stream, ATCartLoadContext *loadCtx);
	void Unload();

	void Save(const wchar_t *fn, bool includeHeader);

	void ColdReset();

	bool WriteMemoryMap5200(const uint8 **readMap, uint8 **writeMap, const uint8 **anticMap, const uint8 *dummyReadPage, uint8 *dummyWritePage);
	bool WriteMemoryMap89(const uint8 **readMap, uint8 **writeMap, const uint8 **anticMap, const uint8 *dummyReadPage, uint8 *dummyWritePage);
	bool WriteMemoryMapAB(const uint8 **readMap, uint8 **writeMap, const uint8 **anticMap, const uint8 *dummyReadPage, uint8 *dummyWritePage);

	uint8 ReadByte4567(uint16 address, bool& remapRequired);
	bool WriteByte4567(uint16 address, uint8 value);
	uint8 ReadByte89AB(uint16 address, bool& remapRequired);
	bool WriteByte89AB(uint16 address, uint8 value);
	bool ReadByteD5(uint16 address, uint8& value);
	bool WriteByteD5(uint16 address, uint8 value);

	void LoadState(ATSaveStateReader& reader);
	void SaveState(ATSaveStateWriter& writer);

protected:
	template<class T> void ExchangeState(T& io);

	ATCartridgeMode mCartMode;
	int	mCartBank;
	int	mCartBank2;
	int	mInitialCartBank;
	int	mInitialCartBank2;
	int mCommandPhase;
	bool mbDirty;
	IATUIRenderer *mpUIRenderer;

	enum FlashReadMode {
		kFlashReadMode_Normal,
		kFlashReadMode_Autoselect,
		kFlashReadMode_WriteStatus
	};

	FlashReadMode mFlashReadMode;

	uint8	mSC3D[4];

	vdfastvector<uint8> mCARTROM;
	vdfastvector<uint8> mCARTRAM;
	VDStringW mImagePath;
};

int ATGetCartridgeModeForMapper(int mapper);
bool ATIsCartridgeModeHWCompatible(ATCartridgeMode cartmode, int hwmode);

#endif	// f_AT_CARTRIDGE_H
