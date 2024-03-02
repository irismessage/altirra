;	Altirra - Atari 800/800XL/5200 emulator
;	Modular Kernel ROM - Decimal Floating-Point Math Pack
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

.macro	ckaddr
.if * != %%1
.error 'Address mismatch: ',*,' != ',%%1
.endif
.endm

;==========================================================================
; AFP [D800]	Convert ASCII string at INBUFF[CIX] to FR0
;
	org		$d800
_afp = afp
.proc afp
dotflag = _fr2
xinvert = _fr2+1
multemp = _fr2+2
sign = _fr2+3
digit2 = _fr2+4

	;zero fr0
	jsr		zfr0

	;clear decimal flag
	lda		#0
	sta		dotflag
	sta		digit2
	sta		sign
	
	;check for sign
	ldy		cix
	lda		(inbuff),y
	cmp		#'+'
	beq		isplus
	cmp		#'-'
	bne		postsign
	mva		#$80 sign
isplus:
	iny
	sty		cix
postsign:	
		
	lda		#$7f
	sta		fr0
	
	;grab digits left of decimal point
	ldx		#1
nextdigit:
	lda		(inbuff),y
	iny
	cmp		#'.'
	beq		isdot
	cmp		#'E'
	beq		isexp
	sec
	sbc		#'0'
	cmp		#10
	bcs		termcheck
	
	;write digit if we haven't exceeded digit count
	cpx		#6
	bcs		afterwrite
	
	bit		digit2
	bpl		writehi

	;clear second digit flag
	ror		digit2
	
	;merge in low digit
	ora		fr0,x
	sta		fr0,x
	
	;advance to next byte
	inx
	bne		afterwrite
	
writehi:
	;set second digit flag
	sec
	ror		digit2
	
	;shift digit to high nibble and write
	asl
	asl
	asl
	asl
	sta		fr0,x
	
afterwrite:
	;adjust digit exponent if we haven't seen a dot yet
	bit		dotflag
	smi:inc	fr0
	
	;go back for more
	jmp		nextdigit
	
isdot:
	lda		dotflag
	bne		termcheck
	mva		#$ff dotflag
	jmp		nextdigit

termcheck:
	dey
	cpy		cix
	beq		err
term:
	;stash offset
	sty		cix

	;divide digit exponent by two and merge in sign
	rol		sign
	ror		fr0
	
	;check if we need a one digit shift
	bcs		nodigitshift

	;shift right one digit
	jsr		fp_shr4

nodigitshift:
	jsr		fp_normalize

	clc
	rts

err:
	sec
	rts
	
isexp:
	cpy		cix
	beq		err

	;check for sign
	ldx		#0
	lda		(inbuff),y
	cmp		#'+'
	beq		isexpplus
	cmp		#'-'
	bne		postexpsign
	ldx		#$ff
isexpplus:
	iny
postexpsign:
	stx		xinvert
	
	;skip zeroes
skipexpzero:
	lda		(inbuff),y
	sec
	sbc		#'0'
	bne		notexpzero
	iny
	bne		skipexpzero
notexpzero:
	;better be a digit
	cmp		#10
	bcs		err
	
	;stash first digit
	tax
	
	;check for another digit
	lda		(inbuff),y
	sec
	sbc		#'0'
	bne		notexpzero2
	iny
	
	stx		multemp
	tax
	lda		fp_mul10,x
	clc
	adc		multemp
notexpzero2:
	
	;apply sign to exponent
	eor		xinvert
	sec
	sbc		xinvert

	;bias digit exponent
	clc
	adc		fr0
	sta		fr0
expterm:
	jmp		term

.print	'AFP Current address: ',*

.endp

fp_shr4:
	ldx		#4
digitshift:
	lsr		fr0+1
	ror		fr0+2
	ror		fr0+3
	ror		fr0+4
	ror		fr0+5
	dex
	bne		digitshift
	rts

fp_mul10:
	dta		0,10,20,30,40,50,60,70,80,90
	
