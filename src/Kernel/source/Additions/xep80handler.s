;	Altirra - Atari 800/800XL/5200 emulator
;	Replacement XEP80 Handler Firmware - E:/S: Device Handler
;	Copyright (C) 2008-2014 Avery Lee
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

		icl		'cio.inc'
		icl		'sio.inc'
		icl		'kerneldb.inc'
		icl		'hardware.inc'

;==========================================================================
keybdv	equ		$e420

;==========================================================================

.macro _loop opcode adrmode operand
		.if :adrmode!='#'
		.error "Immediate addressing mode must be used with lo-opcode"
		.endif
		
		:opcode #<:operand
.endm

.macro _hiop opcode adrmode operand
		.if :adrmode!='#'
		.error "Immediate addressing mode must be used with hi-opcode"
		.endif
		
		.if HIBUILD
		:opcode <:operand
		.else
		:opcode >:operand
		.endif
.endm

.macro _ldalo adrmode operand " "
		_loop lda :adrmode :operand
.endm

.macro _ldahi adrmode operand " "
		_hiop lda :adrmode :operand
.endm

.macro _ldxhi adrmode operand " "
		_hiop ldx :adrmode :operand
.endm

.macro _ldyhi adrmode operand " "
		_hiop ldy :adrmode :operand
.endm

.macro _adchi adrmode operand " "
		_hiop adc :adrmode :operand
.endm

.macro _orahi adrmode operand " "
		_hiop ora :adrmode :operand
.endm

.macro _eorhi adrmode operand " "
		_hiop eor :adrmode :operand
.endm

.macro _andhi adrmode operand " "
		_hiop and :adrmode :operand
.endm

.macro _sbchi adrmode operand " "
		_hiop sbc :adrmode :operand
.endm

.macro _cmphi adrmode operand " "
		_hiop cmp :adrmode :operand
.endm

.macro _cpxhi adrmode operand " "
		_hiop cpx :adrmode :operand
.endm

.macro _cpyhi adrmode operand " "
		_hiop cpy :adrmode :operand
.endm

;==========================================================================
XEPCMD_HORIZ_POS			= $00
XEPCMD_HORIZ_POS_HI			= $50
XEPCMD_SET_LMARGN_LO		= $60
XEPCMD_SET_LMARGN_HI		= $70
XEPCMD_VERT_POS				= $80
XEPCMD_SET_RMARGN_LO		= $A0
XEPCMD_SET_RMARGN_HI		= $B0
XEPCMD_GET_BYTE_AND_ADVANCE	= $C0
XEPCMD_MASTER_RESET			= $C2
XEPCMD_ENTER_BURST_MODE		= $D2
XEPCMD_EXIT_BURST_MODE		= $D3
XEPCMD_CHARSET_A			= $D4
XEPCMD_CHARSET_B			= $D5
XEPCMD_CURSOR_OFF			= $D8
XEPCMD_CURSOR_ON			= $D9
XEPCMD_MOVE_TO_LOGLINE_START= $DB

;==========================================================================

		org		BASEADDR

base_addr:
		jmp		Init

data_begin:
data_byte	dta		0
mode_80		dta		0
saverow		dta		0
savecol		dta		0
savedsp		dta		0
savelmr		dta		0
savermr		dta		0
savechb		dta		0

;==========================================================================
opflag	dta		0
burst	dta		0
shdatal	dta		0
shdatah	dta		0
curlm	dta		0
currm	dta		0
curchb	dta		0
currow	dta		0
curcol	dta		0
curinh	dta		0
data_end:
portbit	dta		$10

;==========================================================================
.proc XEPSDevice
		dta		a(XEPScreenOpen-1)
		dta		a(XEPScreenClose-1)
		dta		a(XEPScreenGetByte-1)
		dta		a(XEPScreenPutByte-1)
		dta		a(XEPScreenGetStatus-1)
		dta		a(XEPScreenSpecial-1)
.endp

;==========================================================================
.proc XEPEDevice
		dta		a(XEPEditorOpen-1)
		dta		a(XEPEditorClose-1)
		dta		a(XEPEditorGetByte-1)
		dta		a(XEPEditorPutByte-1)
		dta		a(XEPEditorGetStatus-1)
		dta		a(XEPEditorSpecial-1)
.endp

;==========================================================================
.proc XEPScreenOpen
		lda		#$40
		jmp		XEPOpen
.endp

;==========================================================================
.proc XEPScreenClose
		ldy		#1
		rts
.endp

;==========================================================================
.proc XEPScreenPutByte
		sec
		ror		dspflg
		php
		jsr		XEPEditorPutByte
		plp
		rol		dspflg
		tya
		rts
.endp

