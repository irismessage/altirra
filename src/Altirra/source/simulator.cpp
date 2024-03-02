//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2009 Avery Lee
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

#include "stdafx.h"
#include <vd2/system/binary.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/strutil.h>
#include "simulator.h"
#include "cassette.h"
#include "harddisk.h"
#include "console.h"
#include "hlekernel.h"
#include "joystick.h"
#include "ksyms.h"
#include "kerneldb.h"
#include "decmath.h"
#include "debugger.h"
#include "oshelper.h"
#include "savestate.h"
#include "cartridge.h"
#include "resource.h"
#include "inputcontroller.h"
#include "inputmanager.h"

namespace {
	class PokeyDummyConnection : public IATPokeyEmulatorConnections {
	public:
		void PokeyAssertIRQ() {}
		void PokeyNegateIRQ() {}
		void PokeyBreak() {}
	};

	PokeyDummyConnection g_pokeyDummyConnection;
}

ATSimulator::ATSimulator()
	: mbROMAutoReloadEnabled(false)
	, mbAutoLoadKernelSymbols(true)
	, mPokey(false)
	, mPokey2(true)
	, mpCassette(NULL)
	, mpJoysticks(NULL)
	, mpCartridge(NULL)
	, mCassetteCIOOpenHandlerHookAddress(0)
	, mKernelSymbolsModuleId(0)
	, mbProgramLoadPending(false)
	, mCallbacksBusy(0)
	, mbCallbacksChanged(false)
	, mpHLEKernel(ATCreateHLEKernel())
	, mpInputManager(new ATInputManager)
	, mpPortAController(new ATPortController)
	, mpPortBController(new ATPortController)
{
	mProgramModuleIds[0] = 0;
	mProgramModuleIds[1] = 0;
	mCartModuleIds[0] = 0;
	mCartModuleIds[1] = 0;

	mpHLEKernel->Init(mCPU, *this);

	mMemoryMode = kATMemoryMode_320K;
	mKernelMode = kATKernelMode_OSB;
	SetKernelMode(kATKernelMode_HLE);
	mHardwareMode = kATHardwareMode_800XL;

	mCPU.Init(this);
	mCPU.SetHLE(mpHLEKernel->AsCPUHLE());
	mGTIA.Init(this);
	mAntic.Init(this, &mGTIA);
	mPokey.Init(this, &mScheduler);
	mPokey2.Init(&g_pokeyDummyConnection, &mScheduler);

	mpPortAController->Init(&mGTIA, &mPokey, 0);
	mpPortBController->Init(&mGTIA, &mPokey, 2);

	mpCassette = new ATCassetteEmulator;
	mpCassette->Init(&mPokey, &mScheduler);
	mPokey.SetCassette(mpCassette);

	mpHardDisk = ATCreateHardDiskEmulator();

	for(int i=0; i<4; ++i) {
		mDiskDrives[i].Init(i, this, &mScheduler);
		mPokey.AddSIODevice(&mDiskDrives[i]);
	}

	mReadBreakAddress = 0x10000;
	mWriteBreakAddress = 0x10000;
	mPendingEvent = kATSimEvent_None;

	mbBreak = false;
	mbBreakOnFrameEnd = false;
	mbTurbo = false;
	mbFrameSkip = true;
	mbPALMode = false;
	mbDiskSIOPatchEnabled = false;
	mbCassetteSIOPatchEnabled = false;
	mbCassetteAutoBootEnabled = false;
	mbBASICEnabled = false;
	mbDualPokeys = false;

	mStartupDelay = 0;
	mbCIOHandlersEstablished = false;

	SetDiskSIOPatchEnabled(true);
	SetCassetteSIOPatchEnabled(true);
	SetCassetteAutoBootEnabled(true);
	SetFPPatchEnabled(true);

	mBreakOnScanline = -1;

	mJoystickControllerData = 0;
	mKeyboardControllerData = 0;
	mMouseControllerData = 0xFF;
	mMouseDeltaX = 0;
	mMouseFineDeltaX = 0;
	mMouseDeltaY = 0;
	mMouseFineDeltaY = 0;

	memset(mDummyRead, 0xFF, sizeof mDummyRead);

	ColdReset();
}

ATSimulator::~ATSimulator() {
	if (mpCartridge) {
		delete mpCartridge;
		mpCartridge = NULL;
	}

	delete mpHardDisk;
	delete mpJoysticks;
	delete mpCassette;
	delete mpHLEKernel;
	delete mpInputManager;
	delete mpPortAController;
	delete mpPortBController;
}

void ATSimulator::Init(void *hwnd) {
	mpJoysticks = ATCreateJoystickManager();
	if (!mpJoysticks->Init(hwnd)) {
		delete mpJoysticks;
		mpJoysticks = NULL;
	}

	mpInputManager->Init(mpJoysticks, &mSlowScheduler, mpPortAController, mpPortBController);
}

void ATSimulator::Shutdown() {
	mpInputManager->Shutdown();

	if (mpJoysticks) {
		delete mpJoysticks;
		mpJoysticks = NULL;
	}
}

void ATSimulator::LoadROMs() {
	VDStringW ppath(VDGetProgramPath());
	VDStringW path;

	path = VDMakePath(ppath.c_str(), L"atariosa.rom");
	if (VDDoesPathExist(path.c_str())) {
		try {
			VDFile f;
			f.open(path.c_str());
			f.read(mOSAKernelROM, 10240);
			f.close();
		} catch(const MyError&) {
		}
	}

	path = VDMakePath(ppath.c_str(), L"atariosb.rom");
	if (VDDoesPathExist(path.c_str())) {
		try {
			VDFile f;
			f.open(path.c_str());
			f.read(mOSBKernelROM, 10240);
			f.close();
		} catch(const MyError&) {
		}
	}

	path = VDMakePath(ppath.c_str(), L"atarixl.rom");
	if (VDDoesPathExist(path.c_str())) {
		try {
			VDFile f;
			f.open(path.c_str());
			f.read(mXLKernelROM, 16384);
			f.close();
		} catch(const MyError&) {
		}
	}

	try {
		VDFile f;

		do {
			path = VDMakePath(ppath.c_str(), L"ataribas.rom");
			if (VDDoesPathExist(path.c_str())) {
				f.open(path.c_str());
			} else {
				path = VDMakePath(ppath.c_str(), L"basic.rom");

				if (VDDoesPathExist(path.c_str()))
					f.open(path.c_str());
				else
					break;
			}

			f.read(mBASICROM, 8192);
			f.close();
		} while(false);
	} catch(const MyError&) {
	}


	ATLoadKernelResource(IDR_KERNEL, mLLEKernelROM, 10240);
	ATLoadKernelResource(IDR_HLEKERNEL, mHLEKernelROM, 16384);
}

void ATSimulator::AddCallback(IATSimulatorCallback *cb) {
	Callbacks::const_iterator it(std::find(mCallbacks.begin(), mCallbacks.end(), cb));
	if (it == mCallbacks.end())
		mCallbacks.push_back(cb);
}

void ATSimulator::RemoveCallback(IATSimulatorCallback *cb) {
	Callbacks::iterator it(std::find(mCallbacks.begin(), mCallbacks.end(), cb));
	if (it != mCallbacks.end()) {
		if (mCallbacksBusy) {
			*it = NULL;
			mbCallbacksChanged = true;
		} else {
			*it = mCallbacks.back();
			mCallbacks.pop_back();
		}
	}
}

void ATSimulator::NotifyEvent(ATSimulatorEvent ev) {
	VDVERIFY(++mCallbacksBusy < 100);

	// Note that this list may change on the fly.
	size_t n = mCallbacks.size();
	for(uint32 i=0; i<n; ++i) {
		IATSimulatorCallback *cb = mCallbacks[i];

		if (cb)
			cb->OnSimulatorEvent(ev);
	}

	VDVERIFY(--mCallbacksBusy >= 0);

	if (!mCallbacksBusy && mbCallbacksChanged) {
		Callbacks::iterator src = mCallbacks.begin();
		Callbacks::iterator dst = src;
		Callbacks::iterator end = mCallbacks.end();

		for(; src != end; ++src) {
			IATSimulatorCallback *cb = *src;

			if (cb) {
				*dst = cb;
				++dst;
			}
		}

		if (dst != end)
			mCallbacks.erase(dst, end);

		mbCallbacksChanged = false;
	}
}

int ATSimulator::GetCartBank() const {
	return mpCartridge ? mpCartridge->GetCartBank() : -1;
}

void ATSimulator::SetReadBreakAddress() {
	mReadBreakAddress = 0x10000;
	RecomputeMemoryMaps();
}

void ATSimulator::SetReadBreakAddress(uint16 addr) {
	mReadBreakAddress = addr;
	RecomputeMemoryMaps();
}

void ATSimulator::SetWriteBreakAddress() {
	mWriteBreakAddress = 0x10000;
	RecomputeMemoryMaps();
}

void ATSimulator::SetWriteBreakAddress(uint16 addr) {
	mWriteBreakAddress = addr;
	RecomputeMemoryMaps();
}

