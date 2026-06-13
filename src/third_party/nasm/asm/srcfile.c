/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2020 The NASM Authors - All Rights Reserved */

/*
 * srcfile.c - keep track of the current position in the input stream.
 *
 * This is used for error messages, listing, and debug information. In
 * both cases we also want to understand where inside a non-nolist
 * macro we may be.
 *
 * This hierarchy is a stack that is kept as a doubly-linked list, as
 * we want to traverse it in either top-down order or bottom-up.
 */

#include "compiler.h"


#include "nasmlib.h"
#include "hashtbl.h"
#include "srcfile.h"

struct src_location_stack _src_top;
struct src_location_stack *_src_bottom = &_src_top;
struct src_location_stack *_src_error = &_src_top;

static struct hash_table filename_hash;

void src_init(void)
{
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

    oldname = _src_bottom->l.filename;
    _src_bottom->l.filename = newname;
    return oldname;
}

void src_set(int32_t line, const char *fname)
{
    src_set_fname(fname);
    src_set_linnum(line);
}

void src_macro_push(const void *macro, struct src_location where)
{
    struct src_location_stack *sl;

    nasm_new(sl);
    sl->l = where;
    sl->macro = macro;
    sl->up = _src_bottom;
    _src_bottom->down = sl;
    _src_bottom = sl;
}

void src_macro_pop(void)
{
    struct src_location_stack *sl = _src_bottom;

    _src_bottom = sl->up;
    _src_bottom->down = NULL;

    nasm_free(sl);
}
