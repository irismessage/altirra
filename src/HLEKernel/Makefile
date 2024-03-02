MADS = mads.exe
HLECC = $(OUT)\hlecompiler.exe

SRCFILES = source\kernel.k \
source\blackboard.k \
source\boot.k \
source\bugcheck.k \
source\cio.k \
source\disk.k \
source\editor.k \
source\init.k \
source\irq.k \
source\keyboard.k \
source\nmi.k \
source\screen.k \
source\sio.k \
source\mathpack.k

all: hlekernel

hlekernel: $(OUT)\hlekernel.rom $(HLECC)

$(OUT)\hlekernel.rom $(OBJ)\hlekernel.xasm $(OBJ)\hlekernel_gen.inl $(OBJ)\hlekernel_gen.h: $(SRCFILES)
	if not exist "$(OBJ)" md "$(OBJ)"
	$(HLECC) source\kernel.k $(OBJ)\hlekernel.xasm $(OBJ)\hlekernel_gen.inl $(OBJ)\hlekernel_gen.h
	$(MADS) -p -b:$$c000 -l:$(OUT)\hlekernel.lst -o:$(OUT)\hlekernel.rom -l $(OBJ)\hlekernel.xasm
