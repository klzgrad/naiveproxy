/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2016 The NASM Authors - All Rights Reserved */

/*
 * rdstrnum.c
 *
 * This converts a NASM string to an integer, used when a string
 * is used in an integer constant context.  This is a binary conversion,
 * not a conversion from a numeric constant in text form.
 */

#include "compiler.h"
#include "nasmlib.h"
#include "nasm.h"

int64_t readstrnum(char *str, int length, bool *warn)
{
    int64_t charconst = 0;
    int i;

    *warn = false;
    if (length > 8) {
        *warn = true;
        length = 8;
    }

    for (i = 0; i < length; i++)
        charconst += (uint64_t)((uint8_t)(*str++)) << (i*8);

    return charconst;
}
