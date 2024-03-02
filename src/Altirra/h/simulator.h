//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2014 Avery Lee
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

#ifndef AT_SIMULATOR_H
#define AT_SIMULATOR_H

#ifdef _MSC_VER
	#pragma once
#endif

#ifndef VDNOINLINE
	#ifdef _MSC_VER
		#define VDNOINLINE __declspec(noinline)
	#else
		#define VDNOINLINE
	#endif
#endif

#include <at/atcore/scheduler.h>
#include "cpu.h"
#include "cpumemory.h"
#include "memorymanager.h"
#include "antic.h"
#include "gtia.h"
#include "pokey.h"
#include "vbxe.h"
#include "pia.h"
#include "simeventmanager.h"
#include "irqcontroller.h"
#include "address.h"

struct ATCartLoadContext;
class ATMemoryManager;
class ATMMUEmulator;
class ATPBIManager;
class ATSIOManager;
class ATUltimate1MBEmulator;
class ATHLEBasicLoader;
class ATHLEProgramLoader;
class ATHLEFPAccelerator;
class ATHLEFastBootHook;
class IATHLECIOHook;
class ATAudioSyncMixer;
class ATFirmwareManager;
class ATDeviceManager;
class IATDeviceSIOManager;
class IATDeviceCIOManager;
class IATPrinterOutput;
class IATDebugTarget;
class IATDevice;
struct ATCPUHookInitNode;
class ATDiskInterface;
class ATDiskEmulator;
class IVDRandomAccessStream;
class IATImage;

enum ATMediaWriteMode : uint8;

enum ATMemoryMode : uint32 {
	kATMemoryMode_48K,
	kATMemoryMode_52K,
	kATMemoryMode_64K,
	kATMemoryMode_128K,
	kATMemoryMode_320K,
	kATMemoryMode_576K,
	kATMemoryMode_1088K,
	kATMemoryMode_16K,
	kATMemoryMode_8K,
	kATMemoryMode_24K,
	kATMemoryMode_32K,
	kATMemoryMode_40K,
	kATMemoryMode_320K_Compy,
	kATMemoryMode_576K_Compy,
	kATMemoryMode_256K,
	kATMemoryModeCount
};

enum ATHardwareMode : uint32 {
	kATHardwareMode_800,
	kATHardwareMode_800XL,
	kATHardwareMode_5200,
	kATHardwareMode_XEGS,
	kATHardwareMode_1200XL,
	kATHardwareMode_130XE,
	kATHardwareModeCount
};

enum ATROMImage {
	kATROMImage_OSA,
	kATROMImage_OSB,
	kATROMImage_XL,
	kATROMImage_XEGS,
	kATROMImage_Other,
	kATROMImage_5200,
	kATROMImage_Basic,
	kATROMImage_Game,
	kATROMImage_KMKJZIDE,
	kATROMImage_KMKJZIDEV2,
	kATROMImage_KMKJZIDEV2_SDX,
	kATROMImage_SIDE_SDX,
	kATROMImage_1200XL,
	kATROMImage_MyIDEII,
	kATROMImage_Ultimate1MB,
	kATROMImage_SIDE2_SDX,
	kATROMImageCount
};

enum ATKernelMode {
	kATKernelMode_Default,
	kATKernelMode_800,
	kATKernelMode_XL,
	kATKernelMode_5200,
	kATKernelModeCount
};

enum ATStorageId {
	kATStorageId_None,
	kATStorageId_UnitMask	= 0x00FF,
	kATStorageId_Disk		= 0x0100,
	kATStorageId_Cartridge	= 0x0200,
	kATStorageId_Firmware	= 0x0300,
	kATStorageId_TypeMask	= 0xFF00,
	kATStorageId_All
};

enum ATVideoStandard : uint32 {
	kATVideoStandard_NTSC,
	kATVideoStandard_PAL,
	kATVideoStandard_SECAM,
	kATVideoStandard_PAL60,
	kATVideoStandard_NTSC50,
	kATVideoStandardCount
};