void ATSimulator::SetTurboModeEnabled(bool turbo) {
	mbTurbo = turbo;
	mPokey.SetTurbo(mbTurbo);
	mGTIA.SetFrameSkip(mbTurbo || mbFrameSkip);
}

void ATSimulator::SetFrameSkipEnabled(bool frameskip) {
	mbFrameSkip = frameskip;
	mGTIA.SetFrameSkip(mbTurbo || mbFrameSkip);
}

void ATSimulator::SetPALMode(bool pal) {
	mbPALMode = pal;
	mAntic.SetPALMode(pal);
	mGTIA.SetPALMode(pal);
	mPokey.SetPal(pal);
	mPokey2.SetPal(pal);
}

void ATSimulator::SetMemoryMode(ATMemoryMode mode) {
	if (mMemoryMode == mode)
		return;

	mMemoryMode = mode;
	RecomputeMemoryMaps();
}

void ATSimulator::SetKernelMode(ATKernelMode mode) {
	if (mKernelMode == mode)
		return;

	mKernelMode = mode;

	UpdateKernel();
	RecomputeMemoryMaps();
}

void ATSimulator::SetHardwareMode(ATHardwareMode mode) {
	if (mHardwareMode == mode)
		return;

	mHardwareMode = mode;

	UpdateXLCartridgeLine();

	UpdateKernel();
	RecomputeMemoryMaps();
}

void ATSimulator::SetDiskSIOPatchEnabled(bool enable) {
	if (mbDiskSIOPatchEnabled == enable)
		return;

	mbDiskSIOPatchEnabled = enable;
	mCPU.SetHook(ATKernelSymbols::DSKINV, enable);
	mCPU.SetHook(ATKernelSymbols::SIOV, mbDiskSIOPatchEnabled || mbCassetteSIOPatchEnabled);
}

void ATSimulator::SetCassetteSIOPatchEnabled(bool enable) {
	if (mbCassetteSIOPatchEnabled == enable)
		return;

	mbCassetteSIOPatchEnabled = enable;

	mCPU.SetHook(ATKernelSymbols::SIOV, mbDiskSIOPatchEnabled || mbCassetteSIOPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::CSOPIV, mbCassetteSIOPatchEnabled);
	HookCassetteOpenVector();
}

void ATSimulator::SetCassetteAutoBootEnabled(bool enable) {
	mbCassetteAutoBootEnabled = enable;
}

void ATSimulator::SetFPPatchEnabled(bool enable) {
	if (mbFPPatchEnabled == enable)
		return;

	mbFPPatchEnabled = enable;
	mCPU.SetHook(ATKernelSymbols::AFP, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::FASC, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::IPF, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::FPI, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::FADD, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::FSUB, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::FMUL, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::FDIV, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::LOG, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::LOG10, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::EXP, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::EXP10, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::SKPSPC, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::ISDIGT, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::PLYEVL, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::ZFR0, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::ZF1, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::LDBUFA, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::FLD0R, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::FLD0P, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::FLD1R, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::FLD1P, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::FST0R, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::FST0P, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::FMOVE, mbFPPatchEnabled);
}

void ATSimulator::SetBASICEnabled(bool enable) {
	mbBASICEnabled = enable;
}

void ATSimulator::SetROMAutoReloadEnabled(bool enable) {
	mbROMAutoReloadEnabled = enable;
}

void ATSimulator::SetAutoLoadKernelSymbolsEnabled(bool enable) {
	mbAutoLoadKernelSymbols = enable;
}

void ATSimulator::SetDualPokeysEnabled(bool enable) {
	if (mbDualPokeys != enable) {
		mbDualPokeys = enable;

		mPokey.SetSlave(enable ? &mPokey2 : NULL);
	}
}

void ATSimulator::ColdReset() {
	if (mbROMAutoReloadEnabled) {
		LoadROMs();
		UpdateKernel();
	}

	UnloadProgramSymbols();
	UnhookCassetteOpenVector();

	// PIA forces all registers to 0 on reset.
	mPORTAOUT	= 0x00;
	mPORTADDR	= 0x00;
	mPORTACTL	= 0x00;
	mPORTBOUT	= 0x00;
	mPORTBDDR	= 0x00;
	mPORTBCTL	= 0x00;

	mCPU.ColdReset();
	mAntic.ColdReset();
	mPokey.ColdReset();
	mpCassette->ColdReset();

	for(int i=0; i<4; ++i)
		mDiskDrives[i].Reset();

	if (mpHardDisk)
		mpHardDisk->ColdReset();

	memset(mMemory, 0, sizeof mMemory);

	if (mpCartridge)
		mpCartridge->ColdReset();

	RecomputeMemoryMaps();
	UpdateXLCartridgeLine();

	mbCIOHandlersEstablished = false;

	const bool hdenabled = (mpHardDisk && mpHardDisk->IsEnabled());
	mCPU.SetHook(ATKernelSymbols::CIOV, hdenabled);
	mCPU.SetHook(0xD610, hdenabled);
	mCPU.SetHook(0xD612, hdenabled);
	mCPU.SetHook(0xD614, hdenabled);
	mCPU.SetHook(0xD616, hdenabled);
	mCPU.SetHook(0xD618, hdenabled);
	mCPU.SetHook(0xD61A, hdenabled);

	// disable program load hook in case it was left on
	mCPU.SetHook(0x01FE, false);
	mCPU.SetHook(0x01FF, false);

	// press option during power on if BASIC is disabled
	// press start during power on if cassette auto-boot is enabled
	uint8 consoleSwitches = 0x0F;

	if (!mbBASICEnabled && (!mpCartridge || mpCartridge->IsBASICDisableAllowed()))
		consoleSwitches &= ~4;

	if (mpCassette->IsLoaded() && mbCassetteAutoBootEnabled)
		consoleSwitches &= ~1;

	mGTIA.SetForcedConsoleSwitches(consoleSwitches);
	mStartupDelay = 5;

	HookCassetteOpenVector();
}

void ATSimulator::WarmReset() {
	// The Atari 400/800 route the reset button to /RNMI on ANTIC, which then fires an
	// NMI with the system reset (bit 5) set in NMIST. On the XL series, /RNMI is permanently
	// tied to +5V by pullup and the reset button directly resets the 6502, ANTIC, FREDDIE,
	// and the PIA.
	UnloadProgramSymbols();

	if (mHardwareMode == kATHardwareMode_800XL) {
		// PIA forces all registers to 0 on reset.
		mPORTAOUT	= 0x00;
		mPORTADDR	= 0x00;
		mPORTACTL	= 0x00;
		mPORTBOUT	= 0x00;
		mPORTBDDR	= 0x00;
		mPORTBCTL	= 0x00;
		RecomputeMemoryMaps();

		mCPU.WarmReset();
		mAntic.WarmReset();
	} else {
		mAntic.RequestNMI();
	}

	if (mpHardDisk)
		mpHardDisk->WarmReset();

	// disable program load hook in case it was left on
	mCPU.SetHook(0x01FE, false);
	mCPU.SetHook(0x01FF, false);
}

void ATSimulator::Resume() {
	if (mbRunning)
		return;

	mbRunning = true;
}

void ATSimulator::Suspend() {
	mbRunning = false;
}

void ATSimulator::Load(const wchar_t *s) {
	VDFile f(s);

	uint8 header[16];
	long actual = f.readData(header, 16);

	if (actual >= 6) {
		sint64 size = f.size();
		f.close();

		if (header[0] == 0xFF && header[1] == 0xFF) {
			LoadProgram(s);
			return;
		}

		if ((header[0] == 'A' && header[1] == 'T' && header[2] == '8' && header[3] == 'X') ||
			(header[2] == 'P' && header[3] == '3') ||
			(header[2] == 'P' && header[3] == '2') ||
			(header[0] == 0x96 && header[1] == 0x02) ||
			(!(size & 127) && size <= 65535*128 && !_wcsicmp(VDFileSplitExt(s), L".xfd")))
		{
			mDiskDrives[0].LoadDisk(s);
			return;
		}

		if (actual >= 12
			&& header[0] == 'R' && header[1] == 'I' && header[2] == 'F' && header[3] == 'F'
			&& header[8] == 'W' && header[9] == 'A' && header[10] == 'V' && header[11] == 'E'
			) {
			mpCassette->Load(s);
			return;
		}

		if (header[0] == 'F' && header[1] == 'U' && header[2] == 'J' && header[3] == 'I') {
			mpCassette->Load(s);
			return;
		}

		if (header[0] == 'C' && header[1] == 'A' && header[2] == 'R' && header[3] == 'T') {
			LoadCartridge(s);
			return;
		}

		const wchar_t *ext = VDFileSplitExt(s);
		if (!vdwcsicmp(ext, L".xex")
			|| !vdwcsicmp(ext, L".obx")
			|| !vdwcsicmp(ext, L".exe"))
		{
			LoadProgram(s);
			return;
		}

		if (!vdwcsicmp(ext, L".atr")
			|| !vdwcsicmp(ext, L".xfd")
			|| !vdwcsicmp(ext, L".dcm"))
		{
			mDiskDrives[0].LoadDisk(s);
			return;
		}

		if (!vdwcsicmp(ext, L".cas")
			|| !vdwcsicmp(ext, L".wav"))
		{
			mpCassette->Load(s);
			return;
		}

		if (!vdwcsicmp(ext, L".rom")
			|| !vdwcsicmp(ext, L".bin"))
		{
			LoadCartridge(s);
			return;
		}
	}

	throw MyError("Unable to identify type of file: %ls.", s);
}

