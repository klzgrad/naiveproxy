dnl --------------------------------------------------------------------------
dnl PA_ADD_FLAGS(flagvar, flags [, real-flags [, success [, failure]]])
dnl
dnl  Add [real-flags] (default [flags]) to the variable [flagvar] if
dnl  and only if [flags] are accepted by all languages affected by
dnl  [flagvar], if those languages have been previously seen in the
dnl  script.
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_ADD_FLAGS],
[
  AS_VAR_PUSHDEF([old], [_$0_$1_orig])
  AS_VAR_PUSHDEF([ok], [_$0_$1_ok])
  AS_VAR_PUSHDEF([flags], [$1])

  AS_VAR_COPY([old], [flags])
  AS_VAR_SET([flags], ["$flags $2"])
  AS_VAR_SET([ok], [yes])

  PA_LANG_FOREACH(PA_FLAGS_LANGLIST($1),
    [AS_VAR_IF([ok], [yes],
     [AC_MSG_CHECKING([if $]_AC_CC[ accepts $2])
      PA_BUILD_IFELSE([],
      [AC_MSG_RESULT([yes])],
      [AC_MSG_RESULT([no])
       AS_VAR_SET([ok], [no])])])
     ])

 AS_VAR_IF([ok], [yes],
  [m4_ifnblank([$3],[AS_VAR_SET([flags], ["$old $3"])])
   m4_foreach_w([_pa_add_flags_flag], [m4_ifblank([$3],[$2],[$3])],
   [AC_DEFINE(PA_SYM([$1_]_pa_add_flags_flag), 1,
    [Define to 1 if compiled with the ]_pa_add_flags_flag[ compiler flag])])
   $4],
  [AS_VAR_SET([flags], ["$old"])
   $5])

  AS_VAR_POPDEF([flags])
  AS_VAR_POPDEF([ok])
  AS_VAR_POPDEF([old])
])
