dnl --------------------------------------------------------------------------
dnl PA_SYM(prefix, string)
dnl
dnl Convert a (semi-) arbitrary string to a CPP symbol
dnl Compact underscores and convert non-C characters to underscore,
dnl except + which is converted to X (so C++ -> CXX).
dnl
dnl Contract multiple underscores together.
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_SYM],[m4_bpatsubsts(PA_CSYM([$*]),[__+],[_])])
