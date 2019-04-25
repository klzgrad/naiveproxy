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
 * eval.c    expression evaluator for the Netwide Assembler
 */

#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>

#include "nasm.h"
#include "nasmlib.h"
#include "ilog2.h"
#include "error.h"
#include "eval.h"
#include "labels.h"
#include "float.h"
#include "assemble.h"

#define TEMPEXPRS_DELTA 128
#define TEMPEXPR_DELTA 8

static scanner scan;            /* Address of scanner routine */

static expr **tempexprs = NULL;
static int ntempexprs;
static int tempexprs_size = 0;

static expr *tempexpr;
static int ntempexpr;
static int tempexpr_size;

static struct tokenval *tokval; /* The current token */
static int i;                   /* The t_type of tokval */

static void *scpriv;
static int *opflags;

static struct eval_hints *hint;
static int64_t deadman;


/*
 * Unimportant cleanup is done to avoid confusing people who are trying
 * to debug real memory leaks
 */
void eval_cleanup(void)
{
    while (ntempexprs)
        nasm_free(tempexprs[--ntempexprs]);
    nasm_free(tempexprs);
}

/*
 * Construct a temporary expression.
 */
static void begintemp(void)
{
    tempexpr = NULL;
    tempexpr_size = ntempexpr = 0;
}

static void addtotemp(int32_t type, int64_t value)
{
    while (ntempexpr >= tempexpr_size) {
        tempexpr_size += TEMPEXPR_DELTA;
        tempexpr = nasm_realloc(tempexpr,
                                tempexpr_size * sizeof(*tempexpr));
    }
    tempexpr[ntempexpr].type = type;
    tempexpr[ntempexpr++].value = value;
}

static expr *finishtemp(void)
{
    addtotemp(0L, 0L);          /* terminate */
    while (ntempexprs >= tempexprs_size) {
        tempexprs_size += TEMPEXPRS_DELTA;
        tempexprs = nasm_realloc(tempexprs,
                                 tempexprs_size * sizeof(*tempexprs));
    }
    return tempexprs[ntempexprs++] = tempexpr;
}

/*
 * Add two vector datatypes. We have some bizarre behaviour on far-
 * absolute segment types: we preserve them during addition _only_
 * if one of the segments is a truly pure scalar.
 */
static expr *add_vectors(expr * p, expr * q)
{
    int preserve;

    preserve = is_really_simple(p) || is_really_simple(q);

    begintemp();

    while (p->type && q->type &&
           p->type < EXPR_SEGBASE + SEG_ABS &&
           q->type < EXPR_SEGBASE + SEG_ABS) {
        int lasttype;

        if (p->type > q->type) {
            addtotemp(q->type, q->value);
            lasttype = q++->type;
        } else if (p->type < q->type) {
            addtotemp(p->type, p->value);
            lasttype = p++->type;
        } else {                /* *p and *q have same type */
            int64_t sum = p->value + q->value;
            if (sum) {
                addtotemp(p->type, sum);
                if (hint)
                    hint->type = EAH_SUMMED;
            }
            lasttype = p->type;
            p++, q++;
        }
        if (lasttype == EXPR_UNKNOWN) {
            return finishtemp();
        }
    }
    while (p->type && (preserve || p->type < EXPR_SEGBASE + SEG_ABS)) {
        addtotemp(p->type, p->value);
        p++;
    }
    while (q->type && (preserve || q->type < EXPR_SEGBASE + SEG_ABS)) {
        addtotemp(q->type, q->value);
        q++;
    }

    return finishtemp();
}

/*
 * Multiply a vector by a scalar. Strip far-absolute segment part
 * if present.
 *
 * Explicit treatment of UNKNOWN is not required in this routine,
 * since it will silently do the Right Thing anyway.
 *
 * If `affect_hints' is set, we also change the hint type to
 * NOTBASE if a MAKEBASE hint points at a register being
 * multiplied. This allows [eax*1+ebx] to hint EBX rather than EAX
 * as the base register.
 */
