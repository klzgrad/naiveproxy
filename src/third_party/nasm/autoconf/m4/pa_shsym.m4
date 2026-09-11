dnl --------------------------------------------------------------------------
dnl PA_SHSYM(...)
dnl
dnl Convert a (semi-) arbitrary string to a shell symbol
dnl Convert non-shell characters to underscores, except + which is converted
dnl to x (so C++ -> cxx). Unlike PA_SYM(), do not compact multiple
dnl underscores.
dnl
dnl This currently differs from PA_CSYM only in not doing case conversion.
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_SHSYM],
[m4_bpatsubsts(m4_quote(m4_normalize([$*])),
[[ ]+],[],[\+],[x],[[^abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_]],[_],
[^._\(.*\)_.$],[[[\1]]])])
