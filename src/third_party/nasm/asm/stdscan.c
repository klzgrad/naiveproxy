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

#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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
static char *stdscan_bufptr = NULL;
static char **stdscan_tempstorage = NULL;
static int stdscan_tempsize = 0, stdscan_templen = 0;
#define STDSCAN_TEMP_DELTA 256

void stdscan_set(char *str)
{
        stdscan_bufptr = str;
}

char *stdscan_get(void)
{
        return stdscan_bufptr;
}

static void stdscan_pop(void)
{
    nasm_free(stdscan_tempstorage[--stdscan_templen]);
}

void stdscan_reset(void)
{
    while (stdscan_templen > 0)
        stdscan_pop();
}

/*
 * Unimportant cleanup is done to avoid confusing people who are trying
 * to debug real memory leaks
 */
void stdscan_cleanup(void)
{
    stdscan_reset();
    nasm_free(stdscan_tempstorage);
}

static char *stdscan_copy(char *p, int len)
{
    char *text;

    text = nasm_malloc(len + 1);
    memcpy(text, p, len);
    text[len] = '\0';

    if (stdscan_templen >= stdscan_tempsize) {
        stdscan_tempsize += STDSCAN_TEMP_DELTA;
        stdscan_tempstorage = nasm_realloc(stdscan_tempstorage,
                                           stdscan_tempsize *
                                           sizeof(char *));
    }
    stdscan_tempstorage[stdscan_templen++] = text;

    return text;
}

/*
 * a token is enclosed with braces. proper token type will be assigned
 * accordingly with the token flag.
 */
static int stdscan_handle_brace(struct tokenval *tv)
{
    if (!(tv->t_flag & TFLAG_BRC_ANY)) {
        /* invalid token is put inside braces */
        nasm_error(ERR_NONFATAL,
                    "`%s' is not a valid decorator with braces", tv->t_charptr);
        tv->t_type = TOKEN_INVALID;
    } else if (tv->t_flag & TFLAG_BRC_OPT) {
        if (is_reg_class(OPMASKREG, tv->t_integer)) {
            /* within braces, opmask register is now used as a mask */
            tv->t_type = TOKEN_OPMASK;
        }
    }

    return tv->t_type;
}

