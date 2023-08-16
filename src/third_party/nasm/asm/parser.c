/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2020 The NASM Authors - All Rights Reserved
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

#include "nctype.h"

#include "nasm.h"
#include "insns.h"
#include "nasmlib.h"
#include "error.h"
#include "stdscan.h"
#include "eval.h"
#include "parser.h"
#include "floats.h"
#include "assemble.h"
#include "tables.h"


static int end_expression_next(void);

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
            nasm_nonfatal("invalid operand size specification");
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
                nasm_nonfatal("conflicting address size specifications");
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
            nasm_nonfatal("invalid size specification in"
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
                nasm_nonfatal("opmask k%"PRIu64" is already set",
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
                nasm_nonfatal("{%s} is not an expected decorator",
                              tokval.t_charptr);
                break;
            }
            break;
        case ',':
        case TOKEN_EOS:
            return false;
        default:
            nasm_nonfatal("only a series of valid decorators expected");
            return true;
        }
        i = stdscan(NULL, &tokval);
    }
}

static inline unused_func
const expr *next_expr(const expr *e, const expr **next_list)
{
    e++;
    if (!e->type) {
        if (next_list) {
            e = *next_list;
            *next_list = NULL;
        } else {
            e = NULL;
        }
    }
    return e;
}

static inline void init_operand(operand *op)
{
    memset(op, 0, sizeof *op);

    op->basereg  = -1;
    op->indexreg = -1;
    op->segment  = NO_SEG;
    op->wrt      = NO_SEG;
}

