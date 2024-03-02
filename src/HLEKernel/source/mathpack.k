;	Altirra - Atari 800/800XL emulator
;	Kernel ROM replacement
;	Copyright (C) 2008-2009 Avery Lee
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

	org		$d800
afp:
	#{
		ATAccelAFP(*mpCPU, *mpMemory);
	#}
	rts

	org		$d8e6
fasc:
	#{
		ATAccelFASC(*mpCPU, *mpMemory);
	#}
	rts

	org		$d9aa
ipf:
	#{
		ATAccelIPF(*mpCPU, *mpMemory);
	#}
	rts

	org		$d9d2
fpi:
	#{
		ATAccelFPI(*mpCPU, *mpMemory);
	#}
	rts

	org		$da44
zfr0:
	ldx		#fr0
	org		$da46
zf1:
	#{
		ATAccelZF1(*mpCPU, *mpMemory);
	#}
	rts

	org		$da51
ldbufa:
	#{
		ATAccelLDBUFA(*mpCPU, *mpMemory);
	#}
	rts

	org		$da60
fsub:
	#{
		ATAccelFSUB(*mpCPU, *mpMemory);
	#}
	rts

	org		$da66
fadd:
	#{
		ATAccelFADD(*mpCPU, *mpMemory);
	#}
	rts

	org		$dadb
fmul:
	#{
		ATAccelFMUL(*mpCPU, *mpMemory);
	#}
	rts

	org		$db28
fdiv:
	#{
		ATAccelFDIV(*mpCPU, *mpMemory);
	#}
	rts

	org		$dba1
skpspc:
	#{
		ATAccelSKPSPC(*mpCPU, *mpMemory);
	#}
	rts

	org		$dbaf
isdigt:
	#{
		ATAccelISDIGT(*mpCPU, *mpMemory);
	#}
	rts

	org		$dc00
normalize:
	#{
		ATAccelNORMALIZE(*mpCPU, *mpMemory);
	#}
	rts

	org		$dd40
plyevl:
	#{
		ATAccelPLYEVL(*mpCPU, *mpMemory);
	#}
	rts

	org		$dd89
fld0r:
	#{
		ATAccelFLD0R(*mpCPU, *mpMemory);
	#}
	rts
		
	org		$dd8d
fld0p:
	#{
		ATAccelFLD0P(*mpCPU, *mpMemory);
	#}
	rts

	org		$dd98
fld1r:
	#{
		ATAccelFLD1R(*mpCPU, *mpMemory);
	#}
	rts

	org		$dd9c
fld1p:
	#{
		ATAccelFLD1P(*mpCPU, *mpMemory);
	#}
	rts

	org		$dda7
fst0r:
	#{
		ATAccelFST0R(*mpCPU, *mpMemory);
	#}
	rts

	org		$ddab
fst0p:
	#{
		ATAccelFST0P(*mpCPU, *mpMemory);
	#}
	rts

	org		$ddb6
fmove:
	#{
		ATAccelFMOVE(*mpCPU, *mpMemory);
	#}
	rts

	org		$ddc0
exp:
	#{
		ATAccelEXP(*mpCPU, *mpMemory);
	#}
	rts

	org		$ddcc
exp10:
	#{
		ATAccelEXP10(*mpCPU, *mpMemory);
	#}
	rts

	org		$de95
redrng:
	#{
		ATAccelREDRNG(*mpCPU, *mpMemory);
	#}
	rts

	org		$decd
log:
	#{
		ATAccelLOG(*mpCPU, *mpMemory);
	#}
	rts

	org		$ded1
log10:
	#{
		ATAccelLOG10(*mpCPU, *mpMemory);
	#}
	rts

;===========================================================
; HALF[DF6C] - Atari Basic 0.5 constant (undocumented)
	org		$df6c
half:
	.fl		0.5


;===========================================================
; _ATNCOEF - Atari Basic ATN() coefficients (undocumented)
;

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
	.fl		1.0

	org		$dff0
pi4:		;pi/4
	.fl		0.78539816339744830961566084581988