.print	'Post-AFP Current address: ',*,' (',$d8e6-*,' bytes free)'

;==========================================================================
	org		$d8e6
_fasc = fasc
.proc fasc
dotcntr = _fr2
expflg = _fr2+1
absexp = _fr2+2
expval = _fr2+3
	jsr		ldbufa
	ldy		#0
	sty		expval
	
	;check if number is negative
	lda		fr0
	tax
	bpl		ispos
	and		#$7f
	sta		fr0
	tax
	lda		#'-'
	sta		(inbuff),y
	iny
ispos:

	;check if number is zero
	lda		fr0+1
	bne		notzero
	
	lda		#$b0
	sta		(inbuff),y
	rts

notzero:
	;check if we should go to exponential form (exp >= 10 or <=-3)
	txa
	sec
	sbc		#$3f
	cmp		#6
	bcc		noexp

	;reset exponent for number printing to 0 and stash exponent
	adc		#$fe
	asl
	sta		expval
	
	;check if mantissa is >10 -- if so, trim off a digit (we always
	;display 9 digits max)
	lda		fr0+1
	cmp		#$10
	bcc		noexplead
	jsr		fp_shr4
	inc		expval
noexplead:
	ldx		#$40
	stx		fr0

noexp:
	;check if number is less than 1.0
	txa
	sec
	sbc		#$40
	bcc		istiny
	
	;initialize dot counter
	;
	;  0.1 = 3F 10 00 00 00 00
	;  1.0 = 40 01 00 00 00 00
	; 10.0 = 40 10 00 00 00 00
	adc		#1
	sta		dotcntr

	;write out 10 digits by default (start offset 1)
	ldx		#1-6
	
	;check if number begins with a leading zero, and if so, skip high digit
	lda		fr0+1
	cmp		#$10
	bcs		nottiny
	dec		dotcntr
	bcc		writelow
	
istiny:
	lda		#'0'
	sta		(inbuff),y
	iny
	sta		dotcntr			;basically disables dot with a too-high value
	lda		#'.'
	sta		(inbuff),y
	iny
	
	ldx		#-5
	
leadzero:
nottiny:

digitloop:
	;check for dot before high digit
	dec		dotcntr
	bne		nohidot
	lda		#'.'
	sta		(inbuff),y
	iny
	
nohidot:

	;write out high digit
	lda		fr0+6,x
	lsr
	lsr
	lsr
	lsr
	ora		#$30
	sta		(inbuff),y
	iny
	
	;write out low digit
	lda		fr0+6,x
	and		#$0f
writelow:
	ora		#$30
	sta		(inbuff),y
	iny

	;next digit
	inx
	bne		digitloop
	
	;check if we should trim off zeroes (exp < $44)
	lda		fr0
	cmp		#$44
	bcs		lzterm

	;trim off leading zeroes
lzloop:
	dey
	lda		(inbuff),y
	cmp		#'0'
	beq		lzloop
lzterm:

	;trim off dot
	cmp		#'.'
	sne:dey

	lda		(inbuff),y

	;check if we have an exponent to deal with
	ldx		expval
	beq		noexp2

	iny
	
	;print an 'E'
	lda		#'E'
	sta		(inbuff),y
	iny
	
	;check for a negative exponent
	txa
	bpl		exppos
	eor		#$ff
	tax
	inx
	lda		#'-'
	bne		expneg
exppos:
	lda		#'+'
expneg:
	sta		(inbuff),y
	iny
	
	;print tens digit, if any
	txa
	sec
	ldx		#$2f
tensloop:
	inx
	sbc		#10
	bcs		tensloop
	pha
	txa
	sta		(inbuff),y
	iny
	pla
	adc		#$3a
noexp2:
	;set high bit on last char
	ora		#$80
	sta		(inbuff),y
	rts
	
.print	'FASC Current address: ',*
.endp

;==========================================================================
; IPF [D9AA]	Convert 16-bit integer at FR0 to FP
;
	org		$d9aa
