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
#include <vd2/system/zip.h>
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
#include "cio.h"
#include "printer.h"
#include "vbxe.h"
#include "rtime8.h"
#include "profiler.h"
#include "verifier.h"
#include "uirender.h"
#include "ide.h"

namespace {
	class PokeyDummyConnection : public IATPokeyEmulatorConnections {
	public:
		void PokeyAssertIRQ() {}
		void PokeyNegateIRQ() {}
		void PokeyBreak() {}
		bool PokeyIsInInterrupt() const { return false; }
		bool PokeyIsKeyPushOK(uint8) const { return true; }
		uint32 PokeyGetTimestamp() const { return 0; }
	};

	PokeyDummyConnection g_pokeyDummyConnection;

	uint32 NoiseFill8(uint8 *dst, size_t count, uint32 seed) {
		while(count--) {
			*dst++ = (uint8)seed;

			uint32 sbits = seed & 0xFF;
			seed = (seed >> 8) ^ (sbits << 24) ^ (sbits << 22) ^ (sbits << 18) ^ (sbits << 17);
		}

		return seed;
	}

	uint32 NoiseFill32(uint32 *dst, size_t count, uint32 seed) {
		uint32 sbits;
		while(count--) {
			*dst++ = seed;

			sbits = seed & 0xFFFF;
			seed = (seed >> 16) ^ (sbits << 16) ^ (sbits << 14) ^ (sbits << 10) ^ (sbits << 9);

			sbits = seed & 0xFFFF;
			seed = (seed >> 16) ^ (sbits << 16) ^ (sbits << 14) ^ (sbits << 10) ^ (sbits << 9);
		}

		return seed;
	}

	uint32 NoiseFill(uint8 *dst, size_t count, uint32 seed) {
		if (count < 8) {
			NoiseFill8(dst, count, seed);
			return seed;
		}

		size_t align = (size_t)-(ptrdiff_t)dst & 7;
		if (align) {
			seed = NoiseFill8(dst, align, seed);
			dst += align;
			count -= align;
		}

		seed = NoiseFill32((uint32 *)dst, count >> 2, seed);
		dst += count & ~(size_t)3;

		size_t remain = count & 3;
		if (remain)
			seed = NoiseFill8(dst, remain, seed);

		return seed;
	}
}

