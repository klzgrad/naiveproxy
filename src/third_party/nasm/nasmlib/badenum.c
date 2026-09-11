/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2017 The NASM Authors - All Rights Reserved */

#include "nasmlib.h"

/* Used to avoid returning NULL to a debug printing function */
const char *invalid_enum_str(int x)
{
    static char buf[64];

    snprintf(buf, sizeof buf, "<invalid %d>", x);
    return buf;
}
