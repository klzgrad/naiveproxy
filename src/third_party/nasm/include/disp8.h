/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2024 The NASM Authors - All Rights Reserved */

/*
 * disp8.h   header file for disp8.c
 */

#ifndef NASM_DISP8_H
#define NASM_DISP8_H

#include "nasm.h"

/*
 * Find shift value for compressed displacement (disp8 << shift)
 */
static inline unsigned int get_disp8_shift(const insn *ins)
{
    bool evex_b;
    unsigned int  evex_w;
    unsigned int  vectlen;
    enum ttypes   tuple = ins->evex_tuple;

    if (likely(!tuple))
        return 0;

    evex_b  = !!(ins->evex & EVEX_B);
    evex_w  = !!(ins->evex & EVEX_W);
    /* XXX: consider RC/SAE here?! */
    vectlen = getfield(EVEX_LL, ins->evex);

    switch (tuple) {
        /* Full, half vector unless broadcast */
    case FV:
        return evex_b ? 2 + evex_w : vectlen + 4;
    case HV:
        return evex_b ? 2 + evex_w : vectlen + 3;

        /* Full vector length */
    case FVM:
        return vectlen + 4;

        /* Fixed tuple lengths */
    case T1S8:
        return 0;
    case T1S16:
        return 1;
    case T1F32:
        return 2;
    case T1F64:
        return 3;
    case M128:
        return 4;

        /* One scalar */
    case T1S:
        return 2 + evex_w;

        /* 2, 4, 8 32/64-bit elements */
    case T2:
        return 3 + evex_w;
    case T4:
        return 4 + evex_w;
    case T8:
        return 5 + evex_w;

        /* Half, quarter, eigth mem */
    case HVM:
        return vectlen + 3;
    case QVM:
        return vectlen + 2;
    case OVM:
        return vectlen + 1;

        /* MOVDDUP */
    case DUP:
        return vectlen + 4;

    default:
        return 0;
    }
}

#endif  /* NASM_DISP8_H */
