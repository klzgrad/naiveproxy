dnl Process this file with autoconf 2.69 or later to produce
dnl a configure script.
AC_PREREQ(2.69)
AC_INIT(config/config.h.in)
AC_CONFIG_HEADERS(config/config.h)

AC_PREFIX_PROGRAM(nasm)

dnl Save initial CFLAGS, to see if -g -O2 came from configure or not
pa_init_cflags="$CFLAGS"

dnl This prevents us from running Wine and thinking we are not
dnl cross-compiling when in fact we are; running Wine here is at
dnl the best very slow and doesn't buy us a single thing at all.
WINELOADER=/dev/null
export WINELOADER

dnl Checks for programs and enable necessary CC extensions
AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE
AC_PROG_CC
AC_PROG_CC_STDC
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_INSTALL

pa_no_optimize=false

dnl If the user did not specify a CFLAGS default, change default
dnl to -O0 for debugging
PA_ARG_DISABLED([optimization],
 [compile without optimization (-O0) to help debugging],
 [pa_no_optimize=true])

dnl Compile and link with dwarf debug
PA_ARG_ENABLED([gdb],
 [disable optimization and compile with extra debug information for GDB debugger],
 [PA_ADD_CFLAGS([-ggdb3])
  pa_no_optimize=true])

AS_IF([$pa_no_optimize],
      [PA_ADD_CFLAGS([-O0])
       PA_ADD_CFLAGS([-fno-omit-frame-pointer])])

dnl Abort on panic
PA_ARG_ENABLED([panic-abort],
 [call abort() on panic to trap in the debugger],
 [AC_DEFINE(ABORT_ON_PANIC)])
AH_TEMPLATE(ABORT_ON_PANIC,
[Define to 1 to call abort() on panics (internal errors), for debugging.])

dnl Check for library extension
PA_LIBEXT

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_C_RESTRICT
AC_TYPE_SIZE_T
AC_C_BIGENDIAN(AC_DEFINE(WORDS_BIGENDIAN),AC_DEFINE(WORDS_LITTLEENDIAN),,)
AH_TEMPLATE(WORDS_BIGENDIAN,
[Define to 1 if your processor stores words with the most significant
byte first (like Motorola and SPARC, unlike Intel and VAX).])
AH_TEMPLATE(WORDS_LITTLEENDIAN,
[Define to 1 if your processor stores words with the least significant
byte first (like Intel and VAX, unlike Motorola and SPARC).])

dnl Force gcc and gcc-compatible compilers treat signed integers
dnl as 2's complement
PA_ADD_CFLAGS([-fwrapv])

dnl Some environments abuse __STRICT_ANSI__ to disable some
dnl function declarations
PA_ADD_CFLAGS([-U__STRICT_ANSI__])

dnl Don't put things in common if we can avoid it.  We don't want to
dnl assume all compilers support common, and this will help find those
dnl problems.  This also works around an OSX linker problem.
PA_ADD_CFLAGS([-fno-common])

dnl Look for programs...
AC_CHECK_PROGS(NROFF,    nroff,    false)
AC_CHECK_PROGS(ASCIIDOC, asciidoc, false)
AC_CHECK_PROGS(XMLTO,    xmlto,    false)

dnl Check for progs needed for manpage generation
AS_IF([test $ASCIIDOC = false],
  [AC_MSG_WARN([No asciidoc package found])]
)
AS_IF([test $XMLTO = false],
  [AC_MSG_WARN([No xmlto package found])]
)

dnl Check for host compiler tools
AC_CHECK_TOOL(AR, ar)
AC_CHECK_TOOL(RANLIB, ranlib, :)
AC_CHECK_TOOL(STRIP, strip)

dnl Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS(inttypes.h)
AC_CHECK_HEADERS(strings.h)
AC_HEADER_STDBOOL
AC_CHECK_HEADERS(stdnoreturn.h)
AC_CHECK_HEADERS(io.h)
AC_CHECK_HEADERS(fcntl.h)
AC_CHECK_HEADERS(unistd.h)
AC_CHECK_HEADERS(sys/mman.h)
AC_CHECK_HEADERS(sys/types.h)
AC_CHECK_HEADERS(sys/stat.h)

dnl Checks for library functions.
AC_CHECK_FUNCS(strcasecmp stricmp)
AC_CHECK_FUNCS(strncasecmp strnicmp)
AC_CHECK_FUNCS(strsep)
AC_CHECK_FUNCS(strnlen)
AC_CHECK_FUNCS(strrchrnul)

AC_CHECK_FUNCS(getuid)
AC_CHECK_FUNCS(getgid)