static expr *scalar_mult(expr * vect, int64_t scalar, int affect_hints)
{
    expr *p = vect;

    while (p->type && p->type < EXPR_SEGBASE + SEG_ABS) {
        p->value = scalar * (p->value);
        if (hint && hint->type == EAH_MAKEBASE &&
            p->type == hint->base && affect_hints)
            hint->type = EAH_NOTBASE;
        p++;
    }
    p->type = 0;

    return vect;
}

static expr *scalarvect(int64_t scalar)
{
    begintemp();
    addtotemp(EXPR_SIMPLE, scalar);
    return finishtemp();
}

static expr *unknown_expr(void)
{
    begintemp();
    addtotemp(EXPR_UNKNOWN, 1L);
    return finishtemp();
}

/*
 * The SEG operator: calculate the segment part of a relocatable
 * value. Return NULL, as usual, if an error occurs. Report the
 * error too.
 */
static expr *segment_part(expr * e)
{
    int32_t seg;

    if (is_unknown(e))
        return unknown_expr();

    if (!is_reloc(e)) {
        nasm_error(ERR_NONFATAL, "cannot apply SEG to a non-relocatable value");
        return NULL;
    }

    seg = reloc_seg(e);
    if (seg == NO_SEG) {
        nasm_error(ERR_NONFATAL, "cannot apply SEG to a non-relocatable value");
        return NULL;
    } else if (seg & SEG_ABS) {
        return scalarvect(seg & ~SEG_ABS);
    } else if (seg & 1) {
        nasm_error(ERR_NONFATAL, "SEG applied to something which"
              " is already a segment base");
        return NULL;
    } else {
        int32_t base = ofmt->segbase(seg + 1);

        begintemp();
        addtotemp((base == NO_SEG ? EXPR_UNKNOWN : EXPR_SEGBASE + base),
                  1L);
        return finishtemp();
    }
}

/*
 * Recursive-descent parser. Called with a single boolean operand,
 * which is true if the evaluation is critical (i.e. unresolved
 * symbols are an error condition). Must update the global `i' to
 * reflect the token after the parsed string. May return NULL.
 *
 * evaluate() should report its own errors: on return it is assumed
 * that if NULL has been returned, the error has already been
 * reported.
 */

/*
 * Grammar parsed is:
 *
 * expr  : bexpr [ WRT expr6 ]
 * bexpr : rexp0 or expr0 depending on relative-mode setting
 * rexp0 : rexp1 [ {||} rexp1...]
 * rexp1 : rexp2 [ {^^} rexp2...]
 * rexp2 : rexp3 [ {&&} rexp3...]
 * rexp3 : expr0 [ {=,==,<>,!=,<,>,<=,>=} expr0 ]
 * expr0 : expr1 [ {|} expr1...]
 * expr1 : expr2 [ {^} expr2...]
 * expr2 : expr3 [ {&} expr3...]
 * expr3 : expr4 [ {<<,>>} expr4...]
 * expr4 : expr5 [ {+,-} expr5...]
 * expr5 : expr6 [ {*,/,%,//,%%} expr6...]
 * expr6 : { ~,+,-,IFUNC,SEG } expr6
 *       | (bexpr)
 *       | symbol
 *       | $
 *       | number
 */

static expr *rexp0(int), *rexp1(int), *rexp2(int), *rexp3(int);

static expr *expr0(int), *expr1(int), *expr2(int), *expr3(int);
static expr *expr4(int), *expr5(int), *expr6(int);

static expr *(*bexpr) (int);

static expr *rexp0(int critical)
{
    expr *e, *f;

    e = rexp1(critical);
    if (!e)
        return NULL;

    while (i == TOKEN_DBL_OR) {
        i = scan(scpriv, tokval);
        f = rexp1(critical);
        if (!f)
            return NULL;
        if (!(is_simple(e) || is_just_unknown(e)) ||
            !(is_simple(f) || is_just_unknown(f))) {
            nasm_error(ERR_NONFATAL, "`|' operator may only be applied to"
                  " scalar values");
        }

        if (is_just_unknown(e) || is_just_unknown(f))
            e = unknown_expr();
        else
            e = scalarvect((int64_t)(reloc_value(e) || reloc_value(f)));
    }
    return e;
}

