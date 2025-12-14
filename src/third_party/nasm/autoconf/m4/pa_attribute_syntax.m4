dnl --------------------------------------------------------------------------
dnl PA_ATTRIBUTE_SYNTAX
dnl
dnl Source code fragment to test for attribute syntax
dnl The use of #ifdef rather than defined() here is intentional:
dnl defined() is known to not always work right.

dnl --------------------------------------------------------------------------
AC_DEFUN([PA_ATTRIBUTE_SYNTAX],
[#include "autoconf/attribute.h"
])
