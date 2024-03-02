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
#include "flash.h"

class ATSaveStateReader;
class ATSaveStateWriter;
class IVDRandomAccessStream;
class IATUIRenderer;
class ATMemoryManager;
class ATMemoryLayer;
class ATScheduler;

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
	kATCartridgeMode_SIC,
	kATCartridgeMode_Atrax_SDX_128K,
	kATCartridgeMode_OSS_043M,
	kATCartridgeMode_OSS_8K,
	kATCartridgeMode_Blizzard_4K,
	kATCartridgeMode_AST_32K,
	kATCartridgeMode_Atrax_SDX_64K,
	kATCartridgeMode_Turbosoft_64K,
	kATCartridgeMode_Turbosoft_128K,
	kATCartridgeMode_MaxFlash_1024K_Bank0,
	kATCartridgeMode_Megacart_1M_2,			// Hardware by Bernd for ABBUC JHV 2009
	kATCartridgeMode_5200_64K_32KBanks,		// Used by M.U.L.E. 64K conversion
	kATCartridgeMode_MicroCalc,
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

	vdfastvector<uint8> *mpCaptureBuffer;
};

class IATCartridgeCallbacks {
public:
	virtual void CartSetAxxxMapped(bool mapped) = 0;
};

class ATCartridgeEmulator {
	ATCartridgeEmulator(const ATCartridgeEmulator&);
	ATCartridgeEmulator& operator=(const ATCartridgeEmulator&);

public:
	ATCartridgeEmulator();
	~ATCartridgeEmulator();

	void Init(ATMemoryManager *memman, ATScheduler *sch, int basePriority);
	void Shutdown();

	void SetUIRenderer(IATUIRenderer *r);
	void SetCallbacks(IATCartridgeCallbacks *cb) { mpCB = cb; }

	int GetCartBank() const { return mCartBank; }
	bool IsABxxMapped() const;
	bool IsBASICDisableAllowed() const;		// Cleared if we have a cart type that doesn't want OPTION pressed (AtariMax).
	bool IsDirty() const { return mbDirty; }
	
	const wchar_t *GetPath() const {
		return mCartMode ? mImagePath.c_str() : NULL;
	}

	ATCartridgeMode GetMode() const { return mCartMode; }

	void Load5200Default();
	void LoadNewCartridge(ATCartridgeMode mode);
	bool Load(const wchar_t *fn, ATCartLoadContext *loadCtx);
	bool Load(const wchar_t *origPath, IVDRandomAccessStream& stream, ATCartLoadContext *loadCtx);
	void Unload();

	void Save(const wchar_t *fn, bool includeHeader);

	void ColdReset();

	void BeginLoadState(ATSaveStateReader& reader);
	void LoadStatePrivate(ATSaveStateReader& reader);
	void EndLoadState(ATSaveStateReader& reader);
	void BeginSaveState(ATSaveStateWriter& writer);
	void SaveStatePrivate(ATSaveStateWriter& writer);

protected:
	template<class T> void ExchangeState(T& io);

	static sint32 ReadByte_BB5200_1(void *thisptr0, uint32 address);
	static sint32 ReadByte_BB5200_2(void *thisptr0, uint32 address);
	static bool WriteByte_BB5200_1(void *thisptr0, uint32 address, uint8 value);
	static bool WriteByte_BB5200_2(void *thisptr0, uint32 address, uint8 value);
	static sint32 ReadByte_BB800_1(void *thisptr0, uint32 address);
	static sint32 ReadByte_BB800_2(void *thisptr0, uint32 address);
	static bool WriteByte_BB800_1(void *thisptr0, uint32 address, uint8 value);
	static bool WriteByte_BB800_2(void *thisptr0, uint32 address, uint8 value);
	static sint32 ReadByte_SIC(void *thisptr0, uint32 address);
	static bool WriteByte_SIC(void *thisptr0, uint32 address, uint8 value);
	static sint32 ReadByte_MaxFlash(void *thisptr0, uint32 address);
	static bool WriteByte_MaxFlash(void *thisptr0, uint32 address, uint8 value);
	static bool WriteByte_Corina1M(void *thisptr0, uint32 address, uint8 value);
	static bool WriteByte_Corina512K(void *thisptr0, uint32 address, uint8 value);
	static bool WriteByte_TelelinkII(void *thisptr0, uint32 address, uint8 value);

