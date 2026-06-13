/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2017 The NASM Authors - All Rights Reserved */

/*
 * exprdump.c
 *
 * Debugging code to dump the contents of an expression vector to stdout
 */

#include "nasm.h"

static const char *expr_type(int32_t type)
{
    static char seg_str[64];

    switch (type) {
    case 0:
        return "null";
    case EXPR_UNKNOWN:
        return "unknown";
    case EXPR_SIMPLE:
        return "simple";
    case EXPR_WRT:
        return "wrt";
    case EXPR_RDSAE:
        return "sae";
    default:
        break;
    }

    if (type >= EXPR_REG_START && type <= EXPR_REG_END) {
        return nasm_reg_names[type - EXPR_REG_START];
    } else if (type >= EXPR_SEGBASE) {
        snprintf(seg_str, sizeof seg_str, "%sseg %lu",
                 (type - EXPR_SEGBASE) == location.segment ? "this " : "",
                 (unsigned long)(type - EXPR_SEGBASE));
        return seg_str;
    } else {
        return "ERR";
    }
}

void dump_expr(const expr *e)
{
    printf("[");
    for (; e->type; e++)
        printf("<%s(%"PRId32"),%"PRId64">",
               expr_type(e->type), e->type, e->value);
    printf("]\n");
}
