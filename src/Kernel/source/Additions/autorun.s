; Altirra - Additions AUTORUN.SYS module
; Copyright (C) 2014-2015 Avery Lee, All Rights Reserved.
;
; Permission is hereby granted, free of charge, to any person obtaining a copy
; of this software and associated documentation files (the "Software"), to deal
; in the Software without restriction, including without limitation the rights
; to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
; copies of the Software, and to permit persons to whom the Software is
; furnished to do so, subject to the following conditions:
;
; The above copyright notice and this permission notice shall be included in
; all copies or substantial portions of the Software.
;
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
; IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
; FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
; AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
; LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
; OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
; SOFTWARE. 

		icl		'hardware.inc'
		icl		'kerneldb.inc'
		icl		'cio.inc'
		icl		'sio.inc'

runad	equ		$02e0
initad	equ		$02e2
dskinv	equ		$e453
ciov	equ		$e456

		org		$2400

;==========================================================================
.proc main
		mva		#CIOCmdPutChars iccmd
		mwa		#message icbal
		mwa		#[.len message] icbll
		ldx		#0
		jmp		ciov

cio_command:
		dta		CIOCmdPutChars,0,a(message),a(.len message)
.endp

;==========================================================================
.proc message
		;		 01234567890123456789012345678901234567
		dta		$7D
		dta		'Altirra Additions disk'+$80,$9B
		dta		'This disk contains helper software to',$9B
		dta		'use with emulated peripherals, such',$9B
		dta		'as R: and T: handlers. See Help for',$9B
		dta		'details on the contents of the',$9B
		dta		'Additions disk.',$9B
		dta		$9B
		dta		'Note that while this disk contains a',$9B
		dta		'mostly compatible DOS 2 replacement,',$9B
		dta		'it',$27,'s still very buggy, so use with',$9B
		dta		'at your own risk.',$9B
		dta		$9B
		dta		'If you have built-in BASIC enabled,',$9B
		dta		'use DOS to enter the CP and CART to',$9B
		dta		'restart BASIC.',$9B
		dta		$9B
.endp

		run		main
