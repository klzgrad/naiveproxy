/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2025 The NASM Authors - All Rights Reserved */

/*
 * preproc.h  header file for preproc.c
 */

#ifndef NASM_PREPROC_H
#define NASM_PREPROC_H

#include "nasmlib.h"
#include "pptok.h"

extern const char * const pp_directives[];
extern const uint8_t pp_directives_len[];

enum preproc_token pp_token_hash(const char *token);
enum preproc_token pp_tasm_token_hash(const char *token);

/* Opens an include file or input file. This uses the include path. */
FILE *pp_input_fopen(const char *filename, enum file_flags mode);

#endif