int stdscan(void *private_data, struct tokenval *tv)
{
    char ourcopy[MAX_KEYWORD + 1], *r, *s;

    (void)private_data;         /* Don't warn that this parameter is unused */

    stdscan_bufptr = nasm_skip_spaces(stdscan_bufptr);
    if (!*stdscan_bufptr)
        return tv->t_type = TOKEN_EOS;

    /* we have a token; either an id, a number or a char */
    if (isidstart(*stdscan_bufptr) ||
        (*stdscan_bufptr == '$' && isidstart(stdscan_bufptr[1]))) {
        /* now we've got an identifier */
        bool is_sym = false;
        int token_type;

        if (*stdscan_bufptr == '$') {
            is_sym = true;
            stdscan_bufptr++;
        }

        r = stdscan_bufptr++;
        /* read the entire buffer to advance the buffer pointer but... */
        while (isidchar(*stdscan_bufptr))
            stdscan_bufptr++;

        /* ... copy only up to IDLEN_MAX-1 characters */
        tv->t_charptr = stdscan_copy(r, stdscan_bufptr - r < IDLEN_MAX ?
                                     stdscan_bufptr - r : IDLEN_MAX - 1);

        if (is_sym || stdscan_bufptr - r > MAX_KEYWORD)
            return tv->t_type = TOKEN_ID;       /* bypass all other checks */

        for (s = tv->t_charptr, r = ourcopy; *s; s++)
            *r++ = nasm_tolower(*s);
        *r = '\0';
        /* right, so we have an identifier sitting in temp storage. now,
         * is it actually a register or instruction name, or what? */
        token_type = nasm_token_hash(ourcopy, tv);

	if (unlikely(tv->t_flag & TFLAG_WARN)) {
	    nasm_error(ERR_WARNING|ERR_PASS1|ERR_WARN_PTR,
		       "`%s' is not a NASM keyword", tv->t_charptr);
	}

        if (likely(!(tv->t_flag & TFLAG_BRC))) {
            /* most of the tokens fall into this case */
            return token_type;
        } else {
            return tv->t_type = TOKEN_ID;
        }
    } else if (*stdscan_bufptr == '$' && !isnumchar(stdscan_bufptr[1])) {
        /*
         * It's a $ sign with no following hex number; this must
         * mean it's a Here token ($), evaluating to the current
         * assembly location, or a Base token ($$), evaluating to
         * the base of the current segment.
         */
        stdscan_bufptr++;
        if (*stdscan_bufptr == '$') {
            stdscan_bufptr++;
            return tv->t_type = TOKEN_BASE;
        }
        return tv->t_type = TOKEN_HERE;
    } else if (isnumstart(*stdscan_bufptr)) {   /* now we've got a number */
        bool rn_error;
        bool is_hex = false;
        bool is_float = false;
        bool has_e = false;
        char c;

        r = stdscan_bufptr;

        if (*stdscan_bufptr == '$') {
            stdscan_bufptr++;
            is_hex = true;
        }

        for (;;) {
            c = *stdscan_bufptr++;

            if (!is_hex && (c == 'e' || c == 'E')) {
                has_e = true;
                if (*stdscan_bufptr == '+' || *stdscan_bufptr == '-') {
                    /*
                     * e can only be followed by +/- if it is either a
                     * prefixed hex number or a floating-point number
                     */
                    is_float = true;
                    stdscan_bufptr++;
                }
            } else if (c == 'H' || c == 'h' || c == 'X' || c == 'x') {
                is_hex = true;
            } else if (c == 'P' || c == 'p') {
                is_float = true;
                if (*stdscan_bufptr == '+' || *stdscan_bufptr == '-')
                    stdscan_bufptr++;
            } else if (isnumchar(c))
                ; /* just advance */
            else if (c == '.')
                is_float = true;
            else
                break;
        }
        stdscan_bufptr--;       /* Point to first character beyond number */

        if (has_e && !is_hex) {
            /* 1e13 is floating-point, but 1e13h is not */
            is_float = true;
        }

        if (is_float) {
            tv->t_charptr = stdscan_copy(r, stdscan_bufptr - r);
            return tv->t_type = TOKEN_FLOAT;
        } else {
            r = stdscan_copy(r, stdscan_bufptr - r);
            tv->t_integer = readnum(r, &rn_error);
            stdscan_pop();
            if (rn_error) {
                /* some malformation occurred */
                return tv->t_type = TOKEN_ERRNUM;
            }
            tv->t_charptr = NULL;
            return tv->t_type = TOKEN_NUM;
        }
    } else if (*stdscan_bufptr == '\'' || *stdscan_bufptr == '"' ||
               *stdscan_bufptr == '`') {
        /* a quoted string */
        char start_quote = *stdscan_bufptr;
        tv->t_charptr = stdscan_bufptr;
        tv->t_inttwo = nasm_unquote(tv->t_charptr, &stdscan_bufptr);
        if (*stdscan_bufptr != start_quote)
            return tv->t_type = TOKEN_ERRSTR;
        stdscan_bufptr++;       /* Skip final quote */
        return tv->t_type = TOKEN_STR;
    } else if (*stdscan_bufptr == '{') {
        /* now we've got a decorator */
        int token_len;

        stdscan_bufptr = nasm_skip_spaces(stdscan_bufptr);

        r = ++stdscan_bufptr;
        /*
         * read the entire buffer to advance the buffer pointer
         * {rn-sae}, {rd-sae}, {ru-sae}, {rz-sae} contain '-' in tokens.
         */
        while (isbrcchar(*stdscan_bufptr))
            stdscan_bufptr++;

        token_len = stdscan_bufptr - r;

        /* ... copy only up to DECOLEN_MAX-1 characters */
        tv->t_charptr = stdscan_copy(r, token_len < DECOLEN_MAX ?
                                        token_len : DECOLEN_MAX - 1);

        stdscan_bufptr = nasm_skip_spaces(stdscan_bufptr);
        /* if brace is not closed properly or token is too long  */
        if ((*stdscan_bufptr != '}') || (token_len > MAX_KEYWORD)) {
            nasm_error(ERR_NONFATAL,
                       "invalid decorator token inside braces");
            return tv->t_type = TOKEN_INVALID;
        }

        stdscan_bufptr++;       /* skip closing brace */

        for (s = tv->t_charptr, r = ourcopy; *s; s++)
            *r++ = nasm_tolower(*s);
        *r = '\0';

        /* right, so we have a decorator sitting in temp storage. */
        nasm_token_hash(ourcopy, tv);

        /* handle tokens inside braces */
        return stdscan_handle_brace(tv);
    } else if (*stdscan_bufptr == ';') {
        /* a comment has happened - stay */
        return tv->t_type = TOKEN_EOS;
    } else if (stdscan_bufptr[0] == '>' && stdscan_bufptr[1] == '>') {
        if (stdscan_bufptr[2] == '>') {
            stdscan_bufptr += 3;
            return tv->t_type = TOKEN_SAR;
        } else {
            stdscan_bufptr += 2;
            return tv->t_type = TOKEN_SHR;
        }
    } else if (stdscan_bufptr[0] == '<' && stdscan_bufptr[1] == '<') {
        stdscan_bufptr += stdscan_bufptr[2] == '<' ? 3 : 2;
        return tv->t_type = TOKEN_SHL;
    } else if (stdscan_bufptr[0] == '/' && stdscan_bufptr[1] == '/') {
        stdscan_bufptr += 2;
        return tv->t_type = TOKEN_SDIV;
    } else if (stdscan_bufptr[0] == '%' && stdscan_bufptr[1] == '%') {
        stdscan_bufptr += 2;
        return tv->t_type = TOKEN_SMOD;
    } else if (stdscan_bufptr[0] == '=' && stdscan_bufptr[1] == '=') {
        stdscan_bufptr += 2;
        return tv->t_type = TOKEN_EQ;
    } else if (stdscan_bufptr[0] == '<' && stdscan_bufptr[1] == '>') {
        stdscan_bufptr += 2;
        return tv->t_type = TOKEN_NE;
    } else if (stdscan_bufptr[0] == '!' && stdscan_bufptr[1] == '=') {
        stdscan_bufptr += 2;
        return tv->t_type = TOKEN_NE;
    } else if (stdscan_bufptr[0] == '<' && stdscan_bufptr[1] == '=') {
        stdscan_bufptr += 2;
        return tv->t_type = TOKEN_LE;
    } else if (stdscan_bufptr[0] == '>' && stdscan_bufptr[1] == '=') {
        stdscan_bufptr += 2;
        return tv->t_type = TOKEN_GE;
    } else if (stdscan_bufptr[0] == '&' && stdscan_bufptr[1] == '&') {
        stdscan_bufptr += 2;
        return tv->t_type = TOKEN_DBL_AND;
    } else if (stdscan_bufptr[0] == '^' && stdscan_bufptr[1] == '^') {
        stdscan_bufptr += 2;
        return tv->t_type = TOKEN_DBL_XOR;
    } else if (stdscan_bufptr[0] == '|' && stdscan_bufptr[1] == '|') {
        stdscan_bufptr += 2;
        return tv->t_type = TOKEN_DBL_OR;
    } else                      /* just an ordinary char */
        return tv->t_type = (uint8_t)(*stdscan_bufptr++);
}
