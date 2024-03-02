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

#include <stdafx.h>
#include <vd2/system/binary.h>
#include <vd2/system/file.h>
#include <vd2/system/registry.h>
#include <vd2/system/hash.h>
#include <vd2/system/int128.h>
#include <at/atcore/consoleoutput.h>
#include <at/atcore/devicestorage.h>
#include <at/atcore/snapshotimpl.h>
#include "ultimate1mb.h"
#include "mmu.h"
#include "pbi.h"
#include "memorymanager.h"
#include "console.h"
#include "uirender.h"
#include "simulator.h"
#include "cpuhookmanager.h"
#include "options.h"
#include "firmwaremanager.h"

ATUltimate1MBEmulator::ATUltimate1MBEmulator() {
	memset(mFirmware, 0xFF, sizeof mFirmware);
}

ATUltimate1MBEmulator::~ATUltimate1MBEmulator() {
}

void *ATUltimate1MBEmulator::AsInterface(uint32 iid) {
	switch(iid) {
		case IATDeviceCartridge::kTypeID: return static_cast<IATDeviceCartridge *>(this);
		case IATDeviceSystemControl::kTypeID: return static_cast<IATDeviceSystemControl *>(this);
	}
	
	return nullptr;
}

void ATUltimate1MBEmulator::Init(
		void *memory,
		ATMMUEmulator *mmu,
		ATPBIManager *pbi,
		ATMemoryManager *memman,
		IATUIRenderer *uir,
		ATScheduler *sched,
		ATCPUHookManager *hookmgr,
		IATDeviceManager *devicemgr
	)
{
	mpMemory = (uint8 *)memory;
	mpMMU = mmu;
	mpUIRenderer = uir;
	mpPBIManager = pbi;
	mpMemMan = memman;
	mpHookMgr = hookmgr;
	mpDeviceMgr = devicemgr;

	mClockEmu.Init();

	mRTCStorage.Init(*mpDeviceMgr->GetService<IATDeviceStorageManager>(),
		[this](IATDeviceStorageManager&) { LoadNVRAM(); },
		[this](IATDeviceStorageManager&) { SaveNVRAM(); }
	);

	if (g_ATOptions.mU1MBFlashChip == "BM29F040")
		mFlashEmu.Init(mFirmware, kATFlashType_BM29F040, sched);
	else if (g_ATOptions.mU1MBFlashChip == "Am29F040B")
		mFlashEmu.Init(mFirmware, kATFlashType_Am29F040B, sched);
	else if (g_ATOptions.mU1MBFlashChip == "SST39SF040")
		mFlashEmu.Init(mFirmware, kATFlashType_SST39SF040, sched);
	else
		mFlashEmu.Init(mFirmware, kATFlashType_A29040, sched);

	mCurrentPBIID = 0;

	// hook the cartridge port
	mpCartridgePort->AddCartridge(this, kATCartridgePriority_MMU, mCartId);

	// establish layers

	ATMemoryHandlerTable handlers;
	handlers.mbPassReads = true;
	handlers.mbPassAnticReads = true;
	handlers.mbPassWrites = true;
	handlers.mpThis = this;

	handlers.mpDebugReadHandler = ReadByteD1xx;
	handlers.mpReadHandler = ReadByteD1xx;
	handlers.mpWriteHandler = WriteByteD1xx;

	mpLayerPBIControl = memman->CreateLayer(kATMemoryPri_PBI, handlers, 0xD1, 0x01);
	memman->SetLayerName(mpLayerPBIControl, "Ultimate1MB PBI control");

	handlers.mpDebugReadHandler = ReadByteD3xx;
	handlers.mpReadHandler = ReadByteD3xx;
	handlers.mpWriteHandler = WriteByteD3xx;
	mpLayerPIAOverlay = memman->CreateLayer(kATMemoryPri_HardwareOverlay, handlers, 0xD3, 0x01);
	memman->SetLayerName(mpLayerPIAOverlay, "Ultimate1MB PIA overlay");
	memman->EnableLayer(mpLayerPIAOverlay, true);

	handlers.mpDebugReadHandler = ReadByteD5xx;
	handlers.mpReadHandler = ReadByteD5xx;
	handlers.mpWriteHandler = WriteByteD5xx;
	mpLayerCartControl = memman->CreateLayer(kATMemoryPri_CartridgeOverlay + 1, handlers, 0xD5, 0x01);		// must be higher than SIDE
	memman->SetLayerName(mpLayerCartControl, "Ultimate1MB CCTL");
	memman->EnableLayer(mpLayerCartControl, true);

	handlers.mpDebugReadHandler = DebugReadByteFlash;
	handlers.mpReadHandler = ReadByteFlash;
	handlers.mpWriteHandler = WriteByteFlash;
	mpLayerFlashControl = memman->CreateLayer(kATMemoryPri_CartridgeOverlay + 3, handlers, 0xA0, 0x20);
	memman->SetLayerName(mpLayerFlashControl, "Ultimate1MB flash control");

	handlers.mbPassAnticReads = false;
	handlers.mbPassReads = false;
	handlers.mbPassWrites = false;
	handlers.mpDebugReadHandler = ReadByteOSFlash;
	handlers.mpReadHandler = ReadByteOSFlash;
	handlers.mpWriteHandler = WriteByteOSFlash;

	mpMemLayerLowerKernelFlash = memman->CreateLayer(kATMemoryPri_ROM + 1, handlers, 0xC0, 0x10);
	memman->SetLayerName(mpMemLayerLowerKernelFlash, "Ultimate1MB lower kernel flash overlay");

	mpMemLayerUpperKernelFlash = memman->CreateLayer(kATMemoryPri_ROM + 1, handlers, 0xD8, 0x28);
	memman->SetLayerName(mpMemLayerUpperKernelFlash, "Ultimate1MB upper kernel flash overlay");

	mpMemLayerBASICGameFlash = memman->CreateLayer(kATMemoryPri_ROM + 1, handlers, 0xA0, 0x20);
	memman->SetLayerName(mpMemLayerBASICGameFlash, "Ultimate1MB self-test flash overlay");

	mpMemLayerSelfTestFlash = memman->CreateLayer(kATMemoryPri_ROM + 1, handlers, 0x50, 0x08);
	memman->SetLayerName(mpMemLayerSelfTestFlash, "Ultimate1MB BASIC/GAME kernel flash overlay");

	mpLayerCart = memman->CreateLayer(kATMemoryPri_CartridgeOverlay + 2, mFirmware, 0xA0, 0x20, true);
	memman->SetLayerName(mpLayerCart, "Ultimate1MB cart window");

	// This needs to be higher priority than the VBXE and SoundBoard register files.
	mpLayerPBIData = memman->CreateLayer(kATMemoryPri_HardwareOverlay + 2, mpMemory + 0xD600, 0xD6, 0x02, false);
	memman->SetLayerName(mpLayerPBIData, "Ultimate1MB PBI RAM");

	mpLayerPBIFirmware = memman->CreateLayer(kATMemoryPri_PBI, mFirmware, 0xD8, 0x08, true);
	memman->SetLayerName(mpLayerPBIFirmware, "Ultimate1MB PBI firmware");

	// must occur after we've set up the layers
	mpMMU->SetROMMappingHook(
		[this]() {
			UpdateFlashShadows();
		}
	);

	mStereoEnableOutput.SetValue(true);
	mStereoEnableOutput.Connect(mpSystemController->GetStereoEnableSignal());
	mCovoxEnableOutput.SetValue(true);
	mCovoxEnableOutput.Connect(mpSystemController->GetCovoxEnableSignal());
}

