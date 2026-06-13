/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2009 The NASM Authors - All Rights Reserved */

#ifndef NASM_QUOTE_H
#define NASM_QUOTE_H

#include "compiler.h"

char *nasm_quote(const char *str, size_t *len);
char *nasm_quote_cstr(const char *str, size_t *len);
size_t nasm_unquote_anystr(char *str, char **endptr,
                           uint32_t badctl, char qstart);
size_t nasm_unquote(char *str, char **endptr);
size_t nasm_unquote_cstr(char *str, char **endptr);
char *nasm_skip_string(const char *str);

/* Arguments used with nasm_quote_anystr() */

/*
 * These are the only control characters when we produce a C string:
 * BEL BS TAB ESC
 */
#define OKCTL ((1U << '\a') | (1U << '\b') | (1U << '\t') | (1U << 27))
#define BADCTL (~(uint32_t)OKCTL)

/* Initial quotation mark */
#define STR_C    '\"'
#define STR_NASM '`'

#endif /* NASM_QUOTE_H */