void ATSimulator::LoadProgram(const wchar_t *s) {
	VDFile f(s);

	f.seek(2);

	uint32 len = (uint32)f.size() - 2;
	mProgramToLoad.resize(len);
	f.read(mProgramToLoad.data(), len);
	mProgramLoadIndex = 0;

	// cold restart the system and wait for DSKINV hook to fire
	ColdReset();

	mCPU.SetHook(ATKernelSymbols::DSKINV, true);
	mbProgramLoadPending = true;

	// try to load symbols
	UnloadProgramSymbols();

	IATDebugger *d = ATGetDebugger();

	if (d) {
		static const wchar_t *const kSymExts[]={
			L".lst", L".lab"
		};

		VDASSERTCT(sizeof(kSymExts)/sizeof(kSymExts[0]) == sizeof(mProgramModuleIds)/sizeof(mProgramModuleIds[0]));

		VDStringW sympath;
		for(int i=0; i<sizeof(mProgramModuleIds)/sizeof(mProgramModuleIds[0]); ++i) {
			sympath.assign(s, VDFileSplitExt(s));
			sympath += kSymExts[i];

			try {
				if (VDDoesPathExist(sympath.c_str())) {
					uint32 moduleId = d->LoadSymbols(sympath.c_str());

					if (moduleId) {
						mProgramModuleIds[i] = moduleId;

						ATConsolePrintf("Loaded symbols %ls\n", sympath.c_str());
					}
				}
			} catch(const MyError&) {
				// ignore
			}
		}
	}
}

bool ATSimulator::IsCartridgeAttached() const {
	return mpCartridge != NULL;
}

void ATSimulator::LoadCartridge(const wchar_t *s) {
	IATDebugger *d = ATGetDebugger();

	if (d) {
		for(int i=0; i<2; ++i) {
			if (!mCartModuleIds[i])
				continue;

			d->UnloadSymbols(mCartModuleIds[i]);	
			mCartModuleIds[i] = 0;
		}
	}

	if (mpCartridge) {
		mpCartridge->Unload();
		delete mpCartridge;
		mpCartridge = NULL;
	}

	UpdateXLCartridgeLine();
	RecomputeMemoryMaps();

	if (!s)
		return;

	if (!mpCartridge)
		mpCartridge = new ATCartridgeEmulator;

	mpCartridge->Load(s);

	RecomputeMemoryMaps();
	UpdateXLCartridgeLine();

	if (d) {
		static const wchar_t *const kSymExts[]={
			L".lst", L".lab"
		};

		VDASSERTCT(sizeof(kSymExts)/sizeof(kSymExts[0]) == sizeof(mCartModuleIds)/sizeof(mCartModuleIds[0]));

		VDStringW sympath;
		for(int i=0; i<sizeof(mCartModuleIds)/sizeof(mCartModuleIds[0]); ++i) {
			sympath.assign(s, VDFileSplitExt(s));
			sympath += kSymExts[i];

			try {
				if (VDDoesPathExist(sympath.c_str())) {
					uint32 moduleId = d->LoadSymbols(sympath.c_str());

					if (moduleId) {
						mCartModuleIds[i] = moduleId;

						ATConsolePrintf("Loaded symbols %ls\n", sympath.c_str());
					}
				}
			} catch(const MyError&) {
				// ignore
			}
		}
	}
}

void ATSimulator::LoadCartridgeSC3D() {
	LoadCartridge(NULL);

	mpCartridge = new ATCartridgeEmulator;
	mpCartridge->LoadSuperCharger3D();
}

ATSimulator::AdvanceResult ATSimulator::AdvanceUntilInstructionBoundary() {
	for(;;) {
		if (!mbRunning)
			return kAdvanceResult_Stopped;

		if (!mCPU.IsInstructionInProgress())
			return kAdvanceResult_Running;

		ATSimulatorEvent cpuEvent = kATSimEvent_None;

		if (mCPU.GetUnusedCycle())
			cpuEvent = (ATSimulatorEvent)mCPU.Advance(false);
		else {
			int x = mAntic.GetBeamX();

			if (!x && mAntic.GetBeamY() == 0) {
				mGTIA.BeginFrame(true);
			}

			ATSCHEDULER_ADVANCE(&mScheduler);
			bool busbusy = mAntic.Advance();

			cpuEvent = (ATSimulatorEvent)mCPU.Advance(busbusy);
			if (!(cpuEvent | mPendingEvent))
				continue;
		}

		ATSimulatorEvent ev = mPendingEvent;
		mPendingEvent = kATSimEvent_None;
		mbRunning = false;

		if (cpuEvent)
			NotifyEvent(cpuEvent);
		if (ev)
			NotifyEvent(ev);

		if (!mbRunning) {
			mGTIA.UpdateScreen();
			return kAdvanceResult_Stopped;
		}
	}
}

ATSimulator::AdvanceResult ATSimulator::Advance() {
	if (!mbRunning)
		return kAdvanceResult_Stopped;

	ATSimulatorEvent cpuEvent = kATSimEvent_None;

	if (mCPU.GetUnusedCycle())
		cpuEvent = (ATSimulatorEvent)mCPU.Advance(false);

	if (!cpuEvent) {
		for(int scans = 0; scans < 8; ++scans) {
			int x = mAntic.GetBeamX();

			if (!x && mAntic.GetBeamY() == 0) {
				if (!mGTIA.BeginFrame(false))
					return kAdvanceResult_WaitingForFrame;
			}

			uint32 cycles = 114 - x;

			while(cycles--) {
				ATSCHEDULER_ADVANCE(&mScheduler);
				bool busbusy = mAntic.Advance();

				cpuEvent = (ATSimulatorEvent)mCPU.Advance(busbusy);
				if (cpuEvent | mPendingEvent)
					goto handle_event;
			}
		}

		return mbRunning ? kAdvanceResult_Running : kAdvanceResult_Stopped;
	}

handle_event:
	ATSimulatorEvent ev = mPendingEvent;
	mPendingEvent = kATSimEvent_None;
	mbRunning = false;

	if (cpuEvent)
		NotifyEvent(cpuEvent);
	if (ev)
		NotifyEvent(ev);

	if (!mbRunning)
		mGTIA.UpdateScreen();

	return mbRunning ? kAdvanceResult_Running : kAdvanceResult_Stopped;
}

void ATSimulator::SetKeyboardControlData(uint32 mask, uint32 data) {
	mKeyboardControllerData = (mKeyboardControllerData & ~mask) | data;
}

void ATSimulator::ApplyMouseDelta(int dx, int dy) {
	mMouseFineDeltaX += dx;
	mMouseFineDeltaY += dy;

	static const int accel = 6;
	mMouseDeltaX += mMouseFineDeltaX / accel;
	mMouseFineDeltaX %= accel;
	mMouseDeltaY += mMouseFineDeltaY / accel;
	mMouseFineDeltaY %= accel;
}

void ATSimulator::ResetMouse() {
	mMouseDeltaX = 0;
	mMouseDeltaY = 0;
	mMouseFineDeltaX = 0;
	mMouseFineDeltaY = 0;
	mMouseControllerData = 0xFF;
}

uint8 ATSimulator::DebugReadByte(uint16 addr) {
	const uint8 *src = mpReadMemoryMap[addr >> 8];
	if (src)
		return src[addr & 0xff];

	// D000-D7FF	2K		hardware address
	switch(addr & 0xff00) {
	case 0xD000:
		return mGTIA.DebugReadByte(addr & 0x1f);
	case 0xD200:
		return mPokey.DebugReadByte(addr & 0xff);

	case 0xD300:
		switch(addr & 0x03) {
		case 0x00:
			return ReadPortA();

		case 0x01:
			return mPORTBCTL & 0x04 ? mpPortBController->GetPortValue() & (mPORTBOUT | ~mPORTBDDR) : mPORTBDDR;
		case 0x02:
			return mPORTACTL;
		case 0x03:
			return mPORTBCTL;
		}
		return 0;

	case 0xD400:
		return mAntic.ReadByte(addr & 0xff);

	case 0xD600:
		return (addr & 1) ? 0xD6 : (addr + 0x0F);

	default:
		if (!((mReadBreakAddress ^ addr) & 0xff00)) {
			if (mpBPReadPage)
				return mpBPReadPage[addr & 0xff];
		}

		return 0xFF;
	}
}

uint16 ATSimulator::DebugReadWord(uint16 address) {
	return (uint16)(DebugReadByte(address) + ((uint32)DebugReadByte(address+1) << 8));
}

