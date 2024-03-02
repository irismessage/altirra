//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2012 Avery Lee
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
#include "hleprogramloader.h"
#include "hleutils.h"
#include "kerneldb.h"
#include "console.h"
#include "cpu.h"
#include "cpuhookmanager.h"
#include "debugger.h"
#include "simeventmanager.h"
#include "simulator.h"

ATHLEProgramLoader::ATHLEProgramLoader()
	: mpCPU(NULL)
	, mpCPUHookMgr(NULL)
	, mpSimEventMgr(NULL)
	, mpSim(NULL)
	, mpLaunchHook(NULL)
	, mpLoadContinueHook(NULL)
	, mbLaunchPending(0)
{
	std::fill(mProgramModuleIds, mProgramModuleIds + sizeof(mProgramModuleIds)/sizeof(mProgramModuleIds[0]), 0);
}

ATHLEProgramLoader::~ATHLEProgramLoader() {
	Shutdown();
}

void ATHLEProgramLoader::Init(ATCPUEmulator *cpu, ATSimulatorEventManager *simEventMgr, ATSimulator *sim) {
	mpCPU = cpu;
	mpCPUHookMgr = cpu->GetHookManager();
	mpSimEventMgr = simEventMgr;
	mpSim = sim;
}

void ATHLEProgramLoader::Shutdown() {
	UnloadProgramSymbols();

	if (mpCPUHookMgr) {
		mpCPUHookMgr->UnsetHook(mpLoadContinueHook);
		mpCPUHookMgr->UnsetHook(mpLaunchHook);
		mpCPUHookMgr = NULL;
	}

	mpSim = NULL;
	mpSimEventMgr = NULL;
	mpCPU = NULL;

	mbLaunchPending = false;
}

void ATHLEProgramLoader::LoadProgram(const wchar_t *symbolHintPath, IVDRandomAccessStream& stream) {
	uint32 len = (uint32)stream.Length();
	mProgramToLoad.resize(len);
	stream.Read(mProgramToLoad.data(), len);
	mProgramLoadIndex = 0;

	// check if this is a SpartaDOS executable by looking for a reloc block
	if (len >= 4 && (mProgramToLoad[0] == 0xfe || mProgramToLoad[0] == 0xfa) && mProgramToLoad[1] == 0xff)
	{
		mProgramToLoad.clear();
		throw MyError("Program load failed: this program must be loaded under SpartaDOS X.");
	}

	mpCPUHookMgr->SetHookMethod(mpLaunchHook, kATCPUHookMode_KernelROMOnly, ATKernelSymbols::DSKINV, 10, this, &ATHLEProgramLoader::OnDSKINV);

	// try to load symbols
	UnloadProgramSymbols();

	IATDebugger *d = ATGetDebugger();

	if (d && symbolHintPath) {
		static const wchar_t *const kSymExts[]={
			L".lst", L".lab", L".lbl", L".dbg"
		};

		VDASSERTCT(sizeof(kSymExts)/sizeof(kSymExts[0]) == sizeof(mProgramModuleIds)/sizeof(mProgramModuleIds[0]));

		const wchar_t *symbolHintPathExt = VDFileSplitExt(symbolHintPath);

		VDStringW sympath;
		for(int i=0; i<sizeof(mProgramModuleIds)/sizeof(mProgramModuleIds[0]); ++i) {
			sympath.assign(symbolHintPath, symbolHintPathExt);
			sympath += kSymExts[i];

			try {
				if (VDDoesPathExist(sympath.c_str())) {
					uint32 moduleId = d->LoadSymbols(sympath.c_str(), false);

					if (moduleId) {
						mProgramModuleIds[i] = moduleId;

						ATConsolePrintf("Loaded symbols %ls\n", sympath.c_str());
					}
				}
			} catch(const MyError&) {
				// ignore
			}
		}

		// process directives AFTER all symbols have been loaded
		for(int i=0; i<sizeof(mProgramModuleIds)/sizeof(mProgramModuleIds[0]); ++i) {
			if (mProgramModuleIds[i])
				d->ProcessSymbolDirectives(mProgramModuleIds[i]);
		}

		// load debugger script
		sympath.assign(symbolHintPath, symbolHintPathExt);
		sympath += L".atdbg";

		try {
			if (VDDoesPathExist(sympath.c_str())) {
				if (mpSim->IsRunning()) {
					// give the debugger script a chance to run
					mpSim->Suspend();
					d->QueueCommandFront("`g -n", false);
				}

				d->QueueBatchFile(sympath.c_str());

				ATConsolePrintf("Loaded debugger script %ls\n", sympath.c_str());
			}
		} catch(const MyError&) {
			// ignore
		}
	}

	mbLaunchPending = true;
}

