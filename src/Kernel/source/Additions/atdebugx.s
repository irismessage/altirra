; Altirra - Atari 800/800XL/5200 emulator
; Copyright (C) 2021 Avery Lee, All Rights Reserved.
; Additions module - debug link driver for SpartaDOS X 4.40+
;
; Copying and distribution of this file, with or without modification,
; are permitted in any medium without royalty provided the copyright
; notice and this notice are preserved.  This file is offered as-is,
; without any warranty.

		icl		'hardware.inc'
		icl		'kerneldb.inc'
		icl		'sio.inc'

siov = $e459

DEVICEID_DEBUG = $7E
DBGCMD_IDENTIFY				= $20
DBGCMD_START_SDX_SYMBOLS	= $24
DBGCMD_ADD_SDX_SYMBOL		= $25
DBGCMD_END_SDX_SYMBOLS		= $26
DBGCMD_PROCESS_START		= $27

printf	smb		'PRINTF'
s_next	smb		'S_NEXT'
symbol	smb		'SYMBOL'

		blk		reloc main

startup:
		ldx		#[.len dcb_identify]
		mva:rpl	dcb_identify,x ddevic,x-
		jsr		siov
		bpl		identify_ok
		jsr		printf
		dta		'No debug link found',$9B,0
		rts

.local dcb_identify
		dta		DEVICEID_DEBUG			;ddevic
		dta		1						;dunit
		dta		DBGCMD_IDENTIFY			;dcomnd
		dta		$40						;dstats
		dta		a(buf)					;dbuflo/hi
		dta		a(1)					;dtimlo, unused
		dta		a(40)					;dbytlo/hi
		dta		40, $41					;daux1/2
.endl

identify_ok:
		;report debugger presence
		mva		buf namelen
		jsr		printf
		dta		'%*s debugger attached',$9B,0
		dta		a(namelen)
		dta		a(buf+1)

		;send start symbols command
		mva		#0 dstats
		mva		#DBGCMD_START_SDX_SYMBOLS dcomnd
		jsr		siov
		tya
		bmi		fail_symerr

		;reset to start of symbol list
		lda		#0
		sta		symbol+2

symbol_loop:
		;set up for sending symbols
		mva		#13 dbytlo
		mwa		symadr dbuflo

		;request next symbol
		jsr		s_next
		beq		symbols_done

		;send symbol to debugger
		lda		#DBGCMD_ADD_SDX_SYMBOL
		ldy		#$80
		jsr		siov2
		tya
		bpl		symbol_loop
fail_symerr:
		sty		symerr
		jsr		printf
		dta		'Error $%02X sending symbols',$9B,0
symerr	dta		a(0)
		rts

symbols_done:
		;mark end of symbols list
		lda		#DBGCMD_END_SDX_SYMBOLS
		ldy		#0
		jsr		siov2

		;report success
		jsr		printf
		dta		'SDX symbols loaded',$9B,0

		;all done
		rts

siov2:
		sta		dcomnd
		sty		dstats
		jmp		siov

symadr	dta		a(symbol)
namelen	dta		a(0)
buf:
