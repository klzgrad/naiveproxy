dnl --------------------------------------------------------------------------
dnl PA_ADD_LANGFLAGS(flag...)
dnl
dnl Attempt to add the option in the given list to each compiler flags
dnl (CFLAGS, CXXFLAGS, ...), if it doesn't break compilation.
dnl --------------------------------------------------------------------------
m4_defun([_PA_LANGFLAG_VAR],
[m4_case([$1],
 [C], [CFLAGS],
 [C++], [CXXFLAGS],
 [Fortran 77], [FFLAGS],
 [Fortran], [FCFLAGS],
 [Erlang], [ERLCFLAGS],
 [Objective C], [OBJCFLAGS],
 [Objective C++], [OBJCXXFLAGS],
 [Go], [GOFLAGS],
 [m4_fatal([PA_ADD_LANGFLAGS: Unknown language: $1])])])

AC_DEFUN([PA_ADD_LANGFLAGS],
[m4_pushdef([_pa_langflags],m4_dquote($1))dnl
m4_set_foreach(_PA_LANG_SEEN_SET,[_pa_lang],dnl
[_pa_flag_found=no
 m4_foreach_w([_pa_flag], _pa_langflags,
 [AS_IF([test $_pa_flag_found = no],
  [PA_ADD_FLAGS(_PA_LANGFLAG_VAR(_pa_lang),_pa_flag,[],[_pa_flag_found=yes])])
  ])])
m4_popdef([_pa_langflags])])
