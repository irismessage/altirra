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

#include "stdafx.h"
#include "hlekernel.h"
#include "cpu.h"
#include "simulator.h"
#include "ksyms.h"
#include "kerneldb.h"

namespace {
	enum {
		CIOStatBreak		= 0x80,	// break key abort
		CIOStatIOCBInUse	= 0x81,	// IOCB in use
		CIOStatUnkDevice	= 0x82,	// unknown device
		CIOStatWriteOnly	= 0x83,	// opened for write only
		CIOStatInvalidCmd	= 0x84,	// invalid command
		CIOStatNotOpen		= 0x85,	// device or file not open
		CIOStatInvalidIOCB	= 0x86,	// invalid IOCB number
		CIOStatReadOnly		= 0x87,	// opened for read only
		CIOStatEndOfFile	= 0x88,	// end of file reached
		CIOStatTruncRecord	= 0x89,	// record truncated
		CIOStatTimeout		= 0x8A,	// device timeout
		CIOStatNAK			= 0x8B,	// device NAK
		CIOStatSerFrameErr	= 0x8C,	// serial bus framing error
		CIOStatCursorRange	= 0x8D,	// cursor out of range
		CIOStatSerOverrun	= 0x8E,	// serial frame overrun error
		CIOStatSerChecksum	= 0x8F,	// serial checksum error
		CIOStatDeviceDone	= 0x90,	// device done error
		CIOStatBadScrnMode	= 0x91,	// bad screen mode
		CIOStatNotSupported	= 0x92,	// function not supported by handler
		CIOStatOutOfMemory	= 0x93,	// not enough memory
		CIOStatDriveNumErr	= 0xA0,	// disk drive # error
		CIOStatTooManyFiles	= 0xA1,	// too many open disk files
		CIOStatDiskFull		= 0xA2,	// disk full
		CIOStatFatalDiskIO	= 0xA3,	// fatal disk I/O error
		CIOStatFileNumDiff	= 0xA4,	// internal file # mismatch
		CIOStatFileNameErr	= 0xA5,	// filename error
		CIOStatPointDLen	= 0xA6,	// point data length error
		CIOStatFileLocked	= 0xA7,	// file locked
		CIOStatInvDiskCmd	= 0xA8,	// invalid command for disk
		CIOStatDirFull		= 0xA9,	// directory full (64 files)
		CIOStatFileNotFound	= 0xAA,	// file not found
		CIOStatInvPoint		= 0xAB,	// invalid point

		CIOCmdOpen			= 0x03,
		CIOCmdGetRecord		= 0x05,
		CIOCmdGetChars		= 0x07,
		CIOCmdPutRecord		= 0x09,
		CIOCmdPutChars		= 0x0B,
		CIOCmdClose			= 0x0C,
		CIOCmdGetStatus		= 0x0D,
		CIOCmdSpecial		= 0x0E	// $0E and up is escape
	};
}

extern ATSimulator g_sim;

class ATHLEKernel : public IATCPUHighLevelEmulator, public IATHLEKernel {
public:
	ATHLEKernel();
	~ATHLEKernel();

	IATCPUHighLevelEmulator *AsCPUHLE() { return this; }

	void Init(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem);

	void EnableLowerROM(bool enable);
	void EnableUpperROM(bool enable);
	void EnableSelfTestROM(bool enable);
	int InvokeHLE(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem, uint16 pcm, uint32 code);

protected:
	uint16 ReadWord(uint16 addr) {
		return mpMemory->ReadByte(addr) + 256*mpMemory->ReadByte(addr+1);
	}

	void WriteWord(uint16 addr, uint16 val) {
		mpMemory->WriteByte(addr+0, (uint8)val);
		mpMemory->WriteByte(addr+1, (uint8)(val >> 8));
	}

#if 0
	int OnReset();
	int OnReset1();
	bool OnDiskBoot();

	int OnVBlankImmediate();
	int OnVBlankDeferred();
	int OnVBlankExit();

	int OnDSKINV();
	int OnSIOV();
	int OnCIOV();
	int OnCIOExit(uint8 status);
	int OnIRQ();
	int OnBlackboard();
#endif

#include "hlekernel_gen.h"

	ATCPUEmulator *mpCPU;
	ATCPUEmulatorMemory *mpMemory;
	bool mbLowerROM;
	bool mbSelfTestROM;
	bool mbUpperROM;
};

IATHLEKernel *ATCreateHLEKernel() {
	return new ATHLEKernel;
}

