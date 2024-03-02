//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2020 Avery Lee
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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#include <stdafx.h>
#include <vd2/system/binary.h>
#include <vd2/system/color.h>
#include <vd2/system/file.h>
#include <vd2/system/hash.h>
#include <vd2/system/int128.h>
#include <vd2/system/registry.h>
#include <at/atcore/consoleoutput.h>
#include <at/atcore/crc.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/wraptime.h>
#include "debuggerlog.h"
#include "side3.h"
#include "memorymanager.h"
#include "ide.h"
#include "uirender.h"
#include "simulator.h"
#include "firmwaremanager.h"

ATDebuggerLogChannel g_ATLCSD(false, false, "SD", "SD card activity");
ATDebuggerLogChannel g_ATLCSIDE3DMA(false, false, "SIDE3DMA", "SIDE3 DMA requests");

void ATCreateDeviceSIDE3(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATSIDE3Emulator> p(new ATSIDE3Emulator);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefSIDE3 = { "side3", "side3", L"SIDE 3", ATCreateDeviceSIDE3, kATDeviceDefFlag_RebootOnPlug };

////////////////////////////////////////////////////////////////////////////////

ATSIDE3Emulator::ATSIDE3Emulator() {
	memset(mFlash, 0xFF, sizeof mFlash);

	mRTC.Init();

	LoadNVRAM();
}

ATSIDE3Emulator::~ATSIDE3Emulator() {
	SaveNVRAM();
}

void *ATSIDE3Emulator::AsInterface(uint32 id) {
	switch(id) {
		case IATDeviceScheduling::kTypeID:	return static_cast<IATDeviceScheduling *>(this);
		case IATDeviceMemMap::kTypeID:		return static_cast<IATDeviceMemMap *>(this);
		case IATDeviceCartridge::kTypeID:	return static_cast<IATDeviceCartridge *>(this);
		case IATDeviceIndicators::kTypeID:	return static_cast<IATDeviceIndicators *>(this);
		case IATDeviceFirmware::kTypeID:	return static_cast<IATDeviceFirmware *>(this);
		case IATDeviceParent::kTypeID:		return static_cast<IATDeviceParent *>(this);
		case IATDeviceDiagnostics::kTypeID:	return static_cast<IATDeviceDiagnostics *>(this);
		case IATDeviceButtons::kTypeID:		return static_cast<IATDeviceButtons *>(this);
		default:
			return nullptr;
	}
}

void ATSIDE3Emulator::GetSettings(ATPropertySet& settings) {
	settings.SetBool("led_enable", mbActivityIndicatorEnable);
	settings.SetBool("recovery", mbRecoveryMode);
}

bool ATSIDE3Emulator::SetSettings(const ATPropertySet& settings) {
	bool indicatorEnable = settings.GetBool("led_enable", true);
	if (mbActivityIndicatorEnable != indicatorEnable) {
		mbActivityIndicatorEnable = indicatorEnable;

		if (!indicatorEnable && mpUIRenderer)
			mpUIRenderer->SetCartridgeActivity(-1, -1);
	}

	mbRecoveryMode = settings.GetBool("recovery", false);

	return true;
}

void ATSIDE3Emulator::Init() {
	ReloadFirmware();

	mFlashCtrl.Init(mFlash, kATFlashType_MX29LV640DT, mpScheduler);

	ATMemoryHandlerTable handlerTable = {};

	handlerTable.mpThis = this;
	handlerTable.mbPassAnticReads = true;
	handlerTable.mbPassReads = true;
	handlerTable.mbPassWrites = true;
	handlerTable.mpDebugReadHandler = [](void *thisptr, uint32 addr) -> sint32 { return ((ATSIDE3Emulator *)thisptr)->OnReadByte<true>(addr); };
	handlerTable.mpReadHandler = [](void *thisptr, uint32 addr) -> sint32 { return ((ATSIDE3Emulator *)thisptr)->OnReadByte<false>(addr); };
	handlerTable.mpWriteHandler = [](void *thisptr, uint32 addr, uint8 value) -> bool { return ((ATSIDE3Emulator *)thisptr)->OnWriteByte(addr, value); };
	mpMemLayerCCTL = mpMemMan->CreateLayer(kATMemoryPri_CartridgeOverlay, handlerTable, 0xD5, 0x01);
	mpMemMan->SetLayerName(mpMemLayerCCTL, "SIDE 3 registers");

	UpdateControlLayer();

	mpMemLayerWindowA = mpMemMan->CreateLayer(kATMemoryPri_CartridgeOverlay, mFlash, 0x80, 0x20, true);
	mpMemMan->SetLayerName(mpMemLayerWindowA, "SIDE 3 right cartridge window");

	mpMemLayerWindowA2 = mpMemMan->CreateLayer(kATMemoryPri_CartridgeOverlay+1, mFlash, 0x90, 0x10, true);
	mpMemMan->SetLayerName(mpMemLayerWindowA2, "SIDE 3 right cartridge window (high half)");

	mpMemLayerWindowB = mpMemMan->CreateLayer(kATMemoryPri_CartridgeOverlay, mFlash, 0xA0, 0x20, true);
	mpMemMan->SetLayerName(mpMemLayerWindowB, "SIDE 3 left cartridge window");

	handlerTable.mbPassReads = false;
	handlerTable.mbPassWrites = false;
	handlerTable.mbPassAnticReads = false;

	handlerTable.mpDebugReadHandler = OnFlashDebugReadA;
	handlerTable.mpReadHandler = OnFlashReadA;
	handlerTable.mpWriteHandler = OnFlashWriteA;

	mpMemLayerFlashControlA = mpMemMan->CreateLayer(kATMemoryPri_CartridgeOverlay+1, handlerTable, 0x80, 0x20);
	mpMemMan->SetLayerName(mpMemLayerFlashControlA, "SIDE 3 flash control (right cart window)");

	handlerTable.mpDebugReadHandler = OnFlashDebugReadB;
	handlerTable.mpReadHandler = OnFlashReadB;
	handlerTable.mpWriteHandler = OnFlashWriteB;

	mpMemLayerFlashControlB = mpMemMan->CreateLayer(kATMemoryPri_CartridgeOverlay+1, handlerTable, 0xA0, 0x20);
	mpMemMan->SetLayerName(mpMemLayerFlashControlB, "SIDE 3 flash control (left cart window)");

	handlerTable.mpDebugReadHandler = [](void *thisptr, uint32 addr) -> sint32 { return ((ATSIDE3Emulator *)thisptr)->OnSpecialReadByteA1<true>(addr); };
	handlerTable.mpReadHandler = [](void *thisptr, uint32 addr) -> sint32 { return ((ATSIDE3Emulator *)thisptr)->OnSpecialReadByteA1<false>(addr); };
	handlerTable.mpWriteHandler = [](void *thisptr, uint32 addr, uint8 value) -> bool { return ((ATSIDE3Emulator *)thisptr)->OnSpecialWriteByteA1(addr, value); };

	mpMemLayerSpecialBank1 = mpMemMan->CreateLayer(kATMemoryPri_CartridgeOverlay+2, handlerTable, 0x8F, 0x01);
	mpMemMan->SetLayerName(mpMemLayerSpecialBank1, "SIDE 3 flash control (right cart special banking)");

	handlerTable.mpDebugReadHandler = [](void *thisptr, uint32 addr) -> sint32 { return ((ATSIDE3Emulator *)thisptr)->OnSpecialReadByteA2<true>(addr); };
	handlerTable.mpReadHandler = [](void *thisptr, uint32 addr) -> sint32 { return ((ATSIDE3Emulator *)thisptr)->OnSpecialReadByteA2<false>(addr); };
	handlerTable.mpWriteHandler = [](void *thisptr, uint32 addr, uint8 value) -> bool { return ((ATSIDE3Emulator *)thisptr)->OnSpecialWriteByteA2(addr, value); };

	mpMemLayerSpecialBank2 = mpMemMan->CreateLayer(kATMemoryPri_CartridgeOverlay+2, handlerTable, 0x9F, 0x01);
	mpMemMan->SetLayerName(mpMemLayerSpecialBank2, "SIDE 3 flash control (left cart special banking)");
}

void ATSIDE3Emulator::Shutdown() {
	mFlashCtrl.Shutdown();

	if (mpCartridgePort) {
		mpCartridgePort->RemoveCartridge(mCartId, this);
		mpCartridgePort = nullptr;
	}

	if (mpMemLayerFlashControlB) {
		mpMemMan->DeleteLayer(mpMemLayerFlashControlB);
		mpMemLayerFlashControlB = NULL;
	}

	if (mpMemLayerFlashControlA) {
		mpMemMan->DeleteLayer(mpMemLayerFlashControlA);
		mpMemLayerFlashControlA = NULL;
	}

	if (mpMemLayerSpecialBank2) {
		mpMemMan->DeleteLayer(mpMemLayerSpecialBank2);
		mpMemLayerSpecialBank2 = NULL;
	}

	if (mpMemLayerSpecialBank1) {
		mpMemMan->DeleteLayer(mpMemLayerSpecialBank1);
		mpMemLayerSpecialBank1 = NULL;
	}

	if (mpMemLayerWindowB) {
		mpMemMan->DeleteLayer(mpMemLayerWindowB);
		mpMemLayerWindowB = NULL;
	}

	if (mpMemLayerWindowA2) {
		mpMemMan->DeleteLayer(mpMemLayerWindowA2);
		mpMemLayerWindowA2 = NULL;
	}

	if (mpMemLayerWindowA) {
		mpMemMan->DeleteLayer(mpMemLayerWindowA);
		mpMemLayerWindowA = NULL;
	}

	if (mpMemLayerCCTL) {
		mpMemMan->DeleteLayer(mpMemLayerCCTL);
		mpMemLayerCCTL = NULL;
	}

	if (mpBlockDevice) {
		vdpoly_cast<IATDevice *>(mpBlockDevice)->SetParent(nullptr, 0);
		mpBlockDevice = nullptr;
	}

	if (mpScheduler) {
		mpScheduler->UnsetEvent(mpDMAEvent);
		mpScheduler->UnsetEvent(mpLEDXferEvent);
		mpScheduler = nullptr;
	}

	if (mpUIRenderer) {
		if (mbActivityIndicatorEnable)
			mpUIRenderer->SetCartridgeActivity(-1, -1);

		mpUIRenderer = nullptr;
	}

	mpMemMan = NULL;
}

void ATSIDE3Emulator::SetSDXEnabled(bool enable) {
	if (mbSDXEnable == enable)
		return;

	mbSDXEnable = enable;
	(void)UpdateEmuCartEnabled();
	UpdateWindows();
}

void ATSIDE3Emulator::ResetCartBank() {
	// A button push resets both the RAM banks and ROM bank B,
	// but not ROM bank A.

	mBankRAMA = 0;
	mBankRAMB = 0;
	mBankFlashB = 0;
	mBankControl = kBC_EnableFlashB | 0x20;
	mFlashOffsetA = 0;
	mFlashOffsetB = 0;

	if (mbSDXEnable) {
		mBankFlashB = 0xC0;
		mBankControl |= kBC_HighBankB;
		mFlashOffsetB = 0x3C0 << 13;
	}

	if (mbRecoveryMode)
		mBankControl &= 0xF0;

	mBankRAMWriteProtect = 0x07;
	UpdateWindows();
}

void ATSIDE3Emulator::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefSIDE3;
}

