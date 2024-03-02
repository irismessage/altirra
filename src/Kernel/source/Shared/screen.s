;	Altirra - Atari 800/800XL/5200 emulator
;	Modular Kernel ROM - Screen Handler
;	Copyright (C) 2008-2012 Avery Lee
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

; These are the addresses produced by the normal XL/XE OS:
;
;               Normal       Split, coarse    Split, fine
; Mode       DL   PF   TX     DL   PF   TX    DL   PF   TX
;  0        9C20 9C40 9F60   9C20 9C40 9F60  9C1F 9C40 9F60
;  1        9D60 9D80 9F60   9D5E 9D80 9F60  9D5D 9D80 9F60
;  2        9E5C 9E70 9F60   9E58 9E70 9F60  9E57 9E70 9F60
;  3        9E50 9E70 9F60   9E4E 9E70 9F60  9E4D 9E70 9F60
;  4        9D48 9D80 9F60   9D4A 9D80 9F60  9D49 9D80 9F60
;  5        9B68 9BA0 9F60   9B6A 9BA0 9F60  9B69 9BA0 9F60
;  6        9778 97E0 9F60   9782 97E0 9F60  9781 97E0 9F60
;  7        8F98 9060 9F60   8FA2 9060 9F60  8FA1 9060 9F60
;  8        8036 8150 9F60   8050 8150 9F60  804F 8150 9F60
;  9        8036 8150 9F60   8036 8150 9F60  8036 8150 9F60
; 10        8036 8150 9F60   8036 8150 9F60  8036 8150 9F60
; 11        8036 8150 9F60   8036 8150 9F60  8036 8150 9F60
; 12        9B80 9BA0 9F60   9B7E 9BA0 9F60  9B7D 9BA0 9F60
; 13        9D6C 9D80 9F60   9D68 9D80 9F60  9D67 9D80 9F60
; 14        8F38 9060 9F60   8F52 9060 9F60  8F51 9060 9F60
; 15        8036 8150 9F60   8050 8150 9F60  804F 8150 9F60
;
; *DL = display list (SDLSTL/SDLSTH)
; *PF = playfield (SAVMSC)
; *TX = text window (TXTMSC)
;
; From this, we can derive a few things:
;	- The text window is always 160 ($A0) bytes below the ceiling.
;	- The playfield is always positioned to have enough room for
;	  the text window, even though this wastes a little bit of
;	  memory for modes 1, 2, 3, 4, and 13. This means that the
;	  PF address does not have to be adjusted for split mode.
;	- The display list and playfield addresses are sometimes
;	  adjusted in order to avoid crossing 1K boundaries for the
;	  display list (gr.7) and 4K boundaries for the playfield (gr.8).
;	  However, these are fixed offsets -- adjusting RAMTOP to $9F
;	  does not remove the DL padding in GR.7 and breaks GR.7/8.
;	- Fine-scrolled modes take one additional byte for the extra
;	  mode 2 line. In fact, it displays garbage that is masked by
;	  a DLI that sets COLPF1 equal to COLPF2. (!)
;
; You might ask, why bother replicating these? Well, there are a
; number of programs that rely on the layout of the default screen
; and break if the memory addressing is different, such as ForemXEP.


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

;==========================================================================
;
.proc ScreenPlayfieldSizesLo
	dta	<($10000-$03C0)			;gr.0
	dta	<($10000-$0280)			;gr.1
	dta	<($10000-$0190)			;gr.2
	dta	<($10000-$0190)			;gr.3
	dta	<($10000-$0280)			;gr.4
	dta	<($10000-$0460)			;gr.5
	dta	<($10000-$0820)			;gr.6
	dta	<($10000-$0FA0)			;gr.7
	dta	<($10000-$1EB0)			;gr.8
	dta	<($10000-$1EB0)			;gr.9
	dta	<($10000-$1EB0)			;gr.10
	dta	<($10000-$1EB0)			;gr.11
	dta	<($10000-$0460)			;gr.12
	dta	<($10000-$0280)			;gr.13
	dta	<($10000-$0FA0)			;gr.14
	dta	<($10000-$1EB0)			;gr.15
.endp

.proc ScreenPlayfieldSizesHi
	dta	>($10000-$03C0)			;gr.0
	dta	>($10000-$0280)			;gr.1
	dta	>($10000-$0190)			;gr.2
	dta	>($10000-$0190)			;gr.3
	dta	>($10000-$0280)			;gr.4
	dta	>($10000-$0460)			;gr.5
	dta	>($10000-$0820)			;gr.6
	dta	>($10000-$0FA0)			;gr.7
	dta	>($10000-$1EB0)			;gr.8
	dta	>($10000-$1EB0)			;gr.9
	dta	>($10000-$1EB0)			;gr.10
	dta	>($10000-$1EB0)			;gr.11
	dta	>($10000-$0460)			;gr.12
	dta	>($10000-$0280)			;gr.13
	dta	>($10000-$0FA0)			;gr.14
	dta	>($10000-$1EB0)			;gr.15
.endp

;==========================================================================
; ANTIC mode is in bits 0-3, PRIOR bits in 6-7.
;
.proc ScreenModeTable
	dta		$02,$06,$07,$08,$09,$0A,$0B,$0D,$0F,$4F,$8F,$CF,$04,$05,$0C,$0E
.endp

;==========================================================================
; Number of mode lines we need to add into the display list.
;
.proc ScreenHeightShifts
	dta		1
	dta		1
	dta		0
	dta		1
	dta		2
	dta		2
	dta		3
	dta		3
	dta		4
	dta		4
	dta		4
	dta		4
	dta		1
	dta		0
	dta		4
	dta		4
