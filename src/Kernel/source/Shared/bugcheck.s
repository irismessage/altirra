;	Altirra - Atari 800/800XL/5200 emulator
;	Modular Kernel ROM - Fatal Exception Handler
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

.proc BugCheck
	; turn off all interrupts
	sei
	lda		#$00
	sta		nmien
	cmp:rne	vcount

	;reset ANTIC to show display
	sta		chactl
	sta		colbk
	mva		#$e1 chbase
	sta		dmactl
	mva		#$82 colpf2
	mva		#$0f colpf1
	lda		#<dlist
	sta		dlistl
	lda		#>dlist
	sta		dlisth
		
	;lock up cpu
	bne		*
	
	.pages 1
dlist:
	:3 dta $70
	dta		$42,a(text)
	dta		$41,a(dlist)
text:
	dta		d'Unimplemented command           '

	.endpg
.endp
