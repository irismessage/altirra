==============================================================================
Introduction
==============================================================================

Altirra is an emulator for the Atari 8-bit series of home computers, including
the 800, 800XL, and 130XE versions. Because modern computers are so much
faster than the original 1.79MHz machine, a nearly complete emulation of the
hardware is provided and thus the emulator can run a large amount of the
software available for the platform.

The main reasons I wrote Altirra was for two purposes: education, and
nostalgia. I grew up with a number of 8-bit computers, and the Atari was my
favorite, partially because it was the ancestor of the Amiga. I've learned
a tremendous amount about the original hardware while writing the emulator,
and it's certainly been an interesting (and sometimes frustrating experience).
This does mean that Altirra is missing some polish and has some rather odd
development features, which I hope you'll bear with. Please excuse my
eccentricity.

Of course, I should note that Atari is a trademark I don't own, that neither
I nor Altirra are affilated with Atari, and that Altirra does not
intentionally include any unlicensed intellectual property of other entities.
Finally, Altirra is not intended for piracy, and I do not condone or approve
of distributing Altirra with unlicensed software.

I hope Altirra is useful to you, and all comments are welcome. Altirra is
a side project, and as such I don't work on it consistently, but I still
wouldn't mind hearing from people who try it.

-- Avery Lee <phaeron (at) virtualdub.org>

==============================================================================
Getting started
==============================================================================

By default, Altirra uses an internal kernel ROM which partially supports basic
Atari kernel services, such as interrupts, display, and disk I/O. If you have
cartridge or disk images already, you can use them with the emulator as-is,
and some of them may work. There are also freely available demo images on
the Internet you can obtain which were written by people around the world to
stretch and demonstrate the power of the Atari hardware, and which you can
view with the emulator if you have not seen an Atari in action before.

To load a disk, simply use the File > Open Disk/Program... option, and select
the disk image. The most common disk images are in .ATR format. If they are
compressed in a .zip archive, you must extract them with a separate utility
first.

Most games are compatible with the original Atari 800, and as such should use
the 800 hardware mode. Some newer games that need more memory will need 800XL
mode and 64K memory instead, and perhaps 128K of memory. When running demos,
800XL hardware mode with 320K of memory will run almost anything, although
you should use PAL for demos as the vast majority of demos are produced in
Europe.

Altirra emulates the hardware with much higher compatibility than its internal
ROM emulates the kernel, so for much better success, it's recommended that you
use real Atari kernel ROMs if available. These should be placed next to
Altirra.exe:

	atariosb.rom	(Atari 800 OS-B kernel ROM)
	atarixl.rom	(Atari 800XL kernel ROM)
	ataribas.rom	(Atari Basic ROM)

With these available, you can switch the firmware to 800 OS-B and 800XL mode.
I won't tell how to obtain these ROMs, and needless to say, you should only
use images of the ROMs if you actually own the real ones. If you don't, you
can still run a lot of software on the internal kernel, and the full Atari
hardware is still emulated.

----------------------
Protected disk support
----------------------

