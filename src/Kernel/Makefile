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
source\editor.xasm

all: kernel

kernel: $(OUT)\kernel.rom

$(OUT)\kernel.rom: $(SRCFILES)
#	-$(MADS) -x -l main.xasm
	-$(MADS) -p -b:$$c000 -l:$(OUT)\kernel.lst -o:$(OUT)\kernel.rom -l source\main.xasm
