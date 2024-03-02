//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008 Avery Lee
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

#include "cpu.h"
#include "cpumemory.h"
#include "memorymanager.h"
#include "antic.h"
#include "gtia.h"
#include "pokey.h"
#include "disk.h"
#include "scheduler.h"
#include "vbxe.h"
#include "pia.h"
#include "simeventmanager.h"

struct ATCartLoadContext;
class ATMemoryManager;
class ATMMUEmulator;
class ATPBIManager;
class ATSIOManager;
class ATKMKJZIDE;
class ATSIDEEmulator;
class ATHLEBasicLoader;
class ATHLEProgramLoader;
class ATHLEFPAccelerator;
class ATHLEFastBootHook;
class IATHLECIOHook;
class ATAudioSyncMixer;

enum ATMemoryMode {
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
	kATMemoryModeCount
};

enum ATHardwareMode {
	kATHardwareMode_800,
	kATHardwareMode_800XL,
	kATHardwareMode_5200,
	kATHardwareMode_XEGS,
	kATHardwareMode_1200XL,
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
	kATROMImageCount
};

enum ATKernelMode {
	kATKernelMode_Default,
	kATKernelMode_OSB,
	kATKernelMode_XL,
	kATKernelMode_HLE,
	kATKernelMode_LLE,
	kATKernelMode_OSA,
	kATKernelMode_Other,
	kATKernelMode_5200,
	kATKernelMode_5200_LLE,
	kATKernelMode_XEGS,
	kATKernelMode_1200XL,
	kATKernelModeCount
};

enum ATIDEHardwareMode {
	kATIDEHardwareMode_MyIDE_D1xx,
	kATIDEHardwareMode_MyIDE_D5xx,
	kATIDEHardwareMode_KMKJZ_V1,
	kATIDEHardwareMode_KMKJZ_V2,
	kATIDEHardwareMode_SIDE,
	kATIDEHardwareModeCount
};

enum {
	kATAddressSpace_CPU		= 0x00000000,
	kATAddressSpace_ANTIC	= 0x10000000,
	kATAddressSpace_VBXE	= 0x20000000,
	kATAddressSpace_PORTB	= 0x30000000,
	kATAddressSpace_RAM		= 0x40000000,
	kATAddressOffsetMask	= 0x00FFFFFF,
	kATAddressSpaceMask		= 0xF0000000
};

enum ATStorageId {
	kATStorageId_None,
	kATStorageId_UnitMask	= 0x00FF,
	kATStorageId_Disk		= 0x0100,
	kATStorageId_Cartridge	= 0x0200,
	kATStorageId_IDEFirmware= 0x0300,
	kATStorageId_TypeMask	= 0xFF00,
	kATStorageId_All
};

enum ATVideoStandard {
	kATVideoStandard_NTSC,
	kATVideoStandard_PAL,
	kATVideoStandard_SECAM,
	kATVideoStandardCount
};

class ATSaveStateReader;
class ATSaveStateWriter;
class ATCassetteEmulator;
class IATHLEKernel;
class IATJoystickManager;
class IATHostDeviceEmulator;
class ATCartridgeEmulator;
class ATPortController;
class ATInputManager;
class IATPrinterEmulator;
class ATVBXEEmulator;
class ATSoundBoardEmulator;
class ATSlightSIDEmulator;
class ATCovoxEmulator;
class ATRTime8Emulator;
class ATCPUProfiler;
class ATCPUVerifier;
class ATCPUHeatMap;
class ATIDEEmulator;
class ATMyIDEEmulator;
class IATAudioOutput;
class IATRS232Emulator;
class ATLightPenPort;
class ATCheatEngine;
class ATMMUEmulator;
class ATAudioMonitor;
class IATPCLinkDevice;
class IATPBIDevice;
class IATVirtualScreenHandler;

