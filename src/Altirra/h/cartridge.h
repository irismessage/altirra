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
#include <at/atcore/devicecart.h>
#include "flash.h"
#include "eeprom.h"

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
	kATCartridgeMode_MegaCart_1M_2,			// Hardware by Bernd for ABBUC JHV 2009
	kATCartridgeMode_5200_64K_32KBanks,		// Used by M.U.L.E. 64K conversion
	kATCartridgeMode_5200_512K_32KBanks,
	kATCartridgeMode_MicroCalc,
	kATCartridgeMode_2K,
	kATCartridgeMode_4K,
	kATCartridgeMode_RightSlot_4K,
	kATCartridgeMode_MegaCart_512K_3,
	kATCartridgeMode_MegaCart_2M_3,
	kATCartridgeMode_MegaCart_4M_3,
	kATCartridgeMode_TheCart_32M,
	kATCartridgeMode_TheCart_64M,
	kATCartridgeMode_TheCart_128M,
	kATCartridgeMode_MegaMax_2M,
	kATCartridgeMode_BountyBob5200Alt,		// Same as cart mapper 7 except that the fixed bank is first.
	kATCartridgeModeCount
};

enum ATCartLoadStatus {
	kATCartLoadStatus_Ok,
	kATCartLoadStatus_UnknownMapper,
	kATCartLoadStatus_HardwareMismatch
};

struct ATCartLoadContext {
	bool mbReturnOnUnknownMapper = false;
	int mCartMapper = -1;
	int mHardwareModeCheck = -1;
	uint32 mCartSize = 0;

	ATCartLoadStatus mLoadStatus = kATCartLoadStatus_Ok;

	vdfastvector<uint8> *mpCaptureBuffer;
};

