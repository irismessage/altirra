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
	kATKernelModeCount
};

enum ATCartridgeMode {
	kATCartridgeMode_None,
	kATCartridgeMode_8K,
	kATCartridgeMode_16K,
	kATCartridgeMode_XEGS_32K,
	kATCartridgeMode_MaxFlash_128K,
	kATCartridgeMode_MaxFlash_1024K
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
	kATSimEvent_Suspend
};

class ATSaveStateReader;
class ATSaveStateWriter;
class ATCassetteEmulator;
class IATHLEKernel;
class IATJoystickManager;
class IATHardDiskEmulator;

class IATSimulatorCallback {
public:
	virtual void OnSimulatorEvent(ATSimulatorEvent ev) = 0;
};

class ATSimulator : ATCPUEmulatorMemory, IATAnticEmulatorConnections, IATPokeyEmulatorConnections, IATGTIAEmulatorConnections, IATDiskActivity {
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

	bool IsTurboModeEnabled() const { return mbTurbo; }
	bool IsFrameSkipEnabled() const { return mbFrameSkip; }
	bool IsPALMode() const { return mbPALMode; }
	ATMemoryMode GetMemoryMode() const { return mMemoryMode; }
	ATKernelMode GetKernelMode() const { return mKernelMode; }
	ATHardwareMode GetHardwareMode() const { return mHardwareMode; }
	bool IsDiskSIOPatchEnabled() const { return mbDiskSIOPatchEnabled; }
	bool IsCassetteSIOPatchEnabled() const { return mbCassetteSIOPatchEnabled; }
	bool IsFPPatchEnabled() const { return mbFPPatchEnabled; }
	bool IsBASICEnabled() const { return mbBASICEnabled; }
	bool IsROMAutoReloadEnabled() const { return mbROMAutoReloadEnabled; }
	bool IsAutoLoadKernelSymbolsEnabled() const { return mbAutoLoadKernelSymbols; }
	bool IsDualPokeysEnabled() const { return mbDualPokeys; }

	uint8	GetBankRegister() const { return mPORTB; }
	uint32	GetCPUBankBase() const { return mpReadMemoryMap[0x40] - mMemory; }
	uint32	GetAnticBankBase() const { return mpAnticMemoryMap[0x40] - mMemory; }
	int		GetCartBank() const { return mCartBank; }

	bool IsReadBreakEnabled() const { return mReadBreakAddress < 0x10000; }
	uint16 GetReadBreakAddress() const { return (uint16)mReadBreakAddress; }
	bool IsWriteBreakEnabled() const { return mWriteBreakAddress < 0x10000; }
	uint16 GetWriteBreakAddress() const { return (uint16)mWriteBreakAddress; }

	void SetReadBreakAddress();
	void SetReadBreakAddress(uint16 addr);
	void SetWriteBreakAddress();
	void SetWriteBreakAddress(uint16 addr);

	void SetBreakOnFrameEnd(bool enabled) { mbBreakOnFrameEnd = enabled; }
	void SetBreakOnScanline(int scanline) { mBreakOnScanline = scanline; }
	void SetTurboModeEnabled(bool turbo);
	void SetFrameSkipEnabled(bool skip);
	void SetPALMode(bool pal);
	void SetMemoryMode(ATMemoryMode mode);
	void SetKernelMode(ATKernelMode mode);
	void SetHardwareMode(ATHardwareMode mode);
	void SetDiskSIOPatchEnabled(bool enable);
	void SetCassetteSIOPatchEnabled(bool enable);
	void SetFPPatchEnabled(bool enable);
	void SetBASICEnabled(bool enable);
	void SetROMAutoReloadEnabled(bool enable);
	void SetAutoLoadKernelSymbolsEnabled(bool enable);
	void SetDualPokeysEnabled(bool enable);

	void ColdReset();
	void WarmReset();
	void Resume();
	void Suspend();

	void Load(const wchar_t *s);
	void LoadCartridge(const wchar_t *s);

	enum AdvanceResult {
		kAdvanceResult_Stopped,
		kAdvanceResult_Running,
		kAdvanceResult_WaitingForFrame
	};

	AdvanceResult AdvanceUntilInstructionBoundary();
	AdvanceResult Advance();

	void SetKeyboardControlData(uint32 mask, uint32 data);
	void ApplyMouseDelta(int dx, int dy);
	void ResetMouse();