void ATUltimate1MBEmulator::Shutdown() {
	mStereoEnableOutput.Disconnect();
	mCovoxEnableOutput.Disconnect();

	if (mpMMU) {
		// must happen before we clear the layers
		mpMMU->SetROMMappingHook(nullptr);
	}

	if (mpMemMan) {
		for(ATMemoryLayer **layerPtr : {
			&mpLayerPBIControl,
			&mpLayerPIAOverlay,
			&mpLayerCartControl,
			&mpLayerFlashControl,
			&mpLayerCart,
			&mpLayerPBIData,
			&mpLayerPBIFirmware,
			&mpMemLayerLowerKernelFlash,
			&mpMemLayerUpperKernelFlash,
			&mpMemLayerBASICGameFlash,
			&mpMemLayerSelfTestFlash,
		}) {
			mpMemMan->DeleteLayer(*layerPtr);
			*layerPtr = nullptr;
		}

		mpMemMan = nullptr;
	}

	mRTCStorage.Shutdown();

	if (mpPBIManager) {
		if (mCurrentPBIID >= 0) {
			mpPBIManager->RemoveDevice(this);
			mCurrentPBIID = -1;
		}

		mpPBIManager = nullptr;
	}

	mpUIRenderer = nullptr;

	if (mpMMU) {
		mpMMU->ClearModeOverrides();
		mpMMU = nullptr;
	}

	if (mpCartridgePort) {
		mpCartridgePort->RemoveCartridge(mCartId, this);
		mpCartridgePort = nullptr;
	}

	if (mpSystemController) {
		mpSystemController->OnU1MBConfigPreLocked(false);
		mpSystemController = nullptr;
	}

	mpMemory = nullptr;
	mpHookMgr = nullptr;
	mpDeviceMgr = nullptr;

	mFlashEmu.Shutdown();
}

bool ATUltimate1MBEmulator::LoadFirmware(ATFirmwareManager& fwmgr, uint64 id) {
	const vduint128 oldHash = VDHash128(mFirmware, sizeof mFirmware);

	memset(mFirmware, 0xFF, sizeof mFirmware);

	fwmgr.LoadFirmware(id, mFirmware, 0, sizeof mFirmware);

	return oldHash != VDHash128(mFirmware, sizeof mFirmware);
}