void ATSIDE3Emulator::ColdReset() {
	mFlashCtrl.ColdReset();
	mRTC.ColdReset();

	memset(mRAM, 0xFF, sizeof mRAM);

	mpScheduler->UnsetEvent(mpDMAEvent);
	mpScheduler->UnsetEvent(mpLEDXferEvent);

	mbDMAFromSD = false;
	mbDMAActive = false;
	mDMASrcAddress = 0;
	mDMADstAddress = 0;
	mDMACounter = 0;
	mDMASrcStep = 1;
	mDMADstStep = 1;
	mDMAAndMask = 0xFF;
	mDMAXorMask = 0;

	mEmuCCTLBase = 0;
	mEmuCCTLMask = 0;
	mEmuAddressMask = 0;
	mEmuDataMask = 0;
	mEmuDisableMaskA = 0;
	mEmuDisableMaskB = 0;
	mEmuFeature = 0;
	mEmuControl = 0;
	mEmuControl2 = 0;
	mEmuBankA = 0;
	mEmuBankB = 0;
	mEmuBank = 0;
	mEmuData = 0;
	mbEmuDisabledA = false;
	mbEmuDisabledB = false;
	mbEmuLocked = false;

	mLEDBrightness = 80;

	mbEmuCartEnableRequested = false;
	(void)UpdateEmuCartEnabled();

	// The ROM/flash bank A register is not reset by the push button, so we
	// must do it explicitly for cold reset.
	mBankFlashA = 0;
	mFlashOffsetA = 0;

	ResetCartBank();

	mRegisterMode = RegisterMode::Primary;
	mbApertureEnable = false;
	mbLEDGreenManualEnabled = false;
	mbColdStartFlag = true;
	mbButtonPressed = false;
	mSDStatus &= 0x40;
	mSDStatus |= 0x05;

	// The hardware resets bit 7 = 1 and doesn't clear it if an SD card is inserted.
	// SIDE3.SYS 4.03 relies on this or it reports No Media on boot.
	if (mpBlockDevice)
		mSDStatus |= 0x80;

	ResetSD();

	mSDNextTransferTime = 0;
	UpdateLEDIntensity();
	UpdateLEDGreen();
	UpdateLED();
}

void ATSIDE3Emulator::LoadNVRAM() {
	VDRegistryAppKey key("Nonvolatile RAM");

	ATRTCMCP7951XEmulator::NVState state {};

	if (key.getBinary("SIDE 3 clock", (char *)&state, sizeof state))
		mRTC.Load(state);
}

void ATSIDE3Emulator::SaveNVRAM() {
	VDRegistryAppKey key("Nonvolatile RAM");

	ATRTCMCP7951XEmulator::NVState state;
	mRTC.Save(state);

	key.setBinary("SIDE 3 clock", (const char *)&state, sizeof state);
}

void ATSIDE3Emulator::InitScheduling(ATScheduler *sch, ATScheduler *slowsch) {
	mpScheduler = sch;
}

void ATSIDE3Emulator::InitMemMap(ATMemoryManager *memmap) {
	mpMemMan = memmap;
}

bool ATSIDE3Emulator::GetMappedRange(uint32 index, uint32& lo, uint32& hi) const {
	if (index == 0) {
		lo = 0xD5F0;
		hi = 0xD5FF;
		return true;
	}

	return false;
}

void ATSIDE3Emulator::InitCartridge(IATDeviceCartridgePort *cartPort) {
	mpCartridgePort = cartPort;
	mpCartridgePort->AddCartridge(this, kATCartridgePriority_Internal, mCartId);
}

bool ATSIDE3Emulator::IsLeftCartActive() const {
	return mbLeftWindowActive;
}

void ATSIDE3Emulator::SetCartEnables(bool leftEnable, bool rightEnable, bool cctlEnable) {
	bool changed = false;

	if (mbLeftWindowEnabled != leftEnable) {
		mbLeftWindowEnabled = leftEnable;
		changed = true;
	}

	if (mbRightWindowEnabled != rightEnable) {
		mbRightWindowEnabled = rightEnable;
		changed = true;
	}
	
	if (mbCCTLEnabled != cctlEnable) {
		mbCCTLEnabled = cctlEnable;
		UpdateControlLayer();
	}

	if (changed && mpMemMan && mpMemLayerWindowB)
		UpdateWindows();
}

void ATSIDE3Emulator::UpdateCartSense(bool leftActive) {
}

void ATSIDE3Emulator::InitIndicators(IATDeviceIndicatorManager *r) {
	mpUIRenderer = r;
}

void ATSIDE3Emulator::InitFirmware(ATFirmwareManager *fwman) {
	mpFirmwareManager = fwman;
}

bool ATSIDE3Emulator::ReloadFirmware() {
	void *flash = mFlash;
	uint32 flashSize = sizeof mFlash;

	vduint128 oldHash = VDHash128(flash, flashSize);

	mFlashCtrl.SetDirty(false);

	memset(flash, 0xFF, flashSize);

	const uint64 id = mpFirmwareManager->GetCompatibleFirmware(kATFirmwareType_SIDE3);
	mpFirmwareManager->LoadFirmware(id, flash, 0, flashSize, nullptr, nullptr, nullptr, nullptr, &mbFirmwareUsable);

	return oldHash != VDHash128(flash, flashSize);
}

const wchar_t *ATSIDE3Emulator::GetWritableFirmwareDesc(uint32 idx) const {
	if (idx == 0)
		return L"Cartridge ROM";
	else
		return nullptr;
}

bool ATSIDE3Emulator::IsWritableFirmwareDirty(uint32 idx) const {
	return idx == 0 && mFlashCtrl.IsDirty();
}

void ATSIDE3Emulator::SaveWritableFirmware(uint32 idx, IVDStream& stream) {
	stream.Write(mFlash, sizeof mFlash);

	mFlashCtrl.SetDirty(false);
}

ATDeviceFirmwareStatus ATSIDE3Emulator::GetFirmwareStatus() const {
	return mbFirmwareUsable ? ATDeviceFirmwareStatus::OK : ATDeviceFirmwareStatus::Missing;
}

IATDeviceBus *ATSIDE3Emulator::GetDeviceBus(uint32 index) {
	return index ? 0 : this;
}

const wchar_t *ATSIDE3Emulator::GetBusName() const {
	return L"SD Card Bus";
}

const char *ATSIDE3Emulator::GetBusTag() const {
	return "sdbus";
}

const char *ATSIDE3Emulator::GetSupportedType(uint32 index) {
	if (index == 0)
		return "harddisk";

	return nullptr;
}

void ATSIDE3Emulator::GetChildDevices(vdfastvector<IATDevice *>& devs) {
	auto *cdev = vdpoly_cast<IATDevice *>(&*mpBlockDevice);

	if (cdev)
		devs.push_back(cdev);
}

void ATSIDE3Emulator::AddChildDevice(IATDevice *dev) {
	if (mpBlockDevice)
		return;

	IATBlockDevice *blockDevice = vdpoly_cast<IATBlockDevice *>(dev);

	if (blockDevice) {
		mpBlockDevice = blockDevice;
		dev->SetParent(this, 0);

		// update WP bit but do not set SD present, that has to be done by code
		mSDStatus &= 0xBF;

		if (blockDevice->IsReadOnly())
			mSDStatus |= 0x40;
	}
}

void ATSIDE3Emulator::RemoveChildDevice(IATDevice *dev) {
	IATBlockDevice *blockDevice = vdpoly_cast<IATBlockDevice *>(dev);

	if (mpBlockDevice == blockDevice) {
		dev->SetParent(nullptr, 0);
		mpBlockDevice = nullptr;

		// clear SD present and set WP bit
		mSDStatus &= 0x7F;
		mSDStatus |= 0x40;
	}
}

