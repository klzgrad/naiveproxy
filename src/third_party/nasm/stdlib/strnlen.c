/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2016 The NASM Authors - All Rights Reserved */

#include "compiler.h"

#ifndef HAVE_STRNLEN

size_t strnlen(const char *s, size_t maxlen)
{
    const char *end = memchr(s, 0, maxlen);

    return end ? (size_t)(end - s) : maxlen;
}

#endif
