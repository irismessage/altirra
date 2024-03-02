//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008-2010 Avery Lee
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
#include "vbxe.h"
#include "gtiarenderer.h"
#include "gtiatables.h"
#include "console.h"
#include "memorymanager.h"

using namespace ATGTIA;

#define VBXE_FETCH(addr) (mpMemory[(addr) & 0x7FFFF])
#define VBXE_FETCH_NOWRAP(addr) (mpMemory[(addr)])
#define VBXE_WRITE(addr, value) ((void)(mpMemory[(addr) & 0x7FFFF] = (value)))

namespace {
	uint8 ConvertPriorityToNative(uint8 pri) {
		pri = ~pri;

		pri = (pri << 4) + (pri >> 4);
		return pri;
	}

	uint8 ConvertPriorityFromNative(uint8 pri) {
		pri = ~pri;

		pri = (pri << 4) + (pri >> 4);
		return pri;
	}
}

// XDLC_TMON, XDLC_GMON, XDLC_HR, XDLC_LR
const ATVBXEEmulator::OvMode ATVBXEEmulator::kOvModeTable[3][4]={
	// GTLH
	/* 0100 */ kOvMode_80Text,
	/* 0101 */ kOvMode_80Text,
	/* 0110 */ kOvMode_80Text,
	/* 0111 */ kOvMode_80Text,
	/* 1000 */ kOvMode_SR,
	/* 1001 */ kOvMode_HR,
	/* 1010 */ kOvMode_LR,
	/* 1011 */ kOvMode_Disabled,
	/* 1100 */ kOvMode_Disabled,
	/* 1101 */ kOvMode_Disabled,
	/* 1110 */ kOvMode_Disabled,
	/* 1111 */ kOvMode_Disabled,
};

ATVBXEEmulator::ATVBXEEmulator()
	: mpMemory(NULL)
	, mpConn(NULL)
	, mpMemMan(NULL)
	, mMemAcControl(0)
	, mMemAcBankA(0)
	, mMemAcBankB(0)
	, mb5200Mode(false)
	, mXdlBaseAddr(0)
	, mXdlAddr(0)
	, mbXdlActive(false)
	, mbXdlEnabled(false)
	, mXdlRepeatCounter(0)
	, mOvMode(kOvMode_Disabled)
	, mOvWidth(kOvWidth_Narrow)
	, mbOvTrans(false)
	, mbOvTrans15(false)
	, mOvHscroll(0)
	, mOvVscroll(0)
	, mOvAddr(0)
	, mOvStep(0)
	, mOvTextRow(0)
	, mChAddr(0)
	, mPfPaletteIndex(0)
	, mOvPaletteIndex(0)
	, mbExtendedColor(false)
	, mbAttrMapEnabled(false)
	, mAttrAddr(0)
	, mAttrStep(0)
	, mAttrWidth(0)
	, mAttrHeight(0)
	, mAttrHscroll(0)
	, mAttrVscroll(0)
	, mAttrRow(0)
	, mPsel(0)
	, mCsel(0)
	, mbIRQEnabled(false)
	, mbIRQRequest(false)
	, mDMACycles(0)
	, mbBlitLogging(false)
	, mbBlitterEnabled(false)
	, mbBlitterActive(false)
	, mbBlitterListActive(false)
	, mbBlitterContinue(false)
	, mBlitterMode(0)
	, mBlitCyclesLeft(0)
	, mBlitCyclesPerRow(0)
	, mBlitListAddr(0)
	, mBlitListFetchAddr(0)
	, mBlitSrcAddr(0)
	, mBlitSrcStepX(0)
	, mBlitSrcStepY(0)
	, mBlitDstAddr(0)
	, mBlitDstStepX(0)
	, mBlitDstStepY(0)
	, mBlitWidth(0)
	, mBlitHeight(0)
	, mBlitAndMask(0)
	, mBlitXorMask(0)
	, mBlitCollisionMask(0)
	, mBlitPatternMode(0)
	, mBlitCollisionCode(0)
	, mBlitZoomX(0)
	, mBlitZoomY(0)
	, mBlitZoomCounterY(0)
	, mpPfPalette(0)
	, mpOvPalette(0)
	, mpMergeBuffer(0)
	, mpAnticBuffer(0)
	, mpMergeBuffer0(0)
	, mpAnticBuffer0(0)
	, mpDst(0)
	, mX(0)
	, mRCIndex(0)
	, mRCCount(0)
	, mbHiresMode(false)
	, mPRIOR(0)
	, mpPriTable(0)
	, mpPriTableHi(0)
	, mpColorTable(0)
	, mpMemLayerMEMACA(NULL)
	, mpMemLayerMEMACB(NULL)
	, mpMemLayerRegisters(NULL)
	, mpMemLayerGTIAOverlay(NULL)
{
	memset(mColorTable, 0, sizeof mColorTable);

	InitPriorityTables();

	mpColorTable = mColorTable;
	mpPriTable = mPriorityTables[0];
	mpPriTableHi = mPriorityTablesHi[0];
}

ATVBXEEmulator::~ATVBXEEmulator() {
}

void ATVBXEEmulator::Init(uint8 *memory, IATVBXEEmulatorConnections *conn, ATMemoryManager *memman) {
	mpMemory = memory;
	mpConn = conn;
	mpMemMan = memman;

	ColdReset();
}

void ATVBXEEmulator::Shutdown() {
	ShutdownMemoryMaps();
}

void ATVBXEEmulator::ColdReset() {
	memcpy(mPalette[0], mDefaultPalette, sizeof mPalette[0]);
	memset(mPalette[1], 0, sizeof(uint32)*256*3);

	mPsel			= 0;
	mCsel			= 0;
	mMemAcBankA		= 0;
	mMemAcBankB		= 0;
	mMemAcControl	= 0;
	mXdlAddr		= 0;
	mXdlBaseAddr	= 0;
	mbXdlEnabled	= false;
	mbXdlActive		= false;
	mOvMode			= kOvMode_Disabled;
	mOvWidth		= kOvWidth_Normal;
	mOvMainPriority	= 0;
	memset(mOvPriority, 0, sizeof mOvPriority);
	mOvAddr			= 0;
	mOvStep			= 0;
	mbOvTrans		= true;
	mbOvTrans15		= false;
	mOvHscroll		= 0;
	mOvVscroll		= 0;
	mBlitListAddr	= 0;
	mBlitListFetchAddr = 0;
	mpPfPalette		= mPalette[0];
	mpOvPalette	= mPalette[1];
	mPfPaletteIndex = 0;
	mOvPaletteIndex = 1;
	mAttrWidth		= 8;
	mAttrHeight		= 8;
	mAttrHscroll	= 0;
	mAttrRow		= 0;
	mChAddr			= 0;

	mbExtendedColor	= false;
	mbAttrMapEnabled = false;

	WarmReset();
}

void ATVBXEEmulator::WarmReset() {
	// VIDEO_CONTROL: set to 0
	mbXdlEnabled	= false;
	mbExtendedColor = false;
	mbOvTrans		= true;
	mbOvTrans15		= false;

	// MEMAC_CONTROL: MCE and MAE set to 0
	mMemAcControl &= 0xf3;

	// MEMAC_BANK_SEL: MGE set to 0
	mMemAcBankA &= 0x7f;

	// MEMAC_B_CONTROL: MBCE and MBAE set to 0
	mMemAcBankB &= 0x3f;

	// BLITTER_START: set to 0
	// BLITTER_BUSY: set to 0
	mbBlitterEnabled = false;
	mbBlitterActive = false;
	mbBlitterListActive = false;
	mBlitCollisionCode = 0;
	mBlitCyclesLeft = 0;

	// IRQ_CONTROL: set to 0
	// IRQ_STATUS: set to 0
	mbXdlActive		= false;
	mbIRQEnabled	= false;
	mbIRQRequest	= false;

	// Nuke current XDL processing.
	mOvWidth = kOvWidth_Normal;
	mOvMode = kOvMode_Disabled;

	InitMemoryMaps();
	UpdateMemoryMaps();
}

void ATVBXEEmulator::Set5200Mode(bool enable) {
	mb5200Mode = enable;

	if (mpMemMan) {
		InitMemoryMaps();
		UpdateMemoryMaps();
	}
}

void ATVBXEEmulator::SetRegisterBase(uint8 page) {
	mRegBase = page;

	if (mpMemMan) {
		InitMemoryMaps();
		UpdateMemoryMaps();
	}
}

void ATVBXEEmulator::SetAnalysisMode(bool enable) {
	mpColorTable = enable ? kATAnalysisColorTable : mColorTable;
}

void ATVBXEEmulator::SetDefaultPalette(const uint32 pal[256]) {
	memcpy(mDefaultPalette, pal, sizeof mDefaultPalette);
}

void ATVBXEEmulator::SetBlitLoggingEnabled(bool enable) {
	mbBlitLogging = enable;
}

void ATVBXEEmulator::DumpStatus() {
	ATConsolePrintf("XDL enabled:       %s\n", mbXdlEnabled ? "Yes" : "No");
	ATConsolePrintf("XDL active:        %s\n", mbXdlActive ? "Yes" : "No");
	ATConsolePrintf("XDL base address:  $%05X\n", mXdlBaseAddr & 0x7FFFF);
	ATConsolePrintf("XDL fetch address: $%05X\n", mXdlAddr & 0x7FFFF);

	static const char *const kWidthNames[]={
		"Narrow",
		"Normal",
		"Wide"
	};
	ATConsolePrintf("Overlay width:     %s\n", kWidthNames[mOvWidth]);

	static const char *const kModeNames[]={
		"Disabled",
		"Low resolution",
		"Standard resolution",
		"High resolution",
		"80-column text",
	};
	ATConsolePrintf("Overlay mode:      %s\n", kModeNames[mOvMode]);
	ATConsolePrintf("Overlay address:   $%05X\n", mOvAddr & 0x7FFFF);
	ATConsolePrintf("Overlay step:      $%03X\n", mOvStep);
	ATConsolePrintf("Overlay priority:  $%02X | %02X %02X %02X %02X\n"
		, ConvertPriorityFromNative(mOvMainPriority)
		, ConvertPriorityFromNative(mOvPriority[0])
		, ConvertPriorityFromNative(mOvPriority[1])
		, ConvertPriorityFromNative(mOvPriority[2])
		, ConvertPriorityFromNative(mOvPriority[3])
		);

	static const char *const kBankModes[4]={
		"Disabled",
		"Antic only",
		"CPU only",
		"CPU and Antic",
	};

	uint32 winABase = (mMemAcControl & 0xF0) << 8;
	uint32 winALimit = std::min<uint32>(0x10000, winABase + (0x1000 << (mMemAcControl & 3)));

	static const uint8 kBankAMask[4]={
		0x7F,
		0x7E,
		0x7C,
		0x78,
	};

	if (mb5200Mode) {
		ATConsolePrintf("MEMAC Window A:    $%02X | $D800-$E7FF -> $%05X\n", mMemAcBankA, (uint32)(mMemAcBankA & 0xF8) << 12);
	} else {
		ATConsolePrintf("MEMAC Window A:    $%02X | $%04X-$%04X -> $%05X - %s\n", mMemAcBankA, winABase, winALimit - 1, (uint32)(mMemAcBankA & kBankAMask[mMemAcControl & 3]) << 12,
			kBankModes[mMemAcBankA & 0x80 ? (mMemAcControl >> 2) & 3 : 0]);
		ATConsolePrintf("MEMAC Window B:    $%02X | $%05X - %s\n", mMemAcBankB, ((uint32)mMemAcBankB & 0x1F) << 14, kBankModes[mMemAcBankB >> 6]);
	}

	ATConsolePrintf("Blitter IRQ:       %s, %s\n"
		, mbIRQEnabled ? "enabled" : "disabled"
		, mbIRQRequest ? "asserted" : "negated");

	if (mbBlitterEnabled && mbBlitterActive)
		ATConsolePrintf("Blitter status:    active (%u rows left)\n", mBlitHeight);
	else
		ATConsolePrintf("Blitter status:    %s\n"
			, mbBlitterEnabled ? mbBlitterListActive ? "reloading" : "idle" : "disabled");
	ATConsolePrintf("Blitter list addr: $%05X\n", mBlitListAddr);
	ATConsolePrintf("Blitter list cur.: $%05X\n", mBlitListFetchAddr);
}

