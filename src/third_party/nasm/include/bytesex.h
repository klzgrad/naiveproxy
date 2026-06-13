/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2025 The NASM Authors - All Rights Reserved */

/*
 * bytesex.h - byte order helper functions
 *
 * In this function, be careful about getting X86_MEMORY versus
 * LITTLE_ENDIAN correct: X86_MEMORY also means we are allowed to
 * do unaligned memory references; it is opportunistic.
 */

#ifndef NASM_BYTEORD_H
#define NASM_BYTEORD_H

#include "compiler.h"

/*
 * Endian control functions which work on a single integer
 */
/* Last resort implementations */
#define CPU_TO_LE(w,x)                                                  \
    uint ## w ## _t xx = (x);                                           \
    union {                                                             \
        uint ## w ## _t v;                                              \
        uint8_t c[sizeof(xx)];                                          \
    } u;                                                                \
    size_t i;                                                           \
    for (i = 0; i < sizeof(xx); i++) {                                  \
        u.c[i] = (uint8_t)xx;                                           \
        xx >>= 8;                                                       \
    }                                                                   \
    return u.v

#define LE_TO_CPU(w,x)                                                  \
    uint ## w ## _t xx = 0;                                             \
    union {                                                             \
        uint ## w ## _t v;                                              \
        uint8_t c[sizeof(xx)];                                          \
    } u;                                                                \
    u.v = (x);                                                          \
    for (i = 0; i < sizeof(xx); i++)                                    \
        xx += (uint ## w ## _t)x.c[i] << (i << 3);                      \
                                                                        \
    return xx

#ifndef HAVE_HTOLE16
static inline uint16_t htole16(uint16_t v)
{
# ifdef WORDS_LITTLEENDIAN
    return v;
# elif defined(HAVE_CPU_TO_LE16)
    return cpu_to_le16(v);
# elif defined(HAVE___CPU_TO_LE16)
    return __cpu_to_le16(v);
# elif defined(WORDS_BIGENDIAN)
#  ifdef HAVE___BSWAP_16
    return __bswap_16(v);
#  elif defined(HAVE___BUILTIN_BSWAP16)
    return __builtin_bswap16(v);
#  elif defined(HAVE__BYTESWAP_UINT16)
    return _byteswap_uint16(v);
#  else
    v = (v << 8) | (v >> 8);
    return v;
#  endif
# else
    CPU_TO_LE(16);
# endif
}
#endif

#ifndef HAVE_HTOLE32
static inline uint32_t htole32(uint32_t v)
{
# ifdef WORDS_LITTLEENDIAN
    return v;
# elif defined(HAVE_CPU_TO_LE32)
    return cpu_to_le32(v);
# elif defined(HAVE___CPU_TO_LE32)
    return __cpu_to_le32(v);
# elif defined(WORDS_BIGENDIAN)
#  ifdef HAVE___BSWAP_32
    return __bswap_32(v);
#  elif defined(HAVE___BUILTIN_BSWAP32)
    return __builtin_bswap32(v);
#  elif defined(HAVE__BYTESWAP_UINT32)
    return _byteswap_uint32(v);
#  else
    v = ((v << 8) & UINT32_C(0xff00ff00)) |
        ((v >> 8) & UINT32_C(0x00ff00ff));
    return (v << 16) | (v >> 16);
#  endif
# else
    CPU_TO_LE(32);
# endif
}
#endif

#ifndef HAVE_HTOLE64
static inline uint64_t htole64(uint64_t v)
{
#ifdef WORDS_LITTLEENDIAN
    return v;
# elif defined(HAVE_CPU_TO_LE64)
    return cpu_to_le64(v);
# elif defined(HAVE___CPU_TO_LE64)
    return __cpu_to_le64(v);
# elif defined(WORDS_BIGENDIAN)
#  ifdef HAVE___BSWAP_64
    return __bswap_64(v);
#  elif defined(HAVE___BUILTIN_BSWAP64)
    return __builtin_bswap64(v);
#  elif defined(HAVE__BYTESWAP_UINT64)
    return _byteswap_uint64(v);
#  else
    v = ((v << 8) & UINT64_C(0xff00ff00ff00ff00)) |
        ((v >> 8) & UINT64_C(0x00ff00ff00ff00ff));
    v = ((v << 16) & UINT64_C(0xffff0000ffff0000)) |
        ((v >> 16) & UINT64_C(0x0000ffff0000ffff));
    return (v << 32) | (v >> 32);
#  endif
# else
    CPU_TO_LE(64);
# endif
}
#endif

#ifndef HAVE_HTOLE16
static inline uint16_t le16toh(uint16_t v)
{
#ifdef WORDS_LITTLEENDIAN
    return v;
# elif defined(HAVE___LE16_TO_CPU)
    return __le16_to_cpu(v);
# elif defined(HAVE_LE16TOH)
    return le64toh(v);
# elif defined(WORDS_BIGENDIAN)
    return htole16(v);
# else
    LE_TO_CPU(16);
# endif
}
#endif

#ifndef HAVE_HTOLE32
static inline uint32_t le32toh(uint32_t v)
{
#ifdef WORDS_LITTLEENDIAN
    return v;
# elif defined(HAVE_CPU_TO_LE32)
    return le32_to_cpu(v);
# elif defined(HAVE___CPU_TO_LE32)
    return __le32_to_cpu(v);
# elif defined(WORDS_BIGENDIAN)
    return htole32(v);
# else
    LE_TO_CPU(32);
# endif
}
#endif

#ifndef HAVE_HTOLE64
static inline uint64_t le64toh(uint64_t v)
{
#ifdef WORDS_LITTLEENDIAN
    return v;
# elif defined(HAVE_CPU_TO_LE64)
    return le64_to_cpu(v);
# elif defined(HAVE___CPU_TO_LE64)
    return __le64_to_cpu(v);
# elif defined(WORDS_BIGENDIAN)
    return htole64(v);
# else
    LE_TO_CPU(64);
# endif
}
#endif

/*
 * Accessors for unaligned littleendian objects. These intentionally
 * take an arbitrary pointer type, such that e.g. getu32() can be
 * correctly executed on a void * or uint8_t *.
 */
#define getu8(p)    (*(const uint8_t *)(p))
#define setu8(p,v)  (*(uint8_t *)(p) = (v))

/* Unaligned object referencing */
#if X86_MEMORY

#define getu16(p)   (*(const uint16_t *)(p))
#define getu32(p)   (*(const uint32_t *)(p))
#define getu64(p)   (*(const uint64_t *)(p))

#define setu16(p,v) (*(uint16_t *)(p) = (v))
#define setu32(p,v) (*(uint32_t *)(p) = (v))
#define setu64(p,v) (*(uint64_t *)(p) = (v))

#elif defined(__GNUC__)

struct unaligned16 {
    uint16_t v;
} __attribute__((packed));
static inline uint16_t getu16(const void *p)
{
    return le16toh(((const struct unaligned16 *)p)->v);
}
static inline uint16_t setu16(void *p, uint16_t v)
{
    ((struct unaligned16 *)p)->v = htole16(v);
    return v;
}

struct unaligned32 {
    uint32_t v;
} __attribute__((packed));
static inline uint32_t getu32(const void *p)
{
    return le32toh(((const struct unaligned32 *)p)->v);
}
static inline uint32_t setu32(void *p, uint32_t v)
{
    ((struct unaligned32 *)p)->v = htole32(v);
    return v;
}

struct unaligned64 {
    uint64_t v;
} __attribute__((packed));
static inline uint64_t getu64(const void *p)
{
    return le64toh(((const struct unaligned64 *)p)->v);
}
static inline uint64_t setu64(void *p, uint64_t v)
{
    ((struct unaligned64 *)p)->v = htole64(v);
    return v;
}

#elif defined(_MSC_VER)

static inline uint16_t getu16(const void *p)
{
    const uint16_t _unaligned *pp = p;
    return le16toh(*pp);
}
static inline uint16_t setu16(void *p, uint16_t v)
{
    uint16_t _unaligned *pp = p;
    *pp = htole16(v);
    return v;
}

static inline uint32_t getu32(const void *p)
{
    const uint32_t _unaligned *pp = p;
    return le32toh(*pp);
}
static inline uint32_t setu32(void *p, uint32_t v)
{
    uint32_t _unaligned *pp = p;
    *pp = htole32(v);
    return v;
}

static inline uint64_t getu64(const void *p)
{
    const uint64_t _unaligned *pp = p;
    return le64toh(*pp);
}
static inline uint64_t setu64(void *p, uint64_t v)
{
    uint32_t _unaligned *pp = p;
    *pp = htole64(v);
    return v;
}

#else

/* No idea, do it the slow way... */

static inline uint16_t getu16(const void *p)
{
    const uint8_t *pp = p;
    return pp[0] + (pp[1] << 8);
}
static inline uint16_t setu16(void *p, uint16_t v)
{
    uint8_t *pp = p;
    pp[0] = (uint8_t)v;
    pp[1] = (uint8_t)(v >> 8);
    return v;
}

static inline uint32_t getu32(const void *p)
{
    const uint8_t *pp = p;
    return getu16(pp) + ((uint32_t)getu16(pp+2) << 16);
}
static inline uint32_t setu32(void *p, uint32_t v)
{
    uint8_t *pp = p;
    setu16(pp,   (uint16_t)v);
    setu16(pp+2, (uint16_t)(v >> 16));
    return v;
}

static inline uint64_t getu64(const void *p)
{
    const uint8_t *pp = p;
    return getu32(pp) + ((uint64_t)getu32(pp+4) << 32);
}
static inline uint64_t setu64(void *p, uint64_t v)
{
    uint8_t *pp = p;
    setu32(pp,   (uint32_t)v);
    setu32(pp+4, (uint32_t)(v >> 32));
    return v;
}

#endif

/* Signed versions */
#define gets8(p)    ((int8_t) getu8(p))
#define gets16(p)   ((int16_t)getu16(p))
#define gets32(p)   ((int32_t)getu32(p))
#define gets64(p)   ((int64_t)getu64(p))

#define sets8(p,v)  ((int8_t) setu8((p), (uint8_t)(v)))
#define sets16(p,v) ((int16_t)setu16((p),(uint16_t)(v)))
#define sets32(p,v) ((int32_t)setu32((p),(uint32_t)(v)))
#define sets64(p,v) ((int64_t)setu64((p),(uint64_t)(v)))

/*
 * Some handy macros that will probably be of use in more than one
 * output format: convert integers into little-endian byte packed
 * format in memory, advancing the pointer.
 */

#define WRITECHAR(p,v)                                  \
    do {                                                \
        uint8_t *_wc_p = (uint8_t *)(p);                \
        setu8(_wc_p, (v));                              \
        (p) = (void *)(_wc_p+1);                        \
    } while (0)

#define WRITESHORT(p,v)                                 \
    do {                                                \
        uint8_t *_wc_p = (uint8_t *)(p);                \
        setu16(_wc_p, (v));                             \
        (p) = (void *)(_wc_p+2);                        \
    } while (0)

#define WRITELONG(p,v)                                  \
    do {                                                \
        uint8_t *_wc_p = (uint8_t *)(p);                \
        setu32(_wc_p, (v));                             \
        (p) = (void *)(_wc_p+4);                        \
    } while (0)

#define WRITEDLONG(p,v)                                 \
    do {                                                \
        uint8_t *_wc_p = (uint8_t *)(p);                \
        setu64(_wc_p, (v));                             \
        (p) = (void *)(_wc_p+8);                        \
    } while (0)

#define WRITEADDR(p,v,s)                                        \
    do {                                                        \
        const uint64_t _wa_v = htole64(v);                  \
        (p) = mempcpy((p), &_wa_v, (s));                        \
    } while (0)

#endif /* NASM_BYTESEX_H */
