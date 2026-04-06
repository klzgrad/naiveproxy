dnl --------------------------------------------------------------------------
dnl PA_OPTION_LTO(default)
dnl
dnl  Try to enable link-time optimization. Enable it by default if
dnl  the "default" argument is set to "yes"; currently the default is "no",
dnl  but that could change in the future -- to force disabled by default,
dnl  set to "no".
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_OPTION_LTO],
[PA_ARG_BOOL([lto],
 [Try to enable link-time optimization for this compiler],
 [$1],
 [PA_ADD_LANGFLAGS([-flto=auto -flto])
PA_ADD_LANGFLAGS([-ffat-lto-objects])
dnl Note: we use _PROG rather than _TOOL since we are prepending the full
dnl CC name which ought to already contain the host triplet if needed
   ccbase=`echo "$CC" | awk '{ print $1; }'`
   AC_CHECK_PROGS(CC_AR, [${ccbase}-ar], [$ac_cv_prog_AR])
   AR="$CC_AR"
   AC_CHECK_PROGS(CC_RANLIB, [${ccbase}-ranlib], [$ac_cv_prog_RANLIB])
   RANLIB="$CC_RANLIB"])])
