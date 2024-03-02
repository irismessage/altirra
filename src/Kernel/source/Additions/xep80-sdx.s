;	Altirra - Atari 800/800XL/5200 emulator
;	Replacement XEP80 Handler Firmware - SpartaDOS X relocator and loader
;	Copyright (C) 2008-2021 Avery Lee
;
;	Copying and distribution of this file, with or without modification,
;	are permitted in any medium without royalty provided the copyright
;	notice and this notice are preserved.  This file is offered as-is,
;	without any warranty.

		blk		sparta BASEADDR

sdx_printf	smb 'PRINTF'
sdx_s_addiz	smb	'S_ADDIZ'
sdx_u_slash	smb	'U_SLASH'

		jmp		__reloc

		XEP_SDX = 1
		XEP_OPTION_TURBO = 1
		XEP_OPTION_ULTRA = 1

		icl		'xep80handler.s'

__handler_end:

;===============================================================================
.proc	main
		;parse switches
		_ldalo	#switches
		_ldxhi	#switches
		ldy		#[.len switches]
		jsr		sdx_u_slash

		;patch out PAL and turbo init if not wanted
		lda		#{cmp $0100}

		bit		switches.u
		smi:sta	XEPReset.xep_turbo_patch
		bit		switches.v
		smi:sta	XEPReset.xep_vhold_patch
		bit		switches.p
		bpl		use_60hz
		lda		#{jsr $0100}
		sta		XEPReset.xep_50hz_patch

		;if we are using 50Hz + VHold, we need to switch the VHold parameters
		;to 50Hz compatible ones
		ldx		#256-[params_50hz_end-params_50hz]
		ldy		#0
update_50hz_loop:
		mva		params_50hz_end-$0100,x XEPAdjustVideoTiming.init_data+3,y
		iny
		iny
		inx
		bne		update_50hz_loop
		
use_60hz:

		;switch to port 1 if desired
		bit		switches._1
		bpl		use_port2
		mva		#$01 portbit
		mva		#$02 portbitr
		mva		#'1' msg_portnum
use_port2:

		jsr		Reinit
		bcs		fail

		;register init handler
		_ldalo	#Reinit
		_ldxhi	#Reinit
		jsr		sdx_s_addiz

		;adjust MEMLO
		_ldalo	#__handler_end
		_ldxhi	#__handler_end
		bit		switches.u
		bmi		use_memlo
		_ldalo	#__handler_end_noultra
		_ldxhi	#__handler_end_noultra
		bit		switches.v
		bmi		use_memlo
		_ldalo	#__handler_end_novhold
		_ldxhi	#__handler_end_novhold
use_memlo:
		sta		memlo
		stx		memlo+1

		jsr		sdx_printf
		dta		'Altirra XEP80 handler V0.93 for SpartaDOS X loaded',$9B,0
		rts

fail:
		jsr		sdx_printf
		dta		'Failed to initialize XEP80 on port 2.',$9B,0
msg_portnum = *-4
		rts
.endp

		;replacement timing chain parameters for 50Hz mode
		;
		; horizontal scan = 12000000/(7*110) = 15.584KHz (ideal 15.625KHz)
		; vertical scan = 15.727KHz/312 = 49.95Hz
		;
		; horizontal blank to sync = (ideal 27.65 us / 7.4 chars)
		; horizontal sync = 8 chars (ideal 4.70 us / 8.05 chars)
		;
params_50hz:
		dta		$6D		;0	horizontal length = 110 characters
		dta		$54		;1	horizontal blank begin = 85 characters
		dta		$56		;2	horizontal sync begin = 87 characters
		dta		$60		;3	horizontal sync end = 95 characters
		dta		$A4		;4	character height = 11 scans, extra scans = 4
		dta		$1B		;5	vertical length = 28 rows
		dta		$18		;6	vertical blank begin = 25 rows
		dta		$46		;7	vertical sync scans 278-280
		dta		$16		;VINT after row 22
params_50hz_end:

.local switches
p		dta		0,'P'	;/P - default to PAL
u		dta		0,'U'	;/U - use ultra speed
v		dta		0,'V'	;/V - apply VHOLD timing changes
_1		dta		0,'1'	;/1 - use port 1
.endl

;===============================================================================
reloc_data smb '__reldat'

.proc	__reloc

relocad = $80
relcnt = $82

		;apply low byte relocations
		ldx		#$ff
		jsr		reloc

		;patch relocation routine to do high byte relocs
		lda		#0
		sta		hi_patch

		;apply high byte relocations
		jsr		reloc

		;run
		jmp		main

relbase:
		dta		a(BASEADDR)

reloc:
		mwa		relbase relocad
page_loop:
		inx
		lda		reloc_data,x
		beq		empty_page
		spl:rts
		sta		relcnt
reloc_loop:
		inx
		ldy		reloc_data,x
		lda		relbase
		clc
		bcc		lo_reloc
hi_patch = *-1
		inx
		adc		reloc_data,x
		lda		relbase+1
lo_reloc:
		adc		(relocad),y
		sta		(relocad),y
		dec		relcnt
		bne		reloc_loop
empty_page:
		inc		relocad+1
		bne		page_loop

		.endp

;===============================================================================

		blk		update address
