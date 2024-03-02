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

.proc	EditorOpen
	jsr		ScreenOpen
	mva		#$ff bufcnt
	mva		#$00 dspflg
	rts
.endp

.proc	EditorClose
	rts
.end

.proc	EditorGetByte
	lda		bufcnt
	bpl		haveLine
	jsr		EditorGetLine
	bpl		haveLine
	jsr		BugCheck
	rts
haveLine:
	#{
		uint8 rval = 0x9B;
		uint8 c = kdb.BUFCNT;
		
		VDASSERT(c < 128);
		
		kdb.BUFCNT = c - 1;
		
		ScreenSwap(true);
		if (c) {
			rval = ScreenRead();
		} else {
			// restore old cursor pos and toggle cursor back on
			ScreenWrite(0x9B);
			ScreenRecomputeCursorAddr();
			ScreenToggleCursor();
		}
		
		mpCPU->SetA(rval);
	#}
	
	ldy		#0
	rts
.endp

.proc	EditorGetLine
	;save off current logical line offset
	#{
		uint8 logcol = kdb.LOGCOL;
		
		kdb.BUFADR = logcol;
	#}
	
readLoop:
	;get a character
	jsr		KeyboardGetByte
	bpl		readOK
	rts
readOK:
	pha
	
	#{
		uint8 prevOffset = (uint8)kdb.BUFADR;
		kdb.BUFCNT = prevOffset;
	#}
	
	jsr		EditorPutByte

	lda		logcol
	cmp		#113
	sne:jsr	EditorSoundLengthAlarm	

	pla
	cmp		#$9b
	bne		readLoop
isEOL:
	#{
		ScreenSwap(true);
		
		// find start of logical line
		uint32 logMask = EditorReadLogicalLineMask();
		uint32 x1 = kdb.LMARGN;
		uint32 y1 = kdb.ROWCRS;
		
		while(y1 > 0) {
			// We need to decrement first because we're actually on the line-after.
			--y1;
			
			if (logMask & (0x800000 >> y1))
				break;
		}
		
		// advance by beginning of logical line
		uint8 offset = kdb.BUFCNT & 0xff;
		x1 = offset % 40;
		y1 += offset / 40;
			
		// record start of logical line
		kdb.BUFADR = (uint16)(x1 + (y1 << 8));
			
		// find end of logical line by finding last non-blank char
		uint32 curLen = 0;
		uint32 lastLen = 0;
		uint32 x = x1;
		uint32 y = y1;
		uint16 addr = ScreenComputeLineAddr(y);
		
		for(;;) {
			++curLen;
			
			if (mpMemory->ReadByte(addr + x))
				lastLen = curLen;
				
			if (++x >= kdb.RMARGN) {
				x = kdb.LMARGN;
				++y;
				
				if (logMask & (0x800000 >> y))
					break;
				
				addr = ScreenComputeLineAddr(y);
			}
		}
		
		VDASSERT(lastLen <= 120);
				
		// record count and current cursor pos
		kdb.BUFCNT = lastLen;
		kdb.BUFADR = kdb.COLCRS + (kdb.ROWCRS << 8);
		
		EditorRecomputeLogicalColumn();
		ScreenToggleCursor();

		// reset cursor to start of line
		kdb.COLCRS = x1;
		kdb.ROWCRS = y1;
		
		ScreenRecomputeCursorAddr();
	#}
	
	lda		#$9b
	ldy		#0
	rts
.endp

#[
static const uint32 kScreenModeBytesPerRow[16]={
	40,
	20,
	20,
	10,
	10,
	20,
	20,
	40,
	40,
	40,
	40,
	40,
	40,
	40,
	20,
	40
};

static const uint32 kScreenModeXShift[16]={
	0,
	0,
	0,
	2,
	3,
	2,
	3,
	2,
	3,
	1,
	1,
	1,
	0,
	0,
	3,
	2
};
#]

;ATASCII	Internal
;00-1F		40-5F
;20-3F		00-1F
;40-5F		20-3F
;60-7F		60-7F
;80-9F		C0-DF
;A0-BF		80-9F
;C0-DF		A0-BF
;E0-FF		E0-FF

ATASCIIToInternalTab:
	dta		$40
	dta		$20
	dta		$60
	dta		$00

.proc	EditorPutByte
	ldy		botscr
	cpy		#24
	bne		screenok
	ldy		dindex
	beq		screenok

	pha
	lda		icax1z
	pha
	lda		icax2z
	pha
	
	mva		#$0c	icax1z
	mva		#0		icax2z
	jsr		ScreenOpen
	
	pla
	sta		icax2z
	pla
	sta		icax1z
	pla
