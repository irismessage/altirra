MADS = mads.exe
ATCOMPILER = $(OUT)\atcompiler.exe

SRCFILES = source\main.xasm \
source\loader.xasm

SHARED_SRCFILESMIN = \
source\Shared\atarifont.bin \
source\Shared\atarifont.inc

SHARED_SRCFILES = \
source\Shared\atarifont.bin \
source\Shared\atariifont.bin \
source\Shared\atarifont.inc \
source\Shared\atariifont.inc \
source\Shared\boot.s \
source\Shared\blackboard.s \
source\Shared\bugcheck.s \
source\Shared\cio.inc \
source\Shared\cio.s \
source\Shared\disk.s \
source\Shared\editor.s \
source\Shared\hardware.inc \
source\Shared\init.s \
source\Shared\interrupt.s \
source\Shared\irq.s \
source\Shared\kerneldb.inc \
source\Shared\keyboard.s \
source\Shared\keytable.s \
source\Shared\mathpack.s \
source\Shared\screen.s \
source\Shared\vbi.s \
source\Shared\sio.inc \
source\Shared\sio.s \
source\Shared\pbi.s \
source\Shared\cassette.s \
source\Shared\misc.s \
source\Shared\bootscreen.s \
source\Shared\driveimage.inc \
source\Shared\selftestentry.s

NOKERNEL_SRCFILES = source\nokernel\main.xasm
BASIC_SRCFILES = source\basic\main.xasm
SUPERKERNEL_SRCFILES = source\superkernel\main.xasm source\superkernel\includes.xasm
NOCARTRIDGE_SRCFILES = source\nocartridge\main.xasm
ATDISKLOADER_SRCFILES = source\ATDiskLoader\main.s
NOHDBIOS_SRCFILES = source\NoHDBIOS\main.s
NOGAME_SRCFILES = source\NoGame\main.s
ULTIMATE_SRCFILES = source\Ultimate\main.s source\Ultimate\bios.s source\Ultimate\kernel.s
BOOTSECDOS2_SRCFILES = source\BootSector\bootsecdos2.s

VERSION_SRCFILES = autobuild\version*.inc autobuild_default\version.inc

all: kernel kernelxl nokernel basic superkernel nocartridge atdiskloader64 atdiskloader128 nohdbios nogame ultimate bootsecdos2

clean:
	if exist "$(OUT)\kernel.rom" del "$(OUT)\kernel.rom"
	if exist "$(OUT)\kernelxl.rom" del "$(OUT)\kernelxl.rom"
	if exist "$(OUT)\nokernel.rom" del "$(OUT)\nokernel.rom"
	if exist "$(OUT)\basic.rom" del "$(OUT)\basic.rom"
	if exist "$(OUT)\superkernel.rom" del "$(OUT)\superkernel.rom"
	if exist "$(OUT)\nocartridge.rom" del "$(OUT)\nocartridge.rom"
	if exist "$(OUT)\atdiskload64.rom" del "$(OUT)\atdiskload64.rom"
	if exist "$(OUT)\atdiskload128.rom" del "$(OUT)\atdiskload128.rom"
	if exist "$(OUT)\nohdbios.rom" del "$(OUT)\nohdbios.rom"
	if exist "$(OUT)\nogame.rom" del "$(OUT)\nogame.rom"
	if exist "$(OUT)\ultimate.rom" del "$(OUT)\ultimate.rom"
	if exist "$(OUT)\ultimate.lzrom" del "$(OUT)\ultimate.lzrom"
	if exist "$(OUT)\bootsecdos2.bin" del "$(OUT)\bootsecdos2.bin"

kernel: $(OUT)\kernel.rom

kernelxl: $(OUT)\kernelxl.rom

nokernel: $(OUT)\nokernel.rom

basic: $(OUT)\basic.rom

superkernel: $(OUT)\superkernel.rom

nocartridge: $(OUT)\nocartridge.rom

atdiskloader64: $(OUT)\atdiskloader64.bin

atdiskloader128: $(OUT)\atdiskloader128.bin

nohdbios: $(OUT)\nohdbios.rom

