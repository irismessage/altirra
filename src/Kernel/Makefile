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

all: kernel nokernel basic superkernel nocartridge

clean:
	if exist "$(OUT)\kernel.rom" del "$(OUT)\kernel.rom"
	if exist "$(OUT)\nokernel.rom" del "$(OUT)\nokernel.rom"
	if exist "$(OUT)\basic.rom" del "$(OUT)\basic.rom"
	if exist "$(OUT)\superkernel.rom" del "$(OUT)\superkernel.rom"
	if exist "$(OUT)\nocartridge.rom" del "$(OUT)\nocartridge.rom"

kernel: $(OUT)\kernel.rom

nokernel: $(OUT)\nokernel.rom

basic: $(OUT)\basic.rom

superkernel: $(OUT)\superkernel.rom

nocartridge: $(OUT)\nocartridge.rom

$(OUT)\kernel.rom: $(SRCFILES)
#	-$(MADS) -x -l main.xasm
	-$(MADS) -p -b:$$c000 -l:$(OUT)\kernel.lst -o:$(OUT)\kernel.rom -l source\main.xasm

$(OUT)\nokernel.rom: $(NOKERNEL_SRCFILES)
	-$(MADS) -p -b:$$c000 -l:$(OUT)\nokernel.lst -o:$(OUT)\nokernel.rom -l source\nokernel\main.xasm

$(OUT)\basic.rom: $(BASIC_SRCFILES)
	-$(MADS) -p -b:$$a000 -l:$(OUT)\basic.lst -o:$(OUT)\basic.rom -l source\basic\main.xasm

$(OUT)\superkernel.rom: $(SUPERKERNEL_SRCFILES)
	-$(MADS) -p -b:$$f800 -l:$(OUT)\superkernel.lst -o:$(OUT)\superkernel.rom -l source\superkernel\main.xasm

$(OUT)\nocartridge.rom: $(NOCARTRIDGE_SRCFILES)
	-$(MADS) -p -b:$$b000 -l:$(OUT)\nocartridge.lst -o:$(OUT)\nocartridge.rom -l source\nocartridge\main.xasm
