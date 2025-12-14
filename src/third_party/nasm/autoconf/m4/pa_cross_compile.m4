dnl --------------------------------------------------------------------------
dnl  PA_CROSS_COMPILE
dnl
dnl Get the canonical name for the build and host (runtime) systems;
dnl then figure out if this is cross-compilation. Specifically, this
dnl disables invoking WINE on non-Windows systems which are configured
dnl to run WINE automatically.
dnl
dnl Use PA_CROSS_COMPILE_TOOL if the target system (output of a code-
dnl generation tool) is applicable.
dnl
dnl This doesn't explicitly print any messages as that is automatically
dnl done elsewhere.
dnl --------------------------------------------------------------------------
AC_DEFUN_ONCE([PA_CROSS_COMPILE],
[
 AC_BEFORE([$0], [AC_LANG_COMPILER])
 AC_BEFORE([$0], [AC_LANG])
 AC_BEFORE([$0], [AC_PROG_CC])
 AC_BEFORE([$0], [AC_PROG_CPP])
 AC_BEFORE([$0], [AC_PROG_CXX])
 AC_BEFORE([$0], [AC_PROG_CXXCPP])
 AC_BEFORE([$0], [AC_PROG_OBJC])
 AC_BEFORE([$0], [AC_PROG_OBJCPP])
 AC_BEFORE([$0], [AC_PROG_OBJCXX])
 AC_BEFORE([$0], [AC_PROG_OBJCXXCPP])
 AC_BEFORE([$0], [AC_PROG_F77])
 AC_BEFORE([$0], [AC_PROG_FC])
 AC_BEFORE([$0], [AC_PROG_GO])

 # Disable WINE
 WINELOADER=/dev/null
 export WINELOADER
 WINESERVER=/dev/null
 export WINESERVER
 WINEPREFIX=/dev/null
 export WINEPREFIX

 AC_CANONICAL_BUILD
 AC_CANONICAL_HOST
])
