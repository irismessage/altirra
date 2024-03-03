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

#ifndef f_AT_SIDE3_H
#define f_AT_SIDE3_H

#include <at/atcore/blockdevice.h>
#include <at/atcore/devicecart.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/deviceparentimpl.h>
#include <at/atcore/devicesnapshot.h>
#include <at/atcore/devicestorageimpl.h>
#include <at/atemulation/flash.h>
#include <at/atemulation/rtcmcp7951x.h>

class IATDeviceIndicatorManager;
class ATMemoryManager;
class ATMemoryLayer;
class ATIDEEmulator;
class ATSimulator;
class ATFirmwareManager;

class ATSIDE3Emulator
	: public ATDevice
	, public IATDeviceScheduling
	, public IATDeviceMemMap
	, public IATDeviceCartridge
	, public IATDeviceIndicators
	, public IATDeviceFirmware
	, public IATDeviceParent
	, public ATDeviceBus
	, public IATDeviceDiagnostics
	, public IATDeviceButtons
	, public IATDeviceSnapshot
	, public IATSchedulerCallback
{
	ATSIDE3Emulator(const ATSIDE3Emulator&) = delete;
	ATSIDE3Emulator& operator=(const ATSIDE3Emulator&) = delete;
public:
	ATSIDE3Emulator();
	~ATSIDE3Emulator();

	void *AsInterface(uint32 id) override;

	void GetSettings(ATPropertySet& settings) override;
	bool SetSettings(const ATPropertySet& settings) override;
	void GetSettingsBlurb(VDStringW& buf) override;
	void Init() override;
	void Shutdown() override;
	void GetDeviceInfo(ATDeviceInfo& info) override;
	void ColdReset() override;

	bool IsSDXEnabled() const { return mbSDXEnable; }
	void SetSDXEnabled(bool enable);

	void ResetCartBank();

public:		// IATDeviceScheduling
	void InitScheduling(ATScheduler *sch, ATScheduler *slowsch);

public:		// IATDeviceMemMap
	void InitMemMap(ATMemoryManager *memmap);
	bool GetMappedRange(uint32 index, uint32& lo, uint32& hi) const;

public:		// IATDeviceCartridge
	void InitCartridge(IATDeviceCartridgePort *cartPort) override;
	bool IsLeftCartActive() const override;
	void SetCartEnables(bool leftEnable, bool rightEnable, bool cctlEnable) override;
	void UpdateCartSense(bool leftActive) override;

public:		// IATDeviceIndicators
	void InitIndicators(IATDeviceIndicatorManager *r) override;

public:		// IATDeviceFirmware
	void InitFirmware(ATFirmwareManager *fwman) override;
	bool ReloadFirmware() override;
	const wchar_t *GetWritableFirmwareDesc(uint32 idx) const override;
	bool IsWritableFirmwareDirty(uint32 idx) const override;
	void SaveWritableFirmware(uint32 idx, IVDStream& stream) override;
	ATDeviceFirmwareStatus GetFirmwareStatus() const override;

public:		// IATDeviceBus
	IATDeviceBus *GetDeviceBus(uint32 index) override;

public:		// IATDeviceParent
	const wchar_t *GetBusName() const override;
	const char *GetBusTag() const override;
	const char *GetSupportedType(uint32 index) override;
	void GetChildDevices(vdfastvector<IATDevice *>& devs) override;
	void AddChildDevice(IATDevice *dev) override;
	void RemoveChildDevice(IATDevice *dev) override;

public:		// IATDeviceDiagnostics
	void DumpStatus(ATConsoleOutput& output) override;
	
public:		// IATDeviceButtons
	uint32 GetSupportedButtons() const override;
	bool IsButtonDepressed(ATDeviceButton idx) const override;
	void ActivateButton(ATDeviceButton idx, bool state) override;

public:		// IATDeviceSnapshot
	void LoadState(const IATObjectState *state, ATSnapshotContext& ctx) override;
	vdrefptr<IATObjectState> SaveState(ATSnapshotContext& ctx) const override;

public:		// IATSchedulerCallback
	void OnScheduledEvent(uint32 id) override;

protected:
	void LoadNVRAM();
	void SaveNVRAM();

	template<bool T_DebugRead>
	sint32 OnReadByte(uint32 addr) const;

	template<bool T_DebugRead>
	sint32 ReadPrimaryReg(uint8 addr8) const;

	sint32 ReadDMAReg(uint8 addr8) const;
	sint32 ReadEmuReg(uint8 addr8) const;

	bool OnWriteByte(uint32 addr, uint8 value);

	void WritePrimaryReg(uint8 addr8, uint8 value);
	void WriteDMAReg(uint8 addr8, uint8 value);
	void WriteEmuReg(uint8 addr8, uint8 value);

	template<bool T_DebugRead>
	sint32 OnSpecialReadByteA1(uint32 addr) const;
	bool OnSpecialWriteByteA1(uint32 addr, uint8 value);

	template<bool T_DebugRead>
	sint32 OnSpecialReadByteA2(uint32 addr) const;
	bool OnSpecialWriteByteA2(uint32 addr, uint8 value);

	void OnEmuBankAccess(uint32 addr, sint32 value);

	// In SIDE 3, window A is $8000-9FFF, B is $A000-BFFF. Yes, this is backwards
	// from the usual convention.
	static sint32 OnFlashDebugReadA(void *thisptr, uint32 addr);
	static sint32 OnFlashDebugReadB(void *thisptr, uint32 addr);
	static sint32 OnFlashReadA(void *thisptr, uint32 addr);
	static sint32 OnFlashReadB(void *thisptr, uint32 addr);
	static bool OnFlashWriteA(void *thisptr, uint32 addr, uint8 value);
	static bool OnFlashWriteB(void *thisptr, uint32 addr, uint8 value);

	void UpdateWindows();
	void UpdateWindowA();
	void UpdateWindowB();
	void UpdateControlLayer();
	void AdvanceSPIRead();
	void WriteSPINoTimingUpdate(uint8 v);
	void WriteSPI(uint8 v);
	uint8 TransferSD(uint8 v);
	uint8 SetupLBA();
	bool SetupRead(uint32 lba, bool addOK);
	void SetupWrite();
	void ResetSD();
	uint8 TransferRTC(uint8 v);
	void AdvanceDMA();
	void RescheduleDMA();

	void UpdateLED();
	void UpdateLEDGreen();
	void UpdateLEDIntensity();
	void EnableXferLED();
	[[nodiscard]] bool UpdateEmuCartEnabled();

	// The DMA engine uses 21-bit addressing. For the original 2MB devices,
	// this is fine, but for the 8MB devices it can only address the lowest
	// 2MB of RAM.
	static constexpr uint32 kDMAMask = 0x1FFFFF;

	enum EventID : uint8 {
		kEventID_DMA = 1,
		kEventID_LED = 2
	};

	enum BankControlBits : uint8 {
		kBC_EnableMemA = 0x01,
		kBC_EnableMemB = 0x02,
		kBC_EnableFlashA = 0x04,	// has priority over memory A
		kBC_EnableFlashB = 0x08,	// has priority over memory B
		kBC_EnableAnyA = kBC_EnableMemA | kBC_EnableFlashA,
		kBC_EnableAnyB = kBC_EnableMemB | kBC_EnableFlashB,
		kBC_HighBankA = 0x30,
		kBC_HighBankB = 0xC0
	};

	IATDeviceIndicatorManager *mpUIRenderer = nullptr;
	ATMemoryManager *mpMemMan = nullptr;
	ATScheduler *mpScheduler = nullptr;
	ATMemoryLayer *mpMemLayerCCTL = nullptr;
	ATMemoryLayer *mpMemLayerWindowA = nullptr;
	ATMemoryLayer *mpMemLayerWindowA2 = nullptr;
	ATMemoryLayer *mpMemLayerWindowB = nullptr;
	ATMemoryLayer *mpMemLayerFlashControlA = nullptr;
	ATMemoryLayer *mpMemLayerFlashControlB = nullptr;
	ATMemoryLayer *mpMemLayerSpecialBank1 = nullptr;
	ATMemoryLayer *mpMemLayerSpecialBank2 = nullptr;
	bool	mbRecoveryMode = false;
	bool	mbSDXEnable = false;
	bool	mbFirmwareUsable = false;
	bool	mbApertureEnable = false;			// Enable mapping SRAM $1FF500-1FF57F to $D500-D57F
	uint8	mBankFlashA = 0;
	uint8	mBankFlashB = 0;
	uint16	mBankRAMA = 0;
	uint16	mBankRAMB = 0;
	uint8	mBankControl = 0;
	uint8	mBankMisc = 0;		// bits 0-2 valid in hardware; bit 2 is unknown but is read/write
	uint32	mFlashOffsetA = 0;				// latched flash offset for native banking
	uint32	mFlashOffsetB = 0;				// latched flash offset for native banking
	uint32	mWindowOffsetA1 = 0;			// effective flash offset for $8000-8FFF
	uint32	mWindowOffsetA2 = 0;			// effective flash offset for $9000-9FFF
	uint32	mWindowOffsetB = 0;				// effective flash offset for $A000-BFFF
	bool	mbWindowUsingFlashA = false;

	bool	mbActivityIndicatorEnable = true;
	uint8	mLEDBrightness = 0;			// $D5F0 primary set
	uint32	mLEDGreenColor = 0;

	bool mbLEDGreenEnabled = false;
	bool mbLEDGreenManualEnabled = false;

	ATEvent	*mpLEDXferEvent = nullptr;
	uint32	mLEDXferOffTime = 0;

	bool	mbButtonPressed = false;
	bool	mbButtonPBIMode = false;
	bool	mbColdStartFlag = false;

	enum class HwVersion : uint8 {
		V10,
		V14
	} mHwVersion = HwVersion::V10;

	enum class RegisterMode : uint8 {
		Primary,
		DMA,
		CartridgeEmu,
		Reserved
	} mRegisterMode = RegisterMode::Primary;

	uint8	mSDStatus = 0x00;		// $D5F3 primary set
	uint8	mSDCRC7 = 0;
	uint64	mSDNextTransferTime = 0;
	uint8	mSDPrevRead = 0;
	uint8	mSDNextRead = 0;
	bool	mbSDSlowClock = false;

	uint8	mSDCommandFrame[6] {};
	uint8	mSDCommandState = 0;
	bool	mbSDHC = true;
	bool	mbSDSPIMode = false;
	bool	mbSDAppCommand = false;
	bool	mbSDCRCEnabled = true;
	uint32	mSDResponseIndex = 0;
	uint32	mSDResponseLength = 0;
	uint32	mSDSendIndex = 0;

	enum class SDActiveCommandMode : uint8 {
		None,
		ReadMultiple,
		Write,
		WriteMultiple
	} mSDActiveCommandMode = SDActiveCommandMode::None;
	uint32	mSDActiveCommandLBA = 0;

	bool	mbDMAActive = false;

	enum class DMAMode : uint8 {	// must match DMA0[6:5] encoding
		SDRead,		// %00
		Memory,		// %01
		SDWrite,	// %10
		Reserved	// %11
	} mDMAMode = DMAMode::Memory;

	uint32	mDMASrcAddress = 0;
	uint32	mDMADstAddress = 0;
	uint16	mDMACounter = 0;
	uint32	mDMAActiveSrcAddress = 0;
	uint32	mDMAActiveDstAddress = 0;
	uint32	mDMAActiveBytesLeft = 0;
	uint32	mDMASrcStep = 0;
	uint32	mDMADstStep = 0;
	uint8	mDMAAndMask = 0;
	uint8	mDMAXorMask = 0;
	uint64	mDMALastCycle = 0;
	uint32	mDMAState = 0;
	uint8	mDMAStatus = 0;

	ATEvent *mpDMAEvent = nullptr;

	uint8	mEmuCCTLBase = 0;
	uint8	mEmuCCTLMask = 0;
	uint8	mEmuAddressMask = 0;
	uint8	mEmuDataMask = 0;
	uint8	mEmuDisableMaskA = 0;
	uint8	mEmuDisableMaskB = 0;
	bool	mbEmuDisabledA = false;
	bool	mbEmuDisabledB = false;
	bool	mbEmuLocked = false;
	bool	mbEmuCartEnableRequested = false;
	bool	mbEmuCartEnableEffective = false;

	enum : uint8 {
		kEF_UseAddress = 0x80,
		kEF_EnableDisA = 0x10,
		kEF_EnableDisB = 0x08,
		kEF_DisableByData = 0x04,
		kEF_EnableCCTLMask = 0x02,
		kEF_BountyBob = 0x01
	};

	uint8	mEmuFeature = 0;

	enum : uint8 {
		kEC_Enable = 0x80,
		kEC_InvertDisableA = 0x40,
		kEC_InvertDisableB = 0x20,
		kEC_Lock = 0x10,
		kEC_Mode = 0x07,
	};

	uint8	mEmuControl = 0;

	enum : uint8 {
		kEC2_DisableWinB = 0x02,
		kEC2_DisableWinA = 0x01
	};

	uint8	mEmuControl2 = 0;
	uint8	mEmuBankA = 0;
	uint8	mEmuBankB = 0;
	uint8	mEmuBank = 0;
	uint8	mEmuData = 0;

	vdrefptr<IATBlockDevice> mpBlockDevice;

	ATFirmwareManager *mpFirmwareManager = nullptr;

	IATDeviceCartridgePort *mpCartridgePort = nullptr;
	uint32	mCartId = 0;
	bool	mbLeftWindowActive = false;
	bool	mbLeftWindowEnabled = false;
	bool	mbRightWindowEnabled = false;
	bool	mbCCTLEnabled = false;

	ATFlashEmulator	mFlashCtrl;
	ATRTCMCP7951XEmulator mRTC;
	ATDeviceVirtualStorage mRTCStorage;

	uint8	mSendBuffer[520];
	uint8	mResponseBuffer[520];

	VDALIGN(4) uint8	mFlash[0x800000];
	VDALIGN(4) uint8	mRAM[0x800000];
};


#endif
