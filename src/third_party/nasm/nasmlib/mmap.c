/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2017 The NASM Authors - All Rights Reserved
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

#include "file.h"

/* ----------------------------------------------------------------------- *
 *   Unix-style memory mapping, using mmap().
 * ----------------------------------------------------------------------- */
#if defined(HAVE_FILENO) && defined(HAVE_MMAP)

/*
 * System page size
 */

/* File scope since not all compilers like static data in inline functions */
static size_t nasm_pagemask;

static size_t get_pagemask(void)
{
    size_t ps = 0;

# if defined(HAVE_SYSCONF) && defined(_SC_PAGESIZE)
    ps = sysconf(_SC_PAGESIZE);
# elif defined(HAVE_GETPAGESIZE)
    ps = getpagesize();
# elif defined(PAGE_SIZE)
    ps = PAGE_SIZE;
# endif

    nasm_pagemask = ps = is_power2(ps) ? (ps - 1) : 0;
    return ps;
}

static inline size_t pagemask(void)
{
    size_t pm = nasm_pagemask;

    if (unlikely(!pm))
        return get_pagemask();

    return pm;
}

/*
 * Try to map an input file into memory
 */
const void *nasm_map_file(FILE *fp, off_t start, off_t len)
{
    const char *p;
    off_t  astart;              /* Aligned start */
    size_t salign;              /* Amount of start adjustment */
    size_t alen;                /* Aligned length */
    const size_t page_mask = pagemask();

    if (unlikely(!page_mask))
        return NULL;            /* Page size undefined? */

    if (unlikely(!len))
        return NULL;            /* Mapping nothing... */

    if (unlikely(len != (off_t)(size_t)len))
        return NULL;            /* Address space insufficient */

    astart = start & ~(off_t)page_mask;
    salign = start - astart;
    alen = (len + salign + page_mask) & ~page_mask;

    p = mmap(NULL, alen, PROT_READ, MAP_SHARED, fileno(fp), astart);
    return unlikely(p == MAP_FAILED) ? NULL : p + salign;
}

/*
 * Unmap an input file
 */
void nasm_unmap_file(const void *p, size_t len)
{
    const size_t page_mask = pagemask();
    uintptr_t astart;
    size_t salign;
    size_t alen;

    if (unlikely(!page_mask))
        return;

    astart = (uintptr_t)p & ~(uintptr_t)page_mask;
    salign = (uintptr_t)p - astart;
    alen = (len + salign + page_mask) & ~page_mask;

    munmap((void *)astart, alen);
}

/* ----------------------------------------------------------------------- *
 *   No memory map support at all
 *   XXX: Add a section with Windows support
 * ----------------------------------------------------------------------- */
#else

const void *nasm_map_file(FILE *fp, off_t start, off_t len)
{
    (void)fp; (void)start; (void)len;
    return NULL;
}

void nasm_unmap_file(const void *p, size_t len)
{
    (void)p; (void)len;
}

#endif
