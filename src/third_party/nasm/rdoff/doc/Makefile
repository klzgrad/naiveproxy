#
# Make info documentation
#

AUXFILES = *.aux *.cp *.fn *.ky *.log *.pg *.toc *.tp *.vr *.dvi

SRCS = rdoff.texi
OUTS = rdoff.info rdoff.html rdoff.ps rdoff.pdf


## Implicit rules
%.html : %.texi
	makeinfo --html $<

%.dvi : %.texi
	texi2dvi $<

%.ps: %.dvi
	dvips $<

%.pdf: %.dvi
	dvipdf $<


## Explicit rules
all: info

info: rdoff.info

html: rdoff.html

ps: rdoff.ps

pdf: rdoff.pdf

clean:
	rm -f $(OUTS) $(AUXFILES)
