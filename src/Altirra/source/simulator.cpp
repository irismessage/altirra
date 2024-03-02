#include "stdafx.h"
#include <vd2/system/binary.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include "simulator.h"
#include "cassette.h"
#include "harddisk.h"
#include "console.h"
#include "hlekernel.h"
#include "joystick.h"
#include "ksyms.h"
#include "decmath.h"
#include "debugger.h"
#include "oshelper.h"
#include "savestate.h"
#include "resource.h"

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
	, mKernelSymbolsModuleId(0)
	, mpHLEKernel(ATCreateHLEKernel())
	, mInitialCartBank(0)
{
	mpHLEKernel->Init(mCPU, *this);

	mMemoryMode = kATMemoryMode_320K;
	mKernelMode = kATKernelMode_OSB;
	SetKernelMode(kATKernelMode_HLE);
	mHardwareMode = kATHardwareMode_800XL;
	mCartMode = kATCartridgeMode_None;

	mCPU.Init(this);
	mCPU.SetHLE(mpHLEKernel->AsCPUHLE());
	mGTIA.Init(this);
	mAntic.Init(this, &mGTIA);
	mPokey.Init(this, &mScheduler);
	mPokey2.Init(&g_pokeyDummyConnection, &mScheduler);

	mpCassette = new ATCassetteEmulator;
	mpCassette->Init(&mPokey, &mScheduler);

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
	mbBASICEnabled = false;
	mbDualPokeys = false;

	SetDiskSIOPatchEnabled(true);
	SetCassetteSIOPatchEnabled(true);
	SetFPPatchEnabled(true);

	mBreakOnScanline = -1;

	mKeyboardControllerData = 0;
	mMouseDeltaX = 0;
	mMouseFineDeltaX = 0;
	mMouseDeltaY = 0;
	mMouseFineDeltaY = 0;

	memset(mDummyRead, 0xFF, sizeof mDummyRead);

	ColdReset();
}

ATSimulator::~ATSimulator() {
	delete mpHardDisk;
	delete mpJoysticks;
	delete mpCassette;
	delete mpHLEKernel;
}

void ATSimulator::Init(void *hwnd) {
	mpJoysticks = ATCreateJoystickManager();
	if (!mpJoysticks->Init(hwnd)) {
		delete mpJoysticks;
		mpJoysticks = NULL;
	}
}

void ATSimulator::Shutdown() {
	if (mpJoysticks) {
		delete mpJoysticks;
		mpJoysticks = NULL;
	}
}

void ATSimulator::LoadROMs() {
	VDStringW ppath(VDGetProgramPath());
	VDStringW path;

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
		*it = mCallbacks.back();
		mCallbacks.pop_back();
	}
}

void ATSimulator::NotifyEvent(ATSimulatorEvent ev) {
	for(Callbacks::const_iterator it(mCallbacks.begin()), itEnd(mCallbacks.end()); it!=itEnd; ++it) {
		IATSimulatorCallback *cb = *it;
		cb->OnSimulatorEvent(ev);
	}
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
	RecomputeMemoryMaps();
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

	mPORTA = 0xFF;
	mPORTACTL = 0x00;
	mPORTB = 0xB3;
	mPORTBCTL = 0x00;

	mCPU.ColdReset();
	mAntic.ColdReset();
	mPokey.ColdReset();
	mpCassette->ColdReset();

	for(int i=0; i<4; ++i)
		mDiskDrives[i].Reset();

	if (mpHardDisk)
		mpHardDisk->ColdReset();

	memset(mMemory, 0, sizeof mMemory);
	mCartBank = mInitialCartBank;
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
}