void ATVBXEEmulator::DumpXDL() {
	uint32 addr = mXdlBaseAddr;
	uint32 scanLines = 0;

	for(;;) {
		if (scanLines >= 240) {
			ATConsoleWrite("Aborting dump - XDL too long (exceeded 240 scanlines).\n");
			break;
		}

		uint8 xdl1 = VBXE_FETCH(addr++);
		uint8 xdl2 = VBXE_FETCH(addr++);

		ATConsolePrintf("%05X: %02X %02X      ", (addr - 2) & 0x7FFFF, xdl1, xdl2);

		if (xdl1 & 4) {
			ATConsoleWrite("; mode off\n");
		} else if (xdl1 & 3) {
			OvMode ovMode = kOvModeTable[(xdl1 & 3) - 1][(xdl2 >> 4) & 3];

			static const char *const kOvModeNames[]={
				"off",
				"lr",
				"sr",
				"hr",
				"text"
			};

			ATConsolePrintf("; mode %s\n", kOvModeNames[ovMode]);
		} else
			ATConsoleWrite("; mode same\n");

		if (xdl1 & 0x10)
			ATConsoleWrite("                  ; map_off\n");
		else if (xdl1 & 0x08)
			ATConsoleWrite("                  ; map_on\n");

		// XDLC_RPTL (1 byte)
		uint32 repeat = 1;
		if (xdl1 & 0x20) {
			repeat = VBXE_FETCH(addr++) + 1;
			ATConsolePrintf("  %02X              ; repeat      %u\n"
				, repeat - 1
				, repeat);
		}

		scanLines += repeat;

		// XDLC_OVADR (5 byte)
		if (xdl1 & 0x40) {
			uint8 ov1 = VBXE_FETCH(addr++);
			uint8 ov2 = VBXE_FETCH(addr++);
			uint8 ov3 = VBXE_FETCH(addr++);
			uint8 step1 = VBXE_FETCH(addr++);
			uint8 step2 = VBXE_FETCH(addr++);

			uint32 ovaddr = (uint32)ov1 + ((uint32)ov2 << 8) + ((uint32)ov3 << 16);
			uint32 step = ((uint32)step1 + ((uint32)step2 << 8)) & 0xFFF;

			ATConsolePrintf("  %02X %02X %02X %02X %02X  ; load_ovl    $%05X, $%03X\n"
				, ov1
				, ov2
				, ov3
				, step1
				, step2
				, ovaddr
				, step);
		}

		// XDLC_OVSCRL (2 byte)
		if (xdl1 & 0x80) {
			// skip hscroll, vscroll
			uint8 hscroll = VBXE_FETCH(addr++);
			uint8 vscroll = VBXE_FETCH(addr++);

			ATConsolePrintf("  %02X %02X           ; ovscroll %d, %d\n"
				, hscroll
				, vscroll
				, hscroll & 7
				, vscroll & 7
				);
		}

		// XDLC_CHBASE (1 byte)
		if (xdl2 & 0x01) {
			uint8 chbase = VBXE_FETCH(addr++);

			ATConsolePrintf("  %02X              ; load_chbase $%05X\n", chbase, (uint32)chbase << 11);
		}

		// XDLC_MAPADR (5 byte)
		if (xdl2 & 0x02) {
			uint8 ad1 = VBXE_FETCH(addr++);
			uint8 ad2 = VBXE_FETCH(addr++);
			uint8 ad3 = VBXE_FETCH(addr++);
			uint8 step1 = VBXE_FETCH(addr++);
			uint8 step2 = VBXE_FETCH(addr++);

			uint32 mapaddr = (uint32)ad1 + ((uint32)ad2 << 8) + ((uint32)ad3 << 16);
			uint32 step = ((uint32)step1 + ((uint32)step2 << 8)) & 0xFFF;

			ATConsolePrintf("  %02X %02X %02X %02X %02X  ; load_map    $%05X, $%03X\n"
				, ad1
				, ad2
				, ad3
				, step1
				, step2
				, mapaddr, step);
		}

		// XDLC_MAPPAR (4 byte)
		if (xdl2 & 0x04) {
			uint8 hscroll = VBXE_FETCH(addr++);
			uint8 vscroll = VBXE_FETCH(addr++);
			uint8 w = VBXE_FETCH(addr++);
			uint8 h = VBXE_FETCH(addr++);
			ATConsolePrintf("  %02X %02X %02X %02X     ; map_attr    %d, %d, %dx%d\n"
				, hscroll
				, vscroll
				, w
				, h
				, hscroll & 31
				, vscroll & 31
				, (w & 31) + 1
				, (h & 31) + 1
				);
		}

		// XDLC_OVATT (2 byte)
		if (xdl2 & 0x08) {
			uint8 ctl = VBXE_FETCH(addr++);
			uint8 pri = VBXE_FETCH(addr++);

			static const char *const kWidthNames[4]={
				"narrow",
				"normal",
				"wide",
				"wide"
			};

			ATConsolePrintf("  %02X %02X           ; ovatt ovwidth=%s, ovpal=%d, pfpal=%d, pri=$%02X\n"
				, ctl
				, pri
				, kWidthNames[ctl & 3]
				, (ctl >> 4) & 3
				, (ctl >> 6) & 3
				, pri);
		}

		// XDLC_END
		if (xdl2 & 0x80) {
			ATConsolePrintf("; end\n");
			break;
		}
	}
}

void ATVBXEEmulator::DumpBlitList() {
	uint32 addr = mBlitListAddr;
	uint32 count = 0;

	for(;;) {
		if (++count > 256) {
			ATConsoleWrite("Blit list exceeds 256 entries -- ending dump.\n");
			break;
		}
		
		ATConsolePrintf("$%05X:\n", addr);

		bool more = DumpBlitListEntry(addr);

		if (!more)
			break;

		addr += 21;
	}
}

bool ATVBXEEmulator::DumpBlitListEntry(uint32 addr) {
	ATConsolePrintf("  Source: $%05X Xinc=%+d Yinc=%+d\n"
		, (uint32)VBXE_FETCH(addr) + ((uint32)VBXE_FETCH(addr + 1) << 8) + ((uint32)(VBXE_FETCH(addr + 2) & 0x7) << 16)
		, (sint8)VBXE_FETCH(addr + 5)
		, ((((uint32)VBXE_FETCH(addr + 3) + ((uint32)VBXE_FETCH(addr + 4) << 8)) & 0x1FFF) + 0xFFFFF000) ^ 0xFFFFF000);
	ATConsolePrintf("  Dest:   $%05X Xinc=%+d Yinc=%+d\n"
		, (uint32)VBXE_FETCH(addr + 6) + ((uint32)VBXE_FETCH(addr + 7) << 8) + ((uint32)(VBXE_FETCH(addr + 8) & 0x7) << 16)
		, (sint8)VBXE_FETCH(addr + 11)
		, ((((uint32)VBXE_FETCH(addr + 9) + ((uint32)VBXE_FETCH(addr + 10) << 8)) & 0x1FFF) + 0xFFFFF000) ^ 0xFFFFF000);
	ATConsolePrintf("  Size:   %u x %u\n"
		, (uint32)VBXE_FETCH(addr + 12) + ((uint32)(VBXE_FETCH(addr + 13) & 0x01) << 8) + 1
		, (uint32)VBXE_FETCH(addr + 14) + 1);
	ATConsolePrintf("  Masks:  AND=$%02X, XOR=$%02X, COLL=$%02X\n"
		, VBXE_FETCH(addr + 15)
		, VBXE_FETCH(addr + 16)
		, VBXE_FETCH(addr + 17)
		);

	const uint8 zoomByte = VBXE_FETCH(addr + 18);
	ATConsolePrintf("  Zoom:   %d x %d\n", (zoomByte & 7) + 1, ((zoomByte >> 4) & 7) + 1);

	const uint8 patternByte = VBXE_FETCH(addr + 19);
	if (patternByte & 0x80)
		ATConsolePrintf("  Patt:   repeat every %d\n", (patternByte & 0x7F) + 1);
	else
		ATConsolePrintf("  Patt:   disabled\n");

	const uint8 controlByte = VBXE_FETCH(addr + 20);
	static const char *const kModeNames[]={
		"Copy",
		"Overlay",
		"Add",
		"Or",
		"And",
		"Xor",
		"HR Overlay",
		"Reserved"
	};

	ATConsolePrintf("  Mode:   %d (%s)\n", controlByte & 7, kModeNames[controlByte & 7]);

	return (controlByte & 0x08) != 0;
}

sint32 ATVBXEEmulator::ReadControl(uint8 addrLo) {
	switch(addrLo) {
		case 0x40:	// CORE_VERSION
			return 0x10;

		case 0x41:	// MINOR_REVISION
			return 0x20;

		case 0x4A:	// COLDETECT
			return 0x00;

		case 0x50:	// BLT_COLLISION_CODE
			return mBlitCollisionCode;

		case 0x53:	// BLITTER_BUSY
			// D7-D2: RAZ
			// D1: BUSY (1 = busy)
			// D0: BCB_LOAD (1 = loading from blit list)
			return (mbBlitterActive ? 0x02 : 0x00) | (mbBlitterListActive ? 0x01 : 0x00);

		case 0x54:	// IRQ_STATUS
			return mbIRQRequest ? 0x01 : 0x00;

		case 0x5E:	// MEMAC_CONTROL
			return mMemAcControl;

		case 0x5F:	// MEMAC_BANK_SEL
			return mMemAcBankA;
	}

	return -1;
}

bool ATVBXEEmulator::WriteControl(uint8 addrLo, uint8 value) {
	switch(addrLo) {
		case 0x40:	// VIDEO_CONTROL
			mbXdlEnabled = (value & 0x01) != 0;
			mbExtendedColor = (value & 0x02) != 0;
			mbOvTrans = (value & 0x04) == 0;
			mbOvTrans15 = (value & 0x08) != 0;
			break;

		case 0x41:	// XDL_ADR0
			mXdlBaseAddr = (mXdlBaseAddr & 0x7FF00) + ((uint32)value << 0);
			break;

		case 0x42:	// XDL_ADR1
			mXdlBaseAddr = (mXdlBaseAddr & 0x700FF) + ((uint32)value << 8);
			break;

		case 0x43:	// XDL_ADR2
			mXdlBaseAddr = (mXdlBaseAddr & 0x0FFFF) + ((uint32)(value & 0x07) << 16);
			break;

		case 0x44:	// CSEL
			mCsel = value;
			break;

		case 0x45:	// PSEL
			mPsel = value & 0x03;
			break;

		case 0x46:	// CR
			mPalette[mPsel][mCsel] = (mPalette[mPsel][mCsel] & 0x00FFFF) + ((uint32)(value & 0xFE) << 16) + (((uint32)(value & 0x80) << 9));
			break;

		case 0x47:	// CG
			mPalette[mPsel][mCsel] = (mPalette[mPsel][mCsel] & 0xFF00FF) + ((uint32)(value & 0xFE) << 8) + (((uint32)(value & 0x80) << 1));
			break;

		case 0x48:	// CB
			mPalette[mPsel][mCsel] = (mPalette[mPsel][mCsel] & 0xFFFF00) + (value & 0xFE) + (value >> 7);
			++mCsel;
			break;

		case 0x50:	// BL_ADR0
			mBlitListAddr = (mBlitListAddr & 0x7FF00) + ((uint32)value << 0);
			break;

		case 0x51:	// BL_ADR1
			mBlitListAddr = (mBlitListAddr & 0x700FF) + ((uint32)value << 8);
			break;

		case 0x52:	// BL_ADR2
			mBlitListAddr = (mBlitListAddr & 0x0FFFF) + ((uint32)(value & 0x07) << 16);
			break;

		case 0x53:	// BLITTER_START
			// D0: 1 = START, 0 = STOP
			if (value & 1) {
				mbBlitterEnabled = true;
				mbBlitterListActive = true;
				mbBlitterActive = false;
				mbBlitterContinue = true;
				mBlitListFetchAddr = mBlitListAddr;

				// We have to load the first entry immediately because some demos are a
				// bit creative and overwrite the first entry without checking blitter
				// status...
				LoadBlitter();
			} else {
				mbBlitterListActive = false;
				mbBlitterActive = false;
				mbBlitterEnabled = false;
			}
			break;

		case 0x54:	// IRQ_CONTROL
			// acknowledge blitter interrupt
			if (mbIRQRequest) {
				mbIRQRequest = false;

				if (mbIRQEnabled)
					mpConn->VBXENegateIRQ();
			}

			// modify blitter IRQ enabled setting
			mbIRQEnabled = (value & 0x01) != 0;
			break;

		case 0x55:	// P0
			mOvPriority[0] = ConvertPriorityToNative(value);
			break;

		case 0x56:	// P1
			mOvPriority[1] = ConvertPriorityToNative(value);
			break;

		case 0x57:	// P2
			mOvPriority[2] = ConvertPriorityToNative(value);
			break;

		case 0x58:	// P3
			mOvPriority[3] = ConvertPriorityToNative(value);
			break;

		case 0x5D:	// MEMAC_B_CONTROL
			if (mMemAcBankB != value) {
				mMemAcBankB = value;
				UpdateMemoryMaps();
			}
			break;

		case 0x5E:	// MEMAC_CONTROL
			if (mMemAcControl != value) {
				mMemAcControl = value;
				UpdateMemoryMaps();
			}
			break;

		case 0x5F:	// MEMAC_BANK_SEL
			if (mMemAcBankA != value) {
				mMemAcBankA = value;
				UpdateMemoryMaps();
			}
			break;

		default:
			return false;
	}

	return true;
}

