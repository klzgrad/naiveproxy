dnl --------------------------------------------------------------------------
dnl PA_FUNC_SNPRINTF
dnl
dnl See if we have [_]snprintf(), using the proper prototypes in case
dnl it is a builtin of some kind.
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_FUNC_SNPRINTF],
[AC_CACHE_CHECK([for sprintf], [pa_cv_func_snprintf],
 [pa_cv_func_snprintf=no
  for pa_try_func_snprintf in snprintf _snprintf
  do
  AS_IF([test $pa_cv_func_snprintf = no],
        [AC_LINK_IFELSE([AC_LANG_SOURCE([
AC_INCLUDES_DEFAULT
const char *snprintf_test(int x);
const char *snprintf_test(int x)
{
    static char buf[[256]];
    size_t sz;
    sz = $pa_try_func_snprintf(buf, sizeof buf, "Hello = %d", x);
    return (sz < sizeof buf) ? buf : NULL;
}

int main(void) {
    puts(snprintf_test(33));
    return 0;
}
])],
 [pa_cv_func_snprintf=$pa_try_func_snprintf])])
 done
 ])
 AS_IF([test $pa_cv_func_snprintf = no],
       [],
       [AC_DEFINE([HAVE_SNPRINTF], 1,
         [Define to 1 if you have some version of the snprintf function.])
	 AS_IF([test $pa_cv_func_snprintf = snprintf],
	       [],
	       [AC_DEFINE_UNQUOTED([snprintf], [$pa_cv_func_snprintf],
	         [Define if your snprintf function is not named snprintf.])])])])
