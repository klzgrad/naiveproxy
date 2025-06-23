dnl --------------------------------------------------------------------------
dnl PA_BUILD_IFELSE(input [,success [,failure]])
dnl
dnl  Same as AC_LINK_IFELSE for languages where linking is applicable,
dnl  otherwise AC_COMPILE_IFELSE.
dnl
dnl If the first argument is empty, use _AC_LANG_IO_PROGRAM.
dnl --------------------------------------------------------------------------
m4_defun([_PA_BUILD_IFELSE],
[m4_case(_AC_LANG,
 [Erlang], [AC_COMPILE_IFELSE($@)],
 [AC_LINK_IFELSE($@)])])

AC_DEFUN([PA_BUILD_IFELSE],
[_PA_BUILD_IFELSE([m4_ifblank([$1],[AC_LANG_SOURCE(_AC_LANG_IO_PROGRAM)],
 [$1])],[$2],[$3])])
