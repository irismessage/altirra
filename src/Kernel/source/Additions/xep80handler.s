;	Altirra - Atari 800/800XL/5200 emulator
;	Replacement XEP80 Handler Firmware - E:/S: Device Handler
;	Copyright (C) 2008-2017 Avery Lee
;
;	Copying and distribution of this file, with or without modification,
;	are permitted in any medium without royalty provided the copyright
;	notice and this notice are preserved.  This file is offered as-is,
;	without any warranty.

		icl		'cio.inc'
		icl		'sio.inc'
		icl		'kerneldb.inc'
		icl		'hardware.inc'
		icl		'xep80config.inc'

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
XEPCMD_CLEAR_LIST_FLAG		= $D0
XEPCMD_SET_LIST_FLAG		= $D1
XEPCMD_EXIT_BURST_MODE		= $D2
XEPCMD_ENTER_BURST_MODE		= $D3
XEPCMD_CHARSET_A			= $D4
XEPCMD_CHARSET_B			= $D5
XEPCMD_MODIFY_TEXT_TO_50HZ	= $D7
XEPCMD_CURSOR_OFF			= $D8
XEPCMD_CURSOR_ON			= $D9
XEPCMD_MOVE_TO_LOGLINE_START= $DB
XEPCMD_SET_EXTRA_BYTE		= $E1
XEPCMD_SET_BAUD_RATE		= $FA
XEPCMD_SET_UMX				= $FC

;==========================================================================

.if !XEP_SDX
		org		BASEADDR

base_addr:
		jmp		Init
.endif

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
curdsp	dta		0
data_end:
portbit	dta		$10
portbitr	dta		$20

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
		lda		dspflg
		pha
		lda		#0
		sta		dspflg
		lda		#$7D
		jsr		XEPEditorPutByte
		pla
		sta		dspflg
		tya
		rts
.endp

;==========================================================================
XEPScreenClose = XEPExitSuccess

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
		jsr		XEPTransmitCommand
		jsr		XEPReceiveByte
		bmi		fail
		pha
		jsr		XEPReceiveCursorUpdate
		pla
fail:
		jmp		XEPLeaveCriticalSection
.endp

;==========================================================================
XEPScreenGetStatus = XEPExitSuccess
XEPScreenSpecial = XEPExitNotSupported

;==========================================================================
XEPEditorClose = XEPExitSuccess

;==========================================================================
.proc XEPEditorGetByte
		php

		lda		#0
space_count = *-1
		beq		no_pending_spaces
		dec		space_count
		lda		#' '
return_char:
		ldy		#1
		plp
		rts
no_pending_spaces:

		lda		#' '
saved_char = *-1
		cmp		#' '
		beq		no_saved_char

		ldy		#' '
		sty		saved_char
		bne		return_char

no_saved_char:
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
		jsr		_XEPCheckSettings.do_col
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

		cmp		#' '
		bne		not_space
		lda		space_count
		bmi		not_space
		inc		space_count
		bne		in_line_2
		
not_space:
		cmp		#$9b
		bne		not_eol
		dec		bufcnt
		clc
		jsr		XEPTransmitByte
		jsr		XEPReceiveCursorUpdate
		lda		#$9b
		ldy		#0
		sty		space_count
		iny
not_eol:
		sty		_status
		ldy		space_count
		beq		no_spaces
		sta		saved_char
		dec		space_count
		lda		#' '
no_spaces:
		ldy		#1
_status = *-1
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
		
		jsr		XEPWaitBurstACK
		bcc		burst_done

		ldy		#CIOStatTimeout
		bne		xit
		
non_burst_mode:
		jsr		XEPReceiveCursorUpdate
burst_done:
		ldy		#1
xit:
		jsr		XEPLeaveCriticalSection
		plp
		rts
		
is_break:
		dec		brkkey
		ldy		#CIOStatBreak
		rts
.endp

