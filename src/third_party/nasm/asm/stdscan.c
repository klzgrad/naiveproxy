/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2024 The NASM Authors - All Rights Reserved */

#include "compiler.h"

#include "nctype.h"

#include "nasm.h"
#include "nasmlib.h"
#include "error.h"
#include "quote.h"
#include "stdscan.h"
#include "insns.h"

/*
 * Standard scanner routine used by parser.c and some output
 * formats. It keeps a succession of temporary-storage strings in
 * stdscan_tempstorage, which can be cleared using stdscan_reset.
 */
enum stdscan_scan_state {
    ss_init                    /* Normal scanner state */
};

struct token_stack {
    struct token_stack *prev;
    struct tokenval tv;
};

struct stdscan_state {
    char *bufptr;
    struct token_stack *pushback;
    enum stdscan_scan_state sstate;
};

static struct stdscan_state scan;
static char **stdscan_tempstorage = NULL;
static int stdscan_tempsize = 0, stdscan_templen = 0;
#define STDSCAN_TEMP_DELTA 256

static void *stdscan_alloc(size_t bytes);

void stdscan_set(const struct stdscan_state *state)
{
    scan = *state;
}

const struct stdscan_state *stdscan_get(void)
{
    struct stdscan_state *save = stdscan_alloc(sizeof(struct stdscan_state));
    *save = scan;
    return save;
}

char *stdscan_tell(void)
{
    return scan.bufptr;
}

static void stdscan_pop(void)
{
    nasm_free(stdscan_tempstorage[--stdscan_templen]);
}

static void stdscan_pushback_pop(void)
{
    struct token_stack *ps;

    ps = scan.pushback->prev;
    nasm_free(scan.pushback);
    scan.pushback = ps;
}

void stdscan_reset(char *buffer)
{
    while (stdscan_templen > 0)
        stdscan_pop();

    while (scan.pushback)
        stdscan_pushback_pop();

    scan.bufptr   = buffer;
    scan.sstate   = ss_init;
}

/*
 * Unimportant cleanup is done to avoid confusing people who are trying
 * to debug real memory leaks
 */
void stdscan_cleanup(void)
{
    stdscan_reset(NULL);
    nasm_free(stdscan_tempstorage);
}

static void *stdscan_alloc(size_t bytes)
{
    void *buf = nasm_malloc(bytes);
    if (stdscan_templen >= stdscan_tempsize) {
        stdscan_tempsize += STDSCAN_TEMP_DELTA;
        stdscan_tempstorage = nasm_realloc(stdscan_tempstorage,
                                           stdscan_tempsize *
                                           sizeof(char *));
    }
    stdscan_tempstorage[stdscan_templen++] = buf;

    return buf;
}

static char *stdscan_copy(const char *p, int len)
{
    char *text = stdscan_alloc(len+1);
    memcpy(text, p, len);
    text[len] = '\0';

    return text;
}

void stdscan_pushback(const struct tokenval *tv)
{
    struct token_stack *ts;

    nasm_new(ts);
    ts->tv = *tv;
    ts->prev = scan.pushback;
    scan.pushback = ts;
}

/*
 * Parse a set of braces. A set of braces can contain either
 * a keyword (which, unlike normal keywords, can contain -)
 * or a sequence like {dfv=foo,bar,baz} which generates a
 * {dfv=} token with t_integer set to the logical OR of the t_integer
 * and t_inttwo values of {dfv=foo}{dfv=bar}{dfv=baz}; these tokens
 * *except* {dfv=} should have TFLAG_ORBIT set.
 */
