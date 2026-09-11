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
CFLAGS      = -zq -6 -ox -wx -wcd=124 -ze -fpi $(DEBUG)
BUILD_CFLAGS    = $(CFLAGS) $(%TARGET_CFLAGS)
INTERNAL_CFLAGS = -I$(srcdir) -I. -I$(srcdir)\include -I$(srcdir)\x86 -Ix86 -I$(srcdir)\asm -Iasm -I$(srcdir)\disasm -I$(srcdir)\output
ALL_CFLAGS  = $(BUILD_CFLAGS) $(INTERNAL_CFLAGS)
LD      = *wlink
LDEBUG      =
LDFLAGS     = op q $(%TARGET_LFLAGS) $(LDEBUG)
LIBS        =
STRIP       = wstrip

PERL		= perl
PERLFLAGS	= -I$(srcdir)\perllib -I$(srcdir)
RUNPERL         = $(PERL) $(PERLFLAGS)

.BEFORE
	set COPYCMD=/y

# rm is handled internally by WMAKE, so it does work even on non-Unix systems
RM_F		= -rm -f
LN_S		= copy
EMPTY		= %create
SIDE		= %null Created by side effect

MAKENSIS        = makensis

# Binary suffixes
O               = obj
A		= lib
X               = .exe

# WMAKE errors out if a suffix is declared more than once, including
# its own built-in declarations.  Thus, we need to explicitly clear the list
# first.  Also, WMAKE only allows implicit rules that point "to the left"
# in this list!
.SUFFIXES:
.SUFFIXES: .man .1 .obj .i .c .lib .exe

# Needed to find C files anywhere but in the current directory
.c : $(VPATH)

