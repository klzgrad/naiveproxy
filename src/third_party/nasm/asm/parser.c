/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2018 The NASM Authors - All Rights Reserved
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
 * parser.c   source line parser for the Netwide Assembler
 */

#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>

#include "nasm.h"
#include "insns.h"
#include "nasmlib.h"
#include "error.h"
#include "stdscan.h"
#include "eval.h"
#include "parser.h"
#include "float.h"
#include "assemble.h"
#include "tables.h"


static int is_comma_next(void);

static struct tokenval tokval;

static int prefix_slot(int prefix)
{
    switch (prefix) {
    case P_WAIT:
        return PPS_WAIT;
    case R_CS:
    case R_DS:
    case R_SS:
    case R_ES:
    case R_FS:
    case R_GS:
        return PPS_SEG;
    case P_LOCK:
        return PPS_LOCK;
    case P_REP:
    case P_REPE:
    case P_REPZ:
    case P_REPNE:
    case P_REPNZ:
    case P_XACQUIRE:
    case P_XRELEASE:
    case P_BND:
    case P_NOBND:
        return PPS_REP;
    case P_O16:
    case P_O32:
    case P_O64:
    case P_OSP:
        return PPS_OSIZE;
    case P_A16:
    case P_A32:
    case P_A64:
    case P_ASP:
        return PPS_ASIZE;
    case P_EVEX:
    case P_VEX3:
    case P_VEX2:
        return PPS_VEX;
    default:
        nasm_panic("Invalid value %d passed to prefix_slot()", prefix);
        return -1;
    }
}

static void process_size_override(insn *result, operand *op)
{
    if (tasm_compatible_mode) {
        switch (tokval.t_integer) {
            /* For TASM compatibility a size override inside the
             * brackets changes the size of the operand, not the
             * address type of the operand as it does in standard
             * NASM syntax. Hence:
             *
             *  mov     eax,[DWORD val]
             *
             * is valid syntax in TASM compatibility mode. Note that
             * you lose the ability to override the default address
             * type for the instruction, but we never use anything
             * but 32-bit flat model addressing in our code.
             */
        case S_BYTE:
            op->type |= BITS8;
            break;
        case S_WORD:
            op->type |= BITS16;
            break;
        case S_DWORD:
        case S_LONG:
            op->type |= BITS32;
            break;
        case S_QWORD:
            op->type |= BITS64;
            break;
        case S_TWORD:
            op->type |= BITS80;
            break;
        case S_OWORD:
            op->type |= BITS128;
            break;
        default:
            nasm_error(ERR_NONFATAL,
                       "invalid operand size specification");
            break;
        }
    } else {
        /* Standard NASM compatible syntax */
        switch (tokval.t_integer) {
        case S_NOSPLIT:
            op->eaflags |= EAF_TIMESTWO;
            break;
        case S_REL:
            op->eaflags |= EAF_REL;
            break;
        case S_ABS:
            op->eaflags |= EAF_ABS;
            break;
        case S_BYTE:
            op->disp_size = 8;
            op->eaflags |= EAF_BYTEOFFS;
            break;
        case P_A16:
        case P_A32:
        case P_A64:
            if (result->prefixes[PPS_ASIZE] &&
                result->prefixes[PPS_ASIZE] != tokval.t_integer)
                nasm_error(ERR_NONFATAL,
                           "conflicting address size specifications");
            else
                result->prefixes[PPS_ASIZE] = tokval.t_integer;
            break;
        case S_WORD:
            op->disp_size = 16;
            op->eaflags |= EAF_WORDOFFS;
            break;
        case S_DWORD:
        case S_LONG:
            op->disp_size = 32;
            op->eaflags |= EAF_WORDOFFS;
            break;
        case S_QWORD:
            op->disp_size = 64;
            op->eaflags |= EAF_WORDOFFS;
            break;
        default:
            nasm_error(ERR_NONFATAL, "invalid size specification in"
                       " effective address");
            break;
        }
    }
}

/*
 * Brace decorators are are parsed here.  opmask and zeroing
 * decorators can be placed in any order.  e.g. zmm1 {k2}{z} or zmm2
 * {z}{k3} decorator(s) are placed at the end of an operand.
 */
