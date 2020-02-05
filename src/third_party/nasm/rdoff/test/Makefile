RDT  = $(patsubst %.asm,%.rdf,$(wildcard *.asm))
NASM = ../../nasm

all: $(RDT)

%.rdf: %.asm
	$(NASM) -f rdf -o $@ -l $*.lst $<

clean:
	rm -f *.rdf *.rdx *.lst