.endp

.proc ScreenHeights
	dta		12, 24, 48, 96, 192
.endp

ScreenHeightsSplit = ScreenWidths
;	dta		10, 20, 40, 80, 160

.proc ScreenDLSizes
	dta	256-32/2
	dta	256-32/2
	dta	256-20/2
	dta	256-32/2
	dta	256-56/2
	dta	256-56/2
	dta	256-104/2
	dta	256-200/2
	dta	256-282/2
	dta	256-282/2
	dta	256-282/2
	dta	256-282/2
	dta	256-32/2
	dta	256-20/2
	dta	256-296/2
	dta	256-282/2

	;split
	dta	256-34/2		;not possible
	dta	256-34/2
	dta	256-24/2
	dta	256-34/2
	dta	256-54/2
	dta	256-54/2
	dta	256-94/2
	dta	256-191/2
	dta	256-256/2
	dta	256-256/2		;not possible
	dta	256-256/2		;not possible
	dta	256-256/2		;not possible
	dta	256-34/2
	dta	256-24/2
	dta	256-270/2
	dta	256-256/2
.endp

.proc ScreenWidthShifts
	dta		2		;gr.0	40 bytes
	dta		1		;gr.1	20 bytes
	dta		1		;gr.2	20 bytes
	dta		0		;gr.3	10 bytes
	dta		0		;gr.4	10 bytes
	dta		1		;gr.5	20 bytes
	dta		1		;gr.6	20 bytes
	dta		2		;gr.7	40 bytes
	dta		2		;gr.8	40 bytes
	dta		2		;gr.9	40 bytes
	dta		2		;gr.10	40 bytes
	dta		2		;gr.11	40 bytes
	dta		2		;gr.12	40 bytes
	dta		2		;gr.13	40 bytes
	dta		1		;gr.14	20 bytes
	dta		2		;gr.15	40 bytes
.endp

.proc ScreenPixelWidthIds
	dta		1		;gr.0	40 pixels
	dta		0		;gr.1	20 pixels
	dta		0		;gr.2	20 pixels
	dta		1		;gr.3	40 pixels
	dta		2		;gr.4	80 pixels
	dta		2		;gr.5	80 pixels
	dta		3		;gr.6	160 pixels
	dta		3		;gr.7	160 pixels
	dta		4		;gr.8	320 pixels
	dta		2		;gr.9	80 pixels
	dta		2		;gr.10	80 pixels
	dta		2		;gr.11	80 pixels
	dta		1		;gr.12	40 pixels
	dta		1		;gr.13	40 pixels
	dta		3		;gr.14	160 pixels
	dta		3		;gr.15	160 pixels
.endp

ScreenPixelWidthsLo = ScreenWidths + 1
.proc ScreenWidths
	dta		<10
	dta		<20
	dta		<40
	dta		<80
	dta		<160
	dta		<320
.endp

.proc ScreenPixelWidthsHi
	dta		>20
	dta		>40
	dta		>80
	dta		>160
	dta		>320
.endp

.proc ScreenEncodingTab
	dta		0		;gr.0	direct bytes
	dta		0		;gr.1	direct bytes
	dta		0		;gr.2	direct bytes
	dta		2		;gr.3	two bits per pixel
	dta		3		;gr.4	one bit per pixel
	dta		2		;gr.5	two bits per pixel
	dta		3		;gr.6	one bit per pixel
	dta		2		;gr.7	two bits per pixel
	dta		3		;gr.8	one bit per pixel
	dta		1		;gr.9	four bits per pixel
	dta		1		;gr.10	four bits per pixel
	dta		1		;gr.11	four bits per pixel
	dta		0		;gr.12	direct bytes
	dta		0		;gr.13	direct bytes
	dta		3		;gr.14	one bit per pixel
	dta		2		;gr.15	two bits per pixel
.endp

.proc ScreenPixelMasks
	dta		$ff, $0f, $03, $01, $ff, $f0, $c0, $80
.endp

;==========================================================================
;==========================================================================

	;Many compilation disks rely on ScreenOpen being at this address.
	org		$f3f6

;==========================================================================
;==========================================================================

;==========================================================================
;
; Return:
;	MEMTOP = first byte used by display
;
; Errors:
;	- If there is not enough memory (MEMTOP > APPMHI), GR.0 is reopened
;	  automatically and an error is returned.
;
; Notes:
;	- Resets character base (CHBAS).
;	- Resets character attributes (CHACT).
;	- Resets playfield colors (COLOR0-COLOR4).
;	- Resets tab map, even if the mode does not have a text window.
;	- Does NOT reset P/M colors (PCOLR0-PCOLR3).
;	- Does NOT reset margins (LMARGN/RMARGN).
;
ScreenOpen = ScreenOpenGr0.use_iocb
.proc	ScreenOpenGr0
	mva		#12 icax1z
	mva		#0 icax2z
use_iocb:
	;shut off ANTIC playfield and instruction DMA
	lda		sdmctl
	and		#$dc
	sta		sdmctl
	sta		dmactl
	
	;reset cursor parameters
	ldx		#11
clear_parms:
	sta		rowcrs,x
	sta		txtrow,x
	dex
	bne		clear_parms

	;mark us as being in main screen context
	stx		swpflg	
	
	;copy mode value to dindex
	lda		icax2z
	and		#15
	sta		dindex
	
	;poke PRIOR value (saves us some time to do it now)
	tax
	lda		ScreenModeTable,x
	and		#$c0
	sta		gprior
	
	;if a GTIA mode is active or we're in mode 0, force off split mode
	bne		kill_split
	txa
	bne		not_gtia_mode_or_gr0
