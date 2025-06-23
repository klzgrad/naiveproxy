dnl --------------------------------------------------------------------------
dnl PA_PROG_CC()
dnl
dnl Similar to AC_PROG_CC, but add a prototype for main() to
dnl AC_INCLUDES_DEFAULT to avoid -Werror from breaking compilation.
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_PROG_CC],
[AC_PROG_CC
 AS_IF([test x$ac_cv_prog != xno],
 [ac_includes_default="$ac_includes_default
#ifndef __cplusplus
extern int main(void);
#endif"])])
