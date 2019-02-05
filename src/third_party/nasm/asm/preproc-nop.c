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
 * This is a null preprocessor which just copies lines from input
 * to output. It's used when someone explicitly requests that NASM
 * not preprocess their source file.
 */

#include "compiler.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <time.h>

#include "nasm.h"
#include "nasmlib.h"
#include "error.h"
#include "preproc.h"
#include "listing.h"

#define BUF_DELTA 512

static FILE *nop_fp;
static int32_t nop_lineinc;

static void nop_init(void)
{
    /* Nothing to do */
}

static void nop_reset(const char *file, int pass, StrList *deplist)
{
    src_set(0, file);
    nop_lineinc = 1;
    nop_fp = nasm_open_read(file, NF_TEXT);

    if (!nop_fp)
	nasm_fatal_fl(ERR_NOFILE, "unable to open input file `%s'", file);
    (void)pass;                 /* placate compilers */

    strlist_add_string(deplist, file);
}

static char *nop_getline(void)
{
    char *buffer, *p, *q;
    int bufsize;

    bufsize = BUF_DELTA;
    buffer = nasm_malloc(BUF_DELTA);
    src_set_linnum(src_get_linnum() + nop_lineinc);

    while (1) {                 /* Loop to handle %line */

        p = buffer;
        while (1) {             /* Loop to handle long lines */
            q = fgets(p, bufsize - (p - buffer), nop_fp);
            if (!q)
                break;
            p += strlen(p);
            if (p > buffer && p[-1] == '\n')
                break;
            if (p - buffer > bufsize - 10) {
                int offset;
                offset = p - buffer;
                bufsize += BUF_DELTA;
                buffer = nasm_realloc(buffer, bufsize);
                p = buffer + offset;
            }
        }

        if (!q && p == buffer) {
            nasm_free(buffer);
            return NULL;
        }

        /*
         * Play safe: remove CRs, LFs and any spurious ^Zs, if any of
         * them are present at the end of the line.
         */
        buffer[strcspn(buffer, "\r\n\032")] = '\0';

        if (!nasm_strnicmp(buffer, "%line", 5)) {
            int32_t ln;
            int li;
            char *nm = nasm_malloc(strlen(buffer));
            if (sscanf(buffer + 5, "%"PRId32"+%d %s", &ln, &li, nm) == 3) {
                src_set(ln, nm);
                nop_lineinc = li;
                nasm_free(nm);
                continue;
            }
            nasm_free(nm);
        }
        break;
    }

    lfmt->line(LIST_READ, buffer);

    return buffer;
}

static void nop_cleanup(int pass)
{
    (void)pass;                     /* placate GCC */
    if (nop_fp) {
        fclose(nop_fp);
        nop_fp = NULL;
    }
}

static void nop_extra_stdmac(macros_t *macros)
{
    (void)macros;
}

static void nop_pre_define(char *definition)
{
    (void)definition;
}

static void nop_pre_undefine(char *definition)
{
    (void)definition;
}

static void nop_pre_include(char *fname)
{
    (void)fname;
}

static void nop_pre_command(const char *what, char *string)
{
    (void)what;
    (void)string;
}

static void nop_include_path(const char *path)
{
    (void)path;
}

static void nop_error_list_macros(int severity)
{
    (void)severity;
}

const struct preproc_ops preproc_nop = {
    nop_init,
    nop_reset,
    nop_getline,
    nop_cleanup,
    nop_extra_stdmac,
    nop_pre_define,
    nop_pre_undefine,
    nop_pre_include,
    nop_pre_command,
    nop_include_path,
    nop_error_list_macros,
};