kill_split:
	lda		icax1z
	and		#$ef
	sta		icax1z	
not_gtia_mode_or_gr0:

	;attempt to allocate playfield memory
	lda		ramtop
	clc
	adc		ScreenPlayfieldSizesHi,x
	bcs		pf_alloc_ok

alloc_fail:
	;we ran out of memory -- attempt to reopen with gr.0 if we aren't
	;already (to prevent recursion), and exit with an error
	txa
	beq		cant_reopen_gr0
	
	jsr		ScreenOpenGr0	
cant_reopen_gr0:
	ldy		#CIOStatOutOfMemory
	rts
	
pf_alloc_ok:
	sta		savmsc+1
	lda		ScreenPlayfieldSizesLo,x
	sta		savmsc
	
	;save off the split screen and clear flags in a more convenient form
	lda		icax1z
	and		#$10
	ora		dindex
	tax
	asl
	asl
	sta		hold2
		
	;compute display list size and subtract that off too
	lda		ScreenDLSizes,x
	asl
	ldy		savmsc+1
	scs:dey
	clc
	adc		savmsc
	sta		memtop
	sta		adress
	sta		sdlstl
	scs:dey
	sty		memtop+1
	sty		adress+1
	sty		sdlsth
	
	;check if we're below APPMHI
	cpy		appmhi+1
	bcc		alloc_fail
	bne		alloc_ok
	cmp		appmhi
	bcc		alloc_fail
	
alloc_ok:
	;set up text window address (-160 from top)
	ldx		ramtop
	dex
	stx		txtmsc+1
	mva		#$60 txtmsc

	;Set row count: 24 for gr.0, 0 for full screen non-gr.0. We will
	;fix up the split case to 4 later while we are writing the display list.
	ldy		#24
	ldx		dindex
	seq:ldy	#0
	sty		botscr

	;--- construct display list
	ldy		#0
	sty		tindex
	
	;add 24 blank lines
	lda		#$70
	sta		(adress),y+
	sta		(adress),y+
	sta		(adress),y+
	
	;Add initial mode line with LMS, and check if we need to do an LMS
	;split (playfield exceeds 4K). As it turns out, this only happens if
	;we're using ANTIC mode E or F.
	lda		ScreenModeTable,x
	and		#$0f
	pha
	ldx		#0
	cmp		#$0e
	scc:ldx	#99
	stx		hold1
	ora		#$40
	ldx		#savmsc
	jsr		write_with_zp_address
	
	;add remaining mode lines
	ldx		dindex
	lda		ScreenHeightShifts,x
	tax
	lda		ScreenHeights,x
	bit		hold2
	svc:lda	ScreenHeightsSplit,x
	tax
	dex
	pla
rowloop:

	;Check if we need to do an LMS mid-way through the screen -- this happens
	;for any mode that requires 8K of memory. The split happens on byte 101.
	;Sadly, we cannot always check for line 101 because of mode 14, which has
	;192 scanlines but only takes 4K of memory.
	cpy		hold1
	bne		notsplit
	
	;Yes, it is split -- add an LMS.
	pha
	ora		#$40
	sta		(adress),y+
	lda		#0
	sta		(adress),y+
	lda		savmsc+1
	clc
	adc		#$0f
	sta		(adress),y+
	pla
	dex
notsplit:
	sta		(adress),y+
	dex
	bne		rowloop
	
	;Add 4 lines of mode 2 if split is enabled and we're not in mode 0. Note
	;that we need an LMS to do this as the text window is not contiguous.
	bit		hold2
	bvc		nosplit
	lda		dindex
	beq		nosplit
		
	;add split mode lines (and fix the botscr line count)
	mva		#$42 (adress),y+
	mva		#$60 (adress),y+
	mva		txtmsc+1 (adress),y+

	ldx		#4
	stx		botscr
	dex
	lda		#$02
splitloop:
	sta		(adress),y+
	dex
	bne		splitloop
nosplit:

	;init character set
	mva		#$02	chact
	mva		#$e0	chbas

	;enable VBI; note that we have not yet enabled display DMA
	lda		#$41					;!! - also used for JVB insn!
	sta		nmien
	
	;terminate display list with jvb
	ldx		#adress
	jsr		write_with_zp_address
	
	;init display list and playfield dma
	lda		sdmctl
	ora		#$22
	sta		sdmctl
	
	;wait for screen to establish (necessary for Timewise splash screen to render)
	ldx		rtclok+2
	cmp:req	rtclok+2
	
	;init colors -- note that we do NOT overwrite pcolr0-3!
	ldy		#4
	mva:rpl	standard_colors,y color0,y-
	
	;init tab map to every 8 columns
	ldx		#14
	lda		#1
	sta:rpl	tabmap,x-

	;reset line status
	stx		bufcnt

	;clear if requested
	jsr		try_clear

	;if there is a text window, show the cursor
	lda		botscr
	beq		no_cursor
	
	;swap to text context, if we have split-screen
	cmp		#4
	sne:jsr	EditorSwapToText

	;init cursor to left margin
	mva		lmargn colcrs
	
	;clear it if needed
	lda		swpflg
	beq		skip_split_clear
	jsr		try_clear
skip_split_clear:

	;show cursor
	jsr		EditorRecomputeAndShowCursor
