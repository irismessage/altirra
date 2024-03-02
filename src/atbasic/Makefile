OUT = ..\..\out\Release
OBJ = ..\..\obj\Release\ATBasic
SRC = source

SRCFILES = \
	source\atbasic.s \
	source\data.s \
	source\error.s \
	source\evaluator.s \
	source\exec.s \
	source\functions.s \
	source\io.s \
	source\list.s \
	makefile \
	source\math.s \
	source\memory.s \
	source\parser.s \
	source\parserbytecode.s \
	source\statements.s \
	source\system.inc \
	source\tokens.inc \
	source\util.s \
	source\variables.s

.SUFFIXES: .s .xex .bin

all: $(OUT)\atbasicx.xex $(OUT)\atbasic.bin

clean:
	del "$(OUT)\atbasicx.xex" "$(OUT)\atbasic.bin"

$(OUT)\atbasicx.xex:	$(SRCFILES)
	@if not exist $(OUT) md $(OUT)
	@for %x in (source\atbasic.s) do "$(MADS)" -c -s -d:CART=0 -o:$@ -l:$(@R).lst -t:$(@R).lab "%~fx"
	
$(OUT)\atbasic.bin:		$(SRCFILES)
	@if not exist $(OUT) md $(OUT)
	@for %x in (source\atbasic.s) do "$(MADS)" -c -s -d:CART=1 -b:$$a000 -o:$@ -l:$(@R).lst -t:$(@R).lab "%~fx"

