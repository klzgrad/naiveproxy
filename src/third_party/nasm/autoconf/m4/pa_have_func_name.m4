dnl --------------------------------------------------------------------------
dnl PA_HAVE_FUNC_NAME
dnl
dnl See if the C compiler supports __func__ or __FUNCTION__.
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_HAVE_FUNC_NAME],
[AC_CACHE_CHECK([for function name constant], [pa_cv_func_name],
 [pa_cv_func_name=no
  for pa_try_func_name in __func__ __FUNCTION__
  do :
  AS_IF([test $pa_cv_func_name = no],
        [AC_LINK_IFELSE([AC_LANG_SOURCE([
AC_INCLUDES_DEFAULT
const char *test_func_name(void);
const char *test_func_name(void)
{
    return $pa_try_func_name;
}
int main(void) {
    puts(test_func_name());
    return 0;
}
])],
 [pa_cv_func_name=$pa_try_func_name])])
 done
 ])
 AS_IF([test $pa_cv_func_name = no],
       [],
       [AC_DEFINE([HAVE_FUNC_NAME], 1,
         [Define to 1 if the compiler supports __func__ or equivalent.])
	 AS_IF([test $pa_cv_func_name = __func__],
	       [],
	       [AC_DEFINE_UNQUOTED([__func__], [$pa_cv_func_name],
	       [Define if __func__ is called something else on your compiler.])])])])