bool ATVBXEEmulator::StaticGTIAWrite(void *thisptr, uint32 reg, uint8 value) {
	if ((uint8)reg >= 0x80)
		((ATVBXEEmulator *)thisptr)->WarmReset();

	return false;
}

void ATVBXEEmulator::InitMemoryMaps() {
	ShutdownMemoryMaps();

	// Window A has priority over window B
	mpMemLayerMEMACA = mpMemMan->CreateLayer(kATMemoryPri_Extsel+1, NULL, 0xD8, 0x10, false);
	mpMemLayerMEMACB = mpMemMan->CreateLayer(kATMemoryPri_Extsel, NULL, 0x40, 0x40, false);

	ATMemoryHandlerTable handler;
	handler.mbPassReads			= true;
	handler.mbPassAnticReads	= true;
	handler.mbPassWrites		= true;
	handler.mpThis				= this;
	handler.mpDebugReadHandler	= NULL;
	handler.mpReadHandler		= NULL;
	handler.mpWriteHandler		= StaticGTIAWrite;

	if (mb5200Mode)
		mpMemLayerGTIAOverlay = mpMemMan->CreateLayer(kATMemoryPri_HardwareOverlay, handler, 0xC0, 0x10);
	else
		mpMemLayerGTIAOverlay = mpMemMan->CreateLayer(kATMemoryPri_HardwareOverlay, handler, 0xD0, 0x01);

	mpMemMan->EnableLayer(mpMemLayerGTIAOverlay, kATMemoryAccessMode_CPUWrite, true);

	handler.mbPassReads			= true;
	handler.mbPassAnticReads	= true;
	handler.mbPassWrites		= true;
	handler.mpThis				= this;
	handler.mpDebugReadHandler	= StaticReadControl;
	handler.mpReadHandler		= StaticReadControl;
	handler.mpWriteHandler		= StaticWriteControl;
	mpMemLayerRegisters = mpMemMan->CreateLayer(kATMemoryPri_Hardware, handler, mRegBase, 0x01);
	mpMemMan->EnableLayer(mpMemLayerRegisters, true);
}

void ATVBXEEmulator::ShutdownMemoryMaps() {
	if (mpMemLayerGTIAOverlay) {
		mpMemMan->DeleteLayer(mpMemLayerGTIAOverlay);
		mpMemLayerGTIAOverlay = NULL;
	}

	if (mpMemLayerRegisters) {
		mpMemMan->DeleteLayer(mpMemLayerRegisters);
		mpMemLayerRegisters = NULL;
	}

	if (mpMemLayerMEMACA) {
		mpMemMan->DeleteLayer(mpMemLayerMEMACA);
		mpMemLayerMEMACA = NULL;
	}

	if (mpMemLayerMEMACB) {
		mpMemMan->DeleteLayer(mpMemLayerMEMACB);
		mpMemLayerMEMACB = NULL;
	}
}

void ATVBXEEmulator::UpdateMemoryMaps() {
	if (mb5200Mode) {
		// Window A ($D800-E7FF)
		uint8 *winA = mpMemory + (((uint32)mMemAcBankA << 12) & 0x7F000);
		mpMemMan->SetLayerMemory(mpMemLayerMEMACA, winA, 0xD8, 0x10);
		mpMemMan->EnableLayer(mpMemLayerMEMACA, true);
	} else {
		// Window B ($4000-7FFF)
		if (mMemAcBankB & 0xC0) {
			uint8 *winB = mpMemory + ((mMemAcBankB & 0x1F) << 14);
			mpMemMan->SetLayerMemory(mpMemLayerMEMACB, winB, 0x40, 0x40);

			// MEMAC-B access - CPU
			mpMemMan->EnableLayer(mpMemLayerMEMACB, kATMemoryAccessMode_CPURead, (mMemAcBankB & 0x80) != 0);
			mpMemMan->EnableLayer(mpMemLayerMEMACB, kATMemoryAccessMode_CPUWrite, (mMemAcBankB & 0x80) != 0);

			// MEMAC-B access - ANTIC
			mpMemMan->EnableLayer(mpMemLayerMEMACB, kATMemoryAccessMode_AnticRead, (mMemAcBankB & 0x40) != 0);
		} else {
			mpMemMan->EnableLayer(mpMemLayerMEMACB, false);
		}

		if ((mMemAcBankA & 0x80) && (mMemAcControl & 0x0C)) {
			static const int kPageCount[4]={
				16,		// 00 - 4K window
				32,		// 01 - 8K window
				64,		// 10 - 16K window
				128		// 11 - 32K window
			};

			int numPages = kPageCount[mMemAcControl & 3];
			int pageBase = mMemAcControl & 0xF0;

			// check for overflow -- window is truncated in this case (does not wrap to $0000)
			if (pageBase + numPages > 0x100)
				numPages = 0x100 - pageBase;

			static const uint32 kAddrMask[4]={
				0x7F000,
				0x7E000,
				0x7C000,
				0x78000
			};

			uint8 *winA = mpMemory + (((uint32)mMemAcBankA << 12) & kAddrMask[mMemAcControl & 3]);
			mpMemMan->SetLayerMemory(mpMemLayerMEMACA, winA, pageBase, numPages);

			// MEMAC-A access - CPU
			mpMemMan->EnableLayer(mpMemLayerMEMACA, kATMemoryAccessMode_CPURead, (mMemAcControl & 0x08) != 0);
			mpMemMan->EnableLayer(mpMemLayerMEMACA, kATMemoryAccessMode_CPUWrite, (mMemAcControl & 0x08) != 0);

			// MEMAC-A access - ANTIC
			mpMemMan->EnableLayer(mpMemLayerMEMACA, kATMemoryAccessMode_AnticRead, (mMemAcControl & 0x04) != 0);
		} else {
			mpMemMan->EnableLayer(mpMemLayerMEMACA, false);
		}
	}
}

void ATVBXEEmulator::BeginFrame() {
	mbXdlActive = mbXdlEnabled;
	mXdlAddr = mXdlBaseAddr;
	mXdlRepeatCounter = 1;
	mOvWidth = kOvWidth_Normal;
	mOvMode = kOvMode_Disabled;

	mpPfPalette = mPalette[0];
	mpOvPalette = mPalette[1];
	mPfPaletteIndex = 0;
	mOvPaletteIndex = 1;

	mbAttrMapEnabled = false;
	mAttrWidth = 8;
	mAttrHeight = 8;
	mAttrHscroll = 0;
	mAttrVscroll = 0;
	mDMACycles = 0;

	mOvHscroll = 0;
	mOvVscroll = 0;
	mOvAddr = 0;
	mOvStep = 0;
}

void ATVBXEEmulator::BeginScanline(uint32 *dst, const uint8 *mergeBuffer, const uint8 *anticBuffer, bool hires) {
	mpDst = dst;

	mbHiresMode = hires;
	mX = 0;
	mpMergeBuffer = mergeBuffer;
	mpMergeBuffer0 = mergeBuffer;
	mpAnticBuffer = anticBuffer;
	mpAnticBuffer0 = anticBuffer;
	mDMACycles = 0;

	if (dst)
		VDMemset32(dst, mpPfPalette[mpColorTable[kColorBAK]], 68*2);

	if (--mXdlRepeatCounter) {
		mOvTextRow = (mOvTextRow + 1) & 7;
	} else {
		if (!mbXdlActive) {
			mXdlRepeatCounter = 0xFFFFFFFF;
			mbAttrMapEnabled = false;
			mOvMode = kOvMode_Disabled;
		} else {
			bool reloadAttrMap = false;

			uint32 xdlStart = mXdlAddr;
			uint8 xdl1 = VBXE_FETCH(mXdlAddr++);
			uint8 xdl2 = VBXE_FETCH(mXdlAddr++);

			if (xdl1 & 4)
				mOvMode = kOvMode_Disabled;
			else if (xdl1 & 3)
				mOvMode = kOvModeTable[(xdl1 & 3) - 1][(xdl2 >> 4) & 3];

			if (xdl1 & 0x10)
				mbAttrMapEnabled = false;
			else if (xdl1 & 0x08) {
				mbAttrMapEnabled = true;
				reloadAttrMap = true;
			}

			// XDLC_RPTL (1 byte)
			if (xdl1 & 0x20)
				mXdlRepeatCounter = VBXE_FETCH(mXdlAddr++);

			++mXdlRepeatCounter;

			// XDLC_OVADR (5 byte)
			if (xdl1 & 0x40) {
				uint8 ov1 = VBXE_FETCH(mXdlAddr++);
				uint8 ov2 = VBXE_FETCH(mXdlAddr++);
				uint8 ov3 = VBXE_FETCH(mXdlAddr++);
				uint8 step1 = VBXE_FETCH(mXdlAddr++);
				uint8 step2 = VBXE_FETCH(mXdlAddr++);

				mOvAddr = (uint32)ov1 + ((uint32)ov2 << 8) + ((uint32)ov3 << 16);
				mOvStep = ((uint32)step1 + ((uint32)step2 << 8)) & 0xFFF;
			}

			// XDLC_OVSCRL (2 byte)
			if (xdl1 & 0x80) {
				mOvHscroll = VBXE_FETCH(mXdlAddr++) & 7;
				mOvVscroll = VBXE_FETCH(mXdlAddr++) & 7;
			}

			// XDLC_CHBASE (1 byte)
			if (xdl2 & 0x01) {
				uint8 chbase = VBXE_FETCH(mXdlAddr++);

				mChAddr = (uint32)chbase << 11;
			}

			// XDLC_MAPADR (5 byte)
			if (xdl2 & 0x02) {
				uint8 ad1 = VBXE_FETCH(mXdlAddr++);
				uint8 ad2 = VBXE_FETCH(mXdlAddr++);
				uint8 ad3 = VBXE_FETCH(mXdlAddr++);
				uint8 step1 = VBXE_FETCH(mXdlAddr++);
				uint8 step2 = VBXE_FETCH(mXdlAddr++);

				mAttrAddr = (uint32)ad1 + ((uint32)ad2 << 8) + ((uint32)ad3 << 16);
				mAttrStep = ((uint32)step1 + ((uint32)step2 << 8)) & 0xFFF;

				reloadAttrMap = true;
			}

			// XDLC_MAPPAR (4 byte)
			if (xdl2 & 0x04) {
				mAttrHscroll = VBXE_FETCH(mXdlAddr++) & 0x1F;
				mAttrVscroll = VBXE_FETCH(mXdlAddr++) & 0x1F;
				uint8 width = VBXE_FETCH(mXdlAddr++);
				uint8 height = VBXE_FETCH(mXdlAddr++);

				mAttrWidth = (width & 31) + 1;
				mAttrHeight = (height & 31) + 1;

				// An attribute map width narrow than 8 is invalid.
				if (mAttrWidth < 8)
					mAttrWidth = 8;
			}

			// XDLC_ATT (2 byte)
			if (xdl2 & 0x08) {
				uint8 ctl = VBXE_FETCH(mXdlAddr++);
				uint8 pri = VBXE_FETCH(mXdlAddr++);

				mOvWidth = ((ctl & 3) == 3) ? kOvWidth_Wide : (OvWidth)(ctl & 3);
				mPfPaletteIndex = ctl >> 6;
				mOvPaletteIndex = (ctl >> 4) & 3;
				mpPfPalette = mPalette[mPfPaletteIndex];
				mpOvPalette = mPalette[mOvPaletteIndex];

				mOvMainPriority = ConvertPriorityToNative(pri);
			}

			// XDLC_END
			if (xdl2 & 0x80)
				mbXdlActive = false;

			mOvTextRow = mOvVscroll & 7;

			if (reloadAttrMap)
				mAttrRow = mAttrVscroll % mAttrHeight;

			// deduct XDL cycles
			mDMACycles += (mXdlAddr - xdlStart);

			// deduct overlay map cycles
			static const uint32 kOvCyclesPerMode[5][3]={
				{ 0, 0, 0 },
				{ 128, 160, 168 },
				{ 256, 320, 336 },
				{ 256, 320, 336 },
				{ 195, 243, 255 },
			};

			mDMACycles += kOvCyclesPerMode[mOvMode][mOvWidth];
			
			// deduct attribute map cycles
			if (mbAttrMapEnabled && (reloadAttrMap || mAttrRow == 0)) {
				static const uint32 kAttrMapWidth[3]={
					256,
					320,
					336
				};

				mDMACycles += ((kAttrMapWidth[mOvWidth] + mAttrHscroll - 1) / mAttrWidth + 1) * 4;
			}

			VDASSERT(mDMACycles < 114 * 8);
		}
	}
}

