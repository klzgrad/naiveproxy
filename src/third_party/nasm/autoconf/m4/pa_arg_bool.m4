dnl --------------------------------------------------------------------------
dnl PA_ARG_BOOL(option,helptext,default,enabled_action,disabled_action)
dnl
dnl  The last three arguments are optional; default can be yes or no.
dnl
dnl  Simpler-to-use versions of AC_ARG_ENABLED, that include the
dnl  test for $enableval and the AS_HELP_STRING definition. This is only
dnl  to be used for boolean options.
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_ARG_BOOL],
[m4_pushdef([pa_default],m4_default(m4_normalize([$3]),[no]))
 m4_pushdef([pa_option],m4_case(pa_default,[yes],[disable],[enable]))
 AC_ARG_ENABLE([$1],
  [AS_HELP_STRING([--]m4_defn([pa_option])[-$1],[$2])],
  [pa_arg_bool_enableval="$enableval"],
  [pa_arg_bool_enableval="]m4_defn([pa_default])["])
 m4_popdef([pa_option], [pa_default])
 AS_IF([test x"$pa_arg_bool_enableval" != xno], [$4], [$5])
])
