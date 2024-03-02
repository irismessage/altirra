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
#include <vd2/system/vdalloc.h>
#include "cpu.h"
#include "cpumemory.h"
#include "cpuhookmanager.h"
#include "cassette.h"
#include "simulator.h"
#include "hostdevice.h"
#include "printer.h"
#include "rs232.h"
#include "virtualscreen.h"
#include "kerneldb.h"
#include "cio.h"
#include "hleciohook.h"

class ATHLECIOHook : public IATHLECIOHook {
public:
	ATHLECIOHook();
	~ATHLECIOHook();

	void Init(ATCPUEmulator *cpu, ATSimulator *sim);
	void Shutdown();

	void WarmReset();
	void ColdReset(uint16 hookPage, const uint8 *upperROM);

	void ReinitHooks();
	void UninitHooks();

protected:
	uint8 OnHookHostDevice(uint16 pc);
	uint8 OnHookPrinter(uint16 pc);
	uint8 OnHookRS232(uint16 pc);
	uint8 OnHookVirtualScreen(uint16 pc);
	uint8 OnHookCIOV(uint16 pc);
	uint8 OnHookCIOVInit(uint16 pc);
	uint8 OnHookCIOINV(uint16 pc);
	uint8 OnHookCassetteOpen(uint16 pc);
	uint8 OnHookEditorGetChar(uint16 pc);
	uint8 OnHookEditorPutChar(uint16 pc);

	ATCPUEmulator *mpCPU;
	ATSimulator *mpSim;

	uint16	mHookPage;
	bool	mbCIOHandlersEstablished;
	uint32	mCassetteCIOOpenHandlerHookAddress;
	uint32	mEditorCIOGetCharHandlerHookAddress;
	uint32	mEditorCIOPutCharHandlerHookAddress;

	ATCPUHookNode *mpDeviceRoutineHooks[19];
	ATCPUHookNode *mpCIOVHook;
	ATCPUHookNode *mpCIOVInitHook;
	ATCPUHookNode *mpCIOINVHook;
	ATCPUHookNode *mpCSOPIVHook;
	ATCPUHookNode *mpCassetteOpenHook;
	ATCPUHookNode *mpEditorGetCharHook;
	ATCPUHookNode *mpEditorPutCharHook;
};

ATHLECIOHook::ATHLECIOHook()
	: mpCPU(NULL)
	, mpSim(NULL)
	, mCassetteCIOOpenHandlerHookAddress(0)
	, mEditorCIOGetCharHandlerHookAddress(0)
	, mEditorCIOPutCharHandlerHookAddress(0)
	, mHookPage(0)
	, mbCIOHandlersEstablished(false)
	, mpCIOVHook(NULL)
	, mpCIOVInitHook(NULL)
	, mpCIOINVHook(NULL)
	, mpCSOPIVHook(NULL)
	, mpCassetteOpenHook(NULL)
	, mpEditorGetCharHook(NULL)
	, mpEditorPutCharHook(NULL)
{
	std::fill(mpDeviceRoutineHooks, mpDeviceRoutineHooks+19, (ATCPUHookNode *)NULL);
}

ATHLECIOHook::~ATHLECIOHook() {
	Shutdown();
}

void ATHLECIOHook::Init(ATCPUEmulator *cpu, ATSimulator *sim) {
	mpCPU = cpu;
	mpSim = sim;

	ReinitHooks();
}

void ATHLECIOHook::Shutdown() {
	UninitHooks();
	mpCPU = NULL;
}

void ATHLECIOHook::WarmReset() {
	mbCIOHandlersEstablished = false;
}

void ATHLECIOHook::ColdReset(uint16 hookPage, const uint8 *upperROM) {
	mHookPage = hookPage;
	mCassetteCIOOpenHandlerHookAddress = 0;
	mEditorCIOPutCharHandlerHookAddress = 0;
	mEditorCIOGetCharHandlerHookAddress = 0;

	if (upperROM) {
		// read cassette OPEN vector from kernel ROM
		uint16 openVec = VDReadUnalignedLEU16(&upperROM[ATKernelSymbols::CASETV - 0xD800]) + 1;

		if ((openVec - 0xC000) < 0x4000)
			mCassetteCIOOpenHandlerHookAddress = openVec;

		uint16 editorPutVec = VDReadUnalignedLEU16(&upperROM[ATKernelSymbols::EDITRV - 0xD800 + 6]) + 1;

		if ((editorPutVec - 0xC000) < 0x4000)
			mEditorCIOPutCharHandlerHookAddress = editorPutVec;

		uint16 editorGetVec = VDReadUnalignedLEU16(&upperROM[ATKernelSymbols::EDITRV - 0xD800 + 4]) + 1;

		if ((editorGetVec - 0xC000) < 0x4000)
			mEditorCIOGetCharHandlerHookAddress = editorGetVec;
	}

	mbCIOHandlersEstablished = false;

	ReinitHooks();
}

