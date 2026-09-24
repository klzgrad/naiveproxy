dnl --------------------------------------------------------------------------
dnl PA_CHECK_BAD_STDC_INLINE
dnl
dnl Some versions of gcc seem to apply -Wmissing-prototypes to C99
dnl inline functions, which means we need to use GNU inline syntax
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_CHECK_BAD_STDC_INLINE],
[AC_MSG_CHECKING([if $CC supports C99 external inlines])
 AC_COMPILE_IFELSE([AC_LANG_SOURCE([
AC_INCLUDES_DEFAULT

/* Don't mistake GNU inlines for c99 */
#if defined(__GNUC__) && !defined(__GNUC_STDC_INLINE__)
# error "Using gnu inline standard"
#endif

inline int foo(int x)
{
	return x+1;
}
 ])],
 [AC_MSG_RESULT([yes])
  AC_DEFINE([HAVE_STDC_INLINE], 1,
    [Define to 1 if your compiler supports C99 extern inline])],
 [AC_MSG_RESULT([no])
  PA_ADD_CFLAGS([-fgnu89-inline])])])