static expr *rexp1(int critical)
{
    expr *e, *f;

    e = rexp2(critical);
    if (!e)
        return NULL;

    while (i == TOKEN_DBL_XOR) {
        i = scan(scpriv, tokval);
        f = rexp2(critical);
        if (!f)
            return NULL;
        if (!(is_simple(e) || is_just_unknown(e)) ||
            !(is_simple(f) || is_just_unknown(f))) {
            nasm_error(ERR_NONFATAL, "`^' operator may only be applied to"
                  " scalar values");
        }

        if (is_just_unknown(e) || is_just_unknown(f))
            e = unknown_expr();
        else
            e = scalarvect((int64_t)(!reloc_value(e) ^ !reloc_value(f)));
    }
    return e;
}

static expr *rexp2(int critical)
{
    expr *e, *f;

    e = rexp3(critical);
    if (!e)
        return NULL;
    while (i == TOKEN_DBL_AND) {
        i = scan(scpriv, tokval);
        f = rexp3(critical);
        if (!f)
            return NULL;
        if (!(is_simple(e) || is_just_unknown(e)) ||
            !(is_simple(f) || is_just_unknown(f))) {
            nasm_error(ERR_NONFATAL, "`&' operator may only be applied to"
                  " scalar values");
        }
        if (is_just_unknown(e) || is_just_unknown(f))
            e = unknown_expr();
        else
            e = scalarvect((int64_t)(reloc_value(e) && reloc_value(f)));
    }
    return e;
}

static expr *rexp3(int critical)
{
    expr *e, *f;
    int64_t v;

    e = expr0(critical);
    if (!e)
        return NULL;

    while (i == TOKEN_EQ || i == TOKEN_LT || i == TOKEN_GT ||
           i == TOKEN_NE || i == TOKEN_LE || i == TOKEN_GE) {
        int j = i;
        i = scan(scpriv, tokval);
        f = expr0(critical);
        if (!f)
            return NULL;

        e = add_vectors(e, scalar_mult(f, -1L, false));

        switch (j) {
        case TOKEN_EQ:
        case TOKEN_NE:
            if (is_unknown(e))
                v = -1;         /* means unknown */
            else if (!is_really_simple(e) || reloc_value(e) != 0)
                v = (j == TOKEN_NE);    /* unequal, so return true if NE */
            else
                v = (j == TOKEN_EQ);    /* equal, so return true if EQ */
            break;
        default:
            if (is_unknown(e))
                v = -1;         /* means unknown */
            else if (!is_really_simple(e)) {
                nasm_error(ERR_NONFATAL,
                      "`%s': operands differ by a non-scalar",
                      (j == TOKEN_LE ? "<=" : j == TOKEN_LT ? "<" : j ==
                       TOKEN_GE ? ">=" : ">"));
                v = 0;          /* must set it to _something_ */
            } else {
                int64_t vv = reloc_value(e);
                if (vv == 0)
                    v = (j == TOKEN_LE || j == TOKEN_GE);
                else if (vv > 0)
                    v = (j == TOKEN_GE || j == TOKEN_GT);
                else            /* vv < 0 */
                    v = (j == TOKEN_LE || j == TOKEN_LT);
            }
            break;
        }

        if (v == -1)
            e = unknown_expr();
        else
            e = scalarvect(v);
    }
    return e;
}

static expr *expr0(int critical)
{
    expr *e, *f;

    e = expr1(critical);
    if (!e)
        return NULL;

    while (i == '|') {
        i = scan(scpriv, tokval);
        f = expr1(critical);
        if (!f)
            return NULL;
        if (!(is_simple(e) || is_just_unknown(e)) ||
            !(is_simple(f) || is_just_unknown(f))) {
            nasm_error(ERR_NONFATAL, "`|' operator may only be applied to"
                  " scalar values");
        }
        if (is_just_unknown(e) || is_just_unknown(f))
            e = unknown_expr();
        else
            e = scalarvect(reloc_value(e) | reloc_value(f));
    }
    return e;
}

