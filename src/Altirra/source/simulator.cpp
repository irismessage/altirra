//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2010 Avery Lee
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
#include "hostdevice.h"
#include "console.h"
#include "hlekernel.h"
#include "joystick.h"
#include "ksyms.h"
#include "kerneldb.h"
#include "debugger.h"
#include "debuggerlog.h"
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
#include "myide.h"
#include "audiomonitor.h"
#include "audiooutput.h"
#include "audiosyncmixer.h"
#include "rs232.h"
#include "cheatengine.h"
#include "mmu.h"
#include "soundboard.h"
#include "slightsid.h"
#include "covox.h"
#include "pclink.h"
#include "pbi.h"
#include "kmkjzide.h"
#include "side.h"
#include "virtualscreen.h"
#include "pokeytables.h"
#include "cpuhookmanager.h"
#include "cpuheatmap.h"
#include "siomanager.h"
#include "hlebasicloader.h"
#include "hleprogramloader.h"
#include "hlefpaccelerator.h"
#include "hlefastboothook.h"
#include "hleciohook.h"
#include "hleutils.h"

namespace {
	class PokeyDummyConnection : public IATPokeyEmulatorConnections {
	public:
		void PokeyAssertIRQ(bool cpuBased) {}
		void PokeyNegateIRQ(bool cpuBased) {}
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

///////////////////////////////////////////////////////////////////////////

class ATSimulator::PrivateData
	: public IATCartridgeCallbacks
{
public:
	PrivateData(ATSimulator& parent)
		: mParent(parent)
	{
	}

	virtual void CartSetAxxxMapped(bool mapped) {
		mParent.UpdateXLCartridgeLine();
	}

	ATSimulator& mParent;
};

///////////////////////////////////////////////////////////////////////////

ATSimulator::ATSimulator()
	: mpPrivateData(new PrivateData(*this))
	, mbROMAutoReloadEnabled(false)
	, mbAutoLoadKernelSymbols(true)
	, mpMemMan(new ATMemoryManager)
	, mpMMU(new ATMMUEmulator)
	, mpPBIManager(new ATPBIManager)
	, mpCPUHookManager(new ATCPUHookManager)
	, mpSimEventManager(new ATSimulatorEventManager)
	, mPokey(false)
	, mPokey2(true)
	, mpAudioOutput(NULL)
	, mpAudioSyncMixer(NULL)
	, mpPokeyTables(new ATPokeyTables)
	, mpAudioMonitor(NULL)
	, mpCassette(NULL)
	, mpIDE(NULL)
	, mpMyIDE(NULL)
	, mpJoysticks(NULL)
	, mbDiskSectorCounterEnabled(false)
	, mKernelSymbolsModuleId(0)
	, mpHLEKernel(ATCreateHLEKernel())
	, mpProfiler(NULL)
	, mpVerifier(NULL)
	, mpHeatMap(NULL)
	, mpMemLayerLoRAM(NULL)
	, mpMemLayerHiRAM(NULL)
	, mpMemLayerExtendedRAM(NULL)
	, mpMemLayerLowerKernelROM(NULL)
	, mpMemLayerUpperKernelROM(NULL)
	, mpMemLayerBASICROM(NULL)
	, mpMemLayerGameROM(NULL)
	, mpMemLayerSelfTestROM(NULL)
	, mpMemLayerHiddenRAM(NULL)
	, mpMemLayerANTIC(NULL)
	, mpMemLayerGTIA(NULL)
	, mpMemLayerPOKEY(NULL)
	, mpMemLayerPIA(NULL)
	, mpMemLayerRT8(NULL)
	, mpMemLayerIDE(NULL)
	, mpMemLayerHook(NULL)
	, mpInputManager(new ATInputManager)
	, mpPortAController(new ATPortController)
	, mpPortBController(new ATPortController)
	, mpLightPen(new ATLightPenPort)
	, mpPrinter(NULL)
	, mpVBXE(NULL)
	, mpVBXEMemory(NULL)
	, mpSoundBoard(NULL)
	, mpSlightSID(NULL)
	, mpCovox(NULL)
	, mpRTime8(NULL)
	, mpRS232(NULL)
	, mpCheatEngine(NULL)
	, mpUIRenderer(NULL)
	, mpPCLink(NULL)
	, mpKMKJZIDE(NULL)
	, mpSIDE(NULL)
	, mpVirtualScreenHandler(NULL)
	, mpSIOManager(new ATSIOManager)
	, mpHLEBasicLoader(NULL)
	, mpHLEProgramLoader(NULL)
	, mpHLEFPAccelerator(NULL)
	, mpHLEFastBootHook(NULL)
	, mpHLECIOHook(NULL)
{
	mCartModuleIds[0] = 0;
	mCartModuleIds[1] = 0;

	mpCartridge[0] = NULL;
	mpCartridge[1] = NULL;

	ATCreateUIRenderer(&mpUIRenderer);

	mpMemMan->Init(mMemory + 0x110000, 3);
	mpHLEKernel->Init(mCPU, *mpMemMan);
	
	mMemoryMode = kATMemoryMode_320K;
	mKernelMode = kATKernelMode_Default;
	mHardwareMode = kATHardwareMode_800XL;

	mpPBIManager->Init(mpMemMan);

	mCPU.Init(mpMemMan, mpCPUHookManager, this);
	mpCPUHookManager->Init(&mCPU, mpMMU, mpPBIManager);

	mCPU.SetHLE(mpHLEKernel->AsCPUHLE());
	mGTIA.Init(this);
	mAntic.Init(this, &mGTIA, &mScheduler);

	mpSIOManager->Init(&mCPU, this);
	mpHLECIOHook = ATCreateHLECIOHook(&mCPU, this);

	mpLightPen->Init(&mAntic);

	mpAudioOutput = ATCreateAudioOutput();
	mpAudioOutput->Init();

	mpAudioSyncMixer = new ATAudioSyncMixer;
	mpAudioSyncMixer->Init(&mScheduler);
	mpAudioOutput->AddSyncAudioSource(mpAudioSyncMixer);

	mPokey.Init(this, &mScheduler, mpAudioOutput, mpPokeyTables);
	mPokey2.Init(&g_pokeyDummyConnection, &mScheduler, NULL, mpPokeyTables);

	mpPortAController->Init(&mGTIA, &mPokey, mpLightPen, 0);
	mpPortBController->Init(&mGTIA, &mPokey, mpLightPen, 2);

	mpCassette = new ATCassetteEmulator;
	mpCassette->Init(&mPokey, &mScheduler, mpAudioOutput);
	mPokey.SetCassette(mpCassette);

	mGTIA.SetUIRenderer(mpUIRenderer);

	mpHostDevice = ATCreateHostDeviceEmulator();
	mpHostDevice->SetUIRenderer(mpUIRenderer);

	mpPrinter = ATCreatePrinterEmulator();

	for(int i=0; i<15; ++i) {
		mDiskDrives[i].Init(i, this, &mScheduler, &mSlowScheduler, mpAudioSyncMixer);
		mPokey.AddSIODevice(&mDiskDrives[i]);
	}

	mPendingEvent = kATSimEvent_None;

	mbBreak = false;
	mbBreakOnFrameEnd = false;
	mbTurbo = false;
	mbFrameSkip = true;
	mGTIA.SetFrameSkip(true);

	mVideoStandard = kATVideoStandard_NTSC;
	mbRandomFillEnabled = false;
	mbDiskSIOPatchEnabled = false;
	mbDiskSIOOverrideDetectEnabled = false;
	mbCassetteSIOPatchEnabled = false;
	mbCassetteAutoBootEnabled = false;
	mbBASICEnabled = false;
	mbFPPatchEnabled = false;
	mbDualPokeys = false;
	mbVBXESharedMemory = false;
	mbVBXEUseD7xx = false;
	mSoundBoardMemBase = 0xD2C0;
	mbFastBoot = true;
	mbKeyboardPresent = true;
	mbForcedSelfTest = false;
	mbMapRAM = false;
	mIDEHardwareMode = kATIDEHardwareMode_MyIDE_D5xx;

	mStartupDelay = 0;

	SetDiskSIOPatchEnabled(true);
	SetCassetteSIOPatchEnabled(true);
	SetCassetteAutoBootEnabled(true);

	mBreakOnScanline = -1;

	mpAnticReadPageMap = mpMemMan->GetAnticMemoryMap();

	mROMImagePaths[kATROMImage_OSA] = L"atariosa.rom";
	mROMImagePaths[kATROMImage_OSB] = L"atariosb.rom";
	mROMImagePaths[kATROMImage_XL] = L"atarixl.rom";
	mROMImagePaths[kATROMImage_1200XL] = L"atari1200xl.rom";
	mROMImagePaths[kATROMImage_Other] = L"otheros.rom";
	mROMImagePaths[kATROMImage_5200] = L"atari5200.rom";
	mROMImagePaths[kATROMImage_Basic] = L"ataribas.rom";
	mROMImagePaths[kATROMImage_KMKJZIDE] = L"hdbios.rom";
}

ATSimulator::~ATSimulator() {
	mGTIA.SetUIRenderer(NULL);

	for(int i=0; i<2; ++i) {
		if (mpCartridge[i]) {
			delete mpCartridge[i];
			mpCartridge[i] = NULL;
		}
	}

	if (mpVirtualScreenHandler) {
		delete mpVirtualScreenHandler;
		mpVirtualScreenHandler = NULL;
	}

	if (mpSIDE) {
		mpSIDE->Shutdown();
		delete mpSIDE;
		mpSIDE = NULL;
	}

	if (mpKMKJZIDE) {
		mpPBIManager->RemoveDevice(mpKMKJZIDE);
		delete mpKMKJZIDE;
		mpKMKJZIDE = NULL;
	}

	delete mpAudioMonitor;
	delete mpRS232;
	delete mpHostDevice;
	delete mpJoysticks;
	delete mpIDE;
	delete mpHLEKernel;
	delete mpInputManager;
	delete mpPortAController;
	delete mpPortBController;
	delete mpLightPen;
	delete mpPrinter;
	delete mpCheatEngine;
	delete mpPokeyTables;

	mpUIRenderer->Release();

	if (mpPBIManager) {
		mpPBIManager->Shutdown();
		delete mpPBIManager;
		mpPBIManager = NULL;
	}

	if (mpHLECIOHook) {
		ATDestroyHLECIOHook(mpHLECIOHook);
		mpHLECIOHook = NULL;
	}

	if (mpSIOManager) {
		mpSIOManager->Shutdown();
		delete mpSIOManager;
		mpSIOManager = NULL;
	}

	if (mpCPUHookManager) {
		mpCPUHookManager->Shutdown();
		delete mpCPUHookManager;
		mpCPUHookManager = NULL;
	}

	if (mpSimEventManager) {
		mpSimEventManager->Shutdown();
		delete mpSimEventManager;
		mpSimEventManager = NULL;
	}

	delete mpMMU;
	mpMMU = NULL;

	ShutdownMemoryMap();
	delete mpMemMan;

	delete mpPrivateData;
}

void ATSimulator::Init(void *hwnd) {
	mpInputManager->Init(&mScheduler, &mSlowScheduler, mpPortAController, mpPortBController, mpLightPen);

	mpJoysticks = ATCreateJoystickManager();
	if (!mpJoysticks->Init(hwnd, mpInputManager)) {
		delete mpJoysticks;
		mpJoysticks = NULL;
	}
}

void ATSimulator::Shutdown() {
	if (mpHLEFastBootHook) {
		ATDestroyHLEFastBootHook(mpHLEFastBootHook);
		mpHLEFastBootHook = NULL;
	}

	if (mpHLEFPAccelerator) {
		ATDestroyHLEFPAccelerator(mpHLEFPAccelerator);
		mpHLEFPAccelerator = NULL;
	}

	if (mpHLEProgramLoader) {
		mpHLEProgramLoader->Shutdown();
		delete mpHLEProgramLoader;
		mpHLEProgramLoader = NULL;
	}

	if (mpHLEBasicLoader) {
		mpHLEBasicLoader->Shutdown();
		delete mpHLEBasicLoader;
		mpHLEBasicLoader = NULL;
	}

	UnloadIDE();

	for(int i=0; i<15; ++i)
		mDiskDrives[i].Flush();

	if (mpJoysticks) {
		delete mpJoysticks;
		mpJoysticks = NULL;
	}

	mpInputManager->Shutdown();

	if (mpVBXE) {
		mGTIA.SetVBXE(NULL);
		mpVBXE->Shutdown();
		delete mpVBXE;
		mpVBXE = NULL;
	}

	if (mpVBXEMemory) {
		VDAlignedFree(mpVBXEMemory);
		mpVBXEMemory = NULL;
	}

	if (mpCovox) {
		delete mpCovox;
		mpCovox = NULL;
	}

	if (mpSlightSID) {
		delete mpSlightSID;
		mpSlightSID = NULL;
	}

	if (mpSoundBoard) {
		mPokey.SetSoundBoard(NULL);
		delete mpSoundBoard;
		mpSoundBoard = NULL;
	}

	if (mpPCLink) {
		mpPCLink->Shutdown();
		delete mpPCLink;
		mpPCLink = NULL;
	}

	if (mpRTime8) {
		delete mpRTime8;
		mpRTime8 = NULL;
	}

	if (mpProfiler) {
		mCPU.SetProfiler(NULL);
		delete mpProfiler;
		mpProfiler = NULL;
	}

	if (mpVerifier) {
		mCPU.SetVerifier(NULL);
		delete mpVerifier;
		mpVerifier = NULL;
	}

	if (mpHeatMap) {
		mCPU.SetHeatMap(NULL);
		delete mpHeatMap;
		mpHeatMap = NULL;
	}

	if (mpCassette) {
		delete mpCassette;
		mpCassette = NULL;
	}

	if (mpAudioSyncMixer) {
		mpAudioOutput->RemoveSyncAudioSource(mpAudioSyncMixer);
		delete mpAudioSyncMixer;
		mpAudioSyncMixer = NULL;
	}

	if (mpAudioOutput) {
		delete mpAudioOutput;
		mpAudioOutput = NULL;
	}
}

void ATSimulator::LoadROMs() {
	VDStringW ppath(VDGetProgramPath());
	VDStringW path;

	mbHaveOSBKernel = false;
	mbHaveXLKernel = false;
	mbHave1200XLKernel = false;
	mbHaveXEGSKernel = false;
	mbHave5200Kernel = false;

	void *arrays[]={
		mOSAKernelROM,
		mOSBKernelROM,
		mXLKernelROM,
		mXEGSKernelROM,
		mOtherKernelROM,
		mBASICROM,
		m5200KernelROM,
		mLLEKernelROM,
		mHLEKernelROM,
		m5200LLEKernelROM,
		mGameROM,
		m1200XLKernelROM,
	};

	static const struct ROMImageDesc {
		sint32	mImageId;
		uint32	mResId;
		uint32	mResOffset;
		uint32	mSize;
	} kROMImageDescs[]={
		{ kATROMImage_OSA,		IDR_NOKERNEL, 6144, 10240 },
		{ kATROMImage_OSB,		IDR_NOKERNEL, 6144, 10240 },
		{ kATROMImage_XL,		IDR_NOKERNEL, 0, 16384 },
		{ kATROMImage_XEGS,		IDR_NOKERNEL, 0, 16384 },
		{ kATROMImage_Other,	IDR_NOKERNEL, 0, 16384 },
		{ kATROMImage_Basic,	IDR_BASIC, 0, 8192 },
		{ kATROMImage_5200,		IDR_5200KERNEL, 0, 2048 },
		{ -1, IDR_KERNEL, 0, 10240 },
		{ -1, IDR_HLEKERNEL, 0, 16384 },
		{ -1, IDR_5200KERNEL, 0, 2048 },
		{ kATROMImage_Game,		IDR_NOGAME, 0, 8192 },
		{ kATROMImage_1200XL,	IDR_NOKERNEL, 0, 16384 },
	};

	for(size_t i=0; i<sizeof(kROMImageDescs)/sizeof(kROMImageDescs[0]); ++i) {
		const ROMImageDesc& desc = kROMImageDescs[i];

		bool success = false;

		if (desc.mImageId >= 0) {
			const wchar_t *relPath = mROMImagePaths[desc.mImageId].c_str();

			if (*relPath) {
				path = VDFileResolvePath(ppath.c_str(), relPath);
				if (VDDoesPathExist(path.c_str())) {
					try {
						VDFile f;
						f.open(path.c_str());
						f.read(arrays[i], desc.mSize);
						f.close();

						success = true;

						if (desc.mImageId == kATROMImage_OSB)
							mbHaveOSBKernel = true;
						else if (desc.mImageId == kATROMImage_XL)
							mbHaveXLKernel = true;
						else if (desc.mImageId == kATROMImage_1200XL)
							mbHave1200XLKernel = true;
						else if (desc.mImageId == kATROMImage_XEGS)
							mbHaveXEGSKernel = true;
						else if (desc.mImageId == kATROMImage_5200)
							mbHave5200Kernel = true;
					} catch(const MyError&) {
					}
				}
			}
		}

		if (!success)
			ATLoadKernelResource(desc.mResId, arrays[i], desc.mResOffset, desc.mSize);
	}

	UpdateKernel();
	InitMemoryMap();
	ReloadIDEFirmware();
}

void ATSimulator::AddCallback(IATSimulatorCallback *cb) {
	mpSimEventManager->AddCallback(cb);
}

void ATSimulator::RemoveCallback(IATSimulatorCallback *cb) {
	mpSimEventManager->RemoveCallback(cb);
}

void ATSimulator::NotifyEvent(ATSimulatorEvent ev) {
	mpSimEventManager->NotifyEvent(ev);
}

bool ATSimulator::IsDiskAccurateTimingEnabled() const {
	return mDiskDrives[0].IsAccurateSectorTimingEnabled();
}

int ATSimulator::GetCartBank(uint32 unit) const {
	return mpCartridge[unit] ? mpCartridge[unit]->GetCartBank() : -1;
}

void ATSimulator::SetProfilingEnabled(bool enabled) {
	if (enabled) {
		if (mpProfiler)
			return;

		mpProfiler = new ATCPUProfiler;
		mpProfiler->Init(&mCPU, mpMemMan, this, &mScheduler, &mSlowScheduler);
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
		mpVerifier->Init(&mCPU, mpMemMan, this);
		mCPU.SetVerifier(mpVerifier);
	} else {
		if (!mpVerifier)
			return;

		mCPU.SetVerifier(NULL);
		delete mpVerifier;
		mpVerifier = NULL;
	}
}

void ATSimulator::SetHeatMapEnabled(bool enabled) {
	if (enabled) {
		if (mpHeatMap)
			return;

		mpHeatMap = new ATCPUHeatMap;
		mCPU.SetHeatMap(mpHeatMap);
	} else {
		if (!mpHeatMap)
			return;

		mCPU.SetHeatMap(NULL);
		delete mpHeatMap;
		mpHeatMap = NULL;
	}
}

void ATSimulator::SetTurboModeEnabled(bool turbo) {
	mbTurbo = turbo;
	mGTIA.SetFrameSkip(mbTurbo || mbFrameSkip);
}

void ATSimulator::SetFrameSkipEnabled(bool frameskip) {
	mbFrameSkip = frameskip;
	mGTIA.SetFrameSkip(mbTurbo || mbFrameSkip);
}

void ATSimulator::SetVideoStandard(ATVideoStandard vs) {
	mVideoStandard = vs;
	
	const bool pal = (vs != kATVideoStandard_NTSC);
	mAntic.SetPALMode(pal);
	mGTIA.SetPALMode(pal);
	mGTIA.SetSECAMMode(vs == kATVideoStandard_SECAM);
}

void ATSimulator::SetMemoryMode(ATMemoryMode mode) {
	if (mMemoryMode == mode)
		return;

	mMemoryMode = mode;
	InitMemoryMap();
}

void ATSimulator::SetKernelMode(ATKernelMode mode) {
	if (mKernelMode == mode)
		return;

	mKernelMode = mode;

	UpdateKernel();
	InitMemoryMap();
}

void ATSimulator::SetHardwareMode(ATHardwareMode mode) {
	if (mHardwareMode == mode)
		return;

	mHardwareMode = mode;

	bool is5200 = mode == kATHardwareMode_5200;

	mPokey.Set5200Mode(is5200);

	if (mpVBXE)
		mpVBXE->Set5200Mode(is5200);

	mpLightPen->SetIgnorePort34(mode == kATHardwareMode_800XL || mode == kATHardwareMode_1200XL || mode == kATHardwareMode_XEGS);

	UpdateXLCartridgeLine();
	
	UpdateKernel();
	InitMemoryMap();
}

void ATSimulator::SetDiskSIOPatchEnabled(bool enable) {
	if (mbDiskSIOPatchEnabled == enable)
		return;

	mbDiskSIOPatchEnabled = enable;

	mpSIOManager->ReinitHooks();
}

void ATSimulator::SetDiskSIOOverrideDetectEnabled(bool enable) {
	if (mbDiskSIOOverrideDetectEnabled == enable)
		return;

	mbDiskSIOOverrideDetectEnabled = enable;
}

void ATSimulator::SetDiskAccurateTimingEnabled(bool enable) {
	for(uint32 i=0; i<sizeof(mDiskDrives)/sizeof(mDiskDrives[0]); ++i)
		mDiskDrives[i].SetAccurateSectorTimingEnabled(enable);
}

void ATSimulator::SetDiskSectorCounterEnabled(bool enable) {
	mbDiskSectorCounterEnabled = enable;
}

void ATSimulator::SetCassetteSIOPatchEnabled(bool enable) {
	if (mbCassetteSIOPatchEnabled == enable)
		return;

	mbCassetteSIOPatchEnabled = enable;

	mpSIOManager->ReinitHooks();
	mpHLECIOHook->ReinitHooks();
}

void ATSimulator::SetCassetteAutoBootEnabled(bool enable) {
	mbCassetteAutoBootEnabled = enable;
}

void ATSimulator::SetFPPatchEnabled(bool enable) {
	if (mbFPPatchEnabled == enable)
		return;

	if (enable) {
		if (!mpHLEFPAccelerator)
			mpHLEFPAccelerator = ATCreateHLEFPAccelerator(&mCPU);
	} else {
		if (mpHLEFPAccelerator) {
			ATDestroyHLEFPAccelerator(mpHLEFPAccelerator);
			mpHLEFPAccelerator = NULL;
		}
	}

	mbFPPatchEnabled = enable;
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
			if (!mbVBXESharedMemory) {
				VDASSERT(!mpVBXEMemory);
				mpVBXEMemory = VDAlignedMalloc(524288, 16);
			}

			mpVBXE = new ATVBXEEmulator;
			mpVBXE->SetRegisterBase(mbVBXEUseD7xx ? 0xD7 : 0xD6);
			mpVBXE->Init(mbVBXESharedMemory ? mMemory + 0x10000 : (uint8 *)mpVBXEMemory, this, mpMemMan);
			mpVBXE->Set5200Mode(mHardwareMode == kATHardwareMode_5200);
			mGTIA.SetVBXE(mpVBXE);
		}
	} else {
		if (mpVBXE) {
			mGTIA.SetVBXE(NULL);
			mpVBXE->Shutdown();
			delete mpVBXE;
			mpVBXE = NULL;
		}

		if (mpVBXEMemory) {
			VDAlignedFree(mpVBXEMemory);
			mpVBXEMemory = NULL;
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

	if (mpVBXE)
		mpVBXE->SetRegisterBase(enable ? 0xD7 : 0xD6);
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

	InitMemoryMap();
}

void ATSimulator::SetPCLinkEnabled(bool enable) {
	if (mpPCLink) {
		if (enable)
			return;

		mpPCLink->Shutdown();
		delete mpPCLink;
		mpPCLink = NULL;
	} else {
		if (!enable)
			return;

		mpPCLink = ATCreatePCLinkDevice();
		mpPCLink->Init(&mScheduler, &mPokey, mpUIRenderer);
	}
}

void ATSimulator::SetSoundBoardEnabled(bool enable) {
	if (enable) {
		if (!mpSoundBoard) {
			mpSoundBoard = new ATSoundBoardEmulator;
			mpSoundBoard->SetMemBase(mSoundBoardMemBase);
			mpSoundBoard->Init(mpMemMan, &mScheduler, mpAudioOutput);
			mPokey.SetSoundBoard(mpSoundBoard);
		}
	} else {
		if (mpSoundBoard) {
			mPokey.SetSoundBoard(NULL);
			delete mpSoundBoard;
			mpSoundBoard = NULL;
		}
	}
}

void ATSimulator::SetSoundBoardMemBase(uint32 membase) {
	mSoundBoardMemBase = membase;

	if (mpSoundBoard)
		mpSoundBoard->SetMemBase(membase);
}

uint32 ATSimulator::GetSoundBoardMemBase() const {
	return mSoundBoardMemBase;
}

void ATSimulator::SetSlightSIDEnabled(bool enable) {
	if (enable) {
		if (!mpSlightSID) {
			mpSlightSID = new ATSlightSIDEmulator;
			mpSlightSID->Init(mpMemMan, &mScheduler, mpAudioOutput);
			mpUIRenderer->SetSlightSID(mpSlightSID);
		}
	} else {
		if (mpSlightSID) {
			mpUIRenderer->SetSlightSID(NULL);
			delete mpSlightSID;
			mpSlightSID = NULL;
		}
	}
}

void ATSimulator::SetCovoxEnabled(bool enable) {
	if (enable) {
		if (!mpCovox) {
			mpCovox = new ATCovoxEmulator;
			mpCovox->Init(mpMemMan, &mScheduler, mpAudioOutput);
		}
	} else {
		if (mpCovox) {
			delete mpCovox;
			mpCovox = NULL;
		}
	}
}

bool ATSimulator::IsRS232Enabled() const {
	return mpRS232 != NULL;
}

void ATSimulator::SetRS232Enabled(bool enable) {
	if (mpRS232) {
		if (enable)
			return;

		delete mpRS232;
		mpRS232 = NULL;
	} else {
		if (!enable)
			return;

		mpRS232 = ATCreateRS232Emulator();
		mpRS232->Init(mpMemMan, &mScheduler, &mSlowScheduler, mpUIRenderer, &mPokey, this);
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

	mpHLECIOHook->ReinitHooks();
}

void ATSimulator::SetFastBootEnabled(bool enable) {
	if (mbFastBoot == enable)
		return;

	mbFastBoot = enable;
	mpSIOManager->ReinitHooks();
}

void ATSimulator::SetKeyboardPresent(bool enable) {
	if (mbKeyboardPresent == enable)
		return;

	mbKeyboardPresent = enable;
	UpdateKeyboardPresentLine();
}

void ATSimulator::SetForcedSelfTest(bool enable) {
	if (mbForcedSelfTest == enable)
		return;

	mbForcedSelfTest = enable;
	UpdateForcedSelfTestLine();
}

void ATSimulator::SetMapRAMEnabled(bool enable) {
	if (mbMapRAM == enable)
		return;

	mbMapRAM = enable;
	InitMemoryMap();
}

void ATSimulator::SetCheatEngineEnabled(bool enable) {
	if (enable) {
		if (mpCheatEngine)
			return;

		mpCheatEngine = new ATCheatEngine;
		mpCheatEngine->Init(mMemory, 49152);
	} else {
		if (mpCheatEngine) {
			delete mpCheatEngine;
			mpCheatEngine = NULL;
		}
	}
}

void ATSimulator::SetAudioMonitorEnabled(bool enable) {
	if (enable) {
		if (!mpAudioMonitor) {
			mpAudioMonitor = new ATAudioMonitor;
			mpAudioMonitor->Init(&mPokey, mpUIRenderer);
		}
	} else {
		if (mpAudioMonitor) {
			mpAudioMonitor->Shutdown();
			delete mpAudioMonitor;
			mpAudioMonitor = NULL;
		}
	}
}

void ATSimulator::SetVirtualScreenEnabled(bool enable) {
	if (enable) {
		if (!mpVirtualScreenHandler) {
			mpVirtualScreenHandler = ATCreateVirtualScreenHandler();
			mpHLECIOHook->ReinitHooks();
		}
	} else {
		if (mpVirtualScreenHandler) {
			delete mpVirtualScreenHandler;
			mpVirtualScreenHandler = NULL;
		}
	}
}

bool ATSimulator::IsKernelAvailable(ATKernelMode mode) const {
	switch(mode) {
		case kATKernelMode_OSB:
			return mbHaveOSBKernel;

		case kATKernelMode_XL:
			return mbHaveXLKernel;

		case kATKernelMode_1200XL:
			return mbHave1200XLKernel;

		case kATKernelMode_5200:
			return mbHave5200Kernel;

		default:
			return true;
	}
}

void ATSimulator::ColdReset() {
	if (mpHLEProgramLoader && !mpHLEProgramLoader->IsLaunchPending()) {
		mpHLEProgramLoader->Shutdown();
		delete mpHLEProgramLoader;
		mpHLEProgramLoader = NULL;
	}

	if (mpHLEBasicLoader && !mpHLEBasicLoader->IsLaunchPending()) {
		mpHLEBasicLoader->Shutdown();
		delete mpHLEBasicLoader;
		mpHLEBasicLoader = NULL;
	}

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

	mIRQFlags = 0;

	// PIA forces all registers to 0 on reset.
	mPORTAOUT	= 0x00;
	mPORTADDR	= 0x00;
	mPORTACTL	= 0x00;
	mPORTBOUT	= 0x00;
	mPORTBDDR	= 0x00;
	mPORTBCTL	= 0x00;
	mbPIAEdgeA = false;
	mbPIAEdgeB = false;
	mPIACB2 = kPIACS_Floating;

	mCPU.ColdReset();
	mAntic.ColdReset();
	mGTIA.ColdReset();
	mPokey.ColdReset();
	mpCassette->ColdReset();

	for(int i=0; i<15; ++i) {
		mDiskDrives[i].Reset();
		mDiskDrives[i].ClearAccessedFlag();
	}

	if (mpHostDevice)
		mpHostDevice->ColdReset();

	if (mpPrinter) {
		mpPrinter->ColdReset();
		mpPrinter->SetHookPageByte(mHookPageByte);
	}

	if (mpVirtualScreenHandler) {
		mpVirtualScreenHandler->ColdReset();
		mpVirtualScreenHandler->SetHookPage(mHookPageByte);
	}

	mpPBIManager->ColdReset();

	// Nuke memory with a deterministic pattern. We used to clear it, but that's not realistic.
	if (mbRandomFillEnabled) {
		NoiseFill(mMemory, sizeof mMemory, 0xA702819E);

		if (mpVBXEMemory)
			NoiseFill((uint8 *)mpVBXEMemory, 0x80000, 0x9B274CA3);
	} else {
		memset(mMemory, 0, sizeof mMemory);

		if (mpVBXEMemory)
			memset(mpVBXEMemory, 0, 0x80000);
	}

	// initialize hook pages
	memset(mHookROM, 0xFF, 0x200);

	for(uint8 page = 0xD6; page <= 0xD7; ++page) {
		uint8 *dst = mHookROM + ((int)(page - 0xD6) << 8);

		for(uint32 i=0; i<128; i += 16) {
			// open
			*dst++ = i + 0x00;
			*dst++ = page;

			// close
			*dst++ = i + 0x02;
			*dst++ = page;

			// get byte
			*dst++ = i + 0x04;
			*dst++ = page;

			// put byte
			*dst++ = i + 0x06;
			*dst++ = page;

			// status
			*dst++ = i + 0x08;
			*dst++ = page;

			// special
			*dst++ = i + 0x0A;
			*dst++ = page;

			// init
			*dst++ = 0x4C;
			*dst++ = i + 0x0D;
			*dst++ = page;

			*dst++ = 0x60;
		}
	}

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

	memcpy(mHookROM, kROM, 5);
	memcpy(mHookROM + 0x100, kROM, 5);

	for(int i=0; i<2; ++i) {
		if (mpCartridge[i])
			mpCartridge[i]->ColdReset();
	}

	if (mpVBXE)
		mpVBXE->ColdReset();

	if (mpSoundBoard)
		mpSoundBoard->ColdReset();

	if (mpSlightSID)
		mpSlightSID->ColdReset();

	if (mpCovox)
		mpCovox->ColdReset();

	if (mpRS232)
		mpRS232->ColdReset();

	if (mpIDE)
		mpIDE->ColdReset();

	if (mpSIDE)
		mpSIDE->ColdReset();

	InitMemoryMap();

	UpdateXLCartridgeLine();
	UpdateKeyboardPresentLine();
	UpdateForcedSelfTestLine();

	mpUIRenderer->SetLedStatus(0);

	if (mHardwareMode == kATHardwareMode_800 || mHardwareMode == kATHardwareMode_5200)
		mGTIA.SetControllerTrigger(3, false);

	if (mHardwareMode != kATHardwareMode_XEGS)
		mGTIA.SetControllerTrigger(2, false);

	if (mHardwareMode != kATHardwareMode_800XL && mHardwareMode != kATHardwareMode_1200XL && mHardwareMode != kATHardwareMode_XEGS)
		mPokey.SetPotPos(4, 228);

	// Reset CIO hook. This must be done after the memory map is reinited.
	mpHLECIOHook->ColdReset(mHookPage, mpKernelUpperROM);

	// press option during power on if BASIC is disabled
	// press start during power on if cassette auto-boot is enabled
	uint8 consoleSwitches = 0x0F;

	mStartupDelay = 1;

	switch(mHardwareMode) {
		case kATHardwareMode_800XL:
		case kATHardwareMode_XEGS:
			if (!mbBASICEnabled && (!mpCartridge[0] || mpCartridge[0]->IsBASICDisableAllowed())) {
				// Don't hold OPTION for a diagnostic cart.
				if (!mpCartridge[0] || DebugReadByte(0xBFFD) < 0x80)
					consoleSwitches &= ~4;
			}

			break;
	}

	if (mpCassette->IsLoaded() && mbCassetteAutoBootEnabled) {
		consoleSwitches &= ~1;
		mStartupDelay = 5;
	}

	mGTIA.SetForcedConsoleSwitches(consoleSwitches);

	if (mpHLEFastBootHook) {
		ATDestroyHLEFastBootHook(mpHLEFastBootHook);
		mpHLEFastBootHook = NULL;
	}

	if (mbFastBoot)
		mpHLEFastBootHook = ATCreateHLEFastBootHook(&mCPU, mpKernelLowerROM, mpKernelUpperROM);

	if (mHardwareMode == kATHardwareMode_5200 && !mpCartridge)
		LoadCartridge5200Default();

	NotifyEvent(kATSimEvent_ColdReset);
}

void ATSimulator::WarmReset() {
	if (mpHLEProgramLoader) {
		mpHLEProgramLoader->Shutdown();
		delete mpHLEProgramLoader;
		mpHLEProgramLoader = NULL;
	}

	if (mpHLEBasicLoader) {
		mpHLEBasicLoader->Shutdown();
		delete mpHLEBasicLoader;
		mpHLEBasicLoader = NULL;
	}

	// The Atari 400/800 route the reset button to /RNMI on ANTIC, which then fires an
	// NMI with the system reset (bit 5) set in NMIST. On the XL series, /RNMI is permanently
	// tied to +5V by pullup and the reset button directly resets the 6502, ANTIC, FREDDIE,
	// and the PIA.
	if (mHardwareMode == kATHardwareMode_800XL || mHardwareMode == kATHardwareMode_1200XL || mHardwareMode == kATHardwareMode_XEGS) {
		// PIA forces all registers to 0 on reset.
		mPORTAOUT	= 0x00;
		mPORTADDR	= 0x00;
		mPORTACTL	= 0x00;
		mPORTBOUT	= 0x00;
		mPORTBDDR	= 0x00;
		mPORTBCTL	= 0x00;
		mbPIAEdgeA = false;
		mbPIAEdgeB = false;
		mPIACB2 = kPIACS_Floating;

		mpMMU->SetBankRegister(0xFF);

		mCPU.WarmReset();
		mAntic.WarmReset();
	} else {
		mAntic.RequestNMI();
	}

	if (mpHostDevice)
		mpHostDevice->WarmReset();

	if (mpPrinter)
		mpPrinter->WarmReset();

	if (mpVirtualScreenHandler)
		mpVirtualScreenHandler->WarmReset();

	if (mpVBXE)
		mpVBXE->WarmReset();

	if (mpSoundBoard)
		mpSoundBoard->WarmReset();

	if (mpSlightSID)
		mpSlightSID->WarmReset();

	if (mpCovox)
		mpCovox->WarmReset();

	mpPBIManager->WarmReset();

	for(size_t i=0; i<sizeof(mDiskDrives)/sizeof(mDiskDrives[0]); ++i)
		mDiskDrives[i].ClearAccessedFlag();

	mpHLECIOHook->WarmReset();
}

void ATSimulator::Resume() {
	if (mbRunning)
		return;

	mbRunning = true;

	if (mPendingEvent == kATSimEvent_AnonymousInterrupt)
		mPendingEvent = kATSimEvent_None;
}

void ATSimulator::Suspend() {
	if (mbRunning) {
		mbRunning = false;

		if (!mPendingEvent)
			mPendingEvent = kATSimEvent_AnonymousInterrupt;
	}
}

void ATSimulator::GetROMImagePath(ATROMImage image, VDStringW& s) const {
	s = mROMImagePaths[image];
}

void ATSimulator::SetROMImagePath(ATROMImage image, const wchar_t *s) {
	mROMImagePaths[image] = s;
}

void ATSimulator::GetDirtyStorage(vdfastvector<ATStorageId>& ids) const {
	for(int i=0; i<sizeof(mDiskDrives)/sizeof(mDiskDrives[0]); ++i) {
		ATStorageId id = (ATStorageId)(kATStorageId_Disk + i);
		if (IsStorageDirty(id))
			ids.push_back(id);
	}

	static const ATStorageId kIds[]={
		kATStorageId_Cartridge,
		kATStorageId_IDEFirmware,
		(ATStorageId)(kATStorageId_IDEFirmware+1),
	};

	for(size_t i=0; i<sizeof(kIds)/sizeof(kIds[0]); ++i) {
		if (IsStorageDirty(kIds[i]))
			ids.push_back(kIds[i]);
	}
}

bool ATSimulator::IsStorageDirty(ATStorageId mediaId) const {
	if (mediaId == kATStorageId_All) {
		if (IsStorageDirty(kATStorageId_Cartridge))
			return true;

		for(int i=0; i<sizeof(mDiskDrives)/sizeof(mDiskDrives[0]); ++i) {
			ATStorageId id = (ATStorageId)(kATStorageId_Disk + i);

			if (IsStorageDirty(id))
				return true;
		}

		if (IsStorageDirty(kATStorageId_IDEFirmware))
			return true;

		if (IsStorageDirty((ATStorageId)(kATStorageId_IDEFirmware + 1)))
			return true;

		return false;
	}

	const uint32 type = mediaId & kATStorageId_TypeMask;
	const uint32 unit = mediaId & kATStorageId_UnitMask;
	switch(type) {
		case kATStorageId_Cartridge:
			if (mpCartridge[unit])
				return mpCartridge[unit]->IsDirty();
			break;

		case kATStorageId_Disk:
			if (unit < sizeof(mDiskDrives)/sizeof(mDiskDrives[0]))
				return mDiskDrives[unit].IsDirty();
			break;

		case kATStorageId_IDEFirmware:
			if (mpKMKJZIDE) {
				switch(unit) {
					case 0: return mpKMKJZIDE->IsMainFlashDirty();
					case 1: return mpKMKJZIDE->IsSDXFlashDirty();
				}
			} else if (mpSIDE) {
				if (unit == 1)
					return mpSIDE->IsSDXFlashDirty();
			}
			break;
	}

	return false;
}

void ATSimulator::UnloadAll() {
	for(int i=0; i<2; ++i)
		UnloadCartridge(i);

	mpCassette->Unload();

	for(int i=0; i<sizeof(mDiskDrives)/sizeof(mDiskDrives[0]); ++i) {
		mDiskDrives[i].UnloadDisk();
		mDiskDrives[i].SetEnabled(false);
	}
}

bool ATSimulator::Load(const wchar_t *path, bool vrw, bool rw, ATLoadContext *loadCtx) {
	VDFileStream stream(path, nsVDFile::kRead | nsVDFile::kDenyWrite | nsVDFile::kOpenExisting | nsVDFile::kSequential);

	return Load(path, path, stream, vrw, rw, loadCtx);
}

bool ATSimulator::Load(const wchar_t *origPath, const wchar_t *imagePath, IVDRandomAccessStream& stream, bool vrw, bool rw, ATLoadContext *loadCtx) {
	uint8 header[16];
	long actual = stream.ReadData(header, 16);
	stream.Seek(0);

	sint64 size = 0;
	int loadIndex = 0;

	const wchar_t *ext = VDFileSplitExt(imagePath);
	if (!vdwcsicmp(ext, L".zip"))
		goto load_as_zip;

	if (!vdwcsicmp(ext, L".gz") || !vdwcsicmp(ext, L"atz"))
		goto load_as_gzip;

	if (loadCtx) {
		if (loadCtx->mLoadIndex >= 0)
			loadIndex = loadCtx->mLoadIndex;

		if (loadCtx->mLoadType == kATLoadType_Cartridge)
			goto load_as_cart;

		if (loadCtx->mLoadType == kATLoadType_Disk)
			goto load_as_disk;

		if (loadCtx->mLoadType == kATLoadType_Tape)
			goto load_as_tape;

		if (loadCtx->mLoadType == kATLoadType_Program)
			goto load_as_program;

		if (loadCtx->mLoadType == kATLoadType_BasicProgram)
			goto load_as_basic_program;

		loadCtx->mLoadType = kATLoadType_Other;
	}

	if (!vdwcsicmp(ext, L".xfd"))
		goto load_as_disk;

	if (!vdwcsicmp(ext, L".bin") || !vdwcsicmp(ext, L".rom") || !vdwcsicmp(ext, L".a52"))
		goto load_as_cart;

	if (actual >= 6) {
		size = stream.Length();

		if (header[0] == 0xFF && header[1] == 0xFF) {
load_as_program:
			LoadProgram(origPath, stream, false);
			return true;
		}

		if (header[0] == 0x1f && header[1] == 0x8b) {
load_as_gzip:
			VDGUnzipStream gzs(&stream, stream.Length());

			vdfastvector<uint8> buffer;

			uint32 size = 0;
			for(;;) {
				// Don't gunzip beyond 64MB.
				if (size >= 0x4000000)
					throw MyError("Gzip stream is too large (exceeds 64MB in size).");

				buffer.resize(size + 1024);

				sint32 actual = gzs.ReadData(buffer.data() + size, 1024);
				if (actual <= 0) {
					buffer.resize(size);
					break;
				}

				size += actual;
			}

			VDMemoryStream ms(buffer.data(), (uint32)buffer.size());

			// Okay, now we have to figure out the filename. If there was one in the gzip
			// stream use that. If the name ended in .gz, then strip that off and use the
			// base name. If it was .atz, replace it with .atr. Otherwise, just replace it
			// with .dat and hope for the best.
			VDStringW newname;

			const char *fn = gzs.GetFilename();
			if (fn && *fn)
				newname = VDTextAToW(fn);
			else {
				newname.assign(imagePath, ext);

				if (!vdwcsicmp(ext, L".atz"))
					newname += L".atr";
				else if (vdwcsicmp(ext, L".gz") != 0)
					newname += L".dat";
			}

			return Load(origPath, newname.c_str(), ms, false, false, loadCtx);
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
					!vdstricmp(ext, ".atx") ||
					!vdstricmp(ext, ".dcm") ||
					!vdstricmp(ext, ".cas") ||
					!vdstricmp(ext, ".wav") ||
					!vdstricmp(ext, ".xex") ||
					!vdstricmp(ext, ".exe") ||
					!vdstricmp(ext, ".obx") ||
					!vdstricmp(ext, ".com") ||
					!vdstricmp(ext, ".a52")
					)
				{
					IVDStream& innerStream = *ziparch.OpenRawStream(i);
					vdfastvector<uint8> data;

					VDZipStream zs(&innerStream, info.mCompressedSize, !info.mbPacked);

					data.resize(info.mUncompressedSize);
					zs.Read(data.data(), info.mUncompressedSize);

					VDMemoryStream ms(data.data(), (uint32)data.size());

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
			mpCassette->Play();
			return true;
		}

		if (header[0] == 'F' && header[1] == 'U' && header[2] == 'J' && header[3] == 'I') {
			mpCassette->Load(stream);
			mpCassette->Play();
			return true;
		}

		if (header[0] == 'C' && header[1] == 'A' && header[2] == 'R' && header[3] == 'T') {
			if (loadCtx)
				loadCtx->mLoadType = kATLoadType_Cartridge;

			return LoadCartridge(0, origPath, imagePath, stream, loadCtx ? loadCtx->mpCartLoadContext : NULL);
		}

		if (!vdwcsicmp(ext, L".xex")
			|| !vdwcsicmp(ext, L".obx")
			|| !vdwcsicmp(ext, L".exe")
			|| !vdwcsicmp(ext, L".com"))
		{
			LoadProgram(origPath, stream, false);
			return true;
		}

		if (!vdwcsicmp(ext, L".bas"))
		{
load_as_basic_program:
			LoadProgram(origPath, stream, true);
			return true;
		}

		if (!vdwcsicmp(ext, L".atr")
			|| !vdwcsicmp(ext, L".dcm"))
		{
load_as_disk:
			if (loadIndex >= 15)
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
load_as_tape:
			mpCassette->Load(stream);
			mpCassette->Play();
			return true;
		}

		if (!vdwcsicmp(ext, L".rom")
			|| !vdwcsicmp(ext, L".car")
			|| !vdwcsicmp(ext, L".a52")
			)
		{
load_as_cart:
			if (loadCtx)
				loadCtx->mLoadType = kATLoadType_Cartridge;

			if (loadIndex >= 2)
				throw MyError("Invalid cartridge unit %u.\n", loadIndex);

			return LoadCartridge(loadIndex, origPath, imagePath, stream, loadCtx ? loadCtx->mpCartLoadContext : NULL);
		}
	}

	throw MyError("Unable to identify type of file: %ls.", origPath);
}

void ATSimulator::LoadProgram(const wchar_t *symbolHintPath, IVDRandomAccessStream& stream, bool basic) {
	if (mpHLEProgramLoader) {
		mpHLEProgramLoader->Shutdown();
		delete mpHLEProgramLoader;
		mpHLEProgramLoader = NULL;
	}

	if (mpHLEBasicLoader) {
		mpHLEBasicLoader->Shutdown();
		delete mpHLEBasicLoader;
		mpHLEBasicLoader = NULL;
	}

	if (basic) {
		vdautoptr<ATHLEBasicLoader> loader(new ATHLEBasicLoader);

		loader->Init(&mCPU, mpSimEventManager, this);
		loader->LoadProgram(stream);

		mpHLEBasicLoader = loader.release();

		SetBASICEnabled(true);

		if (mHardwareMode == kATHardwareMode_800)
			LoadCartridgeBASIC();
	} else {
		vdautoptr<ATHLEProgramLoader> loader(new ATHLEProgramLoader);

		loader->Init(&mCPU, mpSimEventManager, this);
		loader->LoadProgram(symbolHintPath, stream);

		mpHLEProgramLoader = loader.release();
	}

	// cold restart the system and wait for DSKINV hook to fire
	ColdReset();
}

bool ATSimulator::IsCartridgeAttached(uint32 unit) const {
	return mpCartridge[unit] != NULL;
}

void ATSimulator::UnloadCartridge(uint32 index) {

	if (index == 0) {
		IATDebugger *d = ATGetDebugger();

		if (d) {
			for(int i=0; i<3; ++i) {
				if (!mCartModuleIds[i])
					continue;

				d->UnloadSymbols(mCartModuleIds[i]);	
				mCartModuleIds[i] = 0;
			}
		}
	}

	if (mpCartridge[index]) {
		mpCartridge[index]->Unload();
		delete mpCartridge[index];
		mpCartridge[index] = NULL;
	}

	UpdateXLCartridgeLine();
}

bool ATSimulator::LoadCartridge(uint32 unit, const wchar_t *path, ATCartLoadContext *loadCtx) {
	ATLoadContext ctx;

	if (loadCtx)
		ctx.mpCartLoadContext = loadCtx;

	ctx.mLoadType = kATLoadType_Cartridge;
	ctx.mLoadIndex = unit;

	return Load(path, false, false, &ctx);
}

bool ATSimulator::LoadCartridge(uint32 unit, const wchar_t *origPath, const wchar_t *imagePath, IVDRandomAccessStream& stream, ATCartLoadContext *loadCtx) {
	UnloadCartridge(unit);

	IATDebugger *d = ATGetDebugger();

	// We have to use the mpCartridge variable because the cartridge code will
	// set up memory mappings during init that will eventually trigger
	// UpdateXLCartridgeLine().
	mpCartridge[unit] = new ATCartridgeEmulator;
	try {
		mpCartridge[unit]->Init(mpMemMan, unit ? kATMemoryPri_Cartridge2 : kATMemoryPri_Cartridge1);
		mpCartridge[unit]->SetCallbacks(mpPrivateData);

		if (!mpCartridge[unit]->Load(origPath, stream, loadCtx)) {
			ATCartridgeEmulator *tmp = mpCartridge[unit];
			mpCartridge[unit] = NULL;
			delete tmp;
			return false;
		}
	} catch(const MyError&) {
		ATCartridgeEmulator *tmp = mpCartridge[unit];
		mpCartridge[unit] = NULL;
		delete tmp;
		throw;
	}

	mpCartridge[unit]->SetUIRenderer(mpUIRenderer);

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
	UnloadCartridge(0);

	mpCartridge[0] = new ATCartridgeEmulator;
	mpCartridge[0]->Init(mpMemMan, kATMemoryPri_Cartridge1);
	mpCartridge[0]->SetCallbacks(mpPrivateData);
	mpCartridge[0]->SetUIRenderer(mpUIRenderer);
	mpCartridge[0]->LoadSuperCharger3D();
}

void ATSimulator::LoadCartridge5200Default() {
	UnloadCartridge(0);

	mpCartridge[0] = new ATCartridgeEmulator;
	mpCartridge[0]->Init(mpMemMan, kATMemoryPri_Cartridge1);
	mpCartridge[0]->SetCallbacks(mpPrivateData);
	mpCartridge[0]->SetUIRenderer(mpUIRenderer);
	mpCartridge[0]->Load5200Default();
}

void ATSimulator::LoadCartridgeFlash1Mb(bool altbank) {
	UnloadCartridge(0);

	mpCartridge[0] = new ATCartridgeEmulator;
	mpCartridge[0]->Init(mpMemMan, kATMemoryPri_Cartridge1);
	mpCartridge[0]->SetCallbacks(mpPrivateData);
	mpCartridge[0]->SetUIRenderer(mpUIRenderer);
	mpCartridge[0]->LoadFlash1Mb(altbank);
}

void ATSimulator::LoadCartridgeFlash8Mb(bool newer) {
	UnloadCartridge(0);

	mpCartridge[0] = new ATCartridgeEmulator;
	mpCartridge[0]->Init(mpMemMan, kATMemoryPri_Cartridge1);
	mpCartridge[0]->SetCallbacks(mpPrivateData);
	mpCartridge[0]->SetUIRenderer(mpUIRenderer);
	mpCartridge[0]->LoadFlash8Mb(newer);
}

void ATSimulator::LoadCartridgeFlashSIC() {
	UnloadCartridge(0);

	mpCartridge[0] = new ATCartridgeEmulator;
	mpCartridge[0]->Init(mpMemMan, kATMemoryPri_Cartridge1);
	mpCartridge[0]->SetCallbacks(mpPrivateData);
	mpCartridge[0]->SetUIRenderer(mpUIRenderer);
	mpCartridge[0]->LoadFlashSIC();
}

void ATSimulator::LoadCartridgeBASIC() {
	UnloadCartridge(0);

	ATCartridgeEmulator *cart = new ATCartridgeEmulator;
	mpCartridge[0] = cart;
	cart->Init(mpMemMan, kATMemoryPri_Cartridge1);
	cart->SetCallbacks(mpPrivateData);
	cart->SetUIRenderer(mpUIRenderer);

	ATCartLoadContext ctx = {};
	ctx.mCartMapper = kATCartridgeMode_8K;
	cart->Load(L"", VDMemoryStream(mBASICROM, sizeof mBASICROM), &ctx);
}

void ATSimulator::LoadIDE(ATIDEHardwareMode mode, bool write, bool fast, uint32 cylinders, uint32 heads, uint32 sectors, const wchar_t *path) {
	try {
		UnloadIDE();
		mIDEHardwareMode = mode;

		mpIDE = new ATIDEEmulator;
		mpIDE->Init(&mScheduler, mpUIRenderer);
		mpIDE->OpenImage(write, fast, cylinders, heads, sectors, path);

		if (mode == kATIDEHardwareMode_KMKJZ_V1 || mode == kATIDEHardwareMode_KMKJZ_V2) {
			ATKMKJZIDE *kjide = new ATKMKJZIDE(mode == kATIDEHardwareMode_KMKJZ_V2);
			mpKMKJZIDE = kjide;

			ReloadIDEFirmware();

			kjide->Init(mpIDE, &mScheduler, mpUIRenderer);
			mpPBIManager->AddDevice(mpKMKJZIDE);
		} else if (mode == kATIDEHardwareMode_SIDE) {
			ATSIDEEmulator *side = new ATSIDEEmulator;
			mpSIDE = side;

			ReloadIDEFirmware();

			side->Init(mpIDE, &mScheduler, mpUIRenderer, mpMemMan, this);
		} else {
			mpMyIDE = new ATMyIDEEmulator();
			mpMyIDE->Init(mpMemMan, mpIDE, mode == kATIDEHardwareMode_MyIDE_D5xx);
		}
	} catch(...) {
		UnloadIDE();
		throw;
	}
}

void ATSimulator::UnloadIDE() {
	if (mpMyIDE) {
		mpMyIDE->Shutdown();
		delete mpMyIDE;
		mpMyIDE = NULL;
	}

	if (mpSIDE) {
		mpSIDE->Shutdown();
		delete mpSIDE;
		mpSIDE = NULL;
	}

	if (mpKMKJZIDE) {
		mpPBIManager->RemoveDevice(mpKMKJZIDE);
		delete mpKMKJZIDE;
		mpKMKJZIDE = NULL;
	}

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
			cpuEvent = (ATSimulatorEvent)mCPU.AdvanceWithBusTracking();
		else {
			int x = mAntic.GetBeamX();

			if (!x && mAntic.GetBeamY() == 0) {
				mGTIA.BeginFrame(true, false);
			}

			ATSCHEDULER_ADVANCE(&mScheduler);
			uint8 busbusy = mAntic.Advance();

			if (!busbusy)
				cpuEvent = (ATSimulatorEvent)mCPU.AdvanceWithBusTracking();

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
			mGTIA.UpdateScreen(true, false);
			return kAdvanceResult_Stopped;
		}
	}
}

ATSimulator::AdvanceResult ATSimulator::Advance(bool dropFrame) {
	if (!mbRunning)
		return kAdvanceResult_Stopped;

	ATSimulatorEvent cpuEvent = kATSimEvent_None;

	if (mCPU.GetUnusedCycle()) {
		if (mAntic.IsPhantomDMARequired())
			cpuEvent = (ATSimulatorEvent)mCPU.AdvanceWithBusTracking();
		else
			cpuEvent = (ATSimulatorEvent)mCPU.Advance();
	}

	if (!cpuEvent) {
		int scansLeft = (mVideoStandard == kATVideoStandard_NTSC ? 262 : 312) - mAntic.GetBeamY();
		if (scansLeft > 8)
			scansLeft = 8;

		for(int scans = 0; scans < scansLeft; ++scans) {
			int x = mAntic.GetBeamX();

			if (!x && mAntic.GetBeamY() == 0) {
				if (!mGTIA.BeginFrame(false, dropFrame))
					return kAdvanceResult_WaitingForFrame;
			}

			if (mAntic.IsPhantomDMARequired()) {
				cpuEvent = AdvancePhantomDMA(x);

				if (cpuEvent | mPendingEvent)
					goto handle_event;

				x = mAntic.GetBeamX();
			}

			int cycles = 104 - x;

			if (cycles > 0) {
				if (mCPU.GetCPUMode() == kATCPUMode_65C816) {
					while(cycles--) {
						ATSCHEDULER_ADVANCE(&mScheduler);
						uint8 busbusy = mAntic.Advance();

						if (!busbusy)
							cpuEvent = (ATSimulatorEvent)mCPU.Advance65816();

						if (cpuEvent | mPendingEvent)
							goto handle_event;
					}
				} else {
					while(cycles--) {
						ATSCHEDULER_ADVANCE(&mScheduler);
						uint8 busbusy = mAntic.Advance();

						if (!busbusy)
							cpuEvent = (ATSimulatorEvent)mCPU.Advance6502();

						if (cpuEvent | mPendingEvent)
							goto handle_event;
					}
				}
			}

			cycles = 114 - mAntic.GetBeamX();

			if (cycles > 0) {
				if (mCPU.GetCPUMode() == kATCPUMode_65C816) {
					while(cycles--) {
						ATSCHEDULER_ADVANCE(&mScheduler);
						uint8 busbusy = mAntic.Advance();

						if (!busbusy)
							cpuEvent = (ATSimulatorEvent)mCPU.Advance65816WithBusTracking();

						if (cpuEvent | mPendingEvent)
							goto handle_event;
					}
				} else {
					while(cycles--) {
						ATSCHEDULER_ADVANCE(&mScheduler);
						uint8 busbusy = mAntic.Advance();

						if (!busbusy)
							cpuEvent = (ATSimulatorEvent)mCPU.Advance6502WithBusTracking();

						if (cpuEvent | mPendingEvent)
							goto handle_event;
					}
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

	return mbRunning ? kAdvanceResult_Running : kAdvanceResult_Stopped;
}

ATSimulatorEvent VDNOINLINE ATSimulator::AdvancePhantomDMA(int x) {
	int cycles = 8 - x;

	ATSimulatorEvent cpuEvent = kATSimEvent_None;

	while(cycles > 0) {
		ATSCHEDULER_ADVANCE(&mScheduler);
		uint8 busbusy = mAntic.Advance();

		if (!busbusy)
			cpuEvent = (ATSimulatorEvent)mCPU.AdvanceWithBusTracking();

		if (cpuEvent | mPendingEvent)
			break;

		--cycles;
	}

	return cpuEvent;
}

uint8 ATSimulator::DebugReadByte(uint16 addr) const {
	return mpMemMan->DebugReadByte(addr);
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

	return mpMemMan->DebugExtReadByte((uint16)address, (uint8)(address >> 16));
}

uint8 ATSimulator::DebugGlobalReadByte(uint32 address) {
	switch(address & kATAddressSpaceMask) {
		case kATAddressSpace_CPU:
			return DebugExtReadByte(address);

		case kATAddressSpace_ANTIC:
			return DebugAnticReadByte(address);

		case kATAddressSpace_PORTB:
			return mMemory[0x10000 + (address & 0xfffff)];

		case kATAddressSpace_RAM:
			return mMemory[address & 0xffff];

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
			mpMemMan->ExtWriteByte((uint16)address, (uint8)(address >> 16), value);
			break;

		case kATAddressSpace_PORTB:
			mMemory[0x10000 + (address & 0xfffff)] = value;
			break;

		case kATAddressSpace_VBXE:
			if (mpVBXE)
				mpVBXE->GetMemoryBase()[address & 0x7ffff] = value;
			break;

		case kATAddressSpace_RAM:
			mMemory[address & 0xffff] = value;
			break;
	}
}

uint8 ATSimulator::DebugAnticReadByte(uint16 address) {
	return mpMemMan->DebugAnticReadByte(address);
}

bool ATSimulator::IsKernelROMLocation(uint16 address) const {
	// 5000-57FF
	// C000-CFFF
	// D800-FFFF
	uint32 addr = address;

	// check for self-test ROM
	if ((addr - 0x5000) < 0x0800)
		return (mHardwareMode == kATHardwareMode_800XL || mHardwareMode == kATHardwareMode_1200XL || mHardwareMode == kATHardwareMode_XEGS) && (GetBankRegister() & 0x81) == 0x01;

	// check for XL/XE ROM extension
	if ((addr - 0xC000) < 0x1000)
		return (mHardwareMode == kATHardwareMode_800XL || mHardwareMode == kATHardwareMode_1200XL || mHardwareMode == kATHardwareMode_XEGS) && (GetBankRegister() & 0x01);

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
			case kATMemoryMode_8K:
				return 0x2000;

			case kATMemoryMode_16K:
				return 0x4000;

			case kATMemoryMode_24K:
				return 0x6000;

			case kATMemoryMode_32K:
				return 0x8000;

			case kATMemoryMode_40K:
				return 0xA000;

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

void ATSimulator::PIAAssertProceed() {
	if ((mPORTACTL & 0x81) != 0x01)
		return;

	// set IRQA1 status
	mPORTACTL |= 0x80;

	if (!(mIRQFlags & kIRQSource_PIAA1)) {
		if (!mIRQFlags)
			mCPU.AssertIRQ(0);

		mIRQFlags |= kIRQSource_PIAA1;
	}
}

void ATSimulator::PIAAssertInterrupt() {
	// check if IRQB1 is enabled or if IRQB1 is already set
	if ((mPORTBCTL & 0x81) != 0x01)
		return;

	// set IRQB1 status
	mPORTBCTL |= 0x80;

	if (!(mIRQFlags & kIRQSource_PIAB1)) {
		if (!mIRQFlags)
			mCPU.AssertIRQ(0);

		mIRQFlags |= kIRQSource_PIAB1;
	}
}

void ATSimulator::DumpPIAState() {
	ATConsolePrintf("Port A control:   %02x (%s, motor line %s)\n"
		, mPORTACTL
		, mPORTACTL & 0x04 ? "IOR" : "DDR"
		, (mPORTACTL & 0x38) == 0x30 ? "low / on" : "high / off"
		);
	ATConsolePrintf("Port A direction: %02x\n", mPORTADDR);
	ATConsolePrintf("Port A output:    %02x\n", mPORTAOUT);
	ATConsolePrintf("Port A edge:      %s\n", mbPIAEdgeA ? "pending" : "none");

	ATConsolePrintf("Port B control:   %02x (%s, command line %s)\n"
		, mPORTBCTL
		, mPORTBCTL & 0x04 ? "IOR" : "DDR"
		, (mPORTBCTL & 0x38) == 0x30 ? "low / on" : "high / off"
		);
	ATConsolePrintf("Port B direction: %02x\n", mPORTBDDR);
	ATConsolePrintf("Port B output:    %02x\n", mPORTBOUT);
	ATConsolePrintf("Port B edge:      %s\n", mbPIAEdgeB ? "pending" : "none");
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

		if (mpCartridge[0]) {
			if (reader.ReadUint32() != VDMAKEFOURCC('C', 'A', 'R', 'T'))
				throw ATInvalidSaveStateException();
			mpCartridge[0]->LoadState(reader);
		}

		UpdateKernel();
		UpdateXLCartridgeLine();
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

	if (mpCartridge[0]) {
		writer.WriteUint32(VDMAKEFOURCC('C', 'A', 'R', 'T'));
		mpCartridge[0]->SaveState(writer);
	}
}

void ATSimulator::UpdateXLCartridgeLine() {
	if (mHardwareMode == kATHardwareMode_800XL || mHardwareMode == kATHardwareMode_1200XL || mHardwareMode == kATHardwareMode_XEGS) {
		// TRIG3 indicates a cartridge on XL/XE. MULE fails if this isn't set properly,
		// because for some bizarre reason it jumps through WARMSV!
		bool rd5 = false;

		if (mpSIDE && mpSIDE->IsSDXEnabled())
			rd5 = true;
		
		if (!rd5) {
			for(int i=0; i<2; ++i) {
				if (mpCartridge[i] && mpCartridge[i]->IsABxxMapped()) {
					rd5 = true;
					break;
				}
			}
		}

		mGTIA.SetControllerTrigger(3, !rd5);
	}
}

void ATSimulator::UpdateKeyboardPresentLine() {
	if (mHardwareMode == kATHardwareMode_XEGS)
		mGTIA.SetControllerTrigger(2, !mbKeyboardPresent);
}

void ATSimulator::UpdateForcedSelfTestLine() {
	switch(mHardwareMode) {
		case kATHardwareMode_800XL:
		case kATHardwareMode_1200XL:
		case kATHardwareMode_XEGS:
			mPokey.SetPotPos(4, mbForcedSelfTest ? 0 : 228);
			break;
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
			case kATHardwareMode_1200XL:
				mode = mbHave1200XLKernel ? kATKernelMode_1200XL : kATKernelMode_HLE;
				break;
			case kATHardwareMode_XEGS:
				mode = mbHaveXEGSKernel ? kATKernelMode_XEGS :
					mbHaveXLKernel ? kATKernelMode_XL : kATKernelMode_HLE;
				break;
			case kATHardwareMode_5200:
				mode = mbHave5200Kernel ? kATKernelMode_5200 : kATKernelMode_5200_LLE;
				break;
		}
	}

	mActualKernelMode = mode;

	IATDebugger *deb = ATGetDebugger();
	if (mKernelSymbolsModuleId) {
		deb->UnloadSymbols(mKernelSymbolsModuleId);
		mKernelSymbolsModuleId = 0;
	}

	mpKernelLowerROM = NULL;
	mpKernelSelfTestROM = NULL;
	mpKernelUpperROM = NULL;
	mpKernel5200ROM = NULL;

	switch(mode) {
		case kATKernelMode_OSA:
			mpKernelUpperROM = mOSAKernelROM;
			break;
		case kATKernelMode_OSB:
			mpKernelUpperROM = mOSBKernelROM;
			break;
		case kATKernelMode_XL:
			mpKernelLowerROM = mXLKernelROM;
			mpKernelSelfTestROM = mXLKernelROM + 0x1000;
			mpKernelUpperROM = mXLKernelROM + 0x1800;
			break;
		case kATKernelMode_1200XL:
			mpKernelLowerROM = m1200XLKernelROM;
			mpKernelSelfTestROM = m1200XLKernelROM + 0x1000;
			mpKernelUpperROM = m1200XLKernelROM + 0x1800;
			break;
		case kATKernelMode_XEGS:
			mpKernelLowerROM = mXEGSKernelROM;
			mpKernelSelfTestROM = mXEGSKernelROM + 0x1000;
			mpKernelUpperROM = mXEGSKernelROM + 0x1800;
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
			mpKernelUpperROM = mLLEKernelROM;

			if (mbAutoLoadKernelSymbols) {
				VDStringW symbolPath(VDMakePath(VDGetProgramPath().c_str(), L"kernel.lst"));
				if (VDDoesPathExist(symbolPath.c_str()))
					mKernelSymbolsModuleId = deb->LoadSymbols(symbolPath.c_str());
			}
			break;
		case kATKernelMode_5200:
			mpKernel5200ROM = m5200KernelROM;
			break;
		case kATKernelMode_5200_LLE:
			mpKernel5200ROM = m5200LLEKernelROM;
			break;
	}
}

void ATSimulator::ReloadIDEFirmware() {
	if (mpKMKJZIDE) {
		ATKMKJZIDE *kjide = mpKMKJZIDE;
		const bool ver2 = kjide->IsVersion2();
		const wchar_t *path = mROMImagePaths[ver2 ? kATROMImage_KMKJZIDEV2 : kATROMImage_KMKJZIDE].c_str();
		bool loaded = false;

		kjide->LoadFirmware(false, NULL, 0);

		if (VDDoesPathExist(path)) {
			try {
				kjide->LoadFirmware(false, path);
				loaded = true;
			} catch(const MyError&) {
			}
		}

		if (!loaded && !ver2) {
			uint8 image[1536];
			memset(image, 0xFF, sizeof image);
			ATLoadKernelResource(IDR_NOHDBIOS, image, 0, 1536);
			
			kjide->LoadFirmware(false, image, sizeof image);
		}

		if (ver2) {
			path = mROMImagePaths[kATROMImage_KMKJZIDEV2_SDX].c_str();

			kjide->LoadFirmware(true, NULL, 0);
			if (VDDoesPathExist(path)) {
				try {
					kjide->LoadFirmware(true, path);
				} catch(const MyError&) {
				}
			}
		}
	}

	if (mpSIDE) {
		mpSIDE->LoadFirmware(NULL, 0);

		const wchar_t *path = mROMImagePaths[kATROMImage_SIDE_SDX].c_str();
		if (VDDoesPathExist(path)) {
			try {
				mpSIDE->LoadFirmware(path);
			} catch(const MyError&) {
			}
		}
	}
}

namespace {
	template<class T_Class, uint8 (T_Class::*T_ReadHandler)(uint8 address) const>
	sint32 BindReadHandlerConst(void *thisptr, uint32 addr) {
		return (uint8)(((T_Class *)thisptr)->*T_ReadHandler)((uint8)addr);
	}

	template<class T_Class, uint8 (T_Class::*T_ReadHandler)(uint8 address)>
	sint32 BindReadHandler(void *thisptr, uint32 addr) {
		return (uint8)(((T_Class *)thisptr)->*T_ReadHandler)((uint8)addr);
	}

	template<class T_Class, void (T_Class::*T_WriteHandler)(uint8 address, uint8 data)>
	bool BindWriteHandler(void *thisptr, uint32 addr, uint8 data) {
		(((T_Class *)thisptr)->*T_WriteHandler)((uint8)addr, data);
		return true;
	}
}

void ATSimulator::InitMemoryMap() {
	ShutdownMemoryMap();

	// create main RAM layer
	uint32 loMemPages = 0;
	uint32 hiMemPages = 0;

	switch(mMemoryMode) {
		case kATMemoryMode_8K:
			loMemPages = 8*4;
			break;

		case kATMemoryMode_16K:
			loMemPages = 16*4;
			break;

		case kATMemoryMode_24K:
			loMemPages = 16*4;
			break;

		case kATMemoryMode_32K:
			loMemPages = 16*4;
			break;

		case kATMemoryMode_40K:
			loMemPages = 16*4;
			break;

		case kATMemoryMode_48K:
			loMemPages = 48*4;
			break;

		case kATMemoryMode_52K:
			loMemPages = 52*4;
			break;

		default:
			loMemPages = 52*4;
			hiMemPages = 10*4;
			break;
	}

	mpMemLayerLoRAM = mpMemMan->CreateLayer(kATMemoryPri_BaseRAM, mMemory, 0, loMemPages, false);
	mpMemMan->EnableLayer(mpMemLayerLoRAM, true);

	if (hiMemPages) {
		mpMemLayerHiRAM = mpMemMan->CreateLayer(kATMemoryPri_BaseRAM, mMemory + 0xD800, 0xD8, hiMemPages, false);
		mpMemMan->EnableLayer(mpMemLayerHiRAM, true);
	}

	// create extended RAM layer
	mpMemLayerExtendedRAM = mpMemMan->CreateLayer(kATMemoryPri_ExtRAM, mMemory, 0x40, 16 * 4, false);

	// create kernel ROM layer(s)
	if (mpKernelSelfTestROM)
		mpMemLayerSelfTestROM = mpMemMan->CreateLayer(kATMemoryPri_ROM, mpKernelSelfTestROM, 0x50, 0x08, true);

	if (mpKernelLowerROM)
		mpMemLayerLowerKernelROM = mpMemMan->CreateLayer(kATMemoryPri_ROM, mpKernelLowerROM, 0xC0, 0x10, true);
	else if (mpKernel5200ROM)
		mpMemLayerLowerKernelROM = mpMemMan->CreateLayer(kATMemoryPri_ROM, mpKernel5200ROM, 0xF0, 0x08, true);

	if (mpKernelUpperROM)
		mpMemLayerUpperKernelROM = mpMemMan->CreateLayer(kATMemoryPri_ROM, mpKernelUpperROM, 0xD8, 0x28, true);
	else if (mpKernel5200ROM)
		mpMemLayerUpperKernelROM = mpMemMan->CreateLayer(kATMemoryPri_ROM, mpKernel5200ROM, 0xF8, 0x08, true);

	if (mpMemLayerLowerKernelROM)
		mpMemMan->EnableLayer(mpMemLayerLowerKernelROM, true);

	if (mpMemLayerUpperKernelROM)
		mpMemMan->EnableLayer(mpMemLayerUpperKernelROM, true);

	// create BASIC ROM layer
	mpMemLayerBASICROM = mpMemMan->CreateLayer(kATMemoryPri_ROM, mBASICROM, 0xA0, 0x20, true);

	// create game ROM layer
	if (mHardwareMode == kATHardwareMode_XEGS)
		mpMemLayerGameROM = mpMemMan->CreateLayer(kATMemoryPri_ROM, mGameROM, 0xA0, 0x20, true);

	// create hardware layers
	ATMemoryHandlerTable handlerTable;

	handlerTable.mbPassAnticReads = false;
	handlerTable.mbPassReads = false;
	handlerTable.mbPassWrites = false;

	handlerTable.mpThis = &mAntic;
	handlerTable.mpDebugReadHandler = BindReadHandlerConst<ATAnticEmulator, &ATAnticEmulator::ReadByte>;
	handlerTable.mpReadHandler = BindReadHandlerConst<ATAnticEmulator, &ATAnticEmulator::ReadByte>;
	handlerTable.mpWriteHandler = BindWriteHandler<ATAnticEmulator, &ATAnticEmulator::WriteByte>;
	mpMemLayerANTIC = mpMemMan->CreateLayer(kATMemoryPri_ROM, handlerTable, 0xD4, 0x01);

	handlerTable.mpThis = &mGTIA;
	handlerTable.mpDebugReadHandler = BindReadHandlerConst<ATGTIAEmulator, &ATGTIAEmulator::DebugReadByte>;
	handlerTable.mpReadHandler = BindReadHandler<ATGTIAEmulator, &ATGTIAEmulator::ReadByte>;
	handlerTable.mpWriteHandler = BindWriteHandler<ATGTIAEmulator, &ATGTIAEmulator::WriteByte>;

	if (mHardwareMode == kATHardwareMode_5200)
		mpMemLayerGTIA = mpMemMan->CreateLayer(kATMemoryPri_Hardware, handlerTable, 0xC0, 0x04);
	else
		mpMemLayerGTIA = mpMemMan->CreateLayer(kATMemoryPri_Hardware, handlerTable, 0xD0, 0x01);

	handlerTable.mpThis = &mPokey;
	handlerTable.mpDebugReadHandler = BindReadHandlerConst<ATPokeyEmulator, &ATPokeyEmulator::DebugReadByte>;
	handlerTable.mpReadHandler = BindReadHandler<ATPokeyEmulator, &ATPokeyEmulator::ReadByte>;
	handlerTable.mpWriteHandler = BindWriteHandler<ATPokeyEmulator, &ATPokeyEmulator::WriteByte>;

	if (mHardwareMode == kATHardwareMode_5200)
		mpMemLayerPOKEY = mpMemMan->CreateLayer(kATMemoryPri_Hardware, handlerTable, 0xE8, 0x04);
	else
		mpMemLayerPOKEY = mpMemMan->CreateLayer(kATMemoryPri_Hardware, handlerTable, 0xD2, 0x01);

	mpMemMan->EnableLayer(mpMemLayerANTIC, true);
	mpMemMan->EnableLayer(mpMemLayerGTIA, true);
	mpMemMan->EnableLayer(mpMemLayerPOKEY, true);

	if (mHardwareMode != kATHardwareMode_5200) {
		if (mbMapRAM)
			mpMemLayerHiddenRAM = mpMemMan->CreateLayer(kATMemoryPri_ExtRAM, mMemory + 0xD000, 0x50, 0x08, false);

		handlerTable.mpThis = this;
		handlerTable.mpDebugReadHandler = BindReadHandlerConst<ATSimulator, &ATSimulator::PIADebugReadByte>;
		handlerTable.mpReadHandler = BindReadHandler<ATSimulator, &ATSimulator::PIAReadByte>;
		handlerTable.mpWriteHandler = BindWriteHandler<ATSimulator, &ATSimulator::PIAWriteByte>;
		mpMemLayerPIA = mpMemMan->CreateLayer(kATMemoryPri_Hardware, handlerTable, 0xD3, 0x01);
		mpMemMan->EnableLayer(mpMemLayerPIA, true);

		mpMMU->Init(mHardwareMode,
			mMemoryMode,
			mMemory,
			mpMemMan,
			mpMemLayerExtendedRAM,
			mpMemLayerSelfTestROM,
			mpMemLayerLowerKernelROM,
			mpMemLayerUpperKernelROM,
			mpMemLayerBASICROM,
			mpMemLayerGameROM,
			mpMemLayerHiddenRAM,
			mpHLEKernel);
	}

	if (mpRTime8) {
		handlerTable.mpThis = mpRTime8;
		handlerTable.mbPassAnticReads = true;
		handlerTable.mbPassReads = true;
		handlerTable.mbPassWrites = true;
		handlerTable.mpDebugReadHandler = RT8ReadByte;
		handlerTable.mpReadHandler = RT8ReadByte;
		handlerTable.mpWriteHandler = RT8WriteByte;
		mpMemLayerRT8 = mpMemMan->CreateLayer(kATMemoryPri_CartridgeOverlay, handlerTable, 0xD5, 0x01);
		mpMemMan->EnableLayer(mpMemLayerRT8, true);
	}

	// Hook layer
	mpMemLayerHook = mpMemMan->CreateLayer(kATMemoryPri_ROM, mHookPage == 0xD700 ? mHookROM + 0x100 : mHookROM, mHookPageByte, 0x01, true);
	mpMemMan->EnableLayer(mpMemLayerHook, kATMemoryAccessMode_CPURead, true);
}

void ATSimulator::ShutdownMemoryMap() {
	if (mpMMU)
		mpMMU->Shutdown();

#define X(layer) if (layer) { mpMemMan->DeleteLayer(layer); layer = NULL; }
	X(mpMemLayerLoRAM)
	X(mpMemLayerHiRAM)
	X(mpMemLayerExtendedRAM)
	X(mpMemLayerLowerKernelROM)
	X(mpMemLayerUpperKernelROM)
	X(mpMemLayerBASICROM)
	X(mpMemLayerGameROM)
	X(mpMemLayerHiddenRAM)
	X(mpMemLayerSelfTestROM)
	X(mpMemLayerANTIC)
	X(mpMemLayerGTIA)
	X(mpMemLayerPOKEY)
	X(mpMemLayerPIA)
	X(mpMemLayerRT8)
	X(mpMemLayerHook)
#undef X
}

uint8 ATSimulator::PIADebugReadByte(uint8 addr) const {
	switch(addr & 0x03) {
	case 0x00:
	default:
		return ReadPortA();

	case 0x01:
		// PORTB reads output bits instead of input bits for those selected as output. No ANDing with input.
		return mPORTBCTL & 0x04 ? (mpPortBController->GetPortValue() & ~mPORTBDDR) + (mPORTBOUT & mPORTBDDR) : mPORTBDDR;

	case 0x02:
		return mPORTACTL;
	case 0x03:
		return mPORTBCTL;
	}
}

uint8 ATSimulator::PIAReadByte(uint8 addr) {
	switch(addr & 0x03) {
	case 0x00:
	default:
		if (mPORTACTL & 0x04) {
			mPORTACTL &= 0x3F;

			if (mIRQFlags & (kIRQSource_PIAA1 | kIRQSource_PIAA2)) {
				mIRQFlags &= ~(kIRQSource_PIAA1 | kIRQSource_PIAA2);

				if (!mIRQFlags)
					mCPU.NegateIRQ();
			}
		}

		return ReadPortA();

	case 0x01:
		if (mPORTBCTL & 0x04) {
			mPORTBCTL &= 0x3F;

			if (mIRQFlags & (kIRQSource_PIAB1 | kIRQSource_PIAB2)) {
				mIRQFlags &= ~(kIRQSource_PIAB1 | kIRQSource_PIAB2);

				if (!mIRQFlags)
					mCPU.NegateIRQ();
			}
		}

		// PORTB reads output bits instead of input bits for those selected as output. No ANDing with input.
		return mPORTBCTL & 0x04 ? (mpPortBController->GetPortValue() & ~mPORTBDDR) + (mPORTBOUT & mPORTBDDR) : mPORTBDDR;

	case 0x02:
		return mPORTACTL;
	case 0x03:
		return mPORTBCTL;
	}
}

void ATSimulator::PIAWriteByte(uint8 addr, uint8 value) {
	switch(addr & 0x03) {
	case 0x00:
		{
			uint8 oldOutput = mPORTAOUT | ~mPORTADDR;

			if (mPORTACTL & 0x04)
				mPORTAOUT = value;
			else
				mPORTADDR = value;

			uint8 newOutput = mPORTAOUT | ~mPORTADDR;

			if ((newOutput ^ oldOutput) & 0x70)
				mpInputManager->SelectMultiJoy((newOutput & 0x70) >> 4);
		}
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
				if (mHardwareMode == kATHardwareMode_1200XL)
					mpUIRenderer->SetLedStatus((~currBank >> 2) & 3);

				if (mHardwareMode == kATHardwareMode_800XL ||
					mHardwareMode == kATHardwareMode_1200XL ||
					mHardwareMode == kATHardwareMode_XEGS ||
					mMemoryMode == kATMemoryMode_128K ||
					mMemoryMode == kATMemoryMode_320K ||
					mMemoryMode == kATMemoryMode_576K ||
					mMemoryMode == kATMemoryMode_1088K)
				{
					if (mStartupDelay && !(currBank & 0x80)) {
						uint8 forcedSwitches = mGTIA.GetForcedConsoleSwitches();

						if (!(forcedSwitches & 4))
							mGTIA.SetForcedConsoleSwitches(forcedSwitches | 4);
					}

					mpMMU->SetBankRegister(currBank);
				}
			}
		}
		break;
	case 0x02:
		switch(value & 0x38) {
			case 0x00:
			case 0x08:
			case 0x28:
				mbPIAEdgeA = false;
				break;

			case 0x10:
			case 0x18:
				if (mbPIAEdgeA) {
					mbPIAEdgeA = false;
					mPORTACTL |= 0x40;
				}
				break;

			case 0x20:
			case 0x38:
				break;

			case 0x30:
				mbPIAEdgeA = true;
				break;
		}

		if (value & 0x20)
			mPORTACTL &= ~0x40;

		mPORTACTL = (mPORTACTL & 0xc0) + (value & 0x3f);

		if ((mPORTACTL & 0x68) == 0x48) {
			if (!(mIRQFlags & kIRQSource_PIAA2)) {
				if (!mIRQFlags)
					mCPU.AssertIRQ(0);

				mIRQFlags |= kIRQSource_PIAA2;
			}
		} else {
			if (mIRQFlags & kIRQSource_PIAA2) {
				mIRQFlags &= ~kIRQSource_PIAA2;

				if (!mIRQFlags)
					mCPU.NegateIRQ();
			}
		}

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
		switch(value & 0x38) {
			case 0x00:
			case 0x08:
			case 0x10:
			case 0x18:
				if (mbPIAEdgeB) {
					mbPIAEdgeB = false;
					mPORTBCTL |= 0x40;
				}

				mPIACB2 = kPIACS_Floating;
				break;

			case 0x20:
				mbPIAEdgeB = false;
				break;

			case 0x28:
				mbPIAEdgeB = false;
				mPIACB2 = kPIACS_High;
				break;

			case 0x30:
				mbPIAEdgeB = false;
				mPIACB2 = kPIACS_Low;
				break;

			case 0x38:
				if (mPIACB2 == kPIACS_Low)
					mbPIAEdgeB = true;

				mPIACB2 = kPIACS_High;
				break;
		}

		if (value & 0x20)
			mPORTBCTL &= ~0x40;

		mPORTBCTL = (mPORTBCTL & 0xc0) + (value & 0x3f);

		if ((mPORTBCTL & 0x68) == 0x48) {
			if (!(mIRQFlags & kIRQSource_PIAB2)) {
				if (!mIRQFlags)
					mCPU.AssertIRQ(0);

				mIRQFlags |= kIRQSource_PIAB2;
			}
		} else {
			if (mIRQFlags & kIRQSource_PIAB2) {
				mIRQFlags &= ~kIRQSource_PIAB2;

				if (!mIRQFlags)
					mCPU.NegateIRQ();
			}
		}

		// not quite correct -- normally set 0x34 (high) or 0x3c (low)
		mPokey.SetCommandLine(!(value & 8));
		break;
	}
}

sint32 ATSimulator::RT8ReadByte(void *thisptr0, uint32 addr) {
	if (addr - 0xD5B8 < 8)
		return ((ATRTime8Emulator *)thisptr0)->ReadControl((uint8)addr);

	return -1;
}

bool ATSimulator::RT8WriteByte(void *thisptr0, uint32 addr, uint8 value) {
	if (addr - 0xD5B8 < 8) {
		((ATRTime8Emulator *)thisptr0)->WriteControl((uint8)addr, value);
		return true;
	}

	return false;
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

uint8 ATSimulator::CPURecordBusActivity(uint8 value) {
	mAntic.SetPhantomDMAData(value);
	return value;
}

uint8 ATSimulator::AnticReadByte(uint32 addr) {
	return mpMemMan->AnticReadByte(addr);
}

void ATSimulator::AnticAssertNMI() {
	mCPU.AssertNMI();
}

void ATSimulator::AnticEndFrame() {
	mPokey.AdvanceFrame(mGTIA.IsFrameInProgress());

	if (mAntic.GetAnalysisMode()) {
		mGTIA.SetForcedBorder(true);
		mGTIA.RenderActivityMap(mAntic.GetActivityMap());
	} else {
		mGTIA.SetForcedBorder(false);
	}

	NotifyEvent(kATSimEvent_FrameTick);

	mpUIRenderer->SetCassetteIndicatorVisible(mpCassette->IsLoaded() && mpCassette->IsMotorRunning());
	mpUIRenderer->SetCassettePosition(mpCassette->GetPosition());
	mGTIA.UpdateScreen(false, false);

	if (mbBreakOnFrameEnd) {
		mbBreakOnFrameEnd = false;
		PostInterruptingEvent(kATSimEvent_EndOfFrame);
	}

	if (mpJoysticks)
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

	if (y == 248 && mpCheatEngine)
		mpCheatEngine->ApplyCheats();

	mPokey.AdvanceScanLine();

	ATSCHEDULER_ADVANCE(&mSlowScheduler);
}

bool ATSimulator::AnticIsNextCPUCycleWrite() {
	return mCPU.IsNextCycleWrite();
}

uint8 ATSimulator::AnticGetCPUHeldCycleValue() {
	return mCPU.GetHeldCycleValue();
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

void ATSimulator::GTIASelectController(uint8 index, bool potsEnabled) {
	if (mpInputManager)
		mpInputManager->Select5200Controller(index, potsEnabled);
}

void ATSimulator::GTIARequestAnticSync() {
//	mAntic.SyncWithGTIA(-1);
	mAntic.SyncWithGTIA(0);
}

uint32 ATSimulator::GTIAGetLineEdgeTimingId(uint32 offset) const {
	uint32 t = ATSCHEDULER_GETTIME(&mScheduler);
	uint32 x = mAntic.GetBeamX();

	t -= x;

	if (x >= offset)
		t += 114;

	return t;
}

void ATSimulator::PokeyAssertIRQ(bool cpuBased) {
	uint32 oldFlags = mIRQFlags;

	mIRQFlags |= kIRQSource_POKEY;

	if (!oldFlags)
		mCPU.AssertIRQ(cpuBased ? 0 : -1);
}

void ATSimulator::PokeyNegateIRQ(bool cpuBased) {
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

void ATSimulator::OnDiskMotorChange(uint8 drive, bool active) {
	mpUIRenderer->SetDiskMotorActivity(drive - 1, active);
}

void ATSimulator::VBXEAssertIRQ() {
	uint32 oldFlags = mIRQFlags;

	mIRQFlags |= kIRQSource_VBXE;

	if (!oldFlags)
		mCPU.AssertIRQ(0);
}

void ATSimulator::VBXENegateIRQ() {
	uint32 oldFlags = mIRQFlags;

	mIRQFlags &= ~kIRQSource_VBXE;

	if (oldFlags == kIRQSource_VBXE)
		mCPU.NegateIRQ();
}

void ATSimulator::UpdatePrinterHook() {
}

void ATSimulator::UpdateCIOVHook() {
}
