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
 * nasmlib.c	library routines for the Netwide Assembler
 */

#include "compiler.h"

#include <stdlib.h>

#include "nasmlib.h"
#include "error.h"

static no_return nasm_alloc_failed(void)
{
    nasm_fatal("out of memory");
}

static inline void *validate_ptr(void *p)
{
    if (unlikely(!p))
        nasm_alloc_failed();
    return p;
}

void *nasm_malloc(size_t size)
{
    return validate_ptr(malloc(size));
}

void *nasm_calloc(size_t size, size_t nelem)
{
    return validate_ptr(calloc(size, nelem));
}

void *nasm_zalloc(size_t size)
{
    return validate_ptr(calloc(1, size));
}

void *nasm_realloc(void *q, size_t size)
{
    return validate_ptr(q ? realloc(q, size) : malloc(size));
}

void nasm_free(void *q)
{
    if (q)
        free(q);
}

char *nasm_strdup(const char *s)
{
    char *p;
    size_t size = strlen(s) + 1;

    p = nasm_malloc(size);
    return memcpy(p, s, size);
}

char *nasm_strndup(const char *s, size_t len)
{
    char *p;

    len = strnlen(s, len);
    p = nasm_malloc(len+1);
    p[len] = '\0';
    return memcpy(p, s, len);
}

char *nasm_strcat(const char *one, const char *two)
{
    char *rslt;
    size_t l1 = strlen(one);
    size_t l2 = strlen(two);
    rslt = nasm_malloc(l1 + l2 + 1);
    memcpy(rslt, one, l1);
    memcpy(rslt + l1, two, l2+1);
    return rslt;
}

char *nasm_strcatn(const char *str1, ...)
{
    va_list ap;
    char *rslt;                 /* Output buffer */
    size_t s;                   /* Total buffer size */
    size_t n;                   /* Number of arguments */
    size_t *ltbl;               /* Table of lengths */
    size_t l, *lp;              /* Length for current argument */
    const char *p;              /* Currently examined argument */
    char *q;                    /* Output pointer */

    n = 0;                      /* No strings encountered yet */
    p = str1;
    va_start(ap, str1);
    while (p) {
        n++;
        p = va_arg(ap, const char *);
    }
    va_end(ap);

    ltbl = nasm_malloc(n * sizeof(size_t));

    s = 1;                      /* Space for final NULL */
    p = str1;
    lp = ltbl;
    va_start(ap, str1);
    while (p) {
        *lp++ = l = strlen(p);
        s += l;
        p = va_arg(ap, const char *);
    }
    va_end(ap);

    q = rslt = nasm_malloc(s);

    p = str1;
    lp = ltbl;
    va_start(ap, str1);
    while (p) {
        l = *lp++;
        memcpy(q, p, l);
        q += l;
        p = va_arg(ap, const char *);
    }
    va_end(ap);
    *q = '\0';

    nasm_free(ltbl);

    return rslt;
}