no_cursor:
	;swap back to main context and exit
	jmp		EditorSwapToScreen
	
write_with_zp_address:
	sta		(adress),y+
	lda		0,x
	sta		(adress),y+
	lda		1,x
	sta		(adress),y+
	rts
	
try_clear:
	bit		hold2
	smi:jsr	ScreenClear
	rts
	
standard_colors:
	dta		$28
	dta		$ca
	dta		$94
	dta		$46
	dta		$00
.endp

ScreenClose = CIOExitSuccess

;==========================================================================
; Behavior in gr.0:
;	- Reading char advances to next position, but cursor is not moved
;	- Cursor is picked up ($A0)
;	- Wrapping from end goes to left margin on next row
;	- Cursor may be outside of horizontal margins
;	- Error 141 (cursor out of range) if out of range
;	- Cursor will wrap to out of range if at end of screen (no automatic
;	  vertical wrap)
;	- Does NOT update OLDROW/OLDCOL
;
.proc ScreenGetByte
	jsr		ScreenCheckPosition
	bmi		xit
	ldy		rowcrs
	jsr		ScreenComputeFromAddrX0
	ldy		colcrs
	lda		(frmadr),y
	
	;convert from Internal to ATASCII
	jsr		ScreenConvertSetup
	eor		int_to_atascii_tab,x
	
	;advance to next position
	jsr		ScreenAdvancePosMode0

	ldy		#1
xit:
	rts
	
int_to_atascii_tab:
	dta		$20
	dta		$60
	dta		$40
	dta		$00
.endp

;==========================================================================
; Common behavior:
;	- Output is suspended if SSFLAG is set for non-clear and non-EOL chars
;	- Clear screen ($7D) and EOL ($9B) are always handled
;	- ESCFLG and DSPFLG are ignored (they are E: features)
;
; Behavior in gr.0:
;	- Logical lines are extended
;	- Scrolling occurs if bottom is hit
;	- Control chars other than clear and EOL are NOT handled by S:
;	- ROWCRS or COLCRS out of range results in an error.
;	- COLCRS in left margin is ignored and prints within margin.
;	- COLCRS in right margin prints one char and then does EOL.
;
; Behavior in gr.1+:
;	- No cursor is displayed
;	- LMARGN/RMARGN are ignored
;	- Cursor wraps from right side to next line
;	- ROWCRS may be below split screen boundary as long as it is within the
;	  full screen size.
;
.proc ScreenPutByte
	sta		atachr
	jsr		ScreenCheckPosition
	bmi		error

	;check for screen clear
	lda		atachr
	cmp		#$7d
	bne		not_clear_2
	jsr		ScreenClear
	ldy		#1
error:
	rts
	
	;*** ENTRY POINT FROM EDITOR FOR ESC HANDLING ***
not_clear:
	jsr		ScreenCheckPosition
	bmi		error
	
not_clear_2:	
	;set old position now (used by setup code for plot pixel)
	jsr		ScreenSetLastPosition
	
	;restore char
	lda		atachr
	
	;check if we're in gr.0
	ldx		dindex
	sne:jmp	mode_0
	
	;nope, we're in a graphics mode... that makes this easier.
	;check if it's an EOL
	cmp		#$9b
	beq		graphics_eol
	
	;check for display suspend (ctrl+1) and wait until it is cleared
	ldx:rne	ssflag
	
	;convert byte from ATASCII to Internal -- this is required for gr.1
	;and gr.2 to work correctly, and harmless for other modes
	jsr		ScreenConvertATASCIIToInternal

	;fold the pixel and compute masks
	jsr		ScreenFoldColor
	pha

	;compute addressing and shift mask
	jsr		ScreenSetupPlotPixel
	
	pla
	ldy		hold2
	eor		(toadr),y
	and		bitmsk
	eor		(toadr),y
	sta		(toadr),y
	
	;advance position
	inc		colcrs
	sne:inc	colcrs+1
	ldx		dindex
	ldy		ScreenPixelWidthIds,x
	lda		ScreenPixelWidthsHi,y
	cmp		colcrs+1
	bne		graphics_no_wrap
	lda		ScreenPixelWidthsLo,y
	cmp		colcrs
	bne		graphics_no_wrap

graphics_eol:
	;move to left side and then one row down -- note that this may
	;push us into an invalid coordinate, which will result on an error
	;on the next call if not corrected
	ldy		#0
	sty		colcrs
	sty		colcrs+1
	inc		rowcrs
graphics_no_wrap:
	
	;all done
	ldy		#1
	rts
	
mode_0:
	;hide the cursor
	pha
	jsr		ScreenHideCursor
	pla

	;check for EOL, which bypasses the ESC check
	cmp		#$9b
	bne		not_eol
	
	;it's an EOL
	lda		lmargn
	sta		colcrs
	inc		rowcrs
	lda		rowcrs
	cmp		botscr
	bcc		noywrap
	
	;We've gone over -- delete logical line 0 to make room.
	;Note that we need to set ROWCRS here first because the scroll may
	;delete more than one physical line.
	ldx		botscr
	stx		rowcrs
	
	jsr		EditorDeleteLine0
noywrap:
	jsr		EditorRecomputeAndShowCursor
	ldy		#1
	rts