ATHLEKernel::ATHLEKernel()
	: mbLowerROM(false)
	, mbSelfTestROM(false)
	, mbUpperROM(false)
{
}

ATHLEKernel::~ATHLEKernel() {
}

void ATHLEKernel::Init(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem) {
	mpCPU = &cpu;
	mpMemory = &mem;
}

namespace ATKernelFillHelpers {
	class ByteWriter {
	public:
		ByteWriter(uint8 *dst) : mpDst(dst) {}

		ByteWriter& operator=(uint8 c) {
			*mpDst = c;
			return *this;
		}

		ByteWriter operator,(uint8 c) {
			mpDst[1] = c;
			return ByteWriter(mpDst + 1);
		}

	protected:
		uint8 *mpDst;
	};

	class KernelWriter {
	public:
		KernelWriter(uint8 *kernel) : mpKernel(kernel) {}

		ByteWriter operator()(uint32 addr) {
			if (addr >= 0x5000 && addr < 0x5800)
				return ByteWriter(mpKernel + (addr - 0x4000));

			if (addr >= 0xC000 && addr < 0xD000)
				return ByteWriter(mpKernel + (addr - 0xC000));

			if (addr >= 0xD800 && addr < 0x10000)
				return ByteWriter(mpKernel + (addr - 0xC000));

			return ByteWriter(NULL);
		}

	protected:
		uint8 *mpKernel;
	};
}

void ATHLEKernel::EnableLowerROM(bool enable) {
	mbLowerROM = enable;
}

void ATHLEKernel::EnableUpperROM(bool enable) {
	mbUpperROM = enable;
}

void ATHLEKernel::EnableSelfTestROM(bool enable) {
	mbSelfTestROM = enable;
}

int ATHLEKernel::InvokeHLE(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem, uint16 pc, uint32 code) {
	Dispatch(code);
	return -1;
}

#if 0
int ATHLEKernel::InvokeUpperROM(ATCPUEmulator& cpu, ATCPUEmulatorMemory& mem, uint16 pcm, uint32 code) {
	using namespace ATKernelSymbols;

	switch(code) {

		//////////////// Reset
		case 0x8000:	return OnReset();
		case 0x8001:	return OnReset1();

		//////////////// Dummy interrupt handlers
		case 0x8080:	cpu.SetY(cpu.Pop());
						cpu.SetX(cpu.Pop());
		case 0x8081:	cpu.SetA(cpu.Pop());
		case 0x8082:	cpu.InjectOpcode(0x40);
						return -1;

		//////////////// NMI
		case 0x8100:		// NMI immediate
			{
				uint8 status = mem.ReadByte(0xD40F);

				// check for DLI
				if (status & 0x80) {
					// jump through VDSLST
					cpu.Jump(0xFD09);
					return -1;
				}

				cpu.Push(cpu.GetA());

				// check for system reset
				if (status & 0x20) {
					// TODO: Handle system reset.
				}

				// push registers
				cpu.Push(cpu.GetX());
				cpu.Push(cpu.GetY());

				// reset NMI
				mem.WriteByte(NMIRES, 0);

				// check for VBI
				if (status & 0x40) {
					// jump through VVBLKI
					cpu.Jump(0xFD03);
					return -1;
				}
			}
			break;

		case 0x8140:		// Vertical blank immediate
			return OnVBlankImmediate();

		case 0x8150:		// Vertical blank deferred
			return OnVBlankDeferred();

		case 0x8160:		// Vertical blank exit
			return OnVBlankExit();

		case 0x8200:	return OnDSKINV();
		case 0x8300:	return OnSIOV();
		case 0x8400:	return OnIRQ();
		case 0x8500:	return OnCIOV();

		case 0xC000:	return OnBlackboard();
	}
	return -1;
}