void ATSIDE3Emulator::DumpStatus(ATConsoleOutput& output) {
	output <<= "SIDE3 status:";

	static constexpr const char *kRegisterBankNames[] {
		"Primary",
		"DMA",
		"Cartridge Emulation",
		"Reserved"
	};

	output("  Active register set: %s", kRegisterBankNames[(uint8)mRegisterMode & 3]);
	output("  $D500-D57F RAM:      %s", mbApertureEnable ? "enabled" : "off");
	output("  Green LED:           %s", mbLEDGreenManualEnabled ? "enabled (unless SD power on)" : "disabled (unless SD power on)");
	output("  Emu cart enable:     %s", mbEmuCartEnableRequested ? mbEmuCartEnableEffective ? "enabled" : "enabled (no effect due to SDX being disabled)" : "disabled");
	output <<= "";

	output <<= "Primary registers";
	output("  SD Control/Status    $%02X", mSDStatus);
	output("  SD CRC7              $%02X", mSDCRC7);

	const uint32 flashBankA = mBankFlashA + ((mBankControl & kBC_HighBankA) << 4);
	const uint32 flashBankB = mBankFlashB + ((mBankControl & kBC_HighBankB) << 2);
	output("  Win A ($8000-9FFF)   Flash A bank: $%03X ($%06X) - %s", flashBankA, flashBankA << 13, (mBankControl & kBC_EnableFlashA) ? "enabled" : "off");
	output("                       RAM A bank:   $%02X  ($%06X) - %s"
		, mBankRAMA
		, (uint32)mBankRAMA << 13
		, (mBankControl & kBC_EnableMemA) ? (mBankControl & kBC_EnableFlashA) ? "hidden by flash" : (mBankRAMWriteProtect & 1) ? "enabled read-only" : "enabled read/write" : "off");
	output("  Win B ($A000-BFFF)   Flash B bank: $%03X ($%06X) - %s", flashBankB, flashBankB << 13, (mBankControl & kBC_EnableFlashB) ? "enabled" : "off");
	output("                       RAM B bank:   $%02X  ($%06X) - %s"
		, mBankRAMB
		, (uint32)mBankRAMB << 13
		, (mBankControl & kBC_EnableMemB) ? (mBankControl & kBC_EnableFlashB) ? "hidden by flash" : (mBankRAMWriteProtect & 2) ? "enabled read-only" : "enabled read/write" : "off");
	output("  Window control       $%02X", mBankControl);

	output("  RAM write protect    $%02X", mBankRAMWriteProtect);
	output("  Green LED brightness $%02X", mLEDBrightness);

	output <<= "";
	output <<= "DMA registers";
	if (mbDMAFromSD) {
		output("  DMA: $%06X [%c$%02X] <- SD Len=$%05X And=$%02X Xor=$%02X (%s)"
			, mDMADstAddress
			, (sint32)mDMADstStep < 0 ? '-' : '+'
			, abs((sint32)mDMADstStep)
			, (uint32)mDMACounter + 1
			, mDMAAndMask
			, mDMAXorMask
			, mbDMAActive ? "running" : "idle"
		);
	} else {
		output("  DMA: $%06X [%c$%02X] <- $%06X [%c$%02X] Len=$%05X And=$%02X Xor=$%02X"
			, mDMADstAddress
			, (sint32)mDMADstStep < 0 ? '-' : '+'
			, abs((sint32)mDMADstStep)
			, mDMASrcAddress
			, (sint32)mDMASrcStep < 0 ? '-' : '+'
			, abs((sint32)mDMASrcStep)
			, (uint32)mDMACounter + 1
			, mDMAAndMask
			, mDMAXorMask
			, mbDMAActive ? "running" : "idle"
		);
	}

	output <<= "";
	output <<= "Cartridge emulation registers";
	output("  Cartridge emulation: %s", mEmuControl & kEC_Enable ? "enabled" : "off");
	output("  SIDE 3 registers:    %s", mbEmuLocked ? "disabled" : "enabled");
	output("  CCTL mask:           $%02X/$%02X (%s)", mEmuCCTLBase, mEmuCCTLMask, mEmuFeature & kEF_EnableCCTLMask ? "enabled" : "off");
	output("  Address mask:        $%02X (%s)", mEmuAddressMask, mEmuFeature & kEF_UseAddress ? "enabled" : "off");
	output("  Data mask:           $%02X (%s)", mEmuDataMask, mEmuFeature & kEF_UseAddress ? "enabled" : "off");
	output("  Win A disable state: %s", mbEmuDisabledA ? "disabled" : "not disabled");
	output("  Win B disable state: %s", mbEmuDisabledB ? "disabled" : "not disabled");
	output("  Win A disable mask:  $%02X (%s)", mEmuDisableMaskA, mEmuFeature & kEF_EnableDisA ? "enabled" : "off");
	output("  Win B disable mask:  $%02X (%s)", mEmuDisableMaskB, mEmuFeature & kEF_EnableDisB ? "enabled" : "off");
	output("  Feature register:    $%02X (%s, %s, %s, %s, %s, %s)"
		, mEmuFeature
		, mEmuFeature & kEF_UseAddress ? "CCTL address" : "CCTL data"
		, mEmuFeature & kEF_EnableDisA ? "+disA" : "-disA"
		, mEmuFeature & kEF_EnableDisB ? "+disB" : "-disB"
		, mEmuFeature & kEF_DisableByData ? "+disableByData" : "-disableByData"
		, mEmuFeature & kEF_EnableCCTLMask ? "+CCTLmask" : "-CCTLmask"
		, mEmuFeature & kEF_BountyBob ? "+BBSB" : "-BBSB"
	);

	static constexpr const char *kEmuModes[] {
		"standard",
		"Williams",
		"XEGS/BBSB",
		"?mode3",
		"MegaCart",
		"?mode5",
		"?mode6",
		"?mode7",
	};

	output("  Control register:    $%02X (%s, %s, %s, %s)"
		, mEmuControl
		, mEmuControl & kEC_Enable ? "enabled" : "disabled"
		, mEmuControl & kEC_InvertDisableA ? "+!disA" : "-!disA"
		, mEmuControl & kEC_InvertDisableB ? "+!disB" : "-!disB"
		, kEmuModes[mEmuControl & 7]
	);

	output("  Initial win A bank:  $%02X (initially %s)", mEmuBankA, mEmuControl2 & kEC2_DisableWinA ? "disabled" : "enabled");
	output("  Initial win B bank:  $%02X (initially %s)", mEmuBankB, mEmuControl2 & kEC2_DisableWinB ? "disabled" : "enabled");
	output("  Variable bank:       $%02X", mEmuBank);
	output("  Control 2 register:  $%02X", mEmuControl2);
	output("  Loader state:        $%02X", mEmuData);

	output <<= "";

	mRTC.DumpStatus(output);
}

uint32 ATSIDE3Emulator::GetSupportedButtons() const {
	return (1 << kATDeviceButton_CartridgeResetBank) | (1 << kATDeviceButton_CartridgeSDXEnable);
}

bool ATSIDE3Emulator::IsButtonDepressed(ATDeviceButton idx) const {
	return idx == kATDeviceButton_CartridgeSDXEnable && mbSDXEnable;
}

void ATSIDE3Emulator::ActivateButton(ATDeviceButton idx, bool state) {
	if (idx == kATDeviceButton_CartridgeResetBank) {
		// set button status, even if PBI mode is not enabled
		mbButtonPressed = true;

		if (!mbButtonPBIMode) {
			mbEmuLocked = false;
			mEmuControl &= 0x6F;
			mRegisterMode = RegisterMode::Primary;
			ResetCartBank();
		}
	} else if (idx == kATDeviceButton_CartridgeSDXEnable) {
		SetSDXEnabled(state);
	}
}

void ATSIDE3Emulator::OnScheduledEvent(uint32 id) {
	if (id == kEventID_DMA) {
		mpDMAEvent = nullptr;

		AdvanceDMA();
		RescheduleDMA();
	} else if (id == kEventID_LED) {
		mpLEDXferEvent = nullptr;

		const uint32 t = mpScheduler->GetTick();

		if (ATWrapTime{t} < mLEDXferOffTime)
			mpLEDXferEvent = mpScheduler->AddEvent(mLEDXferOffTime - t, this, kEventID_LED);
		else
			UpdateLED();
	}
}

template<bool T_DebugRead>
sint32 ATSIDE3Emulator::OnReadByte(uint32 addr) const {
	const uint8 addr8 = (uint8)addr;
	sint32 rval = -1;

	// If the CCTL RAM aperture and cartridge emulation are both active, they
	// can both respond to the read, by returning data from RAM but also switching
	// banks.
	if (addr8 < 0x80 && mbApertureEnable)
		rval = mRAM[0x1FF500 + addr8];

	// Locked register state only affects writes; it does not block reads
	// from the SIDE3 register set.
	if ((mEmuControl & 0x80) && (addr8 < 0xF0 || mbEmuLocked)) {
		if constexpr (!T_DebugRead)
			const_cast<ATSIDE3Emulator *>(this)->OnEmuBankAccess(addr, -1);
	}

	if (addr8 < 0xF0)
		return rval;

	// === $D5FC-D5FF: Common registers ===
	if (addr8 >= 0xFC) {
		switch(addr8 & 0x03) {
			// === $D5FC: Misc status/control ===
			case 0:
				return (uint8)mRegisterMode
					+ (mbEmuCartEnableRequested ? 0x04 : 0)
					+ (mbButtonPBIMode ? 0x08 : 0)
					+ (mbColdStartFlag ? 0x10 : 0)
					+ (mbLEDGreenManualEnabled ? 0x20 : 0)
					+ (mbSDXEnable ? 0x40 : 0)
					+ (mbButtonPressed ? 0x80 : 0)
					;

			// === $D5FD-D5FF: Signature ===
			case 1:	return mbApertureEnable ? 'R' : 'S';
			case 2:	return 'S';
			case 3:	return 'D';
		}
	}

	switch(mRegisterMode) {
		case RegisterMode::Primary:
			switch(addr8 & 0x0F) {
				case 0x0:	return mLEDBrightness;

				// $D5F1 primary set - open bus
				// $D5F2 primary set - open bus

				// === $D5F3-D5F5: SD/RTC card control ===
				case 0x3:
					return mSDStatus + (mpScheduler->GetTick64() >= mSDNextTransferTime ? 0x00 : 0x02);

				case 0x4:
					if (uint64 t = mpScheduler->GetTick64(); t >= mSDNextTransferTime) {
						uint8 v = mSDNextRead;

						if constexpr (!T_DebugRead) {
							if (mSDStatus & 0x08)
								const_cast<ATSIDE3Emulator *>(this)->AdvanceSPIRead();
						}

						return v;
					} else {
						// We are reading the shifter while it is operating, so compute the number of
						// bits to shift and blend the previous and next bytes. In fast mode, this is
						// only possible for an offset of 0, so we can just use the same calc as for
						// slow mode. The shift direction is left (MSB-first), but we are computing
						// the opposite shift.

						int bitsRemaining = ((uint32)(mSDNextTransferTime - t) + 3) >> 2;

						return (((uint32)mSDPrevRead << 8) + mSDNextRead) >> bitsRemaining;
					}
					break;

				case 0x5:	return mSDCRC7 | 1;

				// === $D5F6-D5FB: Banking registers ===
				case 0x6:	return mBankRAMA;
				case 0x7:	return mBankRAMB;
				case 0x8:	return mBankFlashA;
				case 0x9:	return mBankFlashB;
				case 0xA:	return mBankControl;
				case 0xB:	return mBankRAMWriteProtect;
			}
			break;

		case RegisterMode::DMA:
			switch(addr8 & 0x0F) {
				case 0x0: return ((mDMASrcAddress >> 16) & 0x1F) + (mbDMAFromSD ? 0x00 : 0x20) + (mbDMAActive ? 0x80 : 0x00);
				case 0x1: return (mDMASrcAddress >> 8) & 0xFF;
				case 0x2: return (mDMASrcAddress >> 0) & 0xFF;
				case 0x3: return (mDMADstAddress >> 16) & 0x1F;
				case 0x4: return (mDMADstAddress >> 8) & 0xFF;
				case 0x5: return (mDMADstAddress >> 0) & 0xFF;
				case 0x6: return (mDMACounter >> 8) & 0xFF;
				case 0x7: return (mDMACounter >> 0) & 0xFF;
				case 0x8: return mDMASrcStep & 0xFF;
				case 0x9: return mDMADstStep & 0xFF;
				case 0xA: return mDMAAndMask;
				case 0xB: return mDMAXorMask;
			}
			break;

		case RegisterMode::CartridgeEmu:
			switch(addr8 & 0xF) {
				case 0x0: return mEmuCCTLBase;
				case 0x1: return mEmuCCTLMask;
				case 0x2: return mEmuAddressMask;
				case 0x3: return mEmuDataMask;
				case 0x4: return mEmuDisableMaskA;
				case 0x5: return mEmuDisableMaskB;
				case 0x6: return mEmuFeature;
				case 0x7: return mEmuControl;
				case 0x8: return mEmuBankA;
				case 0x9: return mEmuBankB;
				case 0xA: return mEmuControl2;
				case 0xB: return mEmuData;

				default:
					break;
			}
			break;

		default:
			// all registers in the reserved set return $00, even on floating
			// bus systems
			return 0x00;
	}

	return -1;
}

