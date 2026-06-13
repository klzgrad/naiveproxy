/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2007-2025 The NASM Authors - All Rights Reserved */

/*
 * compiler.h
 *
 * Compiler-specific macros for NASM.  Feel free to add support for
 * other compilers in here.
 *
 * This header file should be included before any other header.
 */

#ifndef NASM_COMPILER_H
#define NASM_COMPILER_H 1

/*
 * At least DJGPP and Cygwin have broken header files if __STRICT_ANSI__
 * is defined.
 */
#ifdef __GNUC__
# undef __STRICT_ANSI__
#endif

/* On Microsoft platforms we support multibyte character sets in filenames */
#define _MBCS 1

#include "autoconf/attribute.h"

#ifdef HAVE_CONFIG_H
# include "config/config.h"
#else
# if defined(_MSC_VER) && (_MSC_VER >= 1310)
#  include "config/msvc.h"
# elif defined(__WATCOMC__)
#  include "config/watcom.h"
# else
#  include "config/unknown.h"
# endif
/* This unconditionally defines some macros we really want */
# include "config/unconfig.h"
#endif /* Configuration file */

/* This is required to get the standard <inttypes.h> macros when compiling
   with a C++ compiler.  This must be defined *before* <inttypes.h> is
   included, directly or indirectly. */
#define __STDC_CONSTANT_MACROS	1
#define __STDC_LIMIT_MACROS	1
#define __STDC_FORMAT_MACROS	1

#ifdef HAVE_INTTYPES_H
# include <inttypes.h>
#else
# include "nasmint.h"
#endif

#include <assert.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif

/*
 * This is impossible to do 100% accurately, because the actual type
 * of size_t may differ from its range.
 */
#ifdef _WIN32
# define PRIz "I"               /* Needed for msvcrt, not ucrt */
#elif defined(PRINTF_SUPPORTS_Z) || \
    (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L)
# define PRIz "z"
#elif SIZE_MAX == UINT_MAX
# define PRIz ""
#elif SIZE_MAX == ULONG_MAX
# define PRIz "l"
#elif SIZE_MAX == ULONGLONG_MAX
# define PRIz "ll"
#else
# error "Unable to determine printf format for size_t"
#endif
#define PRIzd PRIz "u"          /* size_t is always unsigned */
#define PRIzu PRIz "u"
#define PRIzu PRIz "u"
#define PRIzx PRIz "x"
#define PRIzX PRIz "X"

#ifdef HAVE_STDBIT_H

# include <stdbit.h>

# undef WORDS_LITTLEENDIAN
# undef WORDS_BIGENDIAN
# if __STDC_ENDIAN_NATIVE__ == __STDC_ENDIAN_LITTLE__
#  define WORDS_LITTLEENDIAN 1
# elif __STDC_ENDIAN_NATIVE__ == __STDC_ENDIAN_BIG__
#  define WORDS_BIGENDIAN 1
# endif

#else  /* No <stdbit.h> */

# ifdef HAVE_ENDIAN_H
#  include <endian.h>
# elif defined(HAVE_SYS_ENDIAN_H)
#  include <sys/endian.h>
# elif defined(HAVE_MACHINE_ENDIAN_H)
#  include <machine/endian.h>
# endif

/*
 * If we have BYTE_ORDER defined, or the compiler provides
 * __BIG_ENDIAN__ or __LITTLE_ENDIAN__, trust it over what autoconf
 * came up with, especially since autoconf obviously can't figure
 * things out for a universal compiler.
 */
# if defined(__BIG_ENDIAN__) && !defined(__LITTLE_ENDIAN__)
#  undef WORDS_LITTLEENDIAN
#  undef WORDS_BIGENDIAN
#  define WORDS_BIGENDIAN 1
# elif defined(__LITTLE_ENDIAN__) && !defined(__BIG_ENDIAN__)
#  undef WORDS_LITTLEENDIAN
#  undef WORDS_BIGENDIAN
#  define WORDS_LITTLEENDIAN 1
# elif defined(BYTE_ORDER) && defined(LITTLE_ENDIAN) && defined(BIG_ENDIAN)
#  undef WORDS_LITTLEENDIAN
#  undef WORDS_BIGENDIAN
#  if BYTE_ORDER == LITTLE_ENDIAN
#   define WORDS_LITTLEENDIAN 1
#  elif BYTE_ORDER == BIG_ENDIAN
#   define WORDS_BIGENDIAN 1
#  endif
# endif