.proc ipf
	sed

	ldx		#_fr2
	jsr		zf1
	
	ldy		#16
byteloop:
	rol		fr0
	rol		fr0+1
	ldx		#3
addloop:
	lda		_fr2,x
	adc		_fr2,x
	sta		_fr2,x
	dex
	bne		addloop
	dey
	bne		byteloop
	
	lda		#$42
	sta		_fr2
	
	ldx		#_fr2
	jsr		fld0r
	
	cld
	jmp		fp_normalize
	
.print	'IFP Current address: ',*,' -> $D9D2'
.endp

;==========================================================================
; FPI [D9D2]	Convert FR0 to 16-bit integer at FR0 with rounding
;
	org		$d9d2
.proc fpi
_acc0 = _fr2
_acc1 = _fr2+2
	
	;check if number is negative
	lda		fr0
	bmi		err
	
	;add denormalizing and rounding number
	ldy		#>drconst
	ldx		#<drconst
	jsr		fld1r
	jsr		fadd
	bcs		err
	lda		fr0
	
	;check if number is too big
	cmp		#$45
	bcs		err

	;clear temp accum
	lda		#0
	sta		_acc0
	sta		_acc0+1

	ldy		#5
shloop:
	;multiply accumulator by 10
	mva		_acc0+1 _acc1+1
	lda		_acc0
	sta		_acc1
	asl
	rol		_acc0+1
	bcs		err
	asl
	rol		_acc0+1
	bcs		err
	clc
	adc		_acc1
	sta		_acc0
	lda		_acc0+1
	adc		_acc1+1
	bcs		err
	sta		_acc0+1
	asl		_acc0
	rol		_acc0+1
	bcs		err

	;extract high digit
	lda		fr0+1
	and		#$0f
	
	;add digit to accumulator
	clc
	adc		_acc0
	sta		_acc0
	scc:inc	_acc0+1
	
	;shift BCD number left one digit
	ldx		#4
digloop:
	asl		fr0+3
	rol		fr0+2
	rol		fr0+1
	dex
	bne		digloop
	
	;loop until we've done all digits
	dey
	bne		shloop
	
	mwa		_acc0 fr0
	clc
	rts		
err:
	sec
	rts
	
drconst:
	dta		$42, $90, $00, $00, $50, $00
.print	'FPI Current address: ',*,' -> $DA44 (', $DA44-*, ' bytes left)'
.endp

;==========================================================================
; ZFR0 [DA44]	Zero FR0
; ZF1 [DA46]	Zero float at (X)
; ZFL [DA48]	Zero float at (X) with length Y (UNDOCUMENTED)
;
	org		$da44
zfr0:
	ldx		#fr0
	ckaddr	$da46
zf1:
	ldy		#6
	ckaddr	$da48
zfl:
	lda		#0
zflloop:
	sta		0,x
	inx
	dey
	bne		zflloop
	rts

;==========================================================================
; LDBUFA [DA51]	Set LBUFF to #INBUFF (UNDOCUMENTED)
;
	org		$da51
ldbufa:
	mwa		#lbuff inbuff
	rts

;==========================================================================
; FSUB [DA60]	Subtract FR1 from FR0; FR1 is altered
; FADD [DA66]	Add FR1 to FR0; FR1 is altered
	org		$da60
fadd = fsub._fadd
.proc fsub

_diffmode = fr1

	;toggle sign on FR1
	lda		fr1
	eor		#$80
	sta		fr1
	
	;fall through to FADD
	
	ckaddr	$da66
_fadd:
	;if fr1 is zero, we're done
	lda		fr1+1
	beq		xit
	
	;if fr0 is zero, swap
	lda		fr0+1
	beq		swap

	;compute difference in exponents, ignoring sign
	lda		fr0			;load fr0 sign
	eor		fr1			;compute fr0 ^ fr1 signs
	and		#$80		;mask to just sign
	tay
	eor		fr0			;flip fr0 sign to match fr1
	sec
	sbc		fr1			;compute difference in exponents
	bcs		noswap
	
	;swap FR0 and FR1