enum ATLoadType {
	kATLoadType_Other,
	kATLoadType_Cartridge,
	kATLoadType_Disk,
	kATLoadType_Tape,
	kATLoadType_Program,
	kATLoadType_BasicProgram
};

struct ATLoadContext {
	ATLoadType mLoadType;
	ATCartLoadContext *mpCartLoadContext;
	int mLoadIndex;

	ATLoadContext()
		: mLoadType(kATLoadType_Other)
		, mpCartLoadContext(NULL)
		, mLoadIndex(-1) {}
};

class ATSimulator : ATCPUEmulatorCallbacks,
					ATAnticEmulatorConnections,
					ATPIAEmulator,
					IATPokeyEmulatorConnections,
					IATGTIAEmulatorConnections,
					IATDiskActivity,
					IATVBXEEmulatorConnections
{
public:
	ATSimulator();
	~ATSimulator();

	void Init(void *hwnd);
	void Shutdown();

	void LoadROMs();

	void AddCallback(IATSimulatorCallback *cb);
	void RemoveCallback(IATSimulatorCallback *cb);

	void PostInterruptingEvent(ATSimulatorEvent ev) {
		mPendingEvent = ev;
	}

	void NotifyEvent(ATSimulatorEvent ev);

	bool IsRunning() const {
		return mbRunning;
	}

	ATCPUEmulator& GetCPU() { return mCPU; }
	ATCPUEmulatorMemory& GetCPUMemory() { return *mpMemMan; }
	ATMemoryManager *GetMemoryManager() { return mpMemMan; }
	ATMMUEmulator *GetMMU() { return mpMMU; }
	ATAnticEmulator& GetAntic() { return mAntic; }
	ATGTIAEmulator& GetGTIA() { return mGTIA; }
	ATPokeyEmulator& GetPokey() { return mPokey; }
	ATDiskEmulator& GetDiskDrive(int index) { return mDiskDrives[index]; }
	ATCassetteEmulator& GetCassette() { return *mpCassette; }
	IATHostDeviceEmulator *GetHostDevice() { return mpHostDevice; }
	ATInputManager *GetInputManager() { return mpInputManager; }
	IATJoystickManager& GetJoystickManager() { return *mpJoysticks; }
	IATPrinterEmulator *GetPrinter() { return mpPrinter; }
	ATVBXEEmulator *GetVBXE() { return mpVBXE; }
	ATSoundBoardEmulator *GetSoundBoard() { return mpSoundBoard; }
	ATSlightSIDEmulator *GetSlightSID() { return mpSlightSID; }
	ATCovoxEmulator *GetCovox() { return mpCovox; }
	ATCartridgeEmulator *GetCartridge(uint32 unit) { return mpCartridge[unit]; }
	IATUIRenderer *GetUIRenderer() { return mpUIRenderer; }
	ATIDEEmulator *GetIDEEmulator() { return mpIDE; }
	IATAudioOutput *GetAudioOutput() { return mpAudioOutput; }
	IATRS232Emulator *GetRS232() { return mpRS232; }
	ATLightPenPort *GetLightPenPort() { return mpLightPen; }
	IATPCLinkDevice *GetPCLink() { return mpPCLink; }
	ATKMKJZIDE *GetKMKJZIDE() { return mpKMKJZIDE; }
	ATSIDEEmulator *GetSIDE() { return mpSIDE; }
	IATVirtualScreenHandler *GetVirtualScreenHandler() { return mpVirtualScreenHandler; }

	bool IsTurboModeEnabled() const { return mbTurbo; }
	bool IsFrameSkipEnabled() const { return mbFrameSkip; }
	ATVideoStandard GetVideoStandard() const { return mVideoStandard; }
	ATMemoryMode GetMemoryMode() const { return mMemoryMode; }
	ATKernelMode GetKernelMode() const { return mKernelMode; }
	ATKernelMode GetActualKernelMode() const { return mActualKernelMode; }
	ATHardwareMode GetHardwareMode() const { return mHardwareMode; }
	bool IsDiskSIOPatchEnabled() const { return mbDiskSIOPatchEnabled; }
	bool IsDiskSIOOverrideDetectEnabled() const { return mbDiskSIOOverrideDetectEnabled; }
	bool IsDiskAccurateTimingEnabled() const;
	bool IsDiskSectorCounterEnabled() const { return mbDiskSectorCounterEnabled; }
	bool IsCassetteSIOPatchEnabled() const { return mbCassetteSIOPatchEnabled; }
	bool IsCassetteAutoBootEnabled() const { return mbCassetteAutoBootEnabled; }
	bool IsFPPatchEnabled() const { return mbFPPatchEnabled; }
	bool IsBASICEnabled() const { return mbBASICEnabled; }
	bool IsROMAutoReloadEnabled() const { return mbROMAutoReloadEnabled; }
	bool IsAutoLoadKernelSymbolsEnabled() const { return mbAutoLoadKernelSymbols; }
	bool IsDualPokeysEnabled() const { return mbDualPokeys; }
	bool IsVBXESharedMemoryEnabled() const { return mbVBXESharedMemory; }
	bool IsVBXEAltPageEnabled() const { return mbVBXEUseD7xx; }
	bool IsRTime8Enabled() const { return mpRTime8 != NULL; }
	ATIDEHardwareMode GetIDEHardwareMode() const { return mIDEHardwareMode; }

	uint8	GetPortBOutputs() const { return mPORTBOUT; }
	uint8	GetPortBDirections() const { return mPORTBDDR; }
	uint8	GetPortBControl() const { return mPORTBCTL; }

	uint8	GetBankRegister() const { return mPORTBOUT | ~mPORTBDDR; }
	int		GetCartBank(uint32 unit) const;

	const uint8 *GetRawMemory() const { return mMemory; }

	ATCPUProfiler *GetProfiler() const { return mpProfiler; }
	bool IsProfilingEnabled() const { return mpProfiler != NULL; }
	void SetProfilingEnabled(bool enabled);

	ATCPUVerifier *GetVerifier() const { return mpVerifier; }
	bool IsVerifierEnabled() const { return mpVerifier != NULL; }
	void SetVerifierEnabled(bool enabled);

	ATCPUHeatMap *GetHeatMap() const { return mpHeatMap; }
	bool IsHeatMapEnabled() const { return mpHeatMap != NULL; }
	void SetHeatMapEnabled(bool enabled);

	bool IsRandomFillEnabled() const { return mbRandomFillEnabled; }
	void SetRandomFillEnabled(bool enabled) { mbRandomFillEnabled = enabled; }

	void SetBreakOnFrameEnd(bool enabled) { mbBreakOnFrameEnd = enabled; }
	void SetBreakOnScanline(int scanline) { mBreakOnScanline = scanline; }
	void SetTurboModeEnabled(bool turbo);
	void SetFrameSkipEnabled(bool skip);
	void SetVideoStandard(ATVideoStandard vs);
	void SetMemoryMode(ATMemoryMode mode);
	void SetKernelMode(ATKernelMode mode);
	void SetHardwareMode(ATHardwareMode mode);
	void SetDiskSIOPatchEnabled(bool enable);
	void SetDiskSIOOverrideDetectEnabled(bool enable);
	void SetDiskAccurateTimingEnabled(bool enable);
	void SetDiskSectorCounterEnabled(bool enable);
	void SetCassetteSIOPatchEnabled(bool enable);
	void SetCassetteAutoBootEnabled(bool enable);
	void SetFPPatchEnabled(bool enable);
	void SetBASICEnabled(bool enable);
	void SetROMAutoReloadEnabled(bool enable);
	void SetAutoLoadKernelSymbolsEnabled(bool enable);
	void SetDualPokeysEnabled(bool enable);
	void SetVBXEEnabled(bool enable);
	void SetVBXESharedMemoryEnabled(bool enable);
	void SetVBXEAltPageEnabled(bool enable);
	void SetRTime8Enabled(bool enable);
	void SetPCLinkEnabled(bool enable);

	void SetSoundBoardEnabled(bool enable);
	void SetSoundBoardMemBase(uint32 membase);
	uint32 GetSoundBoardMemBase() const;

	void SetSlightSIDEnabled(bool enable);
	void SetCovoxEnabled(bool enable);

	bool IsRS232Enabled() const;
	void SetRS232Enabled(bool enable);

	bool IsPrinterEnabled() const;
	void SetPrinterEnabled(bool enable);

	bool IsFastBootEnabled() const { return mbFastBoot; }
	void SetFastBootEnabled(bool enable);

	bool IsKeyboardPresent() const { return mbKeyboardPresent; }
	void SetKeyboardPresent(bool enable);

	bool IsForcedSelfTest() const { return mbForcedSelfTest; }
	void SetForcedSelfTest(bool enable);

	bool IsMapRAMEnabled() const { return mbMapRAM; }
	void SetMapRAMEnabled(bool enable);

	ATCheatEngine *GetCheatEngine() { return mpCheatEngine; }
	void SetCheatEngineEnabled(bool enable);

	bool IsAudioMonitorEnabled() const { return mpAudioMonitor != NULL; }
	void SetAudioMonitorEnabled(bool enable);

	bool IsVirtualScreenEnabled() const { return mpVirtualScreenHandler != NULL; }
	void SetVirtualScreenEnabled(bool enable);

	bool IsKernelAvailable(ATKernelMode mode) const;

	void ColdReset();
	void WarmReset();
	void Resume();
	void Suspend();

	void GetROMImagePath(ATROMImage image, VDStringW& s) const;
	void SetROMImagePath(ATROMImage image, const wchar_t *s);

	void GetDirtyStorage(vdfastvector<ATStorageId>& ids) const;
	bool IsStorageDirty(ATStorageId mediaId) const;

	void UnloadAll();
	bool Load(const wchar_t *path, bool vrw, bool rw, ATLoadContext *loadCtx);
	bool Load(const wchar_t *origPath, const wchar_t *imagePath, IVDRandomAccessStream& stream, bool vrw, bool rw, ATLoadContext *loadCtx);
	void LoadProgram(const wchar_t *symbolHintPath, IVDRandomAccessStream& stream, bool basic);

	bool IsCartridgeAttached(uint32 index) const;

	void UnloadCartridge(uint32 index);
	bool LoadCartridge(uint32 index, const wchar_t *s, ATCartLoadContext *loadCtx);
	bool LoadCartridge(uint32 index, const wchar_t *origPath, const wchar_t *imagePath, IVDRandomAccessStream&, ATCartLoadContext *loadCtx);
	void LoadCartridgeSC3D();
	void LoadCartridge5200Default();
	void LoadCartridgeFlash1Mb(bool altbank);
	void LoadCartridgeFlash8Mb(bool newer);
	void LoadCartridgeFlashSIC();
	void LoadCartridgeBASIC();

	void LoadIDE(ATIDEHardwareMode mode, bool write, bool fast, uint32 cylinders, uint32 heads, uint32 sectors, const wchar_t *path);
	void UnloadIDE();

	enum AdvanceResult {
		kAdvanceResult_Stopped,
		kAdvanceResult_Running,
		kAdvanceResult_WaitingForFrame
	};

	AdvanceResult AdvanceUntilInstructionBoundary();
	AdvanceResult Advance(bool dropFrame);

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

	void PIAAssertProceed();
	void PIAAssertInterrupt();
	void DumpPIAState();

	void LoadState(ATSaveStateReader& reader);
	void SaveState(ATSaveStateWriter& writer);

	void UpdateXLCartridgeLine();
	void UpdateKeyboardPresentLine();
	void UpdateForcedSelfTestLine();

private:
	void UpdateKernel();
	void ReloadIDEFirmware();
	void InitMemoryMap();
	void ShutdownMemoryMap();
	uint8 ReadPortA() const;
	ATSimulatorEvent VDNOINLINE AdvancePhantomDMA(int x);

	uint8 PIADebugReadByte(uint8 addr) const;
	uint8 PIAReadByte(uint8 addr);
	void PIAWriteByte(uint8 addr, uint8 value);
	static sint32 RT8ReadByte(void *thisptr0, uint32 addr);
	static bool RT8WriteByte(void *thisptr0, uint32 addr, uint8 value);

	uint32 CPUGetCycle();
	uint32 CPUGetUnhaltedCycle();
	uint32 CPUGetTimestamp();
	uint8 CPURecordBusActivity(uint8 value);
	uint8 AnticReadByte(uint32 address);
	void AnticAssertNMI();
	void AnticEndFrame();
	void AnticEndScanline();
	bool AnticIsNextCPUCycleWrite();
	uint8 AnticGetCPUHeldCycleValue();
	uint32 GTIAGetXClock();
	uint32 GTIAGetTimestamp() const;
	void GTIASetSpeaker(bool newState);
	void GTIASelectController(uint8 index, bool potsEnabled);
	void GTIARequestAnticSync();
	uint32 GTIAGetLineEdgeTimingId(uint32 offset) const;
	void PokeyAssertIRQ(bool cpuBased);
	void PokeyNegateIRQ(bool cpuBased);
	void PokeyBreak();
	bool PokeyIsInInterrupt() const;
	bool PokeyIsKeyPushOK(uint8 c) const;
	uint32 PokeyGetTimestamp() const;

	void OnDiskActivity(uint8 drive, bool active, uint32 sector);
	void OnDiskMotorChange(uint8 drive, bool active);

	void VBXEAssertIRQ();
	void VBXENegateIRQ();

	void ClearPokeyTimersOnDiskIo();

	void HookCassetteOpenVector();
	void UnhookCassetteOpenVector();
	void UpdatePrinterHook();
	void UpdateCIOVHook();

	bool mbRunning;
	bool mbBreak;
	bool mbBreakOnFrameEnd;
	bool mbTurbo;
	bool mbFrameSkip;
	ATVideoStandard mVideoStandard;
	bool mbRandomFillEnabled;
	bool mbDiskSIOPatchEnabled;
	bool mbDiskSIOOverrideDetectEnabled;
	bool mbDiskSectorCounterEnabled;
	bool mbCassetteSIOPatchEnabled;
	bool mbCassetteAutoBootEnabled;
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
	bool mbMapRAM;
	ATIDEHardwareMode mIDEHardwareMode;
	uint32 mSoundBoardMemBase;
	int mBreakOnScanline;

	int		mStartupDelay;

	ATMemoryMode	mMemoryMode;
	ATKernelMode	mKernelMode;
	ATKernelMode	mActualKernelMode;
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
	ATDiskEmulator	mDiskDrives[15];
	ATAudioMonitor	*mpAudioMonitor;
	ATCassetteEmulator	*mpCassette;
	ATIDEEmulator	*mpIDE;
	ATMyIDEEmulator	*mpMyIDE;
	IATJoystickManager	*mpJoysticks;
	IATHostDeviceEmulator	*mpHostDevice;
	ATCartridgeEmulator	*mpCartridge[2];
	ATInputManager	*mpInputManager;
	ATPortController *mpPortAController;
	ATPortController *mpPortBController;
	ATLightPenPort *mpLightPen;
	IATPrinterEmulator	*mpPrinter;
	ATVBXEEmulator *mpVBXE;
	void *mpVBXEMemory;
	ATSoundBoardEmulator *mpSoundBoard;
	ATSlightSIDEmulator *mpSlightSID;
	ATCovoxEmulator *mpCovox;
	ATRTime8Emulator *mpRTime8;
	IATRS232Emulator *mpRS232;
	ATCheatEngine *mpCheatEngine;
	IATUIRenderer *mpUIRenderer;
	IATPCLinkDevice *mpPCLink;
	ATKMKJZIDE *mpKMKJZIDE;
	ATSIDEEmulator *mpSIDE;
	IATVirtualScreenHandler *mpVirtualScreenHandler;

	ATHLEBasicLoader *mpHLEBasicLoader;
	ATHLEProgramLoader *mpHLEProgramLoader;
	ATHLEFPAccelerator *mpHLEFPAccelerator;
	ATHLEFastBootHook *mpHLEFastBootHook;
	IATHLECIOHook *mpHLECIOHook;

	uint8	mPORTAOUT;
	uint8	mPORTADDR;
	uint8	mPORTACTL;
	uint8	mPORTBOUT;
	uint8	mPORTBDDR;
	uint8	mPORTBCTL;
	bool	mbPIAEdgeA;
	bool	mbPIAEdgeB;
	uint8	mPIACB2;

	enum {
		kPIACS_Floating,
		kPIACS_Low,
		kPIACS_High
	};

	enum {
		kIRQSource_POKEY = 0x01,
		kIRQSource_VBXE = 0x02,
		kIRQSource_PIAA1 = 0x04,
		kIRQSource_PIAA2 = 0x08,
		kIRQSource_PIAB1 = 0x10,
		kIRQSource_PIAB2 = 0x20
	};

	uint32	mIRQFlags;
	uint32	mVBXEPage;
	uint32	mHookPage;
	uint8	mHookPageByte;

	const uint8 *mpBPReadPage;
	uint8 *mpBPWritePage;

	const uint8 *mpKernelUpperROM;
	const uint8 *mpKernelLowerROM;
	const uint8 *mpKernelSelfTestROM;
	const uint8 *mpKernel5200ROM;
	uint32	mKernelSymbolsModuleId;

	uint32		mCartModuleIds[3];

	IATHLEKernel	*mpHLEKernel;
	ATCPUProfiler	*mpProfiler;
	ATCPUVerifier	*mpVerifier;
	ATCPUHeatMap	*mpHeatMap;

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
	ATMemoryLayer	*mpMemLayerIDE;
	ATMemoryLayer	*mpMemLayerRT8;
	ATMemoryLayer	*mpMemLayerHook;

	VDStringW	mROMImagePaths[kATROMImageCount];

	////////////////////////////////////
	bool	mbHaveOSBKernel;
	bool	mbHaveXLKernel;
	bool	mbHave1200XLKernel;
	bool	mbHaveXEGSKernel;
	bool	mbHave5200Kernel;

	VDALIGN(4)	uint8	mOSAKernelROM[0x2800];
	VDALIGN(4)	uint8	mOSBKernelROM[0x2800];
	VDALIGN(4)	uint8	mXLKernelROM[0x4000];
	VDALIGN(4)	uint8	m1200XLKernelROM[0x4000];
	VDALIGN(4)	uint8	mXEGSKernelROM[0x4000];
	VDALIGN(4)	uint8	mHLEKernelROM[0x4000];
	VDALIGN(4)	uint8	mLLEKernelROM[0x2800];
	VDALIGN(4)	uint8	mBASICROM[0x2000];
	VDALIGN(4)	uint8	mOtherKernelROM[0x4000];
	VDALIGN(4)	uint8	m5200KernelROM[0x0800];
	VDALIGN(4)	uint8	m5200LLEKernelROM[0x0800];
	VDALIGN(4)	uint8	mGameROM[0x2000];

	VDALIGN(4)	uint8	mHookROM[0x200];

	VDALIGN(4)	uint8	mMemory[0x140000];
};

#endif
