dnl --------------------------------------------------------------------------
dnl PA_LANG_FLAGVAR([language])
dnl
dnl  Return the name of the compiler flag variable for the current or
dnl  specified language. Returns empty if the variable name is not known.
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_LANG_FLAGVAR],
[m4_case(m4_quote(m4_default([$1],m4_quote(_AC_LANG))),
	[C], [CFLAGS],
	[C++], [CXXFLAGS],
	[Fortran 77], [FFLAGS],
	[Fortran], [FCFLAGS],
	[Erlang], [ERLCFLAGS],
	[Objective C], [OBJCFLAGS],
	[Objective C++], [OBJCXXFLAGS],
	[Go], [GOFLAGS],
	[m4_fatal([PA_LANG_FLAGVAR: Unknown language: $1])])])

AC_DEFUN([PA_LANG_HAVE_FLAGVAR_LIST],
	 [[[C], [C++], [Fortran 77], [Fortran], [Erlang],
	  [Objective C], [Objective C++], [Go]]])
