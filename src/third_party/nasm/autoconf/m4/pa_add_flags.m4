dnl --------------------------------------------------------------------------
dnl PA_ADD_FLAGS(variable, flag [,actual_flag [,success [,failure]]])
dnl
dnl Attempt to add the given option to CPPFLAGS, if it doesn't break
dnl compilation.  If the option to be tested is different than the
dnl option that should actually be added, add the option to be
dnl actually added as a second argument.
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_ADD_FLAGS],
[AC_MSG_CHECKING([if $CC accepts $2])
 pa_add_flags__old_flags="$$1"
 $1="$$1 $2"
 AC_TRY_LINK(AC_INCLUDES_DEFAULT,
 [printf("Hello, World!\n");],
 [AC_MSG_RESULT([yes])
  $1="$pa_add_flags__old_flags ifelse([$3],[],[$2],[$3])"
  AC_DEFINE(PA_SYM([$1_],[$2]), 1,
   [Define to 1 if compiled with the `$2' compiler flag])
  $4],
 [AC_MSG_RESULT([no])
  $1="$pa_add_flags__old_flags"
  $5])])