#endif

/*
 * Define this to 1 for faster performance if this is a littleendian
 * platform *and* it can do arbitrary unaligned memory references.  It
 * is safe to leave it defined to 0 even if that is true.
 */
#if defined(__386__) || defined(__i386__) || defined(__x86_64__) \
    || defined(_M_IX86) || defined(_M_X64)
# define X86_MEMORY 1
# undef WORDS_BIGENDIAN
# undef WORDS_LITTLEENDIAN
# define WORDS_LITTLEENDIAN 1
#else
# define X86_MEMORY 0
#endif

/* Some versions of MSVC have these only with underscores in front */
#ifndef HAVE_SNPRINTF
# ifdef HAVE__SNPRINTF
#  define snprintf _snprintf
# else
int snprintf(char *, size_t, const char *, ...);
# endif
#endif

#ifndef HAVE_VSNPRINTF
# ifdef HAVE__VSNPRINTF
#  define vsnprintf _vsnprintf
# else
int vsnprintf(char *, size_t, const char *, va_list);
# endif
#endif

#if !defined(HAVE_STRLCPY) || !HAVE_DECL_STRLCPY
size_t strlcpy(char *, const char *, size_t);
#endif

#if !defined(HAVE_STRCHRNUL) || !HAVE_DECL_STRCHRNUL
char * pure_func strrchrnul(const char *, int);
#endif

#if !defined(__cplusplus) || (__STDC_VERSION >= 202311L)
/* C++ and C23 have bool, false, and true as proper keywords */
# ifdef HAVE_STDBOOL_H
/* If <stdbool.h> exists, include it explicitly to prevent it from
   begin included later, causing the "bool" macro to be defined. */
#  include <stdbool.h>
#  ifdef bool
/* Force bool to be a typedef instead of a macro. What a "clever" hack
   this is... */
    typedef bool                /* The macro definition of bool */
#  undef bool
        bool;                   /* No longer the macro definition */
#  endif
# elif defined(HAVE___BOOL)
   typedef _Bool bool;
#  define false 0
#  define true 1
# else
/* This is a bit dangerous, because casting to this ersatz bool
   will not produce the same result as the standard (bool) cast.
   Instead, use the bool() constructor-style macro defined below. */
typedef enum bool { false, true } bool;
# endif
/* This amounts to a C++-style conversion cast to bool.  This works
   because C ignores an argument-taking macro when used without an
   argument and because bool was redefined as a typedef if it previously
   was defined as a macro (see above.) */
# define bool(x) ((bool)!!(x))
#endif

/* Create a NULL pointer of the same type as the address of
   the argument, without actually evaluating said argument. */
#define nullas(p) (0 ? &(p) : NULL)

/* Convert an offsetted NULL pointer dereference to a size_t offset.
   Technically non-portable as taking the offset from a NULL pointer
   is undefined behavior, but... */
#define null_offset(p) ((size_t)((const char *)&(p) - (const char *)NULL))

/* Provide a substitute for offsetof() if we don't have one.  This
   variant works on most (but not *all*) systems... */
#ifndef offsetof
# define offsetof(t,m) null_offset(((t *)NULL)->m)
#endif

/* If typeof is defined as a macro, assume we have typeof even if
   HAVE_TYPEOF is not declared (e.g. due to not using autoconf.) */
#ifdef typeof
# define HAVE_TYPEOF 1
#endif

/* This is like offsetof(), but takes an object rather than a type. */
#ifndef offsetin
# ifdef HAVE_TYPEOF
#  define offsetin(p,m) offsetof(typeof(p),m)
# else
#  define offsetin(p,m)	null_offset(nullas(p)->m)
# endif
#endif

/* The container_of construct: if p is a pointer to member m of
   container class c, then return a pointer to the container of which
   *p is a member. */