swap:
	ldx		#5
swaploop:
	lda		fr0,x
	tay
	lda		fr1,x
	sta		fr0,x
	tya
	sta		fr1,x
	dex
	bpl		swaploop
	
	;loop back and retry
	bmi		_fadd
	
noswap:
	;stash difference mode flag
	sty		_diffmode
	
	;check if we need to denormalize at all
	beq		nodenorm
	
	;check if FR1 is too small in magnitude to matter
	cmp		#6
	bcs		xit

	;denormalize FR1
	tay
denormloop:
	ldx		#4
denormloop2:
	mva		fr1,x fr1+1,x
	dex
	bne		denormloop2
	stx		fr1+1
	dey
	bne		denormloop
	
nodenorm:
	;jump to decimal mode and prepare for add/sub loops
	sed
	ldx		#5

	;check if we are doing a sum or a difference
	rol		_diffmode
	bcs		diff_subloop
	
	;add FR0 and FR1 mantissas
sum_addloop:
	lda		fr0,x
	adc		fr1,x
	sta		fr0,x
	dex
	bne		sum_addloop
		
	;check if we had a carry out
	bcc		xit

	;adjust exponent
	inc		fr0

	;shift down FR0
	ldx		#4
sum_shiftloop:
	lda		fr0,x
	sta		fr0+1,x
	dex
	bne		sum_shiftloop
	
	;add a $01 at the top
	inx
	stx		fr0+1
	
	;all done
	bne		xit

	;subtract FR0 and FR1 mantissas
diff_subloop:
	lda		fr0,x
	sbc		fr1,x
	sta		fr0,x
	dex
	bne		diff_subloop
	
	bcs		diff_noborrow
	jsr		fp_borrow
diff_noborrow:
	
	;normalize if necessary
	jsr		fp_normalize

xit:
	;exit decimal mode
	cld

	;all done
	clc
	rts

.print	'FADD/FSUB Current address: ',*,' -> $DADB (',$dadb-*,' bytes free)'

.endp

;==========================================================================
; FMUL [DADB]:	Multiply FR0 * FR1 -> FR0
;
	org		$dadb
.proc fmul

	;We use FR0:FR3 as a double-precision accumulator, and copy the
	;original multiplicand value in FR0 to FR1. The multiplier in
	;FR1 is converted to binary digit pairs into FR2.
	
_offset = _fr3+5
_offset2 = _fr2

	lda		fr0+1
	beq		xit
	lda		fr1+1
	bne		nonzero
	clc	
nonzero:

	;convert fr0 mantissa to binary
	jsr		fp_fr0tobin
	
	;compute new exponent and stash
	lda		fr0
	clc
	adc		fr1
	sec
	sbc		#$3f
	sta		fr0
				
	;clear accumulator through to exponent byte of fr1
	ldx		#fr0+1
	ldy		#12
	jsr		zfl
	
	;set up for 7 bits per digit pair (0-99 in 0-127)
	ldx		#7
	stx		_offset
	sed
	
	jsr		fp_fmul_innerloop

	;check if we need to shift up the fr0 mantissa
	lda		fr0+1
	bne		xit
	
	;drop exponent
	dec		fr0
	
	;shift mantissa bytes
	ldx		#-5
shiftloop:
	lda		fr0+7,x
	sta		fr0+6,x
	inx
	bne		shiftloop
xit:
	cld
	clc
oops:
	rts	
		
.print	'FMUL Current address: ',*,' -> $DB28 (', $db28-* ,' bytes free)'
.endp

;==========================================================================
; FDIV [DB28]	Divide FR0 / FR1 -> FR0
;
; 
;
	org		$db28
.proc fdiv
_digit = _fptemp0
_index = _fptemp1
	;check if dividend is zero
	lda		fr0+1
	beq		ok
	
	;check if divisor is zero
	lda		fr1+1
	beq		err
	
	ldx		#_fr2
	jsr		zf1
	
	jsr		fp_fdiv_init	
