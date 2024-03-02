;	Altirra - Atari 800/800XL emulator
;	Kernel ROM replacement
;	Copyright (C) 2008 Avery Lee
;
;	This program is free software; you can redistribute it and/or modify
;	it under the terms of the GNU General Public License as published by
;	the Free Software Foundation; either version 2 of the License, or
;	(at your option) any later version.
;
;	This program is distributed in the hope that it will be useful,
;	but WITHOUT ANY WARRANTY; without even the implied warranty of
;	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;	GNU General Public License for more details.
;
;	You should have received a copy of the GNU General Public License
;	along with this program; if not, write to the Free Software
;	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

.proc	ScreenInit
	mva		memtop+1 ramtop
	
	mva		#0	colrsh
	mva		#$FE	drkmsk
	mwa		#0	oldadr
	
	;set XL/XE PAL/NTSC flag
	lda		pal
	ldx		#0
	cmp		#$0f
	seq:ldx	#$ff
	stx		palnts

	rts
.endp

;Display list:
;	24 blank lines (3 bytes)
;	initial mode line with LMS (3 bytes)
;	mode lines
;	LMS for modes >4 pages
;	wait VBL (3 bytes)
;
;	total is 8-10 bytes + mode lines

;Mode	Type	Res		Colors	ANTIC	Mem(unsplit)	Mem(split)
; 0		Text	40x24	1.5		2		960+32 (4)		960+32 (4)
; 1		Text	20x24	5		6		480+32 (2)		560+32 (3)
; 2		Text	20x12	5		7		240+20 (2)		360+22 (2)
; 3		Bitmap	40x24	4		8		240+32 (2)		360+32 (2)
; 4		Bitmap	80x48	2		9		480+56 (3)		560+52 (3)
; 5		Bitmap	80x48	4		A		960+56 (4)		960+52 (4)
; 6		Bitmap	160x96	2		B		1920+104 (8)	1760+92 (8)
; 7		Bitmap	160x96	4		D		3840+104 (16)	3360+92 (14)
; 8		Bitmap	320x192	1.5		F		7680+202 (32)	6560+174 (27)
; 9		Bitmap	80x192	16		F		7680+202 (32)	6560+174 (27)
; 10	Bitmap	80x192	9		F		7680+202 (32)	6560+174 (27)
; 11	Bitmap	80x192	16		F		7680+202 (32)	6560+174 (27)
; 12	Text	40x24	5		4		960+32 (4)		960+32 (4)
; 13	Text	40x12	5		5		480+20 (2)		560+24 (3)
; 14	Bitmap	160x192	2		C		3840+200 (16)	3360+172 (14)
; 15	Bitmap	160x192	4		E		7680+202 (32)	6560+172 (27)

ScreenANTICModeTable	dta		2,6,7,8,9,10,11,13,15,15,15,15,4,5,12,14
ScreenRowCount			dta		24,24,12,24,48,48,96,96,192,192,192,192,24,12,192,192
						dta		24,20,10,20,40,40,80,80,160,160,160,160,20,10,160,160
ScreenPRIORTable		dta		0,0,0,0,0,0,0,0,0,$40,$80,$c0,0,0,0,0
ScreenPageCount			dta		3,1,1,1,3,3,7,15,31,31,31,31,3,1,15,31	;-1
						dta		3,3,1,1,3,3,7,15,31,31,31,31,3,3,15,31
						
ScreenTextSplitOffset:
	dta		a(40*20)
	dta		a(20*20)
	dta		a(20*10)
	dta		a(10*20)
	dta		a(10*40)
	dta		a(20*40)
	dta		a(20*80)
	dta		a(40*80)
	dta		a(40*160)
	dta		a(40*160)
	dta		a(40*160)
	dta		a(40*160)
	dta		a(40*20)
	dta		a(40*10)
	dta		a(20*160)
	dta		a(40*160)
	
ScreenTextRows			dta		