int ATHLEKernel::OnReset() {
	using namespace ATKernelSymbols;

	// reinit CPU
	mpCPU->SetP(mpCPU->GetP() | AT6502::kFlagI);
	mpCPU->SetP(mpCPU->GetP() & ~(AT6502::kFlagD | AT6502::kFlagB));
	mpCPU->SetS(0xFF);

	// clear warmstart flag
	mpMemory->WriteByte(WARMST, 0);

	// TODO: Look for diagnostic cartridge.

	// clear hardware
	for(uint32 i = 0; i < 256; ++i) {
		mpMemory->WriteByte(0xD000 + i, 0);
		mpMemory->WriteByte(0xD200 + i, 0);
		mpMemory->WriteByte(0xD400 + i, 0);
	}

	// clear memory
	for(uint32 i=8; i<0xA000; ++i)
		mpMemory->WriteByte(i, 0);

	// set DOSVEC to blackboard
	WriteWord(DOSVEC, 0xFC80);

	// TODO: Set screen margins to 2, 38.

	// init vectors (TODO: complete)
	WriteWord(VDSLST, 0xF086);
	WriteWord(VVBLKI, 0xFD0B);
	WriteWord(CDTMA1, 0xF083);
	WriteWord(CDTMA2, 0xF083);
	WriteWord(VVBLKD, 0xFD0F);
	WriteWord(VIMIRQ, 0xFF03);
	WriteWord(VSEROR, 0xF083);
	WriteWord(VSERIN, 0xF083);
	WriteWord(VSEROC, 0xF083);
	WriteWord(VTIMR1, 0xF083);
	WriteWord(VTIMR2, 0xF083);
	WriteWord(VTIMR4, 0xF083);
	WriteWord(VKEYBD, 0xF083);
	WriteWord(VPRCED, 0xF083);
	WriteWord(VINTER, 0xF083);
	WriteWord(VBREAK, 0xF083);

	// clear OS vars
	mpMemory->WriteByte(BRKKEY, 0xFF);
	WriteWord(MEMTOP, 0xA000);
	WriteWord(MEMLO, 0x0700);

	// TODO: Init S:, D:, K:, P:, C:, CIO, SIO, interrupt processor.

	// TODO: Check START button for cassette boot.

	// Clear I flag on the 6502 to allow interrupts.
	mpCPU->SetP(mpCPU->GetP() & ~AT6502::kFlagI);

	// Init device table.
	WriteWord(HATABS, 0xE800);

	// TODO: Look for cartridges.

	// TODO: Open screen editor.

	// BOGUS: Emulate opening screen editor.
	mpMemory->WriteByte(COLOR0, 0x28);
	mpMemory->WriteByte(COLOR1, 0xCA);
	mpMemory->WriteByte(COLOR2, 0x94);
	mpMemory->WriteByte(COLOR3, 0x46);
	mpMemory->WriteByte(COLOR4, 0x00);
	mpMemory->WriteByte(CRITIC, 0);
	mpMemory->WriteByte(DRKMSK, 0xFE);
	mpMemory->WriteByte(COLRSH, 0x00);
	// construct display list
	uint32 addr = 0x800;
	mpMemory->WriteByte(addr++, 0x70);
	mpMemory->WriteByte(addr++, 0x70);
	mpMemory->WriteByte(addr++, 0x70);
	mpMemory->WriteByte(addr++, 0x42);
	mpMemory->WriteByte(addr++, 0x00);
	mpMemory->WriteByte(addr++, 0x0c);
	for(uint32 i=0; i<23; ++i)
		mpMemory->WriteByte(addr++, 0x02);
	mpMemory->WriteByte(addr++, 0x41);
	mpMemory->WriteByte(addr++, 0x00);
	mpMemory->WriteByte(addr++, 0x08);

	// set ANTIC display list ptr
	WriteWord(SDLSTL, 0x0800);

	// set charset ptr
	mpMemory->WriteByte(CHBAS, 0xE0);

	// enable ANTIC playfield DMA
	mpMemory->WriteByte(SDMCTL, 0x22);

	// enable VBI
	mpMemory->WriteByte(NMIEN, 0x40);

	// return to 6502 code to wait for VBLANK.
	return 0;
}

int ATHLEKernel::OnReset1() {
	using namespace ATKernelSymbols;
	// TODO: Cassette boot.

	// Initiate disk boot.
	if (OnDiskBoot())
		return -1;

	mpMemory->WriteByte(COLDST, 0);

	// TODO: Jump through cartridge.

	// Jump through DOSVEC.
	mpCPU->Jump(0xF00C);
	return -1;
}