digitloop:
	;just keep going if we're accurate
	lda		fr0
	ora		fr0+1
	beq		nextdigit
	
	;check if we should either divide or add based on current sign (stored in carry)
	bcc		incloop

decloop:
	;increment quotient mantissa byte
	clc
	ldx		_index
	lda		_digit
uploop:
	adc		_fr2+6,x
	sta		_fr2+6,x
	lda		#0
	dex
	bcs		uploop

	;subtract mantissas
	ldx		#5
	sec
subloop:
	lda		fr0,x
	sbc		fr1,x
	sta		fr0,x
	dex
	bpl		subloop
	
	;keep going until we underflow
	bcs		decloop
	bcc		nextdigit
	
incloop:
	;decrement quotient mantissa byte
	sec
	ldx		_index
	lda		#0
	sbc		_digit
downloop:
	adc		_fr2+6,x
	sta		_fr2+6,x
	lda		#$99
	dex
	bcc		downloop
	
	;add mantissas
	ldx		#5
	clc
addloop:
	lda		fr0,x
	adc		fr1,x
	sta		fr0,x
	dex
	bpl		addloop
	
	;keep going until we overflow
	bcc		incloop	
	
nextdigit:
	;shift dividend (make sure to save carry state)
	php
	jsr		fp_fr0_shl4
	plp
	
	;next digit
	lda		_digit
	eor		#$11
	sta		_digit
	and		#$10
	beq		digitloop
	
	;next quo byte
	inc		_index
	bne		digitloop
	
	;move back to fr0
	ldx		#_fr2
	jsr		fld0r

	;normalize
	jsr		fp_normalize

	cld
ok:
	clc
	rts
err:
	sec
	rts
	
.print	'FDIV Current address: ',*,' -> $DBA1 (', $dba1-* ,' bytes free)'
.endp

;==========================================================================
; SKPSPC [DBA1]	Increment CIX while INBUFF[CIX] is a space
	org		$dba1
skpspc:
	ldy		cix
	lda		#' '
skpspc_loop:
	cmp		(inbuff),y
	bne		skpspc_xit
	iny
	bne		skpspc_loop
skpspc_xit:
	sty		cix
	rts

;==========================================================================
; ISDIGT [DBAF]	Check if INBUFF[CIX] is a digit (UNDOCUMENTED)
	org		$dbaf
isdigt = _isdigt
.proc _isdigt
	ldy		cix
	lda		(inbuff),y
	sec
	sbc		#'0'
	cmp		#10
	rts
.endp

.print	'ISDIGT Current address: ',*

;==========================================================================

.proc fp_fr0tobin
	ldx		#5
binloop:
	lda		fr0,x
	lsr
	lsr
	lsr
	lsr
	tay
	lda		fr0,x
	and		#$0f
	clc
	adc		fp_mul10,y
	sta		_fr2,x
	dex
	bne		binloop
	rts
.endp

;==========================================================================
; NORMALIZE [DC00]	Normalize FR0 (UNDOCUMENTED)
	org		$dc00
fp_normalize = normalize
normalize .proc
	ldy		#5
normloop:
	lda		fr0+1
	bne		xit
	dec		fr0
	ldx		#-4
normloop2:
	mva		fr0+6,x fr0+5,x
	inx
	bne		normloop2
	dey
	bne		normloop
	
	;Hmm, we shifted out everything... must be zero; reset exponent. This
	;is critical since Atari Basic depends on the exponent being zero for
	;a zero result.
	sty		fr0
xit:
	rts
.endp

;==========================================================================
; HELPER ROUTINES
;==========================================================================

.proc fp_fr0_shl4
	ldx		#4
bitloop:
	asl		fr0+5
	rol		fr0+4
	rol		fr0+3
	rol		fr0+2
	rol		fr0+1
	rol		fr0
	dex
	bne		bitloop
	rts