not_eol:
	;check for display suspend (ctrl+1) and wait until it is cleared
	ldx:rne	ssflag
	
	pha
	jsr		EditorRecomputeCursorAddr
	pla
	jsr		ScreenConvertATASCIIToInternal
	
	;plot character
	ldy		#0
	sta		(oldadr),y
	
	;inc pos
	inw		oldadr
	jsr		ScreenAdvancePosMode0
	
	;check if we've gone beyond the right margin
	bcs		nowrap
	
	;check if we're beyond the bottom of the screen
	lda		rowcrs
	cmp		botscr
	bcc		no_scroll
	
	;yes -- scroll up
	jsr		EditorDeleteLine0

	;check if we can extend the current logical line -- 3 rows max.
	jsr		check_extend
	beq		post_scroll

	;Mark the current physical line as part of the last logical line.
	;
	;NOTE: There is a subtlety here in that we may delete multiple physical
	;      lines if the top logical line is more than one line long, but we
	;      only want to add one physical line onto our current logical line.
	lda		rowcrs
	jsr		EditorGetLogicalLineInfo
	eor		#$ff
	and		logmap,y
	sta		logmap,y

	jmp		post_scroll
	
no_scroll:
	;check if we can extend the current logical line -- 3 rows max.
	jsr		check_extend
	beq		post_scroll

	;okay, here's the fun part -- we didn't scroll beyond, but we might
	;be on another logical line, in which case we need to scroll everything
	;down to extend it.
	lda		rowcrs
	jsr		EditorTestLogicalLineBit
	beq		post_scroll
	
	;yup, insert a physical line
	jsr		ScreenInsertLine

post_scroll:
	jsr		EditorRecomputeCursorAddr
nowrap:
	jsr		ScreenShowCursor
	ldy		#1
	rts
	
check_extend:
	ldx		rowcrs
	dex
	txa
	jsr		EditorPhysToLogicalRow
	clc
	adc		#3
	cmp		rowcrs
	rts
.endp

;==========================================================================
ScreenGetStatus = CIOExitSuccess

;==========================================================================
.proc ScreenSpecial
	lda		iccomz
	cmp		#$11
	sne:jmp	ScreenDrawLineFill	;draw line
	cmp		#$12
	sne:jmp	ScreenDrawLineFill	;fill
	rts
.endp

;==========================================================================
; Given a color byte, mask it off to the pertinent bits and reflect it
; throughout a byte.
;
; Entry:
;	A = color value
;
; Exit:
;	A = folded color byte
;	DMASK = right-justified bit mask
;	HOLD3 = left-justified bit mask
;
; Modified:
;	HOLD1, ADRESS
;
.proc ScreenFoldColor
	ldx		dindex
	ldy		ScreenEncodingTab,x			;0 = 8-bit, 1 = 4-bit, 2 = 2-bit, 3 = 1-bit
	mvx		ScreenPixelMasks,y dmask
	mvx		ScreenPixelMasks+4,y hold3
	dey
	bmi		fold_done
	and		dmask
	sta		hold1
	asl
	asl
	asl
	asl
	ora		hold1
	dey
	bmi		fold_done
	sta		hold1
	asl
	asl
	ora		hold1
	dey
	bmi		fold_done
	sta		hold1
	asl
	ora		hold1
fold_done:
	rts
.endp

;==========================================================================
;
; Inputs:
;	COLCRS,ROWCRS = next point
;	OLDCOL,OLDROW = previous point
;	ATACHR = color/character to use
;
; Outputs:
;	OLDCOL,OLDROW = next point
;
; The Bresenham algorithm we use (from Wikipedia):
;	dx = |x2 - x1|
;	dy = |y2 - y1|
;	e = dx - dy
;
;	loop
;		plot(x1, y1)
;		if x1 == x2 and y1 == y2 then exit
;		e2 = 2*e
;		if e2 + dy > 0 then
;			err = err - dy
;			x0 = x0 + sign(dx)
;		endif
;		if e2 < dx then
;			err = err + dx
;			y0 = y0 + sign(dy)
;		endif
;	end
;	
.proc ScreenDrawLineFill
	;;##TRACE "Drawing line (%d,%d)-(%d,%d) in mode %d" dw(oldcol) db(oldrow) dw(colcrs) db(rowcrs) db(dindex)
	
	;initialize bit mask and repeat pertinent pixel bits throughout byte
	lda		fildat
	jsr		ScreenFoldColor
	sta		hold4
	lda		atachr
	jsr		ScreenFoldColor
	sta		hold1

	jsr		ScreenSetupPlotPixel

	;compute screen pitch
	ldx		dindex
	ldy		ScreenWidthShifts,x
	lda		ScreenWidths,y
	sta		tmprow
	tax

	;compute abs(dy) and sign(dy)
	lda		rowcrs
	sub		oldrow
	bcs		going_down
	eor		#$ff					;take abs(dy)
	adc		#1						;
	pha
	txa
	eor		#$ff
	tax
	inx
	pla
going_down:
	stx		rowinc
	sta		deltar
	ldy		#0
	txa
	spl:ldy	#$ff
	sty		rowinc+1
	
	;;##TRACE "dy = %d" db(deltar)
	
	;compute abs(dx) and sign(dx)
	ldx		#4
	lda		colcrs
	sub		oldcol
	sta		deltac
	lda		colcrs+1
	sbc		oldcol+1
	bcs		going_right
	eor		#$ff
	tay
	lda		deltac
	eor		#$ff
	adc		#1
	sta		deltac
	tya
	ldx		#0
