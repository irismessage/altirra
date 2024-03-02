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

struct ATCartLoadContext;
class ATMemoryManager;
class ATMMUEmulator;

enum ATMemoryMode {
	kATMemoryMode_48K,
	kATMemoryMode_52K,
	kATMemoryMode_64K,
	kATMemoryMode_128K,
	kATMemoryMode_320K,
	kATMemoryMode_576K,
	kATMemoryMode_1088K,
	kATMemoryMode_16K,
	kATMemoryModeCount
};

enum ATHardwareMode {
	kATHardwareMode_800,
	kATHardwareMode_800XL,
	kATHardwareMode_5200,
	kATHardwareModeCount
};

enum ATROMImage {
	kATROMImage_OSA,
	kATROMImage_OSB,
	kATROMImage_XL,
	kATROMImage_Other,
	kATROMImage_5200,
	kATROMImage_Basic,
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
	kATKernelModeCount
};

enum ATSimulatorEvent {
	kATSimEvent_None,
	kATSimEvent_AnonymousInterrupt,
	kATSimEvent_CPUSingleStep,
	kATSimEvent_CPUStackBreakpoint,
	kATSimEvent_CPUPCBreakpoint,
	kATSimEvent_CPUPCBreakpointsUpdated,
	kATSimEvent_CPUIllegalInsn,
	kATSimEvent_CPUNewPath,
	kATSimEvent_ReadBreakpoint,
	kATSimEvent_WriteBreakpoint,
	kATSimEvent_DiskSectorBreakpoint,
	kATSimEvent_EndOfFrame,
	kATSimEvent_ScanlineBreakpoint,
	kATSimEvent_VerifierFailure,
	kATSimEvent_ColdReset,
	kATSimEvent_FrameTick,
	kATSimEvent_EXEInitSegment,
	kATSimEvent_EXERunSegment
};

enum {
	kATAddressSpace_CPU		= 0x00000000,
	kATAddressSpace_ANTIC	= 0x10000000,
	kATAddressSpace_VBXE	= 0x20000000,
	kATAddressSpace_PORTB	= 0x30000000,
	kATAddressOffsetMask	= 0x00FFFFFF,
	kATAddressSpaceMask		= 0xF0000000
};

enum ATStorageId {
	kATStorageId_None,
	kATStorageId_UnitMask	= 0x00FF,
	kATStorageId_Disk		= 0x0100,
	kATStorageId_Cartridge	= 0x0200,
	kATStorageId_TypeMask	= 0xFF00,
	kATStorageId_All
};

class ATSaveStateReader;
class ATSaveStateWriter;
class ATCassetteEmulator;
class IATHLEKernel;
class IATJoystickManager;
class IATHardDiskEmulator;
class ATCartridgeEmulator;
class ATPortController;
class ATInputManager;
class IATPrinterEmulator;
class ATVBXEEmulator;
class ATSoundBoardEmulator;
class ATRTime8Emulator;
class ATCPUProfiler;
class ATCPUVerifier;
class ATIDEEmulator;
class IATAudioOutput;
class IATRS232Emulator;
class ATLightPenPort;
class ATCheatEngine;
class ATMMUEmulator;
class ATAudioMonitor;
class IATPCLinkDevice;

class IATSimulatorCallback {
public:
	virtual void OnSimulatorEvent(ATSimulatorEvent ev) = 0;
};

enum ATLoadType {
	kATLoadType_Other,
	kATLoadType_Cartridge,
	kATLoadType_Disk
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
	IATHardDiskEmulator *GetHardDisk() { return mpHardDisk; }
	ATInputManager *GetInputManager() { return mpInputManager; }
	IATJoystickManager& GetJoystickManager() { return *mpJoysticks; }
	IATPrinterEmulator *GetPrinter() { return mpPrinter; }
	ATVBXEEmulator *GetVBXE() { return mpVBXE; }
	ATSoundBoardEmulator *GetSoundBoard() { return mpSoundBoard; }
	ATCartridgeEmulator *GetCartridge(uint32 unit) { return mpCartridge[unit]; }
	IATUIRenderer *GetUIRenderer() { return mpUIRenderer; }
	ATIDEEmulator *GetIDEEmulator() { return mpIDE; }
	IATAudioOutput *GetAudioOutput() { return mpAudioOutput; }
	IATRS232Emulator *GetRS232() { return mpRS232; }
	ATLightPenPort *GetLightPenPort() { return mpLightPen; }
	IATPCLinkDevice *GetPCLink() { return mpPCLink; }