void ATVBXEEmulator::RenderScanline(int xend, bool pfpmrendered) {
	int x1 = mX;

	if (x1 >= xend)
		return;

	// render spans and process register changes
	do {
		int x2 = xend;

		if (mRCIndex < mRCCount) {
			const RegisterChange *rc0 = &mRegisterChanges[mRCIndex];
			const RegisterChange *rc = rc0;
			do {
				int xchg = rc->mPos;
				if (xchg > x1) {
					if (x2 > xchg)
						x2 = xchg;
					break;
				}

				++rc;
			} while(++mRCIndex < mRCCount);

			UpdateRegisters(rc0, rc - rc0);
		}

		// render out attpixels
		int x1h = x1 * 2;
		int x2h = x2 * 2;

		while(x1h < x2h) {
			int xth = x2h;

			if (mbAttrMapEnabled)
				xth = RenderAttrPixels(x1h, x2h);
			else
				RenderAttrDefaultPixels(x1h, x2h);

			VDASSERT(xth > x1h);

			bool hiresMode = mbHiresMode;
			bool revMode = false;

			if (mAttrPixels[x1h].mCtrl & 0x04) {
				revMode = true;
				hiresMode = !hiresMode;

				const int x1l = x1h >> 1;
				const int xtl = (xth + 1) >> 1;

				if (hiresMode) {

					static const uint8 kPriTable[8]={
						0,		// BAK
						1,		// PF0
						2,		// PF1
						2,		// PF01
						3,		// PF2
						3,		// PF02
						3,		// PF12
						3,		// PF012
					};

					for(int x = x1l; x < xtl; ++x)
						mTempAnticData[x] = kPriTable[mpMergeBuffer0[x] & 7];

					for(int x = x1l; x < xtl; ++x)
						mTempMergeBuffer[x] = (mpMergeBuffer0[x] & (P0|P1|P2|P3)) | PF2;

					mpAnticBuffer = mTempAnticData;
					mpMergeBuffer = mTempMergeBuffer;
				} else {
					for(int x = x1l; x < xtl; ++x) {
						uint8 d = mpMergeBuffer0[x];

						if (d & PF2) {
							uint8 c = mpAnticBuffer0[x];
							mTempMergeBuffer[x] = (d & ~PF) | (1 << c);
						}
					}

					mpMergeBuffer = mTempMergeBuffer;
				}
			}

			// 40 column mode is set by ANTIC during horizontal blank if ANTIC modes 2, 3, or
			// F are used. 40 column mode has the following effects:
			//
			//	* The priority logic always sees PF2.
			//	* The collision logic sees either BAK or PF2. Adjacent bits are ORed each color
			//	  clock to determine this (PF2C in schematic).
			//	* The playfield bits are used instead to substitute the luminance of PF1 on top
			//	  of the priority logic output. This happens even if players have priority.
			//
			// The flip-flip in the GTIA that controls 40 column mode can only be set by the
			// horizontal sync command, but can be reset at any time whenever either of the
			// top two bits of PRIOR are set. If this happens, the GTIA will begin interpreting
			// AN0-AN2 in lores mode, but ANTIC will continue sending in hires mode. The result
			// is that the bit pair patterns 00-11 produce PF0-PF3 instead of BAK + PF0-PF2 as
			// usual.

			switch(mPRIOR & 0xc0) {
				case 0x00:
					if (hiresMode)
						RenderMode8(x1h, xth);
					else if (pfpmrendered)
						RenderLores(x1h, xth);
					else
						RenderLoresBlank(x1h, xth, mbAttrMapEnabled);
					break;

				case 0x40:
					RenderMode9(x1h, xth);
					break;

				case 0x80:
					RenderMode10(x1h, xth);
					break;

				case 0xC0:
					RenderMode11(x1h, xth);
					break;
			}

			if (revMode) {
				mpAnticBuffer = mpAnticBuffer0;
				mpMergeBuffer = mpMergeBuffer0;
			}

			x1h = xth;
		}

		RenderOverlay(x1, x2);

		x1 = x2;
	} while(x1 < xend);

	mX = x1;
}

void ATVBXEEmulator::EndScanline() {
	if (mpDst) {
		VDMemset32(mpDst + 444*2, mpPfPalette[mpColorTable[8]], (456 - 444) * 2);
		mpDst = NULL;
	}

	// commit any outstanding register changes
	if (mRCIndex < mRCCount)
		UpdateRegisters(&mRegisterChanges[mRCIndex], mRCCount - mRCIndex);

	mRCCount = 0;
	mRCIndex = 0;
	mRegisterChanges.clear();

	// update overlay address
	if (mOvMode != kOvMode_Disabled) {
		if (mOvMode != kOvMode_80Text || mXdlRepeatCounter == 1 || mOvTextRow == 7)
			mOvAddr += mOvStep;
	}

	if (mbAttrMapEnabled && ++mAttrRow >= mAttrHeight) {
		mAttrRow = 0;
		mAttrAddr += mAttrStep;
	}

	mBlitCyclesLeft += (8 * 114) - mDMACycles;
	RunBlitter();
}

void ATVBXEEmulator::AddRegisterChange(uint8 pos, uint8 addr, uint8 value) {
	RegisterChanges::iterator it(mRegisterChanges.end()), itBegin(mRegisterChanges.begin());

	while(it != itBegin && it[-1].mPos > pos)
		--it;

	RegisterChange change;
	change.mPos = pos;
	change.mReg = addr;
	change.mValue = value;
	change.mPad = 0;
	mRegisterChanges.insert(it, change);

	++mRCCount;
}

void ATVBXEEmulator::UpdateRegisters(const RegisterChange *rc, int count) {
	while(count--) {
		// process register change
		uint8 value = rc->mValue;

		switch(rc->mReg) {
		case 0x12:
			value &= 0xfe;
			mColorTable[kColorP0] = value;
			mColorTable[kColorP0P1] = value | mColorTable[kColorP1];
			break;
		case 0x13:
			value &= 0xfe;
			mColorTable[kColorP1] = value;
			mColorTable[kColorP0P1] = mColorTable[kColorP0] | value;
			break;
		case 0x14:
			value &= 0xfe;
			mColorTable[kColorP2] = value;
			mColorTable[kColorP2P3] = value | mColorTable[kColorP3];
			mColorTable[kColorPF3P2] = mColorTable[kColorPF3] | value;
			mColorTable[kColorPF3P2P3] = mColorTable[kColorPF3P3] | value;
			break;
		case 0x15:
			value &= 0xfe;
			mColorTable[kColorP3] = value;
			mColorTable[kColorP2P3] = mColorTable[kColorP2] | value;
			mColorTable[kColorPF3P3] = mColorTable[kColorPF3] | value;
			mColorTable[kColorPF3P2P3] = mColorTable[kColorPF3P2] | value;
			break;
		case 0x16:
			value &= 0xfe;
			mColorTable[kColorPF0] = value;
			break;
		case 0x17:
			value &= 0xfe;
			mColorTable[kColorPF1] = value;
			break;
		case 0x18:
			value &= 0xfe;
			mColorTable[kColorPF2] = value;
			break;
		case 0x19:
			value &= 0xfe;
			mColorTable[kColorPF3] = value;
			mColorTable[kColorPF3P2] = value | mColorTable[kColorP2];
			mColorTable[kColorPF3P3] = value | mColorTable[kColorP3];
			mColorTable[kColorPF3P2P3] = value | mColorTable[kColorP2P3];
			break;
		case 0x1A:
			value &= 0xfe;
			mColorTable[kColorBAK] = value;
			break;
		case 0x1B:
			mPRIOR = value;
			mpPriTable = mPriorityTables[(value & 15) + (value&32 ? 16 : 0)];
			mpPriTableHi = mPriorityTablesHi[(value & 15) + (value&32 ? 16 : 0)];

			if (value & 0xC0)
				mbHiresMode = false;
			break;
		}

		++rc;
	}
}

int ATVBXEEmulator::RenderAttrPixels(int x1h, int x2h) {
	// x1 and x2 are measured in color clocks.
	static const int kBounds[3][2]={
		// Narrow: $40-BF
		{ 64*2, 192*2 },

		// Normal: $30-CF
		{ 48*2, 208*2 },

		// Wide: $2C-D4 (NOTE: This is different from ANTIC!)
		{ 44*2, 212*2 },
	};

	const int x1h0 = x1h;
	const int x2h0 = x2h;
	int xlh = kBounds[mOvWidth][0];
	int xrh = kBounds[mOvWidth][1];

	if (x1h < xlh) {
		if (x2h <= xlh) {
			RenderAttrDefaultPixels(x1h, x2h);
			return x2h;
		}

		RenderAttrDefaultPixels(x1h, xlh);
		x1h = xlh;
	}

	if (x2h > xrh) {
		if (x1h >= xrh) {
			RenderAttrDefaultPixels(x1h, x2h);
			return x2h;
		}

		x2h = xrh;
	}

	if (x2h <= x1h)
		return x1h;

	uint32 offset = (x1h - xlh + mAttrHscroll) % mAttrWidth;
	uint32 srcAddr = mAttrAddr + (x1h - xlh) / mAttrWidth * 4;
	int hiresShift = mAttrWidth > 16 ? 2 : mAttrWidth > 8 ? 1 : 0;

	AttrPixel px;
	px.mPFK = 0;
	px.mPF0 = VBXE_FETCH(srcAddr + 0);
	px.mPF1 = VBXE_FETCH(srcAddr + 1);
	px.mPF2 = VBXE_FETCH(srcAddr + 2);
	px.mCtrl = VBXE_FETCH(srcAddr + 3);
	px.mPriority = mOvPriority[px.mCtrl & 3];
	srcAddr += 4;

	const uint8 resBit = x1h > x1h0 ? 0 : px.mCtrl;

	do {
		px.mHiresFlag = (sint8)(px.mPF0 << (offset >> hiresShift)) >> 7;
		mAttrPixels[x1h] = px;

		if (++offset >= mAttrWidth) {
			px.mPF0 = VBXE_FETCH(srcAddr + 0);
			px.mPF1 = VBXE_FETCH(srcAddr + 1);
			px.mPF2 = VBXE_FETCH(srcAddr + 2);
			px.mCtrl = VBXE_FETCH(srcAddr + 3);
			px.mPriority = mOvPriority[px.mCtrl & 3];
			srcAddr += 4;
			offset = 0;

			if ((px.mCtrl ^ resBit) & 0x04)
				return x1h + 1;
		}
	} while(++x1h < x2h);

	if (x2h < x2h0) {
		RenderAttrDefaultPixels(x2h, x2h0);
		x2h = x2h0;
	}

	return x2h;
}