nogame: $(OUT)\nogame.rom

ultimate: $(OUT)\ultimate.lzrom

bootsecdos2: $(OUT)\bootsecdos2.bin

$(OUT)\kernel.rom: $(SRCFILES) $(SHARED_SRCFILES)
	@for %%x in (source\main.xasm) do $(MADS) -i:autobuild -i:autobuild_default -d:_KERNEL_XLXE=0 -s -p -i:source\Shared -b:$$d800 -l:$(OUT)\kernel.lst -t:$(OUT)\kernel.lab -o:$(OUT)\kernel.rom %~fx

$(OUT)\kernelxl.rom: $(SRCFILES) $(SHARED_SRCFILES) $(VERSION_SRCFILES)
	@for %%x in (source\main.xasm) do $(MADS) -i:autobuild -i:autobuild_default -d:_KERNEL_XLXE=1 -s -p -i:source\Shared -b:$$c000 -l:$(OUT)\kernelxl.lst -t:$(OUT)\kernelxl.lab -o:$(OUT)\kernelxl.rom %~fx

$(OUT)\nokernel.rom: $(NOKERNEL_SRCFILES) $(SHARED_SRCFILESMIN)
	@$(MADS) -s -p -i:source\shared -b:$$c000 -l:$(OUT)\nokernel.lst -o:$(OUT)\nokernel.rom source\nokernel\main.xasm

$(OUT)\basic.rom: $(BASIC_SRCFILES)
	@$(MADS) -s -p -b:$$a000 -l:$(OUT)\basic.lst -o:$(OUT)\basic.rom source\basic\main.xasm

$(OUT)\superkernel.rom: $(SUPERKERNEL_SRCFILES)
	@$(MADS) -s -p -i:source\shared -b:$$f800 -l:$(OUT)\superkernel.lst -o:$(OUT)\superkernel.rom source\superkernel\main.xasm

$(OUT)\nocartridge.rom: $(NOCARTRIDGE_SRCFILES)
	@$(MADS) -s -p -b:$$b000 -l:$(OUT)\nocartridge.lst -o:$(OUT)\nocartridge.rom source\nocartridge\main.xasm

$(OUT)\atdiskloader128.bin: $(ATDISKLOADER_SRCFILES)
	@$(MADS) -d:MEMORY=128 -s -p -b:$$0800 -l:$(OUT)\atdiskloader128.lst -o:$(OUT)\atdiskloader128.bin source\ATDiskLoader\main.s

$(OUT)\atdiskloader64.bin: $(ATDISKLOADER_SRCFILES)
	@$(MADS) -d:MEMORY=64 -s -p -b:$$0800 -l:$(OUT)\atdiskloader64.lst -o:$(OUT)\atdiskloader64.bin source\ATDiskLoader\main.s

$(OUT)\nohdbios.rom: $(NOHDBIOS_SRCFILES)
	@$(MADS) -s -p -b:$$d800 -l:$(OUT)\nohdbios.lst -o:$(OUT)\nohdbios.rom source\NoHDBIOS\main.s

$(OUT)\nogame.rom: $(NOGAME_SRCFILES)
	@$(MADS) -s -p -b:$$a000 -l:$(OUT)\nogame.lst -o:$(OUT)\nogame.rom source\nogame\main.s

$(OUT)\ultimate.rom: $(ULTIMATE_SRCFILES) $(SHARED_SRCFILES)
	@$(MADS) -s -p -i:source\Shared -l:$(OUT)\ultimate.lst -o:$(OUT)\ultimate.rom source\ultimate\main.s

$(OUT)\ultimate.lzrom: $(OUT)\ultimate.rom
	@$(ATCOMPILER) lzpack "$(OUT)\ultimate.rom" "$(OUT)\ultimate.lzrom"

$(OUT)\bootsecdos2.bin: $(BOOTSECDOS2_SRCFILES) $(SHARED_SRCFILES)
	@$(MADS) -s -p -i:source\Shared -l:$(OUT)\bootsecdos2.lst -o:$(OUT)\bootsecdos2.bin source\bootsector\bootsecdos2.s
