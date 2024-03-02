//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2012 Avery Lee
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

#ifndef f_AT_ULTIMATE1MB_H
#define f_AT_ULTIMATE1MB_H

#include <at/atcore/devicecart.h>
#include <at/atcore/devicepbi.h>
#include "flash.h"
#include "pbi.h"
#include "rtcds1305.h"
#include "callback.h"

class ATMMUEmulator;
class ATPBIManager;
class ATMemoryManager;
class ATMemoryLayer;
class ATScheduler;
class IATUIRenderer;
class ATCPUHookManager;
class ATFirmwareManager;

class ATUltimate1MBEmulator final
	: public IATPBIDevice
	, public IATDeviceCartridge
{
	ATUltimate1MBEmulator(const ATUltimate1MBEmulator&) = delete;
	ATUltimate1MBEmulator& operator=(const ATUltimate1MBEmulator&) = delete;
public:
	ATUltimate1MBEmulator();
	~ATUltimate1MBEmulator();

	void Init(void *memory,
		ATMMUEmulator *mmu,
		ATPBIManager *pbi,
		ATMemoryManager *memman,
		IATUIRenderer *uir,
		ATScheduler *sched,
		ATCPUHookManager *hookmgr
		);
	void Shutdown();

	void SetMemoryLayers(
		ATMemoryLayer *layerLowerKernelROM,
		ATMemoryLayer *layerUpperKernelROM,
		ATMemoryLayer *layerBASICROM,
		ATMemoryLayer *layerSelfTestROM,
		ATMemoryLayer *layerGameROM);

	bool IsSoundBoardEnabled() const { return mbSoundBoardEnabled; }
	bool IsFirmwareDirty() const { return mFlashEmu.IsDirty(); }
	uint8 GetVBXEPage() const { return !mbIORAMEnabled && !mbPBISelected ? mVBXEPage : 0; }

	void SetVBXEPageCallback(ATCallbackHandler0<void> h) {
		mVBXEPageHandler = h;
	}

	void SetSBPageCallback(ATCallbackHandler0<void> h) {
		mSBPageHandler = h;
	}

	bool LoadFirmware(ATFirmwareManager& fwmgr, uint64 id);
	bool LoadFirmware(const void *p, uint32 len);
	void SaveFirmware(const wchar_t *path);

	void GetPBIDeviceInfo(ATPBIDeviceInfo& devInfo) const override;
	void SelectPBIDevice(bool enable) override;

	bool IsPBIOverlayActive() const override;
	uint8 ReadPBIStatus(uint8 busData, bool debugOnly) override;

	virtual void ColdReset();
	virtual void WarmReset();

	void LoadNVRAM();
	void SaveNVRAM();

	void DumpStatus();
	void DumpRTCStatus();

public:
	void InitCartridge(IATDeviceCartridgePort *cartPort) override;
	bool IsLeftCartActive() const override;
	void SetCartEnables(bool leftEnable, bool rightEnable, bool cctlEnable) override;
	void UpdateCartSense(bool leftActive) override;

protected:
	void SetKernelBank(uint8 bank);
	void UpdateKernelBank();
	const uint8 *GetKernelBase() const;
	void UpdateExternalCart();
	void UpdateCartLayers();
	void UpdatePBIDevice();
	void SetPBIBank(uint8 bank);
	void SetSDXBank(uint8 bank);
	void SetSDXEnabled(bool enabled);
	void SetSDXModuleEnabled(bool enabled);
	void SetIORAMEnabled(bool enabled);
	void UpdateFlashShadows();

	static sint32 DebugReadByteFlash(void *thisptr, uint32 addr);
	static sint32 ReadByteFlash(void *thisptr, uint32 addr);
	static bool WriteByteFlash(void *thisptr, uint32 addr, uint8 value);

	uint32 GetOSFlashOffset(uint32 addr);
	static sint32 ReadByteOSFlash(void *thisptr, uint32 addr);
	static bool WriteByteOSFlash(void *thisptr, uint32 addr, uint8 value);

	static sint32 ReadByteD1xx(void *thisptr, uint32 addr);
	static bool WriteByteD1xx(void *thisptr, uint32 addr, uint8 value);
	static sint32 ReadByteD3xx(void *thisptr, uint32 addr);
	static bool WriteByteD3xx(void *thisptr, uint32 addr, uint8 value);
	static sint32 ReadByteD5xx(void *thisptr, uint32 addr);
	static bool WriteByteD5xx(void *thisptr, uint32 addr, uint8 value);

	uint32 mCartBankOffset;
	uint8 mKernelBank;
	uint8 mBasicBank;
	uint8 mGameBank;
	sint8 mCurrentPBIID;
	uint8 mSelectedPBIID;
	uint8 mColdFlag;
	bool mbControlLocked;
	bool mbSDXEnabled;
	bool mbSDXModuleEnabled;
	bool mbFlashWriteEnabled;
	bool mbIORAMEnabled;
	bool mbPBISelected;
	bool mbPBIEnabled;
	bool mbPBIButton;
	bool mbExternalCartEnabled;
	bool mbExternalCartEnabledRD4;
	bool mbExternalCartEnabledRD5;
	bool mbExternalCartActive;
	bool mbSoundBoardEnabled;
	uint8 mVBXEPage;

	uint8 *mpMemory;
	ATMMUEmulator *mpMMU;
	ATPBIManager *mpPBIManager;
	IATUIRenderer *mpUIRenderer;
	ATCPUHookManager *mpHookMgr;

	IATDeviceCartridgePort *mpCartridgePort = nullptr;
	uint32 mCartId = 0;

	ATMemoryManager *mpMemMan;
	ATMemoryLayer *mpLayerCart;			// $A000-BFFF
	ATMemoryLayer *mpLayerFlashControl;	// $A000-BFFF
	ATMemoryLayer *mpLayerPBIControl;	// $D100-D1FF
	ATMemoryLayer *mpLayerPIAOverlay;	// $D300-D3FF
	ATMemoryLayer *mpLayerCartControl;	// $D500-D5FF
	ATMemoryLayer *mpLayerPBIData;		// $D600-D7FF
	ATMemoryLayer *mpLayerPBIFirmware;	// $D800-DFFF

	// externally provided layers
	ATMemoryLayer *mpMemLayerLowerKernelROM;	// $C000-CFFF
	ATMemoryLayer *mpMemLayerUpperKernelROM;	// $D800-FFFF
	ATMemoryLayer *mpMemLayerBASICROM;			// $A000-BFFF
	ATMemoryLayer *mpMemLayerSelfTestROM;		// $5000-57FF
	ATMemoryLayer *mpMemLayerGameROM;			// $A000-BFFF

	// shadows
	ATMemoryLayer *mpMemLayerLowerKernelFlash = nullptr;
	ATMemoryLayer *mpMemLayerUpperKernelFlash = nullptr;
	ATMemoryLayer *mpMemLayerBASICGameFlash = nullptr;
	ATMemoryLayer *mpMemLayerSelfTestFlash = nullptr;

	ATFlashEmulator mFlashEmu;
	ATRTCDS1305Emulator mClockEmu;

	ATCallbackHandler0<void> mVBXEPageHandler;
	ATCallbackHandler0<void> mSBPageHandler;

	VDALIGN(4) uint8 mFirmware[0x80000];
};

#endif	// f_AT_ULTIMATE1MB_H
