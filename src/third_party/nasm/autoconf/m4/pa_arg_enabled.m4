dnl --------------------------------------------------------------------------
dnl PA_ARG_ENABLED(option,helptext,enabled_action,disabled_action)
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_ARG_ENABLED],[PA_ARG_BOOL([$1],[$2],no,[$3],[$4])])
