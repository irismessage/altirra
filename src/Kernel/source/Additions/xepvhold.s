;	Altirra - Atari 800/800XL/5200 emulator
;	XEP80 vertical hold adjustment tool
;	Copyright (C) 2008-2021 Avery Lee
;
;	Copying and distribution of this file, with or without modification,
;	are permitted in any medium without royalty provided the copyright
;	notice and this notice are preserved.  This file is offered as-is,
;	without any warranty.

		icl		'cio.inc'
		icl		'sio.inc'
		icl		'hardware.inc'
		icl		'kerneldb.inc'

ciov = $e456

XEPCMD_CLEAR_LIST_FLAG		= $D0
XEPCMD_SET_LIST_FLAG		= $D1
XEPCMD_SET_TCP				= $F6
XEPCMD_SET_TCR				= $F7
XEPCMD_SET_VINT				= $F8

		org		$3400
.proc main
		;switch to burst mode (XIO 21 AUX2=1)
		lda		#1
		jsr		do_xio21

		;switch to list mode
		sec
		ror		dspflg

		;set extra value to $00
		lda		#0
		sta		icbll
		sta		icblh
		jsr		do_put

		;XIO 22 - $F6 (set timing control pointer)
		lda		#XEPCMD_SET_TCP
		jsr		do_xio22

init_loop:
		ldx		#256-[init_data_end-init_data]
init_index = *-1
		lda		init_data_end-$0100,x
		jsr		do_put

		;XIO 22 - $F7 (set timing control register)
		lda		#XEPCMD_SET_TCR
		jsr		do_xio22

		inc		init_index
		bne		init_loop

		;set VINT to occur after row 23
.if USE_PAL
		lda		#$16		;VINT after row 22
.else
		lda		#$17		;VINT after row 23
.endif
		jsr		do_put

		;XIO 22 - $F8 (set vertical interrupt register)
		lda		#XEPCMD_SET_VINT
		jsr		do_xio22

		;clear list mode and delete line to delete the junk from the TCP
		;reprogramming
		asl		dspflg
		lda		#$9c
		jsr		do_put

		;leave to burst mode (XIO 21 AUX2=1) and exit
		lda		#0
do_xio21:
		ldx		#21
		dta		{bit $0100}
do_xio22:
		ldx		#22
		sta		icax2
		dta		{bit $0100}
do_put:
		ldx		#CIOCmdPutChars
		stx		iccmd
		ldx		#0
		jmp		ciov

init_data:
		;The horizontal timing parameters below require some clarification. The
		;XEP80 uses an external character row counter that is clocked off of
		;the horizontal sync (HS) pulse. However, the NS405 prefetches four
		;bytes into its FIFO shortly after the start of horizontal _blank_. This
		;means that the row counter only works properly if HSYNC follows almost
		;immediately after the start of HBLANK. Unfortunately, this also results
		;in the display being shoved to the right, as the default timing
		;parameters do. If horizontal sync is delayed to recenter the display,
		;those prefetched characters end up being shifted down a scanline. This
		;only affects the external character sets as the internal charset uses
		;an internal counter.
		;
		;To hack around this, we extend the horizontal displayed width from 80
		;characters to 85. Those extra 5 columns are normally blank, so this
		;isn't typically visible, but it delays the prefetch long enough to
		;not run afoul of the row count timing.

.if USE_PAL
		;Character cell:	7x11
		;Geometry:			110x28 characters + 4 scans (312 lines)
		;Horizontal scan:	15.584KHz
		;Vertical scan:		49.95Hz
		;
		dta		$6D		;0	horizontal length = 110 characters
		dta		$54		;1	horizontal blank begin = 85 characters
		dta		$56		;2	horizontal sync begin = 87 characters
		dta		$60		;3	horizontal sync end = 95 characters
		dta		$A4		;4	character height = 11 scans, extra scans = 4
		dta		$1B		;5	vertical length = 28 rows
		dta		$18		;6	vertical blank begin = 25 rows
		dta		$46		;7	vertical sync scans 278-280
.else
		;Character cell:	7x9
		;Geometry:			109x29 characters + 0 scans (261 lines)
		;Horizontal scan:	15.727KHz
		;Vertical scan:		60.03Hz
		;
		dta		$6C		;0	horizontal length = 109 characters
		dta		$54		;1	horizontal blank begin = 85 characters
		dta		$57		;2	horizontal sync begin = 88 characters
		dta		$61		;3	horizontal sync end = 96 characters
		dta		$81		;4	character height = 9 scans, extra scans = 1
		dta		$1C		;5	vertical length = 29 rows
		dta		$19		;6	vertical blank begin = 26 rows
		dta		$02		;7	vertical sync scans 233-235
.endif
init_data_end:
.endp

		run		main