.proc	ScreenOpen
	#{
		// shut off ANTIC playfield and instruction DMA
		kdb.DMACTL = kdb.SDMCTL & 0xDC;
		
		// read mode value and mask
		uint8 mode = kdb.ICAX2Z & 15;
		
		// allocate pages of memory at top
		bool isSplit = (kdb.ICAX1Z & 0x10) != 0 && (mode != 0);
		uint8 xmode = mode + (isSplit ? 16 : 0);
		
		static const uint8 kPageCountM1[]={
			3,1,1,1,3,3,7,15,31,31,31,31,3,1,15,31,
			3,3,1,1,3,3,7,15,31,31,31,31,3,3,15,31
		};
		
		uint16 alignMask = ((uint16)kPageCountM1[xmode] << 8) + 0xff;
		
		uint16 memtop = (uint16)kdb.RAMTOP << 8;
		uint16 baseAddr = (memtop - (alignMask + 1)) & ~(alignMask & 0xfff);
		
		// Set playfield base.
		//
		// Note that for modes that exceed 4K of playfield data (8-11, 15),
		// we need to ensure that the 4K page split occurs between two
		// scanlines.
		static const uint16 kPFBaseOffset[]={
			32,32,20,32,56,56,104,104,336,336,336,336,32,20,200,336,
			32,32,22,32,52,52, 92, 92,336,336,336,336,32,24,172,336
		};
		
		uint16 pfBase = baseAddr + kPFBaseOffset[xmode];
		kdb.SAVMSC = pfBase;
		kdb.SAVMSC = pfBase;
		
		// set row count
		static const uint8 kRowCount[]={
			24,24,12,24,48,48,96,96,192,192,192,192,24,12,192,192,
			24,20,10,20,40,40,80,80,160,160,160,160,20,10,160,160
		};
		
		const uint8 rowCount = kRowCount[xmode];
		
		// construct display list
		uint16 dldst = baseAddr;
		
		// add 24 blank lines
		mpMemory->WriteByte(dldst++, 0x70);
		mpMemory->WriteByte(dldst++, 0x70);
		mpMemory->WriteByte(dldst++, 0x70);
		
		// add initial mode line with LMS
		static const uint8 kModeByte[]={
			2,6,7,8,9,10,11,13,15,15,15,15,4,5,12,14
		};
		
		const uint8 modeByte = kModeByte[mode];
		
		mpMemory->WriteByte(dldst++, modeByte | 0x40);
		mpMemory->WriteByte(dldst++, (uint8)pfBase);
		mpMemory->WriteByte(dldst++, (uint8)(pfBase >> 8));
		
		// Add remaining mode lines.
		// Note that for modes 8-11 and 15, we need to insert an LMS instruction
		// at line 97 in order to cross the 4K boundary.
		for(uint8 i=1; i<rowCount; ++i) {
			if (i == 94) {
				mpMemory->WriteByte(dldst++, modeByte | 0x40);
				mpMemory->WriteByte(dldst++, 0);
				mpMemory->WriteByte(dldst++, (uint8)(baseAddr >> 8) + 0x10);
			} else {
				mpMemory->WriteByte(dldst++, modeByte);
			}
		}
			
		// add 4x mode 2 lines for split screen
		if (isSplit) {
			for(int i=0; i<4; ++i)
				mpMemory->WriteByte(dldst++, 0x02);
				
			static const uint16 kSplitOffset[]={
				40*20,
				20*20,
				20*10,
				10*20,
				10*40,
				20*40,
				20*80,
				40*80,
				40*160,
				40*160,
				40*160,
				40*160,
				40*20,
				40*10,
				20*160,
				40*160,
			};
			
			kdb.TXTMSC = kdb.SAVMSC + kSplitOffset[mode];
			kdb.BOTSCR = 4;
		} else {
			// This is 24 even for a full-screen graphics mode.
			kdb.BOTSCR = 24;
		}
		
		// close display lsit
		mpMemory->WriteByte(dldst++, 0x41);
		mpMemory->WriteByte(dldst++, (uint8)baseAddr);
		mpMemory->WriteByte(dldst++, (uint8)(baseAddr >> 8));
		
		// adjust user memory limit
		kdb.MEMTOP = baseAddr;
		
		// init character set
		kdb.CHBAS = 0xe0;
		kdb.CHACT = 0x02;
		kdb.COLOR0 = 0x28;
		kdb.COLOR1 = 0xca;
		kdb.COLOR2 = 0x94;
		kdb.COLOR3 = 0x46;
		kdb.COLOR4 = 0x00;
		
		// init prior
		static const uint8 kPRIORTable[16] = { 0,0,0,0,0,0,0,0,0,0x40,0x80,0xc0,0,0,0,0 };
		kdb.GPRIOR = kPRIORTable[mode];
		
		// reset cursor
		if (mode == 0)
			kdb.COLCRS = kdb.LMARGN;
		else
			kdb.COLCRS = 0;
		kdb.ROWCRS = 0;
		
		kdb.TINDEX = 0;
		kdb.TXTROW = 0;
		
		if (kdb.BOTSCR == 4)
			kdb.TXTCOL = kdb.LMARGN;
		else
			kdb.TXTCOL = 0;
			
		kdb.LOGCOL = 0;
		kdb.SWPFLG = 0;
		kdb.BUFSTR = kdb.LMARGN;

		kdb.LOGMAP[0] = 0xFF;
		kdb.LOGMAP[1] = 0xFF;
		kdb.LOGMAP[2] = 0xFF;
		
		// init display list and playfield DMA
		kdb.SDLSTL = (uint8)baseAddr;
		kdb.SDLSTH = (uint8)(baseAddr >> 8);
		kdb.SDMCTL = (kdb.SDMCTL | 0x22);
		
		// enable VBI
		kdb.NMIRES = 0x40;
		kdb.NMIEN = 0x40;
		
		// set mode index
		kdb.DINDEX = mode;
		
		// clear if requested
		if (!(kdb.ICAX1Z & 0x20)) {
			if (kdb.BOTSCR == 4) {
				ScreenSwap(true);
				ScreenClear();
				ScreenSwap(false);
			}
			
			ScreenClear();
		}
		
		ScreenShowCursor();
	#}

	;wait for one vertical blank to pass
	lda		RTCLOK+2