uint32 ATSimulator::DebugRead24(uint16 address) {
	return (uint32)DebugReadByte(address)
		+ ((uint32)DebugReadByte(address+1) << 8)
		+ ((uint32)DebugReadByte(address+2) << 16);
}

uint8 ATSimulator::ReadPortA() const {
	if (mPORTACTL & 0x04) {
		return mpPortAController->GetPortValue() & (mPORTAOUT | ~mPORTADDR);
	}

	return mPORTADDR;
}

namespace {
	uint32 GetMemorySizeForMemoryMode(ATMemoryMode mode) {
		switch(mode) {
			case kATMemoryMode_48K:
			default:
				return 0xC000;

			case kATMemoryMode_52K:
				return 0xD000;

			case kATMemoryMode_64K:
				return 0x10000;

			case kATMemoryMode_128K:
				return 0x20000;

			case kATMemoryMode_320K:
				return 0x50000;

			case kATMemoryMode_576K:
				return 0x90000;

			case kATMemoryMode_1088K:
				return 0x110000;
		}
	}
}

void ATSimulator::DumpPIAState() {
	ATConsolePrintf("Port A control:   %02x\n", mPORTACTL);
	ATConsolePrintf("Port A direction: %02x\n", mPORTADDR);
	ATConsolePrintf("Port A output:    %02x\n", mPORTAOUT);
	ATConsolePrintf("Port B control:   %02x\n", mPORTBCTL);
	ATConsolePrintf("Port B direction: %02x\n", mPORTBDDR);
	ATConsolePrintf("Port B output:    %02x\n", mPORTBOUT);
}

class ATInvalidSaveStateException : public MyError {
public:
	ATInvalidSaveStateException()
		: MyError("The save state data is invalid.") {}
};

void ATSimulator::LoadState(ATSaveStateReader& reader) {
	try {
		mMemoryMode		= (ATMemoryMode)reader.ReadUint8();
		mKernelMode		= (ATKernelMode)reader.ReadUint8();
		mHardwareMode	= (ATHardwareMode)reader.ReadUint8();
		mPORTAOUT		= reader.ReadUint8();
		mPORTBOUT		= reader.ReadUint8();
		mPORTADDR		= reader.ReadUint8();
		mPORTBDDR		= reader.ReadUint8();
		mPORTACTL		= reader.ReadUint8();
		mPORTBCTL		= reader.ReadUint8();

		uint32 size = GetMemorySizeForMemoryMode(mMemoryMode);
		uint32 rsize = reader.ReadUint32();
		if (rsize != size)
			throw ATInvalidSaveStateException();
		reader.ReadData(mMemory, size);

		if (reader.ReadUint32() != VDMAKEFOURCC('C', 'P', 'U', ' '))
			throw ATInvalidSaveStateException();

		mCPU.LoadState(reader);

		if (reader.ReadUint32() != VDMAKEFOURCC('A', 'N', 'T', 'C'))
			throw ATInvalidSaveStateException();

		mAntic.LoadState(reader);

		if (reader.ReadUint32() != VDMAKEFOURCC('G', 'T', 'I', 'A'))
			throw ATInvalidSaveStateException();
		mGTIA.LoadState(reader);

		if (reader.ReadUint32() != VDMAKEFOURCC('P', 'O', 'K', 'Y'))
			throw ATInvalidSaveStateException();
		mPokey.LoadState(reader);

		if (mpCartridge) {
			if (reader.ReadUint32() != VDMAKEFOURCC('C', 'A', 'R', 'T'))
				throw ATInvalidSaveStateException();
			mpCartridge->LoadState(reader);
		}

		UpdateKernel();
		UpdateXLCartridgeLine();
		RecomputeMemoryMaps();
	} catch(const MyError&) {
		ColdReset();
		throw;
	}
}

void ATSimulator::SaveState(ATSaveStateWriter& writer) {
	if (kAdvanceResult_Running != AdvanceUntilInstructionBoundary())
		throw MyError("The emulation state cannot be saved because emulation is stopped in the middle of an instruction.");

	writer.WriteUint8(mMemoryMode);
	writer.WriteUint8(mKernelMode);
	writer.WriteUint8(mHardwareMode);
	writer.WriteUint8(mPORTAOUT);
	writer.WriteUint8(mPORTBOUT);
	writer.WriteUint8(mPORTADDR);
	writer.WriteUint8(mPORTBDDR);
	writer.WriteUint8(mPORTACTL);
	writer.WriteUint8(mPORTBCTL);

	uint32 size = GetMemorySizeForMemoryMode(mMemoryMode);
	writer.WriteUint32(size);
	writer.WriteData(mMemory, size);

	writer.WriteUint32(VDMAKEFOURCC('C', 'P', 'U', ' '));
	mCPU.SaveState(writer);

	writer.WriteUint32(VDMAKEFOURCC('A', 'N', 'T', 'C'));
	mAntic.SaveState(writer);
	
	writer.WriteUint32(VDMAKEFOURCC('G', 'T', 'I', 'A'));
	mGTIA.SaveState(writer);
	
	writer.WriteUint32(VDMAKEFOURCC('P', 'O', 'K', 'Y'));
	mPokey.SaveState(writer);

	if (mpCartridge) {
		writer.WriteUint32(VDMAKEFOURCC('C', 'A', 'R', 'T'));
		mpCartridge->SaveState(writer);
	}
}

void ATSimulator::UpdateXLCartridgeLine() {
	if (mHardwareMode == kATHardwareMode_800XL) {
		// TRIG3 indicates a cartridge on XL/XE. MULE fails if this isn't set properly,
		// because for some bizarre reason it jumps through WARMSV!
		mGTIA.SetControllerTrigger(3, !(mpCartridge && mpCartridge->IsABxxMapped()));
	} else {
		mGTIA.SetControllerTrigger(3, false);
	}
}

void ATSimulator::UpdateKernel() {
	ATKernelMode mode = mKernelMode;

	if (mode == kATKernelMode_Default) {
		switch(mHardwareMode) {
			case kATHardwareMode_800:
				mode = kATKernelMode_OSB;
				break;
			case kATHardwareMode_800XL:
				mode = kATKernelMode_XL;
				break;
		}
	}

	IATDebugger *deb = ATGetDebugger();
	if (mKernelSymbolsModuleId) {
		deb->UnloadSymbols(mKernelSymbolsModuleId);
		mKernelSymbolsModuleId = 0;
	}

	switch(mode) {
		case kATKernelMode_OSA:
			mpKernelLowerROM = NULL;
			mpKernelSelfTestROM = NULL;
			mpKernelUpperROM = mOSAKernelROM;
			break;
		case kATKernelMode_OSB:
			mpKernelLowerROM = NULL;
			mpKernelSelfTestROM = NULL;
			mpKernelUpperROM = mOSBKernelROM;
			break;
		case kATKernelMode_XL:
			mpKernelLowerROM = mXLKernelROM;
			mpKernelSelfTestROM = mXLKernelROM + 0x1000;
			mpKernelUpperROM = mXLKernelROM + 0x1800;
			break;
		case kATKernelMode_HLE:
			mpKernelLowerROM = mHLEKernelROM;
			mpKernelSelfTestROM = mHLEKernelROM + 0x1000;
			mpKernelUpperROM = mHLEKernelROM + 0x1800;
			break;
		case kATKernelMode_LLE:
			mpKernelLowerROM = NULL;
			mpKernelSelfTestROM = NULL;
			mpKernelUpperROM = mLLEKernelROM;

			if (mbAutoLoadKernelSymbols) {
				VDStringW symbolPath(VDMakePath(VDGetProgramPath().c_str(), L"kernel.lst"));
				if (VDDoesPathExist(symbolPath.c_str()))
					mKernelSymbolsModuleId = deb->LoadSymbols(symbolPath.c_str());
			}
			break;
	}

	// Rehook cassette open vector since it may have changed.
	HookCassetteOpenVector();
}

