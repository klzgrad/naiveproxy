dnl --------------------------------------------------------------------------
dnl PA_FIND_FLAGS(flagvar, flags_list)
dnl
dnl  Add the first set of flags in flags_list that is accepted by
dnl  by all languages affected by [flagvar], if those languages have
dnl  been previously seen in the script.
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_FIND_FLAGS],
[_pa_find_flags_done=no
m4_foreach([_pa_find_flags_flag],[$2],
[
AS_IF([test x$_pa_find_flags_done != xyes],
[PA_ADD_FLAGS([$1],_pa_find_flags_flag,,[_pa_find_flags_done=yes])])])])