.c.obj:
    @set INCLUDE=
    $(CC) -c $(ALL_CFLAGS) -fo=$^@ $[@

MANIFEST =

DIRS =

ZLIB    = $(ZLIBOBJ)

#-- Begin File Lists --#
# Edit in Makefile.in, not here!
NASM    = asm\nasm.obj
NDISASM = disasm\ndisasm.obj

PROGOBJ = $(NASM) $(NDISASM)
PROGS   = nasm$(X) ndisasm$(X)

# Files dependent on warnings.dat
WARNOBJ   = asm\warnings.obj
WARNFILES = asm\warnings_c.h include\warnings.h doc\warnings.src

OUTPUTOBJ = &
	output\outform.obj output\outlib.obj &
	output\nulldbg.obj output\nullout.obj &
	output\outbin.obj output\outaout.obj output\outcoff.obj &
	output\outelf.obj &
	output\outobj.obj output\outas86.obj &
	output\outdbg.obj output\outieee.obj output\outmacho.obj &
	output\codeview.obj

# The source files for these objects are scanned for warnings
LIBOBJ_W = &
	nasmlib\readnum.obj &
	&
	asm\error.obj &
	asm\floats.obj &
	asm\directiv.obj &
	asm\pragma.obj &
	asm\assemble.obj asm\labels.obj asm\parser.obj &
	asm\preproc.obj asm\quote.obj &
	asm\listing.obj asm\eval.obj asm\exprlib.obj asm\exprdump.obj &
	asm\stdscan.obj &
	asm\getbool.obj &
	asm\strfunc.obj &
	asm\segalloc.obj &
	asm\rdstrnum.obj &
	asm\srcfile.obj &
	&
	$(OUTPUTOBJ)

# The source files for these objects are NOT scanned for warnings;
# normally this will include all generated files.
# It is entirely possible that it may be necessary to move some of these
# files to LIBOBJ_W, notably $(OUTPUTOBJ)
LIBOBJ_NW = &
	stdlib\snprintf.obj stdlib\vsnprintf.obj stdlib\strlcpy.obj &
	stdlib\strnlen.obj stdlib\strrchrnul.obj &
	&
	asm\directbl.obj &
	asm\pptok.obj &
	asm\tokhash.obj &
	asm\uncompress.obj &
	&
	macros\macros.obj &
	&
	nasmlib\ver.obj &
	nasmlib\alloc.obj nasmlib\asprintf.obj &
	nasmlib\crc32b.obj nasmlib\crc64.obj nasmlib\md5c.obj &
	nasmlib\string.obj nasmlib\nctype.obj &
	nasmlib\file.obj nasmlib\mmap.obj nasmlib\ilog2.obj &
	nasmlib\realpath.obj nasmlib\path.obj &
	nasmlib\filename.obj nasmlib\rlimit.obj &
	nasmlib\numstr.obj &
	nasmlib\zerobuf.obj nasmlib\bsi.obj &
	nasmlib\rbtree.obj nasmlib\hashtbl.obj &
	nasmlib\raa.obj nasmlib\saa.obj &
	nasmlib\strlist.obj &
	nasmlib\perfhash.obj nasmlib\badenum.obj &
	&
	common\common.obj &
	&
	x86\insnsa.obj x86\insnsb.obj x86\insnsn.obj &
	x86\regs.obj x86\regvals.obj x86\regflags.obj &
	x86\iflag.obj &
	&
	$(WARNOBJ)

# Objects which are only used for the disassembler
LIBOBJ_DIS = &
	disasm\disasm.obj disasm\sync.obj disasm\prefix.obj &
	&
	x86\insnsd.obj x86\regdis.obj

# Objects for the local copy of zlib. The variable ZLIB is set to
# $(ZLIBOBJ) if the internal version of zlib should be used.
ZLIBOBJ = &
	zlib\adler32.obj &
	zlib\crc32.obj &
	zlib\infback.obj &
	zlib\inffast.obj &
	zlib\inflate.obj &
	zlib\inftrees.obj &
	zlib\zutil.obj

LIBOBJ    = $(LIBOBJ_W) $(LIBOBJ_NW) $(ZLIB)
ALLOBJ_W  = $(NASM) $(LIBOBJ_W)
ALLOBJ    = $(PROGOBJ) $(LIBOBJ)
SUBDIRS  = stdlib nasmlib include config output asm disasm x86 &
	   common zlib macros misc
XSUBDIRS = nsis win test doc editors
DEPDIRS  = . $(SUBDIRS)

EDITORS  = editors\nasmtok.el editors\nasmtok.json

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
NDISLIB = ndisasm.lib

nasm$(X): $(NASM) $(NASMLIB)
    $(LD) $(LDFLAGS) name nasm$(X) libr {$(NASMLIB) $(LIBS)} file {$(NASM)}

ndisasm$(X): $(NDISASM) $(NDISLIB) $(NASMLIB)
    $(LD) $(LDFLAGS) name ndisasm$(X) libr {$(NDISLIB) $(NASMLIB) $(LIBS)} file {$(NDISASM)}

nasm.lib: $(LIBOBJ)
    wlib -q -b -n $@ $(LIBOBJ)

ndisasm.lib: $(LIBOBJ_DIS)
    wlib -q -b -n $@ $(LIBOBJ_DIS)

# These are specific to certain Makefile syntaxes (what are they
# actually supposed to look like for wmake?)
WARNTIMES = $(WARNFILES:=.time)
WARNSRCS  = $(LIBOBJ_NW:.obj=.c)

#-- Begin Generated File Rules --#
# Edit in Makefile.in, not here!

# These source files are automagically generated from data files using
# Perl scripts. They're distributed, though, so it isn't necessary to
# have Perl just to recompile NASM from the distribution.

# Perl-generated source files
PERLREQ_CLEANABLE = &
	  x86\insnsb.c x86\insnsa.c x86\insnsd.c x86\insnsi.h x86\insnsn.c &
	  x86\regs.c x86\regs.h x86\regflags.c x86\regdis.c x86\regdis.h &
	  x86\regvals.c asm\tokhash.c asm\tokens.h asm\pptok.h asm\pptok.c &
	  x86\iflag.c x86\iflaggen.h &
	  macros\macros.c &
	  asm\pptok.ph asm\directbl.c asm\directiv.h &
	  $(WARNFILES) &
	  version.h version.mac version.mak nsis\version.nsh

PERLREQ = $(PERLREQ_CLEANABLE)

INSDEP = x86\insns.xda x86\insns.pl x86\insns-iflags.ph x86\iflags.ph

x86\insns.xda: x86\insns.dat x86\preinsns.pl $(DIRS)
	$(RUNPERL) $(srcdir)\x86\preinsns.pl $(srcdir)\x86\insns.dat $@

x86\iflag.c: $(INSDEP)
	$(RUNPERL) $(srcdir)\x86\insns.pl -fc &
		x86\insns.xda x86\iflag.c
x86\iflaggen.h: $(INSDEP)
	$(RUNPERL) $(srcdir)\x86\insns.pl -fh &
		x86\insns.xda x86\iflaggen.h
x86\insnsb.c: $(INSDEP)
	$(RUNPERL) $(srcdir)\x86\insns.pl -b &
		x86\insns.xda x86\insnsb.c
x86\insnsa.c: $(INSDEP)
	$(RUNPERL) $(srcdir)\x86\insns.pl -a &
		x86\insns.xda x86\insnsa.c
x86\insnsd.c: $(INSDEP)
	$(RUNPERL) $(srcdir)\x86\insns.pl -d &
		x86\insns.xda x86\insnsd.c
x86\insnsi.h: $(INSDEP)
	$(RUNPERL) $(srcdir)\x86\insns.pl -i &
		x86\insns.xda x86\insnsi.h
x86\insnsn.c: $(INSDEP)
	$(RUNPERL) $(srcdir)\x86\insns.pl -n &
		x86\insns.xda x86\insnsn.c

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
nsis\version.nsh: version version.pl $(DIRS)
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
asm\tokhash.c: x86\insns.xda x86\insnsn.c asm\tokens.dat asm\tokhash.pl &
	perllib\phash.ph
	$(RUNPERL) $(srcdir)\asm\tokhash.pl c &
		x86\insnsn.c $(srcdir)\x86\regs.dat &
		$(srcdir)\asm\tokens.dat > asm\tokhash.c

# Assembler token metadata
asm\tokens.h: x86\insns.xda x86\insnsn.c asm\tokens.dat asm\tokhash.pl &
	perllib\phash.ph
	$(RUNPERL) $(srcdir)\asm\tokhash.pl h &
		x86\insnsn.c $(srcdir)\x86\regs.dat &
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
doc\pptok.src: asm\pptok.dat asm\pptok.pl perllib\phash.ph
	$(RUNPERL) $(srcdir)\asm\pptok.pl src &
		$(srcdir)\asm\pptok.dat doc\pptok.src

# Directives hash
asm\directiv.h: asm\directiv.dat nasmlib\perfhash.pl perllib\phash.ph
	$(RUNPERL) $(srcdir)\nasmlib\perfhash.pl h &
		$(srcdir)\asm\directiv.dat asm\directiv.h
asm\directbl.c: asm\directiv.dat nasmlib\perfhash.pl perllib\phash.ph
	$(RUNPERL) $(srcdir)\nasmlib\perfhash.pl c &
		$(srcdir)\asm\directiv.dat asm\directbl.c

# Editor token files
editors\nasmtok.el: editors\nasmtok.pl asm\tokhash.c asm\pptok.c &
		 asm\directiv.dat macros\macros.c editors\builtin.mac &
		 version.mak
	$(RUNPERL) $(srcdir)\editors\nasmtok.pl -el $@ $(srcdir) $(objdir)

editors\nasmtok.json: editors\nasmtok.pl asm\tokhash.c asm\pptok.c &
		 asm\directiv.dat macros\macros.c editors\builtin.mac &
		 version.mak
	$(RUNPERL) $(srcdir)\editors\nasmtok.pl -json $@ $(srcdir) $(objdir)

editors: $(EDITORS) $(PHONY)

asm\warnings_c.h: asm\warnings.pl asm\warnings.dat
	$(RUNPERL) $(srcdir)\asm\warnings.pl c asm\warnings_c.h &
		$(srcdir)\asm\warnings.dat

include\warnings.h: asm\warnings.pl asm\warnings.dat
	$(RUNPERL) $(srcdir)\asm\warnings.pl h include\warnings.h &
		$(srcdir)\asm\warnings.dat

doc\warnings.src: asm\warnings.pl asm\warnings.dat
	$(RUNPERL) $(srcdir)\asm\warnings.pl doc doc\warnings.src &
		$(srcdir)\asm\warnings.dat

$(PERLREQ): $(DIRS)

perlreq: $(PERLREQ) $(PHONY)

warnings: $(WARNFILES) $(PHONY)

#-- End Generated File Rules --#

perlreq: $(PERLREQ) .SYMBOLIC

#-- Begin NSIS Rules --#
# Edit in Makefile.in, not here!

nsis\arch.nsh: nsis\getpearch.pl nasm$(X) $(DIRS)
	$(PERL) $(srcdir)\nsis\getpearch.pl nasm$(X) > nsis\arch.nsh

# Should only be done after "make everything".
# The use of redirection here keeps makensis from moving the cwd to the
# source directory.
nsis: nsis\nasm.nsi nsis\arch.nsh nsis\version.nsh
	$(MAKENSIS) -Dsrcdir=$(srcdir) -Dobjdir=$(objdir) - &
		< $(srcdir)\nsis\nasm.nsi

#-- End NSIS Rules --#

clean: .SYMBOLIC
    rm -f *.obj *.s *.i
    rm -f asm\*.obj asm\*.s asm\*.i
    rm -f x86\*.obj x86\*.s x86\*.i
    rm -f lib\*.obj lib\*.s lib\*.i
    rm -f macros\*.obj macros\*.s macros\*.i
    rm -f output\*.obj output\*.s output\*.i
    rm -f common\*.obj common\*.s common\*.i
    rm -f stdlib\*.obj stdlib\*.s stdlib\*.i
    rm -f nasmlib\*.obj nasmlib\*.s nasmlib\*.i
    rm -f disasm\*.obj disasm\*.s disasm\*.i
    rm -f config.h config.log config.status
    rm -f nasm$(X) ndisasm$(X) $(NASMLIB) $(NDISLIB)

distclean: clean .SYMBOLIC
    rm -f config.h config.log config.status
    rm -f Makefile *~ *.bak *.lst *.bin
    rm -f output\*~ output\*.bak
    rm -f test\*.lst test\*.bin test\*.obj test\*.bin

cleaner: clean .SYMBOLIC
    rm -f $(PERLREQ)
    rm -f *.man
    rm -f nasm.spec
#   cd doc && $(MAKE) clean

spotless: distclean cleaner .SYMBOLIC
    rm -f doc\Makefile doc\*~ doc\*.bak

strip: .SYMBOLIC
    $(STRIP) *.exe

doc:
#   cd doc && $(MAKE) all

everything: all doc

#
# This build dependencies in *ALL* makefiles.  Partially for that reason,
# it's expected to be invoked manually.
#
alldeps: perlreq .SYMBOLIC
    $(PERL) syncfiles.pl Makefile.in Mkfiles\openwcom.mak
    $(PERL) mkdep.pl -M Makefile.in Mkfiles\openwcom.mak -- . output lib

#-- Magic hints to mkdep.pl --#
# @object-ending: ".obj"
# @path-separator: "\"
# @exclude: "config/config.h"
# @continuation: "&"
#-- Everything below is generated by mkdep.pl - do not edit --#