class ATCartridgeEmulator final
	: public IATDeviceCartridge
{
	ATCartridgeEmulator(const ATCartridgeEmulator&) = delete;
	ATCartridgeEmulator& operator=(const ATCartridgeEmulator&) = delete;

public:
	ATCartridgeEmulator();
	~ATCartridgeEmulator();

	void Init(ATMemoryManager *memman, ATScheduler *sch, int basePriority, ATCartridgePriority cartPri, bool fastBus);
	void Shutdown();

	void SetUIRenderer(IATUIRenderer *r);

	void SetFastBus(bool fastBus);

	int GetCartBank() const { return mCartBank; }
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

public:		// IATDeviceCartridge
	void InitCartridge(IATDeviceCartridgePort *cartPort) override;
	bool IsLeftCartActive() const override;
	void SetCartEnables(bool leftEnable, bool rightEnable, bool cctlEnable) override;
	void UpdateCartSense(bool leftActive) override;

protected:
	struct LayerEnables {
		bool mbRead;
		bool mbWrite;
		bool mbRD5Masked;

		void Init(uint8 pageBase, uint8 pageSize);
	};

	template<class T> void ExchangeState(T& io);

	static sint32 ReadByte_BB5200_1(void *thisptr0, uint32 address);
	static sint32 ReadByte_BB5200_2(void *thisptr0, uint32 address);
	static bool WriteByte_BB5200_1(void *thisptr0, uint32 address, uint8 value);
	static bool WriteByte_BB5200_2(void *thisptr0, uint32 address, uint8 value);
	static sint32 ReadByte_BB800_1(void *thisptr0, uint32 address);
	static sint32 ReadByte_BB800_2(void *thisptr0, uint32 address);
	static bool WriteByte_BB800_1(void *thisptr0, uint32 address, uint8 value);
	static bool WriteByte_BB800_2(void *thisptr0, uint32 address, uint8 value);
	static sint32 DebugReadByte_SIC(void *thisptr0, uint32 address);
	static sint32 ReadByte_SIC(void *thisptr0, uint32 address);
	static bool WriteByte_SIC(void *thisptr0, uint32 address, uint8 value);
	static sint32 DebugReadByte_TheCart(void *thisptr0, uint32 address);
	static sint32 ReadByte_TheCart(void *thisptr0, uint32 address);
	static bool WriteByte_TheCart(void *thisptr0, uint32 address, uint8 value);
	static sint32 DebugReadByte_MegaCart3(void *thisptr0, uint32 address);
	static sint32 ReadByte_MegaCart3(void *thisptr0, uint32 address);
	static bool WriteByte_MegaCart3(void *thisptr0, uint32 address, uint8 value);
	static sint32 DebugReadByte_MaxFlash(void *thisptr0, uint32 address);
	static sint32 ReadByte_MaxFlash(void *thisptr0, uint32 address);
	static bool WriteByte_MaxFlash(void *thisptr0, uint32 address, uint8 value);
	static bool WriteByte_Corina1M(void *thisptr0, uint32 address, uint8 value);
	static bool WriteByte_Corina512K(void *thisptr0, uint32 address, uint8 value);
	static bool WriteByte_TelelinkII(void *thisptr0, uint32 address, uint8 value);

	static bool WriteByte_CCTL_Phoenix(void *thisptr0, uint32 address, uint8 value);

	template<uint8 T_Mask>
	static bool WriteByte_CCTL_AddressToBank(void *thisptr0, uint32 address, uint8 value);

	template<uint8 T_Mask>
	static sint32 ReadByte_CCTL_AddressToBank_Switchable(void *thisptr0, uint32 address);

	template<uint8 T_Mask>
	static bool WriteByte_CCTL_AddressToBank_Switchable(void *thisptr0, uint32 address, uint8 value);

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

	static sint32 ReadByte_CCTL_TheCart(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_TheCart(void *thisptr0, uint32 address, uint8 value);

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

	static sint32 ReadByte_CCTL_5200_512K_32KBanks(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_5200_512K_32KBanks(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_MicroCalc(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_MicroCalc(void *thisptr0, uint32 address, uint8 value);

	static sint32 ReadByte_CCTL_MegaCart3(void *thisptr0, uint32 address);
	static bool WriteByte_CCTL_MegaCart3(void *thisptr0, uint32 address, uint8 value);

	void InitMemoryLayers();
	void ShutdownMemoryLayers();
	void SetCartBank(int bank);
	void SetCartBank2(int bank);
	void UpdateCartBank();
	void UpdateCartBank2();
	void UpdateLayerBuses();
	void UpdateLayerMasks();
	void UpdateTheCartBanking();
	void UpdateTheCart();

	ATCartridgeMode mCartMode;
	int	mCartBank;
	int	mCartBank2;
	uint32 mCartSizeMask;
	int	mInitialCartBank;
	int	mInitialCartBank2;
	int mBasePriority;
	bool mbDirty;
	bool mbRD4Gate;
	bool mbRD5Gate;
	bool mbCCTLGate;
	bool mbFastBus;
	IATUIRenderer *mpUIRenderer;
	ATMemoryManager *mpMemMan;
	ATScheduler *mpScheduler;
	IATDeviceCartridgePort *mpCartridgePort = nullptr;
	uint32 mCartId = 0;

	LayerEnables mLayerEnablesFixedBank1;
	LayerEnables mLayerEnablesFixedBank2;
	LayerEnables mLayerEnablesVarBank1;
	LayerEnables mLayerEnablesVarBank2;
	LayerEnables mLayerEnablesSpec1;
	LayerEnables mLayerEnablesSpec2;

	ATMemoryLayer *mpMemLayerFixedBank1;
	ATMemoryLayer *mpMemLayerFixedBank2;
	ATMemoryLayer *mpMemLayerVarBank1;
	ATMemoryLayer *mpMemLayerVarBank2;
	ATMemoryLayer *mpMemLayerSpec1;
	ATMemoryLayer *mpMemLayerSpec2;
	ATMemoryLayer *mpMemLayerControl;

	ATFlashEmulator mFlashEmu;
	ATFlashEmulator mFlashEmu2;
	ATEEPROMEmulator mEEPROM;

	union {
		uint8	mSC3D[4];
		uint8	mTheCartRegs[9];
	};

	vdfastvector<uint8> mCARTROM;
	uint32	mCartSize;

	vdfastvector<uint8> mCARTRAM;
	VDStringW mImagePath;

	vdblock<sint16> mTheCartBankInfo;
	bool mbTheCartBankByAddress;
	uint8 mTheCartOSSBank;
	uint8 mTheCartSICEnables;
	uint16 mTheCartBankMask;
	bool mbTheCartConfigLock;

	enum TheCartBankMode {
		kTheCartBankMode_Disabled,
		kTheCartBankMode_8K,
		kTheCartBankMode_16K,
		kTheCartBankMode_Flexi,
		kTheCartBankMode_8KFixed_8K,
		kTheCartBankMode_OSS,		// 4K fixed + 4K variable
		kTheCartBankMode_SIC		// 16K, independently switchable halves
	};

	TheCartBankMode mTheCartBankMode;
};

int ATGetCartridgeModeForMapper(int mapper);
int ATGetCartridgeMapperForMode(int mode, uint32 size);
bool ATIsCartridge5200Mode(ATCartridgeMode cartmode);
bool ATIsCartridgeModeHWCompatible(ATCartridgeMode cartmode, int hwmode);

#endif	// f_AT_CARTRIDGE_H