bool ATUltimate1MBEmulator::LoadFirmware(const void *p, uint32 len) {
	const vduint128 oldHash = VDHash128(mFirmware, sizeof mFirmware);

	if (len > sizeof mFirmware)
		len = sizeof mFirmware;

	if (p)
		memcpy(mFirmware, p, len);
	memset(mFirmware + len, 0xFF, (sizeof mFirmware) - len);

	return oldHash != VDHash128(mFirmware, sizeof mFirmware);
}

void ATUltimate1MBEmulator::SaveFirmware(const wchar_t *path) {
	VDFile f(path, nsVDFile::kWrite|nsVDFile::kDenyAll|nsVDFile::kCreateAlways);
	f.write(mFirmware, sizeof mFirmware);
	mFlashEmu.SetDirty(false);
}

void ATUltimate1MBEmulator::GetPBIDeviceInfo(ATPBIDeviceInfo& devInfo) const {
	devInfo.mDeviceId = mCurrentPBIID;
	devInfo.mbHasIrq = false;
}

void ATUltimate1MBEmulator::SelectPBIDevice(bool enable) {
	if (mbPBISelected == enable)
		return;

	mbPBISelected = enable;

	if (!enable)
		SetPBIBank(0);

	if (mpLayerPBIFirmware)
		mpMemMan->EnableLayer(mpLayerPBIFirmware, enable);

	if (mpLayerPBIData) {
		mpMemMan->EnableLayer(mpLayerPBIControl, mbPBISelected || !mbControlLocked);
		mpMemMan->EnableLayer(mpLayerPBIData, mbIORAMEnabled || mbPBISelected);
	}

	if (mVBXEPage && mVBXEPageHandler)
		mVBXEPageHandler();
}

bool ATUltimate1MBEmulator::IsPBIOverlayActive() const {
	return mbPBISelected;
}

uint8 ATUltimate1MBEmulator::ReadPBIStatus(uint8 busData, bool debugOnly) {
	return busData;
}

void ATUltimate1MBEmulator::ColdReset() {
	mFlashEmu.ColdReset();

	mColdFlag = 0x80;		// ONLY set by cold reset, not warm reset.

	// The SDX module is enabled on warm reset, but the cart enables and bank
	// are only affected by cold reset.
	mCartBankOffset = 1;		// to force reload
	mbSDXEnabled = false;
	SetSDXBank(0);
	SetSDXEnabled(true);
	mbExternalCartEnabled = false;
	mbFlashWriteEnabled = true;

	mVBXEPage = 0xD6;
	mbSoundBoardEnabled = false;
	mStereoEnableOutput.SetValue(true);
	mCovoxEnableOutput.SetValue(true);

	if (mVBXEPageHandler)
		mVBXEPageHandler();

	if (mSBPageHandler)
		mSBPageHandler();

	WarmReset();
}

void ATUltimate1MBEmulator::WarmReset() {
	mClockEmu.ColdReset();

	mMemoryMode = 3;
	UpdateMemoryMode();
	mpHookMgr->EnableOSHooks(false);

	mbControlLocked = false;

	mbPBIEnabled = false;
	mbPBIButton = false;
	mSelectedPBIID = 0;

	UpdatePBIDevice();

	mKernelBank = 0;
	mGameBank = 0;
	mBasicBank = 2;

	UpdateKernelBank();
	SetPBIBank(0);

	SetSDXModuleEnabled(true);

	UpdateExternalCart();
	UpdateCartLayers();
	UpdateFlashShadows();

	// The $D1xx layer is always enabled after reset since config is unlocked.
	mpMemMan->EnableLayer(mpLayerPBIControl, true);

	mpSystemController->OnU1MBConfigPreLocked(true);
}

void ATUltimate1MBEmulator::LoadNVRAM() {
	uint8 buf[0x72];
	memset(buf, 0, sizeof buf);

	if (mpDeviceMgr->GetService<IATDeviceStorageManager>()->LoadNVRAM("Ultimate1MB clock", buf, 0x72))
		mClockEmu.Load(buf);
}

void ATUltimate1MBEmulator::SaveNVRAM() {
	uint8 buf[0x72];
	memset(buf, 0, sizeof buf);

	mClockEmu.Save(buf);

	mpDeviceMgr->GetService<IATDeviceStorageManager>()->SaveNVRAM("Ultimate1MB clock", buf, 0x72);
}

void ATUltimate1MBEmulator::DumpStatus(ATConsoleOutput& output) {
	output <<= "Ultimate1MB status:";
	output("Control registers   %s", mbControlLocked ? "locked" : "unlocked");
	output("Kernel bank         %u ($%05x)", mKernelBank, 0x70000 + ((uint32)mKernelBank << 14));
	output("BASIC bank          %u ($%05x)", mBasicBank, 0x60000 + ((uint32)mBasicBank << 13));
	output("Game bank           %u ($%05x)", mGameBank, 0x68000 + ((uint32)mGameBank << 13));
	output("Cartridge bank      $%05x (%s)", mCartBankOffset, mbSDXEnabled ? "enabled" : "disabled");
	output("I/O memory          %s", mbIORAMEnabled ? "enabled" : "disabled");
	output("Flash writes        %s", mbFlashWriteEnabled ? "enabled" : "protected");
	output("PBI device ID       $%02x (%s)", mSelectedPBIID, mbPBISelected ? "selected" : mbPBIEnabled ? "enabled" : "disabled");
	output("PBI button status   %s", mbExternalCartActive ? "external cart active" : "external cart inactive");
	output("External cart ROM   %s", mbExternalCartEnabled ? mbPBIButton ? "$8000-9FFF only (PBI button mode)" : "enabled" : "disabled");
	output("VBXE decoder        $%02x00", GetVBXEPage());
	output("SoundBoard decoder  %s", mbSoundBoardEnabled ? "$D2C0" : "disabled");
}

