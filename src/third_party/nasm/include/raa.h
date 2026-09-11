/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2009 The NASM Authors - All Rights Reserved */

#ifndef NASM_RAA_H
#define NASM_RAA_H 1

#include "compiler.h"

struct RAA;
typedef uint64_t raaindex;

#define raa_init() NULL
void raa_free(struct RAA *);
int64_t pure_func raa_read(struct RAA *, raaindex);
void * pure_func raa_read_ptr(struct RAA *, raaindex);
struct RAA * never_null raa_write(struct RAA *r, raaindex posn, int64_t value);
struct RAA * never_null raa_write_ptr(struct RAA *r, raaindex posn, void *value);

#endif                          /* NASM_RAA_H */
