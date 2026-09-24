dnl --------------------------------------------------------------------------
dnl PA_OPTION_DEBUG(with_debug, without_debug)
dnl
dnl Set debug flags and optimization flags depending on if
dnl --enable-debug is set or not. Some flags are set regardless...
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_OPTION_DEBUG],
[PA_ARG_DISABLED([gdb], [disable gdb debug extensions],
 [PA_ADD_LANGFLAGS([-g3])], [PA_ADD_LANGFLAGS([-ggdb3],[-g3])])
 PA_ARG_ENABLED([debug], [optimize for debugging],
 [PA_ADD_LANGFLAGS([-Og],[-O0])
  $1],
 [$2])])