static expr *expr1(int critical)
{
    expr *e, *f;

    e = expr2(critical);
    if (!e)
        return NULL;

    while (i == '^') {
        i = scan(scpriv, tokval);
        f = expr2(critical);
        if (!f)
            return NULL;
        if (!(is_simple(e) || is_just_unknown(e)) ||
            !(is_simple(f) || is_just_unknown(f))) {
            nasm_error(ERR_NONFATAL, "`^' operator may only be applied to"
                  " scalar values");
        }
        if (is_just_unknown(e) || is_just_unknown(f))
            e = unknown_expr();
        else
            e = scalarvect(reloc_value(e) ^ reloc_value(f));
    }
    return e;
}

static expr *expr2(int critical)
{
    expr *e, *f;

    e = expr3(critical);
    if (!e)
        return NULL;

    while (i == '&') {
        i = scan(scpriv, tokval);
        f = expr3(critical);
        if (!f)
            return NULL;
        if (!(is_simple(e) || is_just_unknown(e)) ||
            !(is_simple(f) || is_just_unknown(f))) {
            nasm_error(ERR_NONFATAL, "`&' operator may only be applied to"
                  " scalar values");
        }
        if (is_just_unknown(e) || is_just_unknown(f))
            e = unknown_expr();
        else
            e = scalarvect(reloc_value(e) & reloc_value(f));
    }
    return e;
}

static expr *expr3(int critical)
{
    expr *e, *f;

    e = expr4(critical);
    if (!e)
        return NULL;

    while (i == TOKEN_SHL || i == TOKEN_SHR || i == TOKEN_SAR) {
        int j = i;
        i = scan(scpriv, tokval);
        f = expr4(critical);
        if (!f)
            return NULL;
        if (!(is_simple(e) || is_just_unknown(e)) ||
            !(is_simple(f) || is_just_unknown(f))) {
            nasm_error(ERR_NONFATAL, "shift operator may only be applied to"
                  " scalar values");
        } else if (is_just_unknown(e) || is_just_unknown(f)) {
            e = unknown_expr();
        } else {
            switch (j) {
            case TOKEN_SHL:
                e = scalarvect(reloc_value(e) << reloc_value(f));
                break;
            case TOKEN_SHR:
                e = scalarvect(((uint64_t)reloc_value(e)) >>
                               reloc_value(f));
                break;
            case TOKEN_SAR:
                e = scalarvect(((int64_t)reloc_value(e)) >>
                               reloc_value(f));
                break;
            }
        }
    }
    return e;
}

static expr *expr4(int critical)
{
    expr *e, *f;

    e = expr5(critical);
    if (!e)
        return NULL;
    while (i == '+' || i == '-') {
        int j = i;
        i = scan(scpriv, tokval);
        f = expr5(critical);
        if (!f)
            return NULL;
        switch (j) {
        case '+':
            e = add_vectors(e, f);
            break;
        case '-':
            e = add_vectors(e, scalar_mult(f, -1L, false));
            break;
        }
    }
    return e;
}

static expr *expr5(int critical)
{
    expr *e, *f;

    e = expr6(critical);
    if (!e)
        return NULL;
    while (i == '*' || i == '/' || i == '%' ||
           i == TOKEN_SDIV || i == TOKEN_SMOD) {
        int j = i;
        i = scan(scpriv, tokval);
        f = expr6(critical);
        if (!f)
            return NULL;
        if (j != '*' && (!(is_simple(e) || is_just_unknown(e)) ||
                         !(is_simple(f) || is_just_unknown(f)))) {
            nasm_error(ERR_NONFATAL, "division operator may only be applied to"
                  " scalar values");
            return NULL;
        }
        if (j != '*' && !is_just_unknown(f) && reloc_value(f) == 0) {
            nasm_error(ERR_NONFATAL, "division by zero");
            return NULL;
        }
        switch (j) {
        case '*':
            if (is_simple(e))
                e = scalar_mult(f, reloc_value(e), true);
            else if (is_simple(f))
                e = scalar_mult(e, reloc_value(f), true);
            else if (is_just_unknown(e) && is_just_unknown(f))
                e = unknown_expr();
            else {
                nasm_error(ERR_NONFATAL, "unable to multiply two "
                      "non-scalar objects");
                return NULL;
            }
            break;
        case '/':
            if (is_just_unknown(e) || is_just_unknown(f))
                e = unknown_expr();
            else
                e = scalarvect(((uint64_t)reloc_value(e)) /
                               ((uint64_t)reloc_value(f)));
            break;
        case '%':
            if (is_just_unknown(e) || is_just_unknown(f))
                e = unknown_expr();
            else
                e = scalarvect(((uint64_t)reloc_value(e)) %
                               ((uint64_t)reloc_value(f)));
            break;
        case TOKEN_SDIV:
            if (is_just_unknown(e) || is_just_unknown(f))
                e = unknown_expr();
            else
                e = scalarvect(((int64_t)reloc_value(e)) /
                               ((int64_t)reloc_value(f)));
            break;
        case TOKEN_SMOD:
            if (is_just_unknown(e) || is_just_unknown(f))
                e = unknown_expr();
            else
                e = scalarvect(((int64_t)reloc_value(e)) %
                               ((int64_t)reloc_value(f)));
            break;
        }
    }
    return e;
}