vblwait:
	cmp		RTCLOK+2
	beq		vblwait
	
	ldy		#0
	rts		
.endp

.proc ScreenClose
	ldy		#0
	rts
.endp

#[
	static const uint8 kScreenRowCount[2][16]={
		24,24,12,24,48,48,96,96,192,192,192,192,24,12,192,192,
		4,20,10,20,40,40,80,80,160,160,160,160,20,10,160,160
	};
#]

.proc ScreenGetByte
	#{
		uint8 c = ScreenRead();
		mpCPU->SetA(c);
	#}
	ldy		#0
	rts
.endp

#<
uint8 ScreenRead() {
	uint32 mode = kdb.DINDEX & 15;
	uint32 x = kdb.COLCRS;
	uint32 y = kdb.ROWCRS;
		
	static const uint8 kBytesPerRow[]={
		40, 20, 20, 10, 10, 20, 20, 40, 40, 40, 40, 40, 40, 40, 40, 40
	};

	static const uint8 kShiftTable[]={
		0, 0, 0, 2, 3, 2, 3, 2, 3, 1, 1, 1, 0, 0, 1, 3
	};
			
	uint16 rowAddr = kdb.SAVMSC + y * kBytesPerRow[mode];
	uint8 mask;
	int shift;
	
	// ATASCII	Internal
	// 00-1F	40-5F
	// 20-3F	00-1F
	// 40-5F	20-3F
	// 60-7F	60-7F
	// 80-9F	C0-DF
	// A0-BF	80-9F
	// C0-DF	A0-BF
	// E0-FF	E0-FF

	static const uint8 kInternalToATASCIIXorTab[4]={
		0x20, 0x60, 0x40, 0x00
	};
		
	switch(mode) {
		case 0:
		case 1:
		case 2:
			mask = 0xFF;
			rowAddr += x;
			shift = 0;
			break;
		case 12:
		case 13:
			mask = 0xFF;
			rowAddr += x;
			shift = 0;
			break;
			
		case 9:
		case 10:
		case 11:
			mask = 0x0F;
			rowAddr += x >> 1;
			shift = (x & 1) << 2;
			break;
			
		case 3:
		case 5:
		case 7:
		case 15:
			mask = 0x03;
			rowAddr += x >> 2;
			shift = (x & 3) << 1;
			break;
		
		case 4:
		case 6:
		case 8:
		case 14:
			mask = 0x01;
			rowAddr += x >> 3;
			shift = x & 7;
			break;
	}
			
	uint8 c = mpMemory->ReadByte(rowAddr);
	
	if (mode == 0)
		c ^= kInternalToATASCIIXorTab[(c >> 5) & 3];
	else if (mode == 1 || mode == 2)
		c ^= kInternalToATASCIIXorTab[(c >> 5) & 1];
	else
		c = (c >> shift) & mask;
	
	static const uint16 kPixelsPerRow[]={
		40, 20, 20, 40, 80, 80, 160, 160, 320, 80, 80, 80, 40, 40, 160, 160
	};
			
	if (mode == 0) {
		if (++kdb.COLCRS >= kdb.RMARGN) {
			kdb.COLCRS = kdb.LMARGN;
			
			if (++kdb.ROWCRS >= kdb.BOTSCR)
				kdb.ROWCRS = 0;
		}
	} else {
		if (++kdb.COLCRS >= kPixelsPerRow[mode]) {
			kdb.COLCRS = 0;
			if (++kdb.ROWCRS >= kdb.BOTSCR)
				kdb.ROWCRS = 0;
		}
	}
	
	kdb.ATACHR = c;
	
	return c;
}
#>

