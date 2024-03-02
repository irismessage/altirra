//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2015 Avery Lee
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

#include <stdafx.h>
#include <vd2/system/hash.h>
#include <vd2/system/int128.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/deviceserial.h>
#include "veronica.h"
#include "memorymanager.h"
#include "debuggerlog.h"

ATDebuggerLogChannel g_ATLCVRError(true, false, "VRERROR", "Veronica errors");

void ATCreateDeviceVeronicaEmulator(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATVeronicaEmulator> p(new ATVeronicaEmulator);
	p->SetSettings(pset);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefVeronica = { "veronica", "veronica", L"Veronica", ATCreateDeviceVeronicaEmulator };

ATVeronicaEmulator::ATVeronicaEmulator()
	: mpScheduler(nullptr)
	, mpSlowScheduler(nullptr)
	, mpRunEvent(nullptr)
	, mpMemMan(nullptr)
	, mpMemLayerLeftWindow(nullptr)
	, mpMemLayerRightWindow(nullptr)
	, mpCartridgePort(nullptr)
	, mbVersion1(true)
	, mbCorruptNextCycle(false)
	, mpCoProcWinBase(nullptr)
{
}

ATVeronicaEmulator::~ATVeronicaEmulator() {
}

void *ATVeronicaEmulator::AsInterface(uint32 iid) {
	switch(iid) {
		case IATDeviceMemMap::kTypeID: return static_cast<IATDeviceMemMap *>(this);
		case IATDeviceScheduling::kTypeID: return static_cast<IATDeviceScheduling *>(this);
		case IATDeviceDebugTarget::kTypeID: return static_cast<IATDeviceDebugTarget *>(this);
		case IATDeviceCartridge::kTypeID: return static_cast<IATDeviceCartridge *>(this);
		case IATDebugTargetHistory::kTypeID: return static_cast<IATDebugTargetHistory *>(this);
	}

	return nullptr;
}

void ATVeronicaEmulator::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefVeronica;
}

void ATVeronicaEmulator::GetSettings(ATPropertySet& settings) {
	settings.Clear();
	if (mbVersion1)
		settings.SetBool("version1", true);
}

bool ATVeronicaEmulator::SetSettings(const ATPropertySet& settings) {
	mbVersion1 = settings.GetBool("version1");
	return true;
}

void ATVeronicaEmulator::Init() {
	// initialize memory map
	uintptr *readmap = mCoProc.GetReadMap();
	uintptr *writemap = mCoProc.GetWriteMap();

	for(int i=0; i<256; ++i) {
		readmap[i] = (uintptr)mRAM;
		writemap[i] = (uintptr)mRAM;
	}

	// set up handler for registers at 0x200-20F
	mWriteNode.mpWrite = [](uint32 addr, uint8 val, void *thisptr0) {
		auto *thisptr = (ATVeronicaEmulator *)thisptr0;

		if (addr < 0x210)
			thisptr->WriteVControl(val);
		else
			thisptr->mRAM[addr] = val;

	};
	mWriteNode.mpThis = this;

	mCorruptedReadNode.mpDebugRead = OnCorruptableDebugRead;
	mCorruptedReadNode.mpRead = OnCorruptableRead;
	mCorruptedReadNode.mpThis = this;
	mCorruptedWriteNode.mpWrite = OnCorruptableWrite;
	mCorruptedWriteNode.mpThis = this;

	writemap[0x02] = (uintptr)&mWriteNode + 1;
}

void ATVeronicaEmulator::Shutdown() {
	if (mpCartridgePort) {
		mpCartridgePort->RemoveCartridge(this);
		mpCartridgePort = nullptr;
	}

	if (mpMemLayerRightWindow) {
		mpMemMan->DeleteLayer(mpMemLayerRightWindow);
		mpMemLayerRightWindow = nullptr;
	}

	if (mpMemLayerLeftWindow) {
		mpMemMan->DeleteLayer(mpMemLayerLeftWindow);
		mpMemLayerLeftWindow = nullptr;
	}

	if (mpMemLayerControl) {
		mpMemMan->DeleteLayer(mpMemLayerControl);
		mpMemLayerControl = nullptr;
	}

	if (mpSlowScheduler) {
		mpSlowScheduler->UnsetEvent(mpRunEvent);
		mpSlowScheduler = nullptr;
	}

	mpScheduler = nullptr;
	mpMemMan = nullptr;
}