;==========================================================================
.proc XEPScreenGetByte
		jsr		XEPEnterCriticalSection
		lda		#XEPCMD_GET_BYTE_AND_ADVANCE
		sec
		jsr		XEPTransmitByte
		jsr		XEPReceiveByte
		bmi		fail
		pha
		jsr		XEPReceiveCursorUpdate
		pla
fail:
		jsr		XEPLeaveCriticalSection
		rts
.endp

;==========================================================================
.proc XEPScreenGetStatus
		ldy		#1
		rts
.endp

;==========================================================================
.proc XEPScreenSpecial
		rts
.endp

;==========================================================================
.proc XEPEditorOpen
		lda		#$80
		jmp		XEPOpen
.endp

;==========================================================================
.proc XEPEditorClose
		ldy		#1
		rts
.endp

;==========================================================================
.proc XEPEditorGetByte
		php
		lda		bufcnt
		bne		in_line
		
		lda		colcrs
		sta		start_read_hpos

more_chars:
		jsr		XEPGetKey
		cpy		#0
		bmi		fail
		cmp		#$9b
		beq		got_eol
		pha
		php
		jsr		XEPEnterCriticalSection
		clc
		jsr		XEPTransmitByte
		jsr		XEPReceiveCursorUpdate
		jsr		XEPLeaveCriticalSection
		plp
		pla
		tya
		bmi		fail
		jmp		more_chars

got_eol:
		jsr		XEPEnterCriticalSection
		lda		#0
start_read_hpos = *-1
		jsr		XEPCheckSettings.do_col
		lda		#XEPCMD_MOVE_TO_LOGLINE_START
		jsr		XEPTransmitCommand
		inc		bufcnt
		bne		in_line_2
in_line:
		jsr		XEPEnterCriticalSection
in_line_2:
		lda		#XEPCMD_GET_BYTE_AND_ADVANCE
		jsr		XEPTransmitCommand
		jsr		XEPReceiveByte
		pha
		jsr		XEPReceiveCursorUpdate
		pla
		
		cmp		#$9b
		bne		not_eol
		dec		bufcnt
		
		clc
		jsr		XEPTransmitByte
		jsr		XEPReceiveCursorUpdate
		lda		#$9b
		ldy		#1
		
not_eol:
		jsr		XEPLeaveCriticalSection
fail:
		plp
		rts
.endp

;==========================================================================
.proc XEPGetKey
		lda		keybdv+5
		pha
		lda		keybdv+4
		pha
		rts
.endp

;==========================================================================
.proc XEPEditorPutByte
suspend_loop:
		;check for break
		ldy		brkkey
		beq		is_break

		;check for suspend
		ldy		ssflag
		bne		suspend_loop

		php
		jsr		XEPEnterCriticalSection
		pha
		jsr		XEPCheckSettings
		pla
		clc
		jsr		XEPTransmitByte
		bit		burst
		bpl		non_burst_mode
		
		ldx		#0
		ldy		#0
burst_wait_loop:
		lda		porta
		bit		portbit
		bne		burst_done
		dex
		bne		burst_wait_loop
		dey
		bne		burst_wait_loop

		ldy		#CIOStatTimeout
		bne		xit
		
burst_done:
		ldy		#1
xit:
		jsr		XEPLeaveCriticalSection
		plp
		rts
		
non_burst_mode:
		jsr		XEPReceiveCursorUpdate
		jmp		burst_done

is_break:
		dec		brkkey
		ldy		#CIOStatBreak
		rts
.endp

;==========================================================================
XEPEditorGetStatus = XEPScreenGetStatus

;==========================================================================
.proc XEPEditorSpecial
		lda		iccomz
		cmp		#$14
		beq		cmd14
		cmp		#$15
		beq		cmd15
		cmp		#$16
		beq		cmd16
		cmp		#$18
		beq		cmd18
		cmp		#$19
		beq		cmd19
		rts

cmd14:
		php
		jsr		XEPEnterCriticalSection
		lda		icax2z
		jsr		XEPTransmitCommand
		jsr		XEPLeaveCriticalSection
ok_2:
		plp
ok:
		ldy		#1
		rts

cmd15:
		ldx		#0
		lda		icax2z
		seq:ldx	#$ff
		cmp		burst
		beq		ok
		php
		jsr		XEPEnterCriticalSection
		lda		burst
		eor		#$ff
		clc
		adc		#XEPCMD_EXIT_BURST_MODE
		jsr		XEPTransmitCommand
		jsr		XEPLeaveCriticalSection
		jmp		ok_2			

cmd16:
		php
		jsr		XEPEnterCriticalSection
		lda		icax2z
		jsr		XEPTransmitCommand
		jsr		XEPReceiveByte
		sta		dvstat+1
		jsr		XEPLeaveCriticalSection
		plp
		rts