;==========================================================================
.proc XEPWaitBurstACK
		;The transmit code exits immediately after the beginning of the
		;stop bit, so we can land here before the XEP80 has finished
		;receiving the last byte, processed it, and asserted busy. The
		;timing:
		;
		;	~57 cyc.		Leading edge to middle of start bit
		;	~126 cyc.		Interrupt time to break asserted (39 cycles @ 0.555MHz)
		;
		;The XEP80 spec recommends waiting 90us (~161 cyc.) before testing the
		;busy line, but doesn't specify the precise starting condition. In
		;practice there may be another ~87 cyc. from the end of row interrupt
		;already pending, and three WSYNCs are necessary to make this
		;consistently reliable on the actual hardware.
		;
		sta		wsync
		sta		wsync
		sta		wsync
		ldy		#0
		lda		portbitr
burst_wait_loop:
		bit		porta
		bne		burst_done
		dex
		bne		burst_wait_loop
		dey
		bne		burst_wait_loop
		sec
		rts
burst_done:
		clc
		rts
.endp

;==========================================================================
XEPEditorGetStatus = XEPScreenGetStatus

;==========================================================================
.proc XEPEditorSpecial
		lda		iccomz
		cmp		#$14
		beq		cmd14
		cmp		#$16
		bcc		cmd15
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
transmit_and_exit:
		jsr		XEPTransmitCommand
unlock_and_exit:
		jsr		XEPLeaveCriticalSection
		plp
exit_success:
.def :XEPExitSuccess
		ldy		#1
cmd18:
cmd19:
.def :XEPExitNotSupported
		rts

cmd15:	;Set burst mode: ICAX2 = 0 for normal, 1 for burst
		ldx		#0
		lda		icax2z
		seq:dex
		cpx		burst
		beq		exit_success
		stx		burst
		php
		jsr		XEPEnterCriticalSection
		lda		burst
		and		#1
		ora		#XEPCMD_EXIT_BURST_MODE
		jmp		transmit_and_exit

cmd16:
		php
		jsr		XEPEnterCriticalSection
		lda		icax2z
		jsr		XEPTransmitCommand
		jsr		XEPReceiveByte
		sta		dvstat+1
		jmp		unlock_and_exit

.endp

;==========================================================================
.proc _XEPCheckSettings
do_dsp:
		sta		curdsp
		cmp		#1
		lda		#XEPCMD_CLEAR_LIST_FLAG/2
		rol
		jsr		XEPTransmitCommand
		jmp		check_lmargn

.def :XEPCheckSettings
		;check if someone turned on ANTIC DMA -- Basic XE does this,
		;and if screws up the send/receive timing
		lda		sdmctl
		bne		dma_wtf

check_inh:
		lda		crsinh
		cmp		curinh
		bne		do_inh
check_dsp:
		lda		dspflg
		cmp		curdsp
		bne		do_dsp
check_lmargn:
		lda		lmargn
		cmp		curlm
		bne		do_lmargn
check_rmargn:
		lda		rmargn
		cmp		currm
		bne		do_rmargn
check_chbase:
		lda		chbas
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
		.if !XEP_OPTION_ULTRA
		;For the non-turbo driver, we can let this slide if burst mode is
		;enabled, as xmit can be synchronous with horizontal blank and recv
		;is not timing critical. For the turbo driver, we have no choice but
		;to turn off the display.
		lda		burst
		bne		check_inh
		.endif

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
		jmp		check_dsp

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
		jmp		check_chbase
		
do_chbase:
		sta		curchb
		eor		#$33
		cmp		#$ff
		lda		#XEPCMD_CHARSET_A/2
		rol
		jsr		XEPTransmitCommand

.if XEP_OPTION_ULTRA
		;The XEP80 needs a little more time to switch charsets since it needs
		;to rewrite the row pointers. Since these commands don't have a return
		;byte and we're usually not in burst mode, we can't tell when it's done.
		;At normal speed this is fine, in ultra mode it needs a little more time.
		sta		wsync
.endif

		jmp		check_row
		
do_row:
		sta		currow
		ora		#$80
		jsr		XEPTransmitCommand
		jmp		check_col
		
do_col:
		sta		curcol
		cmp		#80
		bcc		do_narrow_col
		pha
		and		#$0f
		jsr		XEPTransmitCommand
		pla
		lsr
		lsr
		lsr
		lsr
		ora		#XEPCMD_HORIZ_POS_HI
do_narrow_col:
		jmp		XEPTransmitCommand
.endp

