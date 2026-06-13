dnl --------------------------------------------------------------------------
dnl PA_LIBEXT
dnl
dnl Guess the library extension based on the object extension
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_LIBEXT],
[AC_MSG_CHECKING([for suffix of library files])
if test x"$LIBEXT" = x; then
  case "$OBJEXT" in
    obj)
      LIBEXT=lib
      ;;
    *)
      LIBEXT=a
      ;;
  esac
fi
AC_MSG_RESULT([$LIBEXT])
AC_SUBST([LIBEXT])])
