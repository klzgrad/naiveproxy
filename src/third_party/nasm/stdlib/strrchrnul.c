/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2017 The NASM Authors - All Rights Reserved */

#include "compiler.h"

#ifndef HAVE_STRRCHRNUL

char * pure_func strrchrnul(const char *s, int c)
{
    char *p;

    p = strrchr(s, c);
    if (!p)
        p = strchr(s, '\0');

    return p;
}

#endif
