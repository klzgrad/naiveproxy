dnl --------------------------------------------------------------------------
dnl PA_VARIADIC_MACROS
dnl
dnl Check to see if the compiler supports C99 variadic macros.
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_VARIADIC_MACROS],
[AC_CACHE_CHECK([if $CC supports variadic macros], [pa_cv_variadic_macros],
[AC_LINK_IFELSE([AC_LANG_SOURCE([
AC_INCLUDES_DEFAULT
#define myprintf(f, ...) printf(f, __VA_ARGS__)
int main(void)
{
	myprintf("%s", "Hello, World!\n");
	return 0;
}
])],[pa_cv_variadic_macros=yes],[pa_cv_variadic_macros=no])])
	AS_IF([test "x$pa_cv_variadic_macros" = xyes],
	[AC_DEFINE([HAVE_VARIADIC_MACROS], 1,
[define to 1 if your compiler supports C99 __VA_ARGS__ variadic macros.])])
])
