dnl --------------------------------------------------------------------------
dnl PA_ADD_CFLAGS(flag [,actual_flag [,success [,failure]]]])
dnl
dnl Attempt to add the given option to xFLAGS, if it doesn't break
dnl compilation.  If the option to be tested is different than the
dnl option that should actually be added, add the option to be
dnl actually added as a second argument.
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_ADD_CFLAGS], [PA_ADD_FLAGS(CFLAGS, [$1], [$2], [$3], [$4])])