void ATSimulator::RecomputeMemoryMaps() {
	mpReadMemoryMap = mReadMemoryMap;
	mpCPUReadPageMap = mReadMemoryMap;
	mpWriteMemoryMap = mWriteMemoryMap;
	mpCPUWritePageMap = mWriteMemoryMap;
	mpAnticMemoryMap = mAnticMemoryMap;

	const uint8 portb = GetBankRegister();

	// 0000-3FFF	16K		memory
	for(int i=0; i<64; ++i) {
		mReadMemoryMap[i] = mMemory + (i << 8);
		mWriteMemoryMap[i] = mMemory + (i << 8);
	}

	// 4000-7FFF	16K		memory (bank switched; XE only)
	uint8 *bankbase = mMemory + 0x4000;
	if (mMemoryMode >= kATMemoryMode_128K && !(portb & 0x10)) {
		bankbase = mMemory + ((portb & 0x0c) << 12) + 0x10000;

		if (mMemoryMode >= kATMemoryMode_320K) {
			bankbase += (uint32)(~portb & 0x60) << 11;

			if (mMemoryMode >= kATMemoryMode_576K) {
				if (!(portb & 0x02))
					bankbase += 0x40000;
				if (mMemoryMode >= kATMemoryMode_1088K) {
					if (!(portb & 0x80))
						bankbase += 0x80000;
				}
			}
		}
	}

	for(int i=0; i<64; ++i) {
		mReadMemoryMap[i+64] = bankbase + (i << 8);
		mWriteMemoryMap[i+64] = bankbase + (i << 8);
	}

	// 8000-9FFF	8K		Cart B
	if (!mpCartridge || !mpCartridge->WriteMemoryMap89(mReadMemoryMap + 0x80, mWriteMemoryMap + 0x80, mDummyRead, mDummyWrite)) {
		uint8 *base = mMemory + 0x8000;

		for(int i=0; i<32; ++i) {
			mReadMemoryMap[i+128] = base + (i << 8);
			mWriteMemoryMap[i+128] = base + (i << 8);
		}
	}

	// A000-BFFF	8K		Cart A / BASIC
	//
	// The cartridge has priority over BASIC, due to the logic in U3 (MMU).
	if (!mpCartridge || !mpCartridge->WriteMemoryMapAB(mReadMemoryMap + 0xA0, mWriteMemoryMap + 0xA0, mDummyRead, mDummyWrite)) {
		bool enableBasic = mbBASICEnabled;

		if (mHardwareMode == kATHardwareMode_800XL) {
			enableBasic = true;
			if (portb & 2)
				enableBasic = false;
		}

		if (enableBasic) {
			for(int i=0; i<32; ++i) {
				mReadMemoryMap[i+0xA0] = mBASICROM + (i << 8);
				mWriteMemoryMap[i+0xA0] = mDummyWrite;
			}
		} else {
			uint8 *base = mMemory + 0xA000;

			for(int i=0; i<32; ++i) {
				mReadMemoryMap[i+0xA0] = base + (i << 8);
				mWriteMemoryMap[i+0xA0] = base + (i << 8);
			}
		}
	}

	// C000-CFFF	4K		Memory or Kernel ROM (XL/XE only)
	if ((portb & 0x01) && mHardwareMode == kATHardwareMode_800XL) {
		if (mpKernelLowerROM) {
			for(int i=0; i<16; ++i)
				mReadMemoryMap[i+0xC0] = mpKernelLowerROM + (i << 8);
		} else {
			for(int i=0; i<16; ++i)
				mReadMemoryMap[i+0xC0] = mDummyRead;
		}

		for(int i=0; i<16; ++i)
			mWriteMemoryMap[i+0xC0] = mDummyWrite;

		mpHLEKernel->EnableLowerROM(true);
	} else if (mMemoryMode >= kATMemoryMode_52K) {
		uint8 *base = mMemory + 0xC000;
		for(int i=0; i<16; ++i) {
			mReadMemoryMap[i+0xC0] = base + (i << 8);
			mWriteMemoryMap[i+0xC0] = base + (i << 8);
		}

		mpHLEKernel->EnableLowerROM(false);
	} else {
		VDMemsetPointer(mReadMemoryMap + 0xC0, mDummyRead, 16);
		VDMemsetPointer(mWriteMemoryMap + 0xC0, mDummyWrite, 16);
	}

	// D000-D7FF	2K		hardware address
	VDMemsetPointer(mReadMemoryMap + 0xD0, 0, 8);
	VDMemsetPointer(mWriteMemoryMap + 0xD0, 0, 8);

	// D800-FFFF	10K		kernel ROM
	if (mMemoryMode < kATMemoryMode_64K || (portb & 0x01)) {
		const uint8 *base = mpKernelUpperROM;
		for(int i=0; i<40; ++i) {
			mReadMemoryMap[i+0xD8] = base + (i << 8);
			mWriteMemoryMap[i+0xD8] = mDummyWrite;
		}

		mpHLEKernel->EnableUpperROM(true);
	} else {
		uint8 *base = mMemory + 0xD800;
		for(int i=0; i<40; ++i) {
			mReadMemoryMap[i+0xD8] = base + (i << 8);
			mWriteMemoryMap[i+0xD8] = base + (i << 8);
		}

		mpHLEKernel->EnableUpperROM(false);
	}

	memcpy(mAnticMemoryMap, mReadMemoryMap, sizeof mAnticMemoryMap);

	uint8 *anticbankbase = mMemory + 0x4000;
	if (mMemoryMode == kATMemoryMode_320K) {
		// Base33 requires the ability to map ANTIC along with the CPU.
		if (!(portb & 0x10)) {
			anticbankbase = mMemory + ((portb & 0x0c) << 12) + 0x10000;
			anticbankbase += (uint32)(~portb & 0x60) << 11;
		}
	} else if (mMemoryMode == kATMemoryMode_128K) {
		if (!(portb & 0x20))
			anticbankbase = mMemory + ((portb & 0x0c) << 12) + 0x10000;
	}

	for(int i=0; i<64; ++i)
		mAnticMemoryMap[i+64] = anticbankbase + (i << 8);

	// 5000-57FF	2K		self test memory (bank switched; XL/XE only)
	//
	// NOTE: The kernel ROM must be enabled for the self-test ROM to appear.
	// Storm breaks if this is not checked.
	if (mMemoryMode >= kATMemoryMode_64K && mMemoryMode != kATMemoryMode_1088K && (portb & 0x81) == 0x01 && mpKernelSelfTestROM) {
		for(int i=0; i<8; ++i)
			mAnticMemoryMap[i+80] = mReadMemoryMap[i+80] = mpKernelSelfTestROM + (i << 8);
		mpHLEKernel->EnableSelfTestROM(true);

		for(int i=0; i<8; ++i)
			mWriteMemoryMap[i+80] = mDummyWrite;
	} else {
		mpHLEKernel->EnableSelfTestROM(false);
	}

	mpBPReadPage = NULL;
	if (mReadBreakAddress == (uint16)mReadBreakAddress) {
		mpBPReadPage = mReadMemoryMap[mReadBreakAddress >> 8];
		mReadMemoryMap[mReadBreakAddress >> 8] = NULL;
	}

	mpBPWritePage = NULL;
	if (mWriteBreakAddress == (uint16)mWriteBreakAddress) {
		mpBPWritePage = mWriteMemoryMap[mWriteBreakAddress >> 8];
		mWriteMemoryMap[mWriteBreakAddress >> 8] = NULL;
	}
}

uint8 ATSimulator::CPUReadByte(uint16 addr) {
	const uint8 *src = mpReadMemoryMap[addr >> 8];
	if (src)
		return src[addr & 0xff];

	if (addr == mReadBreakAddress) {
		mPendingEvent = kATSimEvent_ReadBreakpoint;
	}

	// D000-D7FF	2K		hardware address
	switch(addr & 0xff00) {
	case 0xD000:
		return mGTIA.ReadByte(addr & 0x1f);
	case 0xD200:
		return mPokey.ReadByte(addr & 0x1f);
	case 0xD300:
		switch(addr & 0x03) {
		case 0x00:
			return ReadPortA();
		case 0x01:
			return mPORTBCTL & 0x04 ? mpPortBController->GetPortValue() & (mPORTBOUT | ~mPORTBDDR) : mPORTBDDR;
		case 0x02:
			return mPORTACTL;
		case 0x03:
			return mPORTBCTL;
		}
		return 0;

	case 0xD400:
		return mAntic.ReadByte(addr & 0x0f);

	case 0xD500:
		if (mpCartridge)
			return mpCartridge->ReadByteD5(addr);
		break;

	case 0xD600:
		return (addr & 1) ? 0xD6 : (addr + 0x0F);
	}

	if (((uint32)addr - 0x8000) < 0x4000 && mpCartridge) {
		bool remapRequired = false;
		uint8 v = mpCartridge->ReadByte89AB(addr, remapRequired);

		if (remapRequired) {
			RecomputeMemoryMaps();
			UpdateXLCartridgeLine();	// required since TRIG3 reads /RD5
		}

		return v;
	}

	if (!((mReadBreakAddress ^ addr) & 0xff00)) {
		if (mpBPReadPage)
			return mpBPReadPage[addr & 0xff];
	}
	return 0xFF;
}

uint8 ATSimulator::CPUDebugReadByte(uint16 address) {
	return DebugReadByte(address);
}

