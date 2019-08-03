# -*- makefile -*-
#
# Makefile for building NASM using OpenWatcom
# cross-compile on a DOS/Win32/OS2 platform host
#

top_srcdir  = .
srcdir      = .
VPATH       = $(srcdir)\asm;$(srcdir)\x86;asm;x86;$(srcdir)\macros;macros;$(srcdir)\output;$(srcdir)\lib;$(srcdir)\common;$(srcdir)\stdlib;$(srcdir)\nasmlib;$(srcdir)\disasm
prefix      = C:\Program Files\NASM
exec_prefix = $(prefix)
bindir      = $(prefix)\bin
mandir      = $(prefix)\man

CC      = *wcl386
DEBUG       =
CFLAGS      = -zq -6 -ox -wx -ze -fpi $(DEBUG)
BUILD_CFLAGS    = $(CFLAGS) $(%TARGET_CFLAGS)
INTERNAL_CFLAGS = -I$(srcdir) -I. -I$(srcdir)\include -I$(srcdir)\x86 -Ix86 -I$(srcdir)\asm -Iasm -I$(srcdir)\disasm -I$(srcdir)\output
ALL_CFLAGS  = $(BUILD_CFLAGS) $(INTERNAL_CFLAGS)
LD      = *wlink
LDEBUG      =
LDFLAGS     = op quiet $(%TARGET_LFLAGS) $(LDEBUG)
LIBS        =
STRIP       = wstrip

PERL		= perl
PERLFLAGS	= -I$(srcdir)\perllib -I$(srcdir)
RUNPERL         = $(PERL) $(PERLFLAGS)

MAKENSIS        = makensis

# Binary suffixes
O               = obj
X               = .exe

# WMAKE errors out if a suffix is declared more than once, including
# its own built-in declarations.  Thus, we need to explicitly clear the list
# first.  Also, WMAKE only allows implicit rules that point "to the left"
# in this list!
.SUFFIXES:
.SUFFIXES: .man .1 .$(O) .i .c

# Needed to find C files anywhere but in the current directory
.c : $(VPATH)

