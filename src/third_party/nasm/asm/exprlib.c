/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2017 The NASM Authors - All Rights Reserved
 *   See the file AUTHORS included with the NASM distribution for
 *   the specific copyright holders.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following
 *   conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *
 *     THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 *     CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *     INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *     MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *     DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 *     CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *     SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *     NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *     LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *     HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 *     OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 *     EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ----------------------------------------------------------------------- */

/*
 * exprlib.c
 *
 * Library routines to manipulate expression data types.
 */

#include "nasm.h"

/*
 * Return true if the argument is a simple scalar. (Or a far-
 * absolute, which counts.)
 */
bool is_simple(const expr *vect)
{
    while (vect->type && !vect->value)
        vect++;
    if (!vect->type)
        return true;
    if (vect->type != EXPR_SIMPLE)
        return false;
    do {
        vect++;
    } while (vect->type && !vect->value);
    if (vect->type && vect->type < EXPR_SEGBASE + SEG_ABS)
        return false;
    return true;
}

/*
 * Return true if the argument is a simple scalar, _NOT_ a far-
 * absolute.
 */
bool is_really_simple(const expr *vect)
{
    while (vect->type && !vect->value)
        vect++;
    if (!vect->type)
        return true;
    if (vect->type != EXPR_SIMPLE)
        return false;
    do {
        vect++;
    } while (vect->type && !vect->value);
    if (vect->type)
        return false;
    return true;
}

/*
 * Return true if the argument is relocatable (i.e. a simple
 * scalar, plus at most one segment-base, possibly a subtraction
 * of the current segment base, plus possibly a WRT).
 */
bool is_reloc(const expr *vect)
{
    bool has_rel = false;       /* Has a self-segment-subtract */
    bool has_seg = false;       /* Has a segment base */

    for (; vect->type; vect++) {
        if (!vect->value) {
            /* skip value-0 terms */
            continue;
        } else if (vect->type < EXPR_SIMPLE) {
            /* false if a register is present */
            return false;
        } else if (vect->type == EXPR_SIMPLE) {
            /* skip over a pure number term... */
            continue;
        } else if (vect->type == EXPR_WRT) {
            /* skip over a WRT term... */
            continue;
        } else if (vect->type < EXPR_SEGBASE) {
            /* other special type -> problem */
            return false;
        } else if (vect->value == 1) {
            if (has_seg)
                return false;   /* only one segbase allowed */
            has_seg = true;
        } else if (vect->value == -1) {
            if (vect->type != location.segment + EXPR_SEGBASE)
                return false;   /* can only subtract current segment */
            if (has_rel)
                return false;   /* already is relative */
            has_rel = true;
        }
    }

    return true;
}

/*
 * Return true if the argument contains an `unknown' part.
 */
bool is_unknown(const expr *vect)
{
    while (vect->type && vect->type < EXPR_UNKNOWN)
        vect++;
    return (vect->type == EXPR_UNKNOWN);
}

/*
 * Return true if the argument contains nothing but an `unknown'
 * part.
 */
bool is_just_unknown(const expr *vect)
{
    while (vect->type && !vect->value)
        vect++;
    return (vect->type == EXPR_UNKNOWN);
}

/*
 * Return the scalar part of a relocatable vector. (Including
 * simple scalar vectors - those qualify as relocatable.)
 */
int64_t reloc_value(const expr *vect)
{
    while (vect->type && !vect->value)
        vect++;
    if (!vect->type)
        return 0;
    if (vect->type == EXPR_SIMPLE)
        return vect->value;
    else
        return 0;
}

/*
 * Return the segment number of a relocatable vector, or NO_SEG for
 * simple scalars.
 */
int32_t reloc_seg(const expr *vect)
{
    for (; vect->type; vect++) {
        if (vect->type >= EXPR_SEGBASE && vect->value == 1)
            return vect->type - EXPR_SEGBASE;
    }

    return NO_SEG;
}

/*
 * Return the WRT segment number of a relocatable vector, or NO_SEG
 * if no WRT part is present.
 */
int32_t reloc_wrt(const expr *vect)
{
    while (vect->type && vect->type < EXPR_WRT)
        vect++;
    if (vect->type == EXPR_WRT) {
        return vect->value;
    } else
        return NO_SEG;
}

/*
 * Return true if this expression contains a subtraction of the location
 */
bool is_self_relative(const expr *vect)
{
    for (; vect->type; vect++) {
        if (vect->type == location.segment + EXPR_SEGBASE && vect->value == -1)
            return true;
    }

    return false;
}