static bool parse_braces(decoflags_t *decoflags)
{
    int i, j;

    i = tokval.t_type;

    while (true) {
        switch (i) {
        case TOKEN_OPMASK:
            if (*decoflags & OPMASK_MASK) {
                nasm_error(ERR_NONFATAL,
                           "opmask k%"PRIu64" is already set",
                           *decoflags & OPMASK_MASK);
                *decoflags &= ~OPMASK_MASK;
            }
            *decoflags |= VAL_OPMASK(nasm_regvals[tokval.t_integer]);
            break;
        case TOKEN_DECORATOR:
            j = tokval.t_integer;
            switch (j) {
            case BRC_Z:
                *decoflags |= Z_MASK;
                break;
            case BRC_1TO2:
            case BRC_1TO4:
            case BRC_1TO8:
            case BRC_1TO16:
                *decoflags |= BRDCAST_MASK | VAL_BRNUM(j - BRC_1TO2);
                break;
            default:
                nasm_error(ERR_NONFATAL,
                           "{%s} is not an expected decorator",
                           tokval.t_charptr);
                break;
            }
            break;
        case ',':
        case TOKEN_EOS:
            return false;
        default:
            nasm_error(ERR_NONFATAL,
                       "only a series of valid decorators expected");
            return true;
        }
        i = stdscan(NULL, &tokval);
    }
}

static int parse_mref(operand *op, const expr *e)
{
    int b, i, s;        /* basereg, indexreg, scale */
    int64_t o;          /* offset */

    b = i = -1;
    o = s = 0;
    op->segment = op->wrt = NO_SEG;

    if (e->type && e->type <= EXPR_REG_END) {   /* this bit's a register */
        bool is_gpr = is_class(REG_GPR,nasm_reg_flags[e->type]);

        if (is_gpr && e->value == 1)
            b = e->type;	/* It can be basereg */
        else			/* No, it has to be indexreg */
            i = e->type, s = e->value;
        e++;
    }
    if (e->type && e->type <= EXPR_REG_END) {   /* it's a 2nd register */
        bool is_gpr = is_class(REG_GPR,nasm_reg_flags[e->type]);

        if (b != -1)    /* If the first was the base, ... */
            i = e->type, s = e->value;  /* second has to be indexreg */

        else if (!is_gpr || e->value != 1) {
            /* If both want to be index */
            nasm_error(ERR_NONFATAL,
                       "invalid effective address: two index registers");
            return -1;
        } else
            b = e->type;
        e++;
    }

    if (e->type) {                     /* is there an offset? */
        if (e->type <= EXPR_REG_END) {  /* in fact, is there an error? */
            nasm_error(ERR_NONFATAL,
                       "invalid effective address: impossible register");
            return -1;
        } else {
            if (e->type == EXPR_UNKNOWN) {
                op->opflags |= OPFLAG_UNKNOWN;
                o = 0;  /* doesn't matter what */
                while (e->type)
                    e++;        /* go to the end of the line */
            } else {
                if (e->type == EXPR_SIMPLE) {
                    o = e->value;
                    e++;
                }
                if (e->type == EXPR_WRT) {
                    op->wrt = e->value;
                    e++;
                }
                /*
                 * Look for a segment base type.
                 */
                for (; e->type; e++) {
                    if (!e->value)
                        continue;

                    if (e->type <= EXPR_REG_END) {
                        nasm_error(ERR_NONFATAL,
                                   "invalid effective address: too many registers");
                        return -1;
                    } else if (e->type < EXPR_SEGBASE) {
                        nasm_error(ERR_NONFATAL,
                                   "invalid effective address: bad subexpression type");
                        return -1;
                    } else if (e->value == 1) {
                        if (op->segment != NO_SEG) {
                            nasm_error(ERR_NONFATAL,
                                       "invalid effective address: multiple base segments");
                            return -1;
                        }
                        op->segment = e->type - EXPR_SEGBASE;
                    } else if (e->value == -1 &&
                               e->type == location.segment + EXPR_SEGBASE &&
                               !(op->opflags & OPFLAG_RELATIVE)) {
                        op->opflags |= OPFLAG_RELATIVE;
                    } else {
                        nasm_error(ERR_NONFATAL,
                                   "invalid effective address: impossible segment base multiplier");
                        return -1;
                    }
                }
            }
        }
    }

    nasm_assert(!e->type);      /* We should be at the end */

    op->basereg = b;
    op->indexreg = i;
    op->scale = s;
    op->offset = o;
    return 0;
}

