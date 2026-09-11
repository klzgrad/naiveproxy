/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2025 The NASM Authors - All Rights Reserved */

/*
 * This needs to be in a separate file because zlib.h conflicts
 * with opflags.h.
 */
#include "compiler.h"
#include "zlib.h"
#include "macros.h"
#include "nasmlib.h"
#include "error.h"

/*
 * read line from standard macros set,
 * if there no more left -- return NULL
 */
static void *nasm_z_alloc(void *opaque, unsigned int items, unsigned int size)
{
    (void)opaque;
    return nasm_calloc(items, size);
}

static void nasm_z_free(void *opaque, void *ptr)
{
    (void)opaque;
    nasm_free(ptr);
}

char *uncompress_stdmac(macros_t *sm)
{
    z_stream zs;
    void *buf = nasm_malloc(sm->dsize);

    nasm_zero(zs);
    zs.next_in   = (void *)sm->zdata;
    zs.avail_in  = sm->zsize;
    zs.next_out  = buf;
    zs.avail_out = sm->dsize;
    zs.zalloc    = nasm_z_alloc;
    zs.zfree     = nasm_z_free;

    if (inflateInit2(&zs, 0) != Z_OK)
        panic();

    if (inflate(&zs, Z_FINISH) != Z_STREAM_END)
        panic();

    inflateEnd(&zs);
    return buf;
}