	bool IsTurboModeEnabled() const { return mbTurbo; }
	bool IsFrameSkipEnabled() const { return mbFrameSkip; }
	bool IsPALMode() const { return mbPALMode; }
	ATMemoryMode GetMemoryMode() const { return mMemoryMode; }
	ATKernelMode GetKernelMode() const { return mKernelMode; }
	ATKernelMode GetActualKernelMode() const { return mActualKernelMode; }
	ATHardwareMode GetHardwareMode() const { return mHardwareMode; }
	bool IsDiskSIOPatchEnabled() const { return mbDiskSIOPatchEnabled; }
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
	bool IsIDEUsingD5xx() const { return mbIDEUseD5xx; }

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

	bool IsRandomFillEnabled() const { return mbRandomFillEnabled; }
	void SetRandomFillEnabled(bool enabled) { mbRandomFillEnabled = enabled; }

	void SetBreakOnFrameEnd(bool enabled) { mbBreakOnFrameEnd = enabled; }
	void SetBreakOnScanline(int scanline) { mBreakOnScanline = scanline; }
	void SetTurboModeEnabled(bool turbo);
	void SetFrameSkipEnabled(bool skip);
	void SetPALMode(bool pal);
	void SetMemoryMode(ATMemoryMode mode);
	void SetKernelMode(ATKernelMode mode);
	void SetHardwareMode(ATHardwareMode mode);
	void SetDiskSIOPatchEnabled(bool enable);
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

	bool IsRS232Enabled() const;
	void SetRS232Enabled(bool enable);

	bool IsPrinterEnabled() const;
	void SetPrinterEnabled(bool enable);

	bool IsFastBootEnabled() const { return mbFastBoot; }
	void SetFastBootEnabled(bool enable);

	ATCheatEngine *GetCheatEngine() { return mpCheatEngine; }
	void SetCheatEngineEnabled(bool enable);

	bool IsAudioMonitorEnabled() const { return mpAudioMonitor != NULL; }
	void SetAudioMonitorEnabled(bool enable);

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
	void LoadProgram(const wchar_t *symbolHintPath, IVDRandomAccessStream& stream);

	bool IsCartridgeAttached(uint32 index) const;

	void UnloadCartridge(uint32 index);
	bool LoadCartridge(uint32 index, const wchar_t *s, ATCartLoadContext *loadCtx);
	bool LoadCartridge(uint32 index, const wchar_t *origPath, const wchar_t *imagePath, IVDRandomAccessStream&, ATCartLoadContext *loadCtx);
	void LoadCartridgeSC3D();
	void LoadCartridge5200Default();
	void LoadCartridgeFlash1Mb(bool altbank);
	void LoadCartridgeFlash8Mb();
	void LoadCartridgeFlashSIC();

	void LoadIDE(bool d5xx, bool write, bool fast, uint32 cylinders, uint32 heads, uint32 sectors, const wchar_t *path);
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

	void DumpPIAState();

	void LoadState(ATSaveStateReader& reader);
	void SaveState(ATSaveStateWriter& writer);

private:
	void UpdateXLCartridgeLine();
	void UpdateKernel();
	void InitMemoryMap();
	void ShutdownMemoryMap();
	uint8 ReadPortA() const;
	ATSimulatorEvent VDNOINLINE AdvancePhantomDMA(int x);

	uint8 PIAReadByte(uint8 addr) const;
	void PIAWriteByte(uint8 addr, uint8 value);
	static sint32 RT8ReadByte(void *thisptr0, uint32 addr);
	static bool RT8WriteByte(void *thisptr0, uint32 addr, uint8 value);

	uint32 CPUGetCycle();
	uint32 CPUGetUnhaltedCycle();
	uint32 CPUGetTimestamp();
	uint8 CPUHookHit(uint16 address);
	uint8 CPURecordBusActivity(uint8 value);
	uint8 AnticReadByte(uint32 address);
	void AnticAssertNMI();
	void AnticEndFrame();
	void AnticEndScanline();
	bool AnticIsNextCPUCycleWrite();
	uint32 GTIAGetXClock();
	uint32 GTIAGetTimestamp() const;
	void GTIASetSpeaker(bool newState);
	void GTIASelectController(uint8 index, bool potsEnabled);
	void GTIARequestAnticSync();
	void PokeyAssertIRQ(bool cpuBased);
	void PokeyNegateIRQ(bool cpuBased);
	void PokeyBreak();
	bool PokeyIsInInterrupt() const;
	bool PokeyIsKeyPushOK(uint8 c) const;
	uint32 PokeyGetTimestamp() const;