static expr *eval_floatize(enum floatize type)
{
    uint8_t result[16], *p;     /* Up to 128 bits */
    static const struct {
        int bytes, start, len;
    } formats[] = {
        {  1, 0, 1 },           /* FLOAT_8 */
        {  2, 0, 2 },           /* FLOAT_16 */
        {  4, 0, 4 },           /* FLOAT_32 */
        {  8, 0, 8 },           /* FLOAT_64 */
        { 10, 0, 8 },           /* FLOAT_80M */
        { 10, 8, 2 },           /* FLOAT_80E */
        { 16, 0, 8 },           /* FLOAT_128L */
        { 16, 8, 8 },           /* FLOAT_128H */
    };
    int sign = 1;
    int64_t val;
    int j;

    i = scan(scpriv, tokval);
    if (i != '(') {
        nasm_error(ERR_NONFATAL, "expecting `('");
        return NULL;
    }
    i = scan(scpriv, tokval);
    if (i == '-' || i == '+') {
        sign = (i == '-') ? -1 : 1;
        i = scan(scpriv, tokval);
    }
    if (i != TOKEN_FLOAT) {
        nasm_error(ERR_NONFATAL, "expecting floating-point number");
        return NULL;
    }
    if (!float_const(tokval->t_charptr, sign, result, formats[type].bytes))
        return NULL;
    i = scan(scpriv, tokval);
    if (i != ')') {
        nasm_error(ERR_NONFATAL, "expecting `)'");
        return NULL;
    }

    p = result+formats[type].start+formats[type].len;
    val = 0;
    for (j = formats[type].len; j; j--) {
        p--;
        val = (val << 8) + *p;
    }

    begintemp();
    addtotemp(EXPR_SIMPLE, val);

    i = scan(scpriv, tokval);
    return finishtemp();
}

static expr *eval_strfunc(enum strfunc type)
{
    char *string;
    size_t string_len;
    int64_t val;
    bool parens, rn_warn;

    parens = false;
    i = scan(scpriv, tokval);
    if (i == '(') {
        parens = true;
        i = scan(scpriv, tokval);
    }
    if (i != TOKEN_STR) {
        nasm_error(ERR_NONFATAL, "expecting string");
        return NULL;
    }
    string_len = string_transform(tokval->t_charptr, tokval->t_inttwo,
                                  &string, type);
    if (string_len == (size_t)-1) {
        nasm_error(ERR_NONFATAL, "invalid string for transform");
        return NULL;
    }

    val = readstrnum(string, string_len, &rn_warn);
    if (parens) {
        i = scan(scpriv, tokval);
        if (i != ')') {
            nasm_error(ERR_NONFATAL, "expecting `)'");
            return NULL;
        }
    }

    if (rn_warn)
        nasm_error(ERR_WARNING|ERR_PASS1, "character constant too long");

    begintemp();
    addtotemp(EXPR_SIMPLE, val);

    i = scan(scpriv, tokval);
    return finishtemp();
}