#ifndef container_of
# define container_of(p, c, m) ((c *)((char *)(p) - offsetof(c,m)))
#endif

/* Some misguided platforms hide the defs for these */
#if defined(HAVE_STRCASECMP) && !HAVE_DECL_STRCASECMP
int strcasecmp(const char *, const char *);
#endif

#if defined(HAVE_STRICMP) && !HAVE_DECL_STRICMP
int stricmp(const char *, const char *);
#endif

#if defined(HAVE_STRNCASECMP) && !HAVE_DECL_STRNCASECMP
int strncasecmp(const char *, const char *, size_t);
#endif

#if defined(HAVE_STRNICMP) && !HAVE_DECL_STRNICMP
int strnicmp(const char *, const char *, size_t);
#endif

#if defined(HAVE_STRSEP) && !HAVE_DECL_STRSEP
char *strsep(char **, const char *);
#endif

#if !HAVE_DECL_STRNLEN
size_t strnlen(const char *s, size_t maxlen);
#endif

#ifndef HAVE_MEMPCPY
static inline void *mempcpy(void *dst, const void *src, size_t n)
{
    return (char *)memcpy(dst, src, n) + n;
}
#endif

#ifndef HAVE_MEMPSET
static inline void *mempset(void *dst, int c, size_t n)
{
    return (char *)memset(dst, c, n) + n;
}
#endif

/*
 * Hack to support external-linkage inline functions
 */
#ifndef HAVE_STDC_INLINE
# ifdef __GNUC__
#  ifdef __GNUC_STDC_INLINE__
#   define HAVE_STDC_INLINE
#  else
#   define HAVE_GNU_INLINE
#  endif
# elif defined(__GNUC_GNU_INLINE__)
/* Some other compiler implementing only GNU inline semantics? */
#   define HAVE_GNU_INLINE
# elif defined(_MSC_VER)
/* In MSVC and clang when it is pretending to be MSVC, inline behaves it does in
 * C++.
 */
#  define HAVE_MSVC_INLINE
# elif defined(__STDC_VERSION__)
#  if __STDC_VERSION__ >= 199901L
#   define HAVE_STDC_INLINE
#  endif
# endif
#endif

#ifdef HAVE_STDC_INLINE
# define extern_inline inline
#elif defined(HAVE_GNU_INLINE)
# define extern_inline extern inline
# define inline_prototypes
#elif defined(HAVE_MSVC_INLINE)
# define extern_inline inline
#else
# define inline_prototypes
#endif

/*
 * Hints to the compiler that a particular branch of code is more or
 * less likely to be taken.
 */
#ifdef HAVE___BUILTIN_EXPECT
# define likely(x)	__builtin_expect(bool(x), true)
# define unlikely(x)	__builtin_expect(bool(x), false)
#else
# define likely(x)	bool(x)
# define unlikely(x)	bool(x)
#endif

#ifdef HAVE___BUILTIN_PREFETCH
# define prefetch(x) __builtin_prefetch(x)
#else
# define prefetch(x) ((void)(x))
#endif

/*
 * Attributes
 */

#define safe_alloc     never_null     malloc_func
#define safe_alloc_ptr never_null_ptr malloc_func_ptr

#define safe_malloc(s)          safe_alloc     alloc_size_func1(s)
#define safe_malloc2(s1,s2)     safe_alloc     alloc_size_func2(s1,s2)
#define safe_realloc(s)         never_null     alloc_size_func1(s)
#define safe_malloc_ptr(s)      safe_alloc_ptr alloc_size_func1_ptr(s)
#define safe_malloc2_ptr(s1,s2) safe_alloc_ptr alloc_size_func2_ptr(s1,s2)
#define safe_realloc_ptr(s)     never_null_ptr alloc_size_func1_ptr(s)

/*
 * How to tell the compiler that a function doesn't return
 */
#ifdef HAVE_STDNORETURN_H
# include <stdnoreturn.h>
# define no_return noreturn
#elif defined(_MSC_VER)
# define no_return __declspec(noreturn)
#else
# define no_return noreturn_func
#endif