void ATUltimate1MBEmulator::DumpRTCStatus(ATConsoleOutput& output) {
	mClockEmu.DumpStatus(output);
}

class ATSaveStateUltimate1MB final : public ATSnapExchangeObject<ATSaveStateUltimate1MB, "ATSaveStateUltimate1MB"> {
public:
	template<ATExchanger T>
	void Exchange(T& ex);

	// For clarity, we pack/unpack architectural state into the original register
	// definitions.
	uint8 mUCTL = 0;
	uint8 mUAUX = 0;
	uint8 mUPBI = 0;
	uint8 mCOLDF = 0;
	uint8 mSDXCTL = 0;
};

template<ATExchanger T>
void ATSaveStateUltimate1MB::Exchange(T& ex) {
	ex.Transfer("uctl", &mUCTL);
	ex.Transfer("uaux", &mUAUX);
	ex.Transfer("upbi", &mUPBI);
	ex.Transfer("coldf", &mCOLDF);
	ex.Transfer("sdxctl", &mSDXCTL);
}

void ATUltimate1MBEmulator::LoadState(const IATObjectState *state, ATSnapshotContext& ctx) {
	if (!state) {
		const ATSaveStateUltimate1MB defaultState;
		return LoadState(&defaultState, ctx);
	}

	// Unpack architectural register state.
	//
	// Note that there are some significant differences between this code path
	// and the register write path. The most critical one is that we need to
	// bypass config lock here.

	const ATSaveStateUltimate1MB& u1state = atser_cast<const ATSaveStateUltimate1MB&>(*state);

	mMemoryMode = u1state.mUCTL & 3;
	UpdateMemoryMode();

	SetKernelBank((u1state.mUCTL >> 2) & 3);
	SetSDXEnabled((u1state.mUCTL & 0x10) == 0);
	SetIORAMEnabled((u1state.mUCTL & 0x40) != 0);
	mbControlLocked = (u1state.mUCTL & 0x80) != 0;

	mStereoEnableOutput.SetValue((u1state.mUAUX & 0x01) != 0);
	mCovoxEnableOutput.SetValue((u1state.mUAUX & 0x02) != 0);

	SetUAUX(u1state.mUAUX);
	SetUPBI(u1state.mUPBI);

	mColdFlag = u1state.mCOLDF & 0x80;

	// update SDX bank and cart enables - must be after control lock is loaded
	SetSDXCTL(u1state.mSDXCTL);

	// update PBI control layer state as it is sensitive to config lock
	mpMemMan->EnableLayer(mpLayerPBIControl, !mbControlLocked || mbPBISelected);

	// recompute derived state
	UpdateKernelBank();
	UpdatePBIDevice();
	UpdateHooks();

	mpSystemController->OnU1MBConfigPreLocked(!mbControlLocked);
}

vdrefptr<IATObjectState> ATUltimate1MBEmulator::SaveState(ATSnapshotContext& ctx) const {
	vdrefptr state { new ATSaveStateUltimate1MB };

	// reconstruct architectural register state
	state->mUCTL += mMemoryMode;
	state->mUCTL += mKernelBank << 2;
	state->mUCTL += mbSDXEnabled ? 0x00 : 0x10;
	state->mUCTL += mbIORAMEnabled ? 0x40 : 0x00;
	state->mUCTL += mbControlLocked ? 0x80 : 0x00;

	state->mUAUX += mStereoEnableOutput.GetValue() ? 0x01 : 0x00;
	state->mUAUX += mCovoxEnableOutput.GetValue() ? 0x02 : 0x00;

	state->mUAUX +=
		  mVBXEPage == 0xD6 ? 0x00
		: mVBXEPage == 0xD7 ? 0x10
		: 0x20;

	state->mUAUX += mbSoundBoardEnabled ? 0x40 : 0x00;
	state->mUAUX += mbFlashWriteEnabled ? 0x00 : 0x80;

	if (mbPBIEnabled) {
		switch(mSelectedPBIID) {
			case 0x01:
			default:
				state->mUPBI += 0x00;
				break;

			case 0x04:
				state->mUPBI += 0x01;
				break;

			case 0x10:
				state->mUPBI += 0x02;
				break;

			case 0x40:
				state->mUPBI += 0x03;
				break;
		}
	}

	state->mUPBI += mbPBIButton ? 0x08 : 0x00;
	state->mUPBI += mBasicBank << 4;
	state->mUPBI += mGameBank << 6;

	state->mCOLDF = mColdFlag;

	state->mSDXCTL = (mCartBankOffset >> 13) & 0x3F;

	if (!mbSDXEnabled) {
		if (!mbExternalCartEnabled)
			state->mSDXCTL += 0x80;
		else
			state->mSDXCTL += 0xC0;
	}

	return state;
}