static int64_t eval_ifunc(int64_t val, enum ifunc func)
{
    int errtype;
    uint64_t uval = (uint64_t)val;
    int64_t rv;

    switch (func) {
    case IFUNC_ILOG2E:
    case IFUNC_ILOG2W:
        errtype = (func == IFUNC_ILOG2E) ? ERR_NONFATAL : ERR_WARNING;

        if (!is_power2(uval))
            nasm_error(errtype, "ilog2 argument is not a power of two");
        /* fall through */
    case IFUNC_ILOG2F:
        rv = ilog2_64(uval);
        break;

    case IFUNC_ILOG2C:
        rv = (uval < 2) ? 0 : ilog2_64(uval-1) + 1;
        break;

    default:
        nasm_panic("invalid IFUNC token %d", func);
        rv = 0;
        break;
    }

    return rv;
}

static expr *expr6(int critical)
{
    int32_t type;
    expr *e;
    int32_t label_seg;
    int64_t label_ofs;
    int64_t tmpval;
    bool rn_warn;
    const char *scope;

    if (++deadman > nasm_limit[LIMIT_EVAL]) {
        nasm_error(ERR_NONFATAL, "expression too long");
        return NULL;
    }

    switch (i) {
    case '-':
        i = scan(scpriv, tokval);
        e = expr6(critical);
        if (!e)
            return NULL;
        return scalar_mult(e, -1L, false);

    case '+':
        i = scan(scpriv, tokval);
        return expr6(critical);

    case '~':
        i = scan(scpriv, tokval);
        e = expr6(critical);
        if (!e)
            return NULL;
        if (is_just_unknown(e))
            return unknown_expr();
        else if (!is_simple(e)) {
            nasm_error(ERR_NONFATAL, "`~' operator may only be applied to"
                  " scalar values");
            return NULL;
        }
        return scalarvect(~reloc_value(e));

    case '!':
        i = scan(scpriv, tokval);
        e = expr6(critical);
        if (!e)
            return NULL;
        if (is_just_unknown(e))
            return unknown_expr();
        else if (!is_simple(e)) {
            nasm_error(ERR_NONFATAL, "`!' operator may only be applied to"
                  " scalar values");
            return NULL;
        }
        return scalarvect(!reloc_value(e));

    case TOKEN_IFUNC:
    {
        enum ifunc func = tokval->t_integer;
        i = scan(scpriv, tokval);
        e = expr6(critical);
        if (!e)
            return NULL;
        if (is_just_unknown(e))
            return unknown_expr();
        else if (!is_simple(e)) {
            nasm_error(ERR_NONFATAL, "function may only be applied to"
                  " scalar values");
            return NULL;
        }
        return scalarvect(eval_ifunc(reloc_value(e), func));
    }

    case TOKEN_SEG:
        i = scan(scpriv, tokval);
        e = expr6(critical);
        if (!e)
            return NULL;
        e = segment_part(e);
        if (!e)
            return NULL;
        if (is_unknown(e) && critical) {
            nasm_error(ERR_NONFATAL, "unable to determine segment base");
            return NULL;
        }
        return e;

    case TOKEN_FLOATIZE:
        return eval_floatize(tokval->t_integer);

    case TOKEN_STRFUNC:
        return eval_strfunc(tokval->t_integer);

    case '(':
        i = scan(scpriv, tokval);
        e = bexpr(critical);
        if (!e)
            return NULL;
        if (i != ')') {
            nasm_error(ERR_NONFATAL, "expecting `)'");
            return NULL;
        }
        i = scan(scpriv, tokval);
        return e;

    case TOKEN_NUM:
    case TOKEN_STR:
    case TOKEN_REG:
    case TOKEN_ID:
    case TOKEN_INSN:            /* Opcodes that occur here are really labels */
    case TOKEN_HERE:
    case TOKEN_BASE:
    case TOKEN_DECORATOR:
        begintemp();
        switch (i) {
        case TOKEN_NUM:
            addtotemp(EXPR_SIMPLE, tokval->t_integer);
            break;
        case TOKEN_STR:
            tmpval = readstrnum(tokval->t_charptr, tokval->t_inttwo, &rn_warn);
            if (rn_warn)
                nasm_error(ERR_WARNING|ERR_PASS1, "character constant too long");
            addtotemp(EXPR_SIMPLE, tmpval);
            break;
        case TOKEN_REG:
            addtotemp(tokval->t_integer, 1L);
            if (hint && hint->type == EAH_NOHINT)
                hint->base = tokval->t_integer, hint->type = EAH_MAKEBASE;
            break;
        case TOKEN_ID:
        case TOKEN_INSN:
        case TOKEN_HERE:
        case TOKEN_BASE:
            /*
             * If !location.known, this indicates that no
             * symbol, Here or Base references are valid because we
             * are in preprocess-only mode.
             */
            if (!location.known) {
                nasm_error(ERR_NONFATAL,
                      "%s not supported in preprocess-only mode",
                      (i == TOKEN_HERE ? "`$'" :
                       i == TOKEN_BASE ? "`$$'" :
                       "symbol references"));
                addtotemp(EXPR_UNKNOWN, 1L);
                break;
            }

            type = EXPR_SIMPLE; /* might get overridden by UNKNOWN */
            if (i == TOKEN_BASE) {
                label_seg = in_absolute ? absolute.segment : location.segment;
                label_ofs = 0;
            } else if (i == TOKEN_HERE) {
                label_seg = in_absolute ? absolute.segment : location.segment;
                label_ofs = in_absolute ? absolute.offset : location.offset;
            } else {
                if (!lookup_label(tokval->t_charptr, &label_seg, &label_ofs)) {
                    scope = local_scope(tokval->t_charptr);
                    if (critical == 2) {
                        nasm_error(ERR_NONFATAL, "symbol `%s%s' undefined",
                              scope,tokval->t_charptr);
                        return NULL;
                    } else if (critical == 1) {
                        nasm_error(ERR_NONFATAL,
                              "symbol `%s%s' not defined before use",
                              scope,tokval->t_charptr);
                        return NULL;
                    } else {
                        if (opflags)
                            *opflags |= OPFLAG_FORWARD;
                        type = EXPR_UNKNOWN;
                        label_seg = NO_SEG;
                        label_ofs = 1;
                    }
                }
                if (opflags && is_extern(tokval->t_charptr))
                    *opflags |= OPFLAG_EXTERN;
            }
            addtotemp(type, label_ofs);
            if (label_seg != NO_SEG)
                addtotemp(EXPR_SEGBASE + label_seg, 1L);
            break;
        case TOKEN_DECORATOR:
            addtotemp(EXPR_RDSAE, tokval->t_integer);
            break;
        }
        i = scan(scpriv, tokval);
        return finishtemp();

    default:
        nasm_error(ERR_NONFATAL, "expression syntax error");
        return NULL;
    }
}