void ATVBXEEmulator::RenderAttrDefaultPixels(int x1h, int x2h) {
	const AttrPixel px = {
		0,
		mColorTable[kColorPF0],
		mColorTable[kColorPF1],
		mColorTable[kColorPF2],
		(mPfPaletteIndex << 6) + (mOvPaletteIndex << 4),
		0,
		mOvMainPriority
	};

	for(int x = x1h; x < x2h; ++x)
		mAttrPixels[x] = px;
}

void ATVBXEEmulator::RenderLores(int x1h, int x2h) {
	const uint8 *__restrict colorTable = mpColorTable;
	const uint8 (*__restrict priTable)[2] = mpPriTable;

	uint32 *dst = mpDst + x1h*2;
	uint8 *priDst = mOvPriDecode + x1h;
	const uint8 *src = mpMergeBuffer + (x1h >> 1);

	const AttrPixel *apx = &mAttrPixels[x1h];

	if (x1h & 1) {
		uint8 i0 = *src++;
		uint8 a0 = priTable[i0][0];
		uint8 b0 = priTable[i0][1];
		uint8 c0 = colorTable[b0];
		uint8 d1 = (&apx->mPFK)[a0] | c0;

		dst[0] = dst[1] = mPalette[apx[1].mCtrl >> 6][d1];
		priDst[0] = apx->mPriority & i0;
		++apx;
		dst += 2;
		++priDst;
		++x1h;
	}

	int w = (x2h - x1h) >> 1;

	for(int i=0; i<w; ++i) {
		uint8 i0 = *src++;
		uint8 a0 = priTable[i0][0];
		uint8 b0 = priTable[i0][1];
		uint8 c0 = colorTable[b0];
		uint8 d0 = (&apx[0].mPFK)[a0] | c0;
		uint8 d1 = (&apx[1].mPFK)[a0] | c0;

		dst[0] = dst[1] = mPalette[apx[0].mCtrl >> 6][d0];
		dst[2] = dst[3] = mPalette[apx[1].mCtrl >> 6][d1];
		priDst[0] = apx[0].mPriority & i0;
		priDst[1] = apx[1].mPriority & i0;
		apx += 2;
		dst += 4;
		priDst += 2;
	}

	if (x2h & 1) {
		uint8 i0 = *src;
		uint8 a0 = priTable[i0][0];
		uint8 b0 = priTable[i0][1];
		uint8 c0 = colorTable[b0];
		uint8 d0 = (&apx->mPFK)[a0] | c0;

		dst[0] = dst[1] = mPalette[apx->mCtrl >> 6][d0];
		priDst[0] = apx->mPriority & i0;
	}
}

void ATVBXEEmulator::RenderLoresBlank(int x1h, int x2h, bool attrMapEnabled) {
	const uint8 *__restrict colorTable = mpColorTable;
	const uint8 (*__restrict priTable)[2] = mpPriTable;

	uint32 *dst = mpDst + x1h*2;
	uint8 *priDst = mOvPriDecode + x1h;

	const AttrPixel *apx = &mAttrPixels[x1h];

	const uint8 a0 = priTable[0][0];
	const uint8 b0 = priTable[0][1];
	const uint8 c0 = colorTable[b0];

	if (attrMapEnabled) {
		if (x1h & 1) {
			uint8 d1 = (&apx->mPFK)[a0] | c0;

			dst[0] = dst[1] = mPalette[apx[1].mCtrl >> 6][d1];
			++apx;
			dst += 2;
			++x1h;
		}

		int w = (x2h - x1h) >> 1;

		for(int i=0; i<w; ++i) {
			uint8 d0 = (&apx[0].mPFK)[a0] | c0;
			uint8 d1 = (&apx[1].mPFK)[a0] | c0;

			dst[0] = dst[1] = mPalette[apx[0].mCtrl >> 6][d0];
			dst[2] = dst[3] = mPalette[apx[1].mCtrl >> 6][d1];
			apx += 2;
			dst += 4;
		}

		if (x2h & 1) {
			uint8 d0 = (&apx->mPFK)[a0] | c0;

			dst[0] = dst[1] = mPalette[apx->mCtrl >> 6][d0];
		}
	} else {
		// The attribute map is disabled, so we can assume that all attributes are
		// the same.
		const uint32 pixel = mPalette[apx[0].mCtrl >> 6][(&apx[0].mPFK)[a0] | c0];

		int w = (x2h - x1h) * 2;
		while(w--)
			*dst++ = pixel;
	}

	memset(priDst, 0, x2h - x1h);
}

void ATVBXEEmulator::RenderMode8(int x1h, int x2h) {
	const uint8 *__restrict colorTable = mpColorTable;

	const uint8 *lumasrc = &mpAnticBuffer[x1h >> 1];
	uint32 *dst = mpDst + x1h*2;
	uint8 *priDst = mOvPriDecode + x1h;
	const uint8 *src = mpMergeBuffer + (x1h >> 1);

	if (mbExtendedColor) {
		const uint8 (*__restrict priTable)[2] = mpPriTableHi;
		const AttrPixel *apx = &mAttrPixels[x1h];

		if (x1h & 1) {
			uint8 lb = *lumasrc++;
			uint8 i1 = *src++;

			if (lb & 1)
				i1 -= (i1 & PF2) >> 1;

			i1 += (i1 & PF2) & apx->mHiresFlag;

			uint8 a1 = priTable[i1][0];
			uint8 b1 = priTable[i1][1];
			uint8 c1 = (&apx->mPFK)[a1] | colorTable[b1];

			dst[0] = dst[1] = mPalette[apx->mCtrl >> 6][c1];
			priDst[0] = apx->mPriority & i1;
			++apx;
			dst += 2;
			++priDst;
			++x1h;
		}

		int w = (x2h - x1h) >> 1;
		while(w--) {
			uint8 lb = *lumasrc++;
			uint8 i0 = *src++;
			uint8 i1 = i0;

			if (lb & 2)
				i0 -= (i0 & PF2) >> 1;

			if (lb & 1)
				i1 -= (i1 & PF2) >> 1;

			i0 += (i0 & PF2) & apx[0].mHiresFlag;
			i1 += (i1 & PF2) & apx[1].mHiresFlag;

			uint8 a0 = priTable[i0][0];
			uint8 a1 = priTable[i1][0];
			uint8 b0 = priTable[i0][1];
			uint8 b1 = priTable[i1][1];
			uint8 c0 = (&apx[0].mPFK)[a0] | colorTable[b0];
			uint8 c1 = (&apx[1].mPFK)[a1] | colorTable[b1];

			dst[0] = dst[1] = mPalette[apx[0].mCtrl >> 6][c0];
			dst[2] = dst[3] = mPalette[apx[1].mCtrl >> 6][c1];
			priDst[0] = apx[0].mPriority & i0;
			priDst[1] = apx[1].mPriority & i1;
			apx += 2;
			dst += 4;
			priDst += 2;
		}

		if (x2h & 1) {
			uint8 lb = *lumasrc++;
			uint8 i0 = *src++;

			if (lb & 2)
				i0 -= (i0 & PF2) >> 1;

			i0 += (i0 & PF2) & apx[0].mHiresFlag;

			uint8 a0 = priTable[i0][0];
			uint8 b0 = priTable[i0][1];
			uint8 c0 = (&apx[0].mPFK)[a0] | colorTable[b0];

			dst[0] = dst[1] = mPalette[apx[0].mCtrl >> 6][c0];
			priDst[0] = apx[0].mPriority & i0;
		}
	} else {
		const AttrPixel *apx = &mAttrPixels[x1h];
		const uint8 (*__restrict priTable)[2] = mpPriTableHi;

		if (x1h & 1) {
			uint8 lb = *lumasrc++;
			uint8 i0 = *src++;
			uint8 i1 = i0;

			i1 += (i1 & PF2) & apx->mHiresFlag;

			uint8 a1 = priTable[i1][0];
			uint8 b1 = priTable[i1][1];
			uint8 c1 = (&apx[1].mPFK)[a1] | colorTable[b1];

			if (lb & 1) {
				c1 = (c1 & 0xf0) + (apx->mPF1 & 0x0f);
			}

			dst[0] = dst[1] = mPalette[apx->mCtrl >> 6][c1];

			priDst[0] = apx->mPriority & ((i1 & ~PF2) | (lb & 1 ? PF2 : 0));
			++apx;
			dst += 2;
			++priDst;
			++x1h;
		}

		int w = (x2h - x1h) >> 1;
		while(w--) {
			uint8 lb = *lumasrc++;
			uint8 i0 = *src++;
			uint8 i1 = i0;

			i0 += (i0 & PF2) & apx[0].mHiresFlag;
			i1 += (i1 & PF2) & apx[1].mHiresFlag;

			uint8 a0 = priTable[i0][0];
			uint8 a1 = priTable[i1][0];
			uint8 b0 = priTable[i0][1];
			uint8 b1 = priTable[i1][1];
			uint8 c0 = (&apx[0].mPFK)[a0] | colorTable[b0];
			uint8 c1 = (&apx[1].mPFK)[a1] | colorTable[b1];

			if (lb & 2) {
				c0 = (c0 & 0xf0) + (apx[0].mPF1 & 0x0f);
			}

			if (lb & 1) {
				c1 = (c1 & 0xf0) + (apx[1].mPF1 & 0x0f);
			}

			dst[0] = dst[1] = mPalette[apx[0].mCtrl >> 6][c0];
			dst[2] = dst[3] = mPalette[apx[1].mCtrl >> 6][c1];

			priDst[0] = apx[0].mPriority & ((i0 & ~PF2) | (lb & 2 ? PF2 : 0));
			priDst[1] = apx[1].mPriority & ((i1 & ~PF2) | (lb & 1 ? PF2 : 0));
			apx += 2;
			dst += 4;
			priDst += 2;
		}

		if (x2h & 1) {
			uint8 lb = *lumasrc++;
			uint8 i0 = *src++;

			i0 += (i0 & PF2) & apx[0].mHiresFlag;

			uint8 a0 = priTable[i0][0];
			uint8 b0 = priTable[i0][1];
			uint8 c0 = (&apx[0].mPFK)[a0] | colorTable[b0];

			if (lb & 2) {
				c0 = (c0 & 0xf0) + (apx[0].mPF1 & 0x0f);
			}

			dst[0] = dst[1] = mPalette[apx[0].mCtrl >> 6][c0];

			priDst[0] = apx[0].mPriority & ((i0 & ~PF2) | (lb & 2 ? PF2 : 0));
		}
	}
}

void ATVBXEEmulator::RenderMode9(int x1h, int x2h) {
	static const uint8 kPlayerMaskLookup[16]={0xff};

	const uint8 *__restrict colorTable = mpColorTable;
	const uint8 (*__restrict priTable)[2] = mpPriTable;

	uint32 *dst = mpDst + x1h*2;
	uint8 *priDst = mOvPriDecode + x1h;
	const uint8 *src = mpMergeBuffer + (x1h >> 1);

	// 1 color / 16 luma mode
	//
	// In this mode, PF0-PF3 are forced off, so no playfield collisions ever register
	// and the playfield always registers as the background color. Luminance is
	// ORed in after the priority logic, but its substitution is gated by all P/M bits
	// and so it does not affect players or missiles. It does, however, affect PF3 if
	// the fifth player is enabled.

	const AttrPixel *apx = &mAttrPixels[x1h];

	if (x1h & 1) {
		uint8 i0 = *src++ & (P0|P1|P2|P3|PF3);
		uint8 a0 = priTable[i0][0];
		uint8 b0 = priTable[i0][1];
		uint8 c1 = (&apx[1].mPFK)[a0] | colorTable[b0];

		const uint8 *lumasrc = &mpAnticBuffer[(x1h >> 1) & ~1];
		uint8 l0 = ((lumasrc[0] << 2) + lumasrc[1]) & kPlayerMaskLookup[i0 >> 4];

		dst[0] = dst[1] = mPalette[apx->mCtrl >> 6][c1 | l0];
		priDst[1] = apx->mPriority & i0;
		++apx;
		dst += 2;
		++priDst;
		++x1h;
	}

	int w = (x2h - x1h) >> 1;

	int x1 = x1h >> 1;
	while(w--) {
		uint8 i0 = *src++ & (P0|P1|P2|P3|PF3);
		uint8 a0 = priTable[i0][0];
		uint8 b0 = priTable[i0][1];
		uint8 c0 = (&apx[0].mPFK)[a0] | colorTable[b0];
		uint8 c1 = (&apx[1].mPFK)[a0] | colorTable[b0];

		const uint8 *lumasrc = &mpAnticBuffer[x1++ & ~1];
		uint8 l0 = ((lumasrc[0] << 2) + lumasrc[1]) & kPlayerMaskLookup[i0 >> 4];

		dst[0] = dst[1] = mPalette[apx[0].mCtrl >> 6][c0 | l0];
		dst[2] = dst[3] = mPalette[apx[1].mCtrl >> 6][c1 | l0];
		priDst[0] = apx[0].mPriority & i0;
		priDst[1] = apx[1].mPriority & i0;
		apx += 2;
		dst += 4;
		priDst += 2;
	}

	if (x2h & 1) {
		uint8 i0 = *src++ & (P0|P1|P2|P3|PF3);
		uint8 a0 = priTable[i0][0];
		uint8 b0 = priTable[i0][1];
		uint8 c0 = (&apx[0].mPFK)[a0] | colorTable[b0];

		const uint8 *lumasrc = &mpAnticBuffer[x1++ & ~1];
		uint8 l0 = ((lumasrc[0] << 2) + lumasrc[1]) & kPlayerMaskLookup[i0 >> 4];

		dst[0] = dst[1] = mPalette[apx[0].mCtrl >> 6][c0 | l0];
		priDst[0] = apx[0].mPriority & i0;
	}
}