static void mref_set_optype(operand *op)
{
    int b = op->basereg;
    int i = op->indexreg;
    int s = op->scale;

    /* It is memory, but it can match any r/m operand */
    op->type |= MEMORY_ANY;

    if (b == -1 && (i == -1 || s == 0)) {
        int is_rel = globalbits == 64 &&
            !(op->eaflags & EAF_ABS) &&
            ((globalrel &&
              !(op->eaflags & EAF_FSGS)) ||
             (op->eaflags & EAF_REL));

        op->type |= is_rel ? IP_REL : MEM_OFFS;
    }

    if (i != -1) {
        opflags_t iclass = nasm_reg_flags[i];

        if (is_class(XMMREG,iclass))
            op->type |= XMEM;
        else if (is_class(YMMREG,iclass))
            op->type |= YMEM;
        else if (is_class(ZMMREG,iclass))
            op->type |= ZMEM;
    }
}

/*
 * Convert an expression vector returned from evaluate() into an
 * extop structure.  Return zero on success.
 */
static int value_to_extop(expr * vect, extop *eop, int32_t myseg)
{
    eop->type = EOT_DB_NUMBER;
    eop->offset = 0;
    eop->segment = eop->wrt = NO_SEG;
    eop->relative = false;

    for (; vect->type; vect++) {
        if (!vect->value)       /* zero term, safe to ignore */
            continue;

        if (vect->type <= EXPR_REG_END) /* false if a register is present */
            return -1;

        if (vect->type == EXPR_UNKNOWN) /* something we can't resolve yet */
            return 0;

        if (vect->type == EXPR_SIMPLE) {
            /* Simple number expression */
            eop->offset += vect->value;
            continue;
        }
        if (eop->wrt == NO_SEG && !eop->relative && vect->type == EXPR_WRT) {
            /* WRT term */
            eop->wrt = vect->value;
            continue;
        }

        if (!eop->relative &&
            vect->type == EXPR_SEGBASE + myseg && vect->value == -1) {
            /* Expression of the form: foo - $ */
            eop->relative = true;
            continue;
        }

        if (eop->segment == NO_SEG && vect->type >= EXPR_SEGBASE &&
            vect->value == 1) {
            eop->segment = vect->type - EXPR_SEGBASE;
            continue;
        }

        /* Otherwise, badness */
        return -1;
    }

    /* We got to the end and it was all okay */
    return 0;
}

