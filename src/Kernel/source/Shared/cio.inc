;	Altirra - Atari 800/800XL/5200 emulator
;	Modular Kernel ROM - Character Input/Output Definitions
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

.ifndef f_CIO_INC
.def f_CIO_INC

CIOStatBreak		= $80	;break key abort
CIOStatIOCBInUse	= $81	;IOCB in use
CIOStatUnkDevice	= $82	;unknown device
CIOStatWriteOnly	= $83	;opened for write only
CIOStatInvalidCmd	= $84	;invalid command
CIOStatNotOpen		= $85	;device or file not open
CIOStatInvalidIOCB	= $86	;invalid IOCB number
CIOStatReadOnly		= $87	;opened for read only
CIOStatEndOfFile	= $88	;end of file reached
CIOStatTruncRecord	= $89	;record truncated
CIOStatTimeout		= $8A	;device timeout
CIOStatNAK			= $8B	;device NAK
CIOStatSerFrameErr	= $8C	;serial bus framing error
CIOStatCursorRange	= $8D	;cursor out of range
CIOStatSerOverrun	= $8E	;serial frame overrun error
CIOStatSerChecksum	= $8F	;serial checksum error
CIOStatDeviceDone	= $90	;device done error
CIOStatBadScrnMode	= $91	;bad screen mode
CIOStatNotSupported	= $92	;function not supported by handler
CIOStatOutOfMemory	= $93	;not enough memory
CIOStatDriveNumErr	= $A0	;disk drive # error
CIOStatTooManyFiles	= $A1	;too many open disk files
CIOStatDiskFull		= $A2	;disk full
CIOStatFatalDiskIO	= $A3	;fatal disk I/O error
CIOStatFileNumDiff	= $A4	;internal file # mismatch
CIOStatFileNameErr	= $A5	;filename error
CIOStatPointDLen	= $A6	;point data length error
CIOStatFileLocked	= $A7	;file locked
CIOStatInvDiskCmd	= $A8	;invalid command for disk
CIOStatDirFull		= $A9	;directory full (64 files)
CIOStatFileNotFound	= $AA	;file not found
CIOStatInvPoint		= $AB	;invalid point

CIOCmdOpen			= $03
CIOCmdGetRecord		= $05
CIOCmdGetChars		= $07
CIOCmdPutRecord		= $09
					; $0A	;PUT CHARS alias (required by K-Razy Shoot Out)
CIOCmdPutChars		= $0B
CIOCmdClose			= $0C
CIOCmdGetStatus		= $0D
CIOCmdSpecial		= $0E	;$0E and up is escape

.endif
