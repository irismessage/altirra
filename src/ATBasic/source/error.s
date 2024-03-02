; Altirra BASIC - Error handling module
; Copyright (C) 2014 Avery Lee, All Rights Reserved.
;
; Copying and distribution of this file, with or without modification,
; are permitted in any medium without royalty provided the copyright
; notice and this notice are preserved.  This file is offered as-is,
; without any warranty.

errorBadDeviceNo	inc		errno
errorLoadError		inc		errno
errorInvalidString	inc		errno
errorWTF			inc		errno		;17
errorBadRETURN		inc		errno
errorGOSUBFORGone	inc		errno
errorLineTooLong	inc		errno
errorNoMatchingFOR	inc		errno
errorLineNotFound	inc		errno		;12
errorFPError		inc		errno		;11
errorArgStkOverflow	inc		errno
errorDimError		inc		errno		;9
errorInputStmt		inc		errno
errorValue32K		inc		errno
errorOutOfData		inc		errno
errorStringLength	inc		errno		;5
errorTooManyVars	inc		errno
errorValueErr		inc		errno
errorNoMemory		inc		errno		;2
.nowarn .proc errorDispatch
		;##TRACE "Error %u at line %u" db(errno) dw(dw(stmcur))
		;restore stack
		ldx		#$ff
		txs
		
		;clear BREAK flag in case that's what caused us to stop
		stx		brkkey
		
		;save off error
		lda		errno
		sta		fr0
		sta		errsave
		
		;set stop line
		ldy		#0
		sty		dspflg			;force off list flag while we have a zero
		sty		fr0+1
		sty		iocbidx
		lda		(stmcur),y
		sta		stopln
		iny
		lda		(stmcur),y
		sta		stopln+1
		sty		errno			;reset errno to 1
				
		;check if we have a trap line
		lda		exTrapLine+1
		bmi		no_trap
		
		sta		fr0+1
		lda		exTrapLine
		sta		fr0
		
		;reset trap line
		sec
		ror		exTrapLine+1
		
		;goto trap line
		jmp		stGoto.gotoFR0Int
		
no_trap:
		;ERROR-   11
		jsr		imprint
		dta		c"ERROR-   ",0
		
		jsr		IoPrintInt
		jsr		IoPutNewline
		jmp		immediateMode
.endp