void ATSimulator::WarmReset() {
	mAntic.WarmReset();
	mCPU.WarmReset();

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

	uint8 header[4];
	f.read(header, 4);

	sint64 size = f.size();

	if (header[0] != 0xFF || header[1] != 0xFF) {
		if ((header[0] == 'A' && header[1] == 'T' && header[2] == '8' && header[3] == 'X') ||
			(header[2] == 'P' && header[3] == '3') ||
			(header[2] == 'P' && header[3] == '2') ||
			(header[0] == 0x96 && header[1] == 0x02) ||
			(!(size & 127) && size <= 65535*128 && !_wcsicmp(VDFileSplitExt(s), L".xfd")))
		{
			f.close();
			mDiskDrives[0].LoadDisk(s);
			return;
		}

		throw MyError("Invalid Atari executable: bad header.");
	}

	f.seek(2);

	uint32 len = (uint32)f.size() - 2;
	mProgramToLoad.resize(len);
	f.read(mProgramToLoad.data(), len);
	mProgramLoadIndex = 0;

	// hook DSKINV
	mCPU.SetHook(0xE453, true);

	// cold restart the system and wait for DSKINV hook to fire
	ColdReset();
}

void ATSimulator::LoadCartridge(const wchar_t *s) {
	if (!s) {
		mInitialCartBank = 0;
		mCartBank = 0;
		mCartMode = kATCartridgeMode_None;
		UpdateXLCartridgeLine();
		RecomputeMemoryMaps();
		return;
	}

	VDFile f(s);

	sint64 size = f.size();

	if (size < 1024 || size > 1048576 + 16)
		throw MyError("Unsupported cartridge size.");

	// check for header
	char buf[16];
	f.read(buf, 16);

	bool validHeader = false;
	uint32 size32 = (uint32)size;

	if (!memcmp(buf, "CART", 4)) {
		uint32 type = VDReadUnalignedBEU32(buf + 4);
		uint32 checksum = VDReadUnalignedBEU32(buf + 8);

		size32 -= 16;
		mCARTROM.resize(size32);
		f.read(mCARTROM.data(), size32);

		uint32 csum = 0;
		for(uint32 i=0; i<size32; ++i)
			csum += mCARTROM[i];

		if (csum == checksum) {
			validHeader = true;

			switch(type) {
				case 0:
					mCartMode = kATCartridgeMode_8K;
					break;
				case 1:
					mCartMode = kATCartridgeMode_16K;
					break;
				case 11:
					mCartMode = kATCartridgeMode_XEGS_32K;
					break;
				case 41:
					mCartMode = kATCartridgeMode_MaxFlash_128K;
					break;
				case 42:
					mCartMode = kATCartridgeMode_MaxFlash_1024K;
					break;
			}
		}
	}

	if (!validHeader) {
		mCARTROM.resize(size32);
		f.seek(0);
		f.read(mCARTROM.data(), size32);

		if (size32 <= 8192)
			mCartMode = kATCartridgeMode_8K;
		else if (size32 == 16384)
			mCartMode = kATCartridgeMode_16K;
		else if (size32 == 32768)
			mCartMode = kATCartridgeMode_XEGS_32K;
		else if (size32 == 131072)
			mCartMode = kATCartridgeMode_MaxFlash_128K;
		else if (size32 == 1048576)
			mCartMode = kATCartridgeMode_MaxFlash_1024K;
		else
			throw MyError("Unsupported cartridge size.");
	}

	uint32 allocSize = size32;

	// set initial bank and alloc size
	switch(mCartMode) {
		case kATCartridgeMode_8K:
			mInitialCartBank = 0;
			allocSize = 8192;
			break;
		case kATCartridgeMode_16K:
			mInitialCartBank = 0;
			allocSize = 16384;
			break;
		case kATCartridgeMode_XEGS_32K:
			mInitialCartBank = 3;
			allocSize = 32768;
			break;
		case kATCartridgeMode_MaxFlash_128K:
			mInitialCartBank = 0;
			allocSize = 131072;
			break;

		case kATCartridgeMode_MaxFlash_1024K:
			mInitialCartBank = 127;
			allocSize = 1048576;
			break;
	}

	mCartBank = mInitialCartBank;

	mCARTROM.resize(allocSize, 0);

	RecomputeMemoryMaps();
	UpdateXLCartridgeLine();
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

	mPORTA |= 0xf0;
}