static int parse_mref(operand *op, const expr *e)
{
    int b, i, s;        /* basereg, indexreg, scale */
    int64_t o;          /* offset */

    b = op->basereg;
    i = op->indexreg;
    s = op->scale;
    o = op->offset;

    for (; e->type; e++) {
        if (e->type <= EXPR_REG_END) {
            bool is_gpr = is_class(REG_GPR,nasm_reg_flags[e->type]);

            if (is_gpr && e->value == 1 && b == -1) {
                /* It can be basereg */
                b = e->type;
            } else if (i == -1) {
                /* Must be index register */
                i = e->type;
                s = e->value;
            } else {
                if (b == -1)
                    nasm_nonfatal("invalid effective address: two index registers");
                else if (!is_gpr)
                    nasm_nonfatal("invalid effective address: impossible register");
                else
                    nasm_nonfatal("invalid effective address: too many registers");
                return -1;
            }
        } else if (e->type == EXPR_UNKNOWN) {
            op->opflags |= OPFLAG_UNKNOWN;
        } else if (e->type == EXPR_SIMPLE) {
            o += e->value;
        } else if  (e->type == EXPR_WRT) {
            op->wrt = e->value;
        } else if (e->type >= EXPR_SEGBASE) {
            if (e->value == 1) {
                if (op->segment != NO_SEG) {
                    nasm_nonfatal("invalid effective address: multiple base segments");
                    return -1;
                }
                op->segment = e->type - EXPR_SEGBASE;
            } else if (e->value == -1 &&
                       e->type == location.segment + EXPR_SEGBASE &&
                       !(op->opflags & OPFLAG_RELATIVE)) {
                op->opflags |= OPFLAG_RELATIVE;
            } else {
                nasm_nonfatal("invalid effective address: impossible segment base multiplier");
                return -1;
            }
        } else {
            nasm_nonfatal("invalid effective address: bad subexpression type");
            return -1;
        }
   }

    op->basereg  = b;
    op->indexreg = i;
    op->scale    = s;
    op->offset   = o;
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
 * extop structure.  Return zero on success.  Note that the eop
 * already has dup and elem set, so we can't clear it here.
 */
static int value_to_extop(expr *vect, extop *eop, int32_t myseg)
{
    eop->type = EOT_DB_NUMBER;
    eop->val.num.offset = 0;
    eop->val.num.segment = eop->val.num.wrt = NO_SEG;
    eop->val.num.relative = false;

    for (; vect->type; vect++) {
        if (!vect->value)       /* zero term, safe to ignore */
            continue;

        if (vect->type <= EXPR_REG_END) /* false if a register is present */
            return -1;

        if (vect->type == EXPR_UNKNOWN) /* something we can't resolve yet */
            return 0;

        if (vect->type == EXPR_SIMPLE) {
            /* Simple number expression */
            eop->val.num.offset += vect->value;
            continue;
        }
        if (eop->val.num.wrt == NO_SEG && !eop->val.num.relative &&
            vect->type == EXPR_WRT) {
            /* WRT term */
            eop->val.num.wrt = vect->value;
            continue;
        }

        if (!eop->val.num.relative &&
            vect->type == EXPR_SEGBASE + myseg && vect->value == -1) {
            /* Expression of the form: foo - $ */
            eop->val.num.relative = true;
            continue;
        }

        if (eop->val.num.segment == NO_SEG &&
            vect->type >= EXPR_SEGBASE && vect->value == 1) {
            eop->val.num.segment = vect->type - EXPR_SEGBASE;
            continue;
        }

        /* Otherwise, badness */
        return -1;
    }

    /* We got to the end and it was all okay */
    return 0;
}

/*
 * Parse an extended expression, used by db et al. "elem" is the element
 * size; initially comes from the specific opcode (e.g. db == 1) but
 * can be overridden.
 */
static int parse_eops(extop **result, bool critical, int elem)
{
    extop *eop = NULL, *prev = NULL;
    extop **tail = result;
    int sign;
    int i = tokval.t_type;
    int oper_num = 0;
    bool do_subexpr = false;

    *tail = NULL;

    /* End of string is obvious; ) ends a sub-expression list e.g. DUP */
    for (i = tokval.t_type; i != TOKEN_EOS; i = stdscan(NULL, &tokval)) {
        char endparen = ')';   /* Is a right paren the end of list? */

        if (i == ')')
            break;

        if (!eop) {
            nasm_new(eop);
            eop->dup  = 1;
            eop->elem = elem;
            do_subexpr = false;
        }
        sign = +1;

        /*
         * end_expression_next() here is to distinguish this from
         * a string used as part of an expression...
         */
        if (i == TOKEN_QMARK) {
            eop->type = EOT_DB_RESERVE;
        } else if (do_subexpr && i == '(') {
            extop *subexpr;

            stdscan(NULL, &tokval); /* Skip paren */
            if (parse_eops(&eop->val.subexpr, critical, eop->elem) < 0)
                goto fail;

            subexpr = eop->val.subexpr;
            if (!subexpr) {
                /* Subexpression is empty */
                eop->type = EOT_NOTHING;
            } else if (!subexpr->next) {
                /* Subexpression is a single element, flatten */
                eop->val   = subexpr->val;
                eop->type  = subexpr->type;
                eop->dup  *= subexpr->dup;
                nasm_free(subexpr);
            } else {
                eop->type = EOT_EXTOP;
            }

            /* We should have ended on a closing paren */
            if (tokval.t_type != ')') {
                nasm_nonfatal("expected `)' after subexpression, got `%s'",
                              i == TOKEN_EOS ?
                              "end of line" : tokval.t_charptr);
                goto fail;
            }
            endparen = 0;       /* This time the paren is not the end */
        } else if (i == '%') {
            /* %(expression_list) */
            do_subexpr = true;
            continue;
        } else if (i == TOKEN_SIZE) {
            /* Element size override */
            eop->elem = tokval.t_inttwo;
            do_subexpr = true;
            continue;
        } else if (i == TOKEN_STR && end_expression_next()) {
            eop->type            = EOT_DB_STRING;
            eop->val.string.data = tokval.t_charptr;
            eop->val.string.len  = tokval.t_inttwo;
        } else if (i == TOKEN_STRFUNC) {
            bool parens = false;
            const char *funcname = tokval.t_charptr;
            enum strfunc func = tokval.t_integer;

            i = stdscan(NULL, &tokval);
            if (i == '(') {
                parens = true;
                endparen = 0;
                i = stdscan(NULL, &tokval);
            }
            if (i != TOKEN_STR) {
                nasm_nonfatal("%s must be followed by a string constant",
                              funcname);
                eop->type = EOT_NOTHING;
            } else {
                eop->type = EOT_DB_STRING_FREE;
                eop->val.string.len =
                    string_transform(tokval.t_charptr, tokval.t_inttwo,
                                     &eop->val.string.data, func);
                if (eop->val.string.len == (size_t)-1) {
                    nasm_nonfatal("invalid input string to %s", funcname);
                    eop->type = EOT_NOTHING;
                }
            }
            if (parens && i && i != ')') {
                i = stdscan(NULL, &tokval);
                if (i != ')')
                    nasm_nonfatal("unterminated %s function", funcname);
            }
        } else if (i == '-' || i == '+') {
            char *save = stdscan_get();
            struct tokenval tmptok;

            sign = (i == '-') ? -1 : 1;
            if (stdscan(NULL, &tmptok) != TOKEN_FLOAT) {
                stdscan_set(save);
                goto is_expression;
            } else {
                tokval = tmptok;
                goto is_float;
            }
        } else if (i == TOKEN_FLOAT) {
        is_float:
            eop->type = EOT_DB_FLOAT;

            if (eop->elem > 16) {
                nasm_nonfatal("no %d-bit floating-point format supported",
                              eop->elem << 3);
                eop->val.string.len = 0;
            } else if (eop->elem < 1) {
                nasm_nonfatal("floating-point constant"
                              " encountered in unknown instruction");
                /*
                 * fix suggested by Pedro Gimeno... original line was:
                 * eop->type = EOT_NOTHING;
                 */
                eop->val.string.len = 0;
            } else {
                eop->val.string.len = eop->elem;

                eop = nasm_realloc(eop, sizeof(extop) + eop->val.string.len);
                eop->val.string.data = (char *)eop + sizeof(extop);
                if (!float_const(tokval.t_charptr, sign,
                                 (uint8_t *)eop->val.string.data,
                                 eop->val.string.len))
                    eop->val.string.len = 0;
            }
            if (!eop->val.string.len)
                eop->type = EOT_NOTHING;
        } else {
            /* anything else, assume it is an expression */
            expr *value;

        is_expression:
            value = evaluate(stdscan, NULL, &tokval, NULL,
                             critical, NULL);
            i = tokval.t_type;
            if (!value)                  /* Error in evaluator */
                goto fail;
            if (tokval.t_flag & TFLAG_DUP) {
                /* Expression followed by DUP */
                if (!is_simple(value)) {
                    nasm_nonfatal("non-constant argument supplied to DUP");
                    goto fail;
                } else if (value->value < 0) {
                    nasm_nonfatal("negative argument supplied to DUP");
                    goto fail;
                }
                eop->dup *= (size_t)value->value;
                do_subexpr = true;
                continue;
            }
            if (value_to_extop(value, eop, location.segment)) {
                nasm_nonfatal("expression is not simple or relocatable");
            }
        }

        if (eop->dup == 0 || eop->type == EOT_NOTHING) {
            nasm_free(eop);
        } else if (eop->type == EOT_DB_RESERVE &&
                   prev && prev->type == EOT_DB_RESERVE &&
                   prev->elem == eop->elem) {
            /* Coalesce multiple EOT_DB_RESERVE */
            prev->dup += eop->dup;
            nasm_free(eop);
        } else {
            /* Add this eop to the end of the chain */
            prev = eop;
            *tail = eop;
            tail = &eop->next;
        }

        oper_num++;
        eop = NULL;             /* Done with this operand */

        /*
         * We're about to call stdscan(), which will eat the
         * comma that we're currently sitting on between
         * arguments. However, we'd better check first that it
         * _is_ a comma.
         */
        if (i == TOKEN_EOS || i == endparen)	/* Already at end? */
            break;
        if (i != ',') {
            i = stdscan(NULL, &tokval);		/* eat the comma or final paren */
            if (i == TOKEN_EOS || i == ')')	/* got end of expression */
                break;
            if (i != ',') {
                nasm_nonfatal("comma expected after operand");
                goto fail;
            }
        }
    }

    return oper_num;

fail:
    if (eop)
        nasm_free(eop);
    return -1;
}

insn *parse_line(char *buffer, insn *result)
{
    bool insn_is_label = false;
    struct eval_hints hints;
    int opnum;
    bool critical;
    bool first;
    bool recover;
    bool far_jmp_ok;
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
        nasm_nonfatal("label or instruction expected at start of line");
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
            /*!
             *!label-orphan [on] labels alone on lines without trailing `:'
             *!=orphan-labels
             *!  warns about source lines which contain no instruction but define
             *!  a label without a trailing colon. This is most likely indicative
             *!  of a typo, but is technically correct NASM syntax (see \k{syntax}.)
             */
            nasm_warn(WARN_LABEL_ORPHAN ,
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
            value = evaluate(stdscan, NULL, &tokval, NULL, pass_stable(), NULL);
            i = tokval.t_type;
            if (!value)                  /* Error in evaluator */
                goto fail;
            if (!is_simple(value)) {
                nasm_nonfatal("non-constant argument supplied to TIMES");
                result->times = 1L;
            } else {
                result->times = value->value;
                if (value->value < 0) {
                    nasm_nonfatalf(ERR_PASS2, "TIMES value %"PRId64" is negative", value->value);
                    result->times = 0;
                }
            }
        } else {
            int slot = prefix_slot(tokval.t_integer);
            if (result->prefixes[slot]) {
               if (result->prefixes[slot] == tokval.t_integer)
                    nasm_warn(WARN_OTHER, "instruction has redundant prefixes");
               else
                    nasm_nonfatal("instruction has conflicting prefixes");
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
            nasm_nonfatal("parser: instruction expected");
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
    critical = pass_final() || (result->opcode == I_INCBIN);

    if (opcode_is_db(result->opcode) || result->opcode == I_INCBIN) {
        int oper_num;

        i = stdscan(NULL, &tokval);

        if (first && i == ':') {
            /* Really a label */
            insn_is_label = true;
            goto restart_parse;
        }
        first = false;
        oper_num = parse_eops(&result->eops, critical, db_bytes(result->opcode));
        if (oper_num < 0)
            goto fail;

        if (result->opcode == I_INCBIN) {
            /*
             * Correct syntax for INCBIN is that there should be
             * one string operand, followed by one or two numeric
             * operands.
             */
            if (!result->eops || result->eops->type != EOT_DB_STRING)
                nasm_nonfatal("`incbin' expects a file name");
            else if (result->eops->next &&
                     result->eops->next->type != EOT_DB_NUMBER)
                nasm_nonfatal("`incbin': second parameter is"
                              " non-numeric");
            else if (result->eops->next && result->eops->next->next &&
                     result->eops->next->next->type != EOT_DB_NUMBER)
                nasm_nonfatal("`incbin': third parameter is"
                              " non-numeric");
            else if (result->eops->next && result->eops->next->next &&
                     result->eops->next->next->next)
                nasm_nonfatal("`incbin': more than three parameters");
            else
                return result;
            /*
             * If we reach here, one of the above errors happened.
             * Throw the instruction away.
             */
            goto fail;
        } else {
            /* DB et al */
            result->operands = oper_num;
            if (oper_num == 0)
                /*!
                 *!db-empty [on] no operand for data declaration
                 *!  warns about a \c{DB}, \c{DW}, etc declaration
                 *!  with no operands, producing no output.
                 *!  This is permitted, but often indicative of an error.
                 *!  See \k{db}.
                 */
                nasm_warn(WARN_DB_EMPTY, "no operand for data declaration");
        }
        return result;
    }

    /*
     * Now we begin to parse the operands. There may be up to four
     * of these, separated by commas, and terminated by a zero token.
     */
    far_jmp_ok = result->opcode == I_JMP || result->opcode == I_CALL;

    for (opnum = 0; opnum < MAX_OPERANDS; opnum++) {
        operand *op = &result->oprs[opnum];
        expr *value;            /* used most of the time */
        bool mref = false;      /* is this going to be a memory ref? */
        int bracket = 0;        /* is it a [] mref, or a "naked" mref? */
        bool mib;               /* compound (mib) mref? */
        int setsize = 0;
        decoflags_t brace_flags = 0;    /* flags for decorators in braces */

        init_operand(op);

        i = stdscan(NULL, &tokval);
        if (i == TOKEN_EOS)
            break;              /* end of operands: get out of here */
        else if (first && i == ':') {
            insn_is_label = true;
            goto restart_parse;
        }
        first = false;
        op->type = 0; /* so far, no override */
        /* size specifiers */
        while (i == TOKEN_SPECIAL || i == TOKEN_SIZE) {
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
                nasm_nonfatal("invalid operand size specification");
            }
            i = stdscan(NULL, &tokval);
        }

        if (i == '[' || i == TOKEN_MASM_PTR || i == '&') {
            /* memory reference */
            mref = true;
            bracket += (i == '[');
            i = stdscan(NULL, &tokval);
        }

    mref_more:
        if (mref) {
            bool done = false;
            bool nofw = false;

            while (!done) {
                switch (i) {
                case TOKEN_SPECIAL:
                case TOKEN_SIZE:
                case TOKEN_PREFIX:
                    process_size_override(result, op);
                    break;

                case '[':
                    bracket++;
                    break;

                case ',':
                    tokval.t_type = TOKEN_NUM;
                    tokval.t_integer = 0;
                    stdscan_set(stdscan_get() - 1);     /* rewind the comma */
                    done = nofw = true;
                    break;

                case TOKEN_MASM_FLAT:
                    i = stdscan(NULL, &tokval);
                    if (i != ':') {
                        nasm_nonfatal("unknown use of FLAT in MASM emulation");
                        nofw = true;
                    }
                    done = true;
                    break;

                default:
                    done = nofw = true;
                    break;
                }

                if (!nofw)
                    i = stdscan(NULL, &tokval);
            }
        }

        value = evaluate(stdscan, NULL, &tokval,
                         &op->opflags, critical, &hints);
        i = tokval.t_type;
        if (op->opflags & OPFLAG_FORWARD) {
            result->forw_ref = true;
        }
        if (!value)                  /* Error in evaluator */
            goto fail;

        if (i == '[' && !bracket) {
            /* displacement[regs] syntax */
            mref = true;
            parse_mref(op, value); /* Process what we have so far */
            goto mref_more;
        }

        if (i == ':' && (mref || !far_jmp_ok)) {
            /* segment override? */
            mref = true;

            /*
             * Process the segment override.
             */
            if (value[1].type   != 0    ||
                value->value    != 1    ||
                !IS_SREG(value->type))
                nasm_nonfatal("invalid segment override");
            else if (result->prefixes[PPS_SEG])
                nasm_nonfatal("instruction has conflicting segment overrides");
            else {
                result->prefixes[PPS_SEG] = value->type;
                if (IS_FSGS(value->type))
                    op->eaflags |= EAF_FSGS;
            }

            i = stdscan(NULL, &tokval); /* then skip the colon */
            goto mref_more;
        }

        mib = false;
        if (mref && bracket && i == ',') {
            /* [seg:base+offset,index*scale] syntax (mib) */
            operand o2;         /* Index operand */

            if (parse_mref(op, value))
                goto fail;

            i = stdscan(NULL, &tokval); /* Eat comma */
            value = evaluate(stdscan, NULL, &tokval, &op->opflags,
                             critical, &hints);
            i = tokval.t_type;
            if (!value)
                goto fail;

            init_operand(&o2);
            if (parse_mref(&o2, value))
                goto fail;

            if (o2.basereg != -1 && o2.indexreg == -1) {
                o2.indexreg = o2.basereg;
                o2.scale = 1;
                o2.basereg = -1;
            }

            if (op->indexreg != -1 || o2.basereg != -1 || o2.offset != 0 ||
                o2.segment != NO_SEG || o2.wrt != NO_SEG) {
                nasm_nonfatal("invalid mib expression");
                goto fail;
            }

            op->indexreg = o2.indexreg;
            op->scale = o2.scale;

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
        if (mref) {
            if (bracket == 1) {
                if (i == ']') {
                    bracket--;
                    i = stdscan(NULL, &tokval);
                } else {
                    nasm_nonfatal("expecting ] at end of memory operand");
                    recover = true;
                }
            } else if (bracket == 0) {
                /* Do nothing */
            } else if (bracket > 0) {
                nasm_nonfatal("excess brackets in memory operand");
                recover = true;
            } else if (bracket < 0) {
                nasm_nonfatal("unmatched ] in memory operand");
                recover = true;
            }

            if (i == TOKEN_DECORATOR || i == TOKEN_OPMASK) {
                /* parse opmask (and zeroing) after an operand */
                recover = parse_braces(&brace_flags);
                i = tokval.t_type;
            }
            if (!recover && i != 0 && i != ',') {
                nasm_nonfatal("comma, decorator or end of line expected, got %d", i);
                recover = true;
            }
        } else {                /* immediate operand */
            if (i != 0 && i != ',' && i != ':' &&
                i != TOKEN_DECORATOR && i != TOKEN_OPMASK) {
                nasm_nonfatal("comma, colon, decorator or end of "
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
        } else if ((op->type & FAR) && !far_jmp_ok) {
                nasm_nonfatal("invalid use of FAR operand specifier");
                recover = true;
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
                    nasm_nonfatal("invalid decorator");
                    break;
                }
            } else {            /* it's a register */
                opflags_t rs;
                uint64_t regset_size = 0;

                if (value->type >= EXPR_SIMPLE || value->value != 1) {
                    nasm_nonfatal("invalid operand type");
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
                        nasm_nonfatal("invalid operand type");
                        goto fail;
                    }
                }

                if ((regset_size & (regset_size - 1)) ||
                    regset_size >= (UINT64_C(1) << REGSET_BITS)) {
                    nasm_nonfatalf(ERR_PASS2, "invalid register set size");
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
                        nasm_nonfatal("invalid operand type");
                        goto fail;
                }

                op->type      &= TO;
                op->type      |= REGISTER;
                op->type      |= nasm_reg_flags[value->type];
                op->type      |= (regset_size >> 1) << REGSET_SHIFT;
                op->decoflags |= brace_flags;
                op->basereg   = value->type;

                if (rs) {
                    opflags_t opsize = nasm_reg_flags[value->type] & SIZE_MASK;
                    if (!opsize) {
                        op->type |= rs; /* For non-size-specific registers, permit size override */
                    } else if (opsize != rs) {
                        /*!
                         *!regsize [on] register size specification ignored
                         *!
                         *!  warns about a register with implicit size (such as \c{EAX}, which is always 32 bits)
                         *!  been given an explicit size specification which is inconsistent with the size
                         *!  of the named register, e.g. \c{WORD EAX}. \c{DWORD EAX} or \c{WORD AX} are
                         *!  permitted, and do not trigger this warning. Some registers which \e{do not} imply
                         *!  a specific size, such as \c{K0}, may need this specification unless the instruction
                         *!  itself implies the instruction size:
                         *!-
                         *!  \c      KMOVW K0,[foo]          ; Permitted, KMOVW implies 16 bits
                         *!  \c      KMOV  WORD K0,[foo]     ; Permitted, WORD K0 specifies instruction size
                         *!  \c      KMOV  K0,WORD [foo]     ; Permitted, WORD [foo] specifies instruction size
                         *!  \c      KMOV  K0,[foo]          ; Not permitted, instruction size ambiguous
                         */
                        nasm_warn(WARN_REGSIZE, "invalid register size specification ignored");
                    }
                }
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

    return result;

fail:
    result->opcode = I_none;
    return result;
}

static int end_expression_next(void)
{
    struct tokenval tv;
    char *p;
    int i;

    p = stdscan_get();
    i = stdscan(NULL, &tv);
    stdscan_set(p);

    return (i == ',' || i == ';' || i == ')' || !i);
}

static void free_eops(extop *e)
{
    extop *next;

    while (e) {
        next = e->next;
        switch (e->type) {
        case EOT_EXTOP:
            free_eops(e->val.subexpr);
            break;

        case EOT_DB_STRING_FREE:
            nasm_free(e->val.string.data);
            break;

        default:
            break;
        }

        nasm_free(e);
        e = next;
    }
}

void cleanup_insn(insn * i)
{
    free_eops(i->eops);
}
