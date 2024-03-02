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
#include "ultimate1mb.h"
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
#include "versioninfo.h"

namespace {
	const char kSaveStateVersion[] = "Altirra save state V1";

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

	static void PIAChangeCommandLine(void *thisPtr, uint32 output);
	static void PIAChangeMotorLine(void *thisPtr, uint32 output);
	static void PIAChangeMultiJoy(void *thisPtr, uint32 output);
	static void PIAChangeBanking(void *thisPtr, uint32 output);

	ATSimulator& mParent;
};

void ATSimulator::PrivateData::PIAChangeCommandLine(void *thisPtr, uint32 output) {
	ATSimulator& sim = *(ATSimulator *)thisPtr;

	sim.mPokey.SetCommandLine(!(output & kATPIAOutput_CB2));
}

void ATSimulator::PrivateData::PIAChangeMotorLine(void *thisPtr, uint32 output) {
	ATSimulator& sim = *(ATSimulator *)thisPtr;

	bool state = !(output & kATPIAOutput_CA2);

	if (sim.mpCassette)
		sim.mpCassette->SetMotorEnable(state);

	sim.mPokey.SetAudioLine2(state ? 32 : 0);
}

void ATSimulator::PrivateData::PIAChangeMultiJoy(void *thisPtr, uint32 output) {
	ATSimulator& sim = *(ATSimulator *)thisPtr;

	if (sim.mpInputManager)
		sim.mpInputManager->SelectMultiJoy((output & 0x70) >> 4);
}

void ATSimulator::PrivateData::PIAChangeBanking(void *thisPtr, uint32 output) {
	ATSimulator& sim = *(ATSimulator *)thisPtr;

	sim.UpdateBanking(sim.GetBankRegister());
}

///////////////////////////////////////////////////////////////////////////

