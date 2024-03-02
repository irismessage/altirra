; Altirra BASIC
; Copyright (C) 2014 Avery Lee, All Rights Reserved.
;
; Copying and distribution of this file, with or without modification,
; are permitted in any medium without royalty provided the copyright
; notice and this notice are preserved.  This file is offered as-is,
; without any warranty.

		icl		'system.inc'
		icl		'tokens.inc'

;===========================================================================
; Zero page variables
;
; We try to be sort of compatible with Atari BASIC here, supporting all
; public variables and trying to support some unofficial usage as well.
;
; Test cases:
;	QUADRATO.BAS
;	- Uses $B0-B3 from USR() routine

		org		$0080
		opt		o-
argstk	equ		*
lomem	dta		a(0)		;$0080 (compat) from lomem; argument/operator stack
vntp	dta		a(0)		;$0082 (compat - loaded) variable name table pointer
vntd	dta		a(0)		;$0084 (compat - loaded) variable name table end
vvtp	dta		a(0)		;$0086 (compat - loaded) variable value table pointer
stmtab	dta		a(0)		;$0088 (compat - loaded) statement table pointer
stmcur	dta		a(0)		;$008A (compat - loaded) current statement pointer
starp	dta		a(0)		;$008C (compat - loaded) string and array table
runstk	dta		a(0)		;$008E (compat) runtime stack pointer
memtop2	dta		a(0)		;$0090 (compat) top of BASIC memory

exLineOffset	dta		0		;offset within current line being executed
exLineOffsetNxt	dta		0		;offset of next statement
exLineEnd		dta		0		;offset of end of current line
exTrapLine		dta		a(0)	;TRAP line
opsp		dta		0		;operand stack pointer offset
argsp		dta		0		;argument stack pointer offset
expCurOp	dta		0		;expression evaluator current operator
expCurPrec	dta		0		;expression evaluator current operator precedence
expCommas	dta		0		;expression evaluator comma count
expFCommas	dta		0
expAsnCtx	dta		0		;flag - set if this is an assignment context for arrays
varptr		dta		a(0)	;pointer to current variable
lvarptr		dta		a(0)	;lvar pointer for array assignment
parptr		dta		a(0)	;parsing state machine pointer
parout		dta		0		;parsing output idx
grColor		dta		0		;graphics color
iocbexec	dta		0		;current immediate/deferred mode IOCB
iocbidx		dta		0		;current IOCB
iocbcmd		dta		0		;IOCB command
iterPtr		dta		a(0)	;pointer used for sequential name table indexing
ioPrintCol	dta		0		;IO: current PRINT column
ioTermSave	dta		0		;IO: String terminator byte save location
ioTermOff	dta		0		;IO: String terminator byte offset

		.if *>$b0
		.error "Zero page overflow: ",*
		.endif

stopln	= $ba				;(compat - Atari BASIC manual): line number of error
		; $bb
		
;--------------------------------------------------------------------------
; $BC-BF are reserved as scratch space for use by the currently executing
; statement or by the parser. They must not be used by functions or library
; code.
;
stScratch	= $bc
stScratch2	dta		0
stScratch3	dta		0
stScratch4	dta		0

printDngl	= stScratch		;set if the print statement is 'dangling' - no follow EOL
parStrType	= stScratch		;parsing string type: set if string exp, clear if numeric
parStBegin	= stScratch2	;parsing offset of statement begin (0 if none)

;--------------------------------------------------------------------------
; $C0-C1 are reserved as scratch space for use by the currently executing
; function.
;
funScratch1	= $c0
funScratch2	= $c1
;--------------------------------------------------------------------------
errno	= $c2
errsave	= $c3				;(compat - Atari BASIC manual): error number

			org		$c4
dataln		dta		a(0)	;current DATA statement line
dataptr		dta		a(0)	;current DATA statement pointer
dataoff		dta		0		;current DATA statement offset
dataLnEnd	dta		0		;current DATA statement line end

;--------------------------------------------------------------------------
; $CB-D1 are reserved for use by annoying people that read Mapping The
; Atari.
;--------------------------------------------------------------------------
; Floating-point library vars
;
; $D2-D3 is used as an extension prefix to FR0; $D4-FF are used by the FP
; library, but can be reused outside of it.
;
prefr0	= fr0-2
a0		= fr0				;temporary pointer 0
a1		= fr0+2				;temporary pointer 1
a2		= fr0+4				;temporary pointer 2
a3		= fr0+6				;temporary pointer 3
a4		= fr0+8				;temporary pointer 4
a5		= fr0+10			;temporary pointer 5