bool ATHLEKernel::OnDiskBoot() {
	using namespace ATKernelSymbols;

	// read sector one to page four
	ATDiskEmulator& disk = g_sim.GetDiskDrive(0);

	if (disk.ReadSector(0x0400, 128, 1, mpMemory) & 0x80)
		return false;

	// copy init address to DOSINI
	mpMemory->WriteByte(DOSINI+0, mpMemory->ReadByte(0x0404));
	mpMemory->WriteByte(DOSINI+1, mpMemory->ReadByte(0x0405));

	// move first sector to final address
	uint32 numsecs = mpMemory->ReadByte(0x0401);
	uint32 loadaddr = mpMemory->ReadByte(0x0402) + 256*mpMemory->ReadByte(0x0403);
	uint32 ramlo = loadaddr + 6;

	for(uint32 i=0; i<128; ++i)
		mpMemory->WriteByte(loadaddr + i, mpMemory->ReadByte(0x400 + i));

	// read additional sectors
	for(uint32 i=1; i<numsecs; ++i) {
		loadaddr += 0x80;
		disk.ReadSector(loadaddr, 128, i+1, mpMemory);
	}

	// initiate boot
	mpMemory->WriteByte(RAMLO, (uint8)ramlo);
	mpMemory->WriteByte(RAMLO + 1, (uint8)(ramlo >> 8));

	mpCPU->Jump(0xF003);
	return true;
}

int ATHLEKernel::OnVBlankImmediate() {
	using namespace ATKernelSymbols;

	// increment RTCLOK
	bool slowTick = false;
	for(int i=0; i<3; ++i) {
		uint8 c = mpMemory->ReadByte(RTCLOK + i) + 1;
		mpMemory->WriteByte(RTCLOK + i, c);
		if (c)
			break;
		slowTick = true;
	}

	// attract colors and copy to GTIA
	uint8 attract = mpMemory->ReadByte(ATRACT);

	if (slowTick) {
		++attract;
		if (attract >= 0x80) {
			attract = 0xFE;

			mpMemory->WriteByte(DRKMSK, 0xF6);
			mpMemory->WriteByte(COLRSH, mpMemory->ReadByte(RTCLOK + 1));
		}

		mpMemory->WriteByte(ATRACT, attract);
	}

	// decrement timer 1
	uint16 timer1 = mpMemory->ReadByte(CDTMV1+0) + 256*mpMemory->ReadByte(CDTMV1+1);
	if (timer1) {
		--timer1;
		mpMemory->WriteByte(CDTMV1+0, (uint8)(timer1 >> 0));
		mpMemory->WriteByte(CDTMV1+1, (uint8)(timer1 >> 8));

		if (!timer1) {
			// TODO: Fire timer 1 here.
		}
	}

	// check for critical flag
	if (mpMemory->ReadByte(CRITIC) || (mpMemory->ReadByte(0x100 + (uint8)(mpCPU->GetS() + 4)) & AT6502::kFlagI)) {
		// exit immediately
		mpCPU->Jump(XITVBV);
		return -1;
	}

	// jump through deferred VBI vector
	mpCPU->Jump(0xFD06);
	return -1;
}

int ATHLEKernel::OnVBlankDeferred() {
	using namespace ATKernelSymbols;

	// Clear I flag to allow recursion (note that this only affects us if we exit out to 6502 code).
	mpCPU->SetP(mpCPU->GetP() & ~AT6502::kFlagI);

	// Update hardware registers from shadow registers.
	mpMemory->WriteByte(DLISTH, mpMemory->ReadByte(SDLSTH));
	mpMemory->WriteByte(DLISTL, mpMemory->ReadByte(SDLSTL));
	mpMemory->WriteByte(DMACTL, mpMemory->ReadByte(SDMCTL));
	mpMemory->WriteByte(CHBASE, mpMemory->ReadByte(CHBAS ));
	mpMemory->WriteByte(CHACTL, mpMemory->ReadByte(CHACT ));
	mpMemory->WriteByte(PRIOR , mpMemory->ReadByte(GPRIOR));

	// Update color registers from shadows with attract.
	uint8 drkmsk = mpMemory->ReadByte(DRKMSK);
	uint8 colrsh = mpMemory->ReadByte(COLRSH);

	for(uint32 i=0; i<9; ++i)
		mpMemory->WriteByte(COLPM0 + i, (mpMemory->ReadByte(PCOLR0 + i) ^ colrsh) & drkmsk);

	// Reset console click.
	mpMemory->WriteByte(CONSOL, 0x08);

	// Update timer 2.
	uint16 timer2 = ReadWord(CDTMV2);
	if (timer2) {
		--timer2;

		WriteWord(CDTMV2, timer2);

		if (!timer2) {
			// TODO: jump through CDTMA2
		}
	}

	// Update timers 3-5.
	for(uint32 i=0; i<3; ++i) {
		uint16 timerval = ReadWord(CDTMV3 + i);

		if (timerval) {
			--timerval;
			WriteWord(CDTMV3 + i, timerval);

			if (!timerval) {
				mpMemory->WriteByte(CDTMF3 + i, 0x00);
			}
		}
	}

	// TODO: Copy key from POKEY to CH[02FC].
	// TODO: Decrement keyboard debounce counter.
	// TODO: Process keyboard auto-repeat.
	// TODO: Read game controller data.

	mpCPU->Jump(XITVBV);
	return -1;
}

