/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2025 The NASM Authors - All Rights Reserved */

/*
 * common.c - code common to nasm and ndisasm
 */

#include "compiler.h"
#include "nasm.h"
#include "nasmlib.h"
#include "insns.h"

/*
 * Per-pass global (across segments) state
 */
struct globalopt globl;

/*
 * Name of a register token, if applicable; otherwise NULL
 */
const char *register_name(int token)
{
    if (is_register(token))
        return nasm_reg_names[token - EXPR_REG_START];
    else
        return NULL;
}

/*
 * Common list of prefix names; ideally should be auto-generated
 * from tokens.dat. This MUST match the enum in include/nasm.h.
 */
const char *prefix_name(int token)
{
    static const char * const
        prefix_names[PREFIX_ENUM_LIMIT - PREFIX_ENUM_START] = {
        "a16", "a32", "a64", "asp", "lock", "o16", "o32", "o64", "osp",
        "rep", "repe", "repne", "repnz", "repz", "wait",
        "xacquire", "xrelease", "bnd", "nobnd", "{rex}", "{rex2}",
        "{evex}", "{vex}", "{vex3}", "{vex2}", "{nf}", "{zu}",
        "{pt}", "{pn}"
    };
    const char *name;

    /* A register can also be a prefix */
    name = register_name(token);

    if (!name) {
        const unsigned int prefix = token - PREFIX_ENUM_START;
        if (prefix < ARRAY_SIZE(prefix_names))
            name = prefix_names[prefix];
    }

    return name;
}

/*
 * True for a valid hinting-NOP opcode, after 0F.
 */
bool is_hint_nop(uint64_t op)
{
    if (op >> 16)
        return false;

    if ((op >> 8) == 0x0f)
        op = (uint8_t)op;
    else if (op >> 8)
        return false;

    return ((op & ~7) == 0x18) || (op == 0x0d);
}