uint8 ATSimulator::DebugReadByte(uint16 addr) {
	const uint8 *src = mpReadMemoryMap[addr >> 8];
	if (src)
		return src[addr & 0xff];

	// D000-D7FF	2K		hardware address
	switch(addr & 0xff00) {
//	case 0xD000:
//		return mGTIA.ReadByte(addr & 0x1f);
	case 0xD200:
		return mPokey.DebugReadByte(addr & 0xff);
	case 0xD300:
		switch(addr & 0x03) {
		case 0x00:
			return mPORTA;
		case 0x01:
			return mPORTB;
		case 0x02:
			return mPORTACTL;
		case 0x03:
			return mPORTBCTL;
		}
		return 0;

//	case 0xD400:
//		return mAntic.ReadByte(addr & 0xff);

	case 0xD600:
		return CPUReadByte(addr);

	default:
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

class ATInvalidSaveStateException : public MyError {
public:
	ATInvalidSaveStateException()
		: MyError("The save state data is invalid.") {}
};

void ATSimulator::LoadState(ATSaveStateReader& reader) {
	mMemoryMode		= (ATMemoryMode)reader.ReadUint8();
	mKernelMode		= (ATKernelMode)reader.ReadUint8();
	mHardwareMode	= (ATHardwareMode)reader.ReadUint8();
	mCartMode		= (ATCartridgeMode)reader.ReadUint8();
	mCartBank		= reader.ReadSint16();
	mPORTA			= reader.ReadUint8();
	mPORTB			= reader.ReadUint8();
	mPORTACTL		= reader.ReadUint8();
	mPORTBCTL		= reader.ReadUint8();

	uint32 size = GetMemorySizeForMemoryMode(mMemoryMode);
	uint32 rsize = reader.ReadUint32();
	if (rsize != size)
		throw ATInvalidSaveStateException();
	reader.ReadData(mMemory, size);

	UpdateKernel();
	UpdateXLCartridgeLine();
	RecomputeMemoryMaps();

	mCPU.LoadState(reader);
	mAntic.LoadState(reader);
	mGTIA.LoadState(reader);
	mPokey.LoadState(reader);
}

void ATSimulator::SaveState(ATSaveStateWriter& writer) {
	if (kAdvanceResult_Running != AdvanceUntilInstructionBoundary())
		throw MyError("The emulation state cannot be saved because emulation is stopped in the middle of an instruction.");

	writer.WriteUint8(mMemoryMode);
	writer.WriteUint8(mKernelMode);
	writer.WriteUint8(mHardwareMode);
	writer.WriteUint8(mCartMode);
	writer.WriteSint16(mCartBank);
	writer.WriteUint8(mPORTA);
	writer.WriteUint8(mPORTB);
	writer.WriteUint8(mPORTACTL);
	writer.WriteUint8(mPORTBCTL);

	uint32 size = GetMemorySizeForMemoryMode(mMemoryMode);
	writer.WriteUint32(size);
	writer.WriteData(mMemory, size);

	mCPU.SaveState(writer);
	mAntic.SaveState(writer);
	mGTIA.SaveState(writer);
	mPokey.SaveState(writer);
}

void ATSimulator::UpdateXLCartridgeLine() {
	if (mHardwareMode == kATHardwareMode_800XL) {
		// TRIG3 indicates a cartridge on XL/XE. MULE fails if this isn't set properly,
		// because for some bizarre reason it jumps through WARMSV!
		mGTIA.SetControllerTrigger(3, !mCartMode);
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
}

void ATSimulator::RecomputeMemoryMaps() {
	mpReadMemoryMap = mReadMemoryMap;
	mpCPUReadPageMap = mReadMemoryMap;
	mpWriteMemoryMap = mWriteMemoryMap;
	mpCPUWritePageMap = mWriteMemoryMap;
	mpAnticMemoryMap = mAnticMemoryMap;

	// 0000-3FFF	16K		memory
	for(int i=0; i<64; ++i) {
		mReadMemoryMap[i] = mMemory + (i << 8);
		mWriteMemoryMap[i] = mMemory + (i << 8);
	}

	// 4000-7FFF	16K		memory (bank switched; XE only)
	uint8 *bankbase = mMemory + 0x4000;
	if (mMemoryMode >= kATMemoryMode_128K && !(mPORTB & 0x10)) {
		bankbase = mMemory + ((mPORTB & 0x0c) << 12) + 0x10000;

		if (mMemoryMode >= kATMemoryMode_320K) {
			bankbase += (uint32)(~mPORTB & 0x60) << 11;

			if (mMemoryMode >= kATMemoryMode_576K) {
				if (!(mPORTB & 0x02))
					bankbase += 0x40000;
				if (mMemoryMode >= kATMemoryMode_1088K) {
					if (!(mPORTB & 0x80))
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
	if ((mCartMode == kATCartridgeMode_XEGS_32K || mCartMode == kATCartridgeMode_16K) && mCartBank >= 0) {
		const uint8 *cartbase = mCARTROM.data() + 0x2000 * mCartBank;
		for(int i=0; i<32; ++i) {
			mReadMemoryMap[i+128] = cartbase + (i << 8);
			mWriteMemoryMap[i+128] = mDummyWrite;
		}
	} else {
		uint8 *base = mMemory + 0x8000;

		for(int i=0; i<32; ++i) {
			mReadMemoryMap[i+128] = base + (i << 8);
			mWriteMemoryMap[i+128] = base + (i << 8);
		}
	}

	// A000-BFFF	8K		Cart A (BASIC)
	bool enableBasic = mbBASICEnabled;

	if (mHardwareMode == kATHardwareMode_800XL) {
		if (mPORTB & 2)
			enableBasic = false;
	}

	if (enableBasic) {
		for(int i=0; i<32; ++i) {
			mReadMemoryMap[i+0xA0] = mBASICROM + (i << 8);
			mWriteMemoryMap[i+0xA0] = mDummyWrite;
		}
	} else if (mCartMode && mCartBank >= 0) {
		const uint8 *cartbase = mCARTROM.data();

		switch(mCartMode) {
			case kATCartridgeMode_8K:
				break;
			case kATCartridgeMode_16K:
				cartbase += 0x2000;
				break;
			case kATCartridgeMode_XEGS_32K:
				cartbase += 0x6000;
				break;
			case kATCartridgeMode_MaxFlash_128K:
			case kATCartridgeMode_MaxFlash_1024K:
				cartbase += 0x2000 * mCartBank;
				break;
		}

		for(int i=0; i<32; ++i) {
			mReadMemoryMap[i+0xA0] = cartbase + (i << 8);
			mWriteMemoryMap[i+0xA0] = mDummyWrite;
		}
	} else {
		uint8 *base = mMemory + 0xA000;

		for(int i=0; i<32; ++i) {
			mReadMemoryMap[i+0xA0] = base + (i << 8);
			mWriteMemoryMap[i+0xA0] = base + (i << 8);
		}
	}

	// C000-CFFF	4K		Memory or Kernel ROM (XL/XE only)
	if ((mPORTB & 0x01) && mHardwareMode == kATHardwareMode_800XL) {
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
	if (mMemoryMode < kATMemoryMode_64K || (mPORTB & 0x01)) {
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
	if (mMemoryMode == kATMemoryMode_128K && !(mPORTB & 0x20))
		anticbankbase = mMemory + ((mPORTB & 0x0c) << 12) + 0x10000;

	for(int i=0; i<64; ++i)
		mAnticMemoryMap[i+64] = anticbankbase + (i << 8);

	// 5000-57FF	2K		self test memory (bank switched; XL/XE only)
	if (mMemoryMode >= kATMemoryMode_64K && mMemoryMode != kATMemoryMode_1088K && !(mPORTB & 0x80) && mpKernelSelfTestROM) {
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
			return mPORTA;
		case 0x01:
			return mPORTB;
		case 0x02:
			return mPORTACTL;
		case 0x03:
			return mPORTBCTL;
		}
		return 0;

	case 0xD400:
		return mAntic.ReadByte(addr & 0x0f);

	case 0xD600:
		return (addr & 1) ? 0xD6 : (addr + 0x0F);
	}

	if (!((mReadBreakAddress ^ addr) & 0xff00)) {
		if (mpBPReadPage)
			return mpBPReadPage[addr & 0xff];
	}
	return 0xFF;
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
		case 0x01:
			if (mPORTBCTL & 0x04) {
				mPORTB = value;
				if (mMemoryMode >= kATMemoryMode_64K)
					RecomputeMemoryMaps();
			}
			break;
		case 0x02:
			mPORTACTL = value;
			if (mpCassette)
				mpCassette->SetMotorEnable(!(value & 0x08));
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
		switch(mCartMode) {
		case kATCartridgeMode_8K:
		case kATCartridgeMode_16K:
			break;
		case kATCartridgeMode_XEGS_32K:
			if (!(value & 0x80))
				mCartBank = value & 3;
			else
				mCartBank = -1;
			break;
		case kATCartridgeMode_MaxFlash_128K:
			if (addr < 0xD520) {
				if (addr < 0xD510)
					mCartBank = addr & 15;
				else
					mCartBank = -1;
			}
			break;
		case kATCartridgeMode_MaxFlash_1024K:
			if (addr < 0xD580)
				mCartBank = addr & 127;
			else
				mCartBank = -1;
			break;
		}

		RecomputeMemoryMaps();
		break;
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
	if (mMemoryMode >= kATMemoryMode_64K && !(mPORTB & 0x01))
		return 0;

	if (address == ATKernelSymbols::DSKINV) {		// DSKINV
		if (!mProgramToLoad.empty())
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
				uint8 status = disk.ReadSector(bufadr, 128, sector, this);
				WriteByte(0x0303, status);

				// Set DDEVIC and DUNIT
				WriteByte(ATKernelSymbols::DDEVIC, 0x31);
				WriteByte(ATKernelSymbols::DUNIT, drive);

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

				ATConsoleTaggedPrintf("HOOK: Intercepting DSKINV read: drive=%02X, cmd=%02X, buf=%04X, sector=%04X, status=%02X\n", drive, cmd, bufadr, sector, status);

				mGTIA.SetStatusFlags(1 << (drive - 1));
				mGTIA.ResetStatusFlags(1 << (drive - 1));

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
				uint16 sector = ReadByte(0x030A) + 256*ReadByte(0x030B);

				uint8 status = disk.ReadSector(bufadr, len, sector, this);
				WriteByte(0x0303, status);

				mCPU.SetY(status);
				mCPU.SetP((mCPU.GetP() & 0x7d) | (status & 0x80) | (!status ? 0x02 : 0x00));

				ATConsoleTaggedPrintf("HOOK: Intercepting disk SIO read: buf=%04X, len=%04X, sector=%04X, status=%02X\n", bufadr, len, sector, status);

				mGTIA.PulseStatusFlags(1 << (device - 0x31));
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

				mCPU.SetY(status);
				mCPU.SetP((mCPU.GetP() & 0x7d) | (status & 0x80) | (!status ? 0x02 : 0x00));

				ATConsoleTaggedPrintf("HOOK: Intercepting disk SIO write: buf=%04X, len=%04X, sector=%04X, status=%02X\n", bufadr, len, sector, status);

				mGTIA.PulseStatusFlags(1 << (device - 0x31));
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

				ATConsoleTaggedPrintf("HOOK: Intercepting cassette SIO read: buf=%04X, len=%04X, status=%02X\n", bufadr, len, status);
				return 0x60;
			}
		}
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
	} else if (address == 0x1FE) {		// NOP for program load
		return 0x60;
	} else if (address == 0x1FF) {		// program load continuation
		return LoadProgramHookCont();
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

	mGTIA.SetCassetteIndicatorVisible(mpCassette->IsLoaded());
	mGTIA.SetCassettePosition(mpCassette->GetPosition());
	mGTIA.UpdateScreen();

	if (mbBreakOnFrameEnd) {
		mbBreakOnFrameEnd = false;
		PostInterruptingEvent(kATSimEvent_EndOfFrame);
	}

	if (mpJoysticks) {
		mpJoysticks->Poll();
		uint32 states = mpJoysticks->GetJoystickPortStates() | mKeyboardControllerData;

//		mPORTA = (uint8)~states;
		mPORTA = (mPORTA & 0xf0) + ((uint8)~states & 0x0f);
		mGTIA.SetControllerTrigger(0, 0 != (states & 0x10000));
		mGTIA.SetControllerTrigger(1, 0 != (states & 0x20000));
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
				mPORTA ^= kReverseXorX[(mPORTA >> 4) & 3];
				++mMouseDeltaX;
			} else {
				static const uint8 kForwardXorX[4]={ 0x20, 0x10, 0x10, 0x20 };
				mPORTA ^= kForwardXorX[(mPORTA >> 4) & 3];
				--mMouseDeltaX;
			}
		}

		if (mMouseDeltaY) {
			if (mMouseDeltaY < 0) {
				static const uint8 kReverseXorY[4]={ 0x40, 0x80, 0x80, 0x40 };
				mPORTA ^= kReverseXorY[(mPORTA >> 6) & 3];
				++mMouseDeltaY;
			} else {
				static const uint8 kForwardXorY[4]={ 0x80, 0x40, 0x40, 0x80 };
				mPORTA ^= kForwardXorY[(mPORTA >> 6) & 3];
				--mMouseDeltaY;
			}
		}
	}
}

uint32 ATSimulator::GTIAGetXClock() {
	uint32 x = mAntic.GetBeamX();

//	return x ? (x - 1)*2 : 226;
	return (x+1)*2;
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
	mCPU.SetHook(0xE453, mbDiskSIOPatchEnabled || mbCassetteSIOPatchEnabled);

	// load next segment
	return LoadProgramHookCont();
}

uint8 ATSimulator::LoadProgramHookCont() {
	// set INITAD to virtual RTS and RUNAD to DSKINV
	mMemory[0x2E0] = 0x53;
	mMemory[0x2E1] = 0xE4;
	mMemory[0x2E2] = 0xFE;
	mMemory[0x2E3] = 0x01;

	// resume loading segments
	const uint8 *src0 = mProgramToLoad.data();
	const uint8 *src = src0 + mProgramLoadIndex;
	const uint8 *srcEnd = src0 + mProgramToLoad.size();
	for(;;) {
		// check if we're done
		if (srcEnd - src < 4) {
			// Looks like we're done. Push DSKINV onto the stack and execute the run routine.
			mCPU.Push(0xE4);
			mCPU.Push(0x53 - 1);
			mCPU.Jump(DebugReadWord(0x2E0));
			mCPU.SetHook(0x1FE, false);
			mCPU.SetHook(0x1FF, false);
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
			ATConsoleWrite("WARNING: Invalid Atari executable: bad start/end range.");
			break;
		}

		// load segment data into memory
		ATConsolePrintf("EXE: Loading program %04X-%04X to %04X-%04X\n", src-src0, (src-src0)+len-1, start, end);

		for(uint32 i=0; i<len; ++i)
			WriteByte(start + i, src[i]);

		src += len;

		// check if INITAD has changed
		if (mMemory[0x2E2] != 0xFE || mMemory[0x2E3] != 0x01)
			break;
	}

	mProgramLoadIndex = src - src0;

	// push virtual load hook ($01FF) onto stack and jsr through (INITAD)
	mCPU.Push(0x01);
	mCPU.Push(0xFE);

	mCPU.Jump(DebugReadWord(0x2E2));
	return 0;
}