.endp

.proc fp_fdiv_init
	;compute new exponent
	lda		fr0
	sec
	sbc		fr1
	clc
	adc		#$40
	sta		_fr2
		
	ldx		#0
	stx		fr0
	stx		fr1
	
	jsr		fp_fdiv_tenscheck
	stx		fdiv._digit
	sed
	sec
	lda		#1-6
	sta		fdiv._index
	rts
.endp

.proc fp_fdiv_tenscheck
	inx	
	
	;check if dividend begins with a leading zero digit -- if so, shift it left 4
	;and begin with the tens digit
	lda		fr1+1
	cmp		#$10
	bcs		start_with_ones

	ldy		#4
bitloop:
	asl		fr1+5
	rol		fr1+4
	rol		fr1+3
	rol		fr1+2
	rol		fr1+1
	dey
	bne		bitloop

	ldx		#$10
	
start_with_ones:
	rts
.endp

.proc fp_borrow
	ldx		#5
	sec
diff_borrow:
	lda		#0
	sbc		fr0,x
	sta		fr0,x
	dex
	bne		diff_borrow
	lda		#$80
	eor		fr0
	sta		fr0
	rts
.endp

;--------------------------------------------------------------------------
.proc fp_fmul_innerloop
_offset = _fr3+5
_offset2 = _fr2

	;begin outer loop -- this is where we process one _bit_ out of each
	;multiplier byte in FR2's mantissa (note that this is inverted in that
	;it is bytes-in-bits instead of bits-in-bytes)
offloop:

	;begin inner loop -- here we process the same bit in each multiplier
	;byte, going from byte 5 down to byte 1
	ldx		#5
offloop2:
	;shift a bit out of fr1 mantissa
	lsr		_fr2,x
	bcc		noadd
			
	;add fr1 to fr0 at offset
	stx		_offset2
	ldy		#5
	clc
addloop:
	lda		fr1,y
	adc		fr0+5,x
	sta		fr0+5,x
	dex
	dey
	bpl		addloop
	
	;check if we have a carry out to the upper bytes
	bcc		no_carry
carryloop:
	lda		fr0+5,x
	adc		#0
	sta		fr0+5,x
	dex
	bcs		carryloop
no_carry:

	;restore byte offset
	ldx		_offset2
	
noadd:
	;go back for next byte
	dex
	bne		offloop2

	;double fr1
	ldx		#5
	clc
doublefr1:
	lda		fr1,x
	adc		fr1,x
	sta		fr1,x
	dex
	bpl		doublefr1

	;loop back until all mantissa bytes finished
	dec		_offset
	bne		offloop

	;all done
	rts
.endp

.print	'Pre-PLYEVL Current address: ',*,' -> $DD40 (', $dd40-* ,' bytes free)'

;==========================================================================
; PLYEVL [DD40]	Eval polynomial at (X:Y) with A coefficients using FR0
;
	org		$dd40
.proc plyevl
	;stash arguments
	stx		_flptr2
	sty		_flptr2+1
	sta		_fpcount
	
	;copy FR0 -> PLYARG
	ldx		#<plyarg
	ldy		#>plyarg
	jsr		fst0r
	
	jsr		zfr0
	
	;enter while loop
	jmp		loop0
	
loop:
	;copy PLYARG -> FR1
	ldx		#<plyarg
	ldy		#>plyarg
	jsr		fld1r
	
	;multiply accumulator by Z
	jsr		fmul
	bcs		xit

loop0:	
	;load next coefficient and increment coptr
	lda		_flptr2
	tax
	clc
	adc		#6
	sta		_flptr2
	ldy		_flptr2+1
	scc:inc	_flptr2+1
	jsr		fld1r

	;add coefficient to acc
	jsr		fadd
	bcs		xit

	dec		_fpcount
	bne		loop
xit:
	rts
	
.print	'PLYEVL Current address: ',*,' -> $DD89 (', $dd89-* ,' bytes free)'
.endp

