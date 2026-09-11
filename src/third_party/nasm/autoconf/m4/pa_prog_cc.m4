dnl --------------------------------------------------------------------------
dnl PA_PROG_CC()
dnl
dnl Similar to AC_PROG_CC, but add a prototype for main() to
dnl AC_INCLUDES_DEFAULT to avoid -Werror from breaking compilation.
dnl (Very odd!)
dnl
dnl It can optionally take lists of CFLAGS to be added. For each argument,
dnl only the *first* flag accepted is added.
dnl
dnl BUG: this expands AC_CHECK_HEADERS_ONCE() before the flags get
dnl probed. Don't know yet how to fix that.
dnl --------------------------------------------------------------------------
AC_DEFUN_ONCE([PA_PROG_CC],
[AC_REQUIRE([AC_PROG_CC])
AC_USE_SYSTEM_EXTENSIONS
ac_includes_default="$ac_includes_default
#ifndef __cplusplus
extern int main(void);
#endif"
PA_FIND_FLAGS(CFLAGS,[$1])
])
