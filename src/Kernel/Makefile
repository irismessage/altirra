MADS = mads.exe

SRCFILES = source\main.xasm \
source\screen.xasm \
source\init.xasm \
source\bugcheck.xasm \
source\loader.xasm \
source\sio.xasm \
source\boot.xasm \
source\disk.xasm \
source\nmi.xasm \
source\irq.xasm \
source\blackboard.xasm \
source\cio.xasm \
source\editor.xasm \
source\mathpack.xasm

NOKERNEL_SRCFILES = source\nokernel\main.xasm
BASIC_SRCFILES = source\basic\main.xasm
SUPERKERNEL_SRCFILES = source\superkernel\main.xasm source\superkernel\includes.xasm
NOCARTRIDGE_SRCFILES = source\nocartridge\main.xasm
ATDISKLOADER_SRCFILES = source\ATDiskLoader\main.s

all: kernel nokernel basic superkernel nocartridge atdiskloader64 atdiskloader128

clean:
	if exist "$(OUT)\kernel.rom" del "$(OUT)\kernel.rom"
	if exist "$(OUT)\nokernel.rom" del "$(OUT)\nokernel.rom"
	if exist "$(OUT)\basic.rom" del "$(OUT)\basic.rom"
	if exist "$(OUT)\superkernel.rom" del "$(OUT)\superkernel.rom"
	if exist "$(OUT)\nocartridge.rom" del "$(OUT)\nocartridge.rom"
	if exist "$(OUT)\atdiskload64.rom" del "$(OUT)\atdiskload64.rom"
	if exist "$(OUT)\atdiskload128.rom" del "$(OUT)\atdiskload128.rom"

kernel: $(OUT)\kernel.rom

nokernel: $(OUT)\nokernel.rom

basic: $(OUT)\basic.rom

superkernel: $(OUT)\superkernel.rom

nocartridge: $(OUT)\nocartridge.rom

atdiskloader64: $(OUT)\atdiskloader64.bin

atdiskloader128: $(OUT)\atdiskloader128.bin

$(OUT)\kernel.rom: $(SRCFILES)
	@$(MADS) -s -p -b:$$c000 -l:$(OUT)\kernel.lst -o:$(OUT)\kernel.rom source\main.xasm

$(OUT)\nokernel.rom: $(NOKERNEL_SRCFILES)
	@$(MADS) -s -p -b:$$c000 -l:$(OUT)\nokernel.lst -o:$(OUT)\nokernel.rom source\nokernel\main.xasm

$(OUT)\basic.rom: $(BASIC_SRCFILES)
	@$(MADS) -s -p -b:$$a000 -l:$(OUT)\basic.lst -o:$(OUT)\basic.rom source\basic\main.xasm

$(OUT)\superkernel.rom: $(SUPERKERNEL_SRCFILES)
	@$(MADS) -s -p -b:$$f800 -l:$(OUT)\superkernel.lst -o:$(OUT)\superkernel.rom source\superkernel\main.xasm

$(OUT)\nocartridge.rom: $(NOCARTRIDGE_SRCFILES)
	@$(MADS) -s -p -b:$$b000 -l:$(OUT)\nocartridge.lst -o:$(OUT)\nocartridge.rom source\nocartridge\main.xasm

$(OUT)\atdiskloader128.bin: $(ATDISKLOADER_SRCFILES)
	@$(MADS) -d:MEMORY=128 -s -p -b:$$0800 -l:$(OUT)\atdiskloader128.lst -o:$(OUT)\atdiskloader128.bin source\ATDiskLoader\main.s

$(OUT)\atdiskloader64.bin: $(ATDISKLOADER_SRCFILES)
	@$(MADS) -d:MEMORY=64 -s -p -b:$$0800 -l:$(OUT)\atdiskloader64.lst -o:$(OUT)\atdiskloader64.bin source\ATDiskLoader\main.s
