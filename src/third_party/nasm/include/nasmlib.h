/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2018 The NASM Authors - All Rights Reserved
 *   See the file AUTHORS included with the NASM distribution for
 *   the specific copyright holders.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following
 *   conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *
 *     THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 *     CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *     INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *     MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *     DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 *     CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *     SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *     NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *     LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *     HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 *     OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 *     EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ----------------------------------------------------------------------- */

/*
 * nasmlib.h    header file for nasmlib.c
 */

#ifndef NASM_NASMLIB_H
#define NASM_NASMLIB_H

#include "compiler.h"
#include "bytesex.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif

/*
 * tolower table -- avoids a function call on some platforms.
 * NOTE: unlike the tolower() function in ctype, EOF is *NOT*
 * a permitted value, for obvious reasons.
 */
void tolower_init(void);
extern unsigned char nasm_tolower_tab[256];
#define nasm_tolower(x) nasm_tolower_tab[(unsigned char)(x)]

/* Wrappers around <ctype.h> functions */
/* These are only valid for values that cannot include EOF */
#define nasm_isspace(x)  isspace((unsigned char)(x))
#define nasm_isalpha(x)  isalpha((unsigned char)(x))
#define nasm_isdigit(x)  isdigit((unsigned char)(x))
#define nasm_isalnum(x)  isalnum((unsigned char)(x))
#define nasm_isxdigit(x) isxdigit((unsigned char)(x))

/*
 * Wrappers around malloc, realloc and free. nasm_malloc will
 * fatal-error and die rather than return NULL; nasm_realloc will
 * do likewise, and will also guarantee to work right on being
 * passed a NULL pointer; nasm_free will do nothing if it is passed
 * a NULL pointer.
 */
void * safe_malloc(1) nasm_malloc(size_t);
void * safe_malloc(1) nasm_zalloc(size_t);
void * safe_malloc2(1,2) nasm_calloc(size_t, size_t);
void * safe_realloc(2) nasm_realloc(void *, size_t);
void nasm_free(void *);
char * safe_alloc nasm_strdup(const char *);
char * safe_alloc nasm_strndup(const char *, size_t);
char * safe_alloc nasm_strcat(const char *one, const char *two);
char * safe_alloc end_with_null nasm_strcatn(const char *one, ...);

/* Assert the argument is a pointer without evaluating it */
#define nasm_assert_pointer(p) ((void)sizeof(*(p)))

#define nasm_new(p) ((p) = nasm_zalloc(sizeof(*(p))))
#define nasm_newn(p,n) ((p) = nasm_calloc(sizeof(*(p)),(n)))
/*
 * This is broken on platforms where there are pointers which don't
 * match void * in their internal layout.  It unfortunately also
 * loses any "const" part of the argument, although hopefully the
 * compiler will warn in that case.
 */
#define nasm_delete(p)						\
    do {							\
	void **_pp = (void **)&(p);				\
	nasm_assert_pointer(p);					\
	nasm_free(*_pp);					\
	*_pp = NULL;						\
    } while (0)
#define nasm_zero(x) (memset(&(x), 0, sizeof(x)))
#define nasm_zeron(p,n) (memset((p), 0, (n)*sizeof(*(p))))

/*
 * Wrappers around fread()/fwrite() which fatal-errors on failure.
 * For fread(), only use this if EOF is supposed to be a fatal error!
 */
void nasm_read(void *, size_t, FILE *);
void nasm_write(const void *, size_t, FILE *);

/*
 * NASM assert failure
 */
