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
		ATAccelFSUB(*mpCPU, *mpMemory);
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