	static bool WriteByte_CCTL_Phoenix(void *thisptr0, uint32 address, uint8 value);

	template<uint8 T_Mask>
	static bool WriteByte_CCTL_AddressToBank(void *thisptr0, uint32 address, uint8 value);

	template<uint8 T_Mask>
	static bool WriteByte_CCTL_DataToBank(void *thisptr0, uint32 address, uint8 value);

	template<uint8 T_Mask>
	static bool WriteByte_CCTL_DataToBank_Switchable(void *thisptr0, uint32 address, uint8 value);

	template<uint8 T_Mask>
	static sint32 ReadByte_CCTL_Williams(void *thisptr0, uint32 address);

	template<uint8 T_Mask>
	static bool WriteByte_CCTL_Williams(void *thisptr0, uint32 address, uint8 value);

	template<uint8 T_Address>
	static sint32 ReadByte_CCTL_SDX64(void *thisptr0, uint32 address);

	template<uint8 T_Address>
	static bool WriteByte_CCTL_SDX64(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_SDX128(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_SDX128(void *thisptr0, uint32 address, uint8 value);
	static sint32 ReadByte_CCTL_MaxFlash_128K(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_MaxFlash_128K(void *thisptr0, uint32 address, uint8 value);
	static sint32 ReadByte_CCTL_MaxFlash_128K_MyIDE(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_MaxFlash_128K_MyIDE(void *thisptr0, uint32 address, uint8 value);
	static sint32 ReadByte_CCTL_MaxFlash_1024K(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_MaxFlash_1024K(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_SIC(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_SIC(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_SC3D(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_SC3D(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_TelelinkII(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_TelelinkII(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_OSS_034M(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_OSS_034M(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_OSS_043M(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_OSS_043M(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_OSS_M091(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_OSS_M091(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_OSS_8K(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_OSS_8K(void *thisptr0, uint32 address, uint8 value);
	
	static bool WriteByte_CCTL_Corina(void *thisptr0, uint32 address, uint8 value);
	
	static bool WriteByte_CCTL_AST_32K(void *thisptr0, uint32 address, uint8 value);
	
	static sint32 ReadByte_CCTL_Turbosoft_64K(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_Turbosoft_64K(void *thisptr0, uint32 address, uint8 value);
	static sint32 ReadByte_CCTL_Turbosoft_128K(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_Turbosoft_128K(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_5200_64K_32KBanks(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_5200_64K_32KBanks(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_MicroCalc(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_MicroCalc(void *thisptr0, uint32 address, uint8 value);

	void InitMemoryLayers();
	void ShutdownMemoryLayers();
	void SetCartBank(int bank);
	void SetCartBank2(int bank);
	void UpdateCartBank();
	void UpdateCartBank2();

	ATCartridgeMode mCartMode;
	int	mCartBank;
	int	mCartBank2;
	int	mInitialCartBank;
	int	mInitialCartBank2;
	int mBasePriority;
	int mCommandPhase;
	bool mbDirty;
	IATUIRenderer *mpUIRenderer;
	IATCartridgeCallbacks *mpCB;
	ATMemoryManager *mpMemMan;
	ATScheduler *mpScheduler;
	ATMemoryLayer *mpMemLayerFixedBank1;
	ATMemoryLayer *mpMemLayerFixedBank2;
	ATMemoryLayer *mpMemLayerVarBank1;
	ATMemoryLayer *mpMemLayerVarBank2;
	ATMemoryLayer *mpMemLayerSpec1;
	ATMemoryLayer *mpMemLayerSpec2;
	ATMemoryLayer *mpMemLayerControl;

	enum FlashReadMode {
		kFlashReadMode_Normal,
		kFlashReadMode_Autoselect,
		kFlashReadMode_WriteStatus
	};

	FlashReadMode mFlashReadMode;

	ATFlashEmulator mFlashEmu;

	uint8	mSC3D[4];

	vdfastvector<uint8> mCARTROM;
	uint32	mCartSize;

	vdfastvector<uint8> mCARTRAM;
	VDStringW mImagePath;
};

int ATGetCartridgeModeForMapper(int mapper);
int ATGetCartridgeMapperForMode(int mode);
bool ATIsCartridgeModeHWCompatible(ATCartridgeMode cartmode, int hwmode);

#endif	// f_AT_CARTRIDGE_H
