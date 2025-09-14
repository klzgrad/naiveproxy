/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2013 The NASM Authors - All Rights Reserved
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
 * disp8.c   : Contains a common logic for EVEX compressed displacement
 */

#include "disp8.h"

/*
 * Find N value for compressed displacement (disp8 * N)
 */
uint8_t get_disp8N(insn *ins)
{
    static const uint8_t fv_n[2][2][VLMAX] = {{{16, 32, 64}, {4, 4, 4}},
                                              {{16, 32, 64}, {8, 8, 8}}};
    static const uint8_t hv_n[2][VLMAX]    =  {{8, 16, 32}, {4, 4, 4}};
    static const uint8_t dup_n[VLMAX]      =   {8, 32, 64};

    bool evex_b           = (ins->evex_p[2] & EVEX_P2B) >> 4;
    enum ttypes   tuple   = ins->evex_tuple;
    enum vectlens vectlen = (ins->evex_p[2] & EVEX_P2LL) >> 5;
    bool evex_w           = (ins->evex_p[1] & EVEX_P1W) >> 7;
    uint8_t n = 0;

    switch(tuple) {
    case FV:
        n = fv_n[evex_w][evex_b][vectlen];
        break;
    case HV:
        n = hv_n[evex_b][vectlen];
        break;

    case FVM:
        /* 16, 32, 64 for VL 128, 256, 512 respectively*/
        n = 1 << (vectlen + 4);
        break;
    case T1S8:  /* N = 1 */
    case T1S16: /* N = 2 */
        n = tuple - T1S8 + 1;
        break;
    case T1S:
        /* N = 4 for 32bit, 8 for 64bit */
        n = evex_w ? 8 : 4;
        break;
    case T1F32:
    case T1F64:
        /* N = 4 for 32bit, 8 for 64bit */
        n = (tuple == T1F32 ? 4 : 8);
        break;
    case T2:
    case T4:
    case T8:
        if (vectlen + 7 <= (evex_w + 5) + (tuple - T2 + 1))
            n = 0;
        else
            n = 1 << (tuple - T2 + evex_w + 3);
        break;
    case HVM:
    case QVM:
    case OVM:
        n = 1 << (OVM - tuple + vectlen + 1);
        break;
    case M128:
        n = 16;
        break;
    case DUP:
        n = dup_n[vectlen];
        break;

    default:
        break;
    }

    return n;
}

/*
 * Check if offset is a multiple of N with corresponding tuple type
 * if Disp8*N is available, compressed displacement is stored in compdisp
 */
bool is_disp8n(operand *input, insn *ins, int8_t *compdisp)
{
    int32_t off           = input->offset;
    uint8_t n;
    int32_t disp8;

    n = get_disp8N(ins);

    if (n && !(off & (n - 1))) {
        disp8 = off / n;
        /* if it fits in Disp8 */
        if (disp8 >= -128 && disp8 <= 127) {
            *compdisp = disp8;
            return true;
        }
    }

    *compdisp = 0;
    return false;
}