enum ATMemoryClearMode : uint8 {
	kATMemoryClearMode_Zero,
	kATMemoryClearMode_Random,
	kATMemoryClearMode_DRAM1,
	kATMemoryClearMode_DRAM2,
	kATMemoryClearMode_DRAM3,
	kATMemoryClearModeCount
};

class ATSaveStateReader;
class ATSaveStateWriter;
class ATCassetteEmulator;
class IATJoystickManager;
class ATCartridgeEmulator;
class ATPortController;
class ATInputManager;
class ATVBXEEmulator;
class ATCPUProfiler;
class ATCPUVerifier;
class ATCPUHeatMap;
class IATAudioOutput;
class ATLightPenPort;
class ATCheatEngine;
class ATMMUEmulator;
class ATAudioMonitor;
class IATVirtualScreenHandler;
struct ATCPUTimestampDecoder;
struct ATStateLoadContext;
struct ATImageLoadContext;
class IATBlobImage;
class IATCartridgeImage;

class ATSimulator final : ATCPUEmulatorCallbacks,
					ATAnticEmulatorConnections,
					IATPokeyEmulatorConnections,
					IATGTIAEmulatorConnections,
					IATVBXEEmulatorConnections
{
public:
	ATSimulator();
	~ATSimulator();

	void Init();
	void Shutdown();

	void SetJoystickManager(IATJoystickManager *jm);

	// Reload any currently used ROM images. Returns true if an actively used ROM image was
	// changed.
	bool LoadROMs();

	void PostInterruptingEvent(ATSimulatorEvent ev) {
		mPendingEvent = ev;
	}

	void NotifyEvent(ATSimulatorEvent ev);

	bool IsPowered() const {
		return mbPowered;
	}

	bool IsRunning() const {
		return mbRunning;
	}

	bool IsPaused() const {
		return mbPaused;
	}

	ATSimulatorEventManager *GetEventManager() { return mpSimEventManager; }
	ATCPUEmulator& GetCPU() { return mCPU; }
	ATCPUEmulatorMemory& GetCPUMemory() { return *mpMemMan; }
	IATDebugTarget *GetDebugTarget() { return mpDebugTarget; }
	ATMemoryManager *GetMemoryManager() { return mpMemMan; }
	ATMMUEmulator *GetMMU() { return mpMMU; }
	ATAnticEmulator& GetAntic() { return mAntic; }
	ATGTIAEmulator& GetGTIA() { return mGTIA; }
	ATPokeyEmulator& GetPokey() { return mPokey; }
	ATPIAEmulator& GetPIA() { return mPIA; }
	ATDiskInterface& GetDiskInterface(int index);
	ATDiskEmulator& GetDiskDrive(int index) { return *mpDiskDrives[index]; }
	ATCassetteEmulator& GetCassette() { return *mpCassette; }
	ATInputManager *GetInputManager() { return mpInputManager; }
	IATJoystickManager *GetJoystickManager() { return mpJoysticks; }
	ATPBIManager& GetPBIManager() const { return *mpPBIManager; }
	ATVBXEEmulator *GetVBXE() { return mpVBXE; }
	ATCartridgeEmulator *GetCartridge(uint32 unit) { return mpCartridge[unit]; }
	IATUIRenderer *GetUIRenderer() { return mpUIRenderer; }
	IATAudioOutput *GetAudioOutput() { return mpAudioOutput; }
	ATAudioSyncMixer *GetAudioSyncMixer() { return mpAudioSyncMixer; }
	ATLightPenPort *GetLightPenPort() { return mpLightPen; }
	ATUltimate1MBEmulator *GetUltimate1MB() { return mpUltimate1MB; }
	IATVirtualScreenHandler *GetVirtualScreenHandler() { return mpVirtualScreenHandler; }
	ATIRQController *GetIRQController() { return &mIRQController; }
	ATFirmwareManager *GetFirmwareManager() { return mpFirmwareManager; }
	ATDeviceManager *GetDeviceManager() { return mpDeviceManager; }
	ATScheduler *GetScheduler() { return &mScheduler; }
	ATScheduler *GetSlowScheduler() { return &mSlowScheduler; }
	IATDeviceSIOManager *GetDeviceSIOManager();
	IATDeviceCIOManager *GetDeviceCIOManager();
	ATHLEProgramLoader *GetProgramLoader() { return mpHLEProgramLoader; }

	IATPrinterOutput *GetPrinterOutput() { return mpPrinterOutput; }
	void SetPrinterOutput(IATPrinterOutput *p);

	bool IsTurboModeEnabled() const { return mbTurbo; }
	bool IsFrameSkipEnabled() const { return mbFrameSkip; }
	ATVideoStandard GetVideoStandard() const { return mVideoStandard; }
	bool IsVideo50Hz() const { return mVideoStandard != kATVideoStandard_NTSC && mVideoStandard != kATVideoStandard_PAL60; }

	ATMemoryMode GetMemoryMode() const { return mMemoryMode; }
	ATKernelMode GetKernelMode() const { return mKernelMode; }
	uint64 GetKernelId() const { return mKernelId; }
	uint64 GetActualKernelId() const { return mActualKernelId; }
	uint64 GetBasicId() const { return mBasicId; }
	uint64 GetActualBasicId() const { return mActualBasicId; }
	ATHardwareMode GetHardwareMode() const { return mHardwareMode; }
	bool IsDiskSIOPatchEnabled() const;
	bool IsDiskSIOOverrideDetectEnabled() const { return mbDiskSIOOverrideDetectEnabled; }
	bool IsDiskAccurateTimingEnabled() const;
	bool IsDiskSectorCounterEnabled() const { return mbDiskSectorCounterEnabled; }
	bool IsCassetteSIOPatchEnabled() const { return mbCassetteSIOPatchEnabled; }
	bool IsCassetteAutoBootEnabled() const { return mbCassetteAutoBootEnabled; }
	bool IsCassetteAutoRewindEnabled() const;
	bool IsCassetteRandomizedStartEnabled() const { return mbCassetteRandomizedStartEnabled; }
	bool IsFPPatchEnabled() const { return mbFPPatchEnabled; }
	bool IsBASICEnabled() const { return mbBASICEnabled; }
	bool IsROMAutoReloadEnabled() const { return mbROMAutoReloadEnabled; }
	bool IsAutoLoadKernelSymbolsEnabled() const { return mbAutoLoadKernelSymbols; }
	bool IsDualPokeysEnabled() const { return mbDualPokeys; }
	bool IsVBXESharedMemoryEnabled() const { return mbVBXESharedMemory; }
	bool IsVBXEAltPageEnabled() const { return mbVBXEUseD7xx; }

	bool GetDiskBurstTransfersEnabled() const;
	void SetDiskBurstTransfersEnabled(bool enabled);
	bool GetDeviceCIOBurstTransfersEnabled() const;
	void SetDeviceCIOBurstTransfersEnabled(bool enabled);
	bool GetDeviceSIOBurstTransfersEnabled() const;
	void SetDeviceSIOBurstTransfersEnabled(bool enabled);

	bool HasCIODevice(char c) const;
	bool GetCIOPatchEnabled(char c) const;
	void SetCIOPatchEnabled(char c, bool enabled) const;
	
	bool IsSIOPatchEnabled() const;
	void SetSIOPatchEnabled(bool enable);

	bool IsPBIPatchEnabled() const;
	void SetPBIPatchEnabled(bool enable);

	bool GetDeviceSIOPatchEnabled() const;
	void SetDeviceSIOPatchEnabled(bool enable) const;

	uint8	GetBankRegister() const;
	int		GetCartBank(uint32 unit) const;

	const uint8 *GetRawMemory() const { return mMemory; }
	uint32 RandomizeRawMemory(uint16 start, uint32 count, uint32 seed);

	ATCPUProfiler *GetProfiler() const { return mpProfiler; }
	bool IsProfilingEnabled() const { return mpProfiler != NULL; }
	void SetProfilingEnabled(bool enabled);

	ATCPUVerifier *GetVerifier() const { return mpVerifier; }
	bool IsVerifierEnabled() const { return mpVerifier != NULL; }
	void SetVerifierEnabled(bool enabled);

	ATCPUHeatMap *GetHeatMap() const { return mpHeatMap; }
	bool IsHeatMapEnabled() const { return mpHeatMap != NULL; }
	void SetHeatMapEnabled(bool enabled);

	ATMemoryClearMode GetMemoryClearMode() const { return mMemoryClearMode; }
	void SetMemoryClearMode(ATMemoryClearMode mode) { mMemoryClearMode = mode; }

	bool IsFloatingIoBusEnabled() const { return mbFloatingIoBus; }
	void SetFloatingIoBusEnabled(bool enabled);

	bool IsRandomFillEXEEnabled() const { return mbRandomFillEXEEnabled; }
	void SetRandomFillEXEEnabled(bool enabled);

	void SetBreakOnFrameEnd(bool enabled) { mbBreakOnFrameEnd = enabled; }
	void SetBreakOnScanline(int scanline) { mBreakOnScanline = scanline; }
	void SetTurboModeEnabled(bool turbo);
	void SetFrameSkipEnabled(bool skip);
	void SetVideoStandard(ATVideoStandard vs);
	void SetMemoryMode(ATMemoryMode mode);
	void SetKernel(uint64 id);
	void SetBasic(uint64 id);
	void SetHardwareMode(ATHardwareMode mode);

	uint8 GetAxlonMemoryMode() { return mAxlonMemoryBits; }
	void SetAxlonMemoryMode(uint8 bits);

	bool GetAxlonAliasingEnabled() const { return mbAxlonAliasingEnabled; }
	void SetAxlonAliasingEnabled(bool enabled);

	sint32 GetHighMemoryBanks() { return mHighMemoryBanks; }
	void SetHighMemoryBanks(sint32 bits);

	void SetDiskSIOPatchEnabled(bool enable);
	void SetDiskSIOOverrideDetectEnabled(bool enable);
	void SetDiskAccurateTimingEnabled(bool enable);
	void SetDiskSectorCounterEnabled(bool enable);
	void SetCassetteSIOPatchEnabled(bool enable);
	void SetCassetteAutoBootEnabled(bool enable);
	void SetCassetteAutoRewindEnabled(bool enable);
	void SetCassetteRandomizedStartEnabled(bool enable);
	void SetFPPatchEnabled(bool enable);
	void SetBASICEnabled(bool enable);
	void SetROMAutoReloadEnabled(bool enable);
	void SetAutoLoadKernelSymbolsEnabled(bool enable);
	void SetDualPokeysEnabled(bool enable);
	void SetVBXEEnabled(bool enable);
	void SetVBXESharedMemoryEnabled(bool enable);
	void SetVBXEAltPageEnabled(bool enable);

	bool IsFastBootEnabled() const { return mbFastBoot; }
	void SetFastBootEnabled(bool enable);

	bool IsKeyboardPresent() const { return mbKeyboardPresent; }
	void SetKeyboardPresent(bool enable);

	bool IsForcedSelfTest() const { return mbForcedSelfTest; }
	void SetForcedSelfTest(bool enable);

	bool GetCartridgeSwitch() const { return mbCartridgeSwitch; }
	void SetCartridgeSwitch(bool enable);

	bool IsMapRAMEnabled() const { return mbMapRAM; }
	void SetMapRAMEnabled(bool enable);

	bool IsUltimate1MBEnabled() const { return mpUltimate1MB != NULL; }
	void SetUltimate1MBEnabled(bool enable);

	ATCheatEngine *GetCheatEngine() { return mpCheatEngine; }
	void SetCheatEngineEnabled(bool enable);

	bool IsAudioMonitorEnabled() const { return mpAudioMonitors[0] != NULL; }
	void SetAudioMonitorEnabled(bool enable);

	bool IsVirtualScreenEnabled() const { return mpVirtualScreenHandler != NULL; }
	void SetVirtualScreenEnabled(bool enable);

	bool GetShadowROMEnabled() const { return mbShadowROM; }
	void SetShadowROMEnabled(bool enabled) { mbShadowROM = enabled; }
	bool GetShadowCartridgeEnabled() const { return mbShadowCartridge; }
	void SetShadowCartridgeEnabled(bool enabled);

	int GetPendingHeldKey() const { return mPendingHeldKey; }
	void ClearPendingHeldKey();
	void SetPendingHeldKey(uint8 key);

	uint8 GetPendingHeldSwitches() const { return mPendingHeldSwitches; }
	void SetPendingHeldSwitches(uint8 switches);

	int GetPowerOnDelay() const;
	void SetPowerOnDelay(int tenthsOfSeconds);

	void ColdReset();
	void WarmReset();
	void Resume();
	void ResumeSingleCycle();
	void Suspend();
	void Pause();

	void FlushDeferredEvents();

	void GetDirtyStorage(vdfastvector<ATStorageId>& ids, ATStorageId mediaId = kATStorageId_None) const;
	bool IsStorageDirty(ATStorageId mediaId) const;
	bool IsStoragePresent(ATStorageId mediaId) const;
	void SaveStorage(ATStorageId storageId, const wchar_t *path);

	void SwapDrives(int src, int dst);
	void RotateDrives(int count, int delta);

	void UnloadAll();
	bool Load(const wchar_t *path, ATMediaWriteMode writeMode, ATImageLoadContext *loadCtx);
	bool Load(const wchar_t *origPath, const wchar_t *imagePath, IVDRandomAccessStream& stream, ATMediaWriteMode writeMode, ATImageLoadContext *loadCtx);
	bool Load(const wchar_t *origPath, const wchar_t *imagePath, IATImage *image, ATMediaWriteMode writeMode, ATImageLoadContext *loadCtx);

	bool IsCartridgeAttached(uint32 index) const;

	void UnloadCartridge(uint32 index);
	bool LoadCartridge(uint32 index, const wchar_t *s, ATCartLoadContext *loadCtx);
	bool LoadCartridge(uint32 index, const wchar_t *origPath, const wchar_t *imagePath, IVDRandomAccessStream&, ATCartLoadContext *loadCtx);
	void LoadCartridge(uint32 index, const wchar_t *origPath, IATCartridgeImage *image);
	void LoadCartridge5200Default();
	void LoadNewCartridge(int mode);
	void LoadCartridgeBASIC();

	enum AdvanceResult {
		kAdvanceResult_Stopped,
		kAdvanceResult_Running,
		kAdvanceResult_WaitingForFrame
	};

	AdvanceResult AdvanceUntilInstructionBoundary();
	AdvanceResult Advance(bool dropFrame);

	uint32 GetCpuCycleCounter() const { return const_cast<ATSimulator *>(this)->CPUGetUnhaltedCycle(); }
	uint32 GetTimestamp() const;
	ATCPUTimestampDecoder GetTimestampDecoder() const;

	uint8 DebugReadByte(uint16 address) const;
	uint16 DebugReadWord(uint16 address);
	uint32 DebugRead24(uint16 address);

	uint8 DebugExtReadByte(uint32 address);

	uint8 DebugGlobalReadByte(uint32 address);
	uint16 DebugGlobalReadWord(uint32 address);
	uint32 DebugGlobalRead24(uint32 address);
	void DebugGlobalWriteByte(uint32 address, uint8 value);

	uint8 DebugAnticReadByte(uint16 address);

	bool IsKernelROMLocation(uint16 address) const;

	uint32 ComputeKernelChecksum() const;

	bool LoadState(ATSaveStateReader& reader, ATStateLoadContext *context);
	void SaveState(ATSaveStateWriter& writer);

	void UpdateXLCartridgeLine();
	void UpdateKeyboardPresentLine();
	void UpdateForcedSelfTestLine();
	void UpdateCartridgeSwitch();
	void UpdateBanking(uint8 currbank);
	void UpdateVBXEPage();
	void UpdateSoundBoardPage();

private:
	void InternalColdReset(bool computerOnly);

	void LoadProgram(const wchar_t *path, IATBlobImage *image, bool basic);

	void LoadStateMachineDesc(ATSaveStateReader& reader);
	void LoadStateRefs(ATSaveStateReader& reader, ATStateLoadContext& ctx);
	void LoadStateSection(ATSaveStateReader& reader, int section);
	void LoadStateMemoryArch(ATSaveStateReader& reader);

	bool UpdateKernel(bool trackChanges, bool forceReload = false);
	bool ReloadU1MBFirmware();
	void InitMemoryMap();
	void ShutdownMemoryMap();

	uint32 CPUGetCycle() override;
	uint32 CPUGetUnhaltedCycle() override;
	void CPUGetHistoryTimes(ATCPUHistoryEntry * VDRESTRICT he) const override;

	uint8 AnticReadByte(uint32 address) override;
	void AnticAssertNMI_VBI() override;
	void AnticAssertNMI_DLI() override;
	void AnticAssertNMI_RES() override;
	void AnticEndFrame() override;
	void AnticEndScanline() override;
	bool AnticIsNextCPUCycleWrite() override;
	uint8 AnticGetCPUHeldCycleValue() override;
	void AnticForceNextCPUCycleSlow() override;
	void AnticOnVBlank() override;

	uint32 GTIAGetXClock() override;
	uint32 GTIAGetTimestamp() const override;
	void GTIASetSpeaker(bool newState) override;
	void GTIASelectController(uint8 index, bool potsEnabled) override;
	void GTIARequestAnticSync(int offset) override;
	uint32 GTIAGetLineEdgeTimingId(uint32 offset) const override;
	void PokeyAssertIRQ(bool cpuBased) override;
	void PokeyNegateIRQ(bool cpuBased) override;
	void PokeyBreak() override;
	bool PokeyIsInInterrupt() const override;
	bool PokeyIsKeyPushOK(uint8 c) const override;
	uint32 PokeyGetTimestamp() const override;

	void VBXEAssertIRQ() override;
	void VBXENegateIRQ() override;

	void ReinitHookPage();
	void SetupPendingHeldButtons();
	void SetupAutoHeldButtons();

	void InitDevice(IATDevice& dev);

	void ResetMemoryBuffer(void *dst, size_t len, uint32 seed);

	bool mbRunning;
	bool mbRunSingleCycle = false;
	bool mbPowered;
	bool mbPaused;
	bool mbBreak;
	bool mbBreakOnFrameEnd;
	bool mbTurbo;
	bool mbFrameSkip;
	ATVideoStandard mVideoStandard;
	ATMemoryClearMode mMemoryClearMode;
	bool mbFloatingIoBus;
	bool mbRandomFillEXEEnabled;
	bool mbDiskSIOOverrideDetectEnabled;
	bool mbDiskSectorCounterEnabled;
	bool mbCassetteSIOPatchEnabled;
	bool mbCassetteAutoBootEnabled;
	bool mbCassetteRandomizedStartEnabled;
	bool mbFPPatchEnabled;
	bool mbBASICEnabled;
	bool mbROMAutoReloadEnabled;
	bool mbAutoLoadKernelSymbols;
	bool mbDualPokeys;
	bool mbVBXESharedMemory;
	bool mbVBXEUseD7xx;
	bool mbFastBoot;
	bool mbKeyboardPresent;
	bool mbForcedSelfTest;
	bool mbCartridgeSwitch;
	bool mbMapRAM;
	bool mbShadowROM;
	bool mbShadowCartridge;
	int mBreakOnScanline;

	int	mPowerOnDelay = -1;
	uint32	mPoweronDelayCounter = 0;
	int		mStartupDelay;
	int		mStartupDelay2;
	bool	mbStartupHeldKey = false;
	int		mPendingHeldKey = -1;
	uint8	mPendingHeldSwitches = 0;

	ATMemoryMode	mMemoryMode;
	ATKernelMode	mKernelMode;
	uint64			mKernelId;
	uint64			mActualKernelId;
	uint32			mActualKernelFlags;
	uint64			mBasicId;
	uint64			mActualBasicId;
	ATHardwareMode	mHardwareMode;
	ATSimulatorEvent	mPendingEvent;

	class PrivateData;
	friend class PrivateData;

	PrivateData		*mpPrivateData;
	ATMemoryManager	*mpMemMan;
	ATMMUEmulator	*mpMMU;
	ATPBIManager	*mpPBIManager;
	ATSIOManager	*mpSIOManager;
	ATCPUHookManager *mpCPUHookManager;
	ATCPUHookInitNode *mpCPUHookInitHook = nullptr;
	ATSimulatorEventManager	*mpSimEventManager;

	ATCPUEmulator	mCPU;
	ATAnticEmulator	mAntic;
	ATGTIAEmulator	mGTIA;
	ATPokeyEmulator	mPokey;
	ATPokeyEmulator	mPokey2;
	IATAudioOutput	*mpAudioOutput;
	ATAudioSyncMixer *mpAudioSyncMixer;
	ATPokeyTables	*mpPokeyTables;
	ATScheduler		mScheduler;
	ATScheduler		mSlowScheduler;
	ATDiskEmulator	*mpDiskDrives[15];
	ATAudioMonitor	*mpAudioMonitors[2];
	ATCassetteEmulator	*mpCassette;
	IATJoystickManager	*mpJoysticks;
	ATCartridgeEmulator	*mpCartridge[2];
	ATInputManager	*mpInputManager;
	ATPortController *mpPortAController;
	ATPortController *mpPortBController;
	ATLightPenPort *mpLightPen;
	IATPrinterOutput *mpPrinterOutput;
	ATVBXEEmulator *mpVBXE;
	void *mpVBXEMemory;
	ATCheatEngine *mpCheatEngine;
	IATUIRenderer *mpUIRenderer;
	ATUltimate1MBEmulator *mpUltimate1MB;
	IATVirtualScreenHandler *mpVirtualScreenHandler;

	ATHLEBasicLoader *mpHLEBasicLoader;
	ATHLEProgramLoader *mpHLEProgramLoader;
	ATHLEFPAccelerator *mpHLEFPAccelerator;
	ATHLEFastBootHook *mpHLEFastBootHook;
	IATHLECIOHook *mpHLECIOHook;

	ATPIAEmulator mPIA;
	ATIRQController	mIRQController;

	uint8	mHookPage;

	uint8	mAxlonMemoryBits;
	bool	mbAxlonAliasingEnabled;
	sint32	mHighMemoryBanks;

	const uint8 *mpKernelUpperROM;
	const uint8 *mpKernelLowerROM;
	const uint8 *mpKernelSelfTestROM;
	const uint8 *mpKernel5200ROM;
	uint32	mKernelSymbolsModuleIds[2];

	uint32		mCartModuleIds[3];

	ATCPUProfiler	*mpProfiler;
	ATCPUVerifier	*mpVerifier;
	ATCPUHeatMap	*mpHeatMap;
	IATDebugTarget	*mpDebugTarget;

	ATMemoryLayer	*mpMemLayerLoRAM;
	ATMemoryLayer	*mpMemLayerHiRAM;
	ATMemoryLayer	*mpMemLayerExtendedRAM;
	ATMemoryLayer	*mpMemLayerLowerKernelROM;
	ATMemoryLayer	*mpMemLayerUpperKernelROM;
	ATMemoryLayer	*mpMemLayerBASICROM;
	ATMemoryLayer	*mpMemLayerSelfTestROM;
	ATMemoryLayer	*mpMemLayerGameROM;
	ATMemoryLayer	*mpMemLayerHiddenRAM;
	ATMemoryLayer	*mpMemLayerANTIC;
	ATMemoryLayer	*mpMemLayerGTIA;
	ATMemoryLayer	*mpMemLayerPOKEY;
	ATMemoryLayer	*mpMemLayerPIA;
	ATMemoryLayer	*mpMemLayerIoBusFloat;

	ATFirmwareManager	*mpFirmwareManager;
	ATDeviceManager		*mpDeviceManager;

	class DeviceChangeCallback;
	DeviceChangeCallback *mpDeviceChangeCallback;

	vdblock<uint8> mAxlonMemory;

	////////////////////////////////////
	VDALIGN(4)	uint8	mKernelROM[0x4000];
	VDALIGN(4)	uint8	mBASICROM[0x2000];
	VDALIGN(4)	uint8	mGameROM[0x2000];
	VDALIGN(4)	uint8	mMemory[0x440000];
};

#endif