void ATHLECIOHook::ReinitHooks() {
	UninitHooks();

	ATCPUHookManager& hookmgr = *mpCPU->GetHookManager();

	// H:
	IATHostDeviceEmulator *hd = mpSim->GetHostDevice();
	const bool hdenabled = (hd && hd->IsEnabled());
	if (hdenabled) {
		for(int i=0; i<6; ++i)
			hookmgr.SetHookMethod(mpDeviceRoutineHooks[i], kATCPUHookMode_Always, mHookPage + 0x21 + 2*i, 0, this, &ATHLECIOHook::OnHookHostDevice);
	}

	// RS-232
	IATRS232Emulator *rs232 = mpSim->GetRS232();
	const bool rsenabled = rs232 != NULL;
	if (rsenabled) {
		for(int i=0; i<6; ++i)
			hookmgr.SetHookMethod(mpDeviceRoutineHooks[i + 6], kATCPUHookMode_Always, mHookPage + 0x61 + 2*i, 0, this, &ATHLECIOHook::OnHookRS232);
	}

	// printer
	IATPrinterEmulator *printer = mpSim->GetPrinter();
	const bool prenabled = printer && printer->IsEnabled();
	if (prenabled)
		hookmgr.SetHookMethod(mpDeviceRoutineHooks[12], kATCPUHookMode_Always, mHookPage + 0x47, 0, this, &ATHLECIOHook::OnHookPrinter);

	// virtual screen
	IATVirtualScreenHandler *vs = mpSim->GetVirtualScreenHandler();
	if (vs) {
		for(int i=0; i<6; ++i)
			hookmgr.SetHookMethod(mpDeviceRoutineHooks[i+13], kATCPUHookMode_Always, mHookPage + 0x31 + 2*i, 0, this, &ATHLECIOHook::OnHookVirtualScreen);
	}

	// CIO
	if (prenabled || vs)
		hookmgr.SetHookMethod(mpCIOVHook, kATCPUHookMode_KernelROMOnly, ATKernelSymbols::CIOV, 0, this, &ATHLECIOHook::OnHookCIOV);

	if (hdenabled || prenabled || rsenabled) {
		hookmgr.SetHookMethod(mpCIOVInitHook, kATCPUHookMode_KernelROMOnly, ATKernelSymbols::CIOV, 1, this, &ATHLECIOHook::OnHookCIOVInit);
		hookmgr.SetHookMethod(mpCIOINVHook, kATCPUHookMode_KernelROMOnly, ATKernelSymbols::CIOINV, 0, this, &ATHLECIOHook::OnHookCIOINV);
	}

	// Cassette hooks
	if (mpSim->IsCassetteSIOPatchEnabled()) {
		ATCPUHookManager& hookmgr = *mpCPU->GetHookManager();

		hookmgr.SetHookMethod(mpCSOPIVHook, kATCPUHookMode_KernelROMOnly, ATKernelSymbols::CSOPIV, 0, this, &ATHLECIOHook::OnHookCassetteOpen);

		if (mCassetteCIOOpenHandlerHookAddress)
			hookmgr.SetHookMethod(mpCassetteOpenHook, kATCPUHookMode_KernelROMOnly, mCassetteCIOOpenHandlerHookAddress, 0, this, &ATHLECIOHook::OnHookCassetteOpen);
	}

	if (vs) {
		vs->SetGetCharAddress(mEditorCIOGetCharHandlerHookAddress);

		if (mEditorCIOPutCharHandlerHookAddress)
			hookmgr.SetHookMethod(mpEditorPutCharHook, kATCPUHookMode_KernelROMOnly, mEditorCIOPutCharHandlerHookAddress, 0, this, &ATHLECIOHook::OnHookEditorPutChar);

		if (mEditorCIOGetCharHandlerHookAddress)
			hookmgr.SetHookMethod(mpEditorGetCharHook, kATCPUHookMode_KernelROMOnly, mEditorCIOGetCharHandlerHookAddress, 0, this, &ATHLECIOHook::OnHookEditorGetChar);
	}
}

