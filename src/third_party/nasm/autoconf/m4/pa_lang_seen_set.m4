dnl --------------------------------------------------------------------------
dnl PA_LANG_SEEN_SET
dnl
dnl  Set of the languages that have been used in the configuration.
dnl
dnl This relies on overriding _AC_LANG_SET(from, to),
dnl the internal implementation of _AC_LANG.
dnl
dnl The very first language transition [] -> [C] is ignored, because
dnl it is done from AC_INIT regardless of any user specified language.
dnl --------------------------------------------------------------------------

m4_ifndef([_PA_LANG_SET],
[m4_rename([_AC_LANG_SET], [_PA_LANG_SET])
m4_set_delete([_pa_lang_seen_set])
m4_defun([_AC_LANG_SET],
[m4_ifnblank([$1],[m4_set_add([_pa_lang_seen_set],[$2])])_PA_LANG_SET($@)])])

AC_DEFUN([PA_LANG_SEEN_SET],[[_pa_lang_seen_set]])