bool ATSIDE3Emulator::OnWriteByte(uint32 addr, uint8 value) {
	const uint8 addr8 = (uint8)addr;

	// If both the CCTL RAM aperture and cartridge emulation are enabled, both
	// must be able to handle the access.
	if (addr8 < 0x80 && mbApertureEnable) {
		if (!(mBankRAMWriteProtect & 0x04))
			mRAM[0x1FF500 + addr8] = value;
	}

	// The register file and the cartridge emulator are mutally exclusive for
	// the $D5F0-D5FF range. If the lock is disabled, only the register file
	// will respond to the write, and if the lock is enabled, only the
	// cartridge emulator will.
	if ((mEmuControl & 0x80) && (addr8 < 0xF0 || mbEmuLocked)) {
		OnEmuBankAccess(addr, value);
		return true;
	}

	if (addr8 < 0xF0)
		return false;

	// === $D5FC-D5FF: Common registers ===
	if (addr8 >= 0xFC) {
		switch(addr8 & 3) {
			case 0:
				// D7: button status (1 = pressed; can only be cleared)
				// D6: SDX switch state (1 = down)
				// D5: Green LED manual control (1 = on if SD power off, 0 = on if SD power on)
				// D4: Cold start flag (1 on power-up, can only be changed to 0)
				// D3: PBI button mode enabled (1 = inhibit reset)
				// D2: Internal cartridge pass-through enable (1 = enabled; SDX enabled only)
				// D1-D0: register mode for $D1F0-D1FC

				mRegisterMode = (RegisterMode)(value & 3);

				mbButtonPBIMode = (value & 0x08) != 0;

				if (!(value & 0x80))
					mbButtonPressed = false;

				if (!(value & 0x10))
					mbColdStartFlag = false;

				if (bool led = (value & 0x20) != 0; mbLEDGreenManualEnabled != led) {
					mbLEDGreenManualEnabled = led;

					UpdateLEDGreen();
				}

				mbEmuCartEnableRequested = (value & 0x04) != 0;
				if (UpdateEmuCartEnabled())
					UpdateWindows();
				break;

			case 1:
				mbApertureEnable = (value & 0x40) != 0;
				break;

			default:
				break;
		}

		return true;
	}

	switch(mRegisterMode) {
		case RegisterMode::Primary:
			switch(addr8 & 0x0F) {

				case 0x0:
					if (mLEDBrightness != value) {
						mLEDBrightness = value;
						UpdateLEDIntensity();
					}
					break;

				// $D5F3: SD card control
				// Bits that can be changed:
				//	- D7=1 SD card present (can't be changed if no SD card)
				//	- D5=1 enable SD power
				//	- D4=1 use fast SD transfer clock
				//	- D3=1 SD SPI freerun
				//	- D2=1 RTC select
				//	- D0=1 SD select
				case 0x3:
					{
						// If there is no SD card, bit 7 can't be set. Doesn't matter if
						// SD is powered. Can't be reset by a write.
						if (!mpBlockDevice)
							value &= 0x7F;
						else
							value |= (mSDStatus & 0x80);

						const uint8 delta = mSDStatus ^ value;

						if (delta & 0x04)
							mRTC.Reselect();

						mSDStatus = (mSDStatus & 0x42) + (value & 0xBD);

						// The SD checksum register is cleared on any write to this
						// register, even if nothing has changed and SD power is off.
						mSDCRC7 = 0;

						if (delta & 0x20)
							UpdateLEDGreen();
					}
					break;

				case 0x4:
					// The shift register is shared for both input and output, with
					// the input byte being shifted in as the output byte is shifted out.
					// Where this makes a difference is in free-running mode, where the
					// output is ignored and forced to 1, but the CRC-7 logic does see
					// the now input bytes being shifted through.
					mSDNextRead = value;
					WriteSPI(value);
					break;

				// === $D5F6-D5FB: Banking registers ===
				case 0x6:
					if (mBankRAMA != value) {
						mBankRAMA = value;

						if ((mBankControl & kBC_EnableAnyA) == kBC_EnableMemA)
							UpdateWindowA();
					}
					break;

				case 0x7:
					if (mBankRAMB != value) {
						mBankRAMB = value;

						if ((mBankControl & kBC_EnableAnyB) == kBC_EnableMemB)
							UpdateWindowB();
					}
					break;

				case 0x8:
					mBankFlashA = value;

					if (const uint32 flashOffsetA = ((uint32)mBankFlashA << 13) + ((mBankControl & kBC_HighBankA) << 17);
						mFlashOffsetA != flashOffsetA)
					{
						mFlashOffsetA = flashOffsetA;

						if (mBankControl & kBC_EnableFlashA)
							UpdateWindowA();
					}
					break;

				case 0x9:
					mBankFlashB = value;

					if (const uint32 flashOffsetB = ((uint32)mBankFlashB << 13) + ((mBankControl & kBC_HighBankB) << 15);
						mFlashOffsetB != flashOffsetB)
					{
						mFlashOffsetB = flashOffsetB;

						if (mBankControl & kBC_EnableFlashB)
							UpdateWindowB();
					}
					break;

				case 0xA:
					if (mBankControl != value) {
						mBankControl = value;

						UpdateWindows();
					}
					break;

				case 0xB:
					value &= 0x07;

					if (const uint8 delta = mBankRAMWriteProtect ^ value) {
						mBankRAMWriteProtect = value;

						// only need window updates for A and B, as the CCTL window is part of the control
						// layer that is always enabled
						if (delta & 3) {
							if (mBankControl & (kBC_EnableMemA | kBC_EnableMemB))
								UpdateWindows();
						}
					}
					break;
			}
			break;

		case RegisterMode::DMA:
			AdvanceDMA();

			switch(addr8 & 0x0F) {
				case 0x0:
					mDMASrcAddress = ((uint32)(value & 0x1F) << 16) + (mDMASrcAddress & 0x00FFFF);
					mbDMAFromSD = !(value & 0x20);

					if (value & 0x80) {
						if (!mbDMAActive) {
							mbDMAActive = true;
							mDMALastCycle = mpScheduler->GetTick();
							mDMAState = 0;

							// These registers are double-buffered. The step and AND/XOR values are not.
							mDMAActiveSrcAddress = mDMASrcAddress;
							mDMAActiveDstAddress = mDMADstAddress;
							mDMAActiveBytesLeft = (uint32)mDMACounter + 1;

							if (g_ATLCSIDE3DMA.IsEnabled()) {
								if (mbDMAFromSD) {
									g_ATLCSIDE3DMA("$%06X [%c$%02X] <- SD Len=$%05X And=$%02X Xor=$%02X\n"
										, mDMADstAddress
										, (sint32)mDMADstStep < 0 ? '-' : '+'
										, abs((sint32)mDMADstStep)
										, (uint32)mDMACounter + 1
										, mDMAAndMask
										, mDMAXorMask
									);
								} else {
									g_ATLCSIDE3DMA("$%06X [%c$%02X] <- $%06X [%c$%02X] Len=$%05X And=$%02X Xor=$%02X\n"
										, mDMADstAddress
										, (sint32)mDMADstStep < 0 ? '-' : '+'
										, abs((sint32)mDMADstStep)
										, mDMASrcAddress
										, (sint32)mDMASrcStep < 0 ? '-' : '+'
										, abs((sint32)mDMASrcStep)
										, (uint32)mDMACounter + 1
										, mDMAAndMask
										, mDMAXorMask
									);
								}
							}
						}
					} else {
						mbDMAActive = false;
					}

					break;
				case 0x1: mDMASrcAddress = ((uint32)(value       ) <<  8) + (mDMASrcAddress & 0x1F00FF); break;
				case 0x2: mDMASrcAddress = ((uint32)(value       ) <<  0) + (mDMASrcAddress & 0x1FFF00); break;
				case 0x3: mDMADstAddress = ((uint32)(value & 0x1F) << 16) + (mDMADstAddress & 0x00FFFF); break;
				case 0x4: mDMADstAddress = ((uint32)(value       ) <<  8) + (mDMADstAddress & 0x1F00FF); break;
				case 0x5: mDMADstAddress = ((uint32)(value       ) <<  0) + (mDMADstAddress & 0x1FFF00); break;
				case 0x6:
					mDMACounter = ((uint32)value <<  8) + (mDMACounter & 0x00FF);
					break;
				case 0x7:
					mDMACounter = ((uint32)value <<  0) + (mDMACounter & 0xFF00);
					break;
				case 0x8: mDMASrcStep = (uint32)(sint8)value; break;
				case 0x9: mDMADstStep = (uint32)(sint8)value; break;
				case 0xA: mDMAAndMask = value; break;
				case 0xB: mDMAXorMask = value; break;
			}

			RescheduleDMA();
			break;

		case RegisterMode::CartridgeEmu:
			switch(addr8 & 0xF) {
				case 0x0:
					mEmuCCTLBase = value;
					break;

				case 0x1:
					mEmuCCTLMask = value;
					break;

				case 0x2:
					mEmuAddressMask = value;
					break;

				case 0x3:
					mEmuDataMask = value;
					break;

				case 0x4:
					mEmuDisableMaskA = value;
					break;

				case 0x5:
					mEmuDisableMaskB = value;
					break;

				case 0x6:
					if (mEmuFeature != value) {
						mEmuFeature = value;
						UpdateWindows();
					}
					break;

				case 0x7:
					// bit 3 doesn't exist and is always zero
					value &= 0xF7;

					// bit 4 cannot be set unless cart emu is enabled
					if (!(value & kEC_Enable))
						value &= ~kEC_Lock;

					if (const uint8 delta = mEmuControl ^ value; delta) {
						// if we are turning on cart emu, we need to reset banking state
						if (delta & value & kEC_Enable) {
							mEmuBank = mEmuBankA;
							mbEmuDisabledA = (mEmuControl2 & kEC2_DisableWinA) != 0;
							mbEmuDisabledB = (mEmuControl2 & kEC2_DisableWinB) != 0;
						}

						mbEmuLocked = (value & kEC_Lock) != 0;

						mEmuControl = value;
						UpdateWindows();
					}
					break;

				case 0x8:
					if (mEmuBankA != value) {
						mEmuBankA = value;
						UpdateWindows();
					}
					break;

				case 0x9:
					if (mEmuBankB != value) {
						mEmuBankB = value;
						UpdateWindows();
					}
					break;

				case 0xA:
					value &= 0x03;
					if (mEmuControl2 != value) {
						mEmuControl2 = value;
						UpdateWindows();
					}
					break;

				case 0xB:
					mEmuData = value;
					break;

				default:
					break;
			}
			break;

		default:
			break;
	}

	return true;
}

