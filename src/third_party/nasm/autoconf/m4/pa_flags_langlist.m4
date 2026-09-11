dnl --------------------------------------------------------------------------
dnl PA_FLAGS_LANGLIST(flagvar)
dnl
dnl  Return a list of languages affected by the variable flagvar.
dnl  If flagvar is unknown, assume it affects the current language.
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_FLAGS_LANGLIST],
[m4_case([$1],
	[CPPFLAGS], [[C],[C++],[Objective C],[Objective C++]],
	[CFLAGS], [[C]],
	[CXXFLAGS], [[C++]],
	[FFLAGS], [[Fortran 77]],
	[FCFLAGS], [[Fortran]],
	[ERLCFLAGS], [[Erlang]],
	[OBJCFLAGS], [[Objective C]],
	[OBJCXXFLAGS], [[Objective C++]],
	[GOFLAGS], [[Go]],
	[LDFLAGS], [[C],[C++],[Fortran 77],[Fortran],[Objective C],[Objective C++],[Go]],
	m4_quote(_AC_LANG))])
