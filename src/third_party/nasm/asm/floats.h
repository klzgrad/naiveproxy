/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2020 The NASM Authors - All Rights Reserved */

/*
 * floats.h   header file for the floating-point constant module of
 *	      the Netwide Assembler
 */

#ifndef NASM_FLOATS_H
#define NASM_FLOATS_H

#include "nasm.h"

enum float_round {
    FLOAT_RC_NEAR,
    FLOAT_RC_ZERO,
    FLOAT_RC_DOWN,
    FLOAT_RC_UP
};

/* Note: enum floatize and FLOAT_ERR are defined in nasm.h */

/* Floating-point format description */
struct ieee_format {
    int bytes;                  /* Total bytes */
    int mantissa;               /* Fractional bits in the mantissa */
    int explicit;               /* Explicit integer */
    int exponent;               /* Bits in the exponent */
    int offset;                 /* Offset into byte array for floatize op */
};
extern const struct ieee_format fp_formats[FLOAT_ERR];

int float_const(const char *str, int s, uint8_t *result, enum floatize ffmt);
enum floatize const_func float_deffmt(int bytes);
int float_option(const char *option);

#endif /* NASM_FLOATS_H */