cmd18:
		rts

cmd19:
		rts

.endp

;==========================================================================
.proc XEPCheckSettings
		;check if someone turned on ANTIC DMA -- Basic XE does this,
		;and if screws up the send/receive timing
		lda		sdmctl
		bne		dma_wtf

check_inh:
		lda		crsinh
		cmp		curinh
		bne		do_inh
check_lmargn:
		lda		lmargn
		cmp		curlm
		bne		do_lmargn
check_rmargn:
		lda		rmargn
		cmp		currm
		bne		do_rmargn
check_chbase:
		lda		chbase
		cmp		curchb
		bne		do_chbase
check_row:
		lda		rowcrs
		cmp		currow
		bne		do_row
check_col:
		lda		colcrs
		cmp		curcol
		bne		do_col
		rts

dma_wtf:
		lda		#0
		sta		sdmctl
		sta		dmactl
		beq		check_inh

do_inh:
		sta		curinh
		cmp		#1
		lda		#XEPCMD_CURSOR_ON
		scc:lda	#XEPCMD_CURSOR_OFF
		jsr		XEPTransmitCommand
		jmp		check_lmargn

do_lmargn:
		sta		curlm
		pha
		and		#$0f
		ora		#XEPCMD_SET_LMARGN_LO
		jsr		XEPTransmitCommand
		pla
		and		#$f0
		beq		check_rmargn
		lsr
		lsr
		lsr
		lsr
		ora		#XEPCMD_SET_LMARGN_HI
		jsr		XEPTransmitCommand
		jmp		check_rmargn
		
do_rmargn:
		sta		currm
		pha
		and		#$0f
		ora		#XEPCMD_SET_RMARGN_LO
		jsr		XEPTransmitCommand
		pla
		and		#$f0
		cmp		#$40
		beq		check_chbase
		lsr
		lsr
		lsr
		lsr
		ora		#XEPCMD_SET_RMARGN_HI
		jsr		XEPTransmitCommand
		jmp		check_rmargn
		
do_chbase:
		sta		curchb
		cmp		#$cc
		lda		#XEPCMD_CHARSET_A
		sne:lda	#XEPCMD_CHARSET_B
		jsr		XEPTransmitCommand
		jmp		check_chbase
		
do_row:
		sta		currow
		ora		#$80
		jsr		XEPTransmitCommand
		jmp		check_col
		
do_col:
		sta		curcol
		cmp		#80
		bcs		do_wide_col
		jmp		XEPTransmitCommand

do_wide_col:
		pha
		and		#$0f
		jsr		XEPTransmitCommand
		pla
		lsr
		lsr
		lsr
		lsr
		ora		#XEPCMD_HORIZ_POS_HI
		jmp		XEPTransmitCommand
.endp

;==========================================================================
.proc XEPOpen
		bit		opflag
		beq		not_open
		ldy		#1
		rts
		
not_open:
		eor		opflag
		sta		opflag
		
		lda		#0
		sta		sdmctl
		sta		dmactl
		
		lda		pactl
		ora		#$04
		sta		pactl
		ldx		#$ff
		stx		porta
		tax
		and		#$fb
		sta		pactl
		lda		portbit
		sta		porta
		stx		pactl
		
		php
		jsr		XEPEnterCriticalSection
		
		lda		#XEPCMD_MASTER_RESET
		jsr		XEPTransmitCommand
		jsr		XEPReceiveByte
		bpl		open_successful
		tya
		pha
		lda		opflag
		jsr		XEPClose
		pla
		tay
xit:
		jsr		XEPLeaveCriticalSection
		plp
		rts
		
open_successful:
		mva		#2 lmargn
		mva		#79 rmargn
		ldy		#1
		bne		xit
.endp

;==========================================================================
.proc XEPClose
		bit		opflag
		beq		already_closed
		eor		opflag
		sta		opflag
		bne		already_closed
		
		lda		pactl
		ora		#$04
		sta		pactl
		ldx		#$ff
		stx		porta
		and		#$fb
		sta		pactl
		stx		porta
		ora		#$04
		sta		pactl
		
		mva		#$22 sdmctl
		sta		dmactl
		
already_closed:
		ldy		#1
		rts
.endp

;==========================================================================
.proc XEPEnterCriticalSection
		mvy		#0 nmien
		sei
		rts
.endp

;==========================================================================
.proc XEPLeaveCriticalSection
		mvx		#$40 nmien
		cpy		#0
		rts
.endp

;==========================================================================
.proc XEPReceiveCursorUpdate
		jsr		XEPReceiveByte
		bmi		err
		tax
		bmi		horiz_or_vert_update
		sta		colcrs
		sta		curcol
		rts
