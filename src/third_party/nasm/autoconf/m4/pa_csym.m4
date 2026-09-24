dnl --------------------------------------------------------------------------
dnl PA_CSYM(prefix, string)
dnl
dnl Convert a (semi-) arbitrary string to a CPP symbol
dnl Convert non-C characters to underscore, except + which is converted
dnl to X (so C++ -> CXX). Unlike PA_SYM(), do not compact multiple
dnl underscores.
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_CSYM],
[m4_bpatsubsts(m4_quote(m4_toupper(m4_normalize([$*]))),
[[ ]+],[],[\+],[X],[^\(.\)\([0123456789].*\)$],[[[\1_\2]]],
[[^ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_]],[_],
[^._\(.*\)_.$],[[[\1]]])])