degflg	= $fb				;(compat) degree/radian flag: 0 for radians, 6 for degrees

lbuff	equ		$0580

.macro _ERROR_RETURN
		jmp		errorBadRETURN
.endm

.macro _STATIC_ASSERT
		.if :1
		.else
		.error ":2"
		.endif
.endm

		.if CART==0
		org		$2800
		opt		o+
		
.proc __preloader
		;check if BASIC is on
		ldx		$a000
		inx
		stx		$a000
		dex
		cmp		$a000
		stx		$a000
		beq		basic_ok
		
		;turn basic off
		lda		#0
		sta		basicf
		lda		portb
		ora		#2
		sta		portb
		
basic_ok:
		;reset RAMTOP
		mva		#$a0 ramtop
		
		;reinitialize GR.0 screen if needed (XEP80 doesn't)
		lda		sdmctl
		and		#$20
		beq		dma_off
		
		mva		#CIOCmdClose iccmd
		ldx		#0
		jsr		ciov
dma_off:

		rts
.endp

		ini		__preloader

.proc __loader		
		mva		#CIOCmdOpen iccmd
		mwa		#editor icbll
		mva		#$0c icax1
		mva		#$00 icax2
		ldx		#0
		jsr		ciov
		jmp		main
editor:
		dta		c'E',$9B
.endp
		.endif
		
		org		$a000
		
		.if CART
		opt		h-o+f+
		.else
		opt		o+
		.endif

;==========================================================================
main:
		;init I/O
		ldx		#0
		stx		iocbidx
		
		;check if this is a warm start
		bit		warmst
		bmi		immediateMode

		;initialize LOMEM from MEMLO
		mwa		memlo	lomem
				
		;print banner
		jsr		imprint
		dta		$9B,c'Altirra 8K BASIC 0.8',$9b,0
		
		jmp		stNew

immediateModeReset:
		jsr		ExecReset
immediateMode:
		;use IOCB #0 (E:) for commands
		mva		#0 iocbexec
.proc execLoop
		;reset stack
		ldx		#$ff
		txs

loop:
		;display prompt
		ldx		#0
		stx		iocbidx
		jsr		imprint
		dta		$9B,'Ready',$9B,0

loop2:		
		;read line
		ldx		iocbexec
		jsr		IoSetupReadLine
		jsr		ciov
		
		;check if we got an EOF
		cpy		#$88
		beq		eof
		
		tya
		jsr		ioCheck
		
		;check for an empty line
		jsr		ldbufa
		mva		#0 cix
		jsr		skpspc
		lda		(inbuff),y
		cmp		#$9b
		beq		loop2
		
		;##TRACE "Parsing immediate mode line: [%.*s]" dw(icbll) lbuff
		jsr		parseLine
		
		;check if this line was immediate mode
		ldy		#1
		lda		(stmcur),y
		bpl		loop2
		
		;execute immediate mode line
		sec
		jmp		exec
		
eof:
		;close IOCB #7
		jsr		IoCloseX
		
		;restart in immediate mode
		jmp		immediateMode
.endp

;==========================================================================

		icl		'exec.s'
		icl		'data.s'
		icl		'statements.s'
		icl		'evaluator.s'
		icl		'functions.s'
		icl		'variables.s'
		icl		'math.s'
		icl		'parser.s'
		icl		'parserbytecode.s'
		icl		'io.s'
		icl		'memory.s'
		icl		'list.s'
		icl		'error.s'
		icl		'util.s'

;==========================================================================

		.echo	"Main program ends at ",*," (",[((((*-$a000)*100/8192)/10)*16+(((*-$a000)*100)/8192)%10)],"% full)"

		org		$bffa - 16
		.echo	"Constant table begins at ",*
		.pages 1

devname_c:
		dta		'C'
devname_s:
		dta		'S'
devname_e:
		dta		'E'
devname_p:
		dta		'P'
		;next char must not be a digit

angle_conv_tab:
		.fl		1.57079633
		.fl		90
		.endpg
		
;==========================================================================
		
		.echo	"Program ends at ",*," (",[((((*-$a000)*100/8192)/10)*16+(((*-$a000)*100)/8192)%10)],"% full)"

		.if CART
		org		$bffa
		dta		a(main)			;boot vector
		dta		$00				;do not init
		dta		$05				;boot disk/tape, boot cart
		dta		a(ExNop)		;init vector (no-op)
		.else
		run		__loader
		.endif
		
		end