int ATHLEKernel::OnVBlankExit() {
	// Unstack Y, X, A and return.
	mpCPU->SetY(mpCPU->Pop());
	mpCPU->SetX(mpCPU->Pop());
	mpCPU->SetA(mpCPU->Pop());
	mpCPU->InjectOpcode(0x40);
	return -1;
}

int ATHLEKernel::OnDSKINV() {
	uint8 drive = mpMemory->ReadByte(0x0301);
	uint8 cmd = mpMemory->ReadByte(0x0302);
	uint16 bufadr = mpMemory->ReadByte(0x0304) + 256*mpMemory->ReadByte(0x0305);
	uint16 sector = mpMemory->ReadByte(0x030A) + 256*mpMemory->ReadByte(0x030B);

	if (cmd == 0x52 && drive >= 1 && drive <= 4) {
		ATDiskEmulator& disk = g_sim.GetDiskDrive(drive - 1);
		uint8 status = disk.ReadSector(bufadr, 128, sector, mpMemory);
		mpMemory->WriteByte(0x0303, status);

		// Set DBYTLO/DBYTHI to transfer length (required for last section of JOYRIDE part A to load)
		mpMemory->WriteByte(0x0308, 0x80);
		mpMemory->WriteByte(0x0309, 0x00);

		// SIO does an LDY on the status byte before returning -- BIOS code checks N.
		// Might as well set Z too.
		uint8 p = mpCPU->GetP() & 0x7d;
		if (!status)
			p |= 0x02;
		if (status & 0x80)
			p |= 0x80;
		mpCPU->SetP(p);
		mpCPU->SetY(status);

		ATGTIAEmulator& gtia = g_sim.GetGTIA();
		gtia.SetStatusFlags(1 << (drive - 1));
		gtia.ResetStatusFlags(1 << (drive - 1));
	} else {
		mpMemory->WriteByte(0x0303, 0x80);
		mpCPU->SetP(0x80);
		mpCPU->SetY(0x80);
	}

	mpCPU->InjectOpcode(0x40);
	return -1;
}

int ATHLEKernel::OnSIOV() {
	uint8 device = mpMemory->ReadByte(0x0300);
	uint8 unit = mpMemory->ReadByte(0x0301);

	// SIO checks for device == cassette and then just adds device + unit together.
	if (device + unit != 0x32) {
		mpCPU->SetY(0x8A);		// timeout
		mpMemory->WriteByte(0x0303, 0x8A);
		mpCPU->InjectOpcode(0x60);
		return -1;
	}

	uint8 cmd = mpMemory->ReadByte(0x0302);
	if (cmd != 0x52)
		return 0;

	uint8 dstats = mpMemory->ReadByte(0x0303);
	if ((dstats & 0xc0) != 0x40)
		return 0;

	uint16 bufadr = mpMemory->ReadByte(0x0304) + 256*mpMemory->ReadByte(0x0305);
	uint16 len = mpMemory->ReadByte(0x0308) + 256*mpMemory->ReadByte(0x0309);
	uint16 sector = mpMemory->ReadByte(0x030A) + 256*mpMemory->ReadByte(0x030B);

	ATDiskEmulator& disk = g_sim.GetDiskDrive(0);
	uint8 status = disk.ReadSector(bufadr, len, sector, mpMemory);
	mpMemory->WriteByte(0x0303, status);

	mpCPU->SetY(status);
	mpCPU->SetP((mpCPU->GetP() & 0x7d) | (status & 0x80) | (!status ? 0x02 : 0x00));

	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	gtia.SetStatusFlags(1);
	gtia.ResetStatusFlags(1);

	mpCPU->InjectOpcode(0x60);
	return -1;
}

