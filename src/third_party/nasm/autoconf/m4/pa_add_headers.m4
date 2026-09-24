dnl --------------------------------------------------------------------------
dnl PA_ADD_HEADERS(headers...)
dnl
dnl Call AC_CHECK_HEADERS_ONCE(), and add to ac_includes_default if found
dnl --------------------------------------------------------------------------
AC_DEFUN([_PA_ADD_HEADER],
[AC_CHECK_HEADERS_ONCE([$1])
AS_IF([test "x$]PA_SHSYM([ac_cv_header_$1])[" = xyes],
[ac_includes_default="$ac_includes_default
#include <$1>"
])
])
AC_DEFUN([PA_ADD_HEADERS],[m4_map_args([_PA_ADD_HEADER],$@)])