ATSimulator::ATSimulator()
	: mbROMAutoReloadEnabled(false)
	, mbAutoLoadKernelSymbols(true)
	, mPokey(false)
	, mPokey2(true)
	, mpCassette(NULL)
	, mpIDE(NULL)
	, mpJoysticks(NULL)
	, mpCartridge(NULL)
	, mbDiskSectorCounterEnabled(false)
	, mCassetteCIOOpenHandlerHookAddress(0)
	, mKernelSymbolsModuleId(0)
	, mbProgramLoadPending(false)
	, mCallbacksBusy(0)
	, mbCallbacksChanged(false)
	, mpHLEKernel(ATCreateHLEKernel())
	, mpProfiler(NULL)
	, mpVerifier(NULL)
	, mpInputManager(new ATInputManager)
	, mpPortAController(new ATPortController)
	, mpPortBController(new ATPortController)
	, mpPrinter(NULL)
	, mpVBXE(NULL)
	, mpRTime8(NULL)
	, mpUIRenderer(NULL)
{
	mProgramModuleIds[0] = 0;
	mProgramModuleIds[1] = 0;
	mCartModuleIds[0] = 0;
	mCartModuleIds[1] = 0;

	mpHLEKernel->Init(mCPU, *this);

	mMemoryMode = kATMemoryMode_320K;
	mKernelMode = kATKernelMode_OSB;
	SetKernelMode(kATKernelMode_Default);
	mHardwareMode = kATHardwareMode_800XL;

	mCPU.Init(this);
	mCPU.SetHLE(mpHLEKernel->AsCPUHLE());
	mGTIA.Init(this);
	mAntic.Init(this, &mGTIA, &mScheduler);
	mPokey.Init(this, &mScheduler);
	mPokey2.Init(&g_pokeyDummyConnection, &mScheduler);

	mpPortAController->Init(&mGTIA, &mPokey, 0);
	mpPortBController->Init(&mGTIA, &mPokey, 2);

	mpCassette = new ATCassetteEmulator;
	mpCassette->Init(&mPokey, &mScheduler);
	mPokey.SetCassette(mpCassette);

	ATCreateUIRenderer(&mpUIRenderer);
	mGTIA.SetUIRenderer(mpUIRenderer);

	mpHardDisk = ATCreateHardDiskEmulator();
	mpHardDisk->SetUIRenderer(mpUIRenderer);

	mpPrinter = ATCreatePrinterEmulator();

	for(int i=0; i<8; ++i) {
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
	mbRandomFillEnabled = false;
	mbDiskSIOPatchEnabled = false;
	mbCassetteSIOPatchEnabled = false;
	mbCassetteAutoBootEnabled = false;
	mbBASICEnabled = false;
	mbDualPokeys = false;
	mbVBXESharedMemory = false;
	mbVBXEUseD7xx = false;
	mbFastBoot = true;

	mStartupDelay = 0;
	mbCIOHandlersEstablished = false;

	SetDiskSIOPatchEnabled(true);
	SetCassetteSIOPatchEnabled(true);
	SetCassetteAutoBootEnabled(true);

	mBreakOnScanline = -1;

	memset(mDummyRead, 0xFF, sizeof mDummyRead);

	for(int i=0; i<256; ++i) {
		mDummyReadPageTable[i] = mDummyRead;
		mDummyWritePageTable[i] = mDummyWrite;
		mReadBankTable[i] = mDummyReadPageTable;
		mWriteBankTable[i] = mDummyWritePageTable;
	}

	for(int j=0; j<3; ++j) {
		for(int i=0; i<256; ++i) {
			mHighMemoryReadPageTables[j][i] = mMemory + 0x110000 + (i << 8) + (j << 16);
			mHighMemoryWritePageTables[j][i] = mMemory + 0x110000 + (i << 8) + (j << 16);
		}
	}

	mReadBankTable[0] = mReadMemoryMap;
	mWriteBankTable[0] = mWriteMemoryMap;
	mReadBankTable[1] = mHighMemoryReadPageTables[0];
	mWriteBankTable[1] = mHighMemoryWritePageTables[0];
	mReadBankTable[2] = mHighMemoryReadPageTables[1];
	mWriteBankTable[2] = mHighMemoryWritePageTables[1];
	mReadBankTable[3] = mHighMemoryReadPageTables[2];
	mWriteBankTable[3] = mHighMemoryWritePageTables[2];

	mpAnticReadPageMap = mAnticMemoryMap;
	mpCPUReadPageMap = mReadMemoryMap;
	mpCPUWritePageMap = mWriteMemoryMap;
	mpCPUReadBankMap = mReadBankTable;
	mpCPUWriteBankMap = mWriteBankTable;

	ColdReset();
}

ATSimulator::~ATSimulator() {
	mGTIA.SetUIRenderer(NULL);
	mpUIRenderer->Release();

	if (mpCartridge) {
		delete mpCartridge;
		mpCartridge = NULL;
	}

	delete mpHardDisk;
	delete mpJoysticks;
	delete mpIDE;
	delete mpCassette;
	delete mpHLEKernel;
	delete mpInputManager;
	delete mpPortAController;
	delete mpPortBController;
	delete mpPrinter;
}

void ATSimulator::Init(void *hwnd) {
	mpInputManager->Init(&mSlowScheduler, mpPortAController, mpPortBController);

	mpJoysticks = ATCreateJoystickManager();
	if (!mpJoysticks->Init(hwnd, mpInputManager)) {
		delete mpJoysticks;
		mpJoysticks = NULL;
	}
}

void ATSimulator::Shutdown() {
	for(int i=0; i<8; ++i)
		mDiskDrives[i].Flush();

	if (mpJoysticks) {
		delete mpJoysticks;
		mpJoysticks = NULL;
	}

	mpInputManager->Shutdown();

	if (mpVBXE) {
		mGTIA.SetVBXE(NULL);
		delete mpVBXE;
		mpVBXE = NULL;
	}

	if (mpRTime8) {
		delete mpRTime8;
		mpRTime8 = NULL;
	}

	if (mpProfiler) {
		delete mpProfiler;
		mpProfiler = NULL;
	}

	if (mpVerifier) {
		delete mpVerifier;
		mpVerifier = NULL;
	}
}

void ATSimulator::LoadROMs() {
	VDStringW ppath(VDGetProgramPath());
	VDStringW path;

	mbHaveOSBKernel = false;
	mbHaveXLKernel = false;

	ATLoadKernelResource(IDR_NOKERNEL, mOSAKernelROM, 6144, 10240);

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

	ATLoadKernelResource(IDR_NOKERNEL, mOSBKernelROM, 6144, 10240);

	path = VDMakePath(ppath.c_str(), L"atariosb.rom");
	if (VDDoesPathExist(path.c_str())) {
		try {
			VDFile f;
			f.open(path.c_str());
			f.read(mOSBKernelROM, 10240);
			f.close();

			mbHaveOSBKernel = true;
		} catch(const MyError&) {
		}
	}

	ATLoadKernelResource(IDR_NOKERNEL, mXLKernelROM, 0, 16384);

	path = VDMakePath(ppath.c_str(), L"atarixl.rom");
	if (VDDoesPathExist(path.c_str())) {
		try {
			VDFile f;
			f.open(path.c_str());
			f.read(mXLKernelROM, 16384);
			f.close();

			mbHaveXLKernel = true;
		} catch(const MyError&) {
		}
	}

	ATLoadKernelResource(IDR_NOKERNEL, mOtherKernelROM, 0, 16384);

	path = VDMakePath(ppath.c_str(), L"otheros.rom");
	if (VDDoesPathExist(path.c_str())) {
		try {
			VDFile f;
			f.open(path.c_str());
			f.read(mOtherKernelROM, 16384);
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
				else {
					ATLoadKernelResource(IDR_BASIC, mBASICROM, 0, 8192);
					break;
				}
			}

			f.read(mBASICROM, 8192);
			f.close();
		} while(false);
	} catch(const MyError&) {
	}

	ATLoadKernelResource(IDR_KERNEL, mLLEKernelROM, 0, 10240);
	ATLoadKernelResource(IDR_HLEKERNEL, mHLEKernelROM, 0, 16384);

	UpdateKernel();
	RecomputeMemoryMaps();
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

void ATSimulator::SetProfilingEnabled(bool enabled) {
	if (enabled) {
		if (mpProfiler)
			return;

		mpProfiler = new ATCPUProfiler;
		mpProfiler->Init(&mCPU, this, &mScheduler, &mSlowScheduler);
		mCPU.SetProfiler(mpProfiler);
	} else {
		if (!mpProfiler)
			return;

		mCPU.SetProfiler(NULL);
		delete mpProfiler;
		mpProfiler = NULL;
	}
}

void ATSimulator::SetVerifierEnabled(bool enabled) {
	if (enabled) {
		if (mpVerifier)
			return;

		mpVerifier = new ATCPUVerifier;
		mpVerifier->Init(&mCPU, this, this);
		mCPU.SetVerifier(mpVerifier);
	} else {
		if (!mpProfiler)
			return;

		mCPU.SetVerifier(NULL);
		delete mpVerifier;
		mpVerifier = NULL;
	}
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
	UpdateSIOVHook();
}

void ATSimulator::SetDiskSectorCounterEnabled(bool enable) {
	mbDiskSectorCounterEnabled = enable;
}

void ATSimulator::SetCassetteSIOPatchEnabled(bool enable) {
	if (mbCassetteSIOPatchEnabled == enable)
		return;

	mbCassetteSIOPatchEnabled = enable;

	UpdateSIOVHook();
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
	mCPU.SetHook(ATKernelSymbols::NORMALIZE, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::PLYEVL, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::ZFR0, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::ZF1, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::ZFL, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::LDBUFA, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::FLD0R, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::FLD0P, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::FLD1R, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::FLD1P, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::FST0R, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::FST0P, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::FMOVE, mbFPPatchEnabled);
	mCPU.SetHook(ATKernelSymbols::REDRNG, mbFPPatchEnabled);
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

void ATSimulator::SetVBXEEnabled(bool enable) {
	if (enable)	{
		if (!mpVBXE) {
			mpVBXE = new ATVBXEEmulator;
			mpVBXE->Init(mMemory + (mbVBXESharedMemory ? 0x10000 : 0x90000), this);
			mGTIA.SetVBXE(mpVBXE);

			RecomputeMemoryMaps();
		}
	} else {
		if (mpVBXE) {
			mGTIA.SetVBXE(NULL);
			delete mpVBXE;
			mpVBXE = NULL;

			RecomputeMemoryMaps();
		}
	}
}

void ATSimulator::SetVBXESharedMemoryEnabled(bool enable) {
	if (mbVBXESharedMemory == enable)
		return;

	mbVBXESharedMemory = enable;

	if (mpVBXE) {
		SetVBXEEnabled(false);
		SetVBXEEnabled(true);
	}
}

void ATSimulator::SetVBXEAltPageEnabled(bool enable) {
	mbVBXEUseD7xx = enable;
}

void ATSimulator::SetRTime8Enabled(bool enable) {
	if (mpRTime8) {
		if (enable)
			return;

		delete mpRTime8;
		mpRTime8 = NULL;
	} else {
		if (!enable)
			return;

		mpRTime8 = new ATRTime8Emulator;
	}
}

bool ATSimulator::IsPrinterEnabled() const {
	return mpPrinter && mpPrinter->IsEnabled();
}

void ATSimulator::SetPrinterEnabled(bool enable) {
	if (enable && !mpPrinter)
		mpPrinter = ATCreatePrinterEmulator();

	mpPrinter->SetEnabled(enable);
	mpPrinter->SetHookPageByte(mHookPageByte);
	UpdatePrinterHook();
	UpdateCIOVHook();
}

void ATSimulator::SetFastBootEnabled(bool enable) {
	if (mbFastBoot == enable)
		return;

	mbFastBoot = enable;
	UpdateSIOVHook();
}

void ATSimulator::ColdReset() {
	if (mbVBXEUseD7xx) {
		mVBXEPage = 0xD700;
		mHookPage = 0xD600;
		mHookPageByte = 0xD6;
	} else {
		mVBXEPage = 0xD600;
		mHookPage = 0xD700;
		mHookPageByte = 0xD7;
	}

	if (mbROMAutoReloadEnabled) {
		LoadROMs();
		UpdateKernel();
	}

	if (!mbProgramLoadPending)
		UnloadProgramSymbols();

	UnhookCassetteOpenVector();

	mIRQFlags = 0;

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

	for(int i=0; i<8; ++i)
		mDiskDrives[i].Reset();

	if (mpHardDisk)
		mpHardDisk->ColdReset();

	if (mpPrinter) {
		mpPrinter->ColdReset();
		mpPrinter->SetHookPageByte(mHookPageByte);
	}

	// Nuke memory with a deterministic pattern. We used to clear it, but that's not realistic.
	if (mbRandomFillEnabled)
		NoiseFill(mMemory, sizeof mMemory, 0xA702819E);
	else
		memset(mMemory, 0, sizeof mMemory);

	if (mpCartridge)
		mpCartridge->ColdReset();

	if (mpVBXE)
		mpVBXE->ColdReset();

	RecomputeMemoryMaps();
	UpdateXLCartridgeLine();

	mbCIOHandlersEstablished = false;

	UpdateSIOVHook();
	UpdateCIOVHook();
	UpdatePrinterHook();

	const bool hdenabled = (mpHardDisk && mpHardDisk->IsEnabled());
	mCPU.SetHook(mHookPage + 0x30, hdenabled);
	mCPU.SetHook(mHookPage + 0x32, hdenabled);
	mCPU.SetHook(mHookPage + 0x34, hdenabled);
	mCPU.SetHook(mHookPage + 0x36, hdenabled);
	mCPU.SetHook(mHookPage + 0x38, hdenabled);
	mCPU.SetHook(mHookPage + 0x3A, hdenabled);

	// disable program load hook in case it was left on
	mCPU.SetHook(0x01FE, false);
	mCPU.SetHook(0x01FF, false);

	// press option during power on if BASIC is disabled
	// press start during power on if cassette auto-boot is enabled
	uint8 consoleSwitches = 0x0F;

	mStartupDelay = 1;

	if (!mbBASICEnabled && (!mpCartridge || mpCartridge->IsBASICDisableAllowed()))
		consoleSwitches &= ~4;

	if (mpCassette->IsLoaded() && mbCassetteAutoBootEnabled) {
		consoleSwitches &= ~1;
		mStartupDelay = 5;
	}

	mGTIA.SetForcedConsoleSwitches(consoleSwitches);

	HookCassetteOpenVector();

	// Check if we can find and accelerate the kernel boot routines.
	static const uint8 kMemCheckRoutine[]={
		0xA9, 0xFF,	// lda #$ff
		0x91, 0x04,	// sta ($04),y
		0xD1, 0x04,	// cmp ($04),y
		0xF0, 0x02,	// beq $+4
		0x46, 0x01,	// lsr $01
		0xA9, 0x00,	// lda #0
		0x91, 0x04,	// sta ($04),y
		0xD1, 0x04,	// cmp ($04),y
		0xF0, 0x02,	// beq $+4
		0x46, 0x01,	// lsr $01
		0xC8,		// iny
		0xD0, 0xE9	// bne $c2e4
	};

	bool canHookRAMTest = mbFastBoot && (mpKernelLowerROM && !memcmp(mpKernelLowerROM + 0x02E4, kMemCheckRoutine, sizeof kMemCheckRoutine));
	mCPU.SetHook(0xC2E4, canHookRAMTest);

	static const uint8 kChecksumRoutine[]={
		0xa0, 0x00,	// ldy #0
		0x18,       // clc
		0xB1, 0x9E, // lda ($9e),y
		0x65, 0x8B, // adc $8b
		0x85, 0x8B, // sta $8b
		0x90, 0x02, // bcc $ffc4
		0xE6, 0x8C, // inc $8c
		0xE6, 0x9E, // inc $9e
		0xD0, 0x02, // bne $ffca
		0xE6, 0x9F, // inc $9f
		0xA5, 0x9E, // lda $9e
		0xC5, 0xA0, // cmp $a0
		0xD0, 0xE9, // bne $ffb9
		0xA5, 0x9F, // lda $9f
		0xC5, 0xA1, // cmp $a1
		0xD0, 0xE3, // bne $ffb9
	};

	bool canHookChecksumLoop = mbFastBoot && (mpKernelUpperROM && !memcmp(mpKernelUpperROM + (0xFFB7 - 0xD800), kChecksumRoutine, sizeof kChecksumRoutine));
	mCPU.SetHook(0xFFB7, canHookChecksumLoop);
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

	if (mpPrinter)
		mpPrinter->WarmReset();

	if (mpVBXE)
		mpVBXE->WarmReset();

	// disable program load hook in case it was left on
	mCPU.SetHook(0x01FE, false);
	mCPU.SetHook(0x01FF, false);

	mbCIOHandlersEstablished = false;

	UpdateCIOVHook();
}

void ATSimulator::Resume() {
	if (mbRunning)
		return;

	mbRunning = true;
}

void ATSimulator::Suspend() {
	mbRunning = false;
}

void ATSimulator::UnloadAll() {
	UnloadCartridge();

	mpCassette->Unload();

	for(int i=0; i<sizeof(mDiskDrives)/sizeof(mDiskDrives[0]); ++i) {
		mDiskDrives[i].UnloadDisk();
		mDiskDrives[i].SetEnabled(false);
	}
}

bool ATSimulator::Load(const wchar_t *path, bool vrw, bool rw, ATLoadContext *loadCtx) {
	VDFileStream stream(path);

	return Load(path, path, stream, vrw, rw, loadCtx);
}

bool ATSimulator::Load(const wchar_t *origPath, const wchar_t *imagePath, IVDRandomAccessStream& stream, bool vrw, bool rw, ATLoadContext *loadCtx) {
	uint8 header[16];
	long actual = stream.ReadData(header, 16);
	stream.Seek(0);

	const wchar_t *ext = VDFileSplitExt(imagePath);
	if (!vdwcsicmp(ext, L".zip"))
		goto load_as_zip;

	int loadIndex = 0;

	if (loadCtx) {
		if (loadCtx->mLoadIndex >= 0)
			loadIndex = loadCtx->mLoadIndex;

		if (loadCtx->mLoadType == kATLoadType_Cartridge)
			goto load_as_cart;

		if (loadCtx->mLoadType == kATLoadType_Disk)
			goto load_as_disk;

		loadCtx->mLoadType = kATLoadType_Other;
	}

	if (!vdwcsicmp(ext, L".xfd"))
		goto load_as_disk;

	if (!vdwcsicmp(ext, L".bin") || !vdwcsicmp(ext, L".rom"))
		goto load_as_cart;

	if (actual >= 6) {
		sint64 size = stream.Length();

		if (header[0] == 0xFF && header[1] == 0xFF) {
			LoadProgram(origPath, stream);
			return true;
		}

		if (header[0] == 'P' && header[1] == 'K' && header[2] == 0x03 && header[3] == 0x04) {
load_as_zip:
			VDZipArchive ziparch;

			ziparch.Init(&stream);

			sint32 n = ziparch.GetFileCount();

			for(sint32 i=0; i<n; ++i) {
				const VDZipArchive::FileInfo& info = ziparch.GetFileInfo(i);
				const VDStringA& name = info.mFileName;
				const char *ext = VDFileSplitExt(name.c_str());

				if (!vdstricmp(ext, ".bin") ||
					!vdstricmp(ext, ".rom") ||
					!vdstricmp(ext, ".car") ||
					!vdstricmp(ext, ".xfd") ||
					!vdstricmp(ext, ".atr") ||
					!vdstricmp(ext, ".dcm") ||
					!vdstricmp(ext, ".cas") ||
					!vdstricmp(ext, ".wav") ||
					!vdstricmp(ext, ".xex") ||
					!vdstricmp(ext, ".exe") ||
					!vdstricmp(ext, ".obx") ||
					!vdstricmp(ext, ".com")
					)
				{
					IVDStream& innerStream = *ziparch.OpenRawStream(i);
					vdfastvector<uint8> data;

					VDZipStream zs(&innerStream, info.mCompressedSize, !info.mbPacked);

					data.resize(info.mUncompressedSize);
					zs.Read(data.data(), info.mUncompressedSize);

					VDMemoryStream ms(data.data(), data.size());

					return Load(origPath, VDTextAToW(name).c_str(), ms, false, false, loadCtx);
				}
			}

			throw MyError("The zip file \"%ls\" does not contain a recognized file type.", origPath);
		}

		if ((header[0] == 'A' && header[1] == 'T' && header[2] == '8' && header[3] == 'X') ||
			(header[2] == 'P' && header[3] == '3') ||
			(header[2] == 'P' && header[3] == '2') ||
			(header[0] == 0x96 && header[1] == 0x02) ||
			(!(size & 127) && size <= 65535*128 && !_wcsicmp(ext, L".xfd")))
		{
			goto load_as_disk;
		}

		if (actual >= 12
			&& header[0] == 'R' && header[1] == 'I' && header[2] == 'F' && header[3] == 'F'
			&& header[8] == 'W' && header[9] == 'A' && header[10] == 'V' && header[11] == 'E'
			) {
			mpCassette->Load(stream);
			return true;
		}

		if (header[0] == 'F' && header[1] == 'U' && header[2] == 'J' && header[3] == 'I') {
			mpCassette->Load(stream);
			return true;
		}

		if (header[0] == 'C' && header[1] == 'A' && header[2] == 'R' && header[3] == 'T') {
			if (loadCtx)
				loadCtx->mLoadType = kATLoadType_Cartridge;

			return LoadCartridge(origPath, imagePath, stream, loadCtx ? loadCtx->mpCartLoadContext : NULL);
		}

		if (!vdwcsicmp(ext, L".xex")
			|| !vdwcsicmp(ext, L".obx")
			|| !vdwcsicmp(ext, L".exe")
			|| !vdwcsicmp(ext, L".com"))
		{
			LoadProgram(origPath, stream);
			return true;
		}

		if (!vdwcsicmp(ext, L".atr")
			|| !vdwcsicmp(ext, L".dcm"))
		{
load_as_disk:
			if (loadIndex >= 8)
				throw MyError("Invalid disk drive D%d:.\n", loadIndex + 1);

			ATDiskEmulator& drive = mDiskDrives[loadIndex];
			drive.LoadDisk(origPath, imagePath, stream);

			if (rw)
				drive.SetWriteFlushMode(true, true);
			else if (vrw)
				drive.SetWriteFlushMode(true, false);

			return true;
		}

		if (!vdwcsicmp(ext, L".cas")
			|| !vdwcsicmp(ext, L".wav"))
		{
			mpCassette->Load(stream);
			return true;
		}

		if (!vdwcsicmp(ext, L".rom")
			|| !vdwcsicmp(ext, L".car"))
		{
load_as_cart:
			if (loadCtx)
				loadCtx->mLoadType = kATLoadType_Cartridge;

			return LoadCartridge(origPath, imagePath, stream, loadCtx ? loadCtx->mpCartLoadContext : NULL);
		}
	}

	throw MyError("Unable to identify type of file: %ls.", origPath);
}

void ATSimulator::LoadProgram(const wchar_t *symbolHintPath, IVDRandomAccessStream& stream) {
	stream.Seek(2);

	uint32 len = (uint32)stream.Length() - 2;
	mProgramToLoad.resize(len);
	stream.Read(mProgramToLoad.data(), len);
	mProgramLoadIndex = 0;

	// cold restart the system and wait for DSKINV hook to fire
	ColdReset();

	mCPU.SetHook(ATKernelSymbols::DSKINV, true);
	mbProgramLoadPending = true;

	// try to load symbols
	UnloadProgramSymbols();

	IATDebugger *d = ATGetDebugger();

	if (d && symbolHintPath) {
		static const wchar_t *const kSymExts[]={
			L".lst", L".lab", L".lbl"
		};

		VDASSERTCT(sizeof(kSymExts)/sizeof(kSymExts[0]) == sizeof(mProgramModuleIds)/sizeof(mProgramModuleIds[0]));

		VDStringW sympath;
		for(int i=0; i<sizeof(mProgramModuleIds)/sizeof(mProgramModuleIds[0]); ++i) {
			sympath.assign(symbolHintPath, VDFileSplitExt(symbolHintPath));
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

void ATSimulator::UnloadCartridge() {
	IATDebugger *d = ATGetDebugger();

	if (d) {
		for(int i=0; i<3; ++i) {
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
}

bool ATSimulator::LoadCartridge(const wchar_t *path, ATCartLoadContext *loadCtx) {
	ATLoadContext ctx;

	if (loadCtx)
		ctx.mpCartLoadContext = loadCtx;

	ctx.mLoadType = kATLoadType_Cartridge;

	return Load(path, false, false, &ctx);
}

bool ATSimulator::LoadCartridge(const wchar_t *origPath, const wchar_t *imagePath, IVDRandomAccessStream& stream, ATCartLoadContext *loadCtx) {
	UnloadCartridge();

	IATDebugger *d = ATGetDebugger();

	vdautoptr<ATCartridgeEmulator> cartem(new ATCartridgeEmulator);

	if (!cartem->Load(origPath, imagePath, stream, loadCtx))
		return false;

	mpCartridge = cartem.release();

	RecomputeMemoryMaps();
	UpdateXLCartridgeLine();

	if (d && origPath) {
		static const wchar_t *const kSymExts[]={
			L".lst", L".lab", L".lbl"
		};

		VDASSERTCT(sizeof(kSymExts)/sizeof(kSymExts[0]) == sizeof(mCartModuleIds)/sizeof(mCartModuleIds[0]));

		VDStringW sympath;
		for(int i=0; i<sizeof(mCartModuleIds)/sizeof(mCartModuleIds[0]); ++i) {
			sympath.assign(origPath, VDFileSplitExt(origPath));
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

	return true;
}

void ATSimulator::LoadCartridgeSC3D() {
	UnloadCartridge();

	mpCartridge = new ATCartridgeEmulator;
	mpCartridge->LoadSuperCharger3D();
}

void ATSimulator::LoadCartridgeFlash1Mb(bool altbank) {
	UnloadCartridge();

	mpCartridge = new ATCartridgeEmulator;
	mpCartridge->LoadFlash1Mb(altbank);
}

void ATSimulator::LoadCartridgeFlash8Mb() {
	UnloadCartridge();

	mpCartridge = new ATCartridgeEmulator;
	mpCartridge->LoadFlash8Mb();
}

void ATSimulator::LoadIDE(bool d5xx, bool write, uint32 cylinders, uint32 heads, uint32 sectors, const wchar_t *path) {
	mbIDEUseD5xx = d5xx;

	try {
		UnloadIDE();

		mpIDE = new ATIDEEmulator;
		mpIDE->Init(&mScheduler, mpUIRenderer);
		mpIDE->OpenImage(write, cylinders, heads, sectors, path);
	} catch(...) {
		UnloadIDE();
		throw;
	}
}

void ATSimulator::UnloadIDE() {
	if (mpIDE) {
		delete mpIDE;
		mpIDE = NULL;
	}
}

ATSimulator::AdvanceResult ATSimulator::AdvanceUntilInstructionBoundary() {
	for(;;) {
		if (!mbRunning)
			return kAdvanceResult_Stopped;

		if (!mCPU.IsInstructionInProgress())
			return kAdvanceResult_Running;

		ATSimulatorEvent cpuEvent = kATSimEvent_None;

		if (mCPU.GetUnusedCycle())
			cpuEvent = (ATSimulatorEvent)mCPU.Advance();
		else {
			int x = mAntic.GetBeamX();

			if (!x && mAntic.GetBeamY() == 0) {
				mGTIA.BeginFrame(true);
			}

			ATSCHEDULER_ADVANCE(&mScheduler);
			bool busbusy = mAntic.Advance();

			if (!busbusy)
				cpuEvent = (ATSimulatorEvent)mCPU.Advance();

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
			mGTIA.UpdateScreen(true);
			return kAdvanceResult_Stopped;
		}
	}
}

ATSimulator::AdvanceResult ATSimulator::Advance() {
	if (!mbRunning)
		return kAdvanceResult_Stopped;

	ATSimulatorEvent cpuEvent = kATSimEvent_None;

	if (mCPU.GetUnusedCycle())
		cpuEvent = (ATSimulatorEvent)mCPU.Advance();

	if (!cpuEvent) {
		for(int scans = 0; scans < 8; ++scans) {
			int x = mAntic.GetBeamX();

			if (!x && mAntic.GetBeamY() == 0) {
				if (!mGTIA.BeginFrame(false))
					return kAdvanceResult_WaitingForFrame;
			}

			uint32 cycles = 114 - x;

			if (mCPU.GetCPUMode() == kATCPUMode_65C816) {
				while(cycles--) {
					ATSCHEDULER_ADVANCE(&mScheduler);
					bool busbusy = mAntic.Advance();

					if (!busbusy)
						cpuEvent = (ATSimulatorEvent)mCPU.Advance65816();

					if (cpuEvent | mPendingEvent)
						goto handle_event;
				}
			} else {
				while(cycles--) {
					ATSCHEDULER_ADVANCE(&mScheduler);
					bool busbusy = mAntic.Advance();

					if (!busbusy)
						cpuEvent = (ATSimulatorEvent)mCPU.Advance6502();

					if (cpuEvent | mPendingEvent)
						goto handle_event;
				}
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
		mGTIA.UpdateScreen(true);

	return mbRunning ? kAdvanceResult_Running : kAdvanceResult_Stopped;
}

uint8 ATSimulator::DebugReadByte(uint16 addr) const {
	const uint8 *src = mpReadMemoryMap[addr >> 8];
	if (src)
		return src[addr & 0xff];

	// D000-D7FF	2K		hardware address
	uint32 page = addr & 0xff00;
	switch(page) {
	case 0xD000:
		return mGTIA.DebugReadByte(addr & 0x1f);

	case 0xD100:
		if (mpIDE && !mbIDEUseD5xx)
			return mpIDE->DebugReadByte((uint8)addr);

		return 0xFF;

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

	case 0xD500:
		// D5B8-D5BF: R-Time 8
		if (mpRTime8) {
			if ((uint8)(addr - 0xB8) < 8)
				return mpRTime8->DebugReadControl((uint8)addr);
		}

		if (mpIDE && mbIDEUseD5xx && addr < 0xD520)
			return mpIDE->DebugReadByte((uint8)addr);

		return 0xFF;

	case 0xD600:
	case 0xD700:
		if (page == mVBXEPage) {
			if (mpVBXE)
				return mpVBXE->ReadControl((uint8)addr);
		} else {
			// New Rally X demo has a dumb bug where it jumps to $D510 and expects to execute
			// through to the floating point ROM. We throw a JMP instruction in our I/O space
			// to make this work.

			static const uint8 kROM[]={
				0xEA,
				0xEA,
				0x4C,
				0x00,
				0xD8,
			};

			uint8 off = addr & 0xff;
			if (off < 5)
				return kROM[off];

			if (off < 0x80)
				return (off & 1) ? mHookPageByte : (off + 0x0F);
		}
		return 0xFF;

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

uint8 ATSimulator::DebugExtReadByte(uint32 address) {
	address &= 0xffffff;

	if (address < 0x10000)
		return DebugReadByte(address);

	const uint8 *pt = mpCPUReadBankMap[address >> 16][(uint8)(address >> 8)];
	return pt ? pt[address & 0xff] : 0;
}

uint8 ATSimulator::DebugGlobalReadByte(uint32 address) {
	switch(address & kATAddressSpaceMask) {
		case kATAddressSpace_CPU:
			return DebugExtReadByte(address);

		case kATAddressSpace_ANTIC:
			return DebugAnticReadByte(address);

		case kATAddressSpace_PORTB:
			return mMemory[0x10000 + (address & 0xfffff)];

		case kATAddressSpace_VBXE:
			if (!mpVBXE)
				return 0;

			return mpVBXE->GetMemoryBase()[address & 0x7ffff];

		default:
			return 0;
	}
}

uint16 ATSimulator::DebugGlobalReadWord(uint32 address) {
	uint32 atype = address & kATAddressSpaceMask;
	uint32 aoffset = address & kATAddressOffsetMask;

	return (uint32)DebugGlobalReadByte(address)
		+ ((uint32)DebugGlobalReadByte(atype + ((aoffset + 1) & kATAddressOffsetMask)) << 8);
}

uint32 ATSimulator::DebugGlobalRead24(uint32 address) {
	uint32 atype = address & kATAddressSpaceMask;
	uint32 aoffset = address & kATAddressOffsetMask;

	return (uint32)DebugGlobalReadByte(address)
		+ ((uint32)DebugGlobalReadByte(atype + ((aoffset + 1) & kATAddressOffsetMask)) << 8)
		+ ((uint32)DebugGlobalReadByte(atype + ((aoffset + 2) & kATAddressOffsetMask)) << 16);
}

void ATSimulator::DebugGlobalWriteByte(uint32 address, uint8 value) {
	switch(address & kATAddressSpaceMask) {
		case kATAddressSpace_CPU:
			CPUExtWriteByte((uint16)address, (uint8)(address >> 8), value);
			break;

		case kATAddressSpace_ANTIC:
			const_cast<uint8 *>(mpAnticMemoryMap[(uint8)(address >> 8)])[address & 0xff] = value;
			break;

		case kATAddressSpace_PORTB:
			mMemory[0x10000 + (address & 0xfffff)] = value;
			break;

		case kATAddressSpace_VBXE:
			if (mpVBXE)
				mpVBXE->GetMemoryBase()[address & 0x7ffff] = value;
			break;
	}
}

bool ATSimulator::IsKernelROMLocation(uint16 address) const {
	// 5000-57FF
	// C000-CFFF
	// D800-FFFF
	uint32 addr = address;

	// check for self-test ROM
	if ((addr - 0x5000) < 0x0800)
		return mHardwareMode == kATHardwareMode_800XL && (GetBankRegister() & 0x81) == 0x01;

	// check for XL/XE ROM extension
	if ((addr - 0xC000) < 0x1000)
		return mHardwareMode == kATHardwareMode_800XL && (GetBankRegister() & 0x01);

	// check for main kernel ROM
	if ((addr - 0xD800) < 0x2800)
		return mHardwareMode == kATHardwareMode_800 || (GetBankRegister() & 0x01);

	// it's not kernel ROM
	return false;
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
				mode = mbHaveOSBKernel ? kATKernelMode_OSB : kATKernelMode_HLE;
				break;
			case kATHardwareMode_800XL:
				mode = mbHaveXLKernel ? kATKernelMode_XL : kATKernelMode_HLE;
				break;
		}
	}

	mActualKernelMode = mode;

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
		case kATKernelMode_Other:
			mpKernelLowerROM = mOtherKernelROM;
			mpKernelSelfTestROM = mOtherKernelROM + 0x1000;
			mpKernelUpperROM = mOtherKernelROM + 0x1800;
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
	mpWriteMemoryMap = mWriteMemoryMap;
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
		bankbase = mMemory + ((~portb & 0x0c) << 12) + 0x10000;

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

		if (mMemoryMode < kATMemoryMode_576K) {
			// We push up RAMBO memory by 256K in low memory modes to make VBXE
			// emulation easier (it emulates RAMBO in its high 256K).
			bankbase += 0x40000;
		}
	}

	// fill 4000-FFFF with base memory
	for(int i=0; i<192; ++i) {
		mReadMemoryMap[i+64] = mMemory + 0x4000 + (i << 8);
		mWriteMemoryMap[i+64] = mMemory + 0x4000 + (i << 8);
	}

	// clone ANTIC memory map off CPU map
	memcpy(mAnticMemoryMap, mReadMemoryMap, sizeof mAnticMemoryMap);

	// set CPU banking
	for(int i=0; i<64; ++i) {
		mReadMemoryMap[i+64] = bankbase + (i << 8);
		mWriteMemoryMap[i+64] = bankbase + (i << 8);
	}

	// set ANTIC banking
	uint8 *anticbankbase = mMemory + 0x4000;
	if (mMemoryMode == kATMemoryMode_320K) {
		// Base33 requires the ability to map ANTIC along with the CPU.
		if (!(portb & 0x10)) {
			anticbankbase = mMemory + ((~portb & 0x0c) << 12) + 0x10000;
			anticbankbase += (uint32)(~portb & 0x60) << 11;
			// We push up RAMBO memory by 256K in low memory modes to make VBXE
			// emulation easier (it emulates RAMBO in its high 256K).
			anticbankbase += 0x40000;
		}
	} else if (mMemoryMode == kATMemoryMode_128K) {
		if (!(portb & 0x20)) {
			anticbankbase = mMemory + ((~portb & 0x0c) << 12) + 0x10000;

			// We push up RAMBO memory by 256K in low memory modes to make VBXE
			// emulation easier (it emulates RAMBO in its high 256K).
			anticbankbase += 0x40000;
		}
	}

	for(int i=0; i<64; ++i)
		mAnticMemoryMap[i+64] = anticbankbase + (i << 8);

	// let VBXE remap base memory
	if (mpVBXE)
		mpVBXE->UpdateMemoryMaps(mReadMemoryMap, mWriteMemoryMap, mAnticMemoryMap);

	// 8000-9FFF	8K		Cart B
	if (mpCartridge) {
		mpCartridge->WriteMemoryMap89(mReadMemoryMap + 0x80, mWriteMemoryMap + 0x80, mAnticMemoryMap + 0x80, mDummyRead, mDummyWrite);
	}

	// A000-BFFF	8K		Cart A / BASIC
	//
	// The cartridge has priority over BASIC, due to the logic in U3 (MMU).
	if (!mpCartridge || !mpCartridge->WriteMemoryMapAB(mReadMemoryMap + 0xA0, mWriteMemoryMap + 0xA0, mAnticMemoryMap + 0xA0, mDummyRead, mDummyWrite)) {
		bool enableBasic = mbBASICEnabled;

		if (mHardwareMode == kATHardwareMode_800XL) {
			enableBasic = false;

			// In 576K and 1088K mode, the BASIC bit is reused as a bank bit if extended memory
			// is enabled.
			if ((portb & 0x10) || (mMemoryMode != kATMemoryMode_1088K && mMemoryMode != kATMemoryMode_576K)) {
				if (!(portb & 2))
					enableBasic = true;
			}
		}

		if (enableBasic) {
			for(int i=0; i<32; ++i) {
				mAnticMemoryMap[i+0xA0] = mReadMemoryMap[i+0xA0] = mBASICROM + (i << 8);
				mWriteMemoryMap[i+0xA0] = mDummyWrite;
			}
		}
	}

	// C000-CFFF	4K		Memory or Kernel ROM (XL/XE only)
	if ((portb & 0x01) && mHardwareMode == kATHardwareMode_800XL) {
		if (mpKernelLowerROM) {
			for(int i=0; i<16; ++i)
				mAnticMemoryMap[i+0xC0] = mReadMemoryMap[i+0xC0] = mpKernelLowerROM + (i << 8);
		} else {
			for(int i=0; i<16; ++i)
				mAnticMemoryMap[i+0xC0] = mReadMemoryMap[i+0xC0] = mDummyRead;
		}

		for(int i=0; i<16; ++i)
			mWriteMemoryMap[i+0xC0] = mDummyWrite;

		mpHLEKernel->EnableLowerROM(true);
	} else if (mMemoryMode >= kATMemoryMode_52K) {
		mpHLEKernel->EnableLowerROM(false);
	} else {
		VDMemsetPointer(mAnticMemoryMap + 0xC0, mDummyRead, 16);
		VDMemsetPointer(mReadMemoryMap + 0xC0, mDummyRead, 16);
		VDMemsetPointer(mWriteMemoryMap + 0xC0, mDummyWrite, 16);
	}

	// D000-D7FF	2K		hardware address
	VDMemsetPointer(mAnticMemoryMap + 0xD0, 0, 8);
	VDMemsetPointer(mReadMemoryMap + 0xD0, 0, 8);
	VDMemsetPointer(mWriteMemoryMap + 0xD0, 0, 8);

	// D800-FFFF	10K		kernel ROM
	if (mMemoryMode < kATMemoryMode_64K || (portb & 0x01)) {
		const uint8 *base = mpKernelUpperROM;
		for(int i=0; i<40; ++i) {
			mAnticMemoryMap[i+0xD8] = base + (i << 8);
			mReadMemoryMap[i+0xD8] = base + (i << 8);
			mWriteMemoryMap[i+0xD8] = mDummyWrite;
		}

		mpHLEKernel->EnableUpperROM(true);
	} else {
		mpHLEKernel->EnableUpperROM(false);
	}

	// 5000-57FF	2K		self test memory (bank switched; XL/XE only)
	//
	// NOTE: The kernel ROM must be enabled for the self-test ROM to appear.
	// Storm breaks if this is not checked.
	if (mMemoryMode >= kATMemoryMode_64K && (mMemoryMode != kATMemoryMode_1088K || (portb & 0x10)) && (portb & 0x81) == 0x01 && mpKernelSelfTestROM) {
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
	uint32 page = addr & 0xff00;
	switch(addr & 0xff00) {
	case 0xD000:
		return mGTIA.ReadByte(addr & 0x1f);

	case 0xD100:
		if (mpIDE && !mbIDEUseD5xx)
			return mpIDE->ReadByte((uint8)addr);
		break;

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
		// D5B8-D5BF: R-Time 8
		if (mpRTime8) {
			if ((uint8)(addr - 0xB8) < 8)
				return mpRTime8->ReadControl((uint8)addr);
		}

		if (mpIDE && mbIDEUseD5xx && addr < 0xD520)
			return mpIDE->ReadByte((uint8)addr);

		if (mpCartridge)
			return mpCartridge->ReadByteD5(addr);
		break;

	case 0xD600:
	case 0xD700:
		if (page == mVBXEPage) {
			if (mpVBXE)
				return mpVBXE->ReadControl((uint8)addr);
		} else {
			// New Rally X demo has a dumb bug where it jumps to $D510 and expects to execute
			// through to the floating point ROM. We throw a JMP instruction in our I/O space
			// to make this work.

			static const uint8 kROM[]={
				0xEA,
				0xEA,
				0x4C,
				0x00,
				0xD8,
			};

			uint8 off = addr & 0xff;
			if (off < 5)
				return kROM[off];

			if (off < 0x80)
				return (off & 1) ? mHookPageByte : (off + 0x0F);
		}

		break;
	}

	if (!((mReadBreakAddress ^ addr) & 0xff00)) {
		if (mpBPReadPage)
			return mpBPReadPage[addr & 0xff];
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

	return 0xFF;
}

uint8 ATSimulator::CPUDebugReadByte(uint16 address) {
	return DebugReadByte(address);
}

uint8 ATSimulator::CPUDebugExtReadByte(uint16 address, uint8 bank) {
	return DebugExtReadByte((uint32)address + ((uint32)bank << 16));
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

	const uint32 page = addr & 0xff00;
	switch(page) {
	case 0xD000:
		mGTIA.WriteByte(addr & 0x1f, value);

		if (addr >= 0xD080 && mpVBXE)
			mpVBXE->WarmReset();
		break;
	case 0xD100:
		if (mpIDE && !mbIDEUseD5xx)
			mpIDE->WriteByte((uint8)addr, value);
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
		// D5B8-D5BF: R-Time 8
		if (mpRTime8) {
			if ((uint8)(addr - 0xB8) < 8)
				mpRTime8->WriteControl((uint8)addr, value);
		}

		if (mpIDE && mbIDEUseD5xx && addr < 0xd520)
			mpIDE->WriteByte((uint8)addr, value);

		if (mpCartridge) {
			if (mpCartridge->WriteByteD5(addr, value)) {
				RecomputeMemoryMaps();
				UpdateXLCartridgeLine();	// required since TRIG3 reads /RD5
			}
		}
		return;

	case 0xD600:
	case 0xD700:
		if (page == mVBXEPage) {
			if (mpVBXE)
				mpVBXE->WriteControl((uint8)addr, value);
		}
		return;
	}

	if (((uint32)addr - 0x8000) < 0x4000 && mpCartridge) {
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

uint32 ATSimulator::CPUGetCycle() {
	return ATSCHEDULER_GETTIME(&mScheduler);
}

uint32 ATSimulator::CPUGetUnhaltedCycle() {
	return ATSCHEDULER_GETTIME(&mScheduler) - mAntic.GetHaltedCycleCount();
}

uint32 ATSimulator::CPUGetTimestamp() {
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

		if (drive >= 1 && drive <= 8) {
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
				// DISABLED: This breaks Carina v.1 due to RTCLOK being advanced without the VBI handler
				// seeing it.
				ATKernelDatabase kdb(this);
#if 0
				uint8 rtc0 = kdb.RTCLOK;
				if (rtc0 >= 0xfc) {
					uint8 rtc1 = kdb.RTCLOK[1];
					if (rtc1 == 0xff)
						kdb.RTCLOK[2] = kdb.RTCLOK[2] + 1;

					kdb.RTCLOK[1] = rtc1;
				}

				kdb.RTCLOK = rtc0 + 4;
#endif

				// Clear CRITIC.
				kdb.CRITIC = 0;

				ATConsoleTaggedPrintf("HOOK: Intercepting DSKINV read: drive=%02X, cmd=%02X, buf=%04X, sector=%04X, status=%02X\n", drive, cmd, bufadr, sector, status);

				mpUIRenderer->SetStatusFlags(1 << (drive - 1));
				mpUIRenderer->ResetStatusFlags(1 << (drive - 1));

				ClearPokeyTimersOnDiskIo();

				kdb.SKCTL = (kdb.SSKCTL & 0x07) | 0x10;
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

				mpUIRenderer->PulseStatusFlags(1 << (drive - 1));
				ClearPokeyTimersOnDiskIo();

				// leave SKCTL set to asynchronous receive
				ATKernelDatabase kdb(this);
				kdb.SKCTL = (kdb.SSKCTL & 0x07) | 0x10;
				return 0x60;
			}
		}
	} else if (address == ATKernelSymbols::SIOV) {		// SIOV
		uint8 device = ReadByte(0x0300) + ReadByte(0x0301) - 1;

		if (device >= 0x31 && device <= 0x38) {
			if (!mbDiskSIOPatchEnabled)
				return 0;

			ATDiskEmulator& disk = mDiskDrives[device - 0x31];

			if (!disk.IsEnabled()) {
				if (mbFastBoot)
					goto fastbootignore;

				return 0;
			}

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

				mpUIRenderer->PulseStatusFlags(1 << (device - 0x31));
				ClearPokeyTimersOnDiskIo();

				// leave SKCTL set to asynchronous receive
				kdb.SKCTL = (kdb.SSKCTL & 0x07) | 0x10;

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

				mpUIRenderer->PulseStatusFlags(1 << (device - 0x31));
				ClearPokeyTimersOnDiskIo();

				// leave SKCTL set to asynchronous receive
				ATKernelDatabase kdb(this);
				kdb.SKCTL = (kdb.SSKCTL & 0x07) | 0x10;

				return 0x60;
			} else if (cmd == 0x53) {
				uint8 dstats = ReadByte(0x0303);
				if ((dstats & 0xc0) != 0x40)
					return 0;

				uint16 len = ReadByte(0x0308) + 256*ReadByte(0x0309);
				if (len != 4)
					return 0;

				uint16 bufadr = ReadByte(0x0304) + 256*ReadByte(0x0305);

				uint8 data[5];
				disk.ReadStatus(data);

				for(int i=0; i<4; ++i)
					WriteByte(bufadr+i, data[i]);

				WriteByte(ATKernelSymbols::CHKSUM, data[4]);

				const uint8 status = 0x01;
				WriteByte(0x0303, status);
				WriteByte(0x0030, status);

				// copy DBUFLO/DBUFHI + size -> BUFRLO/BUFRHI
				uint32 endAddr = bufadr + len;
				WriteByte(ATKernelSymbols::BUFRLO, (uint8)endAddr);
				WriteByte(ATKernelSymbols::BUFRHI, (uint8)(endAddr >> 8));

				mCPU.SetY(status);
				mCPU.SetP((mCPU.GetP() & 0x7d) | (status & 0x80) | (!status ? 0x02 : 0x00));

				ATConsoleTaggedPrintf("HOOK: Intercepting disk SIO status req.: buf=%04X\n", bufadr);

				mpUIRenderer->PulseStatusFlags(1 << (device - 0x31));
				ClearPokeyTimersOnDiskIo();
				return 0x60;
			} else if (cmd == 0x4E) {
				uint8 dstats = ReadByte(0x0303);
				if ((dstats & 0xc0) != 0x40)
					return 0;

				uint16 len = ReadByte(0x0308) + 256*ReadByte(0x0309);
				if (len != 12)
					return 0;

				uint16 bufadr = ReadByte(0x0304) + 256*ReadByte(0x0305);

				uint8 data[13];
				disk.ReadPERCOMBlock(data);

				for(int i=0; i<12; ++i)
					WriteByte(bufadr+i, data[i]);

				WriteByte(ATKernelSymbols::CHKSUM, data[12]);

				const uint8 status = 0x01;
				WriteByte(0x0303, status);
				WriteByte(0x0030, status);

				// copy DBUFLO/DBUFHI + size -> BUFRLO/BUFRHI
				uint32 endAddr = bufadr + len;
				WriteByte(ATKernelSymbols::BUFRLO, (uint8)endAddr);
				WriteByte(ATKernelSymbols::BUFRHI, (uint8)(endAddr >> 8));

				mCPU.SetY(status);
				mCPU.SetP((mCPU.GetP() & 0x7d) | (status & 0x80) | (!status ? 0x02 : 0x00));

				ATConsoleTaggedPrintf("HOOK: Intercepting disk SIO read PERCOM req.: buf=%04X\n", bufadr);

				mpUIRenderer->PulseStatusFlags(1 << (device - 0x31));
				ClearPokeyTimersOnDiskIo();
				return 0x60;
			}
		} else if (device == 0x4F) {
			if (mbFastBoot) {
fastbootignore:
				ATKernelDatabase kdb(this);

				// return timeout
				uint8 status = 0x8A;
				kdb.STATUS = status;
				kdb.DSTATS = status;

				mCPU.SetY(status);
				mCPU.SetP((mCPU.GetP() & 0x7d) | (status & 0x80) | (!status ? 0x02 : 0x00));
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

				mpUIRenderer->PulseStatusFlags(1 << 8);

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
						WriteByte(hent+1, 0x20);
						WriteByte(hent+2, mHookPageByte);
						break;
					}
				}
			}

			UpdateCIOVHook();
		}

		// validate IOCB index
		uint8 iocbIdx = mCPU.GetX();
		if (!(iocbIdx & 0x0F) && iocbIdx < 0x80) {
			// check if this is an OPEN or other request
			uint8 cmd = ReadByte(ATKernelSymbols::ICCMD + iocbIdx);

			if (cmd == ATCIOSymbols::CIOCmdOpen) {
				// check if filename starts with P
				uint16 addr = ReadByte(ATKernelSymbols::ICBAL + iocbIdx) + ReadByte(ATKernelSymbols::ICBAH + iocbIdx)*256;

				if (ReadByte(addr) == 'P') {
					if (mpPrinter && mpPrinter->IsEnabled()) {
						// scan HATABS and find printer index
						int hatidx = -1;
						for(int i=0; i<12; ++i) {
							uint16 hent = ATKernelSymbols::HATABS + 3*i;
							if (ReadByte(hent) == 'P') {
								hatidx = i;
								break;
							}
						}

						if (hatidx >= 0) {
							uint8 rcode = mpPrinter->OnCIOCommand(&mCPU, this, iocbIdx, cmd);
							if (rcode && mCPU.GetY() == ATCIOSymbols::CIOStatSuccess) {
								WriteByte(ATKernelSymbols::ICHID + iocbIdx, (uint8)hatidx);
								return rcode;
							}
						}
					}
				}
			} else {
				// check character in HATABS and see if it's the printer
				uint8 devid = ReadByte(ATKernelSymbols::ICHID + iocbIdx);
				uint8 dev = ReadByte(ATKernelSymbols::HATABS + devid);

				if (dev == 'P') {
					if (mpPrinter && mpPrinter->IsEnabled())
						return mpPrinter->OnCIOCommand(&mCPU, this, iocbIdx, cmd);
				}
			}
		}

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
	} else if (address == ATKernelSymbols::NORMALIZE) {
		ATAccelNORMALIZE(mCPU, *this);
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
	} else if (address == ATKernelSymbols::ZFL) {
		ATAccelZFL(mCPU, *this);
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
	} else if (address == ATKernelSymbols::REDRNG) {
		ATAccelREDRNG(mCPU, *this);
		return 0x60;
	} else if (address == 0xC2E4) {
		// XL kernel memory test
		//
		// C2E4: A9 FF  LDA #$FF
		// C2E6: 91 04  STA ($04),Y
		// C2E8: D1 04  CMP ($04),Y
		// C2EA: F0 02  BEQ $C2EE
		// C2EC: 46 01  LSR $01
		// C2EE: A9 00  LDA #$00
		// C2F0: 91 04  STA ($04),Y
		// C2F2: D1 04  CMP ($04),Y
		// C2F4: F0 02  BEQ $C2F8
		// C2F6: 46 01  LSR $01
		// C2F8: C8     INY
		// C2F9: D0 E9  BNE $C2E4

		uint32 addr = ReadByte(0x04) + 256*(uint32)ReadByte(0x05);

		bool success = true;

		addr += mCPU.GetY();
		for(uint32 i=mCPU.GetY(); i<256; ++i) {
			WriteByte(addr, 0xFF);
			if (ReadByte(addr) != 0xFF)
				success = false;

			WriteByte(addr, 0x00);
			if (ReadByte(addr) != 0x00)
				success = false;

			++addr;
		}

		if (!success)
			WriteByte(0x01, ReadByte(0x01) >> 1);

		mCPU.SetY(0);
		mCPU.SetA(0);
		mCPU.SetP((mCPU.GetP() & ~AT6502::kFlagN) | AT6502::kFlagZ);
		mCPU.Jump(0xC2FB - 1);
		return 0xEA;
	} else if (address == 0xFFB7) {
		// XL kernel checksum routine
		//
		// FFB7: A0 00   LDY #$00
		// FFB9: 18      CLC
		// FFBA: B1 9E   LDA ($9E),Y
		// FFBC: 65 8B   ADC $8B
		// FFBE: 85 8B   STA $8B
		// FFC0: 90 02   BCC $FFC4
		// FFC2: E6 8C   INC $8C
		// FFC4: E6 9E   INC $9E
		// FFC6: D0 02   BNE $FFCA
		// FFC8: E6 9F   INC $9F
		// FFCA: A5 9E   LDA $9E
		// FFCC: C5 A0   CMP $A0
		// FFCE: D0 E9   BNE $FFB9
		// FFD0: A5 9F   LDA $9F
		// FFD2: C5 A1   CMP $A1
		// FFD4: D0 E3   BNE $FFB9

		uint16 addr = ReadByte(0x9E) + 256*(uint32)ReadByte(0x9F);
		uint32 checksum = ReadByte(0x8B) + 256*(uint32)ReadByte(0x8C);
		uint16 limit = ReadByte(0xA0) + 256*(uint32)ReadByte(0xA1);

		bool success = true;

		do {
			checksum += ReadByte(addr++);
		} while(addr != limit);

		WriteByte(0x9E, (uint8)addr);
		WriteByte(0x9F, (uint8)(addr >> 8));
		WriteByte(0x8B, (uint8)checksum);
		WriteByte(0x8C, (uint8)(checksum >> 8));

		mCPU.SetY(0);
		mCPU.Jump(0xFFD6 - 1);
		return 0xEA;
	} else if ((address - mHookPage) < 0x100) {
		uint32 offset = address - mHookPage;

		if (offset >= 0x30 && offset < 0x40) {
			if (mpHardDisk && mpHardDisk->IsEnabled())
				mpHardDisk->OnCIOVector(&mCPU, this, offset - 0x30);
			return 0x60;
		} else if (offset >= 0x50 && offset < 0x60) {
			if (mpPrinter && mpPrinter->IsEnabled())
				mpPrinter->OnCIOVector(&mCPU, this, offset - 0x50);
			return 0x60;
		}
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

	mpUIRenderer->SetCassetteIndicatorVisible(mpCassette->IsLoaded() && mpCassette->IsMotorRunning());
	mpUIRenderer->SetCassettePosition(mpCassette->GetPosition());
	mGTIA.UpdateScreen(false);

	if (mbBreakOnFrameEnd) {
		mbBreakOnFrameEnd = false;
		PostInterruptingEvent(kATSimEvent_EndOfFrame);
	}

	mpJoysticks->Poll();
	mpInputManager->Poll();

	// Turn off automatic OPTION key for disabling BASIC once the VBI is enabled.
	if (mStartupDelay && mAntic.IsPlayfieldDMAEnabled()) {
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

	ATSCHEDULER_ADVANCE(&mSlowScheduler);
}

bool ATSimulator::AnticIsNextCPUCycleWrite() {
	return mCPU.IsNextCycleWrite();
}

uint32 ATSimulator::GTIAGetXClock() {
	return mAntic.GetBeamX() * 2;
}

uint32 ATSimulator::GTIAGetTimestamp() const {
	return ATSCHEDULER_GETTIME(&mScheduler);
}

void ATSimulator::GTIASetSpeaker(bool newState) {
	mPokey.SetSpeaker(newState);
}

void ATSimulator::GTIARequestAnticSync() {
//	mAntic.SyncWithGTIA(-1);
	mAntic.SyncWithGTIA(0);
}

void ATSimulator::PokeyAssertIRQ() {
	uint32 oldFlags = mIRQFlags;

	mIRQFlags |= kIRQSource_POKEY;

	if (!oldFlags)
		mCPU.AssertIRQ();
}

void ATSimulator::PokeyNegateIRQ() {
	uint32 oldFlags = mIRQFlags;

	mIRQFlags &= ~kIRQSource_POKEY;

	if (oldFlags == kIRQSource_POKEY)
		mCPU.NegateIRQ();
}

void ATSimulator::PokeyBreak() {
	mbBreak = true;
}

bool ATSimulator::PokeyIsInInterrupt() const {
	return (mCPU.GetP() & AT6502::kFlagI) != 0;
}

bool ATSimulator::PokeyIsKeyPushOK(uint8 c) const {
	return mStartupDelay == 0 && (DebugReadByte(ATKernelSymbols::KEYDEL) == 0 || DebugReadByte(ATKernelSymbols::CH1) != c) && DebugReadByte(ATKernelSymbols::CH) == 0xFF;
}

uint32 ATSimulator::PokeyGetTimestamp() const {
	return ATSCHEDULER_GETTIME(&mScheduler);
}

void ATSimulator::OnDiskActivity(uint8 drive, bool active, uint32 sector) {
	if (active) {
		if (mbDiskSectorCounterEnabled)
			mpUIRenderer->SetStatusCounter(drive - 1, sector);
		else
			mpUIRenderer->SetStatusCounter(drive - 1, drive);

		mpUIRenderer->SetStatusFlags(1 << (drive - 1));
	} else
		mpUIRenderer->ResetStatusFlags(1 << (drive - 1));
}

void ATSimulator::VBXERequestMemoryMapUpdate() {
	RecomputeMemoryMaps();
}

void ATSimulator::VBXEAssertIRQ() {
	uint32 oldFlags = mIRQFlags;

	mIRQFlags |= kIRQSource_VBXE;

	if (!oldFlags)
		mCPU.AssertIRQ();
}

void ATSimulator::VBXENegateIRQ() {
	uint32 oldFlags = mIRQFlags;

	mIRQFlags &= ~kIRQSource_VBXE;

	if (oldFlags == kIRQSource_VBXE)
		mCPU.NegateIRQ();
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

			// Last of the Dragons requires X to be set to a particular value to avoid stomping CRITIC.
			// This is what DOS launches with, since it calls SIO right before doing the run.
			mCPU.SetX(0x20);
			WriteByte(ATKernelSymbols::STATUS, 0x01);

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

void ATSimulator::UpdatePrinterHook() {
	const bool prenabled = (mpPrinter && mpPrinter->IsEnabled());
	mCPU.SetHook(mHookPage + 0x56, prenabled);
}

void ATSimulator::UpdateSIOVHook() {
	mCPU.SetHook(ATKernelSymbols::SIOV, mbDiskSIOPatchEnabled || mbCassetteSIOPatchEnabled || mbFastBoot);
}

void ATSimulator::UpdateCIOVHook() {
	const bool hdenabled = (mpHardDisk && mpHardDisk->IsEnabled());
	const bool prenabled = (mpPrinter && mpPrinter->IsEnabled());
	mCPU.SetHook(ATKernelSymbols::CIOV, (hdenabled && !mbCIOHandlersEstablished) || prenabled);
}
