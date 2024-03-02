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
#include "siomanager.h"
#include "simulator.h"
#include "cpu.h"
#include "cpuhookmanager.h"
#include "kerneldb.h"
#include "debuggerlog.h"
#include "uirender.h"
#include "hleutils.h"
#include "cassette.h"
#include "pclink.h"

ATDebuggerLogChannel g_ATLCHookSIOReqs(false, false, "HOOKSIOREQS", "OS SIO hook requests");
ATDebuggerLogChannel g_ATLCHookSIO(false, false, "HOOKSIO", "OS SIO hook messages");

ATSIOManager::ATSIOManager()
	: mpCPU(NULL)
	, mpMemory(NULL)
	, mpSim(NULL)
	, mpUIRenderer(NULL)
	, mpSIOVHook(NULL)
	, mpDSKINVHook(NULL)
{
}

ATSIOManager::~ATSIOManager() {
}

void ATSIOManager::Init(ATCPUEmulator *cpu, ATSimulator *sim) {
	mpCPU = cpu;
	mpMemory = cpu->GetMemory();
	mpSim = sim;
	mpUIRenderer = sim->GetUIRenderer();

	ReinitHooks();
}

void ATSIOManager::Shutdown() {
	UninitHooks();

	mpUIRenderer = NULL;
	mpSim = NULL;
	mpMemory = NULL;
	mpCPU = NULL;
}

void ATSIOManager::ReinitHooks() {
	UninitHooks();

	ATCPUHookManager& hookmgr = *mpCPU->GetHookManager();

	if (mpSim->IsDiskSIOPatchEnabled() || mpSim->IsCassetteSIOPatchEnabled() || mpSim->IsFastBootEnabled())
		hookmgr.SetHookMethod(mpSIOVHook, kATCPUHookMode_KernelROMOnly, ATKernelSymbols::SIOV, 0, this, &ATSIOManager::OnHookSIOV);

	if (mpSim->IsDiskSIOPatchEnabled())
		hookmgr.SetHookMethod(mpDSKINVHook, kATCPUHookMode_KernelROMOnly, ATKernelSymbols::DSKINV, 0, this, &ATSIOManager::OnHookDSKINV);
}

void ATSIOManager::UninitHooks() {
	if (mpCPU) {
		ATCPUHookManager& hookmgr = *mpCPU->GetHookManager();

		hookmgr.UnsetHook(mpSIOVHook);
		hookmgr.UnsetHook(mpDSKINVHook);
	}
}

