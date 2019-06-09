/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2016 The NASM Authors - All Rights Reserved
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
 * srcfile.c - keep track of the current position in the input stream
 */

#include "compiler.h"

#include <string.h>
#include <inttypes.h>

#include "nasmlib.h"
#include "hashtbl.h"

static const char *file_name = NULL;
static int32_t line_number = 0;

static struct hash_table filename_hash;

void src_init(void)
{
    hash_init(&filename_hash, HASH_MEDIUM);
}

void src_free(void)
{
    hash_free_all(&filename_hash, false);
}

/*
 * Set the current filename, returning the old one.  The input
 * filename is duplicated if needed.
 */
const char *src_set_fname(const char *newname)
{
    struct hash_insert hi;
    const char *oldname;
    void **dp;

    if (newname) {
        dp = hash_find(&filename_hash, newname, &hi);
        if (dp) {
            newname = (const char *)(*dp);
        } else {
            newname = nasm_strdup(newname);
            hash_add(&hi, newname, (void *)newname);
        }
    }

    oldname = file_name;
    file_name = newname;
    return oldname;
}

int32_t src_set_linnum(int32_t newline)
{
    int32_t oldline = line_number;
    line_number = newline;
    return oldline;
}

void src_set(int32_t line, const char *fname)
{
    src_set_fname(fname);
    src_set_linnum(line);
}

const char *src_get_fname(void)
{
    return file_name;
}

int32_t src_get_linnum(void)
{
    return line_number;
}

int32_t src_get(int32_t *xline, const char **xname)
{
    const char *xn = *xname;
    int32_t xl = *xline;

    *xline = line_number;
    *xname = file_name;

    /* XXX: Is the strcmp() really needed here? */
    if (!file_name || !xn || (xn != file_name && strcmp(xn, file_name)))
        return -2;
    else
        return line_number - xl;
}