template<bool T_DebugRead>
sint32 ATSIDE3Emulator::OnSpecialReadByteA1(uint32 addr) const {
	const uint8 addr8 = (uint8)addr;
	uint8 v;

	if (mbWindowUsingFlashA) {
		if (mFlashCtrl.IsControlReadEnabled()) {
			if constexpr (T_DebugRead)
				v = OnFlashDebugReadA(const_cast<ATSIDE3Emulator *>(this), addr);
			else
				v = OnFlashReadA(const_cast<ATSIDE3Emulator *>(this), addr);
		} else
			v = mFlash[mWindowOffsetA1 + 0x0F00 + addr8];
	} else {
		v = mRAM[(mWindowOffsetA1 + 0x0F00 + addr8) & ((sizeof mRAM) - 1)];
	}

	if constexpr (!T_DebugRead) {
		if (addr8 >= 0xF6 && addr8 < 0xFA)
			const_cast<ATSIDE3Emulator *>(this)->OnEmuBankAccess(addr, v);
	}

	return v;
}

bool ATSIDE3Emulator::OnSpecialWriteByteA1(uint32 addr, uint8 value) {
	const uint8 addr8 = (uint8)addr;

	if (mbWindowUsingFlashA)
		OnFlashWriteA(const_cast<ATSIDE3Emulator *>(this), addr, value);
	else if (!(mBankRAMWriteProtect & 1))
		mRAM[(mWindowOffsetA1 & ((sizeof mRAM)-1)) + 0xF00 + addr8] = value;

	if (addr8 >= 0xF6 && addr8 < 0xFA)
		OnEmuBankAccess(addr, value);

	return true;
}

template<bool T_DebugRead>
sint32 ATSIDE3Emulator::OnSpecialReadByteA2(uint32 addr) const {
	const uint8 addr8 = (uint8)addr;
	uint8 v = mRAM[(mWindowOffsetA2 + 0xF00 + addr8) & ((sizeof mRAM) - 1)];

	if (mbWindowUsingFlashA) {
		if (mFlashCtrl.IsControlReadEnabled()) {
			if constexpr (T_DebugRead)
				v = OnFlashDebugReadA(const_cast<ATSIDE3Emulator *>(this), addr);
			else
				v = OnFlashReadA(const_cast<ATSIDE3Emulator *>(this), addr);
		} else
			v = mFlash[mWindowOffsetA2 + 0x0F00 + addr8];
	}

	if constexpr (!T_DebugRead) {
		if (addr8 >= 0xF6 && addr8 < 0xFA)
			const_cast<ATSIDE3Emulator *>(this)->OnEmuBankAccess(addr, v);
	}

	return v;
}

bool ATSIDE3Emulator::OnSpecialWriteByteA2(uint32 addr, uint8 value) {
	const uint8 addr8 = (uint8)addr;

	if (mbWindowUsingFlashA)
		OnFlashWriteA(const_cast<ATSIDE3Emulator *>(this), addr, value);
	else if (!(mBankRAMWriteProtect & 1))
		mRAM[(mWindowOffsetA2 & ((sizeof mRAM)-1)) + 0xF00 + addr8] = value;

	if (addr8 >= 0xF6 && addr8 < 0xFA)
		OnEmuBankAccess(addr, value);

	return true;
}

void ATSIDE3Emulator::OnEmuBankAccess(uint32 addr, sint32 value) {
	// apply CCTL address mask
	if (addr >= 0xD500 && (mEmuFeature & kEF_EnableCCTLMask)) {
		if ((addr & mEmuCCTLMask) != mEmuCCTLBase)
			return;
	}

	// check if we are responding to addresses or data
	uint8 bank;
	uint8 bankMask;
	if (mEmuFeature & kEF_UseAddress) {
		bank = addr;
		bankMask = mEmuAddressMask;
	} else {
		// drop reads
		if (value < 0)
			return;

		bank = (uint8)value;
		bankMask = mEmuDataMask;
	}

	bool disabledA = (bank & mEmuDisableMaskA) && (mEmuFeature & kEF_EnableDisA);
	bool disabledB;

	if (mEmuFeature & kEF_DisableByData) {
		disabledB = (mEmuFeature & kEF_EnableDisB) && bank == bankMask;
	} else {
		disabledB = (bank & mEmuDisableMaskB) && (mEmuFeature & kEF_EnableDisB);
	}
	
	bank &= bankMask;

	if (mEmuFeature & kEF_BountyBob) {
		if (addr & 0x1000)
			bank = (mEmuBank & 0x0F) + ((bank & 0x0F) << 4);
		else
			bank = (mEmuBank & 0xF0) + (bank & 0x0F);
	}

	if (mEmuBank != bank || mbEmuDisabledA != disabledA || mbEmuDisabledB != disabledB) {
		mbEmuDisabledA = disabledA;
		mbEmuDisabledB = disabledB;
		mEmuBank = bank;

		UpdateWindows();
	}
}

sint32 ATSIDE3Emulator::OnFlashDebugReadA(void *thisptr0, uint32 addr) {
	const ATSIDE3Emulator *thisptr = (ATSIDE3Emulator *)thisptr0;

	uint8 value;

	if (addr < 0x9000)
		thisptr->mFlashCtrl.DebugReadByte(thisptr->mWindowOffsetA1 + (addr - 0x8000), value);
	else
		thisptr->mFlashCtrl.DebugReadByte(thisptr->mWindowOffsetA2 + (addr - 0x9000), value);

	return value;
}

sint32 ATSIDE3Emulator::OnFlashDebugReadB(void *thisptr0, uint32 addr) {
	const ATSIDE3Emulator *thisptr = (ATSIDE3Emulator *)thisptr0;

	uint8 value;
	thisptr->mFlashCtrl.DebugReadByte(thisptr->mWindowOffsetB + (addr - 0xA000), value);

	return value;
}

sint32 ATSIDE3Emulator::OnFlashReadA(void *thisptr0, uint32 addr) {
	ATSIDE3Emulator *thisptr = (ATSIDE3Emulator *)thisptr0;

	const uint32 flashAddr = addr < 0x9000 ? thisptr->mWindowOffsetA1 + (addr - 0x8000) : thisptr->mWindowOffsetA2 + (addr - 0x9000);

	uint8 value;
	if (thisptr->mFlashCtrl.ReadByte(flashAddr, value)) {
		if (thisptr->mpUIRenderer) {
			if (thisptr->mFlashCtrl.CheckForWriteActivity()) {
				thisptr->mpUIRenderer->SetFlashWriteActivity();
				thisptr->mbFirmwareUsable = true;
			}
		}

		thisptr->UpdateWindows();
	}

	return value;
}

sint32 ATSIDE3Emulator::OnFlashReadB(void *thisptr0, uint32 addr) {
	ATSIDE3Emulator *thisptr = (ATSIDE3Emulator *)thisptr0;

	uint8 value;
	if (thisptr->mFlashCtrl.ReadByte(thisptr->mWindowOffsetB + (addr - 0xA000), value)) {
		if (thisptr->mpUIRenderer) {
			if (thisptr->mFlashCtrl.CheckForWriteActivity()) {
				thisptr->mpUIRenderer->SetFlashWriteActivity();
				thisptr->mbFirmwareUsable = true;
			}
		}

		thisptr->UpdateWindows();
	}

	return value;
}

bool ATSIDE3Emulator::OnFlashWriteA(void *thisptr0, uint32 addr, uint8 value) {
	ATSIDE3Emulator *thisptr = (ATSIDE3Emulator *)thisptr0;

	const uint32 flashAddr = addr < 0x9000 ? thisptr->mWindowOffsetA1 + (addr - 0x8000) : thisptr->mWindowOffsetA2 + (addr - 0x9000);

	if (thisptr->mFlashCtrl.WriteByte(flashAddr, value)) {
		if (thisptr->mpUIRenderer) {
			if (thisptr->mFlashCtrl.CheckForWriteActivity()) {
				thisptr->mpUIRenderer->SetFlashWriteActivity();
				thisptr->mbFirmwareUsable = true;
			}
		}

		thisptr->UpdateWindows();
	}

	return true;
}

bool ATSIDE3Emulator::OnFlashWriteB(void *thisptr0, uint32 addr, uint8 value) {
	ATSIDE3Emulator *thisptr = (ATSIDE3Emulator *)thisptr0;

	if (thisptr->mFlashCtrl.WriteByte(thisptr->mWindowOffsetB + (addr - 0xA000), value)) {
		if (thisptr->mpUIRenderer) {
			if (thisptr->mFlashCtrl.CheckForWriteActivity()) {
				thisptr->mpUIRenderer->SetFlashWriteActivity();
				thisptr->mbFirmwareUsable = true;
			}
		}

		thisptr->UpdateWindows();
	}

	return true;
}

void ATSIDE3Emulator::UpdateWindows() {
	UpdateWindowA();
	UpdateWindowB();
}