void ATSimulator::CPUWriteByte(uint16 addr, uint8 value) {
	uint8 *dst = mpWriteMemoryMap[addr >> 8];
	if (dst) {
		dst[addr & 0xff] = value;
		return;
	}

	if (addr == mWriteBreakAddress) {
		mPendingEvent = kATSimEvent_WriteBreakpoint;
	}

	switch(addr & 0xff00) {
	case 0xD000:
		mGTIA.WriteByte(addr & 0x1f, value);
		break;
	case 0xD200:
		mPokey.WriteByte(addr & 0x1f, value);
		break;
	case 0xD300:
		switch(addr & 0x03) {
		case 0x00:
			if (mPORTACTL & 0x04)
				mPORTAOUT = value;
			else
				mPORTADDR = value;
			break;
		case 0x01:
			{
				const uint8 prevBank = GetBankRegister();

				if (mPORTBCTL & 0x04)
					mPORTBOUT = value;
				else
					mPORTBDDR = value;

				const uint8 currBank = GetBankRegister();

				if (prevBank != currBank) {
					if (mMemoryMode >= kATMemoryMode_64K)
						RecomputeMemoryMaps();
				}
			}
			break;
		case 0x02:
			mPORTACTL = value;
			{
				// bits 3-5:
				//	0xx		input (passively pulled high)
				//	100		output - set high by interrupt and low by port A data read
				//	101		output - normally set high, pulse low for one cycle by port A data read
				//	110		output - low
				//	111		output - high
				//
				// Right now we don't emulate the interrupt or pulse modes.

				bool state = (value & 0x38) == 0x30;

				if (mpCassette)
					mpCassette->SetMotorEnable(state);

				mPokey.SetAudioLine2(state ? 32 : 0);
			}
			break;
		case 0x03:
			mPORTBCTL = value;

			// not quite correct -- normally set 0x34 (high) or 0x3c (low)
			mPokey.SetCommandLine(!(value & 8));
			break;
		}
		break;
	case 0xD400:
		mAntic.WriteByte(addr & 0x0f, value);
		break;
	case 0xD500:
		if (mpCartridge) {
			if (mpCartridge->WriteByteD5(addr, value)) {
				RecomputeMemoryMaps();
				UpdateXLCartridgeLine();	// required since TRIG3 reads /RD5
			}
		}
		return;
	}

	if (((uint32)addr - 0x8000) < 0xC000 && mpCartridge) {
		if (mpCartridge->WriteByte89AB(addr, value)) {
			RecomputeMemoryMaps();
			UpdateXLCartridgeLine();	// required since TRIG3 reads /RD5
		}
		return;
	}

	if (!((mWriteBreakAddress ^ addr) & 0xff00)) {
		if (mpBPWritePage)
			mpBPWritePage[addr & 0xff] = value;
	}
}

uint32 ATSimulator::GetTimestamp() {
	return mAntic.GetTimestamp();
}

