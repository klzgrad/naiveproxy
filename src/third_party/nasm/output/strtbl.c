/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2017 The NASM Authors - All Rights Reserved
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
 * Common string table handling
 *
 * A number of output formats use a "string table"; a container for
 * a number of strings which may be reused at will.  This implements
 * a string table which eliminates duplicates and returns the index
 * into the string table when queried.
 */

#include "compiler.h"

#include "nasm.h"
#include "nasmlib.h"
#include "error.h"
#include "strtbl.h"

struct strtbl_entry {
    size_t index;
    size_t bytes;
    char str[1];
};

void strtbl_init(struct nasm_strtbl *tbl)
{
    tbl->size = 0;
    hash_init(&tbl->hash, HASH_LARGE);
    strtbl_add(tbl, "");       /* Index 0 is always an empty string */
}

void strtbl_free(struct nasm_strtbl *tbl)
{
    hash_free_all(&tbl->hash, false);
}

size_t strtbl_add(struct nasm_strtbl *tbl, const char *str)
{
    void **sep;
    struct strtbl_entry *se;
    struct hash_insert hi;

    sep = hash_find(&tbl->hash, str, &hi);
    if (sep) {
        se = *sep;
    } else {
        size_t bytes = strlen(str) + 1;

        se = nasm_malloc(sizeof(struct strtbl_entry)-1+bytes);
        se->index = tbl->size;
        tbl->size += bytes;
        se->bytes = bytes;
        memcpy(se->str, str, bytes);

        hash_add(&hi, se->str, se);
    }

    return se->index;
}

size_t strtbl_find(struct nasm_strtbl *tbl, const char *str)
{
    void **sep;
    struct strtbl_entry *se;

    sep = hash_find(&tbl->hash, str, NULL);
    if (sep) {
        se = *sep;
        return se->index;
    } else {
        return STRTBL_NONE;
    }
}

/* This create a linearized buffer containing the actual string table */
void *strtbl_generate(const struct nasm_strtbl *tbl)
{
    char *buf = nasm_malloc(strtbl_size(tbl));
    struct hash_tbl_node *iter = NULL;
    struct strtbl_entry *se;

    while ((se = hash_iterate(&tbl->hash, &iter, NULL)))
        memcpy(buf + se->index, se->str, se->bytes);

    return buf;
}