void ATUltimate1MBEmulator::InitCartridge(IATDeviceCartridgePort *cartPort) {
	mpCartridgePort = cartPort;
}

bool ATUltimate1MBEmulator::IsLeftCartActive() const {
	return mbSDXEnabled && mbSDXModuleEnabled;
}

void ATUltimate1MBEmulator::SetCartEnables(bool leftEnable, bool rightEnable, bool cctlEnable) {
	// U1MB is at the head and really shouldn't be disabled (it's the MMU!).
	VDASSERT(leftEnable && rightEnable && cctlEnable);
}

void ATUltimate1MBEmulator::UpdateCartSense(bool leftActive) {
	mbExternalCartActive = leftActive;
}

void ATUltimate1MBEmulator::InitSystemControl(IATSystemController *sysctrl) {
	mpSystemController = sysctrl;
}

void ATUltimate1MBEmulator::SetROMLayers(
	ATMemoryLayer *layerLowerKernelROM,
	ATMemoryLayer *layerUpperKernelROM,
	ATMemoryLayer *layerBASICROM,
	ATMemoryLayer *layerSelfTestROM,
	ATMemoryLayer *layerGameROM,
	const void *kernelROM)
{
	mpMemLayerLowerKernelROM = layerLowerKernelROM;
	mpMemLayerUpperKernelROM = layerUpperKernelROM;
	mpMemLayerBASICROM = layerBASICROM;
	mpMemLayerSelfTestROM = layerSelfTestROM;
	mpMemLayerGameROM = layerGameROM;

	if (mpMemMan)
		UpdateKernelBank();
}

void ATUltimate1MBEmulator::OnU1MBConfigPreLocked(bool inPreLockState) {
	// we trigger this ourselves, so don't respond to the notification
}

void ATUltimate1MBEmulator::SetKernelBank(uint8 bank) {
	if (mKernelBank != bank) {
		mKernelBank = bank;

		UpdateKernelBank();
	}
}

void ATUltimate1MBEmulator::UpdateKernelBank() {
	// Prior to control lock, the kernel bank is locked to $50000 instead
	// of $7x000. The BASIC, GAME, and PBI selects also act weirdly in this
	// mode (they are mapped with the SDX mapping register!).

	const uint8 *kernelbase = GetKernelBase();

	mpSystemController->OverrideKernelMapping(this, kernelbase, 0, false);

	if (mpMemLayerBASICROM)
		mpMemMan->SetLayerMemory(mpMemLayerBASICROM, mFirmware + (mbControlLocked ? 0x60000 + ((uint32)mBasicBank << 13) : mCartBankOffset));

	if (mpMemLayerGameROM)
		mpMemMan->SetLayerMemory(mpMemLayerGameROM, mFirmware + (mbControlLocked ? 0x68000 + ((uint32)mGameBank << 13) : mCartBankOffset));
}

const uint8 *ATUltimate1MBEmulator::GetKernelBase() const {
	return mFirmware + (mbControlLocked ? 0x70000 + ((uint32)mKernelBank << 14) : 0x50000);
}

void ATUltimate1MBEmulator::UpdateCartLayers() {
	const bool enabled = mbSDXEnabled && mbSDXModuleEnabled;

	mpMemMan->EnableLayer(mpLayerCart, enabled);
	mpMemMan->EnableLayer(mpLayerFlashControl, kATMemoryAccessMode_CPUWrite, enabled && mbFlashWriteEnabled);

	const bool flashRead = enabled && mFlashEmu.IsControlReadEnabled();
	mpMemMan->EnableLayer(mpLayerFlashControl, kATMemoryAccessMode_CPURead, flashRead);
	mpMemMan->EnableLayer(mpLayerFlashControl, kATMemoryAccessMode_AnticRead, flashRead);
}

void ATUltimate1MBEmulator::UpdateExternalCart() {
	const bool extrd4 = mbExternalCartEnabled;
	const bool extrd5 = mbExternalCartEnabled && !mbPBIButton;

	if (mbExternalCartEnabledRD4 != extrd4 ||
		mbExternalCartEnabledRD5 != extrd5)
	{
		mbExternalCartEnabledRD4 = extrd4;
		mbExternalCartEnabledRD5 = extrd5;

		mpCartridgePort->EnablePassThrough(mCartId, extrd5, extrd4, true);
	}
}

void ATUltimate1MBEmulator::UpdatePBIDevice() {
	sint8 newID = mbPBIEnabled && mbControlLocked ? mSelectedPBIID : 0;

	if (newID != mCurrentPBIID) {
		if (mCurrentPBIID)
			mpPBIManager->RemoveDevice(this);

		mCurrentPBIID = newID;

		if (newID)
			mpPBIManager->AddDevice(this);
	}
}

