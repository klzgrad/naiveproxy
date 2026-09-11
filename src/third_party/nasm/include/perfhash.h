/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2017 The NASM Authors - All Rights Reserved */

#ifndef PERFHASH_H
#define PERFHASH_H 1

#include "compiler.h"
#include "nasmlib.h"            /* For invalid_enum_str() */

struct perfect_hash {
    uint64_t crcinit;
    uint32_t hashmask;
    uint32_t tbllen;
    int tbloffs;
    int errval;
    const int16_t *hashvals;
    const char * const *strings;
};

int pure_func perfhash_find(const struct perfect_hash *, const char *);

#endif /* PERFHASH_H */
