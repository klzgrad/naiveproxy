dnl --------------------------------------------------------------------------
dnl PA_LANG_SEEN_LIST(subset)
dnl
dnl  List of the language lang has been used in the configuration
dnl  script so far, possibly subset by [subset].
dnl
dnl This relies on overriding _AC_LANG_SET(from, to),
dnl the internal implementation of _AC_LANG.
dnl --------------------------------------------------------------------------
m4_ifndef([_PA_LANG_SET],
[m4_rename([_AC_LANG_SET], [_PA_LANG_SET])dnl
m4_defun([_AC_LANG_SET], [m4_set_add([_PA_LANG_SEEN_SET],[$2])dnl
_PA_LANG_SET($@)])])

AC_DEFUN([PA_LANG_SEEN_LIST],
[m4_set_delete([_pa_lang_seen_subset])dnl
m4_pushdef([_pa_lang_seen_subset_list],m4_ifnblank([$1],[$1],m4_dquote(m4_set_list([_PA_LANG_SEEN_SET]))))dnl
m4_set_add_all([_pa_lang_seen_subset],_pa_lang_seen_subset_list)dnl
m4_cdr(m4_set_intersection([_pa_lang_seen_subset],[_PA_LANG_SEEN_SET]))dnl
m4_popdef([_pa_lang_seen_subset_list])])