AC_CHECK_FUNCS(realpath)
AC_CHECK_FUNCS(canonicalize_file_name)
AC_CHECK_FUNCS(_fullpath)
AC_CHECK_FUNCS(pathconf)

AC_FUNC_FSEEKO
AC_CHECK_FUNCS([_fseeki64])
AC_CHECK_FUNCS([ftruncate _chsize _chsize_s])
AC_CHECK_FUNCS([fileno _fileno])

AC_CHECK_FUNCS(_filelengthi64)
AC_FUNC_MMAP
AC_CHECK_FUNCS(getpagesize)
AC_CHECK_FUNCS(sysconf)

AC_CHECK_FUNCS([access _access faccessat])

PA_HAVE_FUNC(__builtin_expect, (1,1))

dnl ilog2() building blocks
PA_ADD_HEADERS(intrin.h)
PA_HAVE_FUNC(__builtin_clz, (0U))
PA_HAVE_FUNC(__builtin_clzl, (0UL))
PA_HAVE_FUNC(__builtin_clzll, (0ULL))
PA_HAVE_FUNC(_BitScanReverse, (0))
PA_HAVE_FUNC(_BitScanReverse64, (0))

dnl Functions for which we have replacements available in stdlib/
AC_CHECK_FUNCS([vsnprintf _vsnprintf])
AC_CHECK_FUNCS([snprintf _snprintf])
AC_CHECK_FUNCS([strlcpy])
AC_CHECK_FUNCS([strrchrnul])

dnl These types are POSIX-specific, and Windows does it differently...
AC_CHECK_TYPES([struct _stati64])
AC_CHECK_TYPES([struct stat])
AC_CHECK_FUNCS([stat _stati64])
AC_CHECK_FUNCS([fstat _fstati64])

dnl Check for functions that might not be declared in the headers for
dnl various idiotic reasons (mostly because of library authors
dnl abusing the meaning of __STRICT_ANSI__)
AC_CHECK_DECLS(strcasecmp)
AC_CHECK_DECLS(stricmp)
AC_CHECK_DECLS(strncasecmp)
AC_CHECK_DECLS(strnicmp)
AC_CHECK_DECLS(strsep)
AC_CHECK_DECLS(strlcpy)
AC_CHECK_DECLS(strnlen)
AC_CHECK_DECLS(strrchrnul)

dnl Check for missing types
AC_TYPE_UINTPTR_T

dnl Documentation: should we generate an uncompressed PDF?  It is
dnl about twice as big, but it can be externally compressed (e.g. with xz)
dnl and becomes significantly smaller than the original.
PA_ARG_DISABLED([pdf-compression],
  [generate an uncompressed documentation PDF],
  [PDFOPT='-nocompress'])
AC_SUBST([PDFOPT])

dnl
dnl Look for byte-swapping support...
dnl
PA_ADD_HEADERS(endian.h sys/endian.h machine/endian.h)
PA_HAVE_FUNC(cpu_to_le16, (0))
PA_HAVE_FUNC(cpu_to_le32, (0))
PA_HAVE_FUNC(cpu_to_le64, (0))
PA_HAVE_FUNC(__cpu_to_le16, (0))
PA_HAVE_FUNC(__cpu_to_le32, (0))
PA_HAVE_FUNC(__cpu_to_le64, (0))
PA_HAVE_FUNC(htole16, (0))
PA_HAVE_FUNC(htole32, (0))
PA_HAVE_FUNC(htole64, (0))
PA_HAVE_FUNC(__bswap_16, (0))
PA_HAVE_FUNC(__bswap_32, (0))
PA_HAVE_FUNC(__bswap_64, (0))
PA_HAVE_FUNC(__builtin_bswap16, (0))
PA_HAVE_FUNC(__builtin_bswap32, (0))
PA_HAVE_FUNC(__builtin_bswap64, (0))
PA_HAVE_FUNC(_byteswap_ushort, (0))
PA_HAVE_FUNC(_byteswap_ulong, (0))
PA_HAVE_FUNC(_byteswap_uint64, (0))

dnl
dnl Check for __builtin_constant_p()
dnl
PA_HAVE_FUNC(__builtin_constant_p, (0))