void ATUltimate1MBEmulator::UpdateMemoryMode() {
	static constexpr ATMemoryMode kMemModes[4]={
		kATMemoryMode_64K,
		kATMemoryMode_320K,
		kATMemoryMode_576K_Compy,
		kATMemoryMode_1088K
	};

	mpMMU->SetModeOverrides(kMemModes[mMemoryMode], true);
}

void ATUltimate1MBEmulator::UpdateHooks() {
	bool validOS = false;

	if (mbControlLocked) {
		// Check the kernel and see if it's a sensible one that we can enable OS
		// hooks on.

		const uint8 *kernelbase = mFirmware + 0x70000 + ((uint32)mKernelBank << 14) - 0xC000;
		validOS = true;

		// not an OS kernel if any of the CIO vectors are outside of ROM
		// careful: these vectors are -1, so $BFFF is within range
		for(uint32 i=0xE400; i<=0xE440; i+=16) {
			for(uint32 j=0; j<=10; j+=2) {
				if (VDReadUnalignedLEU16(&kernelbase[i+j]) < 0xBFFF)
					validOS = false;
			}
		}

		// not an OS kernel if any of the standard vectors are not a JMP or JSR
		// (we allow JSR or our internal kernel ROM that has bugchecks)
		for(uint32 i=0xE450; i<=0xE47D; i+=3) {
			if (kernelbase[i] != 0x4C && kernelbase[i] != 0x20) {
				validOS = false;
				break;
			}

			if (kernelbase[i+2] < 0xC0) {
				validOS = false;
				break;
			}
		}
	}
					
	if (validOS)
		mpHookMgr->EnableOSHooks(validOS);
}

void ATUltimate1MBEmulator::SetPBIBank(uint8 bank) {
	if (mpLayerPBIFirmware)
		mpMemMan->SetLayerMemory(mpLayerPBIFirmware, mFirmware + 0x59800 + ((uint32)bank << 13));
}

void ATUltimate1MBEmulator::SetSDXBank(uint8 bank) {
	uint32 offset = (uint32)bank << 13;

	if (mCartBankOffset == offset)
		return;

	mCartBankOffset = offset;

	mpMemMan->SetLayerMemory(mpLayerCart, mFirmware + mCartBankOffset);
}

void ATUltimate1MBEmulator::SetSDXEnabled(bool enabled) {
	if (mbSDXEnabled == enabled)
		return;

	mbSDXEnabled = enabled;

	UpdateCartLayers();

	mpCartridgePort->OnLeftWindowChanged(mCartId, IsLeftCartActive());
}

void ATUltimate1MBEmulator::SetSDXModuleEnabled(bool enabled) {
	if (mbSDXModuleEnabled == enabled)
		return;

	mbSDXModuleEnabled = enabled;

	if (!enabled) {
		SetSDXBank(0);
		SetSDXEnabled(false);
		mbExternalCartEnabled = true;
		UpdateExternalCart();
	}

	UpdateCartLayers();

	mpCartridgePort->OnLeftWindowChanged(mCartId, IsLeftCartActive());
}

void ATUltimate1MBEmulator::SetIORAMEnabled(bool enabled) {
	if (mbIORAMEnabled == enabled)
		return;

	mbIORAMEnabled = enabled;

	mpMemMan->EnableLayer(mpLayerPBIData, enabled || mbPBISelected);

	if (mVBXEPage && mVBXEPageHandler)
		mVBXEPageHandler();
}

void ATUltimate1MBEmulator::SetUAUX(uint8 value) {
	const uint8 vbxePage = (value & 0x20) ? 0 : (value & 0x10) ? 0xD7 : 0xD6;

	mStereoEnableOutput.SetValue((value & 0x01) != 0);
	mCovoxEnableOutput.SetValue((value & 0x02) != 0);

	if (mVBXEPage != vbxePage) {
		mVBXEPage = vbxePage;

		if (mVBXEPageHandler)
			mVBXEPageHandler();
	}

	const bool sb = (value & 0x40) != 0;

	if (mbSoundBoardEnabled != sb) {
		mbSoundBoardEnabled = sb;

		if (mSBPageHandler)
			mSBPageHandler();
	}

	bool flashWritesEnabled = !(value & 0x80);
	if (mbFlashWriteEnabled != flashWritesEnabled) {
		mbFlashWriteEnabled = flashWritesEnabled;
		UpdateCartLayers();
		UpdateFlashShadows();
	}
}

void ATUltimate1MBEmulator::SetUPBI(uint8 value) {
	mSelectedPBIID = 0x01 << (2 * (value & 3));
	mbPBIEnabled = (value & 4) != 0;
	mbPBIButton = (value & 8) != 0;
	mBasicBank = (value >> 4) & 3;
	mGameBank = (value >> 6) & 3;
	UpdatePBIDevice();
	UpdateExternalCart();
	UpdateKernelBank();
}

