dnl --------------------------------------------------------------------------
dnl PA_LANG_SEEN_LIST(subset)
dnl
dnl  List of the language lang has been used in the configuration
dnl  script so far, possibly subset by [subset].
dnl
dnl This relies on overriding _AC_LANG_SET(from, to),
dnl the internal implementation of _AC_LANG.
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_LANG_SEEN_LIST],
[m4_ifblank([$1],
[m4_define([_pa_lang_seen_list_out],m4_dquote(m4_set_list(PA_LANG_SEEN_SET)))],
[m4_set_delete([_pa_lang_seen_subset])dnl
m4_set_add_all([_pa_lang_seen_subset],$1)dnl
m4_define([_pa_lang_seen_list_out],m4_dquote(m4_cdr(m4_set_intersection([_pa_lang_seen_subset],PA_LANG_SEEN_SET))))dnl
m4_dquote(_pa_lang_seen_list_out)])])
