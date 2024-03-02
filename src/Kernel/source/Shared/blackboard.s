;	Altirra - Atari 800/800XL emulator
;	Kernel ROM replacement - Blackboard
;	Copyright (C) 2008 Avery Lee
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

.proc Blackboard
	;print banner
	ldx		#0
	mva		#$09	iccmd
	mwa		#banner	icbal
	lda		#24
	sta		icbll
	stx		icblh
	
echoloop:
	jsr		ciov

	mva		#$07	iccmd		;get characters
	stx		icbll
	bne		echoloop

banner:
	dta		'Altirra BIOS - memo pad',$9B

.endp
