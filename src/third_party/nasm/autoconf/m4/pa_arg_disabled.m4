dnl --------------------------------------------------------------------------
dnl PA_ARG_DISABLED(option,helptext,disabled_action,enabled_action)
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_ARG_DISABLED],[PA_ARG_BOOL([$1],[$2],yes,[$4],[$3])])