/* Function priority: pure < reproducible < unsequenced < const */

#ifndef HAVE_FUNC_ATTRIBUTE_REPRODUCIBLE
# undef reproducible_func
# define reproducible_func pure_func
# undef reproducible_func_ptr
# define reproducible_func_ptr pure_func_ptr
#endif

#ifndef HAVE_FUNC_ATTRIBUTE_UNSEQUENCED
# undef unsequenced_func
# define unsequenced_func reproducible_func
# undef unsequenced_func_ptr
# define unsequenced_func_ptr reproducible_func_ptr
#endif

#ifndef HAVE_FUNC_ATTRIBUTE_CONST
# undef const_func
# define const_func reproducible_func
# undef const_func_ptr
# define const_func_ptr reproducible_func_ptr
#endif

/*
 * A fatal function is both unlikely and no_return
 */
#define fatal_func        no_return unlikely_func void
#define static_fatal_func no_return unlikely_func static void

/*
 * How to tell the compiler that a function takes a printf-like string
 */
#define printf_func(fmt, list)     format_func3(printf,fmt,list)
#define printf_func_ptr(fmt, list) format_func3_ptr(printf,fmt,list)
#define vprintf_func(fmt)          format_func3(printf,fmt,0)
#define vprintf_func_ptr(fmt)      format_func3_ptr(printf,fmt,0)

/* Determine probabilistically if something is a compile-time constant */
#ifdef HAVE___BUILTIN_CONSTANT_P
# if defined(__GNUC__) && (__GNUC__ >= 5)
#  define is_constant(x) __builtin_constant_p((x))
# else
#  define is_constant(x) false
# endif
#else
# define is_constant(x) false
#endif

/*
 * If we can guarantee that a particular expression is constant, use it,
 * otherwise use a different version.
 */
#if defined(__GNUC__) && (__GNUC__ >= 3)
# define not_pedantic_start                             \
    _Pragma("GCC diagnostic push")                      \
    _Pragma("GCC diagnostic ignored \"-Wpedantic\"")
# define not_pedantic_end                       \
    _Pragma("GCC diagnostic pop")
#else
# define not_pedantic_start
# define not_pedantic_end
#endif

#ifdef HAVE___BUILTIN_CHOOSE_EXPR
# define if_constant(x,y) __builtin_choose_expr(is_constant(x),(x),(y))
#else
# define if_constant(x,y) (y)
#endif

/*
 * The autoconf documentation states:
 *
 * `va_copy'
 *    The C99 standard provides `va_copy' for copying `va_list'
 *    variables.  It may be available in older environments too, though
 *    possibly as `__va_copy' (e.g., `gcc' in strict pre-C99 mode).
 *    These can be tested with `#ifdef'.  A fallback to `memcpy (&dst,
 *    &src, sizeof (va_list))' gives maximum portability.
 */
#ifndef va_copy
# ifdef __va_copy
#  define va_copy(dst,src) __va_copy(dst,src)
# else
#  define va_copy(dst,src) memcpy(&(dst),&(src),sizeof(va_list))
# endif
#endif

/*
 * If SIZE_MAX is not defined, rely on size_t being unsigned
 */
#ifndef SIZE_MAX
# define SIZE_MAX (((size_t)0) - 1)
#endif

/*
 * Current function name, if available, otherwise NULL
 */
#ifdef HAVE_FUNC_NAME
# define NASM_FUNC __func__
#else
# define NASM_FUNC NULL
#endif

/* Watcom doesn't handle switch statements with 64-bit types, hack around it */
#ifdef __WATCOMC__
# define BOGUS_CASE 0x76543210

static inline unsigned int watcom_switch_hack(uint64_t x)
{
    if (x > (uint64_t)UINT_MAX)
        return BOGUS_CASE;
    else
        return (unsigned int)x;
}

# define switch(x) switch(sizeof(x) > sizeof(unsigned int) \
                          ? watcom_switch_hack(x) : (unsigned int)(x))

/* This is to make sure BOGUS_CASE doesn't conflict with anything real... */
# define default case BOGUS_CASE: default
#endif

#endif	/* NASM_COMPILER_H */
