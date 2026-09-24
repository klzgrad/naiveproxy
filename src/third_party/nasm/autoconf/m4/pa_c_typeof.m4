dnl --------------------------------------------------------------------------
dnl PA_C_TYPEOF
dnl
dnl Find if typeof() exists, or an equivalent (__typeof__, decltype,
dnl __decltype__)
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_C_TYPEOF],
[AC_CACHE_CHECK([if $CC supports typeof], [pa_cv_typeof],
 [pa_cv_typeof=no
 for pa_typeof_try in typeof __typeof __typeof__ decltype __decltype __decltype__ _Decltype
 do
  AS_IF([test $pa_cv_typeof = no],
        [AC_COMPILE_IFELSE([AC_LANG_SOURCE([
AC_INCLUDES_DEFAULT
int testme(int x);
int testme(int x)
{
    $pa_typeof_try(x) y = x*x;
    return y;
}
])],
 [pa_cv_typeof=$pa_typeof_try])])
 done
 ])
 AS_IF([test $pa_cv_typeof = no],
       [],
       [AC_DEFINE([HAVE_TYPEOF], 1,
	 [Define to 1 if you have some version of the typeof operator.])
	AS_IF([test $pa_cv_typeof = typeof],
	      [],
	      [AC_DEFINE_UNQUOTED([typeof], [$pa_cv_typeof],
	        [Define if your typeof operator is not named `typeof'.])])])])
