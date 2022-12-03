dnl --------------------------------------------------------------------------
dnl PA_FUNC_ATTRIBUTE(attribute_name)
dnl
dnl See if this compiler supports the equivalent of a specific gcc
dnl attribute on a function, using the __attribute__(()) syntax.
dnl All arguments except the attribute name are optional.
dnl
dnl PA_FUNC_ATTRIBUTE(attribute, attribute_opts, return_type,
dnl                   prototype_args, call_args, altname)
dnl
dnl This tests the attribute both on a function pointer and on a
dnl direct function, as some gcc [and others?] versions have problems
dnl with attributes on function pointers, and we might as well check both.
dnl --------------------------------------------------------------------------
AC_DEFUN([_PA_FUNC_ATTRIBUTE],
[m4_define([_pa_faa],ifelse([$2],[],[],[($2)]))
 m4_define([_pa_fam],ifelse([$2],[],[],[(m4_join([,],m4_for(_pa_n,1,m4_count($2),1,[m4_quote([x]_pa_n),])))]))
 m4_define([_pa_suf],ifelse([$2],[],[],[m4_count($2)]))
 m4_define([_pa_mac],ifelse([$6],[],[$1_func]_pa_suf,[$6]))
 AC_MSG_CHECKING([if $CC supports the $1]_pa_faa[ function attribute])
 AC_COMPILE_IFELSE([AC_LANG_SOURCE([
AC_INCLUDES_DEFAULT
extern ifelse([$3],[],[void *],[$3])  __attribute__(([$1]_pa_faa))
  bar(ifelse([$4],[],[int],[$4]));
ifelse([$3],[],[void *],[$3]) foo(void);
ifelse([$3],[],[void *],[$3]) foo(void)
{
	ifelse([$3],[void],[],[return])
		bar(ifelse([$5],[],[1],[$5]));
}
 ])],
 [AC_MSG_RESULT([yes])
  AC_DEFINE(PA_SYM([HAVE_FUNC_ATTRIBUTE],_pa_suf,[_$1]), 1,
    [Define to 1 if your compiler supports __attribute__(($1)) on functions])],
 [AC_MSG_RESULT([no])])
 AH_BOTTOM(m4_quote(m4_join([],
 [#ifndef ],_pa_mac,[
# ifdef ],PA_SYM([HAVE_FUNC_ATTRIBUTE],_pa_suf,[_$1]),[
#  define ],_pa_mac,m4_quote(_pa_fam),[ __attribute__(($1],m4_quote(_pa_fam),[))
# else
#  define ],_pa_mac,m4_quote(_pa_fam),[
# endif
#endif])))
])

AC_DEFUN([_PA_FUNC_PTR_ATTRIBUTE],
[m4_define([_pa_faa],ifelse([$2],[],[],[($2)]))
 m4_define([_pa_fam],ifelse([$2],[],[],[(m4_join([,],m4_for(_pa_n,1,m4_count($2),1,[m4_quote([x]_pa_n),])))]))
 m4_define([_pa_suf],ifelse([$2],[],[],[m4_count($2)]))
 m4_define([_pa_mac],ifelse([$6],[],[$1_func]_pa_suf,[$6])_ptr)
 AC_MSG_CHECKING([if $CC supports the $1]_pa_faa[ function attribute on pointers])
 AC_COMPILE_IFELSE([AC_LANG_SOURCE([
AC_INCLUDES_DEFAULT
extern ifelse([$3],[],[void *],[$3])  __attribute__(([$1]_pa_faa))
  (*bar1)(ifelse([$4],[],[int],[$4]));
ifelse([$3],[],[void *],[$3]) foo1(void);
ifelse([$3],[],[void *],[$3]) foo1(void)
{
	ifelse([$3],[void],[],[return])
		bar1(ifelse([$5],[],[1],[$5]));
}

typedef ifelse([$3],[],[void *],[$3])  __attribute__(([$1]_pa_faa))
  (*bar_t)(ifelse([$4],[],[int],[$4]));
extern bar_t bar2;
ifelse([$3],[],[void *],[$3]) foo2(void);
ifelse([$3],[],[void *],[$3]) foo2(void)
{
	ifelse([$3],[void],[],[return])
		bar2(ifelse([$5],[],[1],[$5]));
}
 ])],
 [AC_MSG_RESULT([yes])
  AC_DEFINE(PA_SYM([HAVE_FUNC_PTR_ATTRIBUTE],_pa_suf,[_$1]), 1,
    [Define to 1 if your compiler supports __attribute__(($1)) on function pointers])],
 [AC_MSG_RESULT([no])])
 AH_BOTTOM(m4_quote(m4_join([],
 [#ifndef ],_pa_mac,[
# ifdef ],PA_SYM([HAVE_FUNC_PTR_ATTRIBUTE],_pa_suf,[_$1]),[
#  define ],_pa_mac,m4_quote(_pa_fam),[ __attribute__(($1],m4_quote(_pa_fam),[))
# else
#  define ],_pa_mac,m4_quote(_pa_fam),[
# endif
#endif])))
])

AC_DEFUN([PA_FUNC_ATTRIBUTE],
[_PA_FUNC_ATTRIBUTE([$1],[$2],[$3],[$4],[$5],[$6])
 _PA_FUNC_PTR_ATTRIBUTE([$1],[$2],[$3],[$4],[$5],[$6])])
