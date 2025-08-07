dnl --------------------------------------------------------------------------
dnl PA_LANG_FOREACH(subset, body)
dnl
dnl  Expand [body] for each language encountered in the configure script also
dnl  present in [subset], or all if [subset] is empty
dnl --------------------------------------------------------------------------
AC_DEFUN([_PA_LANG_DO],dnl
[AC_LANG([$2])dnl
$1])

AC_DEFUN([PA_LANG_FOREACH],dnl
[m4_pushdef([_pa_lang_foreach_current],[_AC_LANG])dnl
m4_map_args([m4_curry([_PA_LANG_DO],[$2])],m4_unquote(PA_LANG_SEEN_LIST($1)))dnl
AC_LANG(_pa_lang_foreach_current)dnl
m4_popdef([_pa_lang_foreach_current])])