insn *parse_line(int pass, char *buffer, insn *result)
{
    bool insn_is_label = false;
    struct eval_hints hints;
    int opnum;
    int critical;
    bool first;
    bool recover;
    int i;

    nasm_static_assert(P_none == 0);

restart_parse:
    first               = true;
    result->forw_ref    = false;

    stdscan_reset();
    stdscan_set(buffer);
    i = stdscan(NULL, &tokval);

    memset(result->prefixes, P_none, sizeof(result->prefixes));
    result->times       = 1;    /* No TIMES either yet */
    result->label       = NULL; /* Assume no label */
    result->eops        = NULL; /* must do this, whatever happens */
    result->operands    = 0;    /* must initialize this */
    result->evex_rm     = 0;    /* Ensure EVEX rounding mode is reset */
    result->evex_brerop = -1;   /* Reset EVEX broadcasting/ER op position */

    /* Ignore blank lines */
    if (i == TOKEN_EOS)
        goto fail;

    if (i != TOKEN_ID       &&
        i != TOKEN_INSN     &&
        i != TOKEN_PREFIX   &&
        (i != TOKEN_REG || !IS_SREG(tokval.t_integer))) {
        nasm_error(ERR_NONFATAL,
                   "label or instruction expected at start of line");
        goto fail;
    }

    if (i == TOKEN_ID || (insn_is_label && i == TOKEN_INSN)) {
        /* there's a label here */
        first = false;
        result->label = tokval.t_charptr;
        i = stdscan(NULL, &tokval);
        if (i == ':') {         /* skip over the optional colon */
            i = stdscan(NULL, &tokval);
        } else if (i == 0) {
            nasm_error(ERR_WARNING | ERR_WARN_OL | ERR_PASS1,
                  "label alone on a line without a colon might be in error");
        }
        if (i != TOKEN_INSN || tokval.t_integer != I_EQU) {
            /*
             * FIXME: location.segment could be NO_SEG, in which case
             * it is possible we should be passing 'absolute.segment'. Look into this.
             * Work out whether that is *really* what we should be doing.
             * Generally fix things. I think this is right as it is, but
             * am still not certain.
             */
            define_label(result->label,
                         in_absolute ? absolute.segment : location.segment,
                         location.offset, true);
        }
    }

    /* Just a label here */
    if (i == TOKEN_EOS)
        goto fail;

    while (i == TOKEN_PREFIX ||
           (i == TOKEN_REG && IS_SREG(tokval.t_integer))) {
        first = false;

        /*
         * Handle special case: the TIMES prefix.
         */
        if (i == TOKEN_PREFIX && tokval.t_integer == P_TIMES) {
            expr *value;

            i = stdscan(NULL, &tokval);
            value = evaluate(stdscan, NULL, &tokval, NULL, pass0, NULL);
            i = tokval.t_type;
            if (!value)                  /* Error in evaluator */
                goto fail;
            if (!is_simple(value)) {
                nasm_error(ERR_NONFATAL,
                      "non-constant argument supplied to TIMES");
                result->times = 1L;
            } else {
                result->times = value->value;
                if (value->value < 0) {
                    nasm_error(ERR_NONFATAL|ERR_PASS2, "TIMES value %"PRId64" is negative", value->value);
                    result->times = 0;
                }
            }
        } else {
            int slot = prefix_slot(tokval.t_integer);
            if (result->prefixes[slot]) {
               if (result->prefixes[slot] == tokval.t_integer)
                    nasm_error(ERR_WARNING | ERR_PASS1,
                               "instruction has redundant prefixes");
               else
                    nasm_error(ERR_NONFATAL,
                               "instruction has conflicting prefixes");
            }
            result->prefixes[slot] = tokval.t_integer;
            i = stdscan(NULL, &tokval);
        }
    }

    if (i != TOKEN_INSN) {
        int j;
        enum prefixes pfx;

        for (j = 0; j < MAXPREFIX; j++) {
            if ((pfx = result->prefixes[j]) != P_none)
                break;
        }

        if (i == 0 && pfx != P_none) {
            /*
             * Instruction prefixes are present, but no actual
             * instruction. This is allowed: at this point we
             * invent a notional instruction of RESB 0.
             */
            result->opcode          = I_RESB;
            result->operands        = 1;
            nasm_zero(result->oprs);
            result->oprs[0].type    = IMMEDIATE;
            result->oprs[0].offset  = 0L;
            result->oprs[0].segment = result->oprs[0].wrt = NO_SEG;
            return result;
        } else {
            nasm_error(ERR_NONFATAL, "parser: instruction expected");
            goto fail;
        }
    }

    result->opcode = tokval.t_integer;
    result->condition = tokval.t_inttwo;

    /*
     * INCBIN cannot be satisfied with incorrectly
     * evaluated operands, since the correct values _must_ be known
     * on the first pass. Hence, even in pass one, we set the
     * `critical' flag on calling evaluate(), so that it will bomb
     * out on undefined symbols.
     */
    if (result->opcode == I_INCBIN) {
        critical = (pass0 < 2 ? 1 : 2);

    } else
        critical = (pass == 2 ? 2 : 0);

    if (opcode_is_db(result->opcode) || result->opcode == I_INCBIN) {
        extop *eop, **tail = &result->eops, **fixptr;
        int oper_num = 0;
        int32_t sign;

        result->eops_float = false;

        /*
         * Begin to read the DB/DW/DD/DQ/DT/DO/DY/DZ/INCBIN operands.
         */
        while (1) {
            i = stdscan(NULL, &tokval);
            if (i == TOKEN_EOS)
                break;
            else if (first && i == ':') {
                insn_is_label = true;
                goto restart_parse;
            }
            first = false;
            fixptr = tail;
            eop = *tail = nasm_malloc(sizeof(extop));
            tail = &eop->next;
            eop->next = NULL;
            eop->type = EOT_NOTHING;
            oper_num++;
            sign = +1;

            /*
             * is_comma_next() here is to distinguish this from
             * a string used as part of an expression...
             */
            if (i == TOKEN_STR && is_comma_next()) {
                eop->type       = EOT_DB_STRING;
                eop->stringval  = tokval.t_charptr;
                eop->stringlen  = tokval.t_inttwo;
                i = stdscan(NULL, &tokval);     /* eat the comma */
            } else if (i == TOKEN_STRFUNC) {
                bool parens = false;
                const char *funcname = tokval.t_charptr;
                enum strfunc func = tokval.t_integer;
                i = stdscan(NULL, &tokval);
                if (i == '(') {
                    parens = true;
                    i = stdscan(NULL, &tokval);
                }
                if (i != TOKEN_STR) {
                    nasm_error(ERR_NONFATAL,
                               "%s must be followed by a string constant",
                               funcname);
                        eop->type = EOT_NOTHING;
                } else {
                    eop->type = EOT_DB_STRING_FREE;
                    eop->stringlen =
                        string_transform(tokval.t_charptr, tokval.t_inttwo,
                                         &eop->stringval, func);
                    if (eop->stringlen == (size_t)-1) {
                        nasm_error(ERR_NONFATAL, "invalid string for transform");
                        eop->type = EOT_NOTHING;
                    }
                }
                if (parens && i && i != ')') {
                    i = stdscan(NULL, &tokval);
                    if (i != ')') {
                        nasm_error(ERR_NONFATAL, "unterminated %s function",
                                   funcname);
                    }
                }
                if (i && i != ',')
                    i = stdscan(NULL, &tokval);
            } else if (i == '-' || i == '+') {
                char *save = stdscan_get();
                int token = i;
                sign = (i == '-') ? -1 : 1;
                i = stdscan(NULL, &tokval);
                if (i != TOKEN_FLOAT) {
                    stdscan_set(save);
                    i = tokval.t_type = token;
                    goto is_expression;
                } else {
                    goto is_float;
                }
            } else if (i == TOKEN_FLOAT) {
is_float:
                eop->type = EOT_DB_STRING;
                result->eops_float = true;

                eop->stringlen = db_bytes(result->opcode);
                if (eop->stringlen > 16) {
                    nasm_error(ERR_NONFATAL, "floating-point constant"
                               " encountered in DY or DZ instruction");
                    eop->stringlen = 0;
                } else if (eop->stringlen < 1) {
                    nasm_error(ERR_NONFATAL, "floating-point constant"
                               " encountered in unknown instruction");
                    /*
                     * fix suggested by Pedro Gimeno... original line was:
                     * eop->type = EOT_NOTHING;
                     */
                    eop->stringlen = 0;
                }

                eop = nasm_realloc(eop, sizeof(extop) + eop->stringlen);
                tail = &eop->next;
                *fixptr = eop;
                eop->stringval = (char *)eop + sizeof(extop);
                if (!eop->stringlen ||
                    !float_const(tokval.t_charptr, sign,
                                 (uint8_t *)eop->stringval, eop->stringlen))
                    eop->type = EOT_NOTHING;
                i = stdscan(NULL, &tokval); /* eat the comma */
            } else {
                /* anything else, assume it is an expression */
                expr *value;

is_expression:
                value = evaluate(stdscan, NULL, &tokval, NULL,
                                 critical, NULL);
                i = tokval.t_type;
                if (!value)                  /* Error in evaluator */
                    goto fail;
                if (value_to_extop(value, eop, location.segment)) {
                    nasm_error(ERR_NONFATAL,
                               "operand %d: expression is not simple or relocatable",
                               oper_num);
                }
            }

            /*
             * We're about to call stdscan(), which will eat the
             * comma that we're currently sitting on between
             * arguments. However, we'd better check first that it
             * _is_ a comma.
             */
            if (i == TOKEN_EOS) /* also could be EOL */
                break;
            if (i != ',') {
                nasm_error(ERR_NONFATAL, "comma expected after operand %d",
                           oper_num);
                goto fail;
            }
        }

        if (result->opcode == I_INCBIN) {
            /*
             * Correct syntax for INCBIN is that there should be
             * one string operand, followed by one or two numeric
             * operands.
             */
            if (!result->eops || result->eops->type != EOT_DB_STRING)
                nasm_error(ERR_NONFATAL, "`incbin' expects a file name");
            else if (result->eops->next &&
                     result->eops->next->type != EOT_DB_NUMBER)
                nasm_error(ERR_NONFATAL, "`incbin': second parameter is"
                           " non-numeric");
            else if (result->eops->next && result->eops->next->next &&
                     result->eops->next->next->type != EOT_DB_NUMBER)
                nasm_error(ERR_NONFATAL, "`incbin': third parameter is"
                           " non-numeric");
            else if (result->eops->next && result->eops->next->next &&
                     result->eops->next->next->next)
                nasm_error(ERR_NONFATAL,
                           "`incbin': more than three parameters");
            else
                return result;
            /*
             * If we reach here, one of the above errors happened.
             * Throw the instruction away.
             */
            goto fail;
        } else /* DB ... */ if (oper_num == 0)
            nasm_error(ERR_WARNING | ERR_PASS1,
                  "no operand for data declaration");
        else
            result->operands = oper_num;

        return result;
    }

    /*
     * Now we begin to parse the operands. There may be up to four
     * of these, separated by commas, and terminated by a zero token.
     */

    for (opnum = 0; opnum < MAX_OPERANDS; opnum++) {
        operand *op = &result->oprs[opnum];
        expr *value;            /* used most of the time */
        bool mref;              /* is this going to be a memory ref? */
        bool bracket;           /* is it a [] mref, or a & mref? */
        bool mib;               /* compound (mib) mref? */
        int setsize = 0;
        decoflags_t brace_flags = 0;    /* flags for decorators in braces */

        op->disp_size = 0;    /* have to zero this whatever */
        op->eaflags   = 0;    /* and this */
        op->opflags   = 0;
        op->decoflags = 0;

        i = stdscan(NULL, &tokval);
        if (i == TOKEN_EOS)
            break;              /* end of operands: get out of here */
        else if (first && i == ':') {
            insn_is_label = true;
            goto restart_parse;
        }
        first = false;
        op->type = 0; /* so far, no override */
        while (i == TOKEN_SPECIAL) {    /* size specifiers */
            switch (tokval.t_integer) {
            case S_BYTE:
                if (!setsize)   /* we want to use only the first */
                    op->type |= BITS8;
                setsize = 1;
                break;
            case S_WORD:
                if (!setsize)
                    op->type |= BITS16;
                setsize = 1;
                break;
            case S_DWORD:
            case S_LONG:
                if (!setsize)
                    op->type |= BITS32;
                setsize = 1;
                break;
            case S_QWORD:
                if (!setsize)
                    op->type |= BITS64;
                setsize = 1;
                break;
            case S_TWORD:
                if (!setsize)
                    op->type |= BITS80;
                setsize = 1;
                break;
            case S_OWORD:
                if (!setsize)
                    op->type |= BITS128;
                setsize = 1;
                break;
            case S_YWORD:
                if (!setsize)
                    op->type |= BITS256;
                setsize = 1;
                break;
            case S_ZWORD:
                if (!setsize)
                    op->type |= BITS512;
                setsize = 1;
                break;
            case S_TO:
                op->type |= TO;
                break;
            case S_STRICT:
                op->type |= STRICT;
                break;
            case S_FAR:
                op->type |= FAR;
                break;
            case S_NEAR:
                op->type |= NEAR;
                break;
            case S_SHORT:
                op->type |= SHORT;
                break;
            default:
                nasm_error(ERR_NONFATAL, "invalid operand size specification");
            }
            i = stdscan(NULL, &tokval);
        }

        if (i == '[' || i == '&') {     /* memory reference */
            mref = true;
            bracket = (i == '[');
            i = stdscan(NULL, &tokval); /* then skip the colon */
            while (i == TOKEN_SPECIAL || i == TOKEN_PREFIX) {
                process_size_override(result, op);
                i = stdscan(NULL, &tokval);
            }
            /* when a comma follows an opening bracket - [ , eax*4] */
            if (i == ',') {
                /* treat as if there is a zero displacement virtually */
                tokval.t_type = TOKEN_NUM;
                tokval.t_integer = 0;
                stdscan_set(stdscan_get() - 1);     /* rewind the comma */
            }
        } else {                /* immediate operand, or register */
            mref = false;
            bracket = false;    /* placate optimisers */
        }

        if ((op->type & FAR) && !mref &&
            result->opcode != I_JMP && result->opcode != I_CALL) {
            nasm_error(ERR_NONFATAL, "invalid use of FAR operand specifier");
        }

        value = evaluate(stdscan, NULL, &tokval,
                         &op->opflags, critical, &hints);
        i = tokval.t_type;
        if (op->opflags & OPFLAG_FORWARD) {
            result->forw_ref = true;
        }
        if (!value)                  /* Error in evaluator */
            goto fail;
        if (i == ':' && mref) { /* it was seg:offset */
            /*
             * Process the segment override.
             */
            if (value[1].type   != 0    ||
                value->value    != 1    ||
                !IS_SREG(value->type))
                nasm_error(ERR_NONFATAL, "invalid segment override");
            else if (result->prefixes[PPS_SEG])
                nasm_error(ERR_NONFATAL,
                      "instruction has conflicting segment overrides");
            else {
                result->prefixes[PPS_SEG] = value->type;
                if (IS_FSGS(value->type))
                    op->eaflags |= EAF_FSGS;
            }

            i = stdscan(NULL, &tokval); /* then skip the colon */
            while (i == TOKEN_SPECIAL || i == TOKEN_PREFIX) {
                process_size_override(result, op);
                i = stdscan(NULL, &tokval);
            }
            value = evaluate(stdscan, NULL, &tokval,
                             &op->opflags, critical, &hints);
            i = tokval.t_type;
            if (op->opflags & OPFLAG_FORWARD) {
                result->forw_ref = true;
            }
            /* and get the offset */
            if (!value)                  /* Error in evaluator */
                goto fail;
        }

        mib = false;
        if (mref && bracket && i == ',') {
            /* [seg:base+offset,index*scale] syntax (mib) */

            operand o1, o2;     /* Partial operands */

            if (parse_mref(&o1, value))
                goto fail;

            i = stdscan(NULL, &tokval); /* Eat comma */
            value = evaluate(stdscan, NULL, &tokval, &op->opflags,
                             critical, &hints);
            i = tokval.t_type;
            if (!value)
                goto fail;

            if (parse_mref(&o2, value))
                goto fail;

            if (o2.basereg != -1 && o2.indexreg == -1) {
                o2.indexreg = o2.basereg;
                o2.scale = 1;
                o2.basereg = -1;
            }

            if (o1.indexreg != -1 || o2.basereg != -1 || o2.offset != 0 ||
                o2.segment != NO_SEG || o2.wrt != NO_SEG) {
                nasm_error(ERR_NONFATAL, "invalid mib expression");
                goto fail;
            }

            op->basereg = o1.basereg;
            op->indexreg = o2.indexreg;
            op->scale = o2.scale;
            op->offset = o1.offset;
            op->segment = o1.segment;
            op->wrt = o1.wrt;

            if (op->basereg != -1) {
                op->hintbase = op->basereg;
                op->hinttype = EAH_MAKEBASE;
            } else if (op->indexreg != -1) {
                op->hintbase = op->indexreg;
                op->hinttype = EAH_NOTBASE;
            } else {
                op->hintbase = -1;
                op->hinttype = EAH_NOHINT;
            }

            mib = true;
        }

        recover = false;
        if (mref && bracket) {  /* find ] at the end */
            if (i != ']') {
                nasm_error(ERR_NONFATAL, "parser: expecting ]");
                recover = true;
            } else {            /* we got the required ] */
                i = stdscan(NULL, &tokval);
                if (i == TOKEN_DECORATOR || i == TOKEN_OPMASK) {
                    /* parse opmask (and zeroing) after an operand */
                    recover = parse_braces(&brace_flags);
                    i = tokval.t_type;
                }
                if (i != 0 && i != ',') {
                    nasm_error(ERR_NONFATAL, "comma or end of line expected");
                    recover = true;
                }
            }
        } else {                /* immediate operand */
            if (i != 0 && i != ',' && i != ':' &&
                i != TOKEN_DECORATOR && i != TOKEN_OPMASK) {
                nasm_error(ERR_NONFATAL, "comma, colon, decorator or end of "
                                         "line expected after operand");
                recover = true;
            } else if (i == ':') {
                op->type |= COLON;
            } else if (i == TOKEN_DECORATOR || i == TOKEN_OPMASK) {
                /* parse opmask (and zeroing) after an operand */
                recover = parse_braces(&brace_flags);
            }
        }
        if (recover) {
            do {                /* error recovery */
                i = stdscan(NULL, &tokval);
            } while (i != 0 && i != ',');
        }

        /*
         * now convert the exprs returned from evaluate()
         * into operand descriptions...
         */
        op->decoflags |= brace_flags;

        if (mref) {             /* it's a memory reference */
            /* A mib reference was fully parsed already */
            if (!mib) {
                if (parse_mref(op, value))
                    goto fail;
                op->hintbase = hints.base;
                op->hinttype = hints.type;
            }
            mref_set_optype(op);
        } else {                /* it's not a memory reference */
            if (is_just_unknown(value)) {       /* it's immediate but unknown */
                op->type      |= IMMEDIATE;
                op->opflags   |= OPFLAG_UNKNOWN;
                op->offset    = 0;        /* don't care */
                op->segment   = NO_SEG;   /* don't care again */
                op->wrt       = NO_SEG;   /* still don't care */

                if(optimizing.level >= 0 && !(op->type & STRICT)) {
                    /* Be optimistic */
                    op->type |=
                        UNITY | SBYTEWORD | SBYTEDWORD | UDWORD | SDWORD;
                }
            } else if (is_reloc(value)) {       /* it's immediate */
                uint64_t n = reloc_value(value);

                op->type      |= IMMEDIATE;
                op->offset    = n;
                op->segment   = reloc_seg(value);
                op->wrt       = reloc_wrt(value);
                op->opflags   |= is_self_relative(value) ? OPFLAG_RELATIVE : 0;

                if (is_simple(value)) {
                    if (n == 1)
                        op->type |= UNITY;
                    if (optimizing.level >= 0 && !(op->type & STRICT)) {
                        if ((uint32_t) (n + 128) <= 255)
                            op->type |= SBYTEDWORD;
                        if ((uint16_t) (n + 128) <= 255)
                            op->type |= SBYTEWORD;
                        if (n <= UINT64_C(0xFFFFFFFF))
                            op->type |= UDWORD;
                        if (n + UINT64_C(0x80000000) <= UINT64_C(0xFFFFFFFF))
                            op->type |= SDWORD;
                    }
                }
            } else if (value->type == EXPR_RDSAE) {
                /*
                 * it's not an operand but a rounding or SAE decorator.
                 * put the decorator information in the (opflag_t) type field
                 * of previous operand.
                 */
                opnum--; op--;
                switch (value->value) {
                case BRC_RN:
                case BRC_RU:
                case BRC_RD:
                case BRC_RZ:
                case BRC_SAE:
                    op->decoflags |= (value->value == BRC_SAE ? SAE : ER);
                    result->evex_rm = value->value;
                    break;
                default:
                    nasm_error(ERR_NONFATAL, "invalid decorator");
                    break;
                }
            } else {            /* it's a register */
                opflags_t rs;
                uint64_t regset_size = 0;

                if (value->type >= EXPR_SIMPLE || value->value != 1) {
                    nasm_error(ERR_NONFATAL, "invalid operand type");
                    goto fail;
                }

                /*
                 * We do not allow any kind of expression, except for
                 * reg+value in which case it is a register set.
                 */
                for (i = 1; value[i].type; i++) {
                    if (!value[i].value)
                        continue;

                    switch (value[i].type) {
                    case EXPR_SIMPLE:
                        if (!regset_size) {
                            regset_size = value[i].value + 1;
                            break;
                        }
                        /* fallthrough */
                    default:
                        nasm_error(ERR_NONFATAL, "invalid operand type");
                        goto fail;
                    }
                }

                if ((regset_size & (regset_size - 1)) ||
                    regset_size >= (UINT64_C(1) << REGSET_BITS)) {
                    nasm_error(ERR_NONFATAL | ERR_PASS2,
                               "invalid register set size");
                    regset_size = 0;
                }

                /* clear overrides, except TO which applies to FPU regs */
                if (op->type & ~TO) {
                    /*
                     * we want to produce a warning iff the specified size
                     * is different from the register size
                     */
                    rs = op->type & SIZE_MASK;
                } else {
                    rs = 0;
                }

                /*
                 * Make sure we're not out of nasm_reg_flags, still
                 * probably this should be fixed when we're defining
                 * the label.
                 *
                 * An easy trigger is
                 *
                 *      e equ 0x80000000:0
                 *      pshufw word e-0
                 *
                 */
                if (value->type < EXPR_REG_START ||
                    value->type > EXPR_REG_END) {
                        nasm_error(ERR_NONFATAL, "invalid operand type");
                        goto fail;
                }

                op->type      &= TO;
                op->type      |= REGISTER;
                op->type      |= nasm_reg_flags[value->type];
                op->type      |= (regset_size >> 1) << REGSET_SHIFT;
                op->decoflags |= brace_flags;
                op->basereg   = value->type;

                if (rs && (op->type & SIZE_MASK) != rs)
                    nasm_error(ERR_WARNING | ERR_PASS1,
                          "register size specification ignored");
            }
        }

        /* remember the position of operand having broadcasting/ER mode */
        if (op->decoflags & (BRDCAST_MASK | ER | SAE))
            result->evex_brerop = opnum;
    }

    result->operands = opnum; /* set operand count */

    /* clear remaining operands */
    while (opnum < MAX_OPERANDS)
        result->oprs[opnum++].type = 0;

    /*
     * Transform RESW, RESD, RESQ, REST, RESO, RESY, RESZ into RESB.
     */
    if (opcode_is_resb(result->opcode)) {
        result->oprs[0].offset *= resb_bytes(result->opcode);
        result->oprs[0].offset *= result->times;
        result->times = 1;
        result->opcode = I_RESB;
    }

    return result;

fail:
    result->opcode = I_none;
    return result;
}

static int is_comma_next(void)
{
    struct tokenval tv;
    char *p;
    int i;

    p = stdscan_get();
    i = stdscan(NULL, &tv);
    stdscan_set(p);

    return (i == ',' || i == ';' || !i);
}

void cleanup_insn(insn * i)
{
    extop *e;

    while ((e = i->eops)) {
        i->eops = e->next;
        if (e->type == EOT_DB_STRING_FREE)
            nasm_free(e->stringval);
        nasm_free(e);
    }
}
