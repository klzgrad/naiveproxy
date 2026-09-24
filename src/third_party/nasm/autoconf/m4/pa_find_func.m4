dnl --------------------------------------------------------------------------
dnl PA_FIND_FUNC(func_description ...)
dnl
dnl Each argument must be a list of arguments to PA_HAVE_FUNC. Stop after
dnl the first function in the list found.
dnl --------------------------------------------------------------------------
AC_DEFUN([_PA_FIND_FUNC],
[
AS_IF([test x"$pa_find_func_found" != xyes],
[PA_HAVE_FUNC($@)
pa_find_func_found="$pa_cv_func_$1"
])])

AC_DEFUN([PA_FIND_FUNC],
[pa_find_func_found=no
m4_map([_PA_FIND_FUNC],[$@])])