void ATVBXEEmulator::RenderMode10(int x1h, int x2h) {
	const uint8 *__restrict colorTable = mpColorTable;
	const uint8 (*__restrict priTable)[2] = mpPriTable;

	uint32 *dst = mpDst + x1h*2;
	uint8 *priDst = mOvPriDecode + x1h;
	const uint8 *src = mpMergeBuffer + (x1h >> 1);

	// 9 colors
	//
	// This mode works by using AN0-AN1 to trigger either the playfield or the player/missle
	// bits going into the priority logic. This means that when player colors are used, the
	// playfield takes the same priority as that player. Playfield collisions are triggered
	// only for PF0-PF3; P0-P3 colors coming from the playfield do not trigger collisions.

	static const uint8 kMode10Lookup[16]={
		P0,
		P1,
		P2,
		P3,
		PF0,
		PF1,
		PF2,
		PF3,
		0,
		0,
		0,
		0,
		PF0,
		PF1,
		PF2,
		PF3
	};

	const AttrPixel *apx = &mAttrPixels[x1h];

	if (x1h & 1) {
		const uint8 *lumasrc = &mpAnticBuffer[((x1h >> 1) - 1) & ~1];
		uint8 l0 = lumasrc[0]*4 + lumasrc[1];

		uint8 i0 = kMode10Lookup[l0] | (*src++ & 0xf8);
		uint8 a0 = priTable[i0][0];
		uint8 b0 = priTable[i0][1];
		uint8 c1 = (&apx[1].mPFK)[a0] | colorTable[b0];

		dst[0] = dst[1] = mPalette[apx[0].mCtrl >> 6][c1];
		priDst[0] = apx[0].mPriority & i0;
		++apx;
		dst += 2;
		++priDst;
		++x1h;
	}

	int w = (x2h - x1h) >> 1;
	int x1 = x1h >> 1;
	while(w--) {
		const uint8 *lumasrc = &mpAnticBuffer[(x1++ - 1) & ~1];
		uint8 l0 = lumasrc[0]*4 + lumasrc[1];

		uint8 i0 = kMode10Lookup[l0] | (*src++ & 0xf8);
		uint8 a0 = priTable[i0][0];
		uint8 b0 = priTable[i0][1];
		uint8 c0 = (&apx[0].mPFK)[a0] | colorTable[b0];
		uint8 c1 = (&apx[1].mPFK)[a0] | colorTable[b0];

		dst[0] = dst[1] = mPalette[apx[0].mCtrl >> 6][c0];
		dst[2] = dst[3] = mPalette[apx[1].mCtrl >> 6][c1];
		priDst[0] = apx[0].mPriority & i0;
		priDst[1] = apx[1].mPriority & i0;
		apx += 2;
		dst += 4;
		priDst += 2;
	}

	if (x2h & 1) {
		const uint8 *lumasrc = &mpAnticBuffer[(x1 - 1) & ~1];
		uint8 l0 = lumasrc[0]*4 + lumasrc[1];

		uint8 i0 = kMode10Lookup[l0] | (*src++ & 0xf8);
		uint8 a0 = priTable[i0][0];
		uint8 b0 = priTable[i0][1];
		uint8 c0 = (&apx[0].mPFK)[a0] | colorTable[b0];

		dst[0] = dst[1] = mPalette[apx[0].mCtrl >> 6][c0];
		priDst[0] = apx[0].mPriority & i0;
	}
}

void ATVBXEEmulator::RenderMode11(int x1h, int x2h) {
	const uint8 *__restrict colorTable = mpColorTable;
	const uint8 (*__restrict priTable)[2] = mpPriTable;

	uint32 *dst = mpDst + x1h*2;
	uint8 *priDst = mOvPriDecode + x1h;
	const uint8 *src = mpMergeBuffer + (x1h >> 1);

	// 16 colors / 1 luma
	//
	// In this mode, PF0-PF3 are forced off, so no playfield collisions ever register
	// and the playfield always registers as the background color. Chroma is
	// ORed in after the priority logic, but its substitution is gated by all P/M bits
	// and so it does not affect players or missiles. It does, however, affect PF3 if
	// the fifth player is enabled.

	static const uint8 kMode11Lookup[16][2][2]={
		{{0xff,0xff},{0xff,0xf0}},
		{{0x00,0xff},{0x00,0xff}},
		{{0x00,0xff},{0x00,0xff}},
		{{0x00,0xff},{0x00,0xff}},
		{{0x00,0xff},{0x00,0xff}},
		{{0x00,0xff},{0x00,0xff}},
		{{0x00,0xff},{0x00,0xff}},
		{{0x00,0xff},{0x00,0xff}},
		{{0x00,0xff},{0x00,0xff}},
		{{0x00,0xff},{0x00,0xff}},
		{{0x00,0xff},{0x00,0xff}},
		{{0x00,0xff},{0x00,0xff}},
		{{0x00,0xff},{0x00,0xff}},
		{{0x00,0xff},{0x00,0xff}},
		{{0x00,0xff},{0x00,0xff}},
		{{0x00,0xff},{0x00,0xff}}
	};

	const AttrPixel *apx = &mAttrPixels[x1h];

	if (x1h & 1) {
		const uint8 i0 = *src++ & (P0|P1|P2|P3|PF3);
		const uint8 a0 = priTable[i0][0];
		const uint8 b0 = priTable[i0][1];
		uint8 pri1 = (&apx[0].mPFK)[a0] | colorTable[b0];

		const uint8 *lumasrc = &mpAnticBuffer[(x1h >> 1) & ~1];
		uint8 l0 = (lumasrc[0] << 6) + (lumasrc[1] << 4);

		uint8 c1 = (pri1 | (l0 & kMode11Lookup[i0 >> 4][l0 == 0][0])) & kMode11Lookup[i0 >> 4][l0 == 0][1];

		dst[0] = dst[1] = mPalette[apx[0].mCtrl >> 6][c1];
		priDst[1] = apx[0].mPriority & i0;
		++apx;
		dst += 2;
		++priDst;
	}

	int w = (x2h - x1h) >> 1;
	int x1 = x1h >> 1;
	while(w--) {
		const uint8 i0 = *src++ & (P0|P1|P2|P3|PF3);
		const uint8 a0 = priTable[i0][0];
		const uint8 b0 = priTable[i0][1];
		uint8 pri0 = (&apx[0].mPFK)[a0] | colorTable[b0];
		uint8 pri1 = (&apx[1].mPFK)[a0] | colorTable[b0];

		const uint8 *lumasrc = &mpAnticBuffer[x1++ & ~1];
		uint8 l0 = (lumasrc[0] << 6) + (lumasrc[1] << 4);

		uint8 c0 = (pri0 | (l0 & kMode11Lookup[i0 >> 4][l0 == 0][0])) & kMode11Lookup[i0 >> 4][l0 == 0][1];
		uint8 c1 = (pri1 | (l0 & kMode11Lookup[i0 >> 4][l0 == 0][0])) & kMode11Lookup[i0 >> 4][l0 == 0][1];

		dst[0] = dst[1] = mPalette[apx[0].mCtrl >> 6][c0];
		dst[2] = dst[3] = mPalette[apx[1].mCtrl >> 6][c1];
		priDst[0] = apx[0].mPriority & i0;
		priDst[1] = apx[1].mPriority & i0;
		apx += 2;
		dst += 4;
		priDst += 2;
	}

	if (x2h & 1) {
		const uint8 i0 = *src++ & (P0|P1|P2|P3|PF3);
		const uint8 a0 = priTable[i0][0];
		const uint8 b0 = priTable[i0][1];
		uint8 pri0 = (&apx[0].mPFK)[a0] | colorTable[b0];

		const uint8 *lumasrc = &mpAnticBuffer[x1++ & ~1];
		uint8 l0 = (lumasrc[0] << 6) + (lumasrc[1] << 4);

		uint8 c0 = (pri0 | (l0 & kMode11Lookup[i0 >> 4][l0 == 0][0])) & kMode11Lookup[i0 >> 4][l0 == 0][1];

		dst[0] = dst[1] = mPalette[apx[0].mCtrl >> 6][c0];
		priDst[0] = apx[0].mPriority & i0;
	}
}

void ATVBXEEmulator::RenderOverlay(int x1, int x2) {
	// x1 and x2 are measured in color clocks.
	static const int kBounds[3][2]={
		// Narrow: $40-BF
		{ 64, 192 },

		// Normal: $30-CF
		{ 48, 208 },

		// Wide: $2C-D4 (NOTE: This is different from ANTIC!)
		{ 44, 212 },
	};

	uint32 hscroll = mOvMode == kOvMode_80Text ? mOvHscroll : 0;
	int xl = kBounds[mOvWidth][0];
	int xr = kBounds[mOvWidth][1];

	// Note that we need to fetch and process an additional 8 HR pixels
	// (2 color clocks) for scrolled text modes. This includes extending
	// the right fetch border and fetching two color clocks ahead. Bitmap
	// modes don't scroll and don't need this.
	int xr2 = xr;

	int x1f = x1;
	int x2f = x2;

	if (hscroll) {
		xr2 += 2;
		x1f += 2;
		x2f += 2;
	}

	if (x1f < xl)
		x1f = xl;

	if (x2f > xr2)
		x2f = xr2;

	if (x2f > x1f) {
		switch(mOvMode) {
			case kOvMode_Disabled:
				return;

			case kOvMode_LR:
				RenderOverlayLR(mOverlayDecode + x1f*4, x1f - xl, x2f - x1f);
				break;

			case kOvMode_SR:
				RenderOverlaySR(mOverlayDecode + x1f*4, x1f - xl, x2f - x1f);
				break;

			case kOvMode_HR:
				RenderOverlayHR(mOverlayDecode + x1f*4, x1f - xl, x2f - x1f);
				break;

			case kOvMode_80Text:
				RenderOverlay80Text(mOverlayDecode + x1f*4, xl, x1f - xl, x2f - x1f);
				break;
		}
	}

	int x1h = x1;
	int x2h = x2;

	if (x1h < xl)
		x1h = xl;

	if (x2h > xr)
		x2h = xr;

	x1h += x1h;
	x2h += x2h;

	const uint8 *dec = &mOverlayDecode[x1h*2 + hscroll];
	uint32 *dst = mpDst + x1h * 2;
	const AttrPixel *apx = &mAttrPixels[x1h];
	const uint8 *prisrc = &mOvPriDecode[x1h];
	if (mbOvTrans) {
		if (mOvMode == kOvMode_80Text) {
			const uint8 *ovpri = &mOvTextTrans[x1h * 2 + hscroll];

			if (mbOvTrans15) {
				for(int xh = x1h; xh < x2h; ++xh) {
					const uint8 pri = *prisrc++;

					if (!pri) {
						uint8 v0 = dec[0];
						uint8 v1 = dec[1];

						if (ovpri[0] && (v0 & 15) != 15)
							dst[0] = mPalette[(apx[0].mCtrl >> 4) & 3][v0];

						if (ovpri[1] && (v1 & 15) != 15)
							dst[1] = mPalette[(apx[0].mCtrl >> 4) & 3][v1];
					}

					dec += 2;
					++apx;
					dst += 2;
					ovpri += 2;
				}
			} else {
				for(int xh = x1h; xh < x2h; ++xh) {
					const uint8 pri = *prisrc++;

					if (!pri) {
						uint8 v0 = dec[0];
						uint8 v1 = dec[1];

						if (ovpri[0])
							dst[0] = mPalette[(apx[0].mCtrl >> 4) & 3][v0];

						if (ovpri[1])
							dst[1] = mPalette[(apx[0].mCtrl >> 4) & 3][v1];
					}

					dec += 2;
					++apx;
					dst += 2;
					ovpri += 2;
				}
			}
		} else {
			if (mbOvTrans15) {
				for(int xh = x1h; xh < x2h; ++xh) {
					const uint8 pri = *prisrc++;

					if (!pri) {
						uint8 v0 = dec[0];
						uint8 v1 = dec[1];

						if (v0 && (v0 & 15) != 15)
							dst[0] = mPalette[(apx[0].mCtrl >> 4) & 3][v0];

						if (v1 && (v1 & 15) != 15)
							dst[1] = mPalette[(apx[0].mCtrl >> 4) & 3][v1];
					}

					dec += 2;
					++apx;
					dst += 2;
				}
			} else {
				for(int xh = x1h; xh < x2h; ++xh) {
					const uint8 pri = *prisrc++;

					if (!pri) {
						uint8 v0 = dec[0];
						uint8 v1 = dec[1];

						if (v0)
							dst[0] = mPalette[(apx[0].mCtrl >> 4) & 3][v0];

						if (v1)
							dst[1] = mPalette[(apx[0].mCtrl >> 4) & 3][v1];
					}

					dec += 2;
					++apx;
					dst += 2;
				}
			}
		}
	} else {
		for(int xh = x1h; xh < x2h; ++xh) {
			const uint8 pri = *prisrc++;

			if (!pri) {
				uint8 v0 = dec[0];
				uint8 v1 = dec[1];

				dst[0] = mPalette[(apx[0].mCtrl >> 4) & 3][v0];
				dst[1] = mPalette[(apx[0].mCtrl >> 4) & 3][v1];
			}

			dec += 2;
			++apx;
			dst += 2;
		}
	}
}

