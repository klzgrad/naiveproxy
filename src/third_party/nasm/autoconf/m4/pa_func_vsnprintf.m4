dnl --------------------------------------------------------------------------
dnl PA_FUNC_VSNPRINTF
dnl
dnl See if we have [_]vsnprintf(), using the proper prototypes in case
dnl it is a builtin of some kind.
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_FUNC_VSNPRINTF],
[AC_CACHE_CHECK([for vsnprintf], [pa_cv_func_vsnprintf],
 [pa_cv_func_vsnprintf=no
  for pa_try_func_vsnprintf in vsnprintf _vsnprintf
  do
  AS_IF([test $pa_cv_func_vsnprintf = no],
        [AC_LINK_IFELSE([AC_LANG_PROGRAM([
AC_INCLUDES_DEFAULT
[
#include <stdarg.h>

const char *vsnprintf_test(const char *fmt, va_list va);
const char *vsnprintf_test(const char *fmt, va_list va)
{
    static char buf[256];
    size_t sz;
    sz = $pa_try_func_vsnprintf(buf, sizeof buf, fmt, va);
    return (sz < sizeof buf) ? buf : NULL;
}

const char *vsnprintf_caller(const char *fmt, ...);
const char *vsnprintf_caller(const char *fmt, ...)
{
    const char *what;
    va_list va;
    va_start(va, fmt);
    what = vsnprintf_test(fmt, va);
    va_end(va);
    return what;
}]], [[
    puts(vsnprintf_caller("Hello = %d", 33));
    return 0;
]])],
 [pa_cv_func_vsnprintf=$pa_try_func_vsnprintf])])
 done
 ])
 AS_IF([test $pa_cv_func_vsnprintf = no],
       [],
       [AC_DEFINE([HAVE_VSNPRINTF], 1,
         [Define to 1 if you have some version of the vsnprintf function.])
	 AS_IF([test $pa_cv_func_vsnprintf = vsnprintf],
	       [],
	       [AC_DEFINE_UNQUOTED([vsnprintf], [$pa_cv_func_vsnprintf],
	         [Define if your vsnprintf function is not named vsnprintf.])])])])