.proc ScreenPutByte
	#{
		ScreenSwap(false);
	
		uint8 d = mpCPU->GetA();
		ScreenWrite(d);		
	#}	
	ldy		#0
	rts
.endp

#<
void ScreenWrite(uint8 d) {
	kdb.ATACHR = d;		// needed by SDX
	
	if (d == 0x7d) {
		ScreenClear();
		return;
	}
	
	uint32 mode = kdb.DINDEX & 15;
	uint32 x = kdb.COLCRS;
	uint32 y = kdb.ROWCRS;
	
	if (d == 0x9b) {			
		if (mode == 0)
			x = kdb.LMARGN;
		else
			x = 0;
			
		if (++y >= kScreenRowCount[kdb.BOTSCR == 4][mode]) {
			--y;
			ScreenScroll();
		}
		
		kdb.ROWCRS = y;
		kdb.COLCRS = x;
		return;
	}
	
	static const uint8 kBytesPerRow[]={
		40, 20, 20, 10, 10, 20, 20, 40, 40, 40, 40, 40, 40, 40, 40, 40
	};

	static const uint8 kShiftTable[]={
		0, 0, 0, 2, 3, 2, 3, 2, 3, 1, 1, 1, 0, 0, 1, 3
	};
			
	uint16 rowAddr = kdb.SAVMSC + y * kBytesPerRow[mode];
	uint8 mask;
	int shift;
	
	// ATASCII	Internal
	// 00-1F	40-5F
	// 20-3F	00-1F
	// 40-5F	20-3F
	// 60-7F	60-7F
	// 80-9F	C0-DF
	// A0-BF	80-9F
	// C0-DF	A0-BF
	// E0-FF	E0-FF
	

	static const uint8 kATASCIIToInternalXorTab[4]={
		0x40, 0x20, 0x60, 0x00
	};
	
	switch(mode) {
		case 0:
			d ^= kATASCIIToInternalXorTab[(d >> 5) & 3];
			mask = 0xFF;
			rowAddr += x;
			shift = 0;
			break;
		case 1:
		case 2:
			d ^= kATASCIIToInternalXorTab[(d >> 5) & 3];
			mask = 0xFF;
			rowAddr += x;
			shift = 0;
			break;
		case 12:
		case 13:
			mask = 0xFF;
			rowAddr += x;
			shift = 0;
			break;
			
		case 9:
		case 10:
		case 11:
			mask = 0x0F;
			rowAddr += x >> 1;
			shift = (x & 1) << 2;
			break;
			
		case 3:
		case 5:
		case 7:
		case 15:
			mask = 0x03;
			rowAddr += x >> 2;
			shift = (x & 3) << 1;
			break;
		
		case 4:
		case 6:
		case 8:
		case 14:
			mask = 0x01;
			rowAddr += x >> 3;
			shift = x & 7;
			break;
	}
			
	uint8 c = mpMemory->ReadByte(rowAddr);
	
	d <<= shift;
	mask <<= shift;
	
	c ^= (c ^ d) & mask;
	
	mpMemory->WriteByte(rowAddr, c);
	
	static const uint16 kPixelsPerRow[]={
		40, 20, 20, 40, 80, 80, 160, 160, 320, 80, 80, 80, 40, 40, 160, 160
	};
			
	if (mode == 0) {
		if (++kdb.COLCRS >= kdb.RMARGN)
			kdb.COLCRS = kdb.LMARGN;
	} else {
		if (++kdb.COLCRS >= kPixelsPerRow[mode])
			kdb.COLCRS = 0;
	}
}
#>

#<
void ScreenClear() {
	const int mode = kdb.DINDEX & 15;
	const uint32 bytesPerRow = kScreenModeBytesPerRow[mode];
	uint32 bytes = bytesPerRow * kScreenRowCount[kdb.BOTSCR == 4][mode];
	uint16 addr = kdb.SAVMSC;
	
	if (bytes) {
		do {
			mpMemory->WriteByte(addr++, 0);
		} while(--bytes);
	}
	
	if (mode == 0) {
		kdb.COLCRS = kdb.LMARGN;
		kdb.LOGMAP[0] = 0xFF;
		kdb.LOGMAP[1] = 0xFF;
		kdb.LOGMAP[2] = 0xFF;
	} else
		kdb.COLCRS = 0;

	kdb.ROWCRS = 0;
}
#>