;==========================================================================
XEPEditorOpen:
		lda		#$80
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
		
		;switch to ORA
		lda		pactl
		ora		#$04
		sta		pactl

		;raise input line
		ldx		portbit
		stx		porta

		;switch to DDRA
		and		#$fb
		sta		pactl

		;switch to port bit to output
		stx		porta

		;switch back to ORA
		ora		#$04
		sta		pactl
		
		php
		jsr		XEPEnterCriticalSection
		
.if XEP_OPTION_ULTRA
		jsr		XEPSetTransmitStd
		jsr		XEPReset
		bpl		open_successful

std_failed:
		;delay a bit in case the XEP-80 is farked up
		ldx		#$40
delay_loop:
		lda		vcount
		cmp:req	vcount
		dex
		bne		delay_loop

		jsr		XEPSetTransmitFast
		jsr		XEPReset
		bpl		open_successful

.else
		jsr		XEPReset
		bpl		open_successful
.endif

		tya
		pha
		jsr		XEPClose.force_close
		pla
		tay
open_successful:
xit:
		jsr		XEPLeaveCriticalSection
		plp
		tya
		rts
.endp

;==========================================================================
.macro XEP_TURBO_SETUP_1
.if XEP_OPTION_ULTRA
		;enter burst mode
		lda		#XEPCMD_ENTER_BURST_MODE
		jsr		XEPTransmitCommand

		;switch UART prescaler from /8 to /4
		lda		#$05			;/6 baud divisor
		jsr		transmit_byte_burst

		lda		#XEPCMD_SET_EXTRA_BYTE
		jsr		transmit_command_burst

		lda		#$10			;/4 prescaler
		jsr		transmit_byte_burst

		lda		#XEPCMD_SET_BAUD_RATE
		jsr		transmit_command_burst

		;switch to fast transmit
		jsr		XEPSetTransmitFast

		;clear the screen to remove the garbage we added (delete line would be
		;faster, but would mess up the row pointers)
		lda		#$7D
		jsr		transmit_byte_burst

		;exit burst mode
		lda		#XEPCMD_EXIT_BURST_MODE
		jsr		XEPTransmitCommand
.endif
.endm

.macro XEP_TURBO_SETUP_2
.def transmit_command_burst
		sec
		dta		{bit 0}
.def transmit_byte_burst
		clc
		jsr		XEPTransmitByte
		jsr		XEPWaitBurstACK
.endm

.proc XEPReset
		lda		#XEPCMD_MASTER_RESET
		jsr		XEPTransmitCommand
		jsr		XEPReceiveByte
		bmi		init_timeout
		eor		#$01
		beq		init_ok
		ldy		#CIOStatNAK
init_timeout:
		rts

init_ok:
.if XEP_DEFAULT_50HZ
		lda		#XEPCMD_MODIFY_TEXT_TO_50HZ
		jsr		XEPTransmitCommand
.endif
.if XEP_SDX
		lda		#XEPCMD_MODIFY_TEXT_TO_50HZ
		bit		XEPTransmitCommand
xep_50hz_patch = *-3
.endif

		mva		#79 rmargn
		lda		#0
		sta		lmargn
		sta		colcrs
		sta		rowcrs
		sta		colcrs+1

.if XEP_SDX
		jsr		XEPSetupTurbo
xep_turbo_patch = *-3

		jsr		XEPAdjustVideoTiming
xep_vhold_patch = *-3
.else
		XEP_TURBO_SETUP_1
.endif

		ldy		#1
		rts

.if !XEP_SDX
		XEP_TURBO_SETUP_2
.endif

.endp

;==========================================================================
.proc XEPClose
		bit		opflag
		beq		already_closed
		eor		opflag
		sta		opflag
		bne		already_closed
		
force_close:
		lda		pactl
		ora		#$04
		sta		pactl
		ldx		#0
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
.if XEP_OPTION_ULTRA
XEPTransmitCommand:
		sec
XEPTransmitByte:
		jmp		XEPTransmitByteStd		;patched when going ultra

XEPReceiveByte:
		jmp		XEPReceiveByteStd

.proc XEPSetTransmitFast
		_ldalo	#XEPTransmitByteFast
		sta		XEPTransmitByte+1
		_ldahi	#XEPTransmitByteFast
		sta		XEPTransmitByte+2

		_ldalo	#XEPReceiveByteFast
		sta		XEPReceiveByte+1
		_ldahi	#XEPReceiveByteFast
		sta		XEPReceiveByte+2
		rts