void ATSIDE3Emulator::UpdateWindowA() {
	// compute RD4 enable
	bool enabled = false;

	if (mbRightWindowEnabled) {
		if ((mEmuControl & kEC_Enable) && mbEmuCartEnableEffective) {
			switch(mEmuControl & kEC_Mode) {
				case 0:		// 8/16K mode
					enabled = !mbEmuDisabledA;
					break;

				case 1:		// Williams
				default:
					enabled = false;
					break;

				case 2:		// XEGS/BB mode
				case 4:		// MegaCart mode
					enabled = !mbEmuDisabledA;

					if (mEmuControl & kEC_InvertDisableA)
						enabled = !enabled;
					break;
			}
		} else {
			enabled = (mBankControl & kBC_EnableAnyA) != 0;
		}
	}

	// upstream cartridge mask
	if (!enabled) {
		mpMemMan->EnableLayer(mpMemLayerWindowA, false);
		mpMemMan->EnableLayer(mpMemLayerWindowA2, false);
		mpMemMan->EnableLayer(mpMemLayerFlashControlA, false);
		mpMemMan->EnableLayer(mpMemLayerSpecialBank1, false);
		mpMemMan->EnableLayer(mpMemLayerSpecialBank2, false);

		const bool bbsbSnoop = (mEmuControl & kEC_Enable) && (mEmuFeature & kEF_BountyBob);
		const ATMemoryAccessMode bbsbLayerMode = bbsbSnoop ? kATMemoryAccessMode_W : kATMemoryAccessMode_0;
		mpMemMan->SetLayerModes(mpMemLayerSpecialBank1, bbsbLayerMode);
		mpMemMan->SetLayerModes(mpMemLayerSpecialBank2, bbsbLayerMode);
		return;
	}

	const bool useFlash = (mBankControl & kBC_EnableFlashA) != 0;
	bool splitBanks = false;

	if (mEmuControl & kEC_Enable) {
		mpMemMan->EnableLayer(mpMemLayerFlashControlA, false);

		uint8 effectiveBank = 0;

		switch(mEmuControl & kEC_Mode) {
			case 0:		// 8/16K mode
			default:
				effectiveBank = mEmuBankA;
				break;
			case 1:		// Williams mode
				effectiveBank = 0;
				break;
			case 2:		// XEGS/BB mode
				effectiveBank = mEmuBank;
				break;
			case 4:		// MegaCart mode
				effectiveBank = mEmuBank << 1;
				break;
		}

		const bool readOnly = (mBankRAMWriteProtect & 1) != 0;
		splitBanks = (mEmuControl & kEC_Mode) == 2 && (mEmuFeature & kEF_BountyBob);

		if (splitBanks) {
			mWindowOffsetA2 = ((uint32)(effectiveBank >> 4) << 13) + 0x21000;

			effectiveBank &= 15;
		}

		mWindowOffsetA1 = (uint32)effectiveBank << 13;

		if (!splitBanks)
			mWindowOffsetA2 = mWindowOffsetA1 + 0x1000;

		const ATMemoryAccessMode bbsbLayerMode = (mEmuFeature & kEF_BountyBob) ? kATMemoryAccessMode_ARW : kATMemoryAccessMode_0;
		mpMemMan->SetLayerModes(mpMemLayerSpecialBank1, bbsbLayerMode);
		mpMemMan->SetLayerModes(mpMemLayerSpecialBank2, bbsbLayerMode);
	} else {
		if (useFlash) {
			mWindowOffsetA1 = mFlashOffsetA;
		} else {
			mWindowOffsetA1 = (uint32)mBankRAMA << 13;
		}

		mWindowOffsetA2 = mWindowOffsetA1 + 0x1000;
		mpMemMan->EnableLayer(mpMemLayerSpecialBank1, false);
		mpMemMan->EnableLayer(mpMemLayerSpecialBank2, false);
	}

	if (!splitBanks) {
		mpMemMan->EnableLayer(mpMemLayerWindowA2, false);
	}

	mbWindowUsingFlashA = useFlash;
	if (useFlash) {
		const bool enableFlashControlReadA = mFlashCtrl.IsControlReadEnabled();

		mpMemMan->SetLayerModes(mpMemLayerFlashControlA, enableFlashControlReadA ? kATMemoryAccessMode_ARW : kATMemoryAccessMode_W);
		mpMemMan->SetLayerMemory(mpMemLayerWindowA, mFlash + mWindowOffsetA1);

		if (splitBanks)
			mpMemMan->SetLayerMemory(mpMemLayerWindowA2, mFlash + mWindowOffsetA2);
	} else {
		mpMemMan->EnableLayer(mpMemLayerFlashControlA, false);
		mpMemMan->SetLayerMemory(mpMemLayerWindowA, mRAM + mWindowOffsetA1);

		if (splitBanks)
			mpMemMan->SetLayerMemory(mpMemLayerWindowA2, mRAM + mWindowOffsetA2);
	}

	const bool readOnly = (mBankRAMWriteProtect & 1) != 0;

	if (splitBanks) {
		mpMemMan->EnableLayer(mpMemLayerWindowA2, true);
		mpMemMan->SetLayerReadOnly(mpMemLayerWindowA2, readOnly);
	}

	mpMemMan->SetLayerReadOnly(mpMemLayerWindowA, readOnly);

	mpMemMan->EnableLayer(mpMemLayerWindowA, true);
}

void ATSIDE3Emulator::UpdateWindowB() {
	// compute RD5 and RAM/ROM enables
	bool enabled = false;
	if ((mEmuControl & kEC_Enable) && mbEmuCartEnableEffective) {
		enabled = !mbEmuDisabledB;

		if (mEmuControl & 7) {
			if (mEmuControl & kEC_InvertDisableB)
				enabled = !enabled;
		}
	} else {
		enabled = (mBankControl & kBC_EnableAnyB) != 0;
	}

	// these must not be gated by mbLeftWindowEnabled
	mbLeftWindowActive = enabled;
	mpCartridgePort->OnLeftWindowChanged(mCartId, enabled);

	// upstream cartridge mask
	if (!enabled) {
		mpMemMan->EnableLayer(mpMemLayerWindowB, false);
		mpMemMan->EnableLayer(mpMemLayerFlashControlB, false);
		return;
	}

	const bool useFlash = (mBankControl & kBC_EnableFlashB) != 0;

	if (mEmuControl & kEC_Enable) {
		mpMemMan->EnableLayer(mpMemLayerFlashControlB, false);

		uint8 effectiveBank = 0;

		switch(mEmuControl & 7) {
			case 0:		// 8/16K mode
			default:
				effectiveBank = mEmuBankB;
				break;
			case 1:		// Williams mode
				effectiveBank = mEmuBank;
				break;
			case 2:		// XEGS/BB mode
				effectiveBank = mEmuBankB;
				break;
			case 4:		// MegaCart mode
				effectiveBank = (mEmuBank << 1) + 1;
				break;
		}

		mpMemMan->SetLayerReadOnly(mpMemLayerWindowB, (mBankRAMWriteProtect & 2) != 0);

		const uint32 offset = (uint32)effectiveBank << 13;
		mWindowOffsetB = offset;
	} else {
		if (useFlash)
			mWindowOffsetB = mFlashOffsetB;
		else
			mWindowOffsetB = (uint32)mBankRAMB << 13;
	}

	if (useFlash) {
		const bool enableFlashControlReadB = mFlashCtrl.IsControlReadEnabled();

		mpMemMan->SetLayerModes(mpMemLayerFlashControlB, enableFlashControlReadB ? kATMemoryAccessMode_ARW : kATMemoryAccessMode_W);
		mpMemMan->SetLayerMemory(mpMemLayerWindowB, mFlash + mWindowOffsetB);
	} else {
		mpMemMan->EnableLayer(mpMemLayerFlashControlB, false);
		mpMemMan->SetLayerMemory(mpMemLayerWindowB, mRAM + mWindowOffsetB);
		mpMemMan->SetLayerReadOnly(mpMemLayerWindowB, (mBankRAMWriteProtect & 2) != 0);
	}

	mpMemMan->EnableLayer(mpMemLayerWindowB, true);
}

void ATSIDE3Emulator::UpdateControlLayer() {
	if (mpMemLayerCCTL)
		mpMemMan->EnableLayer(mpMemLayerCCTL, mbCCTLEnabled);
}

void ATSIDE3Emulator::AdvanceSPIRead() {
	// queue next read if freerunning
	if (mSDStatus & 0x08)
		WriteSPI(0xFF);
}

void ATSIDE3Emulator::WriteSPINoTimingUpdate(uint8 v) {
	mSDPrevRead = mSDNextRead;

	if (mSDStatus & 0x01)
		mSDNextRead = TransferSD(v);
	else
		mSDNextRead = TransferRTC(v);

	mSDCRC7 = ATAdvanceCRC7(mSDCRC7, mSDPrevRead);
}

void ATSIDE3Emulator::WriteSPI(uint8 v) {
	WriteSPINoTimingUpdate(v);

	const uint32 transferDelay = mSDStatus & 0x10 ? 1 : 32;
	mSDNextTransferTime = mpScheduler->GetTick64() + transferDelay;

	EnableXferLED();
}

