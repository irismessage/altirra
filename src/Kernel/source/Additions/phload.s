;	Altirra - Atari 800/800XL/5200 emulator
;	Additions - peripheral handler loader
;	Copyright (C) 2008-2023 Avery Lee
;
;	Copying and distribution of this file, with or without modification,
;	are permitted in any medium without royalty provided the copyright
;	notice and this notice are preserved.  This file is offered as-is,
;	without any warranty.

		icl		'hardware.inc'
		icl		'kerneldb.inc'
		icl		'cio.inc'

ciov	equ		$e456
siov	equ		$e459

devname = $80
hndsize = $82
ptr		= $84

		org		$3e00

;==========================================================================
;Type 4 polling peripheral handler loader
;
;This program exercises CIO handler loading in the XL/XE OS. It requires
;a device that supports type 4 polls. The program iterates over A-Z: and
;loads any peripheral handlers it finds through the OS. This program will
;only work if the peripheral handler support code is intact in the OS and
;hasn't been removed in a modified OS ROM.
;
;This tool mainly exists to exercise the OS functionality, since there are
;no known programs that support loading handlers through CIO. However, it
;is of little practical use since there are also no known devices that
;support type 4 polls besides the test device in Altirra.
;
main:
		;print banner
		ldx		#256-[.len banner]
banner_loop:
		lda		banner+[.len banner]-256,x
		jsr		putchar
		inx
		bne		banner_loop

		;scan A-Z: with handler loading enabled
		mva		#'A' devname
		mva		#$9B devname+1

dev_loop:
		lda		devname

		;check if this device is already registered with CIO
		ldx		#11*3
find_loop:
		cmp		hatabs,x
		beq		skip_dev
		dex
		dex
		dex
		bpl		find_loop

		;nope, issue type 4 poll ($4F/40/devname/devnumber) -- we do this
		;to double check as some OSes may simulate devices not in HATABS
		jsr		do_type4_poll
		tya
		bmi		skip_dev

		;print device letter
		lda		devname
		jsr		putchar

		;do CIO provisional open
		jsr		do_provisional_open
		bmi		open_failed

		;try to load handler
		jsr		try_load_handler

		;if we didn't get nonexistent device (130), adjust MEMLO
		cpy		#CIOStatUnkDevice
		beq		open_failed

		;patch the handler size (zeroed by CIO for a type 4 load)
		mwa		chlink ptr
		ldy		#16
		mva		hndsize (ptr),y+
		mva		hndsize+1 (ptr),y

		;update handler entry checksum
		dey
		clc
		adc		(ptr),y-
		adc		(ptr),y
		adc		#0
		sta		(ptr),y

		;update memlo with handler size, evening it before (also done by
		;the relocating loader in the OS)
		lda		memlo
		lsr
		lda		memlo
		adc		hndsize
		sta		memlo
		lda		memlo+1
		adc		hndsize+1
		sta		memlo+1

open_failed:
		;unconditionally close
		mva		#CIOCmdClose iccmd+$70
		jsr		ciov
		jmp		next_dev

skip_dev:
		lda		#'.'
		jsr		putchar

next_dev:
		inc		devname
		lda		devname
		cmp		#'Z'+1
		bne		dev_loop

		;print EOL and exit
		lda		#$9b
putchar:
		sta		ciochr
		txa
		pha
		jsr		putchar2
		pla
		tax
		rts

.local banner
		dta		'Scanning: '
.endl


putchar2:
		lda		icax1
		sta		icax1z
		lda		icax2
		sta		icax2z
		lda		icpth
		pha
		lda		icptl
		pha
		ldx		#0
		lda		ciochr
		rts

;==============================================================================
do_type4_poll:
		ldx		#11
		mva:rpl	type4_dcb,x ddevic,x-

		mva		devname daux1
		jmp		siov

type4_dcb:
		dta		$4F,$01,$40,$40,a(dvstat),$40,$00,$04,$00,$00,$01

;==============================================================================
do_provisional_open:
		;enable provisional handler load
		mva		#$ff hndlod

		ldx		#5
		mva:rpl	open_iocb,x iccmd+$70,x-
		inx
		stx		icax1+$70
		stx		icax2+$70
		ldx		#$70
		jmp		ciov

open_iocb:
		dta		$03,$00,a(devname),a(2)

;==============================================================================
try_load_handler:
		;save off handler size
		mwa		dvstat hndsize

		;set handler load address to memlo
		mwa		memlo dvstat+2

		;issue status command to force handler load
		mva		#$ff hndlod
		mva		#CIOCmdGetStatus iccmd+$70
		jmp		ciov

;==============================================================================
		run		main