screenok:

	;check if display is suspended
suspendloop:
	ldx		ssflag
	bne		suspendloop

	#{
		ScreenSwap(true);

		uint8 c = mpCPU->GetA();
		
		// check for EOL -- this handling bypasses the control code flag
		if (c == 0x9B) {
			ScreenToggleCursor();
			
			kdb.COLCRS = kdb.LMARGN;
			uint8 y = kdb.ROWCRS + 1;
			kdb.ROWCRS = y;
			
			if (y >= kdb.BOTSCR) {
				uint8 scrolled = ScreenScroll();
				kdb.ROWCRS = kdb.BOTSCR - scrolled;
			}
			
			EditorRecomputeLogicalColumn();
			
			ScreenRecomputeCursorAddr();
			ScreenToggleCursor();
			return;
		}

		// check if display of control codes is desired
		if (!kdb.DSPFLG) {
			bool moveexit = false;
			
			switch(c) {
				case 0x1C:	// up
					if (kdb.ROWCRS)
						--kdb.ROWCRS;
					else
						kdb.ROWCRS = kdb.BOTSCR - 1;
						
					if (kdb.LOGCOL < 40)
						EditorRecomputeLogicalColumn();
					else
						kdb.LOGCOL = kdb.LOGCOL - 40;
						
					moveexit = true;
					break;
				
				case 0x1D:	// down
					if (++kdb.ROWCRS >= kdb.BOTSCR)
						kdb.ROWCRS = 0;
						
					if (EditorIsLogicalLineStart(kdb.ROWCRS))
						kdb.BUFADR = kdb.LMARGN;
						
					moveexit = true;
					break;
					
				case 0x1E:	// left
					if (kdb.COLCRS <= kdb.LMARGN) {
						kdb.LOGCOL = kdb.LOGCOL - (uint8)kdb.COLCRS + kdb.RMARGN - 1;
						kdb.COLCRS = kdb.RMARGN - 1;
					} else {
						--kdb.COLCRS;
						--kdb.LOGCOL;
					}
											
					moveexit = true;
					break;
					
				case 0x1F:	// right
					if (kdb.COLCRS >= kdb.RMARGN) {
						kdb.LOGCOL = kdb.LOGCOL - (uint8)kdb.COLCRS + kdb.LMARGN - 1;
						kdb.COLCRS = kdb.LMARGN;
					} else {
						++kdb.COLCRS;
						++kdb.LOGCOL;
					}
					
					moveexit = true;
					break;
					
				case 0x7D:	// clear
					ScreenClear();
					ScreenRecomputeCursorAddr();
					ScreenToggleCursor();
					EditorRecomputeLogicalColumn();
					return;
					
				case 0x7E:	// backspace
					if (kdb.COLCRS > kdb.LMARGN || kdb.ROWCRS) {
						ScreenToggleCursor();
						
						if (kdb.COLCRS <= kdb.LMARGN) {
							if (!EditorIsLogicalLineStart(kdb.ROWCRS)) {
								kdb.LOGCOL = kdb.LOGCOL - (uint8)kdb.COLCRS + kdb.RMARGN - 40;
								--kdb.ROWCRS;
								kdb.COLCRS = kdb.RMARGN - 1;
							}
						} else {
							--kdb.LOGCOL;
							--kdb.COLCRS;
						}
							
						EditorRecomputeLogicalColumn();
						ScreenRecomputeCursorAddr();
						mpMemory->WriteByte(kdb.OLDADR, 0);
						ScreenToggleCursor();
					}
					return;
										
				case 0x9C:	// delete line
					{
						const int index = kdb.DINDEX & 15;
						const int bpr = kScreenModeBytesPerRow[index];
						uint32 h = kdb.BOTSCR;
						const uint32 y0 = kdb.ROWCRS;
						uint32 scrolled = 0;
						
						for(;;) {
							uint32 y = y0;
							uint16 dst = kdb.SAVMSC + bpr * y;
							uint16 src = dst + bpr;
							
							while(++y < h) {							
								for(int i=0; i<bpr; ++i)
									mpMemory->WriteByte(dst++, mpMemory->ReadByte(src++));
							}
							
							for(int i=0; i<bpr; ++i)
								mpMemory->WriteByte(dst++, 0);
								
							if (index)
								break;
								
							if (++scrolled >= h)
								break;
							
							// delete bit in logical map corresponding to this line	
							uint32 logw = EditorReadLogicalLineMask();
							uint32 lineBit = (1 << (23 - y0));
							
							logw &= ~lineBit;					// clear line start bit
							logw += (logw & (lineBit - 1));		// shift remaining bits up
							++logw;								// set line start bit for bottommost line
							
							kdb.LOGMAP[0] = (uint8)(logw >> 16);
							kdb.LOGMAP[1] = (uint8)(logw >>  8);
							kdb.LOGMAP[2] = (uint8)(logw >>  0);
							
							// exit if we are at the start of another logical line
							if (logw & lineBit)
								break;
						}
						
						EditorRecomputeLogicalColumn();
						ScreenToggleCursor();
					}
					return;
					
				case 0x9D:	// insert line
					ScreenToggleCursor();
					{
						const int index = kdb.DINDEX & 15;
						const int bpr = kScreenModeBytesPerRow[index];
						int y = kdb.ROWCRS;
						int h = kdb.BOTSCR;
						uint16 base = kdb.SAVMSC;
						
						for(int i = h-1; i > y; --i) {
							uint16 dst = base + bpr * i;
							uint16 src = dst - bpr;
												
							for(int i=0; i<bpr; ++i)
								mpMemory->WriteByte(dst + i, mpMemory->ReadByte(src + i));
						}
						
						uint16 newrow = base + bpr * y;
						for(int i=0; i<bpr; ++i)
							mpMemory->WriteByte(newrow + i, 0);
					}
					EditorRecomputeLogicalColumn();
					ScreenToggleCursor();
					return;
			}
			
			if (moveexit) {
				ScreenToggleCursor();
				ScreenRecomputeCursorAddr();
				ScreenToggleCursor();
				mpCPU->SetY(0);
				return;
			}
		}
		
		static const uint8 kATASCIIToInternalTab[4]={
			0x40, 0x20, 0x60, 0x00
		};
			
		c ^= kATASCIIToInternalTab[(c >> 5) & 3];
		
		// plot character
		ScreenRecomputeCursorAddr();
		mpMemory->WriteByte(kdb.OLDADR, c);
		
		// inc pos		
		kdb.OLDADR = kdb.OLDADR + 1;
		
		int logcol = kdb.LOGCOL + 1;
		int rmargn = kdb.RMARGN;
		if (++kdb.COLCRS >= rmargn) {
			int lmargn = kdb.LMARGN;
			
			logcol -= rmargn;
			logcol += 40;
			logcol += lmargn;
						
			if (++kdb.ROWCRS >= kdb.BOTSCR) {
				uint8 scrolled = ScreenScroll();
				kdb.ROWCRS = kdb.BOTSCR - scrolled;
			}
			
			if (logcol >= 120) {
				logcol -= 120;
				kdb.LOGMAP[kdb.ROWCRS >> 3] |= (0x80 >> (kdb.ROWCRS & 7));
			} else {
				kdb.LOGMAP[kdb.ROWCRS >> 3] &= ~(0x80 >> (kdb.ROWCRS & 7));
			}

			kdb.COLCRS = kdb.LMARGN;
			ScreenRecomputeCursorAddr();
		}
		
		kdb.LOGCOL = logcol;
		
		ScreenToggleCursor();
	#}
		
	ldy		#0
	rts
