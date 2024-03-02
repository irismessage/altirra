		opt		h-
		
		icl		'hardware.inc'
		icl		'kerneldb.inc'
		icl		'sio.inc'

dskinv	= $e453

		org		$0700
		opt		f+

		dta		$00						;$0700 ($00)
		dta		3						;$0701 ($03)
		dta		a($0700)				;$0702 ($0700)
		dta		a(boot)					;$0704 ($1540)
		jmp		finish					;$0706 ($0714)
bcb_maxfiles	dta		3				;$0709 ($03)
bcb_drivebits	dta		1				;$070A ($03)
bcb_allocdirc	dta		0				;$070B ($00) (unused)
bcb_secbuf		dta		a(0)			;$070C ($1A7C)
bcb_bootflag	dta		$00				;$070E ($01)
bcb_firstsec	dta		a(0)			;$070F ($0004) first sector of DOS.SYS
bcb_linkoffset	dta		125				;$0711 ($7D)
bcb_loadaddr	dta		a(0)			;$0712 ($07CB)

boot:
finish:
		sec
		rts

		org		$0880
		end