	uint8 DebugReadByte(uint16 address);
	uint16 DebugReadWord(uint16 address);
	uint32 DebugRead24(uint16 address);

	uint8 DebugAnticReadByte(uint16 address) {
		return AnticReadByte(address);
	}

	void LoadState(ATSaveStateReader& reader);
	void SaveState(ATSaveStateWriter& writer);

private:
	void UpdateXLCartridgeLine();
	void UpdateKernel();
	void RecomputeMemoryMaps();

	uint8 CPUReadByte(uint16 address);
	void CPUWriteByte(uint16 address, uint8 value);
	uint32 GetTimestamp();
	uint8 CPUHookHit(uint16 address);
	uint8 AnticReadByte(uint16 address);
	void AnticAssertNMI();
	void AnticEndFrame();
	void AnticEndScanline();
	uint32 GTIAGetXClock();
	void GTIASetSpeaker(bool newState);
	void GTIARequestAnticSync();
	void PokeyAssertIRQ();
	void PokeyNegateIRQ();
	void PokeyBreak();
	void OnDiskActivity(uint8 drive, bool active);
	uint8 LoadProgramHook();
	uint8 LoadProgramHookCont();

	bool mbRunning;
	bool mbBreak;
	bool mbBreakOnFrameEnd;
	bool mbTurbo;
	bool mbFrameSkip;
	bool mbPALMode;
	bool mbDiskSIOPatchEnabled;
	bool mbCassetteSIOPatchEnabled;
	bool mbFPPatchEnabled;
	bool mbBASICEnabled;
	bool mbROMAutoReloadEnabled;
	bool mbAutoLoadKernelSymbols;
	bool mbDualPokeys;
	int mBreakOnScanline;

	int	mCartBank;
	int	mInitialCartBank;

	bool	mbCIOHandlersEstablished;

	ATMemoryMode	mMemoryMode;
	ATKernelMode	mKernelMode;
	ATHardwareMode	mHardwareMode;
	ATCartridgeMode	mCartMode;
	ATSimulatorEvent	mPendingEvent;

	ATCPUEmulator	mCPU;
	ATAnticEmulator	mAntic;
	ATGTIAEmulator	mGTIA;
	ATPokeyEmulator	mPokey;
	ATPokeyEmulator	mPokey2;
	ATScheduler		mScheduler;
	ATDiskEmulator	mDiskDrives[4];
	ATCassetteEmulator	*mpCassette;
	IATJoystickManager	*mpJoysticks;
	IATHardDiskEmulator	*mpHardDisk;

	uint32	mKeyboardControllerData;
	int		mMouseDeltaX;
	int		mMouseFineDeltaX;
	int		mMouseDeltaY;
	int		mMouseFineDeltaY;

	uint8	mPORTA;
	uint8	mPORTACTL;
	uint8	mPORTB;
	uint8	mPORTBCTL;

	uint32	mReadBreakAddress;
	uint32	mWriteBreakAddress;

	const uint8 *const *mpReadMemoryMap;
	uint8 *const *mpWriteMemoryMap;
	const uint8 *const *mpAnticMemoryMap;

	const uint8 *mpBPReadPage;
	uint8 *mpBPWritePage;

	////////////////////////////////////
	const uint8	*mReadMemoryMap[256];
	uint8	*mWriteMemoryMap[256];
	const uint8	*mAnticMemoryMap[256];

	uint8	mOSBKernelROM[0x2800];
	uint8	mXLKernelROM[0x4000];
	uint8	mHLEKernelROM[0x4000];
	uint8	mLLEKernelROM[0x2800];
	vdfastvector<uint8> mCARTROM;
	uint8	mBASICROM[0x2000];

	uint8	mMemory[0x110000];
	uint8	mDummyRead[256];
	uint8	mDummyWrite[256];

	const uint8 *mpKernelUpperROM;
	const uint8 *mpKernelLowerROM;
	const uint8 *mpKernelSelfTestROM;
	uint32	mKernelSymbolsModuleId;

	vdfastvector<uint8>		mProgramToLoad;
	ptrdiff_t	mProgramLoadIndex;
	typedef vdfastvector<IATSimulatorCallback *> Callbacks;
	Callbacks	mCallbacks;

	IATHLEKernel	*mpHLEKernel;
};

#endif
