;	Altirra - Atari 800/800XL/5200 emulator
;	Modular Kernel ROM - Disk Routines
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

.proc DiskInit
	;set disk sector size to 128 bytes
	mwa		#$80	dsctln
	rts
.endp

.proc DiskHandler
	mva		#$31	ddevic
	lda		#$40
	sta		dtimlo
	sta		dstats
	mwa		dsctln	dbytlo
	
	;check for status command
	lda		dcomnd
	sta		ccomnd
	cmp		#$53
	bne		notStatus

	mwa		#dvstat	dbuflo
	mwa		#4		dbytlo
	jmp		siov
	
notStatus:
	;check for put command
	cmp		#$50
	sne:mva	#$80	dstats
	
	;call SIO
	jmp		siov
.endp