void ATHLECIOHook::UninitHooks() {
	ATCPUHookManager& hookmgr = *mpCPU->GetHookManager();

	hookmgr.UnsetHook(mpEditorPutCharHook);
	hookmgr.UnsetHook(mpEditorGetCharHook);
	hookmgr.UnsetHook(mpCassetteOpenHook);
	hookmgr.UnsetHook(mpCSOPIVHook);
	hookmgr.UnsetHook(mpCIOVInitHook);
	hookmgr.UnsetHook(mpCIOVHook);

	for(int i=0; i<19; ++i)
		hookmgr.UnsetHook(mpDeviceRoutineHooks[i]);
}

uint8 ATHLECIOHook::OnHookHostDevice(uint16 pc) {
	IATHostDeviceEmulator *hd = mpSim->GetHostDevice();

	if (!hd || !hd->IsEnabled())
		return 0;

	hd->OnCIOVector(mpCPU, mpCPU->GetMemory(), pc & 14);
	return 0x60;
}

uint8 ATHLECIOHook::OnHookPrinter(uint16 pc) {
	IATPrinterEmulator *printer = mpSim->GetPrinter();

	if (!printer || !printer->IsEnabled())
		return 0;

	printer->OnCIOVector(mpCPU, mpCPU->GetMemory(), pc & 14);
	return 0x60;
}

uint8 ATHLECIOHook::OnHookRS232(uint16 pc) {
	IATRS232Emulator *rs232 = mpSim->GetRS232();

	if (!rs232)
		return 0;

	rs232->OnCIOVector(mpCPU, mpCPU->GetMemory(), pc & 14);
	return 0x60;
}

uint8 ATHLECIOHook::OnHookVirtualScreen(uint16 pc) {
	IATVirtualScreenHandler *vs = mpSim->GetVirtualScreenHandler();

	if (!vs)
		return 0;

	vs->OnCIOVector(mpCPU, mpCPU->GetMemory(), pc & 14);
	return 0x60;
}

uint8 ATHLECIOHook::OnHookCIOVInit(uint16 pc) {
	return OnHookCIOINV(pc);
}

uint8 ATHLECIOHook::OnHookCIOINV(uint16 pc) {
	ATCPUEmulatorMemory *mem = mpCPU->GetMemory();

	ATCPUHookManager& hookmgr = *mpCPU->GetHookManager();
	hookmgr.UnsetHook(mpCIOVInitHook);

	IATHostDeviceEmulator *hd = mpSim->GetHostDevice();
	if (hd && hd->IsEnabled()) {
		for(int i=0; i<12; ++i) {
			uint16 hent = ATKernelSymbols::HATABS + 3*i;
			uint8 c = mem->ReadByte(hent);
			if (c == 'H')
				break;

			if (!c) {
				mem->WriteByte(hent, 'H');
				mem->WriteByte(hent+1, 0x20);
				mem->WriteByte(hent+2, (uint8)(mHookPage >> 8));
				break;
			}
		}
	}

	IATRS232Emulator *rs232 = mpSim->GetRS232();
	if (rs232) {
		const uint8 devName = rs232->GetCIODeviceName();

		if (devName) {
			for(int i=0; i<12; ++i) {
				uint16 hent = ATKernelSymbols::HATABS + 3*i;
				uint8 c = mem->ReadByte(hent);
				if (c == devName)
					break;

				if (!c) {
					mem->WriteByte(hent, devName);
					mem->WriteByte(hent+1, 0x60);
					mem->WriteByte(hent+2, (uint8)(mHookPage >> 8));
					break;
				}
			}
		}
	}

	return 0;
}