void ATVBXEEmulator::RenderOverlayLR(uint8 *dst, int x1, int w) {
	do {
		uint8 px = VBXE_FETCH(mOvAddr + x1);

		dst[0] = dst[1] = dst[2] = dst[3] = px;
		dst += 4;
		++x1;
	} while(--w);
}

void ATVBXEEmulator::RenderOverlaySR(uint8 *dst, int x1, int w) {
	x1 += x1;

	do {
		uint8 b0 = VBXE_FETCH(mOvAddr + x1);
		uint8 b1 = VBXE_FETCH(mOvAddr + x1 + 1);

		dst[0] = dst[1] = b0;
		dst[2] = dst[3] = b1;
		dst += 4;
		x1 += 2;
	} while(--w);
}

void ATVBXEEmulator::RenderOverlayHR(uint8 *dst, int x1, int w) {
	x1 += x1;

	do {
		uint8 b0 = VBXE_FETCH(mOvAddr + x1);
		uint8 b1 = VBXE_FETCH(mOvAddr + x1 + 1);

		dst[0] = b0 >> 4;
		dst[1] = b0 & 15;
		dst[2] = b1 >> 4;
		dst[3] = b1 & 15;
		dst += 4;
		x1 += 2;
	} while(--w);
}

void ATVBXEEmulator::RenderOverlay80Text(uint8 *dst, int rx1, int x1, int w) {
	static const uint32 kExpand4[16]={
		0x00000000,
		0xFF000000,
		0x00FF0000,
		0xFFFF0000,
		0x0000FF00,
		0xFF00FF00,
		0x00FFFF00,
		0xFFFFFF00,
		0x000000FF,
		0xFF0000FF,
		0x00FF00FF,
		0xFFFF00FF,
		0x0000FFFF,
		0xFF00FFFF,
		0x00FFFFFF,
		0xFFFFFFFF,
	};

	x1 += x1;

	// Character sets are always aligned on a 2K boundary (11 bits), so the character
	// data fetch never wraps around the memory base.
	const uint8 *chbase = &mpMemory[mChAddr + mOvTextRow];

	if (mbOvTrans) {
		uint8 *transDst = &mOvTextTrans[rx1*4];

		do {
			const uint32 fetchAddr = (mOvAddr + ((x1 >> 1) & ~1)) & 0x7FFFF;
			uint8 ch = VBXE_FETCH_NOWRAP(fetchAddr);
			uint8 attr = VBXE_FETCH_NOWRAP(fetchAddr + 1);
			uint8 data = chbase[(uint32)ch << 3];

			uint32 baseColor = (uint32)(attr & 0x7f) * 0x01010101;
			uint32 mask = kExpand4[x1 & 2 ? data & 15 : data >> 4];
			uint32 result;

			if (attr & 0x80) {
				result = (~mask & 0x80808080) + baseColor;
				*(uint32 *)transDst = 0xFFFFFFFF;
			} else {
				result = mask & baseColor;
				*(uint32 *)transDst = mask;
			}

			*(uint32 *)dst = result;

			dst += 4;
			transDst += 4;
			x1 += 2;
		} while(--w);
	} else {
		do {
			const uint32 fetchAddr = (mOvAddr + ((x1 >> 1) & ~1)) & 0x7FFFF;
			uint8 ch = VBXE_FETCH_NOWRAP(fetchAddr);
			uint8 attr = VBXE_FETCH_NOWRAP(fetchAddr + 1);
			uint8 data = chbase[(uint32)ch << 3];

			uint32 baseColor = (uint32)(attr & 0x7f) * 0x01010101;
			uint32 mask = kExpand4[x1 & 2 ? data & 15 : data >> 4];
			uint32 result;

			if (attr & 0x80)
				result = (~mask & 0x80808080) + baseColor;
			else
				result = (mask & baseColor) + (~mask & 0x80808080);

			*(uint32 *)dst = result;

			dst += 4;
			x1 += 2;
		} while(--w);
	}
}

void ATVBXEEmulator::RunBlitter() {
	if (!mbBlitterEnabled) {
		mBlitCyclesLeft = 0;
		return;
	}

	while(mBlitCyclesLeft > 0) {
		if (!mbBlitterActive) {
			if (!mbBlitterContinue) {
				mbBlitterListActive = false;
				mbBlitterEnabled = false;

				// raise blitter complete interrupt
				if (!mbIRQRequest) {
					mbIRQRequest = true;

					if (mbIRQEnabled)
						mpConn->VBXEAssertIRQ();
				}
				return;
			}

			LoadBlitter();
			
			if (mBlitCyclesLeft <= 0)
				break;
		}

		// process one row
		uint32 srcRowAddr = mBlitSrcAddr;
		uint32 dstRowAddr = mBlitDstAddr;
		uint32 patWidth = mBlitPatternMode & 0x80 ? mBlitPatternMode - 0x7F : 0xfffff;
		uint32 patCounter = patWidth;
		uint32 dstStepXZoomed = mBlitDstStepX * mBlitZoomX;

		switch(mBlitterMode) {
			case 0:
			default:
				if (mBlitZoomX == 1 && !(mBlitPatternMode & 0x80)) {
					if (mBlitAndMask == 0) {
						for(uint32 x=0; x<mBlitWidth; ++x) {
							VBXE_WRITE(dstRowAddr, mBlitXorMask);
							dstRowAddr += mBlitDstStepX;
						}

						srcRowAddr += mBlitSrcStepX * mBlitWidth;
					} else {
						for(uint32 x=0; x<mBlitWidth; ++x) {
							uint8 c = VBXE_FETCH(srcRowAddr);

							c &= mBlitAndMask;
							c ^= mBlitXorMask;

							VBXE_WRITE(dstRowAddr, c);
							dstRowAddr += mBlitDstStepX;

							srcRowAddr += mBlitSrcStepX;
						}
					}
				} else {
					for(uint32 x=0; x<mBlitWidth; ++x) {
						uint8 c = VBXE_FETCH(srcRowAddr);

						c &= mBlitAndMask;
						c ^= mBlitXorMask;

						for(uint8 i=0; i<mBlitZoomX; ++i) {
							VBXE_WRITE(dstRowAddr, c);
							dstRowAddr += mBlitDstStepX;
						}

						srcRowAddr += mBlitSrcStepX;

						if (!--patCounter) {
							patCounter = patWidth;
							srcRowAddr = mBlitSrcAddr;
						}
					}
				}
				break;

			case 1:
				for(uint32 x=0; x<mBlitWidth; ++x) {
					uint8 c = VBXE_FETCH(srcRowAddr);

					c &= mBlitAndMask;
					c ^= mBlitXorMask;

					if (c) {
						for(uint8 i=0; i<mBlitZoomX; ++i) {
							uint8 d = VBXE_FETCH(dstRowAddr);

							if ((1 << (d >> 5)) & mBlitCollisionMask)
								mBlitCollisionCode = d;

							VBXE_WRITE(dstRowAddr, c);
							dstRowAddr += mBlitDstStepX;
						}
					} else {
						dstRowAddr += dstStepXZoomed;
					}

					srcRowAddr += mBlitSrcStepX;

					if (!--patCounter) {
						patCounter = patWidth;
						srcRowAddr = mBlitSrcAddr;
					}
				}
				break;

			case 2:
				for(uint32 x=0; x<mBlitWidth; ++x) {
					uint8 c = VBXE_FETCH(srcRowAddr);

					c &= mBlitAndMask;
					c ^= mBlitXorMask;

					if (c) {
						for(uint8 i=0; i<mBlitZoomX; ++i) {
							uint8 d = VBXE_FETCH(dstRowAddr);

							if ((1 << (d >> 5)) & mBlitCollisionMask)
								mBlitCollisionCode = d;

							VBXE_WRITE(dstRowAddr, c + d);
							dstRowAddr += mBlitDstStepX;
						}
					} else {
						dstRowAddr += dstStepXZoomed;
					}

					srcRowAddr += mBlitSrcStepX;

					if (!--patCounter) {
						patCounter = patWidth;
						srcRowAddr = mBlitSrcAddr;
					}

				}
				break;

			case 3:
				for(uint32 x=0; x<mBlitWidth; ++x) {
					uint8 c = VBXE_FETCH(srcRowAddr);

					c &= mBlitAndMask;
					c ^= mBlitXorMask;

					if (c) {
						for(uint8 i=0; i<mBlitZoomX; ++i) {
							uint8 d = VBXE_FETCH(dstRowAddr);

							if ((1 << (d >> 5)) & mBlitCollisionMask)
								mBlitCollisionCode = d;

							VBXE_WRITE(dstRowAddr, c | d);
							dstRowAddr += mBlitDstStepX;
						}
					} else {
						dstRowAddr += dstStepXZoomed;
					}

					srcRowAddr += mBlitSrcStepX;

					if (!--patCounter) {
						patCounter = patWidth;
						srcRowAddr = mBlitSrcAddr;
					}
				}
				break;

			case 4:
				for(uint32 x=0; x<mBlitWidth; ++x) {
					uint8 c = VBXE_FETCH(srcRowAddr);

					c &= mBlitAndMask;
					c ^= mBlitXorMask;

					if (c) {
						for(uint8 i=0; i<mBlitZoomX; ++i) {
							uint8 d = VBXE_FETCH(dstRowAddr);

							if ((1 << (d >> 5)) & mBlitCollisionMask)
								mBlitCollisionCode = d;

							VBXE_WRITE(dstRowAddr, c & d);
							dstRowAddr += mBlitDstStepX;
						}
					} else {
						dstRowAddr += dstStepXZoomed;
					}

					srcRowAddr += mBlitSrcStepX;

					if (!--patCounter) {
						patCounter = patWidth;
						srcRowAddr = mBlitSrcAddr;
					}
				}
				break;

			case 5:
				for(uint32 x=0; x<mBlitWidth; ++x) {
					uint8 c = VBXE_FETCH(srcRowAddr);

					c &= mBlitAndMask;
					c ^= mBlitXorMask;

					if (c) {
						for(uint8 i=0; i<mBlitZoomX; ++i) {
							uint8 d = VBXE_FETCH(dstRowAddr);

							if ((1 << (d >> 5)) & mBlitCollisionMask)
								mBlitCollisionCode = d;

							VBXE_WRITE(dstRowAddr, c ^ d);
							dstRowAddr += mBlitDstStepX;
						}
					} else {
						dstRowAddr += dstStepXZoomed;
					}

					srcRowAddr += mBlitSrcStepX;

					if (!--patCounter) {
						patCounter = patWidth;
						srcRowAddr = mBlitSrcAddr;
					}
				}
				break;

			case 6:
				for(uint32 x=0; x<mBlitWidth; ++x) {
					uint8 c = VBXE_FETCH(srcRowAddr);

					c &= mBlitAndMask;
					c ^= mBlitXorMask;

					if (c) {
						for(uint8 i=0; i<mBlitZoomX; ++i) {
							uint8 d = VBXE_FETCH(dstRowAddr);

							if (c & 0x0f) {
								if ((1 << ((d >> 1) & 7)) & mBlitCollisionMask)
									mBlitCollisionCode = d;
							}

							if (c & 0xf0) {
								if ((1 << ((d >> 5) & 7)) & mBlitCollisionMask)
									mBlitCollisionCode = d;
							}

							VBXE_WRITE(dstRowAddr, c);
							dstRowAddr += mBlitDstStepX;
						}
					} else {
						dstRowAddr += dstStepXZoomed;
					}

					srcRowAddr += mBlitSrcStepX;

					if (!--patCounter) {
						patCounter = patWidth;
						srcRowAddr = mBlitSrcAddr;
					}
				}
				break;
		}


		mBlitDstAddr += mBlitDstStepY;

		if (++mBlitZoomCounterY >= mBlitZoomY) {
			mBlitZoomCounterY = 0;
			mBlitSrcAddr += mBlitSrcStepY;

			if (!--mBlitHeight) {
				mbBlitterActive = false;
				mbBlitterListActive = true;
			}
		}

		// Deduct cycles.
		mBlitCyclesLeft -= mBlitCyclesPerRow;
	}
}

