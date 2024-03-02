;	Altirra - Atari 800/800XL/5200 emulator
;	Replacement XEP80 Handler Firmware - E:/S: Device Handler
;	Copyright (C) 2008-2020 Avery Lee
;
;	Copying and distribution of this file, with or without modification,
;	are permitted in any medium without royalty provided the copyright
;	notice and this notice are preserved.  This file is offered as-is,
;	without any warranty.

;===============================================================================

.ifndef XEP_SDX
	XEP_SDX = 0
.endif

;-------------------------------------------------------------------------------
; XEP_OPTION_ULTRA
;
; Enables symmetric 31.5KHz send/receive. Requires XEP_OPTION_TURBO=1.
;
.ifndef XEP_OPTION_ULTRA
	XEP_OPTION_ULTRA = 0
.endif

;-------------------------------------------------------------------------------
; XEP_DEFAULT_50HZ
;
; Issues extra commands to the XEP80 after master reset to switch to a 50Hz
; text display.
;
.ifndef XEP_DEFAULT_50HZ
	XEP_DEFAULT_50HZ = 0
.endif
