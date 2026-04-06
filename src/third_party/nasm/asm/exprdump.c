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
        snprintf(seg_str, sizeof seg_str, "%sseg %d",
                 (type - EXPR_SEGBASE) == location.segment ? "this " : "",
                 type - EXPR_SEGBASE);
        return seg_str;
    } else {
        return "ERR";
    }
}

void dump_expr(const expr *e)
{
    printf("[");
    for (; e->type; e++)
        printf("<%s(%d),%"PRId64">", expr_type(e->type), e->type, e->value);
    printf("]\n");
}