;==========================================================================
; FLD0R [DD89]	Load FR0 from (X:Y)
; FLD0P [DD8D]	Load FR0 from (FLPTR)
;
	org		$dd89
fld0r:
	stx		flptr
	sty		flptr+1
	ckaddr	$dd8d
fld0p:
	ldy		#5
fld0ploop:
	lda		(flptr),y
	sta		fr0,y
	dey
	bpl		fld0ploop
	rts

;==========================================================================
; FLD1R [DD98]	Load FR1 from (X:Y)
; FLD1P [DD9C]	Load FR1 from (FLPTR)
;
	org		$dd98
fld1r:
	stx		flptr
	sty		flptr+1
	ckaddr	$dd9c
fld1p:
	ldy		#5
fld1ploop:
	lda		(flptr),y
	sta		fr1,y
	dey
	bpl		fld1ploop
	rts

;==========================================================================
; FST0R [DD98]	Store FR0 to (X:Y)
; FST0P [DD9C]	Store FR0 to (FLPTR)
;
	org		$dda7
fst0r:
	stx		flptr
	sty		flptr+1
	ckaddr	$ddab
fst0p:
	ldy		#5
fst0ploop:
	lda		fr0,y
	sta		(flptr),y
	dey
	bpl		fst0ploop
	rts

;==========================================================================
; FMOVE [DDB6]	Move FR0 to FR1
;
	org		$ddb6
fmove:
	ldx		#5
fmoveloop:
	lda		fr0,x
	sta		fr1,x
	dex
	bpl		fmoveloop
	rts

;==========================================================================
; EXP [DDC0]	Compute e^x
; EXP10 [DDCC]	Compute 10^x
;
	org		$ddc0
exp10 = exp._exp10
.proc exp
	ldx		#<log10_e
	ldy		#>log10_e
	jsr		fld1r
	jsr		fmul
	bcs		err2

	ckaddr	$ddcc
_exp10:
	;stash sign and compute abs
	lda		fr0
	and		#$80
	sta		_fptemp1
	eor		fr0
	sta		fr0
	
	;compute floor(x)
	ldx		#_fr3
	ldy		#0
	jsr		fst0r
	jsr		fpi
	bcs		err2
		
	;check for exponent being too big
	lda		fr0+1
	bne		err
	
	lda		fr0
	cmp		#196
	bcs		err
	
	;stash integer exponent
	sta		_fptemp0
	
	;compute y = x - floor(x)
	jsr		ipf
	jsr		fmove
	ldx		#_fr3
	ldy		#0
	jsr		fld0r
	jsr		fsub
	
	;compute approximation z = 10^y
	ldx		#<coeff
	ldy		#>coeff
	lda		#10
	jsr		plyevl

	;tweak exponent
	lsr		_fptemp0
	
	;scale by 10 if necessary
	bcc		even
	ldx		#<ten
	ldy		#>ten
	jsr		fld1r
	jsr		fmul
	bcs		err2
even:

	;bias exponent
	lda		_fptemp0
	adc		fr0
	cmp		#64+49
	bcs		err2
	sta		fr0
	
	;check if we should invert
	rol		_fptemp1
	bcc		xit2
	
	jsr		fmove
	ldx		#<fp_one
	ldy		#>fp_one
	jsr		fld0r
	jsr		fdiv
	rts
err:
	sec
err2:
xit2:
	rts
	
log10_e .fl 0.43429448190325182765112891891661
ten:
	.fl		10

coeff:		;Maclaurin series for 10^-x, 10 coefficients
	.fl		0.0050139288
	.fl		0.0195976946
	.fl		0.0680893651
	.fl		0.2069958487
	.fl		0.5393829292
	.fl		1.1712551489
	.fl		2.0346785923
	.fl		2.6509490552
	.fl		2.3025850930
	.fl		1.0000000000

.print	'EXP10 Current address: ',*,' -> $DE95 (', $DE95-* ,' bytes free)'
.endp	