.c.$(O):
    @set INCLUDE=
    $(CC) -c $(ALL_CFLAGS) -fo=$^@ $[@

#-- Begin File Lists --#
# Edit in Makefile.in, not here!
NASM =	asm\nasm.$(O)
NDISASM = disasm\ndisasm.$(O)

LIBOBJ = stdlib\snprintf.$(O) stdlib\vsnprintf.$(O) stdlib\strlcpy.$(O) &
	stdlib\strnlen.$(O) stdlib\strrchrnul.$(O) &
	&
	nasmlib\ver.$(O) &
	nasmlib\crc64.$(O) nasmlib\malloc.$(O) &
	nasmlib\md5c.$(O) nasmlib\string.$(O) &
	nasmlib\file.$(O) nasmlib\mmap.$(O) nasmlib\ilog2.$(O) &
	nasmlib\realpath.$(O) nasmlib\path.$(O) &
	nasmlib\filename.$(O) nasmlib\srcfile.$(O) &
	nasmlib\zerobuf.$(O) nasmlib\readnum.$(O) nasmlib\bsi.$(O) &
	nasmlib\rbtree.$(O) nasmlib\hashtbl.$(O) &
	nasmlib\raa.$(O) nasmlib\saa.$(O) &
	nasmlib\strlist.$(O) &
	nasmlib\perfhash.$(O) nasmlib\badenum.$(O) &
	&
	common\common.$(O) &
	&
	x86\insnsa.$(O) x86\insnsb.$(O) x86\insnsd.$(O) x86\insnsn.$(O) &
	x86\regs.$(O) x86\regvals.$(O) x86\regflags.$(O) x86\regdis.$(O) &
	x86\disp8.$(O) x86\iflag.$(O) &
	&
	asm\error.$(O) &
	asm\float.$(O) &
	asm\directiv.$(O) asm\directbl.$(O) &
	asm\pragma.$(O) &
	asm\assemble.$(O) asm\labels.$(O) asm\parser.$(O) &
	asm\preproc.$(O) asm\quote.$(O) asm\pptok.$(O) &
	asm\listing.$(O) asm\eval.$(O) asm\exprlib.$(O) asm\exprdump.$(O) &
	asm\stdscan.$(O) &
	asm\strfunc.$(O) asm\tokhash.$(O) &
	asm\segalloc.$(O) &
	asm\preproc-nop.$(O) &
	asm\rdstrnum.$(O) &
	&
	macros\macros.$(O) &
	&
	output\outform.$(O) output\outlib.$(O) output\legacy.$(O) &
	output\strtbl.$(O) &
	output\nulldbg.$(O) output\nullout.$(O) &
	output\outbin.$(O) output\outaout.$(O) output\outcoff.$(O) &
	output\outelf.$(O) &
	output\outobj.$(O) output\outas86.$(O) output\outrdf2.$(O) &
	output\outdbg.$(O) output\outieee.$(O) output\outmacho.$(O) &
	output\codeview.$(O) &
	&
	disasm\disasm.$(O) disasm\sync.$(O)

SUBDIRS  = stdlib nasmlib output asm disasm x86 common macros
XSUBDIRS = test doc nsis rdoff
DEPDIRS  = . include config x86 rdoff $(SUBDIRS)
#-- End File Lists --#

what:   .SYMBOLIC
    @echo Please build "dos", "win32", "os2" or "linux386"

dos:    .SYMBOLIC
    @set TARGET_CFLAGS=-bt=DOS -I"$(%WATCOM)\h"
    @set TARGET_LFLAGS=sys causeway
    @%make all

win32:  .SYMBOLIC
    @set TARGET_CFLAGS=-bt=NT -I"$(%WATCOM)\h" -I"$(%WATCOM)\h\nt"
    @set TARGET_LFLAGS=sys nt
    @%make all

os2:    .SYMBOLIC
    @set TARGET_CFLAGS=-bt=OS2 -I"$(%WATCOM)\h" -I"$(%WATCOM)\h\os2"
    @set TARGET_LFLAGS=sys os2v2
    @%make all

linux386:   .SYMBOLIC
    @set TARGET_CFLAGS=-bt=LINUX -I"$(%WATCOM)\lh"
    @set TARGET_LFLAGS=sys linux
    @%make all

all: perlreq nasm$(X) ndisasm$(X) .SYMBOLIC
#   cd rdoff && $(MAKE) all

NASMLIB = nasm.lib

nasm$(X): $(NASM) $(NASMLIB)
    $(LD) $(LDFLAGS) name nasm$(X) libr {$(NASMLIB) $(LIBS)} file {$(NASM)}

ndisasm$(X): $(NDISASM) $(LIBOBJ)
    $(LD) $(LDFLAGS) name ndisasm$(X) libr {$(NASMLIB) $(LIBS)} file {$(NDISASM)}

nasm.lib: $(LIBOBJ)
    wlib -q -b -n $@ $(LIBOBJ)

#-- Begin Generated File Rules --#
# Edit in Makefile.in, not here!

# These source files are automagically generated from data files using
# Perl scripts. They're distributed, though, so it isn't necessary to
# have Perl just to recompile NASM from the distribution.

# Perl-generated source files
PERLREQ = x86\insnsb.c x86\insnsa.c x86\insnsd.c x86\insnsi.h x86\insnsn.c &
	  x86\regs.c x86\regs.h x86\regflags.c x86\regdis.c x86\regdis.h &
	  x86\regvals.c asm\tokhash.c asm\tokens.h asm\pptok.h asm\pptok.c &
	  x86\iflag.c x86\iflaggen.h &
	  macros\macros.c &
	  asm\pptok.ph asm\directbl.c asm\directiv.h &
	  version.h version.mac version.mak nsis\version.nsh

INSDEP = x86\insns.dat x86\insns.pl x86\insns-iflags.ph

x86\iflag.c: $(INSDEP)
	$(RUNPERL) $(srcdir)\x86\insns.pl -fc &
		$(srcdir)\x86\insns.dat x86\iflag.c
x86\iflaggen.h: $(INSDEP)
	$(RUNPERL) $(srcdir)\x86\insns.pl -fh &
		$(srcdir)\x86\insns.dat x86\iflaggen.h
x86\insnsb.c: $(INSDEP)
	$(RUNPERL) $(srcdir)\x86\insns.pl -b &
		$(srcdir)\x86\insns.dat x86\insnsb.c
x86\insnsa.c: $(INSDEP)
	$(RUNPERL) $(srcdir)\x86\insns.pl -a &
		$(srcdir)\x86\insns.dat x86\insnsa.c
x86\insnsd.c: $(INSDEP)
	$(RUNPERL) $(srcdir)\x86\insns.pl -d &
		$(srcdir)\x86\insns.dat x86\insnsd.c
x86\insnsi.h: $(INSDEP)
	$(RUNPERL) $(srcdir)\x86\insns.pl -i &
		$(srcdir)\x86\insns.dat x86\insnsi.h
x86\insnsn.c: $(INSDEP)
	$(RUNPERL) $(srcdir)\x86\insns.pl -n &
		$(srcdir)\x86\insns.dat x86\insnsn.c

# These files contains all the standard macros that are derived from
# the version number.
version.h: version version.pl
	$(RUNPERL) $(srcdir)\version.pl h < $(srcdir)\version > version.h
version.mac: version version.pl
	$(RUNPERL) $(srcdir)\version.pl mac < $(srcdir)\version > version.mac
version.sed: version version.pl
	$(RUNPERL) $(srcdir)\version.pl sed < $(srcdir)\version > version.sed
version.mak: version version.pl
	$(RUNPERL) $(srcdir)\version.pl make < $(srcdir)\version > version.mak
nsis\version.nsh: version version.pl
	$(RUNPERL) $(srcdir)\version.pl nsis < $(srcdir)\version > nsis\version.nsh

# This source file is generated from the standard macros file
# `standard.mac' by another Perl script. Again, it's part of the
# standard distribution.
macros\macros.c: macros\macros.pl asm\pptok.ph version.mac &
	$(srcdir)\macros\*.mac $(srcdir)\output\*.mac
	$(RUNPERL) $(srcdir)\macros\macros.pl version.mac &
		$(srcdir)\macros\*.mac $(srcdir)\output\*.mac

# These source files are generated from regs.dat by yet another
# perl script.
x86\regs.c: x86\regs.dat x86\regs.pl
	$(RUNPERL) $(srcdir)\x86\regs.pl c &
		$(srcdir)\x86\regs.dat > x86\regs.c
x86\regflags.c: x86\regs.dat x86\regs.pl
	$(RUNPERL) $(srcdir)\x86\regs.pl fc &
		$(srcdir)\x86\regs.dat > x86\regflags.c
x86\regdis.c: x86\regs.dat x86\regs.pl
	$(RUNPERL) $(srcdir)\x86\regs.pl dc &
		$(srcdir)\x86\regs.dat > x86\regdis.c
x86\regdis.h: x86\regs.dat x86\regs.pl
	$(RUNPERL) $(srcdir)\x86\regs.pl dh &
		$(srcdir)\x86\regs.dat > x86\regdis.h
x86\regvals.c: x86\regs.dat x86\regs.pl
	$(RUNPERL) $(srcdir)\x86\regs.pl vc &
		$(srcdir)\x86\regs.dat > x86\regvals.c
x86\regs.h: x86\regs.dat x86\regs.pl
	$(RUNPERL) $(srcdir)\x86\regs.pl h &
		$(srcdir)\x86\regs.dat > x86\regs.h

# Assembler token hash
asm\tokhash.c: x86\insns.dat x86\regs.dat asm\tokens.dat asm\tokhash.pl &
	perllib\phash.ph
	$(RUNPERL) $(srcdir)\asm\tokhash.pl c &
		$(srcdir)\x86\insns.dat $(srcdir)\x86\regs.dat &
		$(srcdir)\asm\tokens.dat > asm\tokhash.c

# Assembler token metadata
asm\tokens.h: x86\insns.dat x86\regs.dat asm\tokens.dat asm\tokhash.pl &
	perllib\phash.ph
	$(RUNPERL) $(srcdir)\asm\tokhash.pl h &
		$(srcdir)\x86\insns.dat $(srcdir)\x86\regs.dat &
		$(srcdir)\asm\tokens.dat > asm\tokens.h

# Preprocessor token hash
asm\pptok.h: asm\pptok.dat asm\pptok.pl perllib\phash.ph
	$(RUNPERL) $(srcdir)\asm\pptok.pl h &
		$(srcdir)\asm\pptok.dat asm\pptok.h
asm\pptok.c: asm\pptok.dat asm\pptok.pl perllib\phash.ph
	$(RUNPERL) $(srcdir)\asm\pptok.pl c &
		$(srcdir)\asm\pptok.dat asm\pptok.c
asm\pptok.ph: asm\pptok.dat asm\pptok.pl perllib\phash.ph
	$(RUNPERL) $(srcdir)\asm\pptok.pl ph &
		$(srcdir)\asm\pptok.dat asm\pptok.ph

# Directives hash
asm\directiv.h: asm\directiv.dat nasmlib\perfhash.pl perllib\phash.ph
	$(RUNPERL) $(srcdir)\nasmlib\perfhash.pl h &
		$(srcdir)\asm\directiv.dat asm\directiv.h
asm\directbl.c: asm\directiv.dat nasmlib\perfhash.pl perllib\phash.ph
	$(RUNPERL) $(srcdir)\nasmlib\perfhash.pl c &
		$(srcdir)\asm\directiv.dat asm\directbl.c

#-- End Generated File Rules --#

perlreq: $(PERLREQ) .SYMBOLIC

#-- Begin NSIS Rules --#
# Edit in Makefile.in, not here!

# NSIS is not built except by explicit request, as it only applies to
# Windows platforms
nsis\arch.nsh: nsis\getpearch.pl nasm$(X)
	$(PERL) $(srcdir)\nsis\getpearch.pl nasm$(X) > nsis\arch.nsh

# Should only be done after "make everything".
# The use of redirection here keeps makensis from moving the cwd to the
# source directory.
nsis: nsis\nasm.nsi nsis\arch.nsh nsis\version.nsh
	$(MAKENSIS) -Dsrcdir="$(srcdir)" -Dobjdir="$(objdir)" - < nsis\nasm.nsi

#-- End NSIS Rules --#

clean: .SYMBOLIC
    rm -f *.$(O) *.s *.i
    rm -f asm\*.$(O) asm\*.s asm\*.i
    rm -f x86\*.$(O) x86\*.s x86\*.i
    rm -f lib\*.$(O) lib\*.s lib\*.i
    rm -f macros\*.$(O) macros\*.s macros\*.i
    rm -f output\*.$(O) output\*.s output\*.i
    rm -f common\*.$(O) common\*.s common\*.i
    rm -f stdlib\*.$(O) stdlib\*.s stdlib\*.i
    rm -f nasmlib\*.$(O) nasmlib\*.s nasmlib\*.i
    rm -f disasm\*.$(O) disasm\*.s disasm\*.i
    rm -f config.h config.log config.status
    rm -f nasm$(X) ndisasm$(X) $(NASMLIB)
#   cd rdoff && $(MAKE) clean

distclean: clean .SYMBOLIC
    rm -f config.h config.log config.status
    rm -f Makefile *~ *.bak *.lst *.bin
    rm -f output\*~ output\*.bak
    rm -f test\*.lst test\*.bin test\*.$(O) test\*.bin
#   -del \s autom4te*.cache
#   cd rdoff && $(MAKE) distclean

cleaner: clean .SYMBOLIC
    rm -f $(PERLREQ)
    rm -f *.man
    rm -f nasm.spec
#   cd doc && $(MAKE) clean

spotless: distclean cleaner .SYMBOLIC
    rm -f doc\Makefile doc\*~ doc\*.bak

strip: .SYMBOLIC
    $(STRIP) *.exe

rdf:
#   cd rdoff && $(MAKE)

doc:
#   cd doc && $(MAKE) all

everything: all doc rdf

#
# This build dependencies in *ALL* makefiles.  Partially for that reason,
# it's expected to be invoked manually.
#
alldeps: perlreq .SYMBOLIC
    $(PERL) syncfiles.pl Makefile.in Mkfiles\openwcom.mak
    $(PERL) mkdep.pl -M Makefile.in Mkfiles\openwcom.mak -- . output lib

#-- Magic hints to mkdep.pl --#
# @object-ending: ".$(O)"
# @path-separator: "\"
# @exclude: "config/config.h"
# @continuation: "&"
#-- Everything below is generated by mkdep.pl - do not edit --#