#<
void ScreenShowCursor() {
	if (kdb.CRSINH)
		return;

	int i = kdb.DINDEX & 15;
	uint16 addr = ScreenComputeCursorAddr();
	kdb.OLDADR = addr;
	
	mpMemory->WriteByte(addr, mpMemory->ReadByte(addr) ^ 0x80);
}
#>

#<
void ScreenHideCursor() {
	uint16 addr = kdb.OLDADR;
	
	if (addr < 0x100)
		return;
	
	mpMemory->WriteByte(addr, mpMemory->ReadByte(addr) ^ 0x80);
	kdb.OLDADR = 0;
}
#>

#<
uint16 ScreenComputeCursorAddr() {
	int i = kdb.DINDEX & 15;
	return kdb.SAVMSC + (int)kdb.ROWCRS*kScreenModeBytesPerRow[i] + (kdb.COLCRS >> kScreenModeXShift[i]);
}
#>

#<
uint16 ScreenComputeLineAddr(uint32 y) {
	int i = kdb.DINDEX & 15;
	return kdb.SAVMSC + (int)y*kScreenModeBytesPerRow[i];
}
#>

#<
uint8 ScreenScroll() {
	const int index = kdb.DINDEX & 15;
	const int bpr = kScreenModeBytesPerRow[index];
	uint16 dst = kdb.SAVMSC;
	uint16 src = dst + bpr;
	uint8 h = kdb.BOTSCR;
	uint8 scrolled = 0;
	
	for(;;) {	
		if (h > 1) {
			uint32 bytes = (h - 1) * bpr;
			
			do {
				mpMemory->WriteByte(dst++, mpMemory->ReadByte(src++));
			} while(--bytes);
		}
		
		for(int i=0; i<bpr; ++i)
			mpMemory->WriteByte(dst++, 0);
			
		if (index != 0)
			break;
			
		// scroll logical map
		uint8 l0 = kdb.LOGMAP[0];
		uint8 l1 = kdb.LOGMAP[1];
		uint8 l2 = kdb.LOGMAP[2];
		
		l0 = (l0 << 1) + (l1 >> 7);
		l1 = (l1 << 1) + (l2 >> 7);
		l2 = (l2 << 1) + 1;
		
		kdb.LOGMAP[0] = l0;
		kdb.LOGMAP[1] = l1;
		kdb.LOGMAP[2] = l2;
		
		if (++scrolled >= h)
			break;

		if (l0 & 0x80)
			break;
	}
	
	return scrolled;
}
#>

#<
void ScreenSwapModes() {
	uint8 b1;
	uint8 b2;
	uint16 w1;
	uint16 w2;
	
	b1 = kdb.ROWCRS;	b2 = kdb.TXTROW;	kdb.TXTROW = b1;	kdb.ROWCRS = b2;
	w1 = kdb.COLCRS;	w2 = kdb.TXTCOL;	kdb.TXTCOL = w1;	kdb.COLCRS = w2;
	b1 = kdb.DINDEX;	b2 = kdb.TINDEX;	kdb.TINDEX = b1;	kdb.DINDEX = b2;
	w1 = kdb.SAVMSC;	w2 = kdb.TXTMSC;	kdb.TXTMSC = w1;	kdb.SAVMSC = w2;
	
	b1 = kdb.OLDROW;	b2 = kdb.TXTOLD[0];			kdb.TXTOLD[0] = b1;		kdb.OLDROW = b2;
	w1 = kdb.OLDCOL;	w2 = kdb.TXTOLD[1].r16();	kdb.TXTOLD[1].w16(w1);	kdb.OLDCOL = w2;
	b1 = kdb.OLDCHR;	b2 = kdb.TXTOLD[3];			kdb.TXTOLD[3] = b1;		kdb.OLDCHR = b2;
	w1 = kdb.OLDADR;	w2 = kdb.TXTOLD[4].r16();	kdb.TXTOLD[4].w16(w1);	kdb.OLDADR = w2;
	
	kdb.SWPFLG = kdb.SWPFLG ? 0x00 : 0xFF;
}
#>

#<
void ScreenSwap(bool swapToText) {
	if (swapToText ? kdb.DINDEX && !kdb.SWPFLG : (kdb.SWPFLG != 0))
		ScreenSwapModes();
}
#>
