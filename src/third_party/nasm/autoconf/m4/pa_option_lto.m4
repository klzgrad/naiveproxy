dnl --------------------------------------------------------------------------
dnl PA_OPTION_LTO(default)
dnl
dnl  Try to enable link-time optimization. Enable it by default if
dnl  the "default" argument is set to "yes"; currently the default is "no",
dnl  but that could change in the future -- to force disabled by default,
dnl  set to "no".
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_OPTION_LTO],
[AC_BEFORE([$0],[AC_PROG_AR])dnl
 AC_BEFORE([$0],[AC_PROG_RANLIB])dnl
 PA_ARG_BOOL([lto],
 [Try to enable link-time optimization for this compiler],
 [m4_default([$1],[no])],
 [PA_FIND_FLAGS([-flto=auto],[-flto])
  PA_FIND_FLAGS([-ffat-lto-objects])
  PA_FIND_FLAGS([-fuse-linker-plugin])

  AS_IF([test x$ac_compiler_gnu = xyes],
  [AC_CHECK_TOOL(AR, [gcc-ar], [ar], [:])
   AC_CHECK_TOOL(RANLIB, [gcc-ranlib], [ranlib], [:])])])])
