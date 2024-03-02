MADS = mads.exe
ATCOMPILER = $(OUT)\atcompiler.exe

SRCFILES = source\main.xasm \
source\loader.xasm

SHARED_SRCFILESMIN = \
source\Shared\atarifont.bin \
source\Shared\atarifont.inc

SHARED_SRCFILESINC = \
source\Shared\cio.inc \
source\Shared\sio.inc \
source\Shared\hardware.inc \
source\Shared\kerneldb.inc

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
source\Shared\screenext.s \
source\Shared\vbi.s \
source\Shared\sio.inc \
source\Shared\sio.s \
source\Shared\pbi.s \
source\Shared\phandler.s \
source\Shared\cassette.s \
source\Shared\misc.s \
source\Shared\bootscreen.s \
source\Shared\driveimage.inc \
source\Shared\selftestentry.s \
source\Shared\printer.s

NOKERNEL_SRCFILES = source\nokernel\main.xasm
BASIC_SRCFILES = source\basic\main.xasm
SUPERKERNEL_SRCFILES = source\superkernel\main.xasm source\superkernel\includes.xasm
NOCARTRIDGE_SRCFILES = source\nocartridge\main.xasm
ATDISKLOADER_SRCFILES = source\ATDiskLoader\main.s
NOHDBIOS_SRCFILES = source\NoHDBIOS\main.s
NOGAME_SRCFILES = source\NoGame\main.s
ULTIMATE_SRCFILES = source\Ultimate\main.s source\Ultimate\bios.s source\Ultimate\kernel.s
BOOTSECDOS2_SRCFILES = source\BootSector\bootsecdos2.s
SOFTKICK_SRCFILES = source\softkick.s

VERSION_SRCFILES = autobuild\version*.inc autobuild_default\version.inc

all: kernel kernelxl kernelxlsoftkick nokernel basic superkernel nocartridge atdiskloader64 atdiskloader128 nohdbios nogame ultimate bootsecdos2 r850

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
	if exist "$(OUT)\850relocator.bin" del "$(OUT)\850relocator.bin"
	if exist "$(OUT)\850handler.bin" del "$(OUT)\850handler.bin"
	if exist "$(INT)\850handler-lo.obx" del "$(INT)\850handler-lo.obx"
	if exist "$(INT)\850handler-hi.obx" del "$(INT)\850handler-hi.obx"

kernel: $(OUT)\kernel.rom

kernelxl: $(OUT)\kernelxl.rom

kernelxlsoftkick: $(OUT)\kernelxlsoftkick.xex

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

r850: $(OUT)\850relocator.bin $(OUT)\850handler.bin

$(OUT)\kernel.rom: $(SRCFILES) $(SHARED_SRCFILES)
	@for %%x in (source\main.xasm) do $(MADS) -i:autobuild -i:autobuild_default -d:_KERNEL_XLXE=0 -s -p -i:source\Shared -b:$$d800 -l:$(OUT)\kernel.lst -t:$(OUT)\kernel.lab -o:$(OUT)\kernel.rom %~fx

$(OUT)\kernelxl.rom: $(SRCFILES) $(SHARED_SRCFILES) $(VERSION_SRCFILES)
	@for %%x in (source\main.xasm) do $(MADS) -i:autobuild -i:autobuild_default -d:_KERNEL_XLXE=1 -s -p -i:source\Shared -b:$$c000 -l:$(OUT)\kernelxl.lst -t:$(OUT)\kernelxl.lab -o:$(OUT)\kernelxl.rom %~fx

$(OUT)\kernelxlsoftkick.xex: $(SRCFILES) $(SHARED_SRCFILES) $(VERSION_SRCFILES) $(SOFTKICK_SRCFILES) $(OUT)\kernelxlsoft.bin
	@for %%x in (source\softkick.s) do $(MADS) -i:$(OUT) -s -p -i:source\Shared -l:$(OUT)\kernelxlsoftkick.lst -t:$(OUT)\kernelxlsoftkick.lab -o:$(OUT)\kernelxlsoftkick.xex %~fx

$(OUT)\kernelxlsoft.bin: $(SRCFILES) $(SHARED_SRCFILES) $(VERSION_SRCFILES)
	@for %%x in (source\main.xasm) do $(MADS) -i:autobuild -i:autobuild_default -d:_KERNEL_XLXE=1 -d:_KERNEL_SOFTKICK=1 -s -p -i:source\Shared -b:$$c000 -l:$(OUT)\kernelxlsoft.lst -t:$(OUT)\kernelxlsoft.lab -o:$(OUT)\kernelxlsoft.bin %~fx

$(OUT)\nokernel.rom: $(NOKERNEL_SRCFILES) $(SHARED_SRCFILESMIN)
	@$(MADS) -s -p -i:source\shared -b:$$c000 -l:$(OUT)\nokernel.lst -o:$(OUT)\nokernel.rom source\nokernel\main.xasm

