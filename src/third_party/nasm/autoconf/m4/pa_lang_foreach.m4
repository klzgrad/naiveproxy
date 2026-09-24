dnl --------------------------------------------------------------------------
dnl PA_LANG_FOREACH(subset, body)
dnl
dnl  Expand [body] for each language encountered in the configure script also
dnl  present in [subset], or all if [subset] is empty
dnl --------------------------------------------------------------------------
AC_DEFUN([_PA_LANG_FOREACH],
[m4_pushdef([pa_lang_for_each])dnl
m4_foreach([pa_lang_for_each],$1,[dnl
AC_LANG_PUSH(pa_lang_for_each)
$2
AC_LANG_POP(pa_lang_for_each)
])dnl
m4_popdef([pa_lang_for_each])])

AC_DEFUN([PA_LANG_FOREACH],
[_PA_LANG_FOREACH(m4_dquote(PA_LANG_SEEN_LIST(m4_dquote($1))),[$2])])