uint8 ATHLEProgramLoader::OnDSKINV(uint16) {
	ATKernelDatabase kdb(mpCPU->GetMemory());

	mbLaunchPending = false;

	// reset run/init addresses to hook location near top of stack
	kdb.INITAD = 0x1FE;
	kdb.RUNAD = 0x1FE;

	// push DSKINV address on stack as return address
	mpCPU->PushWord(ATKernelSymbols::DSKINV - 1);

	// turn on load continuation hook
	mpCPUHookMgr->SetHookMethod(mpLoadContinueHook, kATCPUHookMode_Always, 0x01FE, 0, this, &ATHLEProgramLoader::OnLoadContinue);

	// reset DSKINV stub
	mpCPUHookMgr->UnsetHook(mpLaunchHook);

	// set COLDST so kernel doesn't think we were in the middle of init (required for
	// some versions of Alley Cat)
	kdb.COLDST = 0;

	// Set diskette boot flag in BOOT? so Alex's DOSINI handler runs.
	// Note that we need valid code pointed to by the DOSINI vector, since Stealth
	// crashes otherwise.
	kdb.DOSINI = 0xE4C0;		// KnownRTS

	kdb.BOOT_ |= 0x01;

	mpSimEventMgr->NotifyEvent(kATSimEvent_EXELoad);

	// load next segment
	return OnLoadContinue(0);
}

uint8 ATHLEProgramLoader::OnLoadContinue(uint16) {
	ATCPUEmulatorMemory *mem = mpCPU->GetMemory();
	ATKernelDatabase kdb(mem);

	// set INITAD to known RTS
	kdb.INITAD = 0xE4C0;

	// resume loading segments
	const uint8 *src0 = mProgramToLoad.data();
	const uint8 *src = src0 + mProgramLoadIndex;
	const uint8 *srcEnd = src0 + mProgramToLoad.size();
	for(;;) {
		// check if we're done
		if (srcEnd - src < 4) {
launch:
			// Looks like we're done. Push DSKINV onto the stack and execute the run routine.

			// remove hooks
			mpCPUHookMgr->UnsetHook(mpLoadContinueHook);
 
			// Last of the Dragons requires X to be set to a particular value to avoid stomping CRITIC.
			// This is what DOS launches with, since it calls SIO right before doing the run.
			mpCPU->SetX(0x20);
			kdb.STATUS = 0x01;

			// retrieve run address
			uint16 runAddr = kdb.RUNAD;

			mpCPU->Jump(runAddr);
			ATConsolePrintf("EXE: Launching at %04X\n", runAddr);

			mpSimEventMgr->NotifyEvent(kATSimEvent_EXERunSegment);

			vdfastvector<uint8>().swap(mProgramToLoad);
			return 0x4C;
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
				len = (uint32)(srcEnd - src);
				ATConsoleWrite("WARNING: Invalid Atari executable: bad start/end range.\n");
			} else {
				ATConsoleWrite("ERROR: Invalid Atari executable: bad start/end range.\n");
				goto launch;
			}
		}

		// if this is the first segment, set RUNAD to this segment as a default
		if (kdb.RUNAD == 0x01FE)
			kdb.RUNAD = start;

		// load segment data into memory
		ATConsolePrintf("EXE: Loading program %04X-%04X to %04X-%04X\n", src-src0, (src-src0)+len-1, start, end);

		for(uint32 i=0; i<len; ++i)
			mem->WriteByte(start + i, *src++);

		// fake SIO read
		kdb.PBCTL = 0x3C;
		kdb.PBCTL = 0x34;
		kdb.PBCTL = 0x3C;

		ATClearPokeyTimersOnDiskIo(kdb);

		// check if INITAD has changed
		uint16 initAddr = kdb.INITAD;
		if (initAddr != 0xE4C0) {
			ATConsolePrintf("EXE: Jumping to %04X\n", initAddr);
			break;
		}
	}

	mProgramLoadIndex = src - src0;

	// push virtual load hook ($01FE-1) onto stack and jsr through (INITAD)
	mpCPU->PushWord(0x01FD);
	mpCPU->PushWord(kdb.INITAD - 1);

	mpSimEventMgr->NotifyEvent(kATSimEvent_EXEInitSegment);
	return 0x60;
}

void ATHLEProgramLoader::UnloadProgramSymbols() {
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
