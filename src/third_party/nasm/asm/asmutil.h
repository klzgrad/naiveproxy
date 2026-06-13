/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2025 The NASM Authors - All Rights Reserved */

#ifndef NASM_ASMUTIL_H
#define NASM_ASMUTIL_H

/*
 * Get a boolean option value; can be an expression or a set of special
 * ("yes", "no", "false", "true", ...)
 *
 * If the result is not a valid value, print an error message, leave
 * the option value unchanged, and return NULL.
 *
 * Returns the first character past the boolean expression.
 */
char *get_boolean_option(const char *, bool *);

#endif
