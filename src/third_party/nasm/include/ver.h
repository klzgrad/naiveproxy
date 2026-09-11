/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2020 The NASM Authors - All Rights Reserved */

/*
 * NASM version strings, defined in ver.c
 */

#ifndef NASM_VER_H
#define NASM_VER_H

#include "compiler.h"

extern const char nasm_version[];
extern const char nasm_compile_options[];

extern bool reproducible;

extern const char * pure_func nasm_comment(void);
extern size_t pure_func nasm_comment_len(void);

extern const char * pure_func nasm_signature(void);
extern size_t pure_func nasm_signature_len(void);

#endif /* NASM_VER_H */