ATSimulator::ATSimulator()
	: mbRunning(false)
	, mbPaused(false)
	, mpPrivateData(new PrivateData(*this))
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
//	, mKernelSymbolsModuleIds(0)
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
	, mpUltimate1MB(NULL)
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

	mKernelSymbolsModuleIds[0] = 0;
	mKernelSymbolsModuleIds[1] = 0;

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

	mIRQController.Init(&mCPU);
	mPIA.Init(&mIRQController);

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

	mPIA.AllocOutput(PrivateData::PIAChangeMotorLine, this, kATPIAOutput_CA2);
	mPIA.AllocOutput(PrivateData::PIAChangeCommandLine, this, kATPIAOutput_CB2);
	mPIA.AllocOutput(PrivateData::PIAChangeMultiJoy, this, 0x70);			// port A control lines
	mPIA.AllocOutput(PrivateData::PIAChangeBanking, this, 0xFF00);			// port B control lines

	mpPortAController->Init(&mGTIA, &mPokey, &mPIA, mpLightPen, 0);
	mpPortBController->Init(&mGTIA, &mPokey, &mPIA, mpLightPen, 2);

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
	mbCartridgeSwitch = true;
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
	Shutdown();
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

	if (mpInputManager)
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

	if (mpUltimate1MB) {
		mpUltimate1MB->Shutdown();
		delete mpUltimate1MB;
		mpUltimate1MB = NULL;
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

	delete mpAudioMonitor; mpAudioMonitor = NULL;
	delete mpRS232; mpRS232 = NULL;
	delete mpHostDevice; mpHostDevice = NULL;
	delete mpJoysticks; mpJoysticks = NULL;
	delete mpIDE; mpIDE = NULL;
	delete mpHLEKernel; mpHLEKernel = NULL;
	delete mpInputManager; mpInputManager = NULL;
	delete mpPortAController; mpPortAController = NULL;
	delete mpPortBController; mpPortBController = NULL;
	delete mpLightPen; mpLightPen = NULL;
	delete mpPrinter; mpPrinter = NULL;
	delete mpCheatEngine; mpCheatEngine = NULL;
	delete mpPokeyTables; mpPokeyTables = NULL;

	if (mpUIRenderer) {
		mpUIRenderer->Release();
		mpUIRenderer = NULL;
	}

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
	delete mpMemMan; mpMemMan = NULL;

	delete mpPrivateData; mpPrivateData = NULL;
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
		mLLEOSBKernelROM,
		mHLEKernelROM,
		m5200LLEKernelROM,
		mGameROM,
		m1200XLKernelROM,
		mLLEXLKernelROM,
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
		{ -1, IDR_KERNELXL, 0, 16384 },
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
	ReloadU1MBFirmware();
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

uint8 ATSimulator::GetBankRegister() const {
	return mPIA.GetPortBOutput();
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
			mpVBXE->Init(mbVBXESharedMemory ? mMemory + 0x10000 : (uint8 *)mpVBXEMemory, this, mpMemMan);
			mpVBXE->Set5200Mode(mHardwareMode == kATHardwareMode_5200);
			mGTIA.SetVBXE(mpVBXE);

			UpdateVBXEPage();
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
	if (mbVBXEUseD7xx == enable)
		return;

	mbVBXEUseD7xx = enable;
	UpdateVBXEPage();
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
		UpdateSoundBoardPage();
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
		mpRS232->Init(mpMemMan, &mScheduler, &mSlowScheduler, mpUIRenderer, &mPokey, &mPIA);
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

void ATSimulator::SetCartridgeSwitch(bool enable) {
	if (mbCartridgeSwitch == enable)
		return;

	mbCartridgeSwitch = enable;
	UpdateCartridgeSwitch();
}

void ATSimulator::SetUltimate1MBEnabled(bool enable) {
	if (enable) {
		if (mpUltimate1MB)
			return;

		// Force memory mode to 1MB (required)
		mMemoryMode = kATMemoryMode_1088K;

		mpUltimate1MB = new ATUltimate1MBEmulator;
		mpUltimate1MB->Init(mMemory, mpMMU, mpPBIManager, mpMemMan, mpUIRenderer, &mScheduler, mpCPUHookManager);
		mpUltimate1MB->SetExternalCartCallback(ATBINDCALLBACK(this, &ATSimulator::UpdateCartEnable));
		mpUltimate1MB->SetCartCallback(ATBINDCALLBACK(this, &ATSimulator::UpdateXLCartridgeLine));
		mpUltimate1MB->SetVBXEPageCallback(ATBINDCALLBACK(this, &ATSimulator::UpdateVBXEPage));
		mpUltimate1MB->SetSBPageCallback(ATBINDCALLBACK(this, &ATSimulator::UpdateSoundBoardPage));

		ReloadU1MBFirmware();
		InitMemoryMap();

		UpdateCartEnable();
	} else {
		if (!mpUltimate1MB)
			return;

		mpUltimate1MB->Shutdown();
		delete mpUltimate1MB;
		mpUltimate1MB = NULL;

		if (mpMemLayerGameROM)
			mpMemMan->SetLayerMemory(mpMemLayerGameROM, mGameROM);

		if (mpMemLayerBASICROM)
			mpMemMan->SetLayerMemory(mpMemLayerBASICROM, mBASICROM);

		if (mpMemLayerLowerKernelROM)
			mpMemMan->SetLayerMemory(mpMemLayerLowerKernelROM, mpKernelLowerROM);

		if (mpMemLayerUpperKernelROM)
			mpMemMan->SetLayerMemory(mpMemLayerUpperKernelROM, mpKernelUpperROM);

		if (mpMemLayerSelfTestROM)
			mpMemMan->SetLayerMemory(mpMemLayerSelfTestROM, mpKernelSelfTestROM);

		if (mpSIDE)
			mpSIDE->SetExternalEnable(true);
	}
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

	mIRQController.ColdReset();

	// PIA forces all registers to 0 on reset.
	mPIA.Reset();

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

	if (mpMyIDE)
		mpMyIDE->ColdReset();

	InitMemoryMap();

	if (mpUltimate1MB)
		mpUltimate1MB->ColdReset();

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

	// If Ultimate1MB is enabled, we must suppress all OS hooks until the BIOS has locked config.
	mpCPUHookManager->EnableOSHooks(mpUltimate1MB == NULL);

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
		mPIA.Reset();

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

	if (mpUltimate1MB)
		mpUltimate1MB->WarmReset();

	mpPBIManager->WarmReset();

	for(size_t i=0; i<sizeof(mDiskDrives)/sizeof(mDiskDrives[0]); ++i)
		mDiskDrives[i].ClearAccessedFlag();

	mpHLECIOHook->WarmReset();
}

void ATSimulator::Resume() {
	if (mbRunning)
		return;

	mbRunning = true;
	mbPaused = false;

	if (mpUIRenderer)
		mpUIRenderer->SetPaused(false);

	if (mPendingEvent == kATSimEvent_AnonymousInterrupt)
		mPendingEvent = kATSimEvent_None;
}

void ATSimulator::Pause() {
	mbRunning = false;
	mbPaused = true;

	if (mpUIRenderer)
		mpUIRenderer->SetPaused(true);
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
		kATStorageId_Firmware,
		(ATStorageId)(kATStorageId_Firmware+1),
		(ATStorageId)(kATStorageId_Firmware+2),
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

		if (IsStorageDirty(kATStorageId_Firmware))
			return true;

		if (IsStorageDirty((ATStorageId)(kATStorageId_Firmware + 1)))
			return true;

		return false;
	}

	const uint32 type = mediaId & kATStorageId_TypeMask;
	const uint32 unit = mediaId & kATStorageId_UnitMask;
	switch(type) {
		case kATStorageId_Cartridge:
			if (unit < 2 && mpCartridge[unit])
				return mpCartridge[unit]->IsDirty();
			break;

		case kATStorageId_Disk:
			if (unit < sizeof(mDiskDrives)/sizeof(mDiskDrives[0]))
				return mDiskDrives[unit].IsDirty();
			break;

		case kATStorageId_Firmware:
			if (mpKMKJZIDE) {
				switch(unit) {
					case 0: return mpKMKJZIDE->IsMainFlashDirty();
					case 1: return mpKMKJZIDE->IsSDXFlashDirty();
				}
			} else if (mpSIDE) {
				if (unit == 0)
					return mpSIDE->IsSDXFlashDirty();
			} else if (mpMyIDE) {
				if (unit == 0)
					return mpMyIDE->IsFlashDirty();
			}

			if (unit == 2)
				return mpUltimate1MB && mpUltimate1MB->IsFirmwareDirty();

			break;
	}

	return false;
}

bool ATSimulator::IsStoragePresent(ATStorageId mediaId) const {
	const uint32 type = mediaId & kATStorageId_TypeMask;
	const uint32 unit = mediaId & kATStorageId_UnitMask;
	switch(type) {
		case kATStorageId_Cartridge:
			if (unit < 2 && mpCartridge[unit])
				return true;
			break;

		case kATStorageId_Disk:
			if (unit < sizeof(mDiskDrives)/sizeof(mDiskDrives[0]))
				return mDiskDrives[unit].IsEnabled();
			break;

		case kATStorageId_Firmware:
			if (unit == 2)
				return mpUltimate1MB != NULL;

			if (mpKMKJZIDE)
				return unit == 0 || (unit == 1 && mpKMKJZIDE->IsVersion2());
			else if (mpSIDE || mpMyIDE)
				return unit == 0;
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

	const wchar_t *ext = imagePath ? VDFileSplitExt(imagePath) : L"";

	ATLoadType loadType = kATLoadType_Other;

	if (loadCtx) {
		if (loadCtx->mLoadIndex >= 0)
			loadIndex = loadCtx->mLoadIndex;

		loadType = loadCtx->mLoadType;
	}

	// Try to detect by filename.
	if (!vdwcsicmp(ext, L".zip"))
		goto load_as_zip;
	else if (!vdwcsicmp(ext, L".gz") || !vdwcsicmp(ext, L".atz"))
		goto load_as_gzip;
	else if (!vdwcsicmp(ext, L".xfd"))
		loadType = kATLoadType_Disk;
	else if (!vdwcsicmp(ext, L".bin") || !vdwcsicmp(ext, L".rom") || !vdwcsicmp(ext, L".a52"))
		loadType = kATLoadType_Cartridge;

	// If we still haven't determined the load type, try to autodetect by content.
	if (loadType == kATLoadType_Other && actual >= 6) {
		size = stream.Length();

		// Detect archive types.
		if (header[0] == 0x1f && header[1] == 0x8b)
			goto load_as_gzip;
		else if (header[0] == 'P' && header[1] == 'K' && header[2] == 0x03 && header[3] == 0x04)
			goto load_as_zip;
		else if (header[0] == 0xFF && header[1] == 0xFF)
			loadType = kATLoadType_Program;
		else if ((header[0] == 'A' && header[1] == 'T' && header[2] == '8' && header[3] == 'X') ||
			(header[2] == 'P' && header[3] == '3') ||
			(header[2] == 'P' && header[3] == '2') ||
			(header[0] == 0x96 && header[1] == 0x02) ||
			(!(size & 127) && size <= 65535*128 && !_wcsicmp(ext, L".xfd")))
		{
			loadType = kATLoadType_Disk;
		} else if (actual >= 12
			&& header[0] == 'R' && header[1] == 'I' && header[2] == 'F' && header[3] == 'F'
			&& header[8] == 'W' && header[9] == 'A' && header[10] == 'V' && header[11] == 'E'
			)
		{
			loadType = kATLoadType_Tape;
		} else if (header[0] == 'F' && header[1] == 'U' && header[2] == 'J' && header[3] == 'I')
			loadType = kATLoadType_Tape;
		else if (header[0] == 'C' && header[1] == 'A' && header[2] == 'R' && header[3] == 'T')
			loadType = kATLoadType_Cartridge;
		else if (!memcmp(header, kATSaveStateHeader, sizeof kATSaveStateHeader))
			loadType = kATLoadType_SaveState;
		else if (!vdwcsicmp(ext, L".xex")
			|| !vdwcsicmp(ext, L".obx")
			|| !vdwcsicmp(ext, L".exe")
			|| !vdwcsicmp(ext, L".com"))
			loadType = kATLoadType_Program;
		else if (!vdwcsicmp(ext, L".bas"))
			loadType = kATLoadType_BasicProgram;
		else if (!vdwcsicmp(ext, L".atr") || !vdwcsicmp(ext, L".dcm"))
			loadType = kATLoadType_Disk;
		else if (!vdwcsicmp(ext, L".cas") || !vdwcsicmp(ext, L".wav"))
			loadType = kATLoadType_Tape;
		else if (!vdwcsicmp(ext, L".rom")
			|| !vdwcsicmp(ext, L".car")
			|| !vdwcsicmp(ext, L".a52")
			)
		{
			loadType = kATLoadType_Cartridge;
		}
	}

	// Handle archive types first. Note that we do NOT write zip/gzip back into the load
	// type in the load context, in case there is an override for the inner resource that
	// we need to preserve.
	if (loadType == kATLoadType_GZip) {
load_as_gzip:
		// This is really big, so don't put it on the stack.
		vdautoptr<VDGUnzipStream> gzs(new VDGUnzipStream(&stream, stream.Length()));

		vdfastvector<uint8> buffer;

		uint32 size = 0;
		for(;;) {
			// Don't gunzip beyond 64MB.
			if (size >= 0x4000000)
				throw MyError("Gzip stream is too large (exceeds 64MB in size).");

			buffer.resize(size + 1024);

			sint32 actual = gzs->ReadData(buffer.data() + size, 1024);
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
		const wchar_t *newPath = NULL;

		const char *fn = gzs->GetFilename();
		if (fn && *fn) {
			newname = VDTextAToW(fn);
			newPath = newname.c_str();
		} else if (imagePath) {
			newname.assign(imagePath, ext);

			if (!vdwcsicmp(ext, L".atz"))
				newname += L".atr";
			else if (vdwcsicmp(ext, L".gz") != 0)
				newname += L".dat";

			newPath = newname.c_str();
		}

		if (loadCtx)
			loadCtx->mLoadType = kATLoadType_Other;

		return Load(origPath, newPath, ms, false, false, loadCtx);
	} else if (loadType == kATLoadType_Zip) {
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
				!vdstricmp(ext, ".atz") ||
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

				vdautoptr<VDZipStream> zs(new VDZipStream(&innerStream, info.mCompressedSize, !info.mbPacked));

				data.resize(info.mUncompressedSize);
				zs->Read(data.data(), info.mUncompressedSize);

				VDMemoryStream ms(data.data(), (uint32)data.size());

				if (loadCtx)
					loadCtx->mLoadType = kATLoadType_Other;

				return Load(origPath, VDTextAToW(name).c_str(), ms, false, false, loadCtx);
			}
		}

		if (origPath)
			throw MyError("The zip file \"%ls\" does not contain a recognized file type.", origPath);
		else
			throw MyError("The zip file does not contain a recognized file type.");
	}

	// Stash off the detected type (or Other if we failed).
	if (loadCtx)
		loadCtx->mLoadType = loadType;

	// Load the resource.
	if (loadType == kATLoadType_Program) {
		LoadProgram(origPath, stream, false);
		return true;
	} else if (loadType == kATLoadType_BasicProgram) {
		LoadProgram(origPath, stream, true);
		return true;
	} else if (loadType == kATLoadType_Cartridge) {
		if (loadIndex >= 2)
			throw MyError("Invalid cartridge unit %u.\n", loadIndex);

		return LoadCartridge(loadIndex, origPath, imagePath, stream, loadCtx ? loadCtx->mpCartLoadContext : NULL);
	} else if (loadType == kATLoadType_Tape) {
		mpCassette->Load(stream);
		mpCassette->Play();
		return true;
	} else if (loadType == kATLoadType_Disk) {
		if (loadIndex >= 15)
			throw MyError("Invalid disk drive D%d:.\n", loadIndex + 1);

		ATDiskEmulator& drive = mDiskDrives[loadIndex];
		drive.LoadDisk(origPath, imagePath, stream);

		if (rw)
			drive.SetWriteFlushMode(true, true);
		else if (vrw)
			drive.SetWriteFlushMode(true, false);

		return true;
	} else if (loadType == kATLoadType_SaveState) {
		size = stream.Length();

		if (size > 0x10000000)
			throw MyError("Save state file is too big.");

		uint32 len32 = (uint32)size;
		vdblock<uint8> mem(len32);

		stream.Read(mem.data(), len32);
		ATSaveStateReader reader(mem.data(), (uint32)mem.size());

		return LoadState(reader, loadCtx ? loadCtx->mpStateLoadContext : NULL);
	}

	if (origPath)
		throw MyError("Unable to identify type of file: %ls.", origPath);
	else
		throw MyError("Unable to identify type of file.");
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
		mpCartridge[unit]->Init(mpMemMan, &mScheduler, unit ? kATMemoryPri_Cartridge2 : kATMemoryPri_Cartridge1);
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

void ATSimulator::LoadCartridge5200Default() {
	UnloadCartridge(0);

	mpCartridge[0] = new ATCartridgeEmulator;
	mpCartridge[0]->Init(mpMemMan, &mScheduler, kATMemoryPri_Cartridge1);
	mpCartridge[0]->SetCallbacks(mpPrivateData);
	mpCartridge[0]->SetUIRenderer(mpUIRenderer);
	mpCartridge[0]->Load5200Default();
}

void ATSimulator::LoadNewCartridge(int mode) {
	UnloadCartridge(0);

	mpCartridge[0] = new ATCartridgeEmulator;
	mpCartridge[0]->Init(mpMemMan, &mScheduler, kATMemoryPri_Cartridge1);
	mpCartridge[0]->SetCallbacks(mpPrivateData);
	mpCartridge[0]->SetUIRenderer(mpUIRenderer);
	mpCartridge[0]->LoadNewCartridge((ATCartridgeMode)mode);
}

void ATSimulator::LoadCartridgeBASIC() {
	UnloadCartridge(0);

	ATCartridgeEmulator *cart = new ATCartridgeEmulator;
	mpCartridge[0] = cart;
	cart->Init(mpMemMan, &mScheduler, kATMemoryPri_Cartridge1);
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

			UpdateCartEnable();
			UpdateCartridgeSwitch();
		} else {
			mpMyIDE = new ATMyIDEEmulator();
			mpMyIDE->Init(mpMemMan, mpUIRenderer, &mScheduler, mpIDE, this, mode != kATIDEHardwareMode_MyIDE_D1xx, mode == kATIDEHardwareMode_MyIDE_V2_D5xx);

			ReloadIDEFirmware();
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
		if (!mCPU.IsInstructionInProgress())
			return kAdvanceResult_Running;

		if (!mbRunning)
			return kAdvanceResult_Stopped;

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

namespace {
	// Compute a Fletcher-32 checksum.
	//
	// The Fletcher-32 checksum is a split checksum where a simple checksum is computed
	// modulo 2^16-1 and a complex checksum is computed off of the incrementally computed
	// simple checksums. It shares the byte swapping capability of a one's complement
	// checksum -- such as the IPv4 checksum -- but can detect reordered data and is only
	// moderately more expensive to compute.
	// 
	uint32 ComputeFletcher32(const void *src, size_t len, uint32 base = 0) {
		size_t len2 = len >> 1;
		uint32 acc1 = (base & 0xffff);
		uint32 acc2 = (base >> 16);

		const uint16 *src16 = (const uint16 *)src;
		while(len2) {
			// We have to break up the input into packets of 359 words or less. The
			// reason:
			//
			// The incoming accumulators can be from 0 to 0x1fffe due to the previous
			// round of folding.
			// The limiting factor is the second accumulator (complex checksum).
			// The largest value of N for which 0x1fffe+sum_i{1..n}(0x1fffe+0xffff*i) < 2^32
			// is 359:
			//
			//   0x1fffe + sum_i{1..n}(0x1fffe+0xffff*i)
			// = 0x1fffe + 0x1fffe*n + sum_i{1..n}(0xffff*i)
			// = 0x1fffe + 0x1fffe*n + 0xffff*sum_i{0..n-1}(i)
			// = 0x1fffe + 0x1fffe*n + 0xffff*(n-1)*n/2
			// = 0x1fffe + 0x1fffe*n + 0xffff/2*n^2 - 0xffff/2*n
			// 
			// Solving the quadratic formula:
			//  32767.5x^2 + 163837.5x + (131070 - 4294967295) = 0
			//
			// ...gives x = 359.54454.

			uint32 tc = (uint32)len2 > 359 ? 359 : (uint32)len2;
			len2 -= tc;

			do {
				acc1 += *src16++;
				acc2 += acc1;
			} while(--tc);

			// fold down to 16 bits
			acc1 = (acc1 & 0xffff) + (acc1 >> 16);
			acc2 = (acc2 & 0xffff) + (acc2 >> 16);
		}

		// handle extra byte (basically, pad with 00)
		if (len & 1) {
			const uint8 c = *(const uint8 *)src16;

			acc1 += c;
			acc2 += acc1;

			// fold down to 16 bits
			acc1 = (acc1 & 0xffff) + (acc1 >> 16);
			acc2 = (acc2 & 0xffff) + (acc2 >> 16);
		}

		// fold down to 16 bits
		acc1 = (acc1 & 0xffff) + (acc1 >> 16);
		acc2 = (acc2 & 0xffff) + (acc2 >> 16);

		// return merged sum
		return (acc2 << 16) + acc1;
	}
}

uint32 ATSimulator::ComputeKernelChecksum() const {
	uint32 sum = 0;

	if (mpKernel5200ROM)
		sum = ComputeFletcher32(mpKernel5200ROM, 0x0800, sum);

	if (mpKernelLowerROM)
		sum = ComputeFletcher32(mpKernelLowerROM, 0x0800, sum);

	if (mpKernelSelfTestROM)
		sum = ComputeFletcher32(mpKernelSelfTestROM, 0x0800, sum);

	if (mpKernelUpperROM)
		sum = ComputeFletcher32(mpKernelUpperROM, 0x0800, sum);

	return sum;
}

bool ATSimulator::LoadState(ATSaveStateReader& reader, ATStateLoadContext *pctx) {
	ATStateLoadContext dummyCtx = {};

	if (!pctx)
		pctx = &dummyCtx;

	// check header
	uint8 header[12];
	reader.ReadData(header, 12);
	if (memcmp(header, kATSaveStateHeader, 12))
		throw ATInvalidSaveStateException();

	// --- past this point we force a cold reset on failure ---

	bool machineDescFound = false;
	bool archStateFound = false;
	bool privateStateOK = false;
	bool privateStateLoaded = false;

	try {
		while(reader.GetAvailable() >= 8) {
			uint32 fcc = reader.ReadUint32();
			uint32 len = reader.ReadUint32();

			reader.OpenChunk(len);

			// quick sanity check
			VDASSERT(fcc >= 0x20202020 && fcc <= 0x7E7E7E7E);

			switch(fcc) {
				case VDMAKEFOURCC('M', 'A', 'C', 'H'):
					LoadStateMachineDesc(reader);
					machineDescFound = true;

					// init load handlers now
					mCPU.BeginLoadState(reader);
					mAntic.BeginLoadState(reader);
					mPokey.BeginLoadState(reader);
					mGTIA.BeginLoadState(reader);
					mPIA.BeginLoadState(reader);

					for(int i=0; i<2; ++i) {
						if (mpCartridge[i])
							mpCartridge[i]->BeginLoadState(reader);
					}

					reader.RegisterHandlerMethod(kATSaveStateSection_Arch, VDMAKEFOURCC('R', 'A', 'M', ' '), this, &ATSimulator::LoadStateMemoryArch);
					break;

				case VDMAKEFOURCC('I', 'S', 'V', 'R'):
					if (reader.GetAvailable() == sizeof(kSaveStateVersion) - 1) {
						char buf[sizeof(kSaveStateVersion) - 1];

						reader.ReadData(buf, sizeof buf);

						if (!memcmp(buf, kSaveStateVersion, sizeof buf))
							privateStateOK = true;
					}
					break;

				case VDMAKEFOURCC('L', 'I', 'S', 'T'):
					fcc = reader.ReadUint32();

					if (!machineDescFound)
						throw ATInvalidSaveStateException();

					switch(fcc) {
						case VDMAKEFOURCC('R', 'E', 'F', 'S'):
							LoadStateRefs(reader, *pctx);

							// check for errors
							if (!pctx->mbAllowKernelMismatch && pctx->mbKernelMismatchDetected)
								return false;

							break;

						case VDMAKEFOURCC('A', 'R', 'C', 'H'):
							LoadStateSection(reader, kATSaveStateSection_Arch);
							archStateFound = true;
							break;

						case VDMAKEFOURCC('A', 'T', 'R', 'A'):
							if (!archStateFound)
								throw ATInvalidSaveStateException();

							if (privateStateOK) {
								LoadStateSection(reader, kATSaveStateSection_Private);
								privateStateLoaded = true;
							}
							break;
					}
					break;
			}

			reader.CloseChunk();
		}

		if (!machineDescFound || !archStateFound)
			throw ATInvalidSaveStateException();

		if (!privateStateLoaded)
			reader.DispatchChunk(kATSaveStateSection_ResetPrivate, 0);

		reader.DispatchChunk(kATSaveStateSection_End, 0);
	} catch(const MyError&) {
		ColdReset();
		throw;
	}

	pctx->mbPrivateStateLoaded = privateStateLoaded;

	// sync state
	UpdateXLCartridgeLine();
	UpdateBanking(GetBankRegister());

	NotifyEvent(kATSimEvent_StateLoaded);
	return true;
}

void ATSimulator::LoadStateMachineDesc(ATSaveStateReader& reader) {
	uint8 memoryModeId = kATMemoryMode_64K;
	uint8 kernelModeId = kATKernelMode_Default;
	uint8 hardwareModeId = kATHardwareMode_800XL;
	uint8 videoStandardId = kATVideoStandard_NTSC;
	bool stereo = false;
	bool mapram = false;
	uint16 vbxebase = 0;
	bool vbxeshared = false;
	bool slightsid = false;
	uint16 sbbase = 0;
	bool covox = false;

	while(reader.GetAvailable() >= 8) {
		const uint32 fcc = reader.ReadUint32();
		const uint32 len = reader.ReadUint32();

		VDASSERT(fcc >= 0x20202020 && fcc <= 0x7E7E7E7E);

		reader.OpenChunk(len);

		switch(fcc) {
			case VDMAKEFOURCC('R', 'A', 'M', ' '):
				memoryModeId = reader.ReadUint8();
				if (memoryModeId >= kATMemoryModeCount)
					throw ATUnsupportedSaveStateException();
				break;

			case VDMAKEFOURCC('K', 'R', 'N', 'L'):
				kernelModeId = reader.ReadUint8();
				if (kernelModeId >= kATKernelModeCount)
					throw ATUnsupportedSaveStateException();
				break;

			case VDMAKEFOURCC('H', 'D', 'W', 'R'):
				hardwareModeId = reader.ReadUint8();
				if (hardwareModeId >= kATHardwareModeCount)
					throw ATUnsupportedSaveStateException();
				break;

			case VDMAKEFOURCC('V', 'I', 'D', 'O'):
				videoStandardId = reader.ReadUint8();
				if (videoStandardId >= kATVideoStandardCount)
					throw ATUnsupportedSaveStateException();
				break;

			case VDMAKEFOURCC('S', 'T', 'R', 'O'):
				stereo = true;
				break;

			case VDMAKEFOURCC('V', 'B', 'X', 'E'):
				vbxebase = reader.ReadUint16();
				if (vbxebase != 0xD600 && vbxebase != 0xD700)
					throw ATUnsupportedSaveStateException();

				vbxeshared = reader.ReadBool();
				break;

			case VDMAKEFOURCC('M', 'P', 'R', 'M'):
				mapram = true;
				break;

			case VDMAKEFOURCC('S', 'S', 'I', 'D'):
				slightsid = true;
				break;

			case VDMAKEFOURCC('S', 'N', 'B', 'D'):
				sbbase = reader.ReadUint16();

				if (sbbase != 0xD2C0 && sbbase != 0xD500 && sbbase != 0xD600)
					throw ATUnsupportedSaveStateException();
				break;

			case VDMAKEFOURCC('C', 'O', 'V', 'X'):
				covox = true;
				break;
		}

		reader.CloseChunk();
	}	

	SetMemoryMode((ATMemoryMode)memoryModeId);
	SetKernelMode((ATKernelMode)kernelModeId);
	SetHardwareMode((ATHardwareMode)hardwareModeId);
	SetVideoStandard((ATVideoStandard)videoStandardId);
	SetVBXEEnabled(vbxebase != 0);

	if (vbxebase) {
		SetVBXEAltPageEnabled(vbxebase == 0xD700);
		SetVBXESharedMemoryEnabled(vbxeshared);
	}

	SetSlightSIDEnabled(slightsid);
	SetSoundBoardEnabled(sbbase != 0);
	if (sbbase)
		SetSoundBoardMemBase(sbbase);

	SetCovoxEnabled(covox);

	SetDualPokeysEnabled(stereo);
	SetMapRAMEnabled(mapram);

	// Update the kernel now so we can validate it.
	UpdateKernel();

	// Issue a baseline reset so everything is sane.
	ColdReset();

	mStartupDelay = 0;
	mGTIA.SetForcedConsoleSwitches(0xF);
}

void ATSimulator::LoadStateRefs(ATSaveStateReader& reader, ATStateLoadContext& ctx) {
	while(reader.GetAvailable() >= 8) {
		const uint32 fcc = reader.ReadUint32();
		const uint32 len = reader.ReadUint32();

		VDASSERT(fcc >= 0x20202020 && fcc <= 0x7E7E7E7E);

		reader.OpenChunk(len);

		switch(fcc) {
			case VDMAKEFOURCC('K', 'R', 'N', 'L'):
				if (ComputeKernelChecksum() != reader.ReadUint32())
					ctx.mbKernelMismatchDetected = true;
				break;
		}

		reader.CloseChunk();
	}	
}

void ATSimulator::LoadStateSection(ATSaveStateReader& reader, int section) {
	while(reader.GetAvailable() >= 8) {
		const uint32 fcc = reader.ReadUint32();
		const uint32 len = reader.ReadUint32();

		VDASSERT(fcc >= 0x20202020 && fcc <= 0x7E7E7E7E);

		reader.OpenChunk(len);
		reader.DispatchChunk((ATSaveStateSection)section, fcc);
		reader.CloseChunk();
	}	
}

void ATSimulator::LoadStateMemoryArch(ATSaveStateReader& reader) {
	uint32 offset = reader.ReadUint32();

	uint32 size = GetMemorySizeForMemoryMode(mMemoryMode);
	uint32 rsize = reader.GetAvailable();

	if (rsize > size || size - offset < rsize)
		throw ATInvalidSaveStateException();

	// We need to handle the XRAM split in 128K and 320K modes (groan).
	const bool splitRequired = (size > 0x10000 && size < 0x90000);
	while(rsize) {
		uint32 tc = rsize;
		uint32 loadoffset = offset;

		if (splitRequired) {
			if (loadoffset < 0x10000)
				tc = std::min<uint32>(tc, 0x10000 - loadoffset);
			else
				loadoffset += 0x40000;
		}

		reader.ReadData(mMemory + loadoffset, tc);
		offset += tc;
		rsize -= tc;
	}
}

void ATSimulator::SaveState(ATSaveStateWriter& writer) {
	if (kAdvanceResult_Running != AdvanceUntilInstructionBoundary())
		throw MyError("The emulation state cannot be saved because emulation is stopped in the middle of an instruction.");

	// set up write handlers
	mCPU.BeginSaveState(writer);
	mAntic.BeginSaveState(writer);
	mPokey.BeginSaveState(writer);
	mGTIA.BeginSaveState(writer);
	mPIA.BeginSaveState(writer);

	for(int i=0; i<2; ++i) {
		if (mpCartridge[i])
			mpCartridge[i]->BeginSaveState(writer);
	}

	// write file header
	writer.WriteData(kATSaveStateHeader, sizeof kATSaveStateHeader);

	// write description
	writer.BeginChunk(VDMAKEFOURCC('M', 'A', 'C', 'H'));
		writer.BeginChunk(VDMAKEFOURCC('R', 'A', 'M', ' '));
			writer.WriteUint8(mMemoryMode);
		writer.EndChunk();
		writer.BeginChunk(VDMAKEFOURCC('K', 'R', 'N', 'L'));
			writer.WriteUint8(mKernelMode);
		writer.EndChunk();
		writer.BeginChunk(VDMAKEFOURCC('H', 'D', 'W', 'R'));
			writer.WriteUint8(mHardwareMode);
		writer.EndChunk();
		writer.BeginChunk(VDMAKEFOURCC('V', 'I', 'D', 'O'));
			writer.WriteUint8(mVideoStandard);
		writer.EndChunk();

		if (mbDualPokeys) {
			writer.BeginChunk(VDMAKEFOURCC('S', 'T', 'R', 'O'));
			writer.EndChunk();
		}

		if (mpVBXE) {
			writer.BeginChunk(VDMAKEFOURCC('V', 'B', 'X', 'E'));
			writer.WriteUint16(mbVBXEUseD7xx ? 0xD700 : 0xD600);
			writer.WriteBool(mbVBXESharedMemory);
			writer.EndChunk();
		}

		if (mbMapRAM) {
			writer.BeginChunk(VDMAKEFOURCC('M', 'P', 'R', 'M'));
			writer.EndChunk();
		}

		if (mpSlightSID) {
			writer.BeginChunk(VDMAKEFOURCC('S', 'S', 'I', 'D'));
			writer.EndChunk();
		}

		if (mpSoundBoard) {
			writer.BeginChunk(VDMAKEFOURCC('S', 'N', 'B', 'D'));
			writer.WriteUint16(mSoundBoardMemBase);
			writer.EndChunk();
		}

		if (mpCovox) {
			writer.BeginChunk(VDMAKEFOURCC('C', 'O', 'V', 'X'));
			writer.EndChunk();
		}
	writer.EndChunk();

	// write save state version
	writer.BeginChunk(VDMAKEFOURCC('I', 'S', 'V', 'R'));
	writer.WriteData(kSaveStateVersion, (sizeof kSaveStateVersion) - 1);
	writer.EndChunk();

	// write program info
	writer.BeginChunk(VDMAKEFOURCC('I', 'P', 'R', 'G'));
	const VDStringA& verStr = VDTextWToU8(AT_PROGRAM_NAME_STR L" " AT_VERSION_STR AT_VERSION_DEBUG_STR AT_VERSION_PRERELEASE_STR, -1);
	writer.WriteData(verStr.data(), verStr.size());
	writer.EndChunk();

	// write references
	writer.BeginChunk(VDMAKEFOURCC('L', 'I', 'S', 'T'));
	writer.WriteUint32(VDMAKEFOURCC('R', 'E', 'F', 'S'));

		// write kernel checksum chunk
		writer.BeginChunk(VDMAKEFOURCC('K', 'R', 'N', 'L'));
		writer.WriteUint32(ComputeKernelChecksum());
		writer.EndChunk();

	writer.EndChunk();

	// write architectural state
	writer.BeginChunk(VDMAKEFOURCC('L', 'I', 'S', 'T'));
	writer.WriteUint32(VDMAKEFOURCC('A', 'R', 'C', 'H'));
	writer.WriteSection(kATSaveStateSection_Arch);

	uint32 size = GetMemorySizeForMemoryMode(mMemoryMode);

	// In XRAM modes that have less than 576K of memory we push up
	// the XRAM in mMemory to aid in VBXE emu, which we must account
	// for here.
	if (size > 0x10000 && size < 0x90000) {
		writer.BeginChunk(VDMAKEFOURCC('R', 'A', 'M', ' '));
		writer.WriteUint32(0);
		writer.WriteData(mMemory, 0x10000);
		writer.WriteData(mMemory + 0x50000, size - 0x10000);
		writer.EndChunk();
	} else {
		writer.BeginChunk(VDMAKEFOURCC('R', 'A', 'M', ' '));
		writer.WriteUint32(0);
		writer.WriteData(mMemory, size);
		writer.EndChunk();
	}

	writer.EndChunk();

	// write private state
	writer.BeginChunk(VDMAKEFOURCC('L', 'I', 'S', 'T'));
	writer.WriteUint32(VDMAKEFOURCC('A', 'T', 'R', 'A'));

	writer.WriteSection(kATSaveStateSection_Private);

	writer.EndChunk();

	// call termination handlers
	writer.WriteSection(kATSaveStateSection_End);
}

void ATSimulator::UpdateCartEnable() {
	bool enabled = true;

	if (mpUltimate1MB)
		enabled = mpUltimate1MB->IsExternalCartEnabled();

	if (mpSIDE)
		mpSIDE->SetExternalEnable(enabled);
}

void ATSimulator::UpdateXLCartridgeLine() {
	if (mHardwareMode == kATHardwareMode_800XL || mHardwareMode == kATHardwareMode_1200XL || mHardwareMode == kATHardwareMode_XEGS) {
		// TRIG3 indicates a cartridge on XL/XE. MULE fails if this isn't set properly,
		// because for some bizarre reason it jumps through WARMSV!
		bool rd5 = false;

		if (mpSIDE && mpSIDE->IsCartEnabled())
			rd5 = true;

		if (mpMyIDE && mpMyIDE->IsLeftCartEnabled())
			rd5 = true;
		
		if (!rd5) {
			for(int i=0; i<2; ++i) {
				if (mpCartridge[i] && mpCartridge[i]->IsABxxMapped()) {
					rd5 = true;
					break;
				}
			}
		}

		if (mpUltimate1MB) {
			mpUltimate1MB->SetCartActive(rd5);

			if (!mpUltimate1MB->IsExternalCartEnabled())
				rd5 = false;

			if (mpUltimate1MB->IsCartEnabled())
				rd5 = true;
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

void ATSimulator::UpdateCartridgeSwitch() {
	if (mpSIDE)
		mpSIDE->SetSDXEnabled(mbCartridgeSwitch);
}

void ATSimulator::UpdateBanking(uint8 currBank) {
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

	for(size_t i=0; i<vdcountof(mKernelSymbolsModuleIds); ++i) {
		if (mKernelSymbolsModuleIds[i]) {
			deb->UnloadSymbols(mKernelSymbolsModuleIds[i]);
			mKernelSymbolsModuleIds[i] = 0;
		}
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
		case kATKernelMode_LLE_OSB:
			mpKernelUpperROM = mLLEOSBKernelROM;

			if (mbAutoLoadKernelSymbols) {
				const VDStringW& symbolPath = VDMakePath(VDGetProgramPath().c_str(), L"kernel.lst");
				if (VDDoesPathExist(symbolPath.c_str()))
					mKernelSymbolsModuleIds[0] = deb->LoadSymbols(symbolPath.c_str());

				const VDStringW& symbolPath2 = VDMakePath(VDGetProgramPath().c_str(), L"kernel.lab");
				if (VDDoesPathExist(symbolPath2.c_str()))
					mKernelSymbolsModuleIds[1] = deb->LoadSymbols(symbolPath2.c_str());
			}
			break;
		case kATKernelMode_LLE_XL:
			mpKernelLowerROM = mLLEXLKernelROM;
			mpKernelSelfTestROM = mLLEXLKernelROM + 0x1000;
			mpKernelUpperROM = mLLEXLKernelROM + 0x1800;

			if (mbAutoLoadKernelSymbols) {
				const VDStringW& symbolPath = VDMakePath(VDGetProgramPath().c_str(), L"kernelxl.lst");
				if (VDDoesPathExist(symbolPath.c_str()))
					mKernelSymbolsModuleIds[0] = deb->LoadSymbols(symbolPath.c_str());

				const VDStringW& symbolPath2 = VDMakePath(VDGetProgramPath().c_str(), L"kernelxl.lab");
				if (VDDoesPathExist(symbolPath2.c_str()))
					mKernelSymbolsModuleIds[1] = deb->LoadSymbols(symbolPath2.c_str());
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

void ATSimulator::UpdateVBXEPage() {
	if (mpVBXE)
		mpVBXE->SetRegisterBase(mpUltimate1MB ? mpUltimate1MB->GetVBXEPage() : mbVBXEUseD7xx ? 0xD7 : 0xD6);
}

void ATSimulator::UpdateSoundBoardPage() {
	if (mpSoundBoard)
		mpSoundBoard->SetMemBase(mpUltimate1MB ? mpUltimate1MB->IsSoundBoardEnabled() ? 0xD2C0 : 0 : mSoundBoardMemBase);
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

	if (mpMyIDE && mpMyIDE->IsVersion2()) {
		mpMyIDE->LoadFirmware(NULL, 0);

		const wchar_t *path = mROMImagePaths[kATROMImage_MyIDEII].c_str();

		if (VDDoesPathExist(path)) {
			try {
				mpMyIDE->LoadFirmware(path);
			} catch(const MyError&) {
			}
		}
	}
}

void ATSimulator::ReloadU1MBFirmware() {
	if (mpUltimate1MB) {
		mpUltimate1MB->LoadFirmware(NULL);

		bool success = false;

		const wchar_t *path = mROMImagePaths[kATROMImage_Ultimate1MB].c_str();
		if (VDDoesPathExist(path)) {
			try {
				mpUltimate1MB->LoadFirmware(path);
				success = true;
			} catch(const MyError&) {
			}
		}

		if (!success) {
			vdfastvector<uint8> buf;
			if (ATLoadKernelResourceLZPacked(IDR_U1MBBIOS, buf))
				mpUltimate1MB->LoadFirmware(buf.data(), buf.size());
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
	mpMemMan->SetLayerName(mpMemLayerLoRAM, "Low RAM");
	mpMemMan->EnableLayer(mpMemLayerLoRAM, true);

	if (hiMemPages) {
		mpMemLayerHiRAM = mpMemMan->CreateLayer(kATMemoryPri_BaseRAM, mMemory + 0xD800, 0xD8, hiMemPages, false);
		mpMemMan->SetLayerName(mpMemLayerHiRAM, "High RAM");
		mpMemMan->EnableLayer(mpMemLayerHiRAM, true);
	}

	// create extended RAM layer
	mpMemLayerExtendedRAM = mpMemMan->CreateLayer(kATMemoryPri_ExtRAM, mMemory, 0x40, 16 * 4, false);
	mpMemMan->SetLayerName(mpMemLayerExtendedRAM, "Extended RAM");

	// create kernel ROM layer(s)
	if (mpKernelSelfTestROM) {
		mpMemLayerSelfTestROM = mpMemMan->CreateLayer(kATMemoryPri_ROM, mpKernelSelfTestROM, 0x50, 0x08, true);
		mpMemMan->SetLayerName(mpMemLayerSelfTestROM, "Self-test ROM");
	}

	if (mpKernelLowerROM)
		mpMemLayerLowerKernelROM = mpMemMan->CreateLayer(kATMemoryPri_ROM, mpKernelLowerROM, 0xC0, 0x10, true);
	else if (mpKernel5200ROM)
		mpMemLayerLowerKernelROM = mpMemMan->CreateLayer(kATMemoryPri_ROM, mpKernel5200ROM, 0xF0, 0x08, true);

	if (mpKernelUpperROM)
		mpMemLayerUpperKernelROM = mpMemMan->CreateLayer(kATMemoryPri_ROM, mpKernelUpperROM, 0xD8, 0x28, true);
	else if (mpKernel5200ROM)
		mpMemLayerUpperKernelROM = mpMemMan->CreateLayer(kATMemoryPri_ROM, mpKernel5200ROM, 0xF8, 0x08, true);

	if (mpMemLayerLowerKernelROM) {
		mpMemMan->SetLayerName(mpMemLayerLowerKernelROM, "Lower kernel ROM");
		mpMemMan->EnableLayer(mpMemLayerLowerKernelROM, true);
	}

	if (mpMemLayerUpperKernelROM) {
		mpMemMan->SetLayerName(mpMemLayerUpperKernelROM, "Upper kernel ROM");
		mpMemMan->EnableLayer(mpMemLayerUpperKernelROM, true);
	}

	// create BASIC ROM layer
	mpMemLayerBASICROM = mpMemMan->CreateLayer(kATMemoryPri_ROM, mBASICROM, 0xA0, 0x20, true);
	mpMemMan->SetLayerName(mpMemLayerBASICROM, "BASIC ROM");

	// create game ROM layer
	if (mHardwareMode == kATHardwareMode_XEGS) {
		mpMemLayerGameROM = mpMemMan->CreateLayer(kATMemoryPri_ROM, mGameROM, 0xA0, 0x20, true);
		mpMemMan->SetLayerName(mpMemLayerGameROM, "Game ROM");
	}

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
	mpMemMan->SetLayerName(mpMemLayerANTIC, "ANTIC");

	handlerTable.mpThis = &mGTIA;
	handlerTable.mpDebugReadHandler = BindReadHandlerConst<ATGTIAEmulator, &ATGTIAEmulator::DebugReadByte>;
	handlerTable.mpReadHandler = BindReadHandler<ATGTIAEmulator, &ATGTIAEmulator::ReadByte>;
	handlerTable.mpWriteHandler = BindWriteHandler<ATGTIAEmulator, &ATGTIAEmulator::WriteByte>;

	if (mHardwareMode == kATHardwareMode_5200)
		mpMemLayerGTIA = mpMemMan->CreateLayer(kATMemoryPri_Hardware, handlerTable, 0xC0, 0x04);
	else
		mpMemLayerGTIA = mpMemMan->CreateLayer(kATMemoryPri_Hardware, handlerTable, 0xD0, 0x01);

	mpMemMan->SetLayerName(mpMemLayerGTIA, "GTIA");

	handlerTable.mpThis = &mPokey;
	handlerTable.mpDebugReadHandler = BindReadHandlerConst<ATPokeyEmulator, &ATPokeyEmulator::DebugReadByte>;
	handlerTable.mpReadHandler = BindReadHandler<ATPokeyEmulator, &ATPokeyEmulator::ReadByte>;
	handlerTable.mpWriteHandler = BindWriteHandler<ATPokeyEmulator, &ATPokeyEmulator::WriteByte>;

	if (mHardwareMode == kATHardwareMode_5200)
		mpMemLayerPOKEY = mpMemMan->CreateLayer(kATMemoryPri_Hardware, handlerTable, 0xE8, 0x04);
	else
		mpMemLayerPOKEY = mpMemMan->CreateLayer(kATMemoryPri_Hardware, handlerTable, 0xD2, 0x01);

	mpMemMan->SetLayerName(mpMemLayerPOKEY, "POKEY");

	mpMemMan->EnableLayer(mpMemLayerANTIC, true);
	mpMemMan->EnableLayer(mpMemLayerGTIA, true);
	mpMemMan->EnableLayer(mpMemLayerPOKEY, true);

	if (mHardwareMode != kATHardwareMode_5200) {
		if (mbMapRAM) {
			mpMemLayerHiddenRAM = mpMemMan->CreateLayer(kATMemoryPri_ExtRAM, mMemory + 0xD000, 0x50, 0x08, false);
			mpMemMan->SetLayerName(mpMemLayerHiddenRAM, "Hidden RAM");
		}

		handlerTable.mpThis = &mPIA;
		handlerTable.mpDebugReadHandler = BindReadHandlerConst<ATPIAEmulator, &ATPIAEmulator::DebugReadByte>;
		handlerTable.mpReadHandler = BindReadHandler<ATPIAEmulator, &ATPIAEmulator::ReadByte>;
		handlerTable.mpWriteHandler = BindWriteHandler<ATPIAEmulator, &ATPIAEmulator::WriteByte>;
		mpMemLayerPIA = mpMemMan->CreateLayer(kATMemoryPri_Hardware, handlerTable, 0xD3, 0x01);
		mpMemMan->SetLayerName(mpMemLayerPIA, "PIA");
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
		mpMemMan->SetLayerName(mpMemLayerRT8, "R-Time 8");
		mpMemMan->EnableLayer(mpMemLayerRT8, true);
	}

	// Hook layer
	mpMemLayerHook = mpMemMan->CreateLayer(kATMemoryPri_ROM, mHookPage == 0xD700 ? mHookROM + 0x100 : mHookROM, mHookPageByte, 0x01, true);
	mpMemMan->EnableLayer(mpMemLayerHook, kATMemoryAccessMode_CPURead, true);

	if (mpUltimate1MB) {
		mpUltimate1MB->SetMemoryLayers(
			mpMemLayerLowerKernelROM,
			mpMemLayerUpperKernelROM,
			mpMemLayerBASICROM,
			mpMemLayerSelfTestROM,
			mpMemLayerGameROM);
	}
}

void ATSimulator::ShutdownMemoryMap() {
	if (mpUltimate1MB)
		mpUltimate1MB->SetMemoryLayers(NULL, NULL, NULL, NULL, NULL);

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
	mpUIRenderer->Update();

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

void ATSimulator::GTIARequestAnticSync(int offset) {
	mAntic.SyncWithGTIA(offset);
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
	mIRQController.Assert(kATIRQSource_POKEY, cpuBased);
}

void ATSimulator::PokeyNegateIRQ(bool cpuBased) {
	mIRQController.Negate(kATIRQSource_POKEY, cpuBased);
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
	mIRQController.Assert(kATIRQSource_VBXE, true);
}

void ATSimulator::VBXENegateIRQ() {
	mIRQController.Negate(kATIRQSource_VBXE, true);
}

void ATSimulator::UpdatePrinterHook() {
}

void ATSimulator::UpdateCIOVHook() {
}