int ATHLEKernel::OnCIOV() {
	using namespace ATKernelSymbols;

	ATKernelDatabase kdb(mpMemory);

	// save IOCB offset (X) and character (C)
	const uint8 x = mpCPU->GetX();
	kdb.CIOCHR = mpCPU->GetA();
	kdb.ICIDNO = x;

	// validate iocb offset
	if (x & 0x8f)
		return OnCIOExit(CIOStatInvalidIOCB);

	// copy IOCB to ZIOCB
	for(int i=0; i<12; ++i)
		mpMemory->WriteByte(ICHIDZ + i, mpMemory->ReadByte(ICHID + i));

	// are we handling open?
	const uint8 cmd = kdb.ICCOMZ;

	if (cmd == CIOCmdOpen) {
		// check if IOCB is already open
		if (!(kdb.ICHIDZ & 0x80)) {
			// not open -- issue error
			return OnCIOExit(CIOStatIOCBInUse);
		}

		// pull first char of filename
		uint16 nameAddr = kdb.ICBAZ;
		uint8 handlerChar = mpMemory->ReadByte(nameAddr++);

		// search for handler
		sint32 handlerAddr = -1;
		uint8 handlerId;
		for(int i=11*3; i>=0; i-=3) {
			uint8 hc = mpMemory->ReadByte(HATABS + i);

			if (hc == handlerChar) {
				handlerAddr = mpMemory->ReadByte(HATABS + i + 1) + 256*mpMemory->ReadByte(HATABS + i + 2);
				handlerId = (uint8)i;
				break;
			}
		}

		// fail if handler not found
		if (handlerAddr < 0)
			return OnCIOExit(CIOStatUnkDevice);

		// store handler ID
		kdb.ICHIDZ = handlerId;

		// default to device 1
		kdb.ICDNOZ = 1;

		// check for device number
		uint8 c = mpMemory->ReadByte(nameAddr);

		if (c >= '1' && c <= '4') {
			kdb.ICDNOZ = c - '0';
			c = mpMemory->ReadByte(++nameAddr);
		}

		// check for colon
		if (c != ':')
			return OnCIOExit(CIOStatFileNameErr);

		// TODO: Jump to open vector
	} else {
		// check if IOCB is open
		if (kdb.ICHIDZ & 0x80) {
			// not open -- issue error
			return OnCIOExit(CIOStatNotOpen);
		}
	}
}

int ATHLEKernel::OnCIOExit(uint8 status)
{
	using namespace ATKernelSymbols;

	ATKernelDatabase kdb(mpMemory);

	mpCPU->SetA(kdb.CIOCHR);
	mpCPU->SetX(kdb.ICIDNO);
	mpCPU->SetY(status);

	mpCPU->SetP((mpCPU->GetP() & ~(AT6502::kFlagN | AT6502::kFlagZ))
		+ (status == 0 ? AT6502::kFlagZ : 0)
		+ (status & 0x80 ? AT6502::kFlagN : 0));

	return 0x60;
}

int ATHLEKernel::OnIRQ() {
	using namespace ATKernelSymbols;

	// Push A register.
	mpCPU->Push(mpCPU->GetA());

	// Read IRQ status from POKEY.
	uint8 irqstatus = mpMemory->ReadByte(IRQST);

	// Check for serial IRQ.
	const struct VecInfo {
		uint8	irqbit;
		uint16	vector;
	} kVectors[]={
		{ 0x20, VSERIN },
		{ 0x10, VSEROR },
		{ 0x08, VSEROC },
		{ 0x01, VTIMR1 },
		{ 0x02, VTIMR2 },
		{ 0x04, VTIMR4 },
		{ 0x40, VKEYBD },
		{ 0x80, VSEROR },
	};

	for(int i=0; i<8; ++i) {
		uint8 irqbit = kVectors[i].irqbit;

		// bit 3 (complete) needs to be special cased since it is not affected by IRQEN.
		if (irqbit == 0x08 && !(mpMemory->ReadByte(POKMSK) & 0x08))
			continue;

		if (!(irqstatus & irqbit)) {
			// Reset IRQ bit.
			mpMemory->WriteByte(IRQEN, ~irqbit);
			mpMemory->WriteByte(IRQEN, mpMemory->ReadByte(POKMSK));

			// Copy vector to JVECK.
			WriteWord(JVECK, ReadWord(kVectors[i].vector));
			
			// Jump through JVECK.
			mpCPU->Jump(0xFF06);
			return -1;
		}
	}

	// Check for break bit.
	if (mpMemory->ReadByte(0x100 + (uint8)(mpCPU->GetS() + 2)) & AT6502::kFlagB) {
		// Jump through VBREAK.
		mpCPU->Jump(0xFF09);
		return -1;
	}

	// Unknown -- return.
	mpCPU->SetA(mpCPU->Pop());
	mpCPU->InjectOpcode(0x60);
	return -1;
}

int ATHLEKernel::OnBlackboard() {
	return -1;
}
#endif

#include "hlekernel_gen.inl"