uint8 ATSimulator::CPUHookHit(uint16 address) {
	if (address == 0x1FE) {		// NOP for program load
		return 0x60;
	} else if (address == 0x1FF) {		// program load continuation
		return LoadProgramHookCont();
	}

	// don't hook ROM if ROM is disabled
	if (mMemoryMode >= kATMemoryMode_64K && !(GetBankRegister() & 0x01))
		return 0;

	if (address == ATKernelSymbols::DSKINV) {		// DSKINV
		if (mbProgramLoadPending)
			return LoadProgramHook();

		uint8 drive = ReadByte(0x0301);
		uint8 cmd = ReadByte(0x0302);
		uint16 bufadr = ReadByte(0x0304) + 256*ReadByte(0x0305);
		uint16 sector = ReadByte(0x030A) + 256*ReadByte(0x030B);

		if (drive >= 1 && drive <= 4) {
			ATDiskEmulator& disk = mDiskDrives[drive - 1];

			if (!disk.IsEnabled())
				return 0;

			if (cmd == 0x52) {
				// Check if we have a non-standard sector size. If so, see if DSCTLN has been set to
				// match. This variable only exists on the XL and upper BIOSes. Otherwise, punt.
				uint32 secSize = disk.GetSectorSize(sector);
				if (secSize != 128) {
					uint32 dsctln = ReadByte(0x2D5) + ((uint32)ReadByte(0x2D6) << 8);
					if (dsctln != secSize)
						return 0;
				}

				uint8 status = disk.ReadSector(bufadr, secSize, sector, this);

				WriteByte(0x0303, status);
				WriteByte(0x0030, status);

				// Set DDEVIC and DUNIT
				WriteByte(ATKernelSymbols::DDEVIC, 0x31);
				WriteByte(ATKernelSymbols::DUNIT, drive);

				// copy DBUFLO/DBUFHI + size -> BUFRLO/BUFRHI
				uint32 endAddr = bufadr + secSize;
				WriteByte(ATKernelSymbols::BUFRLO, (uint8)endAddr);
				WriteByte(ATKernelSymbols::BUFRHI, (uint8)(endAddr >> 8));

				// Set DBYTLO/DBYTHI to transfer length (required for last section of JOYRIDE part A to load)
				WriteByte(0x0308, (uint8)secSize);
				WriteByte(0x0309, (uint8)(secSize >> 8));

				// SIO does an LDY on the status byte before returning -- BIOS code checks N.
				// Might as well set Z too.
				uint8 p = mCPU.GetP() & 0x7d;
				if (!status)
					p |= AT6502::kFlagZ;
				if (status & 0x80)
					p |= AT6502::kFlagN;

				// Carry flag is set by CMP instruction on command (required by AE).
				p |= AT6502::kFlagC;

				mCPU.SetP(p);
				mCPU.SetY(status);

				// Dimension X relies on the A return value from a successful disk
				// read being set to the read command ($52), since it wipes the DCB
				// area at $0300 with it and doesn't set the command for the next
				// call to DSKINV.
				mCPU.SetA(cmd);

				// Advance RTCLOK by four frames (required by several loaders that check RTCLOK for delay).
				ATKernelDatabase kdb(this);
				uint8 rtc0 = kdb.RTCLOK;
				if (rtc0 >= 0xfc) {
					uint8 rtc1 = kdb.RTCLOK[1];
					if (rtc1 == 0xff)
						kdb.RTCLOK[2] = kdb.RTCLOK[2] + 1;

					kdb.RTCLOK[1] = rtc1;
				}

				kdb.RTCLOK = rtc0 + 4;

				// Clear CRITIC.
				kdb.CRITIC = 0;

				ATConsoleTaggedPrintf("HOOK: Intercepting DSKINV read: drive=%02X, cmd=%02X, buf=%04X, sector=%04X, status=%02X\n", drive, cmd, bufadr, sector, status);

				mGTIA.SetStatusFlags(1 << (drive - 1));
				mGTIA.ResetStatusFlags(1 << (drive - 1));

				ClearPokeyTimersOnDiskIo();

				return 0x60;
			} else if (cmd == 0x50 || cmd == 0x57) {
				uint8 status = disk.WriteSector(bufadr, 128, sector, this);
				WriteByte(0x0303, status);

				// Set DBYTLO/DBYTHI to transfer length (required for last section of JOYRIDE part A to load)
				WriteByte(0x0308, 0x80);
				WriteByte(0x0309, 0x00);

				// SIO does an LDY on the status byte before returning -- BIOS code checks N.
				// Might as well set Z too.
				uint8 p = mCPU.GetP() & 0x7d;
				if (!status)
					p |= 0x02;
				if (status & 0x80)
					p |= 0x80;
				mCPU.SetP(p);
				mCPU.SetY(status);

				ATConsoleTaggedPrintf("HOOK: Intercepting DSKINV write: drive=%02X, cmd=%02X, buf=%04X, sector=%04X, status=%02X\n", drive, cmd, bufadr, sector, status);

				mGTIA.PulseStatusFlags(1 << (drive - 1));
				ClearPokeyTimersOnDiskIo();
				return 0x60;
			}
		}
	} else if (address == ATKernelSymbols::SIOV) {		// SIOV
		uint8 device = ReadByte(0x0300) + ReadByte(0x0301) - 1;

		if (device >= 0x31 && device < 0x34) {
			if (!mbDiskSIOPatchEnabled)
				return 0;

			ATDiskEmulator& disk = mDiskDrives[device - 0x31];

			if (!disk.IsEnabled())
				return 0;

			uint8 cmd = ReadByte(0x0302);
			if (cmd == 0x52) {		// read
				uint8 dstats = ReadByte(0x0303);
				if ((dstats & 0xc0) != 0x40)
					return 0;

				uint16 bufadr = ReadByte(0x0304) + 256*ReadByte(0x0305);
				uint16 len = ReadByte(0x0308) + 256*ReadByte(0x0309);

				// Abort read acceleration if the buffer overlaps the parameter region.
				// Yes, there is game stupid enough to do this (Formula 1 Racing). Specifically,
				// we need to check if TIMFLG is in the buffer as that will cause the read to
				// prematurely abort.
				if (bufadr <= ATKernelSymbols::TIMFLG && (ATKernelSymbols::TIMFLG - bufadr) < len)
					return 0;

				uint16 sector = ReadByte(0x030A) + 256*ReadByte(0x030B);

				uint8 status = disk.ReadSector(bufadr, len, sector, this);
				WriteByte(0x0303, status);
				WriteByte(0x0030, status);

				// copy DBUFLO/DBUFHI + size -> BUFRLO/BUFRHI
				uint32 endAddr = bufadr + len;
				WriteByte(ATKernelSymbols::BUFRLO, (uint8)endAddr);
				WriteByte(ATKernelSymbols::BUFRHI, (uint8)(endAddr >> 8));

				mCPU.SetY(status);
				mCPU.SetP((mCPU.GetP() & 0x7d) | (status & 0x80) | (!status ? 0x02 : 0x00));

				// Clear CRITIC.
				ATKernelDatabase kdb(this);
				kdb.CRITIC = 0;

				ATConsoleTaggedPrintf("HOOK: Intercepting disk SIO read: buf=%04X, len=%04X, sector=%04X, status=%02X\n", bufadr, len, sector, status);

				mGTIA.PulseStatusFlags(1 << (device - 0x31));
				ClearPokeyTimersOnDiskIo();
				return 0x60;
			} else if (cmd == 0x50 || cmd == 0x57) {
				uint8 dstats = ReadByte(0x0303);
				if ((dstats & 0xc0) != 0x80)
					return 0;

				uint16 bufadr = ReadByte(0x0304) + 256*ReadByte(0x0305);
				uint16 len = ReadByte(0x0308) + 256*ReadByte(0x0309);
				uint16 sector = ReadByte(0x030A) + 256*ReadByte(0x030B);

				uint8 status = disk.WriteSector(bufadr, len, sector, this);
				WriteByte(0x0303, status);
				WriteByte(0x0030, status);

				// copy DBUFLO/DBUFHI + size -> BUFRLO/BUFRHI
				uint32 endAddr = bufadr + len;
				WriteByte(ATKernelSymbols::BUFRLO, (uint8)endAddr);
				WriteByte(ATKernelSymbols::BUFRHI, (uint8)(endAddr >> 8));

				mCPU.SetY(status);
				mCPU.SetP((mCPU.GetP() & 0x7d) | (status & 0x80) | (!status ? 0x02 : 0x00));

				ATConsoleTaggedPrintf("HOOK: Intercepting disk SIO write: buf=%04X, len=%04X, sector=%04X, status=%02X\n", bufadr, len, sector, status);

				mGTIA.PulseStatusFlags(1 << (device - 0x31));
				ClearPokeyTimersOnDiskIo();
				return 0x60;
			}
		} else if (device == 0x5F) {
			if (!mbCassetteSIOPatchEnabled)
				return 0;

			ATCassetteEmulator& cassette = *mpCassette;

			// Check if a read or write is requested
			uint8 dstats = ReadByte(0x0303);
			if (!(dstats & 0x80)) {
				uint16 bufadr = ReadByte(0x0304) + 256*ReadByte(0x0305);
				uint16 len = ReadByte(0x0308) + 256*ReadByte(0x0309);

				uint8 status = cassette.ReadBlock(bufadr, len, this);
				WriteByte(0x0303, status);

				mCPU.SetY(status);
				mCPU.SetP((mCPU.GetP() & 0x7d) | (status & 0x80) | (!status ? 0x02 : 0x00));

				mGTIA.PulseStatusFlags(1 << 4);

				ATConsoleTaggedPrintf("HOOK: Intercepting cassette SIO read: buf=%04X, len=%04X, status=%02X\n", bufadr, len, status);
				return 0x60;
			}
		}
	} else if (address == ATKernelSymbols::CSOPIV || address == mCassetteCIOOpenHandlerHookAddress) {	// cassette OPEN handler
		// read I/O bits in ICAX1Z
		ATKernelDatabase kdb(this);

		if (address == mCassetteCIOOpenHandlerHookAddress) {
			// save ICAX2Z into FTYPE (IRG mode)
			kdb.ICAX2Z = kdb.FTYPE;

			// abort emulation if it's not an open-for-read request
			if ((kdb.ICAX1Z & 0x0C) != 0x04)
				return 0;
		}

		// press play on cassette
		mpCassette->Play();

		// turn motor on by clearing port A control bit 3
		kdb.PACTL &= ~0x08;

		// skip forward by nine seconds
		mpCassette->SkipForward(9.0f);

		// set open mode to read
		kdb.WMODE = 0;

		// set cassette buffer size to 128 bytes and mark it as empty
		kdb.BPTR = 0x80;
		kdb.BLIM = 0x80;

		// clear EOF flag
		kdb.FEOF = 0x00;

		// set success
		mCPU.SetY(0x01);
		mCPU.SetP(mCPU.GetP() & ~(AT6502::kFlagN | AT6502::kFlagZ));
		return 0x60;
	} else if (address == ATKernelSymbols::CIOV) {
		if (!mbCIOHandlersEstablished) {
			mbCIOHandlersEstablished = true;

			if (mpHardDisk && mpHardDisk->IsEnabled()) {
				for(int i=0; i<12; ++i) {
					uint16 hent = ATKernelSymbols::HATABS + 3*i;
					if (!ReadByte(hent)) {
						WriteByte(hent, 'H');
						WriteByte(hent+1, 0x00);
						WriteByte(hent+2, 0xD6);
						break;
					}
				}
			}
		}

		mCPU.SetHook(address, false);
		return 0;
	} else if (address == ATKernelSymbols::AFP) {
		ATAccelAFP(mCPU, *this);
		return 0x60;
	} else if (address == ATKernelSymbols::FASC) {
		ATAccelFASC(mCPU, *this);
		return 0x60;
	} else if (address == ATKernelSymbols::IPF) {
		ATAccelIPF(mCPU, *this);
		return 0x60;
	} else if (address == ATKernelSymbols::FPI) {
		ATAccelFPI(mCPU, *this);
		return 0x60;
	} else if (address == ATKernelSymbols::FADD) {
		ATAccelFADD(mCPU, *this);
		return 0x60;
	} else if (address == ATKernelSymbols::FSUB) {
		ATAccelFSUB(mCPU, *this);
		return 0x60;
	} else if (address == ATKernelSymbols::FMUL) {
		ATAccelFMUL(mCPU, *this);
		return 0x60;
	} else if (address == ATKernelSymbols::FDIV) {
		ATAccelFDIV(mCPU, *this);
		return 0x60;
	} else if (address == ATKernelSymbols::LOG) {
		ATAccelLOG(mCPU, *this);
		return 0x60;
	} else if (address == ATKernelSymbols::SKPSPC) {
		ATAccelSKPSPC(mCPU, *this);
		return 0x60;
	} else if (address == ATKernelSymbols::ISDIGT) {
		ATAccelISDIGT(mCPU, *this);
		return 0x60;
	} else if (address == ATKernelSymbols::PLYEVL) {
		ATAccelPLYEVL(mCPU, *this);
		return 0x60;
	} else if (address == ATKernelSymbols::ZFR0) {
		ATAccelZFR0(mCPU, *this);
		return 0x60;
	} else if (address == ATKernelSymbols::ZF1) {
		ATAccelZF1(mCPU, *this);
		return 0x60;
	} else if (address == ATKernelSymbols::LDBUFA) {
		ATAccelLDBUFA(mCPU, *this);
		return 0x60;
	} else if (address == ATKernelSymbols::FLD0R) {
		ATAccelFLD0R(mCPU, *this);
		return 0x60;
	} else if (address == ATKernelSymbols::FLD0P) {
		ATAccelFLD0P(mCPU, *this);
		return 0x60;
	} else if (address == ATKernelSymbols::FLD1R) {
		ATAccelFLD1R(mCPU, *this);
		return 0x60;
	} else if (address == ATKernelSymbols::FLD1P) {
		ATAccelFLD1P(mCPU, *this);
		return 0x60;
	} else if (address == ATKernelSymbols::FST0R) {
		ATAccelFST0R(mCPU, *this);
		return 0x60;
	} else if (address == ATKernelSymbols::FST0P) {
		ATAccelFST0P(mCPU, *this);
		return 0x60;
	} else if (address == ATKernelSymbols::FMOVE) {
		ATAccelFMOVE(mCPU, *this);
		return 0x60;
	} else if (address >= 0xD610 && address < 0xD620) {
		mpHardDisk->OnCIOVector(&mCPU, this, address - 0xD610);
		return 0x60;
	}

	return 0;
}

uint8 ATSimulator::AnticReadByte(uint16 addr) {
	const uint8 *src = mpAnticMemoryMap[addr >> 8];
	if (src)
		return src[addr & 0xff];

	return ReadByte(addr);
}

void ATSimulator::AnticAssertNMI() {
	mCPU.AssertNMI();
}

void ATSimulator::AnticEndFrame() {
	mPokey.AdvanceFrame();

	if (mAntic.GetAnalysisMode()) {
		mGTIA.SetForcedBorder(true);
		mGTIA.RenderActivityMap(mAntic.GetActivityMap());
	} else {
		mGTIA.SetForcedBorder(false);
	}

	mGTIA.SetCassetteIndicatorVisible(mpCassette->IsLoaded() && mpCassette->IsMotorRunning());
	mGTIA.SetCassettePosition(mpCassette->GetPosition());
	mGTIA.UpdateScreen();

	if (mbBreakOnFrameEnd) {
		mbBreakOnFrameEnd = false;
		PostInterruptingEvent(kATSimEvent_EndOfFrame);
	}

	mpInputManager->Poll();

	// Turn off automatic OPTION key for disabling BASIC once the VBI is enabled.
	if (mStartupDelay && mAntic.IsVBIEnabled()) {
		if (!--mStartupDelay) {
			mGTIA.SetForcedConsoleSwitches(0x0F);

			if (mpCassette->IsLoaded() && mbCassetteAutoBootEnabled && !mbCassetteSIOPatchEnabled) {
				// push a space into the keyboard routine
				mPokey.PushKey(0x21, false);
			}
		}
	}
}

