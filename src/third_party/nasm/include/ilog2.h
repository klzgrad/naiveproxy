/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2025 The NASM Authors - All Rights Reserved */

/*
 * ilog2_xx(x) = x ? floor(log_2(x)) : 0
 */
#ifndef NASM_ILOG2_H
#define NASM_ILOG2_H

#include "compiler.h"

#ifdef ILOG2_C                  /* For generating the out-of-line functions */
# ifndef HAVE_MSVC_INLINE
#  undef extern_inline
#  define extern_inline
# endif
# define inline_prototypes
#endif

#ifdef inline_prototypes
extern unsigned int const_func ilog2_32(uint32_t v);
extern unsigned int const_func ilog2_64(uint64_t v);
extern int const_func alignlog2_32(uint32_t v);
extern int const_func alignlog2_64(uint64_t v);
#endif

#ifdef extern_inline

# define ROUND(v, a, w)                                  \
    do {                                                \
        if (v & (((UINT32_C(1) << w) - 1) << w)) {      \
            a  += w;                                    \
            v >>= w;                                    \
        }                                               \
    } while (0)

# define static_nz(x) (is_constant(x != 0) && (x != 0))
# define defang_zero(x) ((x) | !static_nz(x))

# if defined(HAVE_STDC_LEADING_ZEROS)

extern_inline unsigned int const_func ilog2_32(uint32_t v)
{
    return stdc_leading_zeros(defang_zero(v)) ^ 31;
}

extern_inline unsigned int const_func ilog2_64(uint64_t v)
{
    return stdc_leading_zeros(defang_zero(v)) ^ 63;
}

# else

#  if defined(HAVE___BUILTIN_CLZ) && INT_MAX == 2147483647

extern_inline unsigned int const_func ilog2_32(uint32_t v)
{
    return __builtin_clz(v|1) ^ 31;
}

#  elif defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))

extern_inline unsigned int const_func ilog2_32(uint32_t v)
{
    unsigned int n;

#   ifdef __x86_64__
    __asm__("bsrl %1,%0"
            : "=r" (n)
            : "rm" (v), "0" (0));
#   else
    __asm__("bsrl %1,%0"
            : "=r" (n)
            : "rm" (defang_zero(v)));
#   endif
    return n;
}

#  elif defined(HAVE__BITSCANREVERSE)

extern_inline unsigned int const_func ilog2_32(uint32_t v)
{
    unsigned long ix;
    return _BitScanReverse(&ix, v) ? v : 0;
}

#  else

extern_inline unsigned int const_func ilog2_32(uint32_t v)
{
    unsigned int p = 0;

    ROUND(v, p, 16);
    ROUND(v, p,  8);
    ROUND(v, p,  4);
    ROUND(v, p,  2);
    ROUND(v, p,  1);

    return p;
}

#  endif

#  if defined(HAVE__BUILTIN_CLZLL) && LLONG_MAX == 9223372036854775807LL

extern_inline unsigned int const_func ilog2_64(uint64_t v)
{
    return __builtin_clzll(defang_zero(v)) ^ 63;
}

#  elif defined(__GNUC__) && defined(__x86_64__)

extern_inline unsigned int const_func ilog2_64(uint64_t v)
{
    uint64_t n;

    __asm__("bsrq %1,%0"
            : "=r" (n)
            : "rm" (v), "0" (UINT64_C(0)));
    return n;
}

#  elif defined(HAVE__BITSCANREVERSE64)

extern_inline unsigned int const_func ilog2_64(uint64_t v)
{
    unsigned long ix;
    return _BitScanReverse64(&ix, v) ? ix : 0;
}

#  else

extern_inline unsigned int const_func ilog2_64(uint64_t vv)
{
    unsigned int p = 0;
    uint32_t v;

    v = vv >> 32;
    if (v)
        p += 32;
    else
        v = vv;

    return p + ilog2_32(v);
}

#  endif
# endif

/*
 * v == 0 ? 0 : is_power2(x) ? ilog2_X(v) : -1
 */
extern_inline int const_func alignlog2_32(uint32_t v)
{
    if (unlikely(v & (v-1)))
        return -1;              /* invalid alignment */

    return ilog2_32(v);
}

extern_inline int const_func alignlog2_64(uint64_t v)
{
    if (unlikely(v & (v-1)))
        return -1;              /* invalid alignment */

    return ilog2_64(v);
}

#undef ROUND

#endif /* extern_inline */

#endif /* ILOG2_H */
