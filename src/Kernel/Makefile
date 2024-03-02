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

all: kernel nokernel basic

clean:
	if exist "$(OUT)\kernel.rom" del "$(OUT)\kernel.rom"
	if exist "$(OUT)\nokernel.rom" del "$(OUT)\nokernel.rom"
	if exist "$(OUT)\basic.rom" del "$(OUT)\basic.rom"

kernel: $(OUT)\kernel.rom

nokernel: $(OUT)\nokernel.rom

basic: $(OUT)\basic.rom

$(OUT)\kernel.rom: $(SRCFILES)
#	-$(MADS) -x -l main.xasm
	-$(MADS) -p -b:$$c000 -l:$(OUT)\kernel.lst -o:$(OUT)\kernel.rom -l source\main.xasm

$(OUT)\nokernel.rom: $(NOKERNEL_SRCFILES)
	-$(MADS) -p -b:$$c000 -l:$(OUT)\nokernel.lst -o:$(OUT)\nokernel.rom -l source\nokernel\main.xasm

$(OUT)\basic.rom: $(BASIC_SRCFILES)
	-$(MADS) -p -b:$$a000 -l:$(OUT)\basic.lst -o:$(OUT)\basic.rom -l source\basic\main.xasm
