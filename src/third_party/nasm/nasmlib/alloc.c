/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2019 The NASM Authors - All Rights Reserved */

/*
 * nasmlib.c	library routines for the Netwide Assembler
 */

#include "compiler.h"
#include "nasmlib.h"
#include "error.h"
#include "alloc.h"

size_t _nasm_last_string_size;

fatal_func nasm_alloc_failed(void)
{
    nasm_critical("out of memory!");
}

void *nasm_malloc(size_t size)
{
    void *p;

again:
    p = malloc(size);

    if (unlikely(!p)) {
        if (!size) {
            size = 1;
            goto again;
        }
        nasm_alloc_failed();
    }
    return p;
}

void *nasm_calloc(size_t nelem, size_t size)
{
    void *p;

again:
    p = calloc(nelem, size);

    if (unlikely(!p)) {
        if (!nelem || !size) {
            nelem = size = 1;
            goto again;
        }
        nasm_alloc_failed();
    }

    return p;
}

void *nasm_zalloc(size_t size)
{
    return nasm_calloc(size, 1);
}

/*
 * Unlike the system realloc, we do *not* allow size == 0 to be
 * the equivalent to free(); we guarantee returning a non-NULL pointer.
 *
 * The check for calling malloc() is theoretically redundant, but be
 * paranoid about the system library...
 */
void *nasm_realloc(void *q, size_t size)
{
    if (unlikely(!size))
        size = 1;
    q = q ? realloc(q, size) : malloc(size);
    return validate_ptr(q);
}

void nasm_free(void *q)
{
    if (q){
        free(q);
	q = NULL;
    }
}

char *nasm_strdup(const char *s)
{
    char *p;
    const size_t size = strlen(s) + 1;

    _nasm_last_string_size = size;
    p = nasm_malloc(size);
    return memcpy(p, s, size);
}

char *nasm_strndup(const char *s, size_t len)
{
    char *p;

    len = strnlen(s, len);
    _nasm_last_string_size = len + 1;
    p = nasm_malloc(len+1);
    p[len] = '\0';
    return memcpy(p, s, len);
}

char *nasm_strcat(const char *one, const char *two)
{
    char *rslt;
    const size_t l1 = strlen(one);
    const size_t s2 = strlen(two) + 1;

    _nasm_last_string_size = l1 + s2;
    rslt = nasm_malloc(l1 + s2);
    memcpy(rslt, one, l1);
    memcpy(rslt + l1, two, s2);
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

    _nasm_last_string_size = s;

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