going_right:
	sta		deltac+1

	;;##TRACE "dx = %d" dw(deltac)
	
	;set up x shift routine
	txa
	ldx		dindex
	ora		ScreenEncodingTab,x
	tay
	mva		shift_lo_tab,y oldcol
	mva		#>left_shift_8 oldcol+1
	ldy		ScreenEncodingTab,x
	mva		fill_lo_tab,y tmpcol
	mva		#>fill_right_8 tmpcol+1
	
	;compute max(dx, dy)
	lda		deltac
	ldy		deltac+1
	bne		dy_larger
	cmp		deltar
	bcs		dy_larger
	ldy		#0
	lda		deltar
dy_larger:
	sta		countr
	sty		countr+1
	
	;;##TRACE "Pixel count = %d" dw(countr)
	;check if we actually have anything to do
	ora		countr+1
	bne		count_nonzero
	
	;whoops, line is zero length... we're done
	jmp		done
	
count_nonzero:
	lda		countr
	sne:dec	countr+1

	;compute initial error accumulator in adress (dx-dy)
	lda		deltac
	sub		deltar
	sta		adress
	lda		deltac+1
	sbc		#0
	sta		adress+1
	
	;------- pixel loop state -------
	;	toadr		current row address
	;	dmask		right-justified bit mask
	;	hold3		left-justified bit mask
	;	bitmsk		current bit mask
	;	hold2		current byte offset within row
	;	colcrs		y address increment/decrement (note different from Atari OS)
	;	oldcol		left/right shift routine
	;	tmprow		screen pitch, in bytes
	;	tmpcol		right shift routine
pixel_loop:
	;compute 2*err -> frmadr
	;;##TRACE "Error accum = %d (dx=%d, dy=%d(%d))" dsw(adress) dw(deltac) db(deltar) dsw(rowinc)
	lda		adress
	asl
	sta		frmadr
	lda		adress+1
	rol
	sta		frmadr+1
	
	;check for y increment (2*e < dx, or frmadr < deltac)
	bmi		do_yinc
	lda		frmadr
	cmp		deltac
	lda		frmadr+1
	sbc		deltac+1
	bcs		no_yinc

do_yinc:
	;bump y (add/subtract pitch, e += dx)
	lda		rowinc
	clc
	adc		toadr
	sta		toadr
	lda		rowinc+1
	adc		toadr+1
	sta		toadr+1
	
	adw		adress deltac adress
no_yinc:

	;check for x increment (2*e + dy > 0, or frmadr + deltar > 0)
	lda		frmadr
	clc
	adc		deltar
	tax
	lda		frmadr+1
	adc		#0
	bmi		no_xinc
	bne		do_xinc
	txa
	beq		no_xinc
do_xinc:
	;update error accumulator
	lda		adress
	sub		deltar
	sta		adress
	scs:dec	adress+1

	;bump x
	lda		bitmsk
	jmp		(oldcol)
post_xinc:
	sta		bitmsk
no_xinc:

	;plot pixel
	;;##TRACE "Plotting at $%04X+%d with mask $%02X" dw(toadr) db(hold2) db(bitmsk)	
	ldy		hold2
	lda		hold1
	eor		(toadr),y
	and		bitmsk
	eor		(toadr),y
	sta		(toadr),y

	;do fill if needed
	lda		iccomz
	cmp		#$12
	beq		do_fill
	
next_pixel:
	;loop back for next pixel
	dec		countr
	bne		pixel_loop
	dec		countr+1
	bpl		pixel_loop
	
done:
	jsr		ScreenSetLastPosition
	ldy		#1
	rts
	
do_fill:
	ldy		hold2				;load current byte offset
	ldx		bitmsk				;save current bitmask
	bne		fill_start
fill_loop:
	lda		(toadr),y			;load screen byte
	bit		bitmsk				;mask to current pixel
	bne		fill_done			;exit loop if non-zero
	eor		hold4				;XOR with fill color
	and		bitmsk				;mask change bits to current pixel
	eor		(toadr),y			;merge with screen byte
	sta		(toadr),y			;save screen byte
fill_start:
	lda		bitmsk
	jmp		(tmpcol)
	
	.pages 1
fill_right_4:
	lsr
	lsr
fill_right_2:
	lsr
fill_right_1:
	lsr
	bcc		fill_right_ok
	lda		hold3
fill_right_8:
	iny
	cpy		tmprow
	scc:ldy	#0
fill_right_ok:
	sta		bitmsk
	bne		fill_loop
fill_done:
	stx		bitmsk
	jmp		next_pixel
	.endpg
	
fill_lo_tab:
	dta		<fill_right_8
	dta		<fill_right_4
	dta		<fill_right_2
	dta		<fill_right_1
	
shift_lo_tab:
	dta		<left_shift_8	
	dta		<left_shift_4	
	dta		<left_shift_2	
	dta		<left_shift_1
	dta		<right_shift_8	
	dta		<right_shift_4	
	dta		<right_shift_2	
	dta		<right_shift_1	
	
	.pages 1
left_shift_4:
	asl
	asl
left_shift_2:
	asl
left_shift_1:
	asl
	bcc		left_shift_ok
left_shift_8:
	dec		hold2
	lda		dmask
left_shift_ok:
	jmp		post_xinc
	
right_shift_4:
	lsr
	lsr
right_shift_2:
	lsr
right_shift_1:
	lsr
	bcc		right_shift_ok
right_shift_8:
	inc		hold2
	lda		hold3
right_shift_ok:
	jmp		post_xinc
	.endpg
.endp

;==========================================================================
.proc ScreenClear
	;first, set up for clearing the split-screen window (4*40 bytes)
	mvy		#<160 adress
	lda		#>160
	
	;check if we are in the split screen text window
	ldx		swpflg
	bne		not_main_screen
	
	;nope, it's the main screen... compute size
	ldx		dindex
	jsr		ScreenComputeMainSize	