void ATSimulator::AnticEndScanline() {
	int y = mAntic.GetBeamY();

	if (y == mBreakOnScanline) {
		mBreakOnScanline = -1;
		PostInterruptingEvent(kATSimEvent_ScanlineBreakpoint);
	}

	mPokey.AdvanceScanLine();

	if (!(y & 31)) {
		if (mMouseDeltaX) {
			if (mMouseDeltaX < 0) {
				static const uint8 kReverseXorX[4]={ 0x10, 0x20, 0x20, 0x10 };
				mMouseControllerData ^= kReverseXorX[(mMouseControllerData >> 4) & 3];
				++mMouseDeltaX;
			} else {
				static const uint8 kForwardXorX[4]={ 0x20, 0x10, 0x10, 0x20 };
				mMouseControllerData ^= kForwardXorX[(mMouseControllerData >> 4) & 3];
				--mMouseDeltaX;
			}
		}

		if (mMouseDeltaY) {
			if (mMouseDeltaY < 0) {
				static const uint8 kReverseXorY[4]={ 0x40, 0x80, 0x80, 0x40 };
				mMouseControllerData ^= kReverseXorY[(mMouseControllerData >> 6) & 3];
				++mMouseDeltaY;
			} else {
				static const uint8 kForwardXorY[4]={ 0x80, 0x40, 0x40, 0x80 };
				mMouseControllerData ^= kForwardXorY[(mMouseControllerData >> 6) & 3];
				--mMouseDeltaY;
			}
		}
	}

	ATSCHEDULER_ADVANCE(&mSlowScheduler);
}

bool ATSimulator::AnticIsNextCPUCycleWrite() {
	return mCPU.IsNextCycleWrite();
}

uint32 ATSimulator::GTIAGetXClock() {
	return mAntic.GetBeamX() * 2;
}

void ATSimulator::GTIASetSpeaker(bool newState) {
	mPokey.SetSpeaker(newState);
}

void ATSimulator::GTIARequestAnticSync() {
//	mAntic.SyncWithGTIA(-1);
	mAntic.SyncWithGTIA(0);
}

void ATSimulator::PokeyAssertIRQ() {
	mCPU.AssertIRQ();
}

void ATSimulator::PokeyNegateIRQ() {
	mCPU.NegateIRQ();
}

void ATSimulator::PokeyBreak() {
	mbBreak = true;
}

void ATSimulator::OnDiskActivity(uint8 drive, bool active) {
	if (active)
		mGTIA.SetStatusFlags(1 << (drive - 1));
	else
		mGTIA.ResetStatusFlags(1 << (drive - 1));
}

uint8 ATSimulator::LoadProgramHook() {
	// reset run/init addresses to hook location near top of stack
	mMemory[0x02E0] = 0xFE;
	mMemory[0x02E1] = 0x01;
	mMemory[0x02E2] = 0xFE;
	mMemory[0x02E3] = 0x01;

	// turn on load continuation hooks
	mCPU.SetHook(0x1FE, true);
	mCPU.SetHook(0x1FF, true);

	// reset DSKINV stub
	mCPU.SetHook(ATKernelSymbols::DSKINV, mbDiskSIOPatchEnabled);
	mbProgramLoadPending = false;

	// set COLDST so kernel doesn't think we were in the middle of init (required for
	// some versions of Alley Cat)
	mMemory[ATKernelSymbols::COLDST] = 0;

	// load next segment
	return LoadProgramHookCont();
}

uint8 ATSimulator::LoadProgramHookCont() {
	// set INITAD to virtual RTS
	mMemory[0x2E2] = 0xFE;
	mMemory[0x2E3] = 0x01;

	// resume loading segments
	const uint8 *src0 = mProgramToLoad.data();
	const uint8 *src = src0 + mProgramLoadIndex;
	const uint8 *srcEnd = src0 + mProgramToLoad.size();
	for(;;) {
		// check if we're done
		if (srcEnd - src < 4) {
launch:
			// Looks like we're done. Push DSKINV onto the stack and execute the run routine.

			// Set diskette boot flag in BOOT? so Alex's DOSINI handler runs.
			// Note that we need valid code pointed to by the DOSINI vector, since Stealth
			// crashes otherwise.
			const uint8 *rts = (const uint8 *)memchr(mpKernelUpperROM, 0x60, 0x2800);
			if (rts) {
				uint16 rtsAddr = 0xD800 + (rts - mpKernelUpperROM);
				WriteByte(ATKernelSymbols::DOSINI, (uint8)rtsAddr);
				WriteByte(ATKernelSymbols::DOSINI + 1, (uint8)(rtsAddr >> 8));
			} else {
				ATConsolePrintf("EXE: Warning: Unable to find RTS instruction in upper kernel ROM for (DOSINI) vector.\n");
			}

			WriteByte(0x09, ReadByte(0x09) | 0x01);

			// remove hooks
			mCPU.SetHook(0x1FE, false);
			mCPU.SetHook(0x1FF, false);
 
			// retrieve run address
			uint16 runAddr = DebugReadWord(0x2E0);

			// check if the run address points at the RTS -- if so, just go straight to DSKINV
			if (runAddr == 0x1FE)
				mCPU.Jump(0xE453);
			else {
				mCPU.Push(0xE4);
				mCPU.Push(0x53 - 1);
			}

			mCPU.Jump(runAddr);
			ATConsolePrintf("EXE: Launching at %04X\n", DebugReadWord(0x2E0));
			vdfastvector<uint8>().swap(mProgramToLoad);
			return 0;
		}

		// read start/end addresses for this segment
		uint16 start = VDReadUnalignedLEU16(src+0);
		if (start == 0xFFFF) {
			src += 2;
			continue;
		}

		uint16 end = VDReadUnalignedLEU16(src+2);
		src += 4;

		uint32 len = (uint32)(end - start) + 1;
		if (end < start || (uint32)(srcEnd - src) < len) {
			if (end >= start) {
				len = srcEnd - src;
				ATConsoleWrite("WARNING: Invalid Atari executable: bad start/end range.\n");
			} else {
				ATConsoleWrite("ERROR: Invalid Atari executable: bad start/end range.\n");
				goto launch;
			}
		}

		// if this is the first segment, set RUNAD to this segment as a default
		if (mMemory[0x2E0] == 0xFE && mMemory[0x2E1] == 0x01) {
			mMemory[0x2E0] = (uint8)start;
			mMemory[0x2E1] = (uint8)(start >> 8);
		}

		// load segment data into memory
		ATConsolePrintf("EXE: Loading program %04X-%04X to %04X-%04X\n", src-src0, (src-src0)+len-1, start, end);

		for(uint32 i=0; i<len; ++i)
			WriteByte(start + i, src[i]);

		src += len;

		// check if INITAD has changed
		if (mMemory[0x2E2] != 0xFE || mMemory[0x2E3] != 0x01) {
			ATConsolePrintf("EXE: Jumping to %04X\n", DebugReadWord(0x2E2));
			break;
		}
	}

	mProgramLoadIndex = src - src0;

	// push virtual load hook ($01FF) onto stack and jsr through (INITAD)
	mCPU.Push(0x01);
	mCPU.Push(0xFE);

	mCPU.Jump(DebugReadWord(0x2E2));
	return 0;
}

void ATSimulator::UnloadProgramSymbols() {
	IATDebugger *d = ATGetDebugger();

	if (!d)
		return;

	for(int i=0; i<2; ++i) {
		if (!mProgramModuleIds[i])
			continue;

		d->UnloadSymbols(mProgramModuleIds[i]);	
		mProgramModuleIds[i] = 0;
	}
}

void ATSimulator::ClearPokeyTimersOnDiskIo() {
	ATKernelDatabase kdb(this);

	// Kill all audio voices.
	for(int i=0; i<4; ++i)
		kdb.AUDC1[i+i] = 0;

	// Turn off serial interrupts.
	kdb.IRQEN = kdb.POKMSK & 0xC7;

	// Set AUDCTL to 1.79MHz 3, use 16-bit 3+4
	kdb.AUDCTL = 0x28;
}

void ATSimulator::HookCassetteOpenVector() {
	UnhookCassetteOpenVector();

	if (mbCassetteSIOPatchEnabled) {
		// read cassette OPEN vector from kernel ROM
		uint16 openVec = VDReadUnalignedLEU16(&mpKernelUpperROM[ATKernelSymbols::CASETV - 0xD800]) + 1;

		if ((openVec - 0xC000) < 0x4000) {
			mCassetteCIOOpenHandlerHookAddress = openVec;
			mCPU.SetHook(mCassetteCIOOpenHandlerHookAddress, true);
		}
	}
}

void ATSimulator::UnhookCassetteOpenVector() {
	if (mCassetteCIOOpenHandlerHookAddress) {
		mCPU.SetHook(mCassetteCIOOpenHandlerHookAddress, false);
		mCassetteCIOOpenHandlerHookAddress = 0;
	}
}
