/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2020 The NASM Authors - All Rights Reserved */

#include "ver.h"
#include "version.h"

/* This is printed when entering nasm -v */
const char nasm_version[] = NASM_VER;
const char nasm_compile_options[] = ""
#ifdef DEBUG
    " with -DDEBUG"
#endif
    ;

bool reproducible;              /* Reproducible output */

/* These are used by some backends. For a reproducible build,
 * these cannot contain version numbers.
 */
static const char * const _nasm_comment[2] =
{
    "The Netwide Assembler " NASM_VER,
    "The Netwide Assembler"
};

static const char * const _nasm_signature[2] = {
    "NASM " NASM_VER,
    "NASM"
};

const char * pure_func nasm_comment(void)
{
    return _nasm_comment[reproducible];
}

size_t pure_func nasm_comment_len(void)
{
    return strlen(nasm_comment());
}

const char * pure_func nasm_signature(void)
{
    return _nasm_signature[reproducible];
}

size_t pure_func nasm_signature_len(void)
{
    return strlen(nasm_signature());
}
