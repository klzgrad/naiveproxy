/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2018 The NASM Authors - All Rights Reserved */

#ifndef NASMLIB_ALLOC_H
#define NASMLIB_ALLOC_H

#include "compiler.h"

fatal_func nasm_alloc_failed(void);

static inline void * pure_func validate_ptr(void *p)
{
    if (unlikely(!p))
        nasm_alloc_failed();
    return p;
}

extern size_t _nasm_last_string_size;

#endif /* NASMLIB_ALLOC_H */
