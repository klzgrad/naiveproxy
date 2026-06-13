dnl --------------------------------------------------------------------------
dnl PA_ADD_LANGFLAGS(flag...)
dnl
dnl Attempt to add the first accepted option in the given list to each
dnl compiler flags (CFLAGS, CXXFLAGS, ...).
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_ADD_LANGFLAGS],
[PA_LANG_FOREACH(PA_LANG_HAVE_FLAGVAR_LIST,
 [PA_FIND_FLAGS(m4_quote(PA_LANG_FLAGVAR),[$@])])])
