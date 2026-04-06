dnl --------------------------------------------------------------------------
dnl PA_ADD_HEADERS(headers...)
dnl
dnl Call AC_CHECK_HEADERS(), and add to ac_includes_default if found
dnl --------------------------------------------------------------------------
AC_DEFUN([_PA_ADD_HEADER],
[AC_CHECK_HEADERS([$1],[ac_includes_default="$ac_includes_default
#include <$1>"
])
])

AC_DEFUN([PA_ADD_HEADERS],
[m4_map_args_w([$1],[_PA_ADD_HEADER(],[)])])