bool ATSIOManager::TryAccelRequest(const ATSIORequest& req) {
	g_ATLCHookSIOReqs("Checking SIO request: device $%02X, command $%02X\n", req.mDevice, req.mCommand);

	// Abort read acceleration if the buffer overlaps the parameter region.
	// Yes, there is game stupid enough to do this (Formula 1 Racing). Specifically,
	// we need to check if TIMFLG is in the buffer as that will cause the read to
	// prematurely abort.
	if (req.mAddress <= ATKernelSymbols::TIMFLG && (ATKernelSymbols::TIMFLG - req.mAddress) < req.mLength)
		return 0;

	ATKernelDatabase kdb(mpMemory);
	uint8 status = 0x01;

	if (req.mDevice >= 0x31 && req.mDevice <= 0x3F) {
		ATDiskEmulator& disk = mpSim->GetDiskDrive(req.mDevice - 0x31);

		if (mpSim->IsDiskSIOOverrideDetectEnabled()) {
			if (!disk.IsAccessed())
				return false;
		} else {
			if (!disk.IsEnabled()) {
				if (mpSim->IsFastBootEnabled())
					goto fastbootignore;

				return false;
			}
		}

		if (!mpSim->IsDiskSIOPatchEnabled())
			return false;

		if (req.mCommand == 0x52) {		// read
			if ((req.mMode & 0xc0) != 0x40)
				return false;

			status = disk.ReadSector(req.mAddress, req.mLength, req.mSector, mpMemory);

			// copy DBUFLO/DBUFHI + size -> BUFRLO/BUFRHI
			uint32 endAddr = req.mAddress + req.mLength;
			kdb.BUFRLO_BUFRHI = endAddr;

			g_ATLCHookSIO("Intercepting disk SIO read: buf=%04X, len=%04X, sector=%04X, status=%02X\n", req.mAddress, req.mLength, req.mSector, status);

			mpUIRenderer->PulseStatusFlags(1 << (req.mDevice - 0x31));

			// leave SKCTL set to asynchronous receive
			kdb.SKCTL = (kdb.SSKCTL & 0x07) | 0x10;
		} else if (req.mCommand == 0x50 || req.mCommand == 0x57) {
			if ((req.mMode & 0xc0) != 0x80)
				return 0;

			status = disk.WriteSector(req.mAddress, req.mLength, req.mSector, mpMemory);

			// copy DBUFLO/DBUFHI + size -> BUFRLO/BUFRHI
			uint32 endAddr = req.mAddress + req.mLength;
			kdb.BUFRLO_BUFRHI = endAddr;

			g_ATLCHookSIO("Intercepting disk SIO write: buf=%04X, len=%04X, sector=%04X, status=%02X\n", req.mAddress, req.mLength, req.mSector, status);

			mpUIRenderer->PulseStatusFlags(1 << (req.mDevice - 0x31));

			// leave SKCTL set to asynchronous receive
			kdb.SKCTL = (kdb.SSKCTL & 0x07) | 0x10;
		} else if (req.mCommand == 0x53) {
			if ((req.mMode & 0xc0) != 0x40)
				return false;

			if (req.mLength != 4)
				return false;

			uint8 data[5];
			disk.ReadStatus(data);

			for(int i=0; i<4; ++i)
				mpMemory->WriteByte(req.mAddress+i, data[i]);

			kdb.CHKSUM = data[4];

			// copy DBUFLO/DBUFHI + size -> BUFRLO/BUFRHI
			uint32 endAddr = req.mAddress + req.mLength;
			kdb.BUFRLO_BUFRHI = endAddr;

			g_ATLCHookSIO("Intercepting disk SIO status req.: buf=%04X\n", req.mAddress);

			mpUIRenderer->PulseStatusFlags(1 << (req.mDevice - 0x31));

			ATClearPokeyTimersOnDiskIo(kdb);
		} else if (req.mCommand == 0x4E) {
			if ((req.mMode & 0xc0) != 0x40)
				return false;

			if (req.mLength != 12)
				return false;

			uint8 data[13];
			disk.ReadPERCOMBlock(data);

			for(int i=0; i<12; ++i)
				mpMemory->WriteByte(req.mAddress+i, data[i]);

			mpMemory->WriteByte(ATKernelSymbols::CHKSUM, data[12]);

			// copy DBUFLO/DBUFHI + size -> BUFRLO/BUFRHI
			uint32 endAddr = req.mAddress + req.mLength;
			kdb.BUFRLO_BUFRHI = endAddr;

			g_ATLCHookSIO("Intercepting disk SIO read PERCOM req.: buf=%04X\n", req.mAddress);

			mpUIRenderer->PulseStatusFlags(1 << (req.mDevice - 0x31));
		} else
			return false;
	} else if (req.mDevice == 0x4F) {
		if (!mpSim->IsFastBootEnabled())
			return false;

fastbootignore:
		// return timeout
		status = 0x8A;
	} else if (req.mDevice == 0x5F) {
		if (!mpSim->IsCassetteSIOPatchEnabled())
			return false;

		ATCassetteEmulator& cassette = mpSim->GetCassette();

		// Check if a read or write is requested
		if (req.mMode & 0x80)
			return false;

		status = cassette.ReadBlock(req.mAddress, req.mLength, mpMemory);

		mpUIRenderer->PulseStatusFlags(1 << 16);

		g_ATLCHookSIO("Intercepting cassette SIO read: buf=%04X, len=%04X, status=%02X\n", req.mAddress, req.mLength, status);
	} else if (req.mDevice == 0x6f) {
		IATPCLinkDevice *pclink = mpSim->GetPCLink();

		if (!pclink || !mpSim->IsDiskSIOPatchEnabled())
			return false;

		uint8 command = kdb.DCOMND;

		if (pclink->TryAccelSIO(*mpCPU, *mpMemory, kdb, req.mDevice, command))
			return true;
	} else {
		return false;
	}

	ATClearPokeyTimersOnDiskIo(kdb);

	// Clear CRITIC.
	kdb.CRITIC = 0;
	kdb.STATUS = status;
	kdb.DSTATS = status;

	mpCPU->Ldy(status);

	return true;
}

uint8 ATSIOManager::OnHookDSKINV(uint16) {
	ATKernelDatabase kdb(mpMemory);

	// check if we support the command
	const uint8 cmd = kdb.DCOMND;

	switch(cmd) {
		case 0x50:	// put
		case 0x52:	// read
		case 0x57:	// write
			break;

		default:
			return 0;
	}

	// Set sector size. If we have an XL bios, use DSCTLN, otherwise force 128 bytes.
	// Since hooks only trigger from ROM, we can check if the lower ROM exists.
	if (mpSim->IsKernelROMLocation(0xC000))
		kdb.DBYTLO_DBYTHI = kdb.DSCTLN;
	else
		kdb.DBYTLO_DBYTHI = 0x80;

	// set device and invoke SIOV
	kdb.DDEVIC = 0x31;

	return OnHookSIOV(0);
}

uint8 ATSIOManager::OnHookSIOV(uint16) {
	ATKernelDatabase kdb(mpMemory);

	// read out SIO block
	uint8 siodata[16];

	for(int i=0; i<16; ++i)
		siodata[i] = mpMemory->ReadByte(ATKernelSymbols::DDEVIC + i);

	// assemble parameter block
	ATSIORequest req;

	req.mDevice		= siodata[0] + siodata[1] - 1;
	req.mCommand	= siodata[2];
	req.mMode		= siodata[3];
	req.mAddress	= VDReadUnalignedLEU16(&siodata[4]);
	req.mLength		= VDReadUnalignedLEU16(&siodata[8]);
	req.mSector		= VDReadUnalignedLEU16(&siodata[10]);

	for(int i=0; i<6; ++i)
		req.mAUX[i] = siodata[i + 10];

	return TryAccelRequest(req) ? 0x60 : 0;
}