.endp

.proc XEPSetTransmitStd
		_ldalo	#XEPTransmitByteStd
		sta		XEPTransmitByte+1
		_ldahi	#XEPTransmitByteStd
		sta		XEPTransmitByte+2

		_ldalo	#XEPReceiveByteStd
		sta		XEPReceiveByte+1
		_ldahi	#XEPReceiveByteStd
		sta		XEPReceiveByte+2

		rts
.endp

.else
XEPTransmitByte = XEPTransmitByteStd
XEPTransmitCommand = XEPTransmitCommandStd
.endif

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
.if XEP_OPTION_ULTRA
.proc XEPTransmitByteStd
.else
.proc XEPTransmitCommandStd
		sec
.def :XEPTransmitByteStd
.endif
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
; Standard receive routine (XEP80 -> computer)
;
.if XEP_OPTION_ULTRA
.proc XEPReceiveByteStd
.else
.proc XEPReceiveByte
.endif
		;set timeout (we are being sloppy on X to save time)
		ldy		#$40
		
		;wait for PORTA bit to go low
		lda		portbitr
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
		;reset work vars
		ldx		#data_end-data_begin-1
		lda		#0
		sta:rpl	data_begin,x-
		
		;open 80-column display
		lda		#$40
		jsr		XEPOpen
		bpl		open_success

		sec
		rts

open_success:
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

.if !XEP_SDX
		;adjust MEMTOP
		_ldalo	#base_addr
		sta		memtop
		_ldahi	#base_addr
		sta		memtop+1
.endif

		;mark success for Init
		clc

.if !XEP_SDX
		;check if this is a warmstart, and don't call into DOS if not
		lda		warmst
		beq		skip_chain
		
		jmp		skip_chain
dosini_chain = *-2

skip_chain:
.endif
		rts
.endp

;==========================================================================
.if !XEP_SDX
.proc Init
		;attempt to initialize XEP-80
		jsr		Reinit
		bcs		fail

		;hook DOSINI
		mwa		dosini Reinit.dosini_chain
		
		_ldalo	#Reinit
		sta		dosini
		_ldahi	#Reinit
		sta		dosini+1
		
		;all done
		clc
fail:
		rts
.endp
.endif

;================================================================================
; CUT POINT - EVERYTHING BELOW THIS IS JETTISONED IN SDX IF NOT USING VHOLD/ULTRA
;================================================================================

__handler_end_novhold:

;==========================================================================
.if XEP_SDX
.proc XEPAdjustVideoTiming
		ldx		#256-[init_data_end-init_data]
init_loop:
		lda		init_data_end-$0100+1,x
		pha
		lda		init_data_end-$0100,x
		inx
		inx
		stx		init_index
		jsr		XEPTransmitCommand
		pla
		jsr		XEPEditorPutByte
		ldx		#0
init_index = *-1
		bne		init_loop
		rts

init_data:
		;Two sneaky things here:
		; - Every other byte below is a command.
		; - The timing chain parameters here are for 60Hz, and are overwritten
		;   by the SDX driver for 50Hz.
		;
		dta		XEPCMD_SET_LIST_FLAG
		dta		$00		;index = 0
		dta		$F6		;set timing chain index
		dta		$6C		;0	horizontal length = 109 characters
		dta		$F7
		dta		$54		;1	horizontal blank begin = 85 characters
		dta		$F7
		dta		$57		;2	horizontal sync begin = 88 characters
		dta		$F7
		dta		$61		;3	horizontal sync end = 96 characters
		dta		$F7
		dta		$81		;4	character height = 9 scans, extra scans = 1
		dta		$F7
		dta		$1C		;5	vertical length = 29 rows
		dta		$F7
		dta		$19		;6	vertical blank begin = 26 rows
		dta		$F7
		dta		$02		;7	vertical sync scans 233-235
		dta		$F7
		dta		$17		;VINT after row 23
		dta		$F8
		dta		$20
		dta		XEPCMD_CLEAR_LIST_FLAG
		dta		$7D
init_data_end:
.endp
.endif

;==========================================================================
; CUT POINT - EVERYTHING BELOW THIS IS JETTISONED IN SDX IF NOT USING ULTRA
;==========================================================================

__handler_end_noultra:

;==========================================================================
; Fast 31.5KHz transmit routine (computer -> XEP80)
;
; The basic timing here is 57 cycles per bit, but there is an important
; catch. The joystick ports are designed for resilience over speed and
; have significant capacitance, and more importantly, PIA port A has
; single-ended drivers. This leads to rather long rise times for 0 -> 1
; transitions.
;
; With some PIA chips and systems, the rise rate is not quite fast enough
; to cross the XEP80's threshold in a half bit time at 31.5KHz. To fix
; this, we apply precompensation so that 1 bits are started a bit earlier
; than 0 bits. Fortunately, the transition for 0 bits is much faster due
; to the PIA actively sinking the output, so the start bit transition
; is fast and sets a stable starting offset.
;
.if XEP_OPTION_ULTRA
.proc XEPTransmitByteFast
		sta		shdatal
		lda		#$ff
		rol
		
		;send start bit
		sta		wsync
		sta		shdatah			;4		*,105-107
		lda		portbit			;4		108-111
		eor		#$ff			;2		112-113
		sta		porta			;4		0-3
		
		;send 9 data bits
		ldx		#9				;2		4-5
		bit		$80				;3		6-8
transmit_loop:
		lsr		shdatah			;6		9-14
		ror		shdatal			;6		15-20
		lda		#$ff			;2		21-22
		bcs		first_one		;2/3	23-24 -or- 23-24,25*,26
		eor		portbit			;4		25*,26-28,29*,30 -or- ()
		nop:nop					;4		31-32,33*,34-35 -or- ()
first_one:
		pha:pla					;7		36,37*,38-40,41*,42-44 -or- 27-28,29*,30-32,33*,34-35
		sta		porta			;4		45*,46-48,49*,50 -or- 36,37*,38-40
		bcc		first_zero		;3/2	51-52,53*,54 -or- 41*,42-43
		nop:nop					;  4	() -or- 44,45*,46-48
		nop:nop					;  4	() -or- 49*,50-52,53*,54
first_zero:
		lda		#$ff			;2		55-56
		dex						;2		57*,58-59
		beq		transmit_done	;2/3	60-61 -or- 60-62

		lsr		shdatah			;6		63-68
		ror		shdatal			;6		69-74
		sta		wsync			;4		75-78 + suspend
		bcs		second_one		;2/3	*,105 -or- *,105-106
		eor		portbit			;4		106-109
		nop:nop					;4		110-113
second_one:
		sta		porta			;4		0-3 -or- 107-110
		bcc		second_zero		;3/2	4-6 -or- 111-112
		nop:nop					;  4	() -or- 113-2
		nop:nop					;  4	() -or- 3-6
second_zero:

		dex						;2		7-8
		bne		transmit_loop	;3		9-11		!! - unconditional

transmit_done:
		;send stop bit -- we do this separately to save a few cycles
		sta		wsync
		sta		porta
		rts
.endp
.endif

;==========================================================================
.if XEP_SDX
.proc XEPSetupTurbo
	XEP_TURBO_SETUP_1
	XEP_TURBO_SETUP_2
.endp
.endif

;==========================================================================
; Fast receive routine
;
; The XEP80 normally transmits at 15.625Kbaud, but in ultra mode we raise
; receive to 31.25Kbaud. This gives a bit cell time of 57.3 cycles (NTSC)
; or 56.8 cycles (PAL), so 57 cycles is good. There will be a bit of jitter
; due to refresh cycles, but it's hard to avoid that.
;
.if XEP_OPTION_ULTRA
.proc XEPReceiveByteFast
		;set timeout (we are being sloppy on X to save time)
		ldy		#$40
		
		;wait for PORTA bit to go low
		lda		portbitr
wait_loop:
		bit		porta
		beq		found_start		;2+1
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
		ldx		#5				;2
		dex:rne					;24
		
		;sample the center of the start bit, make sure it is one
		bit		porta			;4
		bne		wait_loop		;3
		jsr		delay12			;12
		
		;now shift in 10 bits at 52 CPU cycles apart (~57 machine cycles)
		ldx		#10				;2
receive_loop:
		pha:pla					;7
		pha:pla					;7
		pha:pla					;7
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
delay12:
		rts
.endp
.endif

;===============================================================================
.if !XEP_SDX
		run		Init
.endif
