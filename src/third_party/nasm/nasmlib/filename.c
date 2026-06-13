/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2017 The NASM Authors - All Rights Reserved */

/*
 * nasmlib.c	library routines for the Netwide Assembler
 */

#include "compiler.h"
#include "nasmlib.h"
#include "error.h"

/*
 * Add/modify a filename extension, assumed to be a period-delimited
 * field at the very end of the filename.  Returns a newly allocated
 * string buffer.
 */
const char *filename_set_extension(const char *inname, const char *extension)
{
    const char *q = inname;
    char *p;
    size_t elen = strlen(extension);
    size_t baselen;

    q = strrchrnul(inname, '.');   /* find extension or end of string */
    baselen = q - inname;

    p = nasm_malloc(baselen + elen + 1);

    memcpy(p, inname, baselen);
    memcpy(p+baselen, extension, elen+1);

    return p;
}
