dnl --------------------------------------------------------------------------
dnl PA_SYM(prefix, string)
dnl
dnl Convert a (semi-) arbitrary string to a CPP symbol
dnl Compact underscores and convert non-C characters to underscore,
dnl except + which is converted to X (so C++ -> CXX).
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_SYM],
[m4_bpatsubsts(m4_quote(m4_toupper([$*])),
 [,],[],[\+],[X],[[^ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789]+],[_],dnl
[^._?\(.*\)_.$],[[\1]])])