expr *evaluate(scanner sc, void *scprivate, struct tokenval *tv,
               int *fwref, int critical, struct eval_hints *hints)
{
    expr *e;
    expr *f = NULL;

    deadman = 0;
    
    hint = hints;
    if (hint)
        hint->type = EAH_NOHINT;

    if (critical & CRITICAL) {
        critical &= ~CRITICAL;
        bexpr = rexp0;
    } else
        bexpr = expr0;

    scan = sc;
    scpriv = scprivate;
    tokval = tv;
    opflags = fwref;

    if (tokval->t_type == TOKEN_INVALID)
        i = scan(scpriv, tokval);
    else
        i = tokval->t_type;

    while (ntempexprs)          /* initialize temporary storage */
        nasm_free(tempexprs[--ntempexprs]);

    e = bexpr(critical);
    if (!e)
        return NULL;

    if (i == TOKEN_WRT) {
        i = scan(scpriv, tokval);       /* eat the WRT */
        f = expr6(critical);
        if (!f)
            return NULL;
    }
    e = scalar_mult(e, 1L, false);      /* strip far-absolute segment part */
    if (f) {
        expr *g;
        if (is_just_unknown(f))
            g = unknown_expr();
        else {
            int64_t value;
            begintemp();
            if (!is_reloc(f)) {
                nasm_error(ERR_NONFATAL, "invalid right-hand operand to WRT");
                return NULL;
            }
            value = reloc_seg(f);
            if (value == NO_SEG)
                value = reloc_value(f) | SEG_ABS;
            else if (!(value & SEG_ABS) && !(value % 2) && critical) {
                nasm_error(ERR_NONFATAL, "invalid right-hand operand to WRT");
                return NULL;
            }
            addtotemp(EXPR_WRT, value);
            g = finishtemp();
        }
        e = add_vectors(e, g);
    }
    return e;
}