uint8 ATSIDE3Emulator::TransferSD(uint8 v) {
	if (!mpBlockDevice)
		return 0xFF;

	uint8 reply = 0xFF;

	if (mSDResponseIndex < mSDResponseLength) {
		reply = mResponseBuffer[mSDResponseIndex++];

		if (mSDResponseIndex >= mSDResponseLength) {
			switch(mSDActiveCommandMode) {
				case SDActiveCommandMode::None:
					break;

				case SDActiveCommandMode::ReadMultiple:
					if (mSDActiveCommandLBA + 1 >= mpBlockDevice->GetSectorCount()) {
						mResponseBuffer[0] = 0x08;
						mSDResponseIndex = 0;
						mSDResponseLength = 1;
					} else {
						if (mbDMAActive && mbDMAFromSD)
							g_ATLCSD("Read multiple LBA $%04X (DMA length remaining $%04X)\n", mSDActiveCommandLBA, mDMAActiveBytesLeft);
						else
							g_ATLCSD("Read multiple LBA $%04X\n", mSDActiveCommandLBA);

						if (SetupRead(mSDActiveCommandLBA, false))
							++mSDActiveCommandLBA;
						else
							mSDActiveCommandMode = SDActiveCommandMode::None;
					}
					break;
			}
		}
	}

	switch(mSDActiveCommandMode) {
		case SDActiveCommandMode::None:
		case SDActiveCommandMode::ReadMultiple:
			if (mSDCommandState < 6) {
				// for the first byte, check the start and transmitter bits (01)
				if (mSDCommandState > 0 || (v & 0xC0) == 0x40)
					mSDCommandFrame[mSDCommandState++] = v;

				// for the last byte, check if we got a stop bit, and toss the frame
				// if not
				if (mSDCommandState == 6 && !(v & 0x01))
					mSDCommandState = 0;
			}

			if (mSDCommandState >= 6) {
				mSDCommandState = 0;

				// verify CRC7 if we are in SD mode still or CMD8 is being sent, as
				// CMD8 is always CRC checked
				bool commandValid = true;

				if (mbSDCRCEnabled || (mSDCommandFrame[0] & 0x3F) == 8) {
					uint8 crc7 = 0;

					for(uint32 i = 0; i < 5; ++i)
						crc7 = ATAdvanceCRC7(crc7, mSDCommandFrame[i]);

					crc7 |= 1;

					if (crc7 != mSDCommandFrame[5]) {
						g_ATLCSD("Dropping command frame with bad CRC7: %02X %02X %02X %02X %02X %02X (expected %02X)\n"
							, mSDCommandFrame[0]
							, mSDCommandFrame[1]
							, mSDCommandFrame[2]
							, mSDCommandFrame[3]
							, mSDCommandFrame[4]
							, mSDCommandFrame[5]
							, crc7);

						commandValid = false;
					}
				}

				if (commandValid) {
					if (g_ATLCSD.IsEnabled()) {
						int cmd = mSDCommandFrame[0] & 0x3F;
						const char *cmdname = nullptr;

						if (mbSDAppCommand) {
							switch(cmd) {
								case 41: cmdname = "SD send op cond"; break;
								case 55: cmdname = "App cmd"; break;
							}
						} else {
							switch(cmd) {
								case  0: cmdname = "Go idle state"; break;
								case  8: cmdname = "Send IF cond"; break;
								case  9: cmdname = "Send CSD"; break;
								case 10: cmdname = "Send CID"; break;
								case 12: cmdname = "Stop transmission"; break;
								case 17: cmdname = "Read single block"; break;
								case 18: cmdname = "Read multiple block"; break;
								case 23: cmdname = "Set block count"; break;
								case 24: cmdname = "Write block"; break;
								case 25: cmdname = "Write multiple block"; break;
								case 55: cmdname = "App cmd"; break;
								case 58: cmdname = "Read OCR"; break;
								case 59: cmdname = "CRC on/off"; break;
							}
						}

						g_ATLCSD("Command: %02X %02X %02X %02X %02X %02X (%sCMD%-2d%s%s%s)\n"
							, mSDCommandFrame[0]
							, mSDCommandFrame[1]
							, mSDCommandFrame[2]
							, mSDCommandFrame[3]
							, mSDCommandFrame[4]
							, mSDCommandFrame[5]
							, mbSDAppCommand && cmd != 55 ? "A" : ""
							, cmd
							, mbSDAppCommand && cmd != 55 ? "" : " "
							, cmdname ? " " : ""
							, cmdname ? cmdname : ""
						);
					}

					const uint8 commandId = mSDCommandFrame[0] & 0x3F;

					if (!mbSDSPIMode) {
						if (commandId == 0) {
							g_ATLCSD("CMD0 received -- switching to SPI mode.\n");
							mbSDSPIMode = true;
							mbSDCRCEnabled = false;

							mResponseBuffer[0] = 0xFF;
							mResponseBuffer[1] = 0x01;
							mSDResponseIndex = 0;
							mSDResponseLength = 2;
						}
					} else if (mbSDAppCommand) {
						mbSDAppCommand = false;

						switch(commandId) {
							case 41:	// ACMD41 (send op cond)
								mResponseBuffer[0] = 0x00;
								mSDResponseIndex = 0;
								mSDResponseLength = 1;
								break;

							case 55:	// CMD55 (app command) (even if already shifted)
								mbSDAppCommand = true;
								mResponseBuffer[0] = 0x00;
								mSDResponseIndex = 0;
								mSDResponseLength = 1;
								break;

							default:
								// return illegal command
								mResponseBuffer[0] = 0x04;
								mSDResponseIndex = 0;
								mSDResponseLength = 1;
								break;
						}
					} else if (mSDActiveCommandMode == SDActiveCommandMode::None || commandId == 12) {
						switch(commandId) {
							case 0:		// CMD0 (go idle state)
								mResponseBuffer[0] = 0x01;
								mSDResponseIndex = 0;
								mSDResponseLength = 1;
								break;

							case 8:		// CMD8 (send interface condition)
								mResponseBuffer[0] = 0x00;
								mResponseBuffer[1] = 0x00;
								mResponseBuffer[2] = 0x00;
								mResponseBuffer[3] = mSDCommandFrame[3];	// voltage
								mResponseBuffer[4] = mSDCommandFrame[4];	// check pattern
								{
									uint8 crc7 = 0;

									for(int i=0; i<5; ++i)
										crc7 = ATAdvanceCRC7(crc7, mResponseBuffer[i]);

									mResponseBuffer[5] = crc7 | 1;
								}
								mSDResponseIndex = 0;
								mSDResponseLength = 6;
								break;

							case 9:		// CMD9 (send CSD)
								// send R1 response
								mResponseBuffer[0] = 0x00;

								// start with a CSD 2.0 block for SDHC/SDXC
								mResponseBuffer[ 1] = 0xFE;
								mResponseBuffer[ 2] = 0b01000000;	// [127:120] CSD structure (01), reserved (000000)
								mResponseBuffer[ 3] = 0b00001010;	// [119:112] TAAC = 0Eh
								mResponseBuffer[ 4] = 0b00000000;	// [111:104] NSAC = 00h
								mResponseBuffer[ 5] = 0b00110010;	// [103: 96] TRAN_SPEED = 32h
								mResponseBuffer[ 6] = 0b01011011;	// [ 95: 88] CCC = 010110110101
								mResponseBuffer[ 7] = 0b01011001;	// [ 87: 80] CCC con't, READ_BL_LEN = 9
								mResponseBuffer[ 8] = 0b00000000;	// [ 79: 72] READ_BL_PARTIAL=0, WRITE_BLK_MISALIGN=0, READ_BLK_MISALIGN=0, DSR_IMP=0, reserved (4)
								mResponseBuffer[ 9] = 0b00000000;	// [ 71: 64] reserved(2), C_SIZE=0 (TBD)
								mResponseBuffer[10] = 0b00000000;	// [ 63: 56] C_SIZE cont'd
								mResponseBuffer[11] = 0b00000000;	// [ 55: 48] C_SIZE cont'd
								mResponseBuffer[12] = 0b01111111;	// [ 47: 40] reserved(1), SECTOR_SIZE=7Fh
								mResponseBuffer[13] = 0b10000000;	// [ 39: 32] SECTOR_SIZE cont'd, WP_GRP_SIZE=0
								mResponseBuffer[14] = 0b00001010;	// [ 31: 24] WP_GRP_ENABLE=0, reserved(2), R2W_FACTOR=010b, WRITE_BL_LEN=9
								mResponseBuffer[15] = 0b01000000;	// [ 23: 16] WRITE_BL_LEN con'td, WRITE_BL_PARTIAL=0, reserved(5)
								mResponseBuffer[16] = 0b00000000;	// [ 15:  8] FILE_FORMAT_GRP=0, COPY=0, PERM_WRITE_PROTECT=0, TMP_WRITE_PROTECT=0, FILE_FORMAT=0, reserved=0
								mResponseBuffer[17] = 0b00000000;	// [  7:  0] CRC (TBD), 1-bit

								if (uint32 blockCount = mpBlockDevice->GetSectorCount(); mbSDHC) {
									// set device size (units of 512KB - 1)
									uint32 sizeBlocks = ((blockCount + 1023) >> 10) - 1;
									mResponseBuffer[ 9] = (uint8)(sizeBlocks >> 16);
									mResponseBuffer[10] = (uint8)(sizeBlocks >>  8);
									mResponseBuffer[11] = (uint8)(sizeBlocks >>  0);
								} else {
									// change CSD version to 1.0
									mResponseBuffer[2] = 0;

									// compute C_SIZE and C_SIZE_MULT
									// block count = (C_SIZE + 1) * 2^(C_SIZE_MULT + 2)
									// This means that minimum size = 8 blocks, maximum size = 4096*2^9 = 2097152 blocks
									uint32 base = blockCount;
									uint32 exp = 2;

									// if the card exceeds 1GB in size, we must specify the capacity in 1KB blocks instead
									if (blockCount > 2097152) {
										++mResponseBuffer[7];
										blockCount >>= 1;
									}

									while(base > 4096 && exp < 9) {
										base >>= 1;
										++exp;
									}

									// C_SIZE is in [73:62], so we encode it <<6 and OR it into [79:56]
									const uint32 encodedCSize = std::min<uint32>(base - 1, 4095) << 6;
									mResponseBuffer[ 8] |= (uint8)(encodedCSize >> 16);
									mResponseBuffer[ 9] |= (uint8)(encodedCSize >>  8);
									mResponseBuffer[10] |= (uint8)(encodedCSize >>  0);

									// C_SIZE_MULT is in [49:47], so encode it <<7 and OR it into [55:40]
									const uint32 encodedCSizeMult = (exp - 2) << 7;
									mResponseBuffer[11] |= (uint8)(encodedCSizeMult >> 8);
									mResponseBuffer[12] |= (uint8)encodedCSizeMult;
								}

								// set CRC7
								mResponseBuffer[17] = ATComputeCRC7(0, mResponseBuffer + 2, 15) | 1;

								// set data frame CRC16
								VDWriteUnalignedBEU16(&mResponseBuffer[18], ATComputeCRC16(0, mResponseBuffer + 2, 16));

								mSDResponseIndex = 0;
								mSDResponseLength = 20;
								break;

							case 10:	// CMD10 (send CID)
								// send R1 response
								mResponseBuffer[0] = 0x00;

								// start with a CSD 2.0 block for SDHC/SDXC
								mResponseBuffer[ 1] = 0xFE;
								mResponseBuffer[ 2] = 0x00;			// [127:120] MID=0
								mResponseBuffer[ 3] = 'X';			// [119:112] OID
								mResponseBuffer[ 4] = 'X';			// [111:104] OID cont'd
								mResponseBuffer[ 5] = 'S';			// [103: 96] PNM
								mResponseBuffer[ 6] = 'D';			// [ 95: 88] PNM cont'd
								mResponseBuffer[ 7] = 'E';			// [ 87: 80] PNM cont'd
								mResponseBuffer[ 8] = 'M';			// [ 79: 72] PNM cont'd
								mResponseBuffer[ 9] = 'U';			// [ 71: 64] PNM cont'd
								mResponseBuffer[10] = 0b00010000;	// [ 63: 56] PRV = 1.0
								mResponseBuffer[11] = 0b00000000;	// [ 55: 48] PSN = 0
								mResponseBuffer[12] = 0b00000000;	// [ 47: 40] PSN cont'd
								mResponseBuffer[13] = 0b00000000;	// [ 39: 32] PSN cont'd
								mResponseBuffer[14] = 0b00000000;	// [ 31: 24] PSN cont'd
								mResponseBuffer[15] = 0b00000001;	// [ 23: 16] reserved(), MDT = January 2020 (141h)
								mResponseBuffer[16] = 0b01000001;	// [ 15:  8] MDT cont'd
								mResponseBuffer[17] = 0b00000000;	// [  7:  0] CRC (TBD), 1-bit

								// set CRC7
								mResponseBuffer[17] = ATComputeCRC7(0, mResponseBuffer + 1, 15) | 1;

								// set CRC16
								VDWriteUnalignedBEU16(&mResponseBuffer[19], ATComputeCRC16(0, mResponseBuffer + 2, 16));

								mSDResponseIndex = 0;
								mSDResponseLength = 20;
								break;

							case 12:	// CMD12 (stop transmission)
								mResponseBuffer[0] = 0xFF;
								mResponseBuffer[1] = 0x00;
								mSDResponseIndex = 0;
								mSDResponseLength = 2;
								mSDActiveCommandMode = SDActiveCommandMode::None;
								break;

							case 17:	// CMD17 (read sector)
								{
									uint8 error = SetupLBA();

									if (error) {
										mResponseBuffer[0] = 0xFF;
										mResponseBuffer[1] = error;
										mSDResponseIndex = 0;
										mSDResponseLength = 2;
									} else
										SetupRead(mSDActiveCommandLBA, true);
								}
								break;

							case 18:	// CMD18 (read multiple)
								{
									uint8 error = SetupLBA();

									mResponseBuffer[0] = 0xFF;
									mResponseBuffer[1] = error;
									mSDResponseIndex = 0;
									mSDResponseLength = 2;

									if (!error)
										mSDActiveCommandMode = SDActiveCommandMode::ReadMultiple;
								}
								break;

							case 24:	// CMD24 (write block)
								{
									uint8 error = SetupLBA();

									mResponseBuffer[0] = 0xFF;
									mResponseBuffer[1] = error;
									mSDResponseIndex = 0;
									mSDResponseLength = 2;

									if (!error)
										mSDActiveCommandMode = SDActiveCommandMode::Write;
								}
								break;

							case 25:	// CMD25 (write multiple block)
								{
									uint8 error = SetupLBA();

									mResponseBuffer[0] = 0xFF;
									mResponseBuffer[1] = error;
									mSDResponseIndex = 0;
									mSDResponseLength = 2;

									if (!error)
										mSDActiveCommandMode = SDActiveCommandMode::WriteMultiple;
								}
								break;

							case 55:	// CMD55 (app command)
								mResponseBuffer[0] = 0x00;
								mSDResponseIndex = 0;
								mSDResponseLength = 1;
								mbSDAppCommand = true;
								break;

							case 58:	// CMD58 (read OCR)
								mResponseBuffer[0] = 0x00;
								mResponseBuffer[1] = mbSDHC ? 0x40 : 0x00;
								mResponseBuffer[2] = 0x00;
								mResponseBuffer[3] = 0x00;
								mResponseBuffer[4] = 0x00;
								mSDResponseIndex = 0;
								mSDResponseLength = 5;
								break;

							case 59:	// CMD59 (CRC on/off)
								mbSDCRCEnabled = (mSDCommandFrame[4] & 0x01) != 0;
								break;

							default:
								// return illegal command
								mResponseBuffer[0] = 0x04;
								mSDResponseIndex = 0;
								mSDResponseLength = 1;
								break;
						}
					}
				}
			}
			break;

		case SDActiveCommandMode::Write:
			if (mSDSendIndex == 0) {
				if (v == 0xFE)
					mSDSendIndex = 1;
			} else if (mSDSendIndex > 0) {
				mSendBuffer[mSDSendIndex++ - 1] = v;

				if (mSDSendIndex == 515) {
					mSDSendIndex = 0;

					SetupWrite();

					mSDActiveCommandMode = SDActiveCommandMode::None;
				}
			}
			break;

		case SDActiveCommandMode::WriteMultiple:
			if (mSDSendIndex == 0) {
				if (v == 0xFE)
					mSDSendIndex = 1;
			} else if (mSDSendIndex > 0) {
				mSendBuffer[mSDSendIndex++ - 1] = v;

				if (mSDSendIndex == 515) {
					mSDSendIndex = 0;

					SetupWrite();
					++mSDActiveCommandLBA;
				}
			}
			break;
	}

	return reply;
}