$(OUT)\basic.rom: $(BASIC_SRCFILES)
	@$(MADS) -s -p -b:$$a000 -l:$(OUT)\basic.lst -o:$(OUT)\basic.rom source\basic\main.xasm

$(OUT)\superkernel.rom: $(SUPERKERNEL_SRCFILES)
	@$(MADS) -s -p -i:source\shared -b:$$f800 -l:$(OUT)\superkernel.lst -o:$(OUT)\superkernel.rom source\superkernel\main.xasm

$(OUT)\nocartridge.rom: $(NOCARTRIDGE_SRCFILES)
	@$(MADS) -s -p -b:$$b000 -l:$(OUT)\nocartridge.lst -o:$(OUT)\nocartridge.rom source\nocartridge\main.xasm

$(OUT)\atdiskloader128.bin: $(ATDISKLOADER_SRCFILES)
	@$(MADS) -d:MEMORY=128 -s -p -b:$$0700 -l:$(OUT)\atdiskloader128.lst -t:$(OUT)\atdiskloader128.lab -o:$(OUT)\atdiskloader128.bin source\ATDiskLoader\main.s

$(OUT)\atdiskloader64.bin: $(ATDISKLOADER_SRCFILES)
	@$(MADS) -d:MEMORY=64 -s -p -b:$$0700 -l:$(OUT)\atdiskloader64.lst -l:$(OUT)\atdiskloader64.lab -o:$(OUT)\atdiskloader64.bin source\ATDiskLoader\main.s

$(OUT)\nohdbios.rom: $(NOHDBIOS_SRCFILES)
	@$(MADS) -s -p -b:$$d800 -l:$(OUT)\nohdbios.lst -o:$(OUT)\nohdbios.rom source\NoHDBIOS\main.s

$(OUT)\nogame.rom: $(NOGAME_SRCFILES)
	@$(MADS) -s -p -b:$$a000 -l:$(OUT)\nogame.lst -o:$(OUT)\nogame.rom source\nogame\main.s

$(OUT)\ultimate.rom: $(ULTIMATE_SRCFILES) $(SHARED_SRCFILES)
	@$(MADS) -s -p -i:source\Shared -l:$(OUT)\ultimate.lst -o:$(OUT)\ultimate.rom source\ultimate\main.s

$(OUT)\ultimate.lzrom: $(OUT)\ultimate.rom $(ATCOMPILER)
	@$(ATCOMPILER) lzpack "$(OUT)\ultimate.rom" "$(OUT)\ultimate.lzrom"

$(OUT)\bootsecdos2.bin: $(BOOTSECDOS2_SRCFILES) $(SHARED_INCFILES)
	@$(MADS) -s -p -i:source\Shared -l:$(OUT)\bootsecdos2.lst -o:$(OUT)\bootsecdos2.bin source\bootsector\bootsecdos2.s

$(INT)\850handler-2800.obx: $(SHARED_INCFILES) source\850\handler.s
	@$(MADS) -D:BASEADDR=10240 -D:HIBUILD=0 -s -p -i:source\Shared -o:$(INT)\850handler-2800.obx -l:$(OUT)\850handler.lst source\850\handler.s
	
$(INT)\850handler-2801.obx: $(SHARED_INCFILES) source\850\handler.s
	@$(MADS) -D:BASEADDR=10241 -D:HIBUILD=0 -s -p -i:source\Shared -o:$(INT)\850handler-2801.obx source\850\handler.s

$(INT)\850handler-a800.obx: $(SHARED_INCFILES) source\850\handler.s
	@$(MADS) -D:BASEADDR=43008 -D:HIBUILD=0 -s -p -i:source\Shared -o:$(INT)\850handler-a800.obx source\850\handler.s

$(INT)\850handler-2800hi.obx: $(SHARED_INCFILES) source\850\handler.s
	@$(MADS) -D:BASEADDR=10240 -D:HIBUILD=1 -s -p -i:source\Shared -o:$(INT)\850handler-2800hi.obx source\850\handler.s

$(INT)\850handler-relocs.inc $(OUT)\850handler.bin: $(INT)\850handler-2800.obx $(INT)\850handler-2801.obx $(INT)\850handler-a800.obx $(INT)\850handler-2800hi.obx $(ATCOMPILER)
	@$(ATCOMPILER) makereloc $(INT)\850handler-2800.obx $(INT)\850handler-2801.obx $(INT)\850handler-a800.obx $(INT)\850handler-2800hi.obx $(OUT)\850handler.bin $(INT)\850handler-relocs.inc

$(OUT)\850relocator.bin: $(SHARED_INCFILES) source\850\relocator.s $(INT)\850handler-2800.obx $(INT)\850handler-2801.obx $(INT)\850handler-a800.obx $(INT)\850handler-relocs.inc
	@$(MADS) -s -p -i:source\Shared -i:$(INT) -l:$(OUT)\850relocator.lst -o:$(OUT)\850relocator.bin source\850\relocator.s