dnl
dnl Check for supported gcc attributes; some compilers (e.g. Sun CC)
dnl support these, but don't define __GNUC__ as they don't support
dnl some other features of gcc.
dnl
PA_ADD_CFLAGS([-Werror=attributes])
PA_FUNC_ATTRIBUTE(noreturn)
PA_FUNC_ATTRIBUTE(returns_nonnull)
PA_FUNC_ATTRIBUTE(malloc)
PA_FUNC_ATTRIBUTE(alloc_size, (1))
PA_FUNC_ATTRIBUTE(sentinel,,, [const char *, ...], ["a","b",NULL])
PA_FUNC_ATTRIBUTE(format, [(printf,1,2)], int, [const char *, ...], ["%d",1])
PA_FUNC_ATTRIBUTE(const)
PA_FUNC_ATTRIBUTE(pure)
PA_FUNC_ATTRIBUTE(cold)
PA_FUNC_ATTRIBUTE_ERROR

dnl
dnl support function sections (if available)
dnl
PA_ARG_ENABLED([sections],
 [compile with function/data section support],
 [PA_ADD_CLDFLAGS([-ffunction-sections])
  PA_ADD_CLDFLAGS([-fdata-sections])
  PA_ADD_CLDFLAGS([-Wl,--gc-sections])],
 [])

dnl
dnl support LTO
dnl
PA_ARG_ENABLED([lto],
 [compile with gcc-style link time optimization],
 [PA_ADD_CLDFLAGS([-flto])
  dnl Note: we use _PROG rather than _TOOL since we are prepending the full
  dnl CC name which ought to already contain the host triplet if needed
  ccbase=`echo "$CC" | awk '{ print $1; }'`
  AC_CHECK_PROGS(CC_AR, [${ccbase}-ar], [$ac_cv_prog_AR])
  AR="$CC_AR"
  AC_CHECK_PROGS(CC_RANLIB, [${ccbase}-ranlib], [$ac_cv_prog_RANLIB])
  RANLIB="$CC_RANLIB"], [])

dnl
dnl support sanitizers (if available)
dnl
PA_ARG_ENABLED([sanitizer],
 [compile with sanitizers enabled],
 [PA_ADD_CFLAGS([-fno-omit-frame-pointer])
  PA_ADD_CLDFLAGS([-fsanitize=address])
  PA_ADD_CLDFLAGS([-fsanitize=undefined])])

dnl
dnl Don't make symbols visible, there is no point and it just
dnl makes the code slower.
dnl
PA_ADD_CLDFLAGS([-fvisibility=hidden])

dnl If we have gcc, add appropriate code cleanliness options
PA_ADD_CFLAGS([-W])
PA_ADD_CFLAGS([-Wall])
PA_ADD_CFLAGS([-pedantic])
dnl LLVM doesn't error out on invalid -W options unless this option is
dnl specified first.  Enable this so this script can actually discover
dnl which -W options are possible for this compiler.
PA_ADD_CFLAGS([-Werror=unknown-warning-option])
dnl Suppress format warning on Windows targets due to their <inttypes.h>
PA_ADD_CFLAGS([-Wpedantic-ms-format],[-Wno-pedantic-ms-format])
PA_ADD_CFLAGS([-Wc90-c99-compat])
PA_ADD_CFLAGS([-Wlong-long],[-Wno-long-long])
dnl This is needed because we intentionally expect strncpy() to fill
dnl in a zero-padded (not zero-terminated) buffer in several backends
PA_ADD_CFLAGS([-Wstringop-truncation],[-Wno-stringop-truncation])
dnl PA_ADD_CFLAGS([-Wwrite-strings])
PA_ARG_ENABLED([werror],
 [compile with -Werror to error out on any warning],
 [PA_ADD_CFLAGS([-Werror])],
 [PA_ADD_CFLAGS([-Werror=implicit])
  PA_ADD_CFLAGS([-Werror=missing-braces])
  PA_ADD_CFLAGS([-Werror=return-type])
  PA_ADD_CFLAGS([-Werror=trigraphs])
  PA_ADD_CFLAGS([-Werror=pointer-arith])
  PA_ADD_CFLAGS([-Werror=strict-prototypes])
  PA_ADD_CFLAGS([-Werror=missing-prototypes])
  PA_ADD_CFLAGS([-Werror=missing-declarations])
  PA_ADD_CFLAGS([-Werror=comment])
  PA_ADD_CFLAGS([-Werror=vla])]
)

dnl
dnl On some versions of gcc, -Werror=missing-prototypes causes problems
dnl with C99-style external inlines.  Test this *after* adding the -Werror
dnl options.
dnl
PA_CHECK_BAD_STDC_INLINE

dnl
dnl support ccache
dnl
PA_ARG_ENABLED([ccache], [compile with ccache], [CC="ccache $CC"], [])

AC_OUTPUT_COMMANDS([mkdir -p config nasmlib nsis output stdlib x86 asm disasm rdoff macros common])
AC_OUTPUT(Makefile doc/Makefile)