	void OnDiskActivity(uint8 drive, bool active, uint32 sector);
	void VBXEAssertIRQ();
	void VBXENegateIRQ();

	uint8 LoadProgramHook();
	uint8 LoadProgramHookCont();
	void UnloadProgramSymbols();
	void ClearPokeyTimersOnDiskIo();

	void HookCassetteOpenVector();
	void UnhookCassetteOpenVector();
	void UpdatePrinterHook();
	void UpdateSIOVHook();
	void UpdateCIOVHook();

	bool mbRunning;
	bool mbBreak;
	bool mbBreakOnFrameEnd;
	bool mbTurbo;
	bool mbFrameSkip;
	bool mbPALMode;
	bool mbRandomFillEnabled;
	bool mbDiskSIOPatchEnabled;
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
	bool mbIDEUseD5xx;
	uint32 mSoundBoardMemBase;
	int mBreakOnScanline;

	int		mStartupDelay;
	bool	mbCIOHandlersEstablished;
	uint32	mCassetteCIOOpenHandlerHookAddress;

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

	ATCPUEmulator	mCPU;
	ATAnticEmulator	mAntic;
	ATGTIAEmulator	mGTIA;
	vdautoptr<IATAudioOutput> mpAudioOutput;
	ATPokeyEmulator	mPokey;
	ATPokeyEmulator	mPokey2;
	ATScheduler		mScheduler;
	ATScheduler		mSlowScheduler;
	ATDiskEmulator	mDiskDrives[15];
	ATAudioMonitor	*mpAudioMonitor;
	ATCassetteEmulator	*mpCassette;
	ATIDEEmulator	*mpIDE;
	IATJoystickManager	*mpJoysticks;
	IATHardDiskEmulator	*mpHardDisk;
	ATCartridgeEmulator	*mpCartridge[2];
	ATInputManager	*mpInputManager;
	ATPortController *mpPortAController;
	ATPortController *mpPortBController;
	ATLightPenPort *mpLightPen;
	IATPrinterEmulator	*mpPrinter;
	ATVBXEEmulator *mpVBXE;
	void *mpVBXEMemory;
	ATSoundBoardEmulator *mpSoundBoard;
	ATRTime8Emulator *mpRTime8;
	IATRS232Emulator *mpRS232;
	ATCheatEngine *mpCheatEngine;
	IATUIRenderer *mpUIRenderer;
	IATPCLinkDevice *mpPCLink;

	uint8	mPORTAOUT;
	uint8	mPORTADDR;
	uint8	mPORTACTL;
	uint8	mPORTBOUT;
	uint8	mPORTBDDR;
	uint8	mPORTBCTL;

	enum {
		kIRQSource_POKEY,
		kIRQSource_VBXE
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

	vdfastvector<uint8>		mProgramToLoad;
	bool		mbProgramLoadPending;
	ptrdiff_t	mProgramLoadIndex;
	uint32		mProgramModuleIds[3];
	uint32		mCartModuleIds[3];

	typedef vdfastvector<IATSimulatorCallback *> Callbacks;
	Callbacks	mCallbacks;
	int			mCallbacksBusy;
	bool		mbCallbacksChanged;

	IATHLEKernel	*mpHLEKernel;
	ATCPUProfiler	*mpProfiler;
	ATCPUVerifier	*mpVerifier;

	ATMemoryLayer	*mpMemLayerLoRAM;
	ATMemoryLayer	*mpMemLayerHiRAM;
	ATMemoryLayer	*mpMemLayerExtendedRAM;
	ATMemoryLayer	*mpMemLayerLowerKernelROM;
	ATMemoryLayer	*mpMemLayerUpperKernelROM;
	ATMemoryLayer	*mpMemLayerBASICROM;
	ATMemoryLayer	*mpMemLayerSelfTestROM;
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
	bool	mbHave5200Kernel;

	__declspec(align(4))	uint8	mOSAKernelROM[0x2800];
	__declspec(align(4))	uint8	mOSBKernelROM[0x2800];
	__declspec(align(4))	uint8	mXLKernelROM[0x4000];
	__declspec(align(4))	uint8	mHLEKernelROM[0x4000];
	__declspec(align(4))	uint8	mLLEKernelROM[0x2800];
	__declspec(align(4))	uint8	mBASICROM[0x2000];
	__declspec(align(4))	uint8	mOtherKernelROM[0x4000];
	__declspec(align(4))	uint8	m5200KernelROM[0x0800];
	__declspec(align(4))	uint8	m5200LLEKernelROM[0x0800];

	__declspec(align(4))	uint8	mMemory[0x140000];
};

#endif