static int stdscan_parse_braces(struct tokenval *tv)
{
    size_t prefix_len = 0;
    size_t suffix_len = 0;
    size_t brace_len;
    const char *startp;
    char *endp;
    const char *pfx, *r, *e;
    char *buf;
    char nextchar;
    int64_t t_integer, t_inttwo;
    bool first;

    startp = scan.bufptr;        /* Beginning including { */
    pfx = r = scan.bufptr = nasm_skip_spaces(++scan.bufptr);

    /*
     * Read the token to advance the buffer pointer
     * {rn-sae}, {rd-sae}, {ru-sae}, {rz-sae} contain '-' in tokens.
     */
    while (nasm_isbrcchar(*scan.bufptr))
        scan.bufptr++;

    e = scan.bufptr;

    /*
     * Followed by equal sign?
     */
    scan.bufptr = nasm_skip_spaces(scan.bufptr);
    if (r != e && *scan.bufptr == '=') {
        prefix_len = e - pfx;
        r = e = ++scan.bufptr;
        /* Note that the first suffix is blank */
    }

    /*
     * Find terminating brace, assuming it exists, to allocate a
     * buffer large enough for the whole possible compound token.
     * Don't fill it yet as we are using it as a work buffer for now.
     */
    endp = strchr(scan.bufptr, '}');
    if (!endp) {
        nasm_nonfatal("unterminated braces at end of line");
        return tv->t_type = TOKEN_INVALID;
    }

    brace_len = ++endp - startp;
    buf = tv->t_charptr = stdscan_alloc(brace_len + 1);

    if (prefix_len) {
        memcpy(buf, pfx, prefix_len);
        buf[prefix_len++] = '=';
    }
    t_integer = t_inttwo = 0;
    first = true;

    while (1) {
        suffix_len = e - r;

        memcpy(buf + prefix_len, r, suffix_len);
        buf[prefix_len + suffix_len] = '\0';

        /* Note: nasm_token_hash doesn't modify t_charptr */
        nasm_token_hash(buf, tv);

        if (!(tv->t_flag & TFLAG_BRC_ANY)) {
            /* invalid token is put inside braces */
            nasm_nonfatal("`{%s}' is not a valid brace token", buf);
            tv->t_type = TOKEN_INVALID;
            break;
        }

        if (tv->t_type == TOKEN_REG &&
            is_reg_class(OPMASKREG, tv->t_integer)) {
            /* within braces, opmask register is now used as a mask */
            tv->t_type = TOKEN_OPMASK;
        }

        if (tv->t_flag & TFLAG_ORBIT) {
            t_integer |= tv->t_integer;
            t_inttwo  |= tv->t_inttwo;
        } else {
            t_integer = tv->t_integer;
            t_inttwo  = tv->t_inttwo;
        }

        scan.bufptr = nasm_skip_spaces(scan.bufptr);
        nextchar = *scan.bufptr;

        if (nextchar == '}')
            break;

        if (!prefix_len ||
            !(nextchar == ',' || (first && nasm_isbrcchar(nextchar)))) {
            nasm_nonfatal("invalid character `%c' in brace sequence",
                          nextchar);
            tv->t_type = TOKEN_INVALID;
            break;
        }

        if (nextchar == ',')
            scan.bufptr = nasm_skip_spaces(++scan.bufptr);

        r = scan.bufptr;
        while (nasm_isbrcchar(*scan.bufptr))
            scan.bufptr++;
        e = scan.bufptr;

        first = false;
    }

    memcpy(tv->t_charptr, startp, brace_len);
    buf[brace_len] = '\0';
    scan.bufptr = endp;

    tv->t_integer = t_integer;
    tv->t_inttwo  = t_inttwo;
    return tv->t_type;
}

static int stdscan_token(struct tokenval *tv);

int stdscan(void *private_data, struct tokenval *tv)
{
    int i;

    (void)private_data;         /* Don't warn that this parameter is unused */


    if (unlikely(scan.pushback)) {
        *tv = scan.pushback->tv;
        stdscan_pushback_pop();
        return tv->t_type;
    }

    nasm_zero(*tv);

    scan.bufptr = nasm_skip_spaces(scan.bufptr);
    tv->t_start = scan.bufptr;

    if (!*scan.bufptr)
        return tv->t_type = TOKEN_EOS;

    i = stdscan_token(tv);
    tv->t_len = scan.bufptr - tv->t_start;

    return i;
}

