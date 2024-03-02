;	Altirra - Atari 800/800XL/5200 emulator
;	Modular Kernel ROM - Keyboard Scan Code Table
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

KeyCodeToATASCIITable:
	;lowercase
	dta		$6C, $6A, $3B, $F0, $F0, $6B, $2B, $2A
	dta		$6F, $F0, $70, $75, $9B, $69, $2D, $3D
	dta		$76, $F0, $63, $F0, $F0, $62, $78, $7A
	dta		$34, $F0, $33, $36, $1B, $35, $32, $31
	dta		$2C, $20, $2E, $6E, $F0, $6D, $2F, $F0
	dta		$72, $F0, $65, $79, $7F, $74, $77, $71
	dta		$39, $F0, $30, $37, $7E, $38, $3C, $3E
	dta		$66, $68, $64, $F0, $F0, $67, $73, $61
	
	;SHIFT
	dta		$4C, $4A, $3A, $F0, $F0, $4B, $5C, $5E
	dta		$4F, $F0, $50, $55, $9B, $49, $5F, $7C
	dta		$56, $F0, $43, $F0, $F0, $42, $58, $5A
	dta		$24, $F0, $23, $26, $1B, $25, $22, $21
	dta		$5B, $20, $5D, $4E, $F0, $4D, $3F, $F0
	dta		$52, $F0, $45, $59, $9F, $54, $57, $51
	dta		$28, $F0, $29, $27, $9C, $40, $7D, $9D
	dta		$46, $48, $44, $F0, $F0, $47, $53, $41
	
	;CTRL
	dta		$0C, $0A, $7B, $F0, $F0, $0B, $1E, $1F
	dta		$0F, $F0, $10, $15, $9B, $09, $1C, $1D
	dta		$16, $F0, $03, $F0, $F0, $02, $18, $1A
	dta		$F0, $F0, $9B, $F0, $1B, $F0, $FD, $F0
	dta		$00, $20, $60, $0E, $F0, $0D, $F0, $F0
	dta		$12, $F0, $05, $19, $9E, $14, $17, $11
	dta		$F0, $F0, $F0, $F0, $FE, $F0, $7D, $FF
	dta		$06, $08, $04, $F0, $F0, $07, $13, $01
