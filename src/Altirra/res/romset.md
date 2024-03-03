# Altirra ROM image set

This ROM image set consists of the following five files:

* atbasic.rom (Altirra 8K BASIC)
* altirraos-800.rom (AltirraOS for 400/800 Computers)
* altirraos-xl.rom (AltirraOS for XL/XE/XEGS Computers)
* altirraos-5200.rom (AltirraOS for 5200 SuperSystem)
* altirraos-816.rom (AltirraOS for 65C816 Accelerators)

These are replacements for the standard Atari ROMs that would normally be present in these systems; they are not the original ROMs, but new ones rewritten from scratch to match the same programming interface without the original code. They may be used in either real hardware or in emulators with sufficient accuracy. The mapping to standard filenames expected by most emulators is as follows:

* atbasic.rom -> ATARIBAS.ROM
* altirraos-800.rom -> ATARIOSB.ROM
* altirraos-xl.rom, altirraos-816.rom -> ATARIXL.ROM / ATARIXEGS.ROM
* altirraos-5200.rom -> ATARI5200.ROM

On real hardware, you will need a way to burn or flash replacement ROMs, such as an EPROM burner, 32-in-1 OS switcher, or Ultimate1MB. For Altirra 8K BASIC, there is a soft-loadable version of the interpreter on the Additions disk that comes with Altirra.

**Please note:** The replacement ROMs have been written to support software that conforms to Atari's original specifications for usage of public variables and entry points into the firmware ROMs. Software that makes use of undocumented entry points or behaviors from the original OS and BASIC ROMs may not work with these ROMs. An attempt has been made to achieve reasonably high compatibility, but some programs have hardcoded assumptions that cannot work with such a replacement ROM and will only run on the original firmware.

## Updates


This ROM image set package is produced by the Tools > Export ROM Set command of the Altirra emulator. Newer versions of the emulator include updated versions of the ROM set.

## License

The license for all files in the ROM set is the following all-permissive license:

Altirra 8K BASIC  
AltirraOS for 400/800 Computers  
AltirraOS for XL/XE/XEGS Computers  
AltirraOS for 5200 SuperSystem  
AltirraOS for 65C816 Accelerators  

Copyright © 2017-2023 Avery Lee, All Rights Reserved.

Copying and distribution of this file, with or without modification, are permitted in any medium without royalty provided the copyright notice and this notice are preserved. This file is offered as-is, without any warranty.

## Manuals

