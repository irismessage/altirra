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

#include "cpu.h"
#include "antic.h"
#include "gtia.h"
#include "pokey.h"
#include "disk.h"
#include "scheduler.h"
#include "vbxe.h"

enum ATMemoryMode {
	kATMemoryMode_48K,
	kATMemoryMode_52K,
	kATMemoryMode_64K,
	kATMemoryMode_128K,
	kATMemoryMode_320K,
	kATMemoryMode_576K,
	kATMemoryMode_1088K,
	kATMemoryModeCount
};

enum ATHardwareMode {
	kATHardwareMode_800,
	kATHardwareMode_800XL,
	kATHardwareModeCount
};

enum ATKernelMode {
	kATKernelMode_Default,
	kATKernelMode_OSB,
	kATKernelMode_XL,
	kATKernelMode_HLE,
	kATKernelMode_LLE,
	kATKernelMode_OSA,
	kATKernelMode_Other,
	kATKernelModeCount
};

enum ATSimulatorEvent {
	kATSimEvent_None,
	kATSimEvent_CPUSingleStep,
	kATSimEvent_CPUStackBreakpoint,
	kATSimEvent_CPUPCBreakpoint,
	kATSimEvent_CPUPCBreakpointsUpdated,
	kATSimEvent_CPUIllegalInsn,
	kATSimEvent_ReadBreakpoint,
	kATSimEvent_WriteBreakpoint,
	kATSimEvent_DiskSectorBreakpoint,
	kATSimEvent_EndOfFrame,
	kATSimEvent_ScanlineBreakpoint,
	kATSimEvent_Resume,
	kATSimEvent_Suspend,
	kATSimEvent_VerifierFailure
};

enum {
	kATAddressSpace_CPU		= 0x00000000,
	kATAddressSpace_ANTIC	= 0x10000000,
	kATAddressSpace_VBXE	= 0x20000000,
	kATAddressSpace_PORTB	= 0x30000000,
	kATAddressOffsetMask	= 0x00FFFFFF,
	kATAddressSpaceMask		= 0xF0000000
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
class ATRTime8Emulator;
class ATCPUProfiler;
class ATCPUVerifier;

class IATSimulatorCallback {
public:
	virtual void OnSimulatorEvent(ATSimulatorEvent ev) = 0;
};

class ATSimulator : ATCPUEmulatorMemory,
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
	ATCPUEmulatorMemory& GetCPUMemory() { return *this; }
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
	ATCartridgeEmulator *GetCartridge() { return mpCartridge; }

	bool IsTurboModeEnabled() const { return mbTurbo; }
	bool IsFrameSkipEnabled() const { return mbFrameSkip; }
	bool IsPALMode() const { return mbPALMode; }
	ATMemoryMode GetMemoryMode() const { return mMemoryMode; }
	ATKernelMode GetKernelMode() const { return mKernelMode; }
	ATKernelMode GetActualKernelMode() const { return mActualKernelMode; }
	ATHardwareMode GetHardwareMode() const { return mHardwareMode; }
	bool IsDiskSIOPatchEnabled() const { return mbDiskSIOPatchEnabled; }
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

	uint8	GetBankRegister() const { return mPORTBOUT | ~mPORTBDDR; }
	uint32	GetCPUBankBase() const { return mpReadMemoryMap[0x40] - mMemory; }
	uint32	GetAnticBankBase() const { return mpAnticMemoryMap[0x40] - mMemory; }
	int		GetCartBank() const;

	bool IsReadBreakEnabled() const { return mReadBreakAddress < 0x10000; }
	uint16 GetReadBreakAddress() const { return (uint16)mReadBreakAddress; }
	bool IsWriteBreakEnabled() const { return mWriteBreakAddress < 0x10000; }
	uint16 GetWriteBreakAddress() const { return (uint16)mWriteBreakAddress; }

	void SetReadBreakAddress();
	void SetReadBreakAddress(uint16 addr);
	void SetWriteBreakAddress();
	void SetWriteBreakAddress(uint16 addr);

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

	bool IsPrinterEnabled() const;
	void SetPrinterEnabled(bool enable);

	void ColdReset();
	void WarmReset();
	void Resume();
	void Suspend();

	void UnloadAll();
	void Load(const wchar_t *s);
	void LoadProgram(const wchar_t *s);

	bool IsCartridgeAttached() const;

	void LoadCartridge(const wchar_t *s);
	void LoadCartridgeSC3D();

	enum AdvanceResult {
		kAdvanceResult_Stopped,
		kAdvanceResult_Running,
		kAdvanceResult_WaitingForFrame
	};

	AdvanceResult AdvanceUntilInstructionBoundary();
	AdvanceResult Advance();

	uint8 DebugReadByte(uint16 address) const;
	uint16 DebugReadWord(uint16 address);
	uint32 DebugRead24(uint16 address);

