dnl --------------------------------------------------------------------------
dnl PA_COMMON_ATTRIBUTES
dnl
dnl Test for a bunch of common function attributes and define macros for them.
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_COMMON_ATTRIBUTES],
[PA_ADD_CPPFLAGS([-Werror=attributes])
 PA_FUNC_ATTRIBUTE(noreturn)
 PA_FUNC_ATTRIBUTE(returns_nonnull,,[void *],,,never_null)
 PA_FUNC_ATTRIBUTE(malloc,,[void *])
 PA_FUNC_ATTRIBUTE(alloc_size,[1],[void *],[int],[80])
 PA_FUNC_ATTRIBUTE(alloc_size,[1,2],[void *],[int,int],[20,40])
 PA_FUNC_ATTRIBUTE(sentinel,,,[const char *, ...],["a","b",NULL],end_with_null)
 PA_FUNC_ATTRIBUTE(format,[printf,1,2],int,[const char *, ...],["%d",1])
 PA_FUNC_ATTRIBUTE(const)
 PA_FUNC_ATTRIBUTE(unsequenced)
 PA_FUNC_ATTRIBUTE(reproducible)
 PA_FUNC_ATTRIBUTE(pure)
 PA_FUNC_ATTRIBUTE(cold,,,,,unlikely_func)
 PA_FUNC_ATTRIBUTE(maybe_unused)
 PA_FUNC_ATTRIBUTE(unused)
 PA_FUNC_ATTRIBUTE_ERROR])
