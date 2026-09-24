dnl --------------------------------------------------------------------------
dnl PA_ADD_FLAGS(variable, flag [,actual_flag [,success [,failure]]]])
dnl
dnl  Add [flags] to the variable [flagvar] if and only if it is accepted
dnl  by all languages affected by [flagvar], if those languages have
dnl  been previously seen in the script.
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_ADD_FLAGS],
[ AS_VAR_PUSHDEF([old],[PA_SHSYM([_$0_$1_orig])])
  AS_VAR_PUSHDEF([flags], [$1])
  AS_VAR_PUSHDEF([cache],[PA_SHSYM([pa_cv_$1_$2])])

  AS_VAR_COPY([old],[flags])

  AC_CACHE_VAL([cache],
  [AS_VAR_APPEND([flags],[' $2'])
   AS_VAR_SET([cache],[yes])
   PA_LANG_FOREACH([PA_FLAGS_LANGLIST($1)],
    [AS_VAR_IF([cache],[yes],
     [AC_MSG_CHECKING([whether $]_AC_CC[ accepts $2])
      m4_case([$1],
      [LDFLAGS],
      [AC_LINK_IFELSE([AC_LANG_PROGRAM([[]],[[]])],
		      [],[AS_VAR_SET([cache],[no])])],
      [AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]],[[]])],
                      [],[AS_VAR_SET([cache],[no])])])
     AC_MSG_RESULT([$cache])
    ])])
   AS_VAR_COPY([flags],[old])
  ])

  AS_VAR_IF([cache],[yes],
    [m4_define([_pa_add_flags_newflags],[m4_default([$3],[$2])])dnl
     AS_VAR_APPEND([flags],[' _pa_add_flags_newflags'])
     m4_foreach_w([_pa_add_flags_flag],[_pa_add_flags_newflags],
     [AC_DEFINE(PA_SYM([$1_]_pa_add_flags_flag), 1,
      [Define to 1 if compiled with ]_pa_add_flags_flag[ in $1])])
$4],[$5])

  AS_VAR_POPDEF([cache])
  AS_VAR_POPDEF([flags])
  AS_VAR_POPDEF([old])
])
