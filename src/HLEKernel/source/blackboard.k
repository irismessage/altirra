;	Altirra - Atari 800/800XL emulator
;	Kernel ROM replacement
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
	#{
		// check if we have at least 32K of memory
		if (kdb.RAMTOP >= 0x80) {
			// load the self-test module out of the LLE-XL kernel
			uint8 buf[2048];
			ATLoadKernelResource(IDR_KERNELXL, buf, 0x1000, 0x800);
			
			// patch the name to say HLE instead of LLE
			for(int i=0; i<2048-3; ++i) {
				if (buf[i] == 'L'-0x20 && buf[i+1] == 'L'-0x20 && buf[i+2] == 'E'-0x20) {
					buf[i] = 'H'-0x20;
					break;
				}
			}
			
			// force off self-test ROM, if it exists
			kdb.PORTB |= 0x80;
			
			// write into memory at 0x5000
			for(int i=0; i<2048; ++i)
				mpMemory->WriteByte(0x5000 + i, buf[i]);
				
			// jump to 0x5000
			mpCPU->Jump(0x5000);
			return;
		}
	#}
	
	;print banner
	ldx		#0
	mva		#$0b	iccmd		;put characters
	mwa		#banner	icbal		;address
	mwa		#banner_end-banner		icbll		;count
	jsr		ciov
	
	ldx		#>vbi
	ldy		#<vbi
	lda		#7
	jsr		setvbv
	
	mwa		#dli vdslst
	
	mwa		sdlstl	$80
	lda		#$c2
	ldy		#3
	sta		($80),y
	
	mva		#$c0 nmien
	
	;echo all keys
echoloop:
	ldx		#0
	mva		#$07	iccmd		;get characters
	mwa		#$0100	icbal
	mwa		#1		icbll
	jsr		ciov
	jmp		echoloop

bkarray	equ		$82
bkcols	dta	$94,$96,$98

dli:
	pha
		
	lda		bkarray+1
	sta		wsync
	sta		colpf2

	lda		bkarray+2
	sta		wsync
	sta		colpf2

	lda		bkarray+1
	sta		wsync
	sta		colpf2

	lda		bkarray
	sta		wsync
	sta		colpf2

	pla
	rti
	
vbi:
	ldx		#3
atloop:
	lda		bkcols-1,x
	eor		colrsh
	and		drkmsk
	sta		bkarray-1,x
	dex
	bne		atloop

	jmp		xitvbv

banner:
	dta 'Altirra internal HLE BIOS - memo mode',$9B
	dta	$9B

;	     0123456789012345678901234567890123456
	dta 'No bootable disk images or cartridges',$9B
	dta 'were found.',$9B
	dta $9B
	dta 'This is a internal substitute for',$9B
	dta 'the normal Atari ROM kernel. Check',$9B
	dta 'the README for instructions on using',$9B
	dta 'real Atari ROM images for higher',$9B
	dta 'compatibility.',$9B
	dta $9B
banner_end:

.endp