void ATVeronicaEmulator::WarmReset() {
	mLastSync = ATSCHEDULER_GETTIME(mpScheduler);
}

void ATVeronicaEmulator::ColdReset() {
	memset(mRAM, 0, sizeof mRAM);

	mCoProc.ColdReset();

	mAControl = 0xCC;
	mVControl = 0x3F;

	mPRNG = ATSCHEDULER_GETTIME(mpScheduler) & 0x7FFFFFFF;
	if (!mPRNG)
		mPRNG = 1;

	mpMemMan->SetLayerModes(mpMemLayerRightWindow, kATMemoryAccessMode_0);
	mpMemMan->SetLayerModes(mpMemLayerLeftWindow, kATMemoryAccessMode_0);

	UpdateWindowBase();

	UpdateCoProcWindowDormant();
	UpdateCoProcWindowActive();

	WarmReset();
}

void ATVeronicaEmulator::InitMemMap(ATMemoryManager *memmap) {
	mpMemMan = memmap;

	ATMemoryHandlerTable handlers={};
	handlers.mpDebugReadHandler = OnDebugRead;
	handlers.mpReadHandler = OnRead;
	handlers.mpWriteHandler = OnWrite;
	handlers.mpThis = this;
	handlers.mbPassAnticReads = true;
	handlers.mbPassReads = true;
	handlers.mbPassWrites = true;

	mpMemLayerControl = mpMemMan->CreateLayer(kATMemoryPri_Cartridge1, handlers, 0xD5, 0x01);
	mpMemMan->SetLayerName(mpMemLayerControl, "Veronica control");
	mpMemMan->EnableLayer(mpMemLayerControl, true);

	mpMemLayerLeftWindow = mpMemMan->CreateLayer(kATMemoryPri_Cartridge1, mRAM + 0x12000, 0xA0, 0x20, false);
	mpMemMan->SetLayerName(mpMemLayerLeftWindow, "Veronica left window");

	mpMemLayerRightWindow = mpMemMan->CreateLayer(kATMemoryPri_Cartridge1, mRAM + 0x10000, 0x80, 0x20, false);
	mpMemMan->SetLayerName(mpMemLayerRightWindow, "Veronica right window");
}

bool ATVeronicaEmulator::GetMappedRange(uint32 index, uint32& lo, uint32& hi) const {
	switch(index) {
	case 0:
		lo = 0xD5C0;
		hi = 0xD5C1;
		return true;

	default:
		return false;
	}
}

void ATVeronicaEmulator::InitScheduling(ATScheduler *sch, ATScheduler *slowsch) {
	mpScheduler = sch;
	mpSlowScheduler = slowsch;
}

void ATVeronicaEmulator::InitCartridge(IATDeviceCartridgePort *port) {
	mpCartridgePort = port;
	mpCartridgePort->AddCartridge(this);
}

bool ATVeronicaEmulator::IsRD5Active() const {
	return (mAControl & 0x20) != 0;
}

IATDebugTarget *ATVeronicaEmulator::GetDebugTarget(uint32 index) {
	if (index == 0)
		return this;

	return nullptr;
}

const char *ATVeronicaEmulator::GetName() {
	return "Veronica CPU";
}

ATDebugDisasmMode ATVeronicaEmulator::GetDisasmMode() {
	return kATDebugDisasmMode_65C816;
}

void ATVeronicaEmulator::GetExecState(ATCPUExecState& state) {
	mCoProc.GetExecState(state);
}

void ATVeronicaEmulator::SetExecState(const ATCPUExecState& state) {
	mCoProc.SetExecState(state);
}

uint8 ATVeronicaEmulator::ReadByte(uint32 address) {
	if (address >= 0x10000)
		return 0;

	// We know that all reads are direct, so no need to check for handlers.
	const uintptr pageBase = mCoProc.GetReadMap()[address >> 8];
	
	return *(const uint8 *)(pageBase + address);
}

void ATVeronicaEmulator::ReadMemory(uint32 address, void *dst, uint32 n) {
	const uintptr *readMap = mCoProc.GetReadMap();

	while(n) {
		if (address >= 0x10000) {
			memset(dst, 0, n);
			break;
		}

		uint32 tc = 256 - (address & 0xff);
		if (tc > n)
			tc = n;

		memcpy(dst, (const uint8 *)(readMap[address >> 8] + address), tc);

		n -= tc;
		address += tc;
		dst = (char *)dst + tc;
	}
}

