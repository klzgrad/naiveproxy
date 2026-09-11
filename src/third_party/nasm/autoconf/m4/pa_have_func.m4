dnl --------------------------------------------------------------------------
dnl PA_HAVE_FUNC([func_name ...][,arguments [,headers [,return_type]]])
dnl
dnl Look for a function with the specified arguments which could be
dnl a macro/builtin/intrinsic function. If "arguments" are omitted,
dnl then (0) is used assumed; if "return_type" is omitted or "void", the
dnl expression is cast to (void).
dnl --------------------------------------------------------------------------
AC_DEFUN([_PA_HAVE_FUNC_INCLUDE],
[m4_echo([#ifdef ]PA_CSYM([HAVE_$1])[
#include <$1>
#endif
])])

AC_DEFUN([PA_HAVE_FUNC],
[AS_VAR_PUSHDEF([cache],[PA_SHSYM([pa_cv_func_$1])])
 AC_CACHE_CHECK([for $1], [cache],
[
m4_ifnblank([$3],[AC_CHECK_HEADERS_ONCE($3)])dnl
m4_pushdef([pa_func_args],m4_strip(m4_default([$2],[(0)])))dnl
m4_pushdef([pa_func_type],m4_default([$4],[void]))dnl
AC_LINK_IFELSE([AC_LANG_PROGRAM(
[AC_INCLUDES_DEFAULT]
m4_map_args_w([$3], [_PA_HAVE_FUNC_INCLUDE(], [)]),
m4_cond(pa_func_type,[void],[
    (void)$1]pa_func_args[;],[
    ]pa_func_type[ tmp = $1]pa_func_args[;
    (void)tmp;])
)],[AS_VAR_SET([cache],[yes])],[AS_VAR_SET([cache],[no])])
m4_popdef([pa_func_args])dnl
m4_popdef([pa_func_type])dnl
])

AS_VAR_IF([cache],[yes],
[AC_DEFINE(PA_CSYM([HAVE_$1]), 1,
["Define to 1 if you have the `$1' intrinsic function."])])
])