;==========================================================================
; REDRNG [DE95]	Reduce range via y = (x-1)/(x+1) (undocumented)
;
	org		$de95
redrng = _redrng
.proc _redrng
	ldx		#<_fr2
	ldy		#>_fr2
	jsr		fst0r
	ldx		#<fp_one
	ldy		#>fp_one
	jsr		fld1r
	jsr		fadd
	bcs		fail
	ldx		#<_fr3
	ldy		#>_fr3
	jsr		fst0r
	ldx		#<_fr2
	ldy		#>_fr2
	jsr		fld0r
	ldx		#<fp_one
	ldy		#>fp_one
	jsr		fld1r
	jsr		fsub
	bcs		fail
	ldx		#<_fr3
	ldy		#>_fr3
	jsr		fld1r
	jsr		fdiv
fail:
	rts
.endp
.print	'RNGRED Current address: ',*,' -> $DECD (', $DECD-* ,' bytes free)'

;==========================================================================
; LOG [DECD]	Compute ln x
; LOG10 [DED1]	Compute log10 x
;
	org		$decd
log10 = log._log10
.proc log
	lsr		_fptemp1
	bpl		entry
	ckaddr	$ded1
_log10:
	sec
	rol		_fptemp1
entry:
	;throw error on negative number
	lda		fr0
	bmi		err		
	
	;stash exponentx2 and reset
	asl
	sta		_fptemp0
	lda		#$3f
	sta		fr0
	
	;check if we can pull out another power of 10
	lda		fr0+1
	beq		err
	
	cmp		#10
	bcs		noextra
	
	;shift left one digit
	inc		_fptemp0
	ldy		#4
shloop:
	ldx		#5
shloop2:
	rol		fr0,x
	dex
	bne		shloop2
	dey
	bne		shloop
	
noextra:
	;subtract one
	ldx		#<fp_one
	ldy		#>fp_one
	jsr		fld1r
	jsr		fsub
	
	;do polynomial expansion
	ldx		#<coeff
	ldy		#>coeff
	lda		#10
	jsr		plyevl
	bcs		err2
	
	;stash
	jsr		fmove
	
	;convert exponent back to float
	lda		_fptemp0
	sta		fr0
	lda		#0
	sta		fr0+1
	jsr		ipf
	
	;merge
	jsr		fadd
	bcs		err2
	
	;scale if doing log
	bit		_fptemp1
	bmi		xit2
	
	ldx		#<ln10
	ldy		#>ln10
	jsr		fld1r
	jsr		fmul
	
xit2:
	rts
err:
	sec
err2:
	rts
		
ln10:
	.fl		2.3025850929940456840179914546844
	
;==========================================================================
.print 'pre-HALF Current address: ',*
	org		$df6c
half:
	.fl		0.5
	
coeff:		;Maclaurin expansion for log10(1+x)
	.fl		0.0394813165
	.fl		-0.0434294482
	.fl		0.0482549424
	.fl		-0.0542868102
	.fl		0.0620420688
	.fl		-0.0723824137
	.fl		0.0868588964
	.fl		-0.1085736205
	.fl		0.1447648273
	.fl		-128

.print 'LOG10 Current address: ',*
.endp

;==========================================================================
.print 'Math pack current address: ',*,' (',$dfae-*,' bytes free)'

	org		$dfae
atncoef:	;coefficients for atn(x) ~= f(x^2)
			;see Abramowitz & Stegun 4.4.49
	.fl		0
	.fl		0	
	.fl		0.0028662257
	.fl		-0.0161657367
	.fl		0.0492096138
	.fl		-0.0752896400
	.fl		0.1065626393
	.fl		-0.1420889944
	.fl		0.1999355085
	.fl		-0.3333314528
fp_one:
	.fl		1.0				;also an arctan coeff
	org		$dff0
fp_pi4:	;pi/4 - needed by Atari Basic ATN()
	.fl		0.78539816339744830961566084581988