uint8 ATVeronicaEmulator::DebugReadByte(uint32 address) {
	return ReadByte(address);
}

void ATVeronicaEmulator::DebugReadMemory(uint32 address, void *dst, uint32 n) {
	ReadMemory(address, dst, n);
}

void ATVeronicaEmulator::WriteByte(uint32 address, uint8 value) {
	if (address >= 0x10000)
		return;

	const uintptr pageBase = mCoProc.GetWriteMap()[address >> 8];

	if (pageBase & 1) {
		auto& writeNode = *(ATCoProcWriteMemNode *)(pageBase - 1);

		writeNode.mpWrite(address, value, writeNode.mpThis);
	} else {
		*(uint8 *)(pageBase + address) = value;
	}
}

void ATVeronicaEmulator::WriteMemory(uint32 address, const void *src, uint32 n) {
	const uintptr *writeMap = mCoProc.GetWriteMap();

	while(n) {
		if (address >= 0x10000)
			break;

		const uintptr pageBase = writeMap[address >> 8];

		if (pageBase & 1) {
			auto& writeNode = *(ATCoProcWriteMemNode *)(pageBase - 1);

			writeNode.mpWrite(address, *(const uint8 *)src, writeNode.mpThis);
			++address;
			src = (const uint8 *)src + 1;
			--n;
		} else {
			uint32 tc = 256 - (address & 0xff);
			if (tc > n)
				tc = n;

			memcpy((uint8 *)(pageBase + address), src, tc);

			n -= tc;
			address += tc;
			src = (const char *)src + tc;
		}
	}
}

bool ATVeronicaEmulator::GetHistoryEnabled() const {
	return !mHistory.empty();
}

void ATVeronicaEmulator::SetHistoryEnabled(bool enable) {
	if (enable) {
		if (mHistory.empty()) {
			mHistory.resize(131072, ATCPUHistoryEntry());
			mCoProc.SetHistoryBuffer(mHistory.data());
		}
	} else {
		if (!mHistory.empty()) {
			decltype(mHistory) tmp;
			tmp.swap(mHistory);
			mHistory.clear();
			mCoProc.SetHistoryBuffer(nullptr);
		}
	}
}

std::pair<uint32, uint32> ATVeronicaEmulator::GetHistoryRange() const {
	const uint32 hcnt = mCoProc.GetHistoryCounter();

	return std::pair<uint32, uint32>(hcnt - 131072, hcnt);
}

uint32 ATVeronicaEmulator::ExtractHistory(const ATCPUHistoryEntry **hparray, uint32 start, uint32 n) const {
	if (!n || mHistory.empty())
		return 0;

	const uint32 hcnt = mCoProc.GetHistoryCounter();
	uint32 hidx = start;
	const ATCPUHistoryEntry *hstart = mHistory.data();
	const ATCPUHistoryEntry *hend = hstart + 131072;
	const ATCPUHistoryEntry *hsrc = hstart + (hidx & 131071);

	for(uint32 i=0; i<n; ++i) {
		*hparray++ = hsrc;

		if (++hsrc == hend)
			hsrc = hstart;
	}

	return n;
}

uint32 ATVeronicaEmulator::ConvertRawTimestamp(uint32 rawTimestamp) const {
	return mLastSync - ((mCoProc.GetTimeBase() - rawTimestamp + 7) >> 3);
}

void ATVeronicaEmulator::OnScheduledEvent(uint32 id) {
	mpRunEvent = mpSlowScheduler->AddEvent(100, this, 1);

	Sync();
}

sint32 ATVeronicaEmulator::OnDebugRead(void *thisptr0, uint32 addr) {
	const auto thisptr = (ATVeronicaEmulator *)thisptr0;

	if (addr == 0xD5C0) {
		thisptr->Sync();
		return thisptr->mAControl;
	}

	return -1;
}

sint32 ATVeronicaEmulator::OnRead(void *thisptr0, uint32 addr) {
	const auto thisptr = (ATVeronicaEmulator *)thisptr0;

	if (addr == 0xD5C0) {
		thisptr->Sync();
		return thisptr->mAControl;
	}

	return -1;
}

