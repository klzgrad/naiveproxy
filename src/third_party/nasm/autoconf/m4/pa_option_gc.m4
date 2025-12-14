dnl --------------------------------------------------------------------------
dnl PA_OPTION_GC
dnl
dnl Option to compile with garbage collection; currently only supports
dnl gcc/ELF. Enabled by default.
dnl --------------------------------------------------------------------------
AC_DEFUN([PA_OPTION_GC],
[PA_ARG_DISABLED([gc],
 [do not compile with dead code garbage collection support],
 [],
 [PA_ADD_LDFLAGS([-Wl,--as-needed])
  PA_ADD_CFLAGS([-ffunction-sections])
  PA_ADD_CFLAGS([-fdata-sections])
  PA_ADD_LDFLAGS([-Wl,--gc-sections])])])