.endp
	
.proc	EditorGetStatus
	rts
.endp

.proc	EditorSpecial
	rts
.endp

.proc	EditorInit
	rts
.endp

.proc	EditorSoundLengthAlarm
	ldy		#$80
	lda		#$08
soundloop:
	ldx		#15
	pha
delay:
	lda		vcount
	cmp:req	vcount
	dex
	bne		delay
	pla
	eor		#$08
	sta		consol
	dey
	bne		soundloop
	rts	
.endp

#<
void EditorRecomputeLogicalColumn() {
	uint32 mask = EditorReadLogicalLineMask();
	uint8 logcol = (uint8)kdb.COLCRS;
	uint8 row = kdb.ROWCRS;
	
	while(row > 0 && !(mask & (0x800000 >> row))) {
		logcol += 40;
		--row;
	}
	
	kdb.LOGCOL = logcol;
	kdb.BUFADR = kdb.LMARGN;
}
#>

#<
uint32 EditorReadLogicalLineMask() {
	return ((uint32)kdb.LOGMAP[0] << 16) + ((uint32)kdb.LOGMAP[1] << 8) + ((uint32)kdb.LOGMAP[0] << 0);
}
#>

#<
bool EditorIsLogicalLineStart(uint8 row) {
	return row >= kdb.BOTSCR || (kdb.LOGMAP[row >> 3] & (0x80 >> (row & 7))) != 0;
}
#>
