/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2025 The NASM Authors - All Rights Reserved */

/*
 * macros.h - format of builtin macro data
 */

#ifndef NASM_MACROS_H
#define NASM_MACROS_H

#include "compiler.h"

/* Builtin macro set */
struct builtin_macros {
    unsigned int dsize, zsize;
    const void *zdata;
};
typedef const struct builtin_macros macros_t;

char *uncompress_stdmac(macros_t *sm);

/* --- From standard.mac via macros.pl -> macros.c --- */

extern macros_t nasm_stdmac_tasm;
extern macros_t nasm_stdmac_nasm;
extern macros_t nasm_stdmac_version;

struct use_package {
    const char *package;
    macros_t *macros;
    unsigned int index;
};
extern const struct use_package *nasm_find_use_package(const char *);
extern const unsigned int use_package_count;

#endif
