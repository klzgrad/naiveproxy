dnl --------------------------------------------------------------------------
dnl PA_HAVE_FUNC(func_name)
dnl
dnl Look for a function with the specified arguments which could be
dnl a builtin/intrinsic function.
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_HAVE_FUNC],
[AC_MSG_CHECKING([for $1])
 AC_LINK_IFELSE([AC_LANG_SOURCE([
AC_INCLUDES_DEFAULT
int main(void) {
    (void)$1$2;
    return 0;
}
 ])],
 [AC_MSG_RESULT([yes])
  AC_DEFINE(AS_TR_CPP([HAVE_$1]), 1,
  [Define to 1 if you have the `$1' intrinsic function.])],
 [AC_MSG_RESULT([no])])
])