horiz_or_vert_update:
		cmp		#$c0
		bcs		vert_update
		and		#$7f
		sta		colcrs
		sta		curcol
		jsr		XEPReceiveByte
		bmi		err
vert_update:
		and		#$1f
		sta		rowcrs
		sta		currow
		tya
err:
		rts
.endp

;==========================================================================
; Input:
;	A = byte
;	C = command flag (1 = command, 0 = data)
;
; Modified:
;	A, X
;
; Preserved:
;	Y
;
XEPTransmitByte = XEPTransmitCommand.do_byte
.proc XEPTransmitCommand
		sec
do_byte:
		;##ASSERT (p&4)
		;##TRACE "Transmitting byte %02X" (a)
		sta		shdatal
		rol		shdatah
		
		;send start bit
		lda		portbit
		eor		#$ff
		sta		wsync
		sta		porta
		
		;send data bits
		ldx		#9
transmit_loop:
		lsr		shdatah
		ror		shdatal
		lda		#0
		scs:lda	portbit
		eor		#$ff
		sta		wsync
		sta		porta
		dex
		bne		transmit_loop
		
		;send stop bit
		lda		#$ff
		sta		wsync
		sta		porta
		rts
.endp

;==========================================================================
.proc XEPReceiveByte
		;set timeout
		ldx		#0
		ldy		#$08
		
		;wait for PORTA bit to go low
		lda		portbit
		asl
wait_loop:
		bit		porta
		beq		found_start
		dex
		bne		wait_loop
		dey
		bne		wait_loop
		
		;timeout
		;##TRACE "Timeout"
		ldy		#CIOStatTimeout
		rts
		
found_start:
		;wait until approx middle of start bit
		ldx		#10				;2
		dex:rne					;49
		
		;sample the center of the start bit, make sure it is one
		bit		porta
		bne		wait_loop		;3
		pha						;3
		pla						;4
		
		;now shift in 10 bits at 105 CPU cycles apart (114 machine cycles)
		ldx		#10				;2
receive_loop:
		ldy		#14				;2
		dey:rne					;69
		bit		$00				;3
		ror		shdatah			;6
		ror		shdatal			;6
		lda		porta			;4
		lsr						;2
		and		portbit			;4
		clc						;2
		adc		#$ff			;2
		dex						;2
		bne		receive_loop	;3
		
		;check that we got a proper stop bit
		bcc		stop_bit_bad
		
		;shift out the command bit into the carry and return
		lda		shdatah
		rol		shdatal
		rol
		;##TRACE "Received byte %02X" (a)
		ldy		#1
		rts

stop_bit_bad:
		ldy		#CIOStatSerFrameErr
		rts
.endp

;==========================================================================
; Note that DOS 2.0's AUTORUN.SYS does some pretty funky things here -- it
; jumps through (DOSINI) after loading the handler, but that must NOT
; actually invoke DOS's init, or the EXE loader hangs. Therefore, we have
; to check whether we're handling a warmstart, and if we're not, we have
; to return without chaining.
;
.proc Reinit
		;install CIO handlers for E: and S:
		ldx		#30
check_loop:
		lda		hatabs,x
		cmp		#'E'
		bne		not_e
		_ldalo	#XEPEDevice
		sta		hatabs+1,x
		_ldahi	#XEPEDevice
		sta		hatabs+2,x
		bne		not_s
not_e:
		cmp		#'S'
		bne		not_s
		_ldalo	#XEPSDevice
		sta		hatabs+1,x
		_ldahi	#XEPSDevice
		sta		hatabs+2,x
not_s:
		dex
		dex
		dex
		bpl		check_loop

		;reset put char vector for E:
		_ldalo	#[XEPEditorPutByte-1]
		sta		icptl

		_ldahi	#[XEPEditorPutByte-1]
		sta		icpth

		;adjust MEMTOP
		_ldalo	#base_addr
		sta		memtop
		_ldahi	#base_addr
		sta		memtop+1

		;reset work vars
		ldx		#data_end-data_begin-1
		lda		#0
		sta:rpl	data_begin,x-
		
		;open 80-column display
		lda		#$40
		jsr		XEPOpen

		;check if this is a warmstart, and don't call into DOS if not
		lda		warmst
		beq		skip_chain
		
		jmp		$ffff
dosini_chain = *-2

skip_chain:
		rts
.endp

bss_end = *

;==========================================================================
.proc Init
		;hook DOSINI
		mwa		dosini Reinit.dosini_chain
		
		_ldalo	#Reinit
		sta		dosini
		_ldahi	#Reinit
		sta		dosini+1
		
		;all done
		clc
		rts
.endp

		run		Init
