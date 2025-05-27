dnl --------------------------------------------------------------------------
dnl PA_OPTION_PROFILING(with_profiling, without_profiling)
dnl
dnl Try to enable profiling if --enable-profiling is set.
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_OPTION_PROFILING],
[PA_ARG_ENABLED([profiling], [compile with profiling (-pg option)],
[PA_ADD_LANGFLAGS([-pg])
 AC_DEFINE([WITH_PROFILING], 1,
  [Define to 1 to include code specifically indended to help profiling.])
])])
