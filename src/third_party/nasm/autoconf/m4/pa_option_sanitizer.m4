dnl --------------------------------------------------------------------------
dnl PA_OPTION_SANITIZER
dnl
dnl Option to compile with sanitizers enabled.
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_OPTION_SANITIZER],
[PA_ARG_ENABLED([sanitizer],
 [compile with sanitizers enabled],
 [PA_ADD_CFLAGS([-fno-omit-frame-pointer])
  PA_ADD_CFLAGS([-fsanitize=address])
  PA_ADD_CFLAGS([-fsanitize=undefined])])])