void ATVBXEEmulator::LoadBlitter() {
	if (mbBlitLogging) {
		ATConsoleTaggedPrintf("VBXE: Starting new blit at $%05X:\n", mBlitListFetchAddr);

		DumpBlitListEntry(mBlitListFetchAddr);
	}

	uint8 rawSrcAddr0 = VBXE_FETCH(mBlitListFetchAddr + 0);
	uint8 rawSrcAddr1 = VBXE_FETCH(mBlitListFetchAddr + 1);
	uint8 rawSrcAddr2 = VBXE_FETCH(mBlitListFetchAddr + 2);
	uint8 rawSrcStepY0 = VBXE_FETCH(mBlitListFetchAddr + 3);
	uint8 rawSrcStepY1 = VBXE_FETCH(mBlitListFetchAddr + 4);
	uint8 rawSrcStepX = VBXE_FETCH(mBlitListFetchAddr + 5);
	uint8 rawDstAddr0 = VBXE_FETCH(mBlitListFetchAddr + 6);
	uint8 rawDstAddr1 = VBXE_FETCH(mBlitListFetchAddr + 7);
	uint8 rawDstAddr2 = VBXE_FETCH(mBlitListFetchAddr + 8);
	uint8 rawDstStepY0 = VBXE_FETCH(mBlitListFetchAddr + 9);
	uint8 rawDstStepY1 = VBXE_FETCH(mBlitListFetchAddr + 10);
	uint8 rawDstStepX = VBXE_FETCH(mBlitListFetchAddr + 11);
	uint8 rawBltWidth0 = VBXE_FETCH(mBlitListFetchAddr + 12);
	uint8 rawBltWidth1 = VBXE_FETCH(mBlitListFetchAddr + 13);
	uint8 rawBltHeight = VBXE_FETCH(mBlitListFetchAddr + 14);
	uint8 rawBltAndMask = VBXE_FETCH(mBlitListFetchAddr + 15);
	uint8 rawBltXorMask = VBXE_FETCH(mBlitListFetchAddr + 16);
	uint8 rawBltCollisionMask = VBXE_FETCH(mBlitListFetchAddr + 17);
	uint8 rawBltZoom = VBXE_FETCH(mBlitListFetchAddr + 18);
	uint8 rawPatternMode = VBXE_FETCH(mBlitListFetchAddr + 19);
	uint8 rawBltControl = VBXE_FETCH(mBlitListFetchAddr + 20);
	mBlitListFetchAddr += 21;

	mbBlitterActive = true;

	mBlitSrcAddr = (uint32)rawSrcAddr0 + ((uint32)rawSrcAddr1 << 8) + ((uint32)rawSrcAddr2 << 16);
	mBlitSrcStepX = (sint8)rawSrcStepX;
	mBlitSrcStepY = ((((uint32)rawSrcStepY0 + ((uint32)rawSrcStepY1 << 8)) & 0x1FFF) + 0xFFFFF000) ^ 0xFFFFF000;
	mBlitDstAddr = (uint32)rawDstAddr0 + ((uint32)rawDstAddr1 << 8) + ((uint32)rawDstAddr2 << 16);
	mBlitDstStepX = (sint8)rawDstStepX;
	mBlitDstStepY = ((((uint32)rawDstStepY0 + ((uint32)rawDstStepY1 << 8)) & 0x1FFF) + 0xFFFFF000) ^ 0xFFFFF000;
	mBlitWidth = (uint32)rawBltWidth0 + (uint32)((rawBltWidth1 & 0x01) << 8) + 1;
	mBlitHeight = (uint32)rawBltHeight + 1;
	mBlitAndMask = rawBltAndMask;
	mBlitXorMask = rawBltXorMask;
	mBlitCollisionMask = rawBltCollisionMask;
	mBlitPatternMode = rawPatternMode;

	mbBlitterContinue = (rawBltControl & 0x08) != 0;

	mBlitterMode = rawBltControl & 7;

	mBlitZoomX = (rawBltZoom & 7) + 1;
	mBlitZoomY = ((rawBltZoom >> 4) & 7) + 1;
	mBlitZoomCounterY = 0;

	mBlitCollisionCode = 0;

	// Deduct cycles for blit list.
	mBlitCyclesLeft -= 21;

	// Compute memory cycles per row blitted.
	uint32 dstBytesPerRow = mBlitWidth * mBlitZoomX;
	switch(mBlitterMode) {

		// Mode 0 is read-write.
		case 0:
			if (mBlitAndMask == 0)
				mBlitCyclesPerRow = dstBytesPerRow;
			else
				mBlitCyclesPerRow = dstBytesPerRow * 2;
			break;

		// Mode 1 is read-modify-write if collision detection is enabled.
		case 1:
			if (mBlitAndMask == 0)
				mBlitCyclesPerRow = dstBytesPerRow;
			else if (mBlitCollisionMask == 0)
				mBlitCyclesPerRow = dstBytesPerRow * 2;
			else
				mBlitCyclesPerRow = dstBytesPerRow * 3;
			break;

		// Modes 2-6 are always read-modify-write.
		default:
			if (mBlitAndMask == 0)
				mBlitCyclesPerRow = dstBytesPerRow * 2;
			else
				mBlitCyclesPerRow = dstBytesPerRow * 3;
			break;
	}
}

void ATVBXEEmulator::InitPriorityTables() {
	uint8 tab[32][256];

	ATInitGTIAPriorityTables(tab);

	// We need to rewrite the tables to split out playfield and player colors, since
	// the former can change so often with VBXE.
	for(int table=0; table<32; ++table) {
		const uint8 *src = tab[table];
		uint8 *dst = mPriorityTables[table][0];
		uint8 *dst2 = mPriorityTablesHi[table][0];

		for(int idx=0; idx<256; ++idx) {
			// The first value is the index in the attribute cell (0-3); the
			// second value is from the color table. PF0-PF2 must come from
			// the attribute cell in CCR modes; in hires modes only PF1 and
			// PF2 come from there since the PF0 cell is used for the PF2/PF3
			// selector instead.

			switch(src[idx]) {
				case kColorP0:
					dst[0] = 0;
					dst[1] = kColorP0;
					dst2[0] = 0;
					dst2[1] = kColorP0;
					break;
				case kColorP1:
					dst[0] = 0;
					dst[1] = kColorP1;
					dst2[0] = 0;
					dst2[1] = kColorP1;
					break;
				case kColorP2:
					dst[0] = 0;
					dst[1] = kColorP2;
					dst2[0] = 0;
					dst2[1] = kColorP2;
					break;
				case kColorP3:
					dst[0] = 0;
					dst[1] = kColorP3;
					dst2[0] = 0;
					dst2[1] = kColorP3;
					break;
				case kColorPF0:
					dst[0] = 1;
					dst[1] = kColorBlack;
					dst2[0] = 0;
					dst2[1] = kColorPF0;
					break;
				case kColorPF1:
					dst[0] = 2;
					dst[1] = kColorBlack;
					dst2[0] = 2;
					dst2[1] = kColorBlack;
					break;
				case kColorPF2:
					dst[0] = 3;
					dst[1] = kColorBlack;
					dst2[0] = 3;
					dst2[1] = kColorBlack;
					break;
				case kColorPF3:
					dst[0] = 0;
					dst[1] = kColorPF3;
					dst2[0] = 0;
					dst2[1] = kColorPF3;
					break;
				case kColorBAK:
					dst[0] = 0;
					dst[1] = kColorBAK;
					dst2[0] = 0;
					dst2[1] = kColorBAK;
					break;
				case kColorBlack:
					dst[0] = 0;
					dst[1] = kColorBlack;
					dst2[0] = 0;
					dst2[1] = kColorBlack;
					break;
				case kColorP0P1:
					dst[0] = 0;
					dst[1] = kColorP0P1;
					dst2[0] = 0;
					dst2[1] = kColorP0P1;
					break;
				case kColorP2P3:
					dst[0] = 0;
					dst[1] = kColorP2P3;
					dst2[0] = 0;
					dst2[1] = kColorP2P3;
					break;
				case kColorPF0P0:
					dst[0] = 1;
					dst[1] = kColorP0;
					dst2[0] = 0;
					dst2[1] = kColorPF0P0;
					break;
				case kColorPF0P1:
					dst[0] = 1;
					dst[1] = kColorP1;
					dst2[0] = 0;
					dst2[1] = kColorPF0P1;
					break;
				case kColorPF0P0P1:
					dst[0] = 1;
					dst[1] = kColorP0P1;
					dst2[0] = 0;
					dst2[1] = kColorPF0P0P1;
					break;
				case kColorPF1P0:
					dst[0] = 2;
					dst[1] = kColorP0;
					dst2[0] = 2;
					dst2[1] = kColorP0;
					break;
				case kColorPF1P1:
					dst[0] = 2;
					dst[1] = kColorP1;
					dst2[0] = 2;
					dst2[1] = kColorP1;
					break;
				case kColorPF1P0P1:
					dst[0] = 2;
					dst[1] = kColorP0P1;
					dst2[0] = 2;
					dst2[1] = kColorP0P1;
					break;
				case kColorPF2P2:
					dst[0] = 3;
					dst[1] = kColorP2;
					dst2[0] = 3;
					dst2[1] = kColorP2;
					break;
				case kColorPF2P3:
					dst[0] = 3;
					dst[1] = kColorP3;
					dst2[0] = 3;
					dst2[1] = kColorP3;
					break;
				case kColorPF2P2P3:
					dst[0] = 3;
					dst[1] = kColorP2P3;
					dst2[0] = 3;
					dst2[1] = kColorP2P3;
					break;
				case kColorPF3P2:
					dst[0] = 0;
					dst[1] = kColorPF3P2;
					dst2[0] = 0;
					dst2[1] = kColorPF3P2;
					break;
				case kColorPF3P3:
					dst[0] = 0;
					dst[1] = kColorPF3P3;
					dst2[0] = 0;
					dst2[1] = kColorPF3P3;
					break;
				case kColorPF3P2P3:
					dst[0] = 0;
					dst[1] = kColorPF3P2P3;
					dst2[0] = 0;
					dst2[1] = kColorPF3P2P3;
					break;
			}

			dst += 2;
			dst2 += 2;
		}
	}
}
