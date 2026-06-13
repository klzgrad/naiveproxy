/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2018 The NASM Authors - All Rights Reserved */

/*
 * nasmlib.c	library routines for the Netwide Assembler
 */

#include "compiler.h"
#include "nasm.h"
#include "nasmlib.h"
#include "insns.h"

static int32_t next_seg  = 2;

int32_t seg_alloc(void)
{
    int32_t this_seg = next_seg;

    next_seg += 2;
    return this_seg;
}