If you have actual original diskettes with Atari software on them and have
transferred them to your computer, you can use them with Altirra. A special
trick is that Altirra can use special disk formats that allow you to run
copy protected software authentically, without bypassing the copy protection.
(Please heed the usual warning here about only using validly licensed
software; I'll assume you own a real copy.) Two protected formats are
supported.

APE (.pro):
	This is the format produced and used by the Atari Peripheral Emulator
	(APE). Support for the .pro format was implemented by reverse
	engineering the protected disks (but not the program), so emulation
	is spotty. The PRO format works mostly by encoding the order in which
	phantom sectors are read by the program, so they do not truly support
	full emulation and must be run with the Accurate Sector Timing option
	disabled. Not all PRO disks will work, unfortunately.

VAPI (.atx);
	This is a newer format created by a group of people dedicated to
	Atari software archival. Unlike the .pro format, the .atx format
	encodes sector positioning, which allows Altirra to actually emulate
	true disk read timing for a high level of compatibility. Accurate
	Sector Timing is recommended when using VAPI disks.

Copy protection algorithms are notoriously fickle, and thus may fail to boot
without optimal settings. For maximum compatibility, use the following
settings:

	Firmware: 800 OS-B or 800XL
	Disk > SIO patch: off
	Disk > Burst SIO transfer: off
	Disk > Accurate Sector Timing: on for .atx, off for .pro

Many programs will boot with the SIO patch enabled, though, as it is designed
to try to maintain correct sector timing even though the emulated Atari isn't
actually spending time reading a sector. For programs that switch to custom
disk routines, you can either use warp mode to speed the load, or try
enabling the burst SIO transfer mode.

==============================================================================
Options
==============================================================================

Altirra has a number of options that can be used to customize the emulated
Atari hardware.

-------------------
Hardware options
-------------------

800:
	Enables the base Atari 800 hardware.

800XL:
	Enables Atari 800XL/130XE hardware features.

-------------------
Firmware options
-------------------

Default:
	Chooses either the OS-B ROM for 800 hardware or the XL ROM for 800XL
	hardware.

800 (OS-A):
	Selects the OS-A version of the Atari 800 firmware. You must have
	ATARIOSA.ROM for this to work.

800 (OS-B):
	Selects the OS-B version of the Atari 800 firmware. You must have
	ATARIOSB.ROM for this to work.

600XL/800XL:
	Selects the Atari 800XL firmware. You must have ATARIXL.ROM for this
	to work.

HLE Kernel:
	Selects the internal high level emulation (HLE) kernel, which executes
	kernel calls in native code. No firmware is required. This mode is
	very incomplete.

LLE Kernel:
	Selects the internal low level emulation (LLE) kernel, which is a
	replacement kernel in pure 6502 code. No firmware is required. This
	mode is partially complete and will boot some software. Enabling
	the disk SIO patch is recommended.

-------------------
Memory size options
-------------------

48K:
	Standard Atari 800 configuration with RAM from 0000-BFFF. The range
	from C000-CFFF is unconnected.

52K:
	Extended Atari 800 configuration with RAM from 0000-CFFF.

64K (XL):
	Standard Atari 800XL configuration with RAM from 0000-FFFF, with the
	upper 14K swappable with Kernel ROM via PORTB.

128K (130XE):
	Atari 130XE configuration with main ROM from 0000-FFFF and external
	RAM bank switched via a window at 4000-7FFF. This mode is the only
	mode that supports ANTIC bank switching.

320K:
	Extended Atari 800/800XL configuration with an additional 12 banks
	of memory. This mode does not support ANTIC bank switching.

576K:
	Extended Atari 800/800XL configuration with an additional 28 banks
	of memory. This mode does not support either ANTIC bank switching
	or enabling BASIC.

1088K:
	Extended Atari 800/800XL configuration with an additional 60 banks
	of memory. In addition to disabling ANTIC banking and BASIC, this
	mode also takes an additional bit in PORTB and disables the
	XL self-test ROM.

------------
Disk options
------------

PAL:
	Selects PAL ANTIC/GTIA operation, including aspect ratio and a 50Hz
	refresh rate. This is recommended for demos since many are written
	in Europe.

NTSC Artifacting:
	Enables emulation of false colors from alternating high resolution
	pixels with NTSC video encoding. This is necessary to see colors in
	some games that use artifacting, such as Choplifter and Pitstop II.

PAL Artifacting:
	Enables emulation of false colors from chroma blending in the delay
	line of a PAL video decoder. This gives more accurate color output
	in programs that alternate color and grayscale lines to increase
	the effective color depth.

Enhanced text output (hardware intercept):
	Replaces the standard emulated video display with a text screen
	using native Windows fonts. This disables emulation of most Atari
	video features and only supports basic text modes, but produces
	a higher quality text display.	

------------
Disk options
------------

SIO patch:
	Intercepts and accelerates disk I/O calls to the serial input/output
	(SIO) and disk (DSKIN) routines in the kernel. This tremendously
	speeds up disk access in the emulated programs.

	Some demos or protected apps may not be compatible with the SIO patch,
	however.

Burst SIO transfer:
	When enabled, the disk drive sends data across the SIO bus as fast
	as the program can accept it. This is useful for speeding up disk
	loads for programs that use custom SIO routines. It will, however,
	cause the foreground task in the Atari to run very slowly during
	the transfer.

Accurate sector timing:
	Causes the emulator to simulate rotational delays caused by sector
	position and seek delays (step and head settling time). This results
	in a slower disk load, but is sometimes necessary to load protected
	disks.

	When disabled or when the SIO patch is enabled, Altirra still attempts
	to track disk rotation for protected disks. This means that CPU timing
	is not preserved, but rotational position is still noted.

----------------
Cassette options
----------------

SIO patch:
	Intercepts and accelerates cassette I/O calls to the serial input/
	output (SIO) routine in the kernel. This greatly speeds up cassette
	loads.

Auto-boot on startup:
	Automatically holds down START during system startup and then hits
	a key to initiate a cassette tape load. This only works with cassette
	tapes that have a machine language program; BASIC tapes must be loaded
	via CLOAD at the BASIC prompt instead.

Load data as audio:
	Converts the data from a .CAS tape image into raw audio data so it
	plays through the speaker.

-----------
CPU options
-----------

The first three options should not be used unless you are debugging code in
the interactive debugger, as they will slow down execution or cause debug
breaks in code that may be executing normally.

Record instruction history:
	Causes the CPU to record a trace record for every instruction
	executed.

Track code paths:
	Enables tracking of all memory where instructions are executed and
	which instructions are branch or call targets.

Stop on BRK instruction:
	Causes the debugger to stop when a breakpoint (BRK) instruction is
	executed. This often indicates that the Atari has crashed.
	
Enable illegal instructions:
	Allows execution of undocumented NMOS 6502 instructions, which are
	used by some programs. This only works in 6502C mode.

CPU model > 6502C:
	Selects the base NMOS 6502C as the CPU model. This is the CPU type
	used by all official Atari models.

CPU model > 65C02:
	Selects the CMOS 65C02 as the CPU model, which contains some extra
	instructions. This is occasionally used as a homebrew upgrade option.
	It may introduce some compatibility problems with games.

CPU model > 65C816:
	Selects the 16-bit 65C816 as the CPU model, which contains many
	more instructions and a new 16-bit native mode. This is used in some
	homebrew and commercial accelerators to both increase speed and
	memory address space.

	Currently, the 65C816 mode does neither: the CPU still runs at
	1.79MHz and there is no memory available outside of bank 0. However,
	this mode can run some extended BIOS and application code that
	makes use of 65C816 features.

-------------
Audio options
-------------

Stereo:
	Enables a second POKEY. Addresses in the D2xx range with bit 4 clear
	address the left channel, and addresses with bit 4 set address the
	right channel. The IRQ and serial facilities of the second POKEY are
	active but unconnected.

------------
Misc options
------------

Warp speed:
	Disables the speed limiter and audio sync, and runs the emulator as
	fast as possible.

Pause when inactive:
	When set, causes Altirra to pause emulation when its window goes
	inactive. Use this to prevent the emulated Atari from using CPU time
	when in the background.

Accelerated floating point:
	Intercepts known entry points in the floating point library with
	native math routines. This accelerates Atari decimal math to a large
	degree, particularly speeding up Atari Basic.

	The emulated FP library is higher accuracy than the real library due
	to better rounding and use of native math routines, so it will produce
	slightly different results.

==============================================================================
Keyboard mappings
==============================================================================

The PC keyboard is mapped to the Atari keyboard by character, so that text
can be typed naturally using whatever layout is active in Windows. The
following keys are specially mapped:

F1		Temporarily engages warp speed.
Shift+F1	Toggles joystick mapping to the keyboard.
F2		Atari Start button.
F3		Atari Select button.
F4		Atari Option button.
Ctrl+F5		Warm reset.
Shift+F5	Cold reset.
Ctrl+F7		Toggles PAL mode.
Break		Atari Break key.
Ctrl+Break	Break into debugger.
Right-Alt	Release mouse capture.
End		Atari key
Del		Shift+< (Clear)
Ins		Shift+> (Insert)
Page Down	Help

==============================================================================
Emulation accuracy
==============================================================================

Altirra was written with reference to the original Atari hardware and OS
manuals, as well as many helpful documents on the Internet and even hardware
specs and gate diagrams that have since been graciously released to the
public, and is designed to target very high hardware emulation accuracy.
Despite that, there are still many areas in which its behavior does not quite
match that of a real Atari. Here is the known emulation status.

CPU:
	Instruction execution is believed to be nearly cycle exact with an
	NMOS 6502C. Page crossing and false reads are implemented, including
	indexing retry cycles. All instructions, including illegal
	instructions, are supported.

	65C02 and 65C816 support is still fairly experimental at this point.
	The 6502 emulation mode portion of the 65C816 is stable, but the
	native mode may have some problems. There is currently no
	extended memory, i.e. memory outside of bank 0.

ANTIC:
	DMA is emulated at cycle level. Cycle stealing timing, including
	refresh DMA, is believed to be cycle-exact. There may be cases where
	mid-scanline changes do not take effect at exactly the right time.
	The timing of several registers, most notably VSCROL, VCOUNT, and
	WSYNC, has been calibrated to match a real Atari.

GTIA:
	P/M and playfield collisions are fully supported on a per-cycle basis,
	Undocumented priority modes are implemented, including the colorful
	mode 0.

	Mid-scanline PRIOR-based GTIA mode changes are supported, including
	the false ANTIC E mode that results when switching GTIA modes on
	and off. The color clock position difference in Graphics 10 is also
	implemented.

	The colors produced by Altirra are designed to provide a reasonable
	look. Actual colors produced by real Ataris vary considerably based
	on model and TV standard; PAL Ataris produce slightly different
	colors than NTSC models. I have seen an NTSC 130XE that actually
	generates fairly horrid colors, only about 12 unique hues of the 15
	it is supposed to generate.

	NTSC artifacting is supported.

POKEY:
	All timers run on cycle granularity and output is downfiltered from
	1.79MHz equivalent rate waveforms.

	Two-tone mode is supported, as is serial tone beeping.

	The polynomial counters use the same polynomials as the real hardware,
	but the sequence position seen by RANDOM may not be correct when
	switching between 9-bit	and 17-bit polynomial mode.

	POKEY serial port parameters are only partially emulated, so invalid
	or otherwise out-of-spec serial port parameters may work in Altirra
	even though they wouldn't on a real Atari.

RAM:
	CPU, ANTIC, Kernel ROM, and Self-Test ROM banking are all supported,
	assuming they are enabled according to the current machine and memory
	settings.

FLOPPY DISK:
	Intended to emulate an Atari 810. I even checked the 810 schematics
	and partially disassembled the firmware to determine the exact SIO
	bus transfer rates and error handling behavior.

	Only the base Atari 810 command set is supported, although the disk
	emulator will read and write disks with 256 byte sectors, as well as
	mega images of up to 65535 sectors (the max supported by the SIO
	protocol).

	The disk subsystem emulates the rotational and seek latency of an
	Atari 810 when accurate sector timing is enabled. When disabled,
	rotational position is still simulated to select phantom sectors
	for VAPI disks.

CASSETTE:
	Only partially implemented. The emulation is based on raw SIO
	waveforms and not decoded sectors; CAS files are converted to
	bitstreams.

	When reading raw .wav files, Altirra attempts to decode the cassette
	similarly to the 410 hardware, with two bandpass filters and a
	comparator with hysteresis. As such, it should have fairly good
	success at decoding raw tapes.

KERNEL:
	The kernel is fully compatible when using authentic Atari firmware,
	because, well, it IS the actual firmware.

	The low level kernel is quite incomplete, but contains partial
	implementations of SIO, CIO, and interrupt services. SIO can boot DOS
	2.0S, but in some cases it may corrupt transfers and the SIO patch
	bypass is recommended. Note that the low level kernel is written like
	a real kernel ROM, so it is not specific to Altirra and can be run
	on other emulators or on a real Atari.

	At this point, the high level emulation (HLE) kernel actually has
	better compatibility than the LLE kernel. Timing-wise it will not
	be as good as a real 6502-based kernel, but at this point it boots
	a wide variety of demos and games and is now the default. There are
	missing features in the S: and E: device handlers that can cause
	glitches when running productivity software.

	As noted above, the floating point acceleration does not always
	exactly match the results from the real floating point ROM.

PBI:
	The 800XL parallel bus interface is not supported. I've never seen or
	touched any PBI peripherals anyway.

==============================================================================
End
==============================================================================