	uint8 DebugExtReadByte(uint32 address);

	uint8 DebugGlobalReadByte(uint32 address);
	uint16 DebugGlobalReadWord(uint32 address);
	uint32 DebugGlobalRead24(uint32 address);
	void DebugGlobalWriteByte(uint32 address, uint8 value);

	uint8 DebugAnticReadByte(uint16 address) {
		return AnticReadByte(address);
	}

	bool IsKernelROMLocation(uint16 address) const;

	void DumpPIAState();

	void LoadState(ATSaveStateReader& reader);
	void SaveState(ATSaveStateWriter& writer);

private:
	void UpdateXLCartridgeLine();
	void UpdateKernel();
	void RecomputeMemoryMaps();
	uint8 ReadPortA() const;

	uint8 CPUReadByte(uint16 address);
	uint8 CPUDebugReadByte(uint16 address);
	uint8 CPUDebugExtReadByte(uint16 address, uint8 bank);
	void CPUWriteByte(uint16 address, uint8 value);
	uint32 CPUGetCycle();
	uint32 CPUGetUnhaltedCycle();
	uint32 CPUGetTimestamp();
	uint8 CPUHookHit(uint16 address);
	uint8 AnticReadByte(uint16 address);
	void AnticAssertNMI();
	void AnticEndFrame();
	void AnticEndScanline();
	bool AnticIsNextCPUCycleWrite();
	uint32 GTIAGetXClock();
	void GTIASetSpeaker(bool newState);
	void GTIARequestAnticSync();
	void PokeyAssertIRQ();
	void PokeyNegateIRQ();
	void PokeyBreak();
	bool PokeyIsInInterrupt() const;
	bool PokeyIsKeyPushOK(uint8 c) const;
	void OnDiskActivity(uint8 drive, bool active, uint32 sector);
	void VBXERequestMemoryMapUpdate();
	void VBXEAssertIRQ();
	void VBXENegateIRQ();

	uint8 LoadProgramHook();
	uint8 LoadProgramHookCont();
	void UnloadProgramSymbols();
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
	int mBreakOnScanline;

	int		mStartupDelay;
	bool	mbCIOHandlersEstablished;
	uint32	mCassetteCIOOpenHandlerHookAddress;

	ATMemoryMode	mMemoryMode;
	ATKernelMode	mKernelMode;
	ATKernelMode	mActualKernelMode;
	ATHardwareMode	mHardwareMode;
	ATSimulatorEvent	mPendingEvent;

	ATCPUEmulator	mCPU;
	ATAnticEmulator	mAntic;
	ATGTIAEmulator	mGTIA;
	ATPokeyEmulator	mPokey;
	ATPokeyEmulator	mPokey2;
	ATScheduler		mScheduler;
	ATScheduler		mSlowScheduler;
	ATDiskEmulator	mDiskDrives[8];
	ATCassetteEmulator	*mpCassette;
	IATJoystickManager	*mpJoysticks;
	IATHardDiskEmulator	*mpHardDisk;
	ATCartridgeEmulator	*mpCartridge;
	ATInputManager	*mpInputManager;
	ATPortController *mpPortAController;
	ATPortController *mpPortBController;
	IATPrinterEmulator	*mpPrinter;
	ATVBXEEmulator *mpVBXE;
	ATRTime8Emulator *mpRTime8;

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

	uint32	mReadBreakAddress;
	uint32	mWriteBreakAddress;

	const uint8 *const *mpReadMemoryMap;
	uint8 *const *mpWriteMemoryMap;
	const uint8 *const *mpAnticMemoryMap;

	const uint8 *mpBPReadPage;
	uint8 *mpBPWritePage;

	const uint8 *mpKernelUpperROM;
	const uint8 *mpKernelLowerROM;
	const uint8 *mpKernelSelfTestROM;
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

	////////////////////////////////////
	const uint8	*mReadMemoryMap[256];
	uint8	*mWriteMemoryMap[256];
	const uint8	*mAnticMemoryMap[256];

	bool	mbHaveOSBKernel;
	bool	mbHaveXLKernel;

	uint8	mOSAKernelROM[0x2800];
	uint8	mOSBKernelROM[0x2800];
	uint8	mXLKernelROM[0x4000];
	uint8	mHLEKernelROM[0x4000];
	uint8	mLLEKernelROM[0x2800];
	uint8	mBASICROM[0x2000];
	uint8	mOtherKernelROM[0x4000];

	uint8	mMemory[0x140000];
	uint8	mDummyRead[256];
	uint8	mDummyWrite[256];

	const uint8	*mHighMemoryReadPageTables[3][256];
	uint8	*mHighMemoryWritePageTables[3][256];
	const uint8	*mDummyReadPageTable[256];
	uint8	*mDummyWritePageTable[256];
	const uint8	**mReadBankTable[256];
	uint8	**mWriteBankTable[256];
};

#endif