void ATUltimate1MBEmulator::SetSDXCTL(uint8 value) {
	// SDX control (write only)
	//
	// D7 = 0	=> Enable SDX
	// D6 = 0	=> Enable external cartridge (only if SDX disabled)
	// D0:D5	=> SDX bank
	//
	// Forced to 10000000 if SDX module is disabled.

	if (mbSDXModuleEnabled) {
		SetSDXBank(value & 63);
		SetSDXEnabled(!(value & 0x80));

		mbExternalCartEnabled = ((value & 0xc0) == 0x80);
		UpdateExternalCart();
	}

	// Pre-control lock, the SDX bank is also used for the BASIC and GAME
	// banks (!).
	if (!mbControlLocked)
		UpdateKernelBank();
}

sint32 ATUltimate1MBEmulator::DebugReadByteFlash(void *thisptr0, uint32 addr) {
	ATUltimate1MBEmulator *const thisptr = (ATUltimate1MBEmulator *)thisptr0;
	uint8 value;

	if (thisptr->mFlashEmu.DebugReadByte(thisptr->mCartBankOffset + (addr - 0xA000), value)) {
		thisptr->UpdateCartLayers();
		thisptr->UpdateFlashShadows();
	}

	return value;
}

sint32 ATUltimate1MBEmulator::ReadByteFlash(void *thisptr0, uint32 addr) {
	ATUltimate1MBEmulator *const thisptr = (ATUltimate1MBEmulator *)thisptr0;
	uint8 value;

	if (thisptr->mFlashEmu.ReadByte(thisptr->mCartBankOffset + (addr - 0xA000), value)) {
		thisptr->UpdateCartLayers();
		thisptr->UpdateFlashShadows();
	}

	return value;
}

bool ATUltimate1MBEmulator::WriteByteFlash(void *thisptr0, uint32 addr, uint8 value) {
	ATUltimate1MBEmulator *const thisptr = (ATUltimate1MBEmulator *)thisptr0;

	if (thisptr->mFlashEmu.WriteByte(thisptr->mCartBankOffset + (addr - 0xA000), value)) {
		// check for write activity
		if (thisptr->mFlashEmu.CheckForWriteActivity())
			thisptr->mpUIRenderer->SetFlashWriteActivity();

		// reconfigure banking
		thisptr->UpdateCartLayers();
		thisptr->UpdateFlashShadows();
	}

	return true;
}

uint32 ATUltimate1MBEmulator::GetOSFlashOffset(uint32 addr) {
	if (addr < 0x5800) {
		// self-test kernel ROM
		return 0x70000 + ((uint32)mKernelBank << 14) + (addr - 0x4000);
	} else if (addr < 0xC000) {
		// BASIC or Game ROM (BASIC has priority)
		if (mpMMU->IsBASICROMEnabled())
			return 0x60000 + ((uint32)mBasicBank << 13) + (addr - 0xA000);
		else
			return 0x68000 + ((uint32)mGameBank << 13) + (addr - 0xA000);
	} else {
		// lower/upper kernel ROM
		return 0x70000 + ((uint32)mKernelBank << 14) + (addr - 0xC000);
	}
}

sint32 ATUltimate1MBEmulator::ReadByteOSFlash(void *thisptr0, uint32 addr) {
	ATUltimate1MBEmulator *const thisptr = (ATUltimate1MBEmulator *)thisptr0;
	const uint32 flashOffset = thisptr->GetOSFlashOffset(addr);
	uint8 value;

	if (thisptr->mFlashEmu.ReadByte(flashOffset, value)) {
		thisptr->UpdateCartLayers();
		thisptr->UpdateFlashShadows();
	}

	return value;
}

bool ATUltimate1MBEmulator::WriteByteOSFlash(void *thisptr0, uint32 addr, uint8 value) {
	ATUltimate1MBEmulator *const thisptr = (ATUltimate1MBEmulator *)thisptr0;
	const uint32 flashOffset = thisptr->GetOSFlashOffset(addr);

	if (thisptr->mFlashEmu.WriteByte(flashOffset, value)) {
		// check for write activity
		if (thisptr->mFlashEmu.CheckForWriteActivity())
			thisptr->mpUIRenderer->SetFlashWriteActivity();

		// reconfigure banking
		thisptr->UpdateCartLayers();
		thisptr->UpdateFlashShadows();
	}

	return true;
}

sint32 ATUltimate1MBEmulator::ReadByteD1xx(void *thisptr0, uint32 addr) {
	ATUltimate1MBEmulator *const thisptr = (ATUltimate1MBEmulator *)thisptr0;

	if ((thisptr->mbPBISelected || !thisptr->mbControlLocked) && addr <= 0xD1BF)
		return thisptr->mpMemory[addr];

	return -1;
}

bool ATUltimate1MBEmulator::WriteByteD1xx(void *thisptr0, uint32 addr, uint8 value) {
	ATUltimate1MBEmulator *const thisptr = (ATUltimate1MBEmulator *)thisptr0;

	if (addr <= 0xD1BF) {
		if (addr == 0xD1BF)
			thisptr->SetPBIBank(value & 3);

		if (thisptr->mbPBISelected || !thisptr->mbControlLocked)
			thisptr->mpMemory[addr] = value;
	}

	return false;
}