fatal_func nasm_assert_failed(const char *, int, const char *);
#define nasm_assert(x)                                          \
    do {                                                        \
        if (unlikely(!(x)))                                     \
            nasm_assert_failed(__FILE__,__LINE__,#x);           \
    } while (0)

/*
 * NASM failure at build time if the argument is false
 */
#ifdef static_assert
# define nasm_static_assert(x) static_assert(x, #x)
#elif defined(HAVE_FUNC_ATTRIBUTE_ERROR) && defined(__OPTIMIZE__)
# define nasm_static_assert(x)                                           \
    if (!(x)) {                                                         \
        extern void __attribute__((error("assertion " #x " failed")))   \
            _nasm_static_fail(void);					\
        _nasm_static_fail();                                            \
    }
#else
/* See http://www.drdobbs.com/compile-time-assertions/184401873 */
# define nasm_static_assert(x) \
    do { enum { _static_assert_failed = 1/(!!(x)) }; } while (0)
#endif

/* Utility function to generate a string for an invalid enum */
const char *invalid_enum_str(int);

/*
 * ANSI doesn't guarantee the presence of `stricmp' or
 * `strcasecmp'.
 */
#if defined(HAVE_STRCASECMP)
#define nasm_stricmp strcasecmp
#elif defined(HAVE_STRICMP)
#define nasm_stricmp stricmp
#else
int pure_func nasm_stricmp(const char *, const char *);
#endif

#if defined(HAVE_STRNCASECMP)
#define nasm_strnicmp strncasecmp
#elif defined(HAVE_STRNICMP)
#define nasm_strnicmp strnicmp
#else
int pure_func nasm_strnicmp(const char *, const char *, size_t);
#endif

int pure_func nasm_memicmp(const char *, const char *, size_t);

#if defined(HAVE_STRSEP)
#define nasm_strsep strsep
#else
char *nasm_strsep(char **stringp, const char *delim);
#endif

#ifndef HAVE_DECL_STRNLEN
size_t pure_func strnlen(const char *, size_t);
#endif

/* This returns the numeric value of a given 'digit'. */
#define numvalue(c)         ((c) >= 'a' ? (c) - 'a' + 10 : (c) >= 'A' ? (c) - 'A' + 10 : (c) - '0')

/*
 * Convert a string into a number, using NASM number rules. Sets
 * `*error' to true if an error occurs, and false otherwise.
 */
int64_t readnum(const char *str, bool *error);

/*
 * Convert a character constant into a number. Sets
 * `*warn' to true if an overflow occurs, and false otherwise.
 * str points to and length covers the middle of the string,
 * without the quotes.
 */
int64_t readstrnum(char *str, int length, bool *warn);

/*
 * seg_alloc: allocate a hitherto unused segment number.
 */
int32_t seg_alloc(void);

/*
 * Add/replace or remove an extension to the end of a filename
 */
const char *filename_set_extension(const char *inname, const char *extension);

/*
 * Utility macros...
 *
 * This is a useful #define which I keep meaning to use more often:
 * the number of elements of a statically defined array.
 */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/*
 * List handling
 *
 *  list_for_each - regular iterator over list
 *  list_for_each_safe - the same but safe against list items removal
 *  list_last - find the last element in a list
 */
#define list_for_each(pos, head)                        \
    for (pos = head; pos; pos = pos->next)
#define list_for_each_safe(pos, n, head)                \
    for (pos = head, n = (pos ? pos->next : NULL); pos; \
        pos = n, n = (n ? n->next : NULL))
#define list_last(pos, head)                            \
    for (pos = head; pos && pos->next; pos = pos->next) \
        ;
#define list_reverse(head, prev, next)                  \
    do {                                                \
        if (!head || !head->next)                       \
            break;                                      \
        prev = NULL;                                    \
        while (head) {                                  \
            next = head->next;                          \
            head->next = prev;                          \
            prev = head;                                \
            head = next;                                \
        }                                               \
        head = prev;                                    \
    } while (0)

/*
 * Power of 2 align helpers
 */
#undef ALIGN_MASK		/* Some BSD flavors define these in system headers */
#undef ALIGN
#define ALIGN_MASK(v, mask)     (((v) + (mask)) & ~(mask))
#define ALIGN(v, a)             ALIGN_MASK(v, (a) - 1)
#define IS_ALIGNED(v, a)        (((v) & ((a) - 1)) == 0)

/*
 * Routines to write littleendian data to a file
 */
#define fwriteint8_t(d,f) putc(d,f)
void fwriteint16_t(uint16_t data, FILE * fp);
void fwriteint32_t(uint32_t data, FILE * fp);
void fwriteint64_t(uint64_t data, FILE * fp);
void fwriteaddr(uint64_t data, int size, FILE * fp);

/*
 * Binary search routine. Returns index into `array' of an entry
 * matching `string', or <0 if no match. `array' is taken to
 * contain `size' elements.
 *
 * bsi() is case sensitive, bsii() is case insensitive.
 */
int bsi(const char *string, const char **array, int size);
int bsii(const char *string, const char **array, int size);

/*
 * These functions are used to keep track of the source code file and name.
 */
void src_init(void);
void src_free(void);
const char *src_set_fname(const char *newname);
const char *src_get_fname(void);
int32_t src_set_linnum(int32_t newline);
int32_t src_get_linnum(void);
/* Can be used when there is no need for the old information */
void src_set(int32_t line, const char *filename);
/*
 * src_get gets both the source file name and line.
 * It is also used if you maintain private status about the source location
 * It return 0 if the information was the same as the last time you
 * checked, -2 if the name changed and (new-old) if just the line changed.
 */
int32_t src_get(int32_t *xline, const char **xname);

char *nasm_skip_spaces(const char *p);
char *nasm_skip_word(const char *p);
char *nasm_zap_spaces_fwd(char *p);
char *nasm_zap_spaces_rev(char *p);
char *nasm_trim_spaces(char *p);
char *nasm_get_word(char *p, char **tail);
char *nasm_opt_val(char *p, char **opt, char **val);

/*
 * Converts a relative pathname rel_path into an absolute path name.
 *
 * The buffer returned must be freed by the caller
 */
char * safe_alloc nasm_realpath(const char *rel_path);

/*
 * Path-splitting and merging functions
 */
char * safe_alloc nasm_dirname(const char *path);
char * safe_alloc nasm_basename(const char *path);
char * safe_alloc nasm_catfile(const char *dir, const char *path);

const char * pure_func prefix_name(int);

/*
 * Wrappers around fopen()... for future change to a dedicated structure
 */
enum file_flags {
    NF_BINARY	= 0x00000000,   /* Binary file (default) */
    NF_TEXT	= 0x00000001,   /* Text file */
    NF_NONFATAL = 0x00000000,   /* Don't die on open failure (default) */
    NF_FATAL    = 0x00000002,   /* Die on open failure */
    NF_FORMAP   = 0x00000004    /* Intended to use nasm_map_file() */
};

FILE *nasm_open_read(const char *filename, enum file_flags flags);
FILE *nasm_open_write(const char *filename, enum file_flags flags);

/* Probe for existence of a file */
bool nasm_file_exists(const char *filename);

#define ZERO_BUF_SIZE 65536     /* Default value */
#if defined(BUFSIZ) && (BUFSIZ > ZERO_BUF_SIZE)
# undef ZERO_BUF_SIZE
# define ZERO_BUF_SIZE BUFSIZ
#endif
extern const uint8_t zero_buffer[ZERO_BUF_SIZE];

/* Missing fseeko/ftello */
#ifndef HAVE_FSEEKO
# undef off_t                   /* Just in case it is a macro */
# ifdef HAVE__FSEEKI64
#  define fseeko _fseeki64
#  define ftello _ftelli64
#  define off_t  int64_t
# else
#  define fseeko fseek
#  define ftello ftell
#  define off_t  long
# endif
#endif

const void *nasm_map_file(FILE *fp, off_t start, off_t len);
void nasm_unmap_file(const void *p, size_t len);
off_t nasm_file_size(FILE *f);
off_t nasm_file_size_by_path(const char *pathname);
bool nasm_file_time(time_t *t, const char *pathname);
void fwritezero(off_t bytes, FILE *fp);

static inline bool const_func overflow_general(int64_t value, int bytes)
{
    int sbit;
    int64_t vmax, vmin;

    if (bytes >= 8)
        return false;

    sbit = (bytes << 3) - 1;
    vmax =  ((int64_t)2 << sbit) - 1;
    vmin = -((int64_t)2 << sbit);

    return value < vmin || value > vmax;
}

static inline bool const_func overflow_signed(int64_t value, int bytes)
{
    int sbit;
    int64_t vmax, vmin;

    if (bytes >= 8)
        return false;

    sbit = (bytes << 3) - 1;
    vmax =  ((int64_t)1 << sbit) - 1;
    vmin = -((int64_t)1 << sbit);

    return value < vmin || value > vmax;
}

static inline bool const_func overflow_unsigned(int64_t value, int bytes)
{
    int sbit;
    int64_t vmax, vmin;

    if (bytes >= 8)
        return false;

    sbit = (bytes << 3) - 1;
    vmax = ((int64_t)2 << sbit) - 1;
    vmin = 0;

    return value < vmin || value > vmax;
}

static inline int64_t const_func signed_bits(int64_t value, int bits)
{
    if (bits < 64) {
        value &= ((int64_t)1 << bits) - 1;
        if (value & (int64_t)1 << (bits - 1))
            value |= (int64_t)((uint64_t)-1 << bits);
    }
    return value;
}

/* check if value is power of 2 */
#define is_power2(v)   ((v) && ((v) & ((v) - 1)) == 0)

#endif