bool ATVeronicaEmulator::OnWrite(void *thisptr0, uint32 addr, uint8 value) {
	const auto thisptr = (ATVeronicaEmulator *)thisptr0;

	if (addr == 0xD5C0) {
		// D2, D6 unused and always 1
		value |= 0x44;

		if (thisptr->mAControl & 1)
			thisptr->Sync();

		const uint8 delta = thisptr->mAControl ^ value;

		if (delta) {
			// D0 is reset control and requires a full sync
			// D1 swaps RAM banks and requires a full sync
			// D7 is shared semaphore and requires a full sync
			if (delta & 0x83) {
				if (value & 1)
					thisptr->mpSlowScheduler->SetEvent(100, thisptr, 1, thisptr->mpRunEvent);
				else
					thisptr->mpSlowScheduler->UnsetEvent(thisptr->mpRunEvent);

				// run cycles from the past
				thisptr->Sync();

				// check if the semaphore has changed
				if (delta & 0x80) {
					thisptr->mVControl = (thisptr->mVControl & 0x7F) + (~value & 0x80);
					memset(thisptr->mRAM + 0x200, thisptr->mVControl, 16);
				}
			}

			thisptr->mAControl = value;

			// D1=1 swaps RAM banks between main and Veronica CPU
			// D3=1 selects upper 16K half
			if (delta & 0x0A)
				thisptr->UpdateWindowBase();

			if (delta & 0x01) {
				// We must not disturb the semaphore here.
				thisptr->mVControl = (thisptr->mVControl & 0x80) + 0x3F;
				memset(thisptr->mRAM + 0x200, thisptr->mVControl, 16);
				thisptr->UpdateCoProcWindowDormant();
			}

			if (delta & 0x03)
				thisptr->UpdateCoProcWindowActive();

			// D4=1 enables $8000-9FFF window
			if (delta & 0x10)
				thisptr->mpMemMan->SetLayerModes(thisptr->mpMemLayerRightWindow, value & 0x10 ? kATMemoryAccessMode_ARW : kATMemoryAccessMode_0);

			// D5=1 enables $A000-BFFF window
			if (delta & 0x20) {
				thisptr->mpMemMan->SetLayerModes(thisptr->mpMemLayerLeftWindow, value & 0x20 ? kATMemoryAccessMode_ARW : kATMemoryAccessMode_0);
				thisptr->mpCartridgePort->UpdateRD5();
			}

			// check if we are bringing the coprocessor out of reset (must be done after we
			// have updated windows)
			if (delta & value & 0x01)
				thisptr->mCoProc.WarmReset();

			// If we swapped the banks on V1 hardware, we may potentially corrupt the next cycle if it
			// is an access to the banking window. However, don't do this if we're just starting up,
			// as we don't emulate reset timing precisely.
			if (thisptr->mbVersion1 && (delta & 0x02) && (value & 0x01) && !(delta & 0x01)) {
				thisptr->mbCorruptNextCycle = true;
			}
		}
		return true;
	}

	return false;
}

uint8 ATVeronicaEmulator::OnCorruptableDebugRead(uint32 addr, void *thisptr0) {
	ATVeronicaEmulator *const thisptr = (ATVeronicaEmulator *)thisptr0;
	uint16 randomVal = thisptr->PeekRand16();
	uint8 v = thisptr->mpCoProcWinBase[addr & 0x3FFF];

	if (!(randomVal & 0x1F))
		v ^= (uint8)(randomVal >> 8);

	return v;
}

uint8 ATVeronicaEmulator::OnCorruptableRead(uint32 addr, void *thisptr0) {
	ATVeronicaEmulator *const thisptr = (ATVeronicaEmulator *)thisptr0;
	uint16 randomVal = thisptr->Rand16();
	uint8 v = thisptr->mpCoProcWinBase[addr & 0x3FFF];

	if (!(randomVal & 0x1F)) {
		g_ATLCVRError("Corrupting read from '816 banking window at $%04X due to in-progress bank switch.", addr);
		v ^= (uint8)(randomVal >> 8);
	}

	return v;
}

void ATVeronicaEmulator::OnCorruptableWrite(uint32 addr, uint8 value, void *thisptr0) {
	ATVeronicaEmulator *const thisptr = (ATVeronicaEmulator *)thisptr0;
	uint16 randomVal = thisptr->Rand16();

	if (!(randomVal & 0x1F)) {
		g_ATLCVRError("Corrupting write to '816 banking window at $%04X due to in-progress bank switch.", addr);
	} else {
		thisptr->mpCoProcWinBase[addr & 0x3FFF] = value;
	}
}