not_main_screen:

	;clear memory
	tax
	clc
	adc		savmsc+1
	sta		toadr+1
	mva		savmsc toadr
	
	lda		#0
	ldy		adress
	beq		loop_start
loop:
	dey
	sta		(toadr),y
	bne		loop
loop_start:
	dec		toadr+1
	dex
	bpl		loop

	;reset coordinates and cursor (we're going to wipe the cursor)
	sta		colcrs+1
	sta		rowcrs
	sta		oldadr+1
	
	;reset logical line map and text window parameters if appropriate
	ldx		dindex
	bne		is_graphic_screen
	jsr		ScreenResetLogicalLineMap
	lda		lmargn

is_graphic_screen:
	sta		colcrs
	rts
.endp

;==========================================================================
; Insert a physical line at the current location.
;
; Entry:
;	ROWCRS = row before which to insert new line
;	C = 0 if physical line only, C = 1 if should start new logical line
;
; Modified:
;	HOLD1
;
.proc ScreenInsertLine
	;save new logline flag
	php
	
	;compute addresses
	ldy		botscr
	dey
	jsr		ScreenComputeFromAddrX0
	
	ldy		botscr
	jsr		ScreenComputeToAddrX0
	
	;copy lines
	ldx		botscr
	bne		line_loop_start
line_loop:
	ldy		#39
char_loop:
	lda		(frmadr),y
	sta		(toadr),y
	dey
	bpl		char_loop
line_loop_start:	
	lda		frmadr
	sta		toadr
	sec
	sbc		#40
	sta		frmadr
	lda		frmadr+1
	sta		toadr+1
	sbc		#0
	sta		frmadr+1
	
	dex
	cpx		rowcrs
	bne		line_loop
	
no_copy:
	;clear the current line
	ldy		#39
	lda		#0
clear_loop:
	sta		(toadr),y
	dey
	bpl		clear_loop
	
	;insert bit into logical line mask
	lda		rowcrs
	jsr		EditorGetLogicalLineInfo
	
	plp
	lda		#0
	scc:lda	ReversedBitMasks,x
	sta		hold1
	
	lda		#0
	sec
	sbc		ReversedBitMasks,x		;-bit
	asl
	and		logmap,y
	clc
	adc		logmap,y
	ora		hold1					;set logical line bit if needed
	ror
	sta		logmap,y
	
	dey
	spl:ror	logmap+1
	dey
	spl:ror	logmap+2
	rts
.endp

;==========================================================================
.proc ScreenHideCursor
	;check if we had a cursor (note that we CANNOT use CRSINH for this as
	;it can be changed by app code!)
	lda		oldadr+1
	beq		no_cursor
	
	;erase the cursor
	ldy		#0
	lda		oldchr
	sta		(oldadr),y
	sty		oldadr+1
no_cursor:
	rts
.endp

;==========================================================================
.proc ScreenShowCursor
	;;##ASSERT dw(oldadr) >= dw(savmsc)
	;check if the cursor is enabled
	ldy		crsinh
	bne		cursor_inhibited
	lda		(oldadr),y
	sta		oldchr
	eor		#$80
	sta		(oldadr),y
	rts
	
cursor_inhibited:
	;mark no cursor
	ldy		#0
	sty		oldadr+1
	rts
.endp

;==========================================================================
.proc	ScreenCheckPosition
	;Check for ROWCRS out of range. Note that for split screen modes we still
	;check against the full height!
	lda		rowcrs
	ldx		dindex
	bne		rowcheck_not_gr0
	cmp		botscr
	bcc		rowcheck_pass	
invalid_position:
	ldy		#CIOStatCursorRange
	rts
	
rowcheck_not_gr0:
	ldy		ScreenHeightShifts,x
	cmp		ScreenHeights,y
	beq		rowcheck_pass
	bcs		invalid_position		
rowcheck_pass:

	;check width
	ldy		ScreenPixelWidthIds,x
	lda		ScreenPixelWidthsHi,y
	cmp		colcrs+1
	bcc		invalid_position
	bne		valid_position
	lda		ScreenPixelWidthsLo,y
	cmp		colcrs
	bcc		invalid_position
valid_position:
	ldy		#1
	rts
.endp

;==========================================================================
; Compute the size of the main screen, in bytes
;
; Inputs:
;	X = screen mode (0-15)
;
; Outputs:
;	ADRESS = low byte of size
;	A = high byte of size
;
; Modified:
;	ADRESS
;	
.proc ScreenComputeMainSize
	lda		#120
	sta		adress
	lda		ScreenWidthShifts,x
	clc
	adc		ScreenHeightShifts,x
	tax
	lda		#0
loop:
	asl		adress
	rol
	dex
	bne		loop
done:
	rts
.endp

;==========================================================================
; Swap between the main screen and the split screen.
;
; Conventionally, the main screen is left as the selected context when
; the display handler is not active.
;
; Inputs:
;	C = 0	for main screen
;	C = 1	for split screen
;
; Modified:
;	X
;
; Preserved:
;	A
;	
.proc ScreenSwap
	;check if the correct set is in place
	pha
	lda		#0
	adc		swpflg
	beq		already_there
	
	;Nope, we need to swap. Conveniently, the data to be swapped
	;is in a 12 byte block:
	;
	;	ROWCRS ($0054)		TXTROW ($0290)
	;	COLCRS ($0055)		TXTCOL ($0291)
	;	DINDEX ($0057)		TINDEX ($0293)
	;	SAVMSC ($0058)		TXTMSC ($0294)
	;	OLDROW ($005A)		TXTOLD ($0296)
	;	OLDCOL ($005B)		TXTOLD ($0297)
	;	OLDCHR ($005D)		TXTOLD ($0299)
	;	OLDADR ($005E)		TXTOLD ($029A)
	
	ldx		#11
swap_loop:
	lda		rowcrs,x
	ldy		txtrow,x
	sty		rowcrs,x
	sta		txtrow,x
	dex
	bpl		swap_loop
	
	;invert swap flag
	lda		swpflg
	eor		#$ff
	sta		swpflg
	
already_there:
	pla
	rts
.endp

;==========================================================================
; Compute character address.
;
; Entry:
;	X = byte index
;	Y = line index
;
; Exit:
;	A:X = address
;
; Used:
;	ADRESS
;
.proc	ScreenComputeAddr
	jsr		ScreenComputeRangeSize
	sta		adress
	txa
	clc
	adc		adress			;row*10,20,40+col
	scc:inc	adress+1
	clc
	adc		savmsc
	tax
	lda		adress+1
	adc		savmsc+1
	rts
.endp

;==========================================================================
ScreenComputeFromAddr = ScreenComputeFromAddrX0.with_x
.proc	ScreenComputeFromAddrX0
	ldx		#0
with_x:
	jsr		ScreenComputeAddr
	stx		frmadr
	sta		frmadr+1
	rts
.endp

;==========================================================================
ScreenComputeToAddr = ScreenComputeToAddrX0.with_x
.proc	ScreenComputeToAddrX0
	ldx		#0
with_x:
	jsr		ScreenComputeAddr
	stx		toadr
	sta		toadr+1
	rts
.endp

;==========================================================================
; Compute size, in bytes, of a series of lines.
;
; Entry:
;	Y = line count
;
; Exit:
;	ADRESS+1	High byte of size
;	A			Low byte of size
;	
; Preserved:
;	X
;
; Modified:
;	Y
;
.proc	ScreenComputeRangeSize
	mva		#0 adress+1
	sty		adress
	ldy		dindex
	lda		ScreenWidthShifts,y
	tay
	lda		adress
	asl
	rol		adress+1		;row*2
	asl
	rol		adress+1		;row*4
	clc
	adc		adress			;row*5
	scc:inc	adress+1
shift_loop:
	asl
	rol		adress+1		;row*10,20,40
	dey
	bpl		shift_loop
	rts
.endp

;==========================================================================
.proc ScreenConvertSetup
	pha
	rol
	rol
	rol
	rol
	and		#$03
	tax
	pla
	rts
.endp

;==========================================================================
; Convert an ATASCII character to displayable INTERNAL format.
;
; Entry:
;	A = ATASCII char
;
; Exit:
;	A = INTERNAL char
;
.proc	ScreenConvertATASCIIToInternal
	jsr		ScreenConvertSetup
	eor		ATASCIIToInternalTab,x
	rts
.endp

;==========================================================================
; Setup for pixel plot.
;
; Entry:
;	OLDCOL, OLDROW = position
;
; Exit:
;	TOADR = screen row
;	HOLD2 = byte offset within row
;	BITMSK = shifted bit mask for pixel
;
; Modified:
;	ADRESS
;
.proc ScreenSetupPlotPixel
	;;##TRACE "Folded pixel = $%02X" db(hold1)
	
	;compute initial address
	ldy		oldrow
	jsr		ScreenComputeToAddrX0
	
	;;##TRACE "Initial row address = $%04X" dw(toadr)
	
	;compute initial byte offset
	lda		oldcol+1
	ror
	lda		oldcol
	sta		hold2
	lda		#0
	ldx		dindex
	ldy		ScreenEncodingTab,x
	beq		no_xshift	
xshift_loop:
	ror		hold2
	ror
	dey
	bne		xshift_loop
no_xshift:

	;;##TRACE "Initial row offset = $%02X" db(hold2)

	;preshift bit mask
	rol
	rol
	rol
	rol
	tax
	lda		hold3
	dex
	bmi		xmaskshift_done
xmaskshift_loop:
	lsr
	dex
	bpl		xmaskshift_loop
xmaskshift_done:
	sta		bitmsk
	
	;;##TRACE "Initial bitmasks = $%02X $%02X" db(bitmsk) db(dmask)
	rts
.endp

;==========================================================================
; ScreenSetLastPosition
;
; Copies COLCRS/ROWCRS to OLDCOL/OLDROW.
;
.proc ScreenSetLastPosition
	ldx		#2
loop:
	lda		rowcrs,x
	sta		oldrow,x
	dex
	bpl		loop
	rts
.endp

;==========================================================================
; ScreenResetLogicalLineMap
;
; Marks all lines as the start of logical lines.
;
.proc ScreenResetLogicalLineMap
	lda		#$ff
	sta		logmap
	sta		logmap+1
	sta		logmap+2
	rts
.endp

;==========================================================================
; ScreenAdvancePosMode0
;
; Advance to the next cursor position in reading order, for mode 0.
;
; Exit:
;	C = 1 if no wrap, 0 if wrapped
;
; Modified:
;	X
;
.proc ScreenAdvancePosMode0
	inc		colcrs
	ldx		rmargn
	cpx		colcrs
	bcs		post_wrap
	ldx		lmargn
	stx		colcrs
	inc		rowcrs
post_wrap:
	rts
.endp