/* Skip id chars and return an appropriate string */
static int stdscan_symbol(struct tokenval *tv)
{
    char *p = scan.bufptr;
    const char *r = p;
    size_t len;

    p++;                        /* Leading character already verified */

    /* Skip the entire symbol but only copy up to IDLEN_MAX characters */
    while (nasm_isidchar(*p))
        p++;

    scan.bufptr = p;
    len = p - r;
    if (len >= IDLEN_MAX)
        len = IDLEN_MAX - 1;

    tv->t_len = len;
    tv->t_charptr = stdscan_copy(r, len);
    return tv->t_type = TOKEN_ID;
}

static int stdscan_token(struct tokenval *tv)
{
    const char *r;

    /* we have a token; either an id, a number, operator or char */
    if (nasm_isidstart(*scan.bufptr)) {
        stdscan_symbol(tv);

        if (tv->t_len <= MAX_KEYWORD) {
            /* Check to see if it is a keyword of some kind */
            int token_type = nasm_token_hash(tv->t_charptr, tv);

            if (unlikely(tv->t_flag & TFLAG_WARN)) {
                nasm_warn(WARN_PTR, "`%s' is not a NASM keyword",
                          tv->t_charptr);
            }

            if (likely(!(tv->t_flag & TFLAG_BRC))) {
                /* most of the tokens fall into this case */
                return token_type;
            }
        }
        return tv->t_type = TOKEN_ID;
    } else if (*scan.bufptr == '$' &&
               (!globl.dollarhex || !nasm_isdigit(scan.bufptr[1]))) {
        /*
         * It's a $ sign with no following hex number; this must
         * mean it's a Here token ($), evaluating to the current
         * assembly location, a Base token ($$), evaluating to
         * the base of the current segment, or an identifier beginning
         * with $ (escaped by a previous $).
         */
        scan.bufptr++;
        if (*scan.bufptr == '$' && !nasm_isidchar(scan.bufptr[1])) {
            scan.bufptr++;
            return tv->t_type = TOKEN_BASE;
        } else if (nasm_isidchar(*scan.bufptr)) {
            /* $-escaped symbol that does NOT start with $ */
            return stdscan_symbol(tv);
        } else {
            return tv->t_type = TOKEN_HERE;
        }
    } else if (nasm_isnumstart(*scan.bufptr)) {   /* now we've got a number */
        bool rn_error;
        bool is_hex = false;
        bool is_float = false;
        bool has_e = false;
        char c;

        r = scan.bufptr;

        if (*scan.bufptr == '$') {
            scan.bufptr++;
            is_hex = true;
            warn_dollar_hex();
        }

        for (;;) {
            c = *scan.bufptr++;

            if (!is_hex && (c == 'e' || c == 'E')) {
                has_e = true;
                if (*scan.bufptr == '+' || *scan.bufptr == '-') {
                    /*
                     * e can only be followed by +/- if it is either a
                     * prefixed hex number or a floating-point number
                     */
                    is_float = true;
                    scan.bufptr++;
                }
            } else if (c == 'H' || c == 'h' || c == 'X' || c == 'x') {
                is_hex = true;
            } else if (c == 'P' || c == 'p') {
                is_float = true;
                if (*scan.bufptr == '+' || *scan.bufptr == '-')
                    scan.bufptr++;
            } else if (nasm_isnumchar(c))
                ; /* just advance */
            else if (c == '.')
                is_float = true;
            else
                break;
        }
        scan.bufptr--;       /* Point to first character beyond number */

        if (has_e && !is_hex) {
            /* 1e13 is floating-point, but 1e13h is not */
            is_float = true;
        }

        if (is_float) {
            tv->t_charptr = stdscan_copy(r, scan.bufptr - r);
            return tv->t_type = TOKEN_FLOAT;
        } else {
            r = stdscan_copy(r, scan.bufptr - r);
            tv->t_integer = readnum(r, &rn_error);
            stdscan_pop();
            if (rn_error) {
                /* some malformation occurred */
                return tv->t_type = TOKEN_ERRNUM;
            }
            tv->t_charptr = NULL;
            return tv->t_type = TOKEN_NUM;
        }
    } else if (*scan.bufptr == '\'' || *scan.bufptr == '"' ||
               *scan.bufptr == '`') {
        /* a quoted string */
        char start_quote = *scan.bufptr;
        tv->t_charptr = scan.bufptr;
        tv->t_inttwo = nasm_unquote(tv->t_charptr, &scan.bufptr);
        if (*scan.bufptr != start_quote)
            return tv->t_type = TOKEN_ERRSTR;
        scan.bufptr++;       /* Skip final quote */
        return tv->t_type = TOKEN_STR;
    } else if (*scan.bufptr == '{') {
        return stdscan_parse_braces(tv);
        /* now we've got a decorator */
    } else if (*scan.bufptr == ';') {
        /* a comment has happened - stay */
        return tv->t_type = TOKEN_EOS;
    } else if (scan.bufptr[0] == '>' && scan.bufptr[1] == '>') {
        if (scan.bufptr[2] == '>') {
            scan.bufptr += 3;
            return tv->t_type = TOKEN_SAR;
        } else {
            scan.bufptr += 2;
            return tv->t_type = TOKEN_SHR;
        }
    } else if (scan.bufptr[0] == '<' && scan.bufptr[1] == '<') {
        scan.bufptr += scan.bufptr[2] == '<' ? 3 : 2;
        return tv->t_type = TOKEN_SHL;
    } else if (scan.bufptr[0] == '/' && scan.bufptr[1] == '/') {
        scan.bufptr += 2;
        return tv->t_type = TOKEN_SDIV;
    } else if (scan.bufptr[0] == '%' && scan.bufptr[1] == '%') {
        scan.bufptr += 2;
        return tv->t_type = TOKEN_SMOD;
    } else if (scan.bufptr[0] == '=' && scan.bufptr[1] == '=') {
        scan.bufptr += 2;
        return tv->t_type = TOKEN_EQ;
    } else if (scan.bufptr[0] == '<' && scan.bufptr[1] == '>') {
        scan.bufptr += 2;
        return tv->t_type = TOKEN_NE;
    } else if (scan.bufptr[0] == '!' && scan.bufptr[1] == '=') {
        scan.bufptr += 2;
        return tv->t_type = TOKEN_NE;
    } else if (scan.bufptr[0] == '<' && scan.bufptr[1] == '=') {
        if (scan.bufptr[2] == '>') {
            scan.bufptr += 3;
            return tv->t_type = TOKEN_LEG;
        } else {
            scan.bufptr += 2;
            return tv->t_type = TOKEN_LE;
        }
    } else if (scan.bufptr[0] == '>' && scan.bufptr[1] == '=') {
        scan.bufptr += 2;
        return tv->t_type = TOKEN_GE;
    } else if (scan.bufptr[0] == '&' && scan.bufptr[1] == '&') {
        scan.bufptr += 2;
        return tv->t_type = TOKEN_DBL_AND;
    } else if (scan.bufptr[0] == '^' && scan.bufptr[1] == '^') {
        scan.bufptr += 2;
        return tv->t_type = TOKEN_DBL_XOR;
    } else if (scan.bufptr[0] == '|' && scan.bufptr[1] == '|') {
        scan.bufptr += 2;
        return tv->t_type = TOKEN_DBL_OR;
    } else {
        /* just an ordinary char */
        return tv->t_type = (uint8_t)(*scan.bufptr++);
    }
}