void ATVeronicaEmulator::WriteVControl(uint8 val) {
	val |= 0x1F;

	if (val == mVControl)
		return;

	const uint8 delta = val ^ mVControl;

	mVControl = val;
	memset(mRAM + 0x200, mVControl, 16);

	// D7 is semaphore bit.
	if (delta & 0x80)
		mAControl = (mAControl & 0x7F) + (~val & 0x80);

	// D6 toggles between 4000-7FFF and C000-FFFF.
	// D5 toggles half of bank used.
	if (delta & 0x60) {
		if (delta & 0x40)
			UpdateCoProcWindowDormant();

		UpdateCoProcWindowActive();
	}
}

void ATVeronicaEmulator::UpdateCoProcWindowDormant() {
	uintptr *VDRESTRICT readmap = mCoProc.GetReadMap();
	uintptr *VDRESTRICT writemap = mCoProc.GetWriteMap();

	if (mVControl & 0x40) {
		readmap += 0xC0;
		writemap += 0xC0;
	} else {
		readmap += 0x40;
		writemap += 0x40;
	}

	for(int i=0; i<64; ++i) {
		readmap[i] = (uintptr)mRAM;
		writemap[i] = (uintptr)mRAM;
	}
}

void ATVeronicaEmulator::UpdateCoProcWindowActive() {
	uintptr *VDRESTRICT readmap = mCoProc.GetReadMap();
	uintptr *VDRESTRICT writemap = mCoProc.GetWriteMap();

	uintptr offset;

	if (mVControl & 0x40) {
		readmap += 0x40;
		writemap += 0x40;
		offset = 0x4000;
	} else {
		readmap += 0xC0;
		writemap += 0xC0;
		offset = 0xC000;
	}

	uintptr winbaseoffset = 0;

	if (mVControl & 0x20)
		winbaseoffset += 0x4000;

	if (!(mAControl & 0x02))
		winbaseoffset += 0x8000;

	uint8 *winmem = mRAM + 0x10000 + winbaseoffset;
	mpCoProcWinBase = winmem;

	uintptr winbase = (uintptr)winmem - offset;

	for(int i=0; i<64; ++i) {
		readmap[i] = winbase;
		writemap[i] = winbase;
	}
}

void ATVeronicaEmulator::UpdateWindowBase() {
	const uint8 *window = mRAM + 0x10000;

	if (mAControl & 0x02)
		window += 0x8000;

	if (mAControl & 0x08)
		window += 0x4000;

	mpMemMan->SetLayerMemory(mpMemLayerRightWindow, window);
	mpMemMan->SetLayerMemory(mpMemLayerLeftWindow, window + 0x2000);
}

void ATVeronicaEmulator::Sync() {
	const uint32 t = ATSCHEDULER_GETTIME(mpScheduler);
	const uint32 cycles = t - mLastSync;

	mLastSync = t;

	if (cycles && (mAControl & 1)) {
		uint32 vcycles = cycles * 8;

		if (mbCorruptNextCycle) {
			mbCorruptNextCycle = false;

			uintptr *VDRESTRICT readmap = mCoProc.GetReadMap();
			uintptr *VDRESTRICT writemap = mCoProc.GetWriteMap();

			if (mVControl & 0x40) {
				readmap += 0x40;
				writemap += 0x40;
			} else {
				readmap += 0xC0;
				writemap += 0xC0;
			}

			uintptr prevRead = readmap[0];
			uintptr prevWrite = writemap[0];
			uintptr tempRead = mCorruptedReadNode.AsBase();
			uintptr tempWrite = mCorruptedWriteNode.AsBase();

			for(int i=0; i<64; ++i) {
				readmap[i] = tempRead;
				writemap[i] = tempWrite;
			}

			mCoProc.AddCycles(1);
			mCoProc.Run();

			for(int i=0; i<64; ++i) {
				readmap[i] = prevRead;
				writemap[i] = prevWrite;
			}

			--vcycles;
		}

		mCoProc.AddCycles(vcycles);
		mCoProc.Run();
	}
}

uint32 ATVeronicaEmulator::PeekRand16() const {
	return mPRNG & 0xffff;
}

uint32 ATVeronicaEmulator::Rand16() {
	uint32 t = mPRNG & 0xffff;

	mPRNG = (t << 12) ^ (t << 15) ^ (mPRNG >> 16);

	return t;
}