sint32 ATUltimate1MBEmulator::ReadByteD3xx(void *thisptr0, uint32 addr) {
	ATUltimate1MBEmulator *const thisptr = (ATUltimate1MBEmulator *)thisptr0;

	if (addr == 0xD3E2) {
		// RTCIN
		return thisptr->mClockEmu.ReadState() ? 0x08 : 0x00;
	} else if (addr == 0xD383) {
		// COLDF
		return thisptr->mColdFlag;
	} else if (addr == 0xD384) {
		uint8 v = 0;

		if (thisptr->mbPBIButton)
			v += 0x80;

		if (thisptr->mbExternalCartActive)
			v += 0x40;

		return v;
	} else if (addr >= 0xD380)
		return 0xFF;
	
	return -1;
}

bool ATUltimate1MBEmulator::WriteByteD3xx(void *thisptr0, uint32 addr, uint8 value) {
	ATUltimate1MBEmulator *const thisptr = (ATUltimate1MBEmulator *)thisptr0;

	if (addr >= 0xD380) {
		// The U1MB only decodes $D300-D37F for the PIA.
		if (!thisptr->mbControlLocked) {
			if (addr == 0xD380) {
				// UCTL (write only)

				thisptr->mMemoryMode = value & 3;
				thisptr->UpdateMemoryMode();

				thisptr->SetKernelBank((value >> 2) & 3);
				thisptr->SetSDXModuleEnabled(!(value & 0x10));
				thisptr->SetIORAMEnabled((value & 0x40) != 0);

				if ((value & 0x80) && !thisptr->mbControlLocked) {
					thisptr->mbControlLocked = true;

					thisptr->mpMemMan->EnableLayer(thisptr->mpLayerPBIControl, thisptr->mbPBISelected);

					thisptr->UpdateKernelBank();
					thisptr->UpdatePBIDevice();

					thisptr->UpdateHooks();

					thisptr->mpSystemController->OnU1MBConfigPreLocked(false);
				}
			} else if (addr == 0xD381) {
				// UAUX (write only)
				thisptr->SetUAUX(value);
			} else if (addr == 0xD382) {
				// UPBI/UCAR (write only)
				thisptr->SetUPBI(value);
			} else if (addr == 0xD383) {
				// COLDF
				thisptr->mColdFlag = (value & 0x80);
			}
		}

		if (addr == 0xD3E2) {
			// RTCOUT (write)
			thisptr->mClockEmu.WriteState((value & 1) != 0, !(value & 2), (value & 4) != 0);
		}
		return true;
	}

	return false;
}

sint32 ATUltimate1MBEmulator::ReadByteD5xx(void *thisptr0, uint32 addr) {
	ATUltimate1MBEmulator *const thisptr = (ATUltimate1MBEmulator *)thisptr0;

	if (addr <= 0xD5BF) {
		if (thisptr->mbPBISelected || !thisptr->mbControlLocked)
			return thisptr->mpMemory[addr];
	}

	return -1;
}

bool ATUltimate1MBEmulator::WriteByteD5xx(void *thisptr0, uint32 addr, uint8 value) {
	ATUltimate1MBEmulator *const thisptr = (ATUltimate1MBEmulator *)thisptr0;

	if (addr < 0xD5BF) {
		if (thisptr->mbPBISelected || !thisptr->mbControlLocked) {
			thisptr->mpMemory[addr] = value;
			return true;
		}
	} else if (thisptr->mbSDXModuleEnabled) {
		if (addr == 0xD5E0) {
			thisptr->SetSDXCTL(value);

			// We have to let this through so that SIDE also sees writes here.
			// Otherwise, the internal BIOS cannot disable the SIDE cartridge.
			//return true;
		}
	}

	return false;
}

void ATUltimate1MBEmulator::UpdateFlashShadows() {
	ATMemoryAccessMode modes = mFlashEmu.IsControlReadEnabled() ? kATMemoryAccessMode_ARW : kATMemoryAccessMode_W;

	if (!mbFlashWriteEnabled)
		modes = (ATMemoryAccessMode)(modes & ~kATMemoryAccessMode_W);

	bool kernel = mpMMU->IsKernelROMEnabled();
	mpMemMan->SetLayerModes(mpMemLayerLowerKernelFlash, kernel ? modes : kATMemoryAccessMode_0);
	mpMemMan->SetLayerModes(mpMemLayerUpperKernelFlash, kernel ? modes : kATMemoryAccessMode_0);
	mpMemMan->SetLayerModes(mpMemLayerSelfTestFlash, mpMMU->IsSelfTestROMEnabled() ? modes : kATMemoryAccessMode_0);
	mpMemMan->SetLayerModes(mpMemLayerBASICGameFlash, mpMMU->IsBASICOrGameROMEnabled() ? modes : kATMemoryAccessMode_0);
}
