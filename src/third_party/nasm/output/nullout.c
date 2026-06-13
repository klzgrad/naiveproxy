/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2018 The NASM Authors - All Rights Reserved */

#include "nasm.h"
#include "nasmlib.h"
#include "outlib.h"

enum directive_result
null_directive(enum directive directive, char *value)
{
    (void)directive;
    (void)value;
    return DIRR_UNKNOWN;
}

void null_sectalign(int32_t seg, unsigned int value)
{
    (void)seg;
    (void)value;
}

void null_reset(void)
{
    /* Nothing to do */
}

int32_t null_segbase(int32_t segment)
{
    return segment;
}