Visit the [Altirra home page](http://virtualdub.org/altirra.html) for the Altirra 8K BASIC manual.

## Source code

The source code for these files is included in the source of the [Altirra emulator](http://virtualdub.org/altirra.html). AltirraOS is within the Visual C++ project called `Kernel`,
while Altirra BASIC has its own project called `atbasic`. Both are Makefile projects that invoke NMAKE to do the actual build through MADS.

## Other stuff

Looking for other software, like a replacement R: device handler? You might find it on the Additions disk that comes with Altirra.

## Version history

### AltirraOS

In current versions, the version string for AltirraOS 400/800, XL/XE/XEGS, and 65C816 can be read programmatically from the ROM image. For the 16K images, the version string is stored at the end of the self-test region, at offsets $17F8-17FF in the image ($57F8-57FF in memory), and for the 10K image, it is at $0CB0 in the image ($E4B0 in memory) as part of the memo pad banner.

* Version 3.41

    * SIO now resets BRKKEY after returning Break condition.

* Version 3.40

    * FASC no longer alters the first byte of FR0.

* Version 3.39

    * S: now properly ignores the no-clear flag when opening a GR.0 screen.

* Version 3.38

    * Fixed K: handler not allowing inverse video to be applied to vertical bar (|).
    * XL/XE/XEGS: $85 EOF code is now supported in custom key definition tables.
    * 65C816: Fixed incorrect Y return value from screen editor with cursor inhibited.
    * Fixed a timing issue in SETVBV.

* Version 3.37

    * XL/XE/XEGS: HELPFG now has bits 6 and 7 set for Shift+Help and Ctrl+Help.
    * XL/XE/XEGS: Improved compatibility with NOCLIK values in $01-7F range.
    * 65C816: Fixed KEYREP and KRPDEL not being implemented only in the 816-specific version.

* Version 3.36

    * Improve compatibility with programs that rely on C=1 on exit from CIO on program launch (fixes GUNDISK.XEX).

* Version 3.35

    * XL/XE/XEGS: Fixed a bug in the peripheral handler loader where handlers loaded through a CIO type 4 poll were improperly raising MEMLO. This is only supposed to happen for startup (type 3) polls.

* Version 3.34

    * Adjusted TIMFLG usage in SIO routines to match the official description.
    * Improved compatibility with tape loaders that rely on the initial register values when invoking the run address.

* Version 3.33

    * Improved compatibility with programs that use the KEYDEL counter to detect held keys (fixes menu cursor movement in BrushupV40).

* Version 3.32

    * Fixed math pack compatibility issue with FDIV modifying FLPTR (fixes B-Graph pie chart routine).
    * Fixed a cursor position checking bug with split screen Gr.0 (fixes the BASIC game House of Usher).

* Version 3.31

    * Fixed a lockup in the E: device when processing a Delete Char command at column 0 with LMARGN=0.
    * Fixed E: move left command not working at column 0 with LMARGN=0.
    * Added workaround to SIO to allow an ACK response from a device when a Complete or Error response is expected in the SIO protocol, to match the behavior of the stock OS. This fixes the Zero Adjust mode of the Indus GT Diagnostics.

* Version 3.30

    * Applied workarounds for floating-point constant encoding errors in MADS assembler. This fixes minor rounding errors in math pack ATN() constants with MADS 1.9.8b5, and major errors with MADS 2.1.0b8 (3.29 was assembled with 1.9.8b5).

* Version 3.29

    * Implemented support for XL/XE NOCLIK variable.

* Version 3.28

    * Fixed a memory overlap bug in FDIV that occasionally introduced small errors into results.
    * Fixed E: put byte routine returning Y<>1 with cursor inhibited.

* Version 3.27

    * 800 build: Modified internal variable usage to accommodate direct calls into the E: and P: handlers from Monkey Wrench II.
    * Improved compatibility of P: handler with OS-B and XL/XE OS behavior -- EOLs and inverse characters are handled more similarly, and the 800 build now pads on close with $20 while the XL/XE build uses $9B.

* Version 3.26

    * Line draws in GR.0 no longer restore the cursor, which was causing screen corruption in Worms of Bemer.

* Version 3.25

    * Fixed an issue with screen clears overwriting memory above RAMTOP.

* Version 3.24

    * 65C816 build: Fixed system device (@:) returning bogus data.
    * 65C816 build: Fixed SIO crashing when called in native mode.
    * 65C816 build: Fixed memory corruption in screen editor due to incompletely ported QUICKED.SYS fix.
    * 65C816 build: Modified native VBI dispatching to work around bug in SpartaDOS X 4.48 BogoMIPS calibration loop.

* Version 3.23

    * Fixed an SIO issue with an out-of-spec short delay from asserting the command line to first command byte.
    * Fixed disk boot issues if a PBI device or cartridge init routine issued an SIO request with DAUX2 > 0 before the disk boot.
    * XL/XE/XEGS: The boot screen now resets the Break key state properly after a boot is interrupted by the Break key.

* Version 3.22

    * 800 build: Raise memory scan limit from $C000 to $D000 to allow 52K memory configurations.

* Version 3.21

    * XL/XE/XEGS build: KRPDEL is now set/used, and keyboard repeat rates are adjusted properly for PAL/SECAM.

* Version 3.20

    * Modified OLDADR usage to fix compatibility issue with SDX QUICKED.SYS.
    * SIO transmit operations can now be interrupted by user break (Break key). This fixes a hang with some older versions of the SIDE Loader, which accidentally rely on being able to force-exit SIO via the Break key IRQ.
    * Fixed audio configuration when loading from tape.
    * Disk boot is now attempted after tape boot and a right cart not requesting cart start. This fixes a random crash when booting the tape version of Fun With Spelling With Heathcliff, which relies on the VBI synchronization from a failed SIO poll to avoid a race condition on boot.
    * The boot screen no longer forces a cold boot when invoked as part of the boot process. It now resumes the boot after successfully reading sector 1, so that internal BASIC disable state is preserved. A cold boot is still forced when invoking the boot screen via the self-test vector after initial boot.
    * The Display Handler now properly sets the default background color (COLOR4) to $06 when opening a GR.11 screen.

* Version 3.17

    * Fixed regression in 3.16 where fine scrolling was set up too often on screen opens.

* Version 3.16

    * Minor optimizations to text I/O routines.
    * XL/XE/XEGS build: HELPFG is now set when Help is pressed.
    * 65C816 build: COP #n no longer crashes in emulation mode.

* Version 3.15

    * ICBLLZ/ICBLHZ is now updated after a CIO get operation. This fixes Lightspeed DOS directory listings.

* Version 3.14

    * Type 3 poll loop is now exited if PBI device firmware responds to a poll request with error Y=$80 (user break). This fixes a boot loop with Black Box 2.16 firmware.

* Version 3.13

    * SIOV now sets A=0 (undocumented XL/XE OS behavior).

* Version 3.12

    * CBAUDL/H are now set by the cassette routines.

* Version 3.11

    * Version number now decoupled from main emulator.
    * FDIV no longer returns 0 for 0/0.
    * AFP(".") properly returns an error instead of 0.
    * EXP10() was returning an error instead of underflowing to 0 for some large negative inputs.
    * Entering the boot screen (self test) now sets COLDST to prevent BASIC from attempting to preserve an existing program on reset.

* Version 3.10

    * Added 65C816 version.

* Version 3.00

    * Fixed polarity of CKEY.

* Version 2.90

    * Modified values of PALNTS for better compatibility with XL/XE OS.
    * Fix short block flag not being handled by direct SIO calls for the cassette device.
    * Suppress type 3 poll to disk boot only (fixes Pole Position and Missile Command cartridge audio).

* Version 2.80

    * Fixed XEGS game cartridge activation.
    * Fixed errors getting dropped in cassette handler.
    * Fixed extra initial block and incorrect partial block problems when writing cassette streams.
    * Fixed CIO read record when line exactly fits in buffer.
    * Fixed broken inverse key.
    * S: clear also clears split-screen area.
    * Optimized C/E->data frame path in SIO for better robustness when DLIs are active.
    * Fixed race condition in SETVBV.

### Altirra BASIC

* Version 1.58

    * IOCB#7 is now automatically closed on I/O errors to avoid SAVE files being kept open for write.

* Version 1.57

    * Compatibility fix to DRAWTO statement to allow LOCATE statements to work after XIO n,#6,0.

* Version 1.56

    * Fixed crash when Break is pressed prior to startup banner.

* Version 1.55

    * Added compatibility workaround for programs that use locations 183 and 184 to read the current DATA line.

* Version 1.54

    * PMBASE is no longer altered if PMGRAPHICS has not been executed.

* Version 1.53

    * Fixed READ line not getting reset after NEW.

* Version 1.52

    * Disk version now works around SDX bug when restoring screen on exit.

* Version 1.51

    * Fixed INT(1000000000) crash.
    * Newly added variables are removed on parsing error.