uint8 ATHLECIOHook::OnHookCIOV(uint16 pc) {
	ATCPUEmulatorMemory *mem = mpCPU->GetMemory();

	// validate IOCB index
	uint8 iocbIdx = mpCPU->GetX();
	if (!(iocbIdx & 0x0F) && iocbIdx < 0x80) {
		// check if this is an OPEN or other request
		uint8 cmd = mem->ReadByte(ATKernelSymbols::ICCMD + iocbIdx);

		if (cmd == ATCIOSymbols::CIOCmdOpen) {
			// check if filename starts with P
			uint16 addr = mem->ReadByte(ATKernelSymbols::ICBAL + iocbIdx) + mem->ReadByte(ATKernelSymbols::ICBAH + iocbIdx)*256;

			if (mem->ReadByte(addr) == 'P') {
				IATPrinterEmulator *printer = mpSim->GetPrinter();

				if (printer && printer->IsEnabled()) {
					// scan HATABS and find printer index
					int hatidx = -1;
					for(int i=0; i<12; ++i) {
						uint16 hent = ATKernelSymbols::HATABS + 3*i;
						if (mem->ReadByte(hent) == 'P') {
							hatidx = i;
							break;
						}
					}

					if (hatidx >= 0) {
						uint8 rcode = printer->OnCIOCommand(mpCPU, mem, iocbIdx, cmd);
						if (rcode && mpCPU->GetY() == ATCIOSymbols::CIOStatSuccess) {
							mem->WriteByte(ATKernelSymbols::ICHID + iocbIdx, (uint8)hatidx);
							return rcode;
						}
					}
				}
			}
		} else {
			// check character in HATABS and see if it's the printer or editor
			uint8 devid = mem->ReadByte(ATKernelSymbols::ICHID + iocbIdx);
			uint8 dev = mem->ReadByte(ATKernelSymbols::HATABS + devid);

			if (dev == 'P') {
				IATPrinterEmulator *printer = mpSim->GetPrinter();

				if (printer && printer->IsEnabled())
					return printer->OnCIOCommand(mpCPU, mem, iocbIdx, cmd);
			} else if (dev == 'E') {
				IATVirtualScreenHandler *vs = mpSim->GetVirtualScreenHandler();

				if (vs) {
					switch(cmd) {
						case ATCIOSymbols::CIOCmdOpen:
							vs->OnCIOVector(mpCPU, mem, 0);
							break;

						case ATCIOSymbols::CIOCmdClose:
							vs->OnCIOVector(mpCPU, mem, 2);
							break;
					}
				}
			}
		}
	}

	return 0;
}

uint8 ATHLECIOHook::OnHookCassetteOpen(uint16 pc) {
	// read I/O bits in ICAX1Z
	ATKernelDatabase kdb(mpCPU->GetMemory());

	if (pc == mCassetteCIOOpenHandlerHookAddress) {
		// save ICAX2Z into FTYPE (IRG mode)
		kdb.ICAX2Z = kdb.FTYPE;

		// abort emulation if it's not an open-for-read request
		if ((kdb.ICAX1Z & 0x0C) != 0x04)
			return 0;
	}

	// press play on cassette
	ATCassetteEmulator& cassette = mpSim->GetCassette();
	cassette.Play();

	// turn motor on by clearing port A control bit 3
	kdb.PACTL &= ~0x08;

	// skip forward by nine seconds
	cassette.SkipForward(9.0f);

	// set open mode to read
	kdb.WMODE = 0;

	// set cassette buffer size to 128 bytes and mark it as empty
	kdb.BPTR = 0x80;
	kdb.BLIM = 0x80;

	// clear EOF flag
	kdb.FEOF = 0x00;

	// set success
	mpCPU->Ldy(0x01);
	return 0x60;
}

uint8 ATHLECIOHook::OnHookEditorGetChar(uint16 pc) {
	IATVirtualScreenHandler *vs = mpSim->GetVirtualScreenHandler();

	if (!vs)
		return 0;

	vs->OnCIOVector(mpCPU, mpCPU->GetMemory(), 4 /* get byte */);
	return 0x60;
}

uint8 ATHLECIOHook::OnHookEditorPutChar(uint16 pc) {
	IATVirtualScreenHandler *vs = mpSim->GetVirtualScreenHandler();

	if (!vs)
		return 0;

	vs->OnCIOVector(mpCPU, mpCPU->GetMemory(), 6 /* put byte */);
	return 0x60;
}

///////////////////////////////////////////////////////////////////////////

IATHLECIOHook *ATCreateHLECIOHook(ATCPUEmulator *cpu, ATSimulator *sim) {
	vdautoptr<ATHLECIOHook> hook(new ATHLECIOHook);

	hook->Init(cpu, sim);

	return hook.release();
}

void ATDestroyHLECIOHook(IATHLECIOHook *hook) {
	delete static_cast<ATHLECIOHook *>(hook);
}