uint8 ATSIDE3Emulator::SetupLBA() {
	uint32 lba = VDReadUnalignedBEU32(&mSDCommandFrame[1]);

	if (!mbSDHC) {
		if (lba & 0x1FF)
			return 0x20;

		lba >>= 9;
	}

	if (lba >= mpBlockDevice->GetSectorCount())
		return 0x40;

	mSDActiveCommandLBA = lba;
	return 0;
}

bool ATSIDE3Emulator::SetupRead(uint32 lba, bool addOK) {
	if (!mpBlockDevice || lba >= mpBlockDevice->GetSectorCount()) {
		mResponseBuffer[0] = 0xFF;
		mResponseBuffer[1] = 0x40;
		mSDResponseIndex = 0;
		mSDResponseLength = 2;
		return false;
	}

	mResponseBuffer[0] = 0xFF;
	mResponseBuffer[1] = addOK ? 0x00 : 0xFF;
	mResponseBuffer[2] = 0xFE;

	if (mpUIRenderer)
		mpUIRenderer->SetIDEActivity(false, lba);

	try {
		mpBlockDevice->ReadSectors(mResponseBuffer + 3, lba, 1);
	} catch(...) {
	}

	VDWriteUnalignedBEU16(&mResponseBuffer[515], ATComputeCRC16(0, mResponseBuffer + 3, 512));

	mSDResponseIndex = 0;
	mSDResponseLength = 516;
	return true;
}

void ATSIDE3Emulator::SetupWrite() {
	if (!mpBlockDevice || mSDActiveCommandLBA >= mpBlockDevice->GetSectorCount()) {
		mResponseBuffer[0] = 0xFF;
		mResponseBuffer[1] = 0x40;
		mSDResponseIndex = 0;
		mSDResponseLength = 2;
		return;
	}

	if (mbSDCRCEnabled) {
		const uint16 expectedCRC = ATComputeCRC16(0, mSendBuffer, 512);
		const uint16 receivedCRC = VDReadUnalignedBEU16(&mSendBuffer[512]);

		if (expectedCRC != receivedCRC) {
			mResponseBuffer[0] = 0xFF;
			mResponseBuffer[1] = 0x0B;		// data rejected due to CRC error
			mSDResponseIndex = 0;
			mSDResponseLength = 2;
			return;
		}
	}

	mResponseBuffer[0] = 0xFF;
	mResponseBuffer[1] = 0x0D;		// data rejected due to write error
	mSDResponseIndex = 0;
	mSDResponseLength = 2;

	if (!mpBlockDevice->IsReadOnly()) {
		if (mpUIRenderer)
			mpUIRenderer->SetIDEActivity(true, mSDActiveCommandLBA);

		try {
			mpBlockDevice->WriteSectors(mSendBuffer, mSDActiveCommandLBA, 1);
			mResponseBuffer[1] = 0x05;		// data accepted
		} catch(...) {
		}
	}
}

void ATSIDE3Emulator::ResetSD() {
	mSDActiveCommandMode = SDActiveCommandMode::None;
	mSDCommandState = 0;
	mSDSendIndex = 0;
	mbSDSPIMode = false;
	mbSDCRCEnabled = true;
}

uint8 ATSIDE3Emulator::TransferRTC(uint8 v) {
	return mRTC.Transfer(v);
}

void ATSIDE3Emulator::AdvanceDMA() {
	if (!mbDMAActive)
		return;

	uint32 ticks = mpScheduler->GetTick() - mDMALastCycle;
	uint32 maxCount = (uint32)mDMAActiveBytesLeft;
	uint32 count = 0;

	if (mbDMAFromSD) {
		maxCount = std::min<uint32>(maxCount, ticks >> 1);

		if (count < maxCount)
			EnableXferLED();

		for(; count < maxCount; ++count) {
			uint8 v = mSDNextRead;
			WriteSPINoTimingUpdate(0xFF);

			// Because DMA transfers are intended for SD, after 512 bytes the DMA engine will
			// discard two bytes for the CRC and then wait for a new Start Block token ($FE).
			// No Start Block is needed for the first sector.
			if (mDMAState == 514) {
				if (v == 0xFE)
					mDMAState = 0;
			} else if (mDMAState++ < 512) {
				mRAM[mDMAActiveDstAddress] = v;
				mDMAActiveDstAddress = (mDMAActiveDstAddress + mDMADstStep) & 0x1FFFFF;
				--mDMAActiveBytesLeft;
			}
		}

		mDMALastCycle += count*2;
	} else {
		maxCount = std::min<uint32>(maxCount, ticks);

		for(; count < maxCount; ++count) {
			mRAM[mDMAActiveDstAddress] = (mRAM[mDMAActiveSrcAddress] & mDMAAndMask) ^ mDMAXorMask;

			mDMAActiveDstAddress = (mDMAActiveDstAddress + mDMADstStep) & 0x1FFFFF;
			mDMAActiveSrcAddress = (mDMAActiveSrcAddress + mDMASrcStep) & 0x1FFFFF;
		}

		mDMALastCycle += count;
		mDMAActiveBytesLeft -= count;
	}

	if (!mDMAActiveBytesLeft)
		mbDMAActive = false;
}

void ATSIDE3Emulator::RescheduleDMA() {
	if (mbDMAActive) {

		uint32 ticks = (uint32)mDMACounter + 1;

		if (mbDMAFromSD)
			ticks += ticks;

		uint32 t = mpScheduler->GetTick();
		uint32 tNext = mDMALastCycle + ticks;
		uint32 dt = ATWrapTime{tNext} <= t ? 1 : tNext - t;

		if (dt > 128)
			dt = 128;

		mpScheduler->SetEvent(dt, this, kEventID_DMA, mpDMAEvent);
	} else {
		mpScheduler->UnsetEvent(mpDMAEvent);
	}
}

void ATSIDE3Emulator::UpdateLED() {
	if (!mpUIRenderer || !mbActivityIndicatorEnable)
		return;

	mpUIRenderer->SetCartridgeActivity(mbLEDGreenEnabled ? mLEDGreenColor : -1, mpLEDXferEvent ? 0xA00000 : -1);
}

void ATSIDE3Emulator::UpdateLEDGreen() {
	// The green LED is (SDpower xor LEDbit).
	bool greenEnabled = (mSDStatus & 0x20) != 0;

	if (mbLEDGreenManualEnabled)
		greenEnabled = !greenEnabled;

	if (mbLEDGreenEnabled != greenEnabled) {
		mbLEDGreenEnabled = greenEnabled;

		UpdateLED();
	}
}

void ATSIDE3Emulator::UpdateLEDIntensity() {
	float f = powf((float)mLEDBrightness / 255.0f, 1.0 / 3.0f);
	float g = 1.5f * std::max<float>(0.0f, f);
	float rb = g - 1.0f;
	mLEDGreenColor = VDColorRGB(saturate(vdfloat32x4::set(rb, g, rb, 0.0f))).LinearToSRGB().ToRGB8();

	if (mbLEDGreenEnabled)
		UpdateLED();
}

void ATSIDE3Emulator::EnableXferLED() {
	static constexpr uint32 kLEDOffTime = 65536;

	mLEDXferOffTime = mpScheduler->GetTick() + kLEDOffTime;
	if (!mpLEDXferEvent) {
		mpLEDXferEvent = mpScheduler->AddEvent(kLEDOffTime, this, kEventID_LED);
		UpdateLED();
	}
}

bool ATSIDE3Emulator::UpdateEmuCartEnabled() {
	bool enabled = mbEmuCartEnableRequested || !mbSDXEnable;

	if (mbEmuCartEnableEffective == enabled)
		return false;

	mbEmuCartEnableEffective = enabled;

	if (!(mEmuControl & kEC_Enable))
		return false;

	return true;
}
