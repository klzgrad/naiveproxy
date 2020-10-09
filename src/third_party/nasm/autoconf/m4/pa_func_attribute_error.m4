dnl --------------------------------------------------------------------------
dnl PA_FUNC_ATTRIBUTE_ERROR
dnl
dnl See if this compiler supports __attribute__((error("foo")))
dnl The generic version of this doesn't work as it makes the compiler
dnl throw an error by design.
dnl
dnl This doesn't use a function pointer because there is no need:
dnl the error function will never be a function pointer.
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_FUNC_ATTRIBUTE_ERROR],
[AC_MSG_CHECKING([if $CC supports the error function attribute])
 AC_COMPILE_IFELSE([AC_LANG_SOURCE([
AC_INCLUDES_DEFAULT
extern void __attribute__((error("message"))) barf(void);
void foo(void);
void foo(void)
{
	if (0)
		barf();
}
 ])],
 [AC_MSG_RESULT([yes])
  AC_DEFINE([HAVE_FUNC_ATTRIBUTE_ERROR], 1,
 [Define to 1 if your compiler supports __attribute__((error)) on functions])],
 [AC_MSG_RESULT([no])])
])
