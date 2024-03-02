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
	rts
.endp

.proc	EditorClose
	rts
.endp

.proc	EditorGetByte
	jsr		KeyboardGetByte
	bpl		readOK
	rts
readOK:
	pha
	jsr		EditorPutByte
	pla
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
		
		// check if display of control codes is desired
		if (!kdb.DSPFLG) {
			bool moveexit = false;
			
			switch(c) {
				case 0x1C:	// up
					if (kdb.ROWCRS)
						--kdb.ROWCRS;
					else
						kdb.ROWCRS = kdb.BOTSCR - 1;
						
					moveexit = true;
					break;
				
				case 0x1D:	// down
					if (++kdb.ROWCRS >= kdb.BOTSCR)
						kdb.ROWCRS = 0;
						
					moveexit = true;
					break;
					
				case 0x1E:	// left
					if (kdb.COLCRS <= kdb.LMARGN)
						kdb.COLCRS = kdb.RMARGN - 1;
					else
						--kdb.COLCRS;
					
					moveexit = true;
					break;
					
				case 0x1F:	// right
					if (kdb.COLCRS >= kdb.RMARGN)
						kdb.COLCRS = kdb.LMARGN;
					else
						++kdb.COLCRS;
					
					moveexit = true;
					break;
					
				case 0x7D:	// clear
					ScreenClear();
					return;
					
				case 0x7E:	// backspace
					if (kdb.COLCRS > kdb.LMARGN) {
						ScreenToggleCursor();
						--kdb.COLCRS;
						ScreenRecomputeCursorAddr();
						mpMemory->WriteByte(kdb.OLDADR, 0);
						ScreenToggleCursor();
					}
					return;
					
				case 0x9B:	// return
					{
						ScreenToggleCursor();
						
						kdb.COLCRS = kdb.LMARGN;
						uint8 y = kdb.ROWCRS + 1;
						kdb.ROWCRS = y;
						
						if (y >= kdb.BOTSCR) {
							ScreenScroll();
							kdb.ROWCRS = kdb.BOTSCR - 1;
						}
						
						ScreenRecomputeCursorAddr();
						ScreenToggleCursor();
					}
					return;
					
				case 0x9C:	// delete line
					{
						const int index = kdb.DINDEX & 15;
						const int bpr = kScreenModeBytesPerRow[index];
						uint32 y = kdb.ROWCRS;
						uint32 h = kdb.BOTSCR;
						uint16 dst = kdb.SAVMSC + bpr * y;
						uint16 src = dst + bpr;
						
						while(++y < h) {							
							for(int i=0; i<bpr; ++i)
								mpMemory->WriteByte(dst++, mpMemory->ReadByte(src++));
						}
						
						for(int i=0; i<bpr; ++i)
							mpMemory->WriteByte(dst++, 0);
					}
					return;
					
				case 0x9D:	// insert line
					{
						const int index = kdb.DINDEX & 15;
						const int bpr = kScreenModeBytesPerRow[index];
						int y = kdb.ROWCRS;
						int h = kdb.BOTSCR;
						uint16 base = kdb.SAVMSC;
						
						for(int i = h-1; i > y; ++i) {
							uint16 dst = base + bpr * i;
							uint16 src = dst - bpr;
												
							for(int i=0; i<bpr; ++i)
								mpMemory->WriteByte(dst + i, mpMemory->ReadByte(src + i));
						}
						
						uint16 newrow = base + bpr * y;
						for(int i=0; i<bpr; ++i)
							mpMemory->WriteByte(newrow + i, 0);
					}
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
		if (++kdb.COLCRS >= kdb.RMARGN) {
			if (++kdb.ROWCRS >= kdb.BOTSCR) {
				ScreenScroll();
				kdb.ROWCRS = kdb.BOTSCR - 1;
			}
			
			kdb.COLCRS = kdb.LMARGN;
			ScreenRecomputeCursorAddr();
		}
		
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
