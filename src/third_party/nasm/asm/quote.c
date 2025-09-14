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
 * quote.c
 */

#include "compiler.h"
#include "nasmlib.h"
#include "quote.h"
#include "nctype.h"
#include "error.h"

/*
 * Create a NASM quoted string in newly allocated memory. Update the
 * *lenp parameter with the output length (sans final NUL).
 */

char *nasm_quote(const char *str, size_t *lenp)
{
    const char *p, *ep;
    char c, c1, *q, *nstr;
    unsigned char uc;
    bool sq_ok, dq_ok;
    size_t qlen;
    size_t len = *lenp;

    sq_ok = dq_ok = true;
    ep = str+len;
    qlen = 0;			/* Length if we need `...` quotes */
    for (p = str; p < ep; p++) {
	c = *p;
	switch (c) {
	case '\'':
	    sq_ok = false;
	    qlen++;
	    break;
	case '\"':
	    dq_ok = false;
	    qlen++;
	    break;
	case '`':
	case '\\':
	    qlen += 2;
	    break;
	default:
	    if (c < ' ' || c > '~') {
		sq_ok = dq_ok = false;
		switch (c) {
		case '\a':
		case '\b':
		case '\t':
		case '\n':
		case '\v':
		case '\f':
		case '\r':
		case 27:
		    qlen += 2;
		    break;
		default:
		    c1 = (p+1 < ep) ? p[1] : 0;
		    if (c1 >= '0' && c1 <= '7')
			uc = 0377; /* Must use the full form */
		    else
			uc = c;
		    if (uc > 077)
			qlen++;
		    if (uc > 07)
			qlen++;
		    qlen += 2;
		    break;
		}
	    } else {
		qlen++;
	    }
	    break;
	}
    }

    if (sq_ok || dq_ok) {
	/* Use '...' or "..." */
	nstr = nasm_malloc(len+3);
	nstr[0] = nstr[len+1] = sq_ok ? '\'' : '\"';
        q = &nstr[len+2];
	if (len > 0)
	    memcpy(nstr+1, str, len);
    } else {
	/* Need to use `...` quoted syntax */
	nstr = nasm_malloc(qlen+3);
	q = nstr;
	*q++ = '`';
	for (p = str; p < ep; p++) {
	    c = *p;
	    switch (c) {
	    case '`':
	    case '\\':
		*q++ = '\\';
		*q++ = c;
		break;
	    case 7:
		*q++ = '\\';
		*q++ = 'a';
		break;
	    case 8:
		*q++ = '\\';
		*q++ = 'b';
		break;
	    case 9:
		*q++ = '\\';
		*q++ = 't';
		break;
	    case 10:
		*q++ = '\\';
		*q++ = 'n';
		break;
	    case 11:
		*q++ = '\\';
		*q++ = 'v';
		break;
	    case 12:
		*q++ = '\\';
		*q++ = 'f';
		break;
	    case 13:
		*q++ = '\\';
		*q++ = 'r';
		break;
	    case 27:
		*q++ = '\\';
		*q++ = 'e';
		break;
	    default:
		if (c < ' ' || c > '~') {
		    c1 = (p+1 < ep) ? p[1] : 0;
		    if (c1 >= '0' && c1 <= '7')
			uc = 0377; /* Must use the full form */
		    else
			uc = c;
		    *q++ = '\\';
		    if (uc > 077)
			*q++ = ((unsigned char)c >> 6) + '0';
		    if (uc > 07)
			*q++ = (((unsigned char)c >> 3) & 7) + '0';
		    *q++ = ((unsigned char)c & 7) + '0';
		    break;
		} else {
		    *q++ = c;
		}
		break;
	    }
	}
	*q++ = '`';
	nasm_assert((size_t)(q-nstr) == qlen+2);
    }
    *q = '\0';
    *lenp = q - nstr;
    return nstr;
}

static unsigned char *emit_utf8(unsigned char *q, uint32_t v)
{
    uint32_t vb1, vb2, vb3, vb4, vb5;

    if (v <= 0x7f) {
	*q++ = v;
        goto out0;
    }

    vb1 = v >> 6;
    if (vb1 <= 0x1f) {
	*q++ = 0xc0 + vb1;
        goto out1;
    }

    vb2 = vb1 >> 6;
    if (vb2 <= 0x0f) {
        *q++ = 0xe0 + vb2;
        goto out2;
    }

    vb3 = vb2 >> 6;
    if (vb3 <= 0x07) {
        *q++ = 0xf0 + vb3;
        goto out3;
    }

    vb4 = vb3 >> 6;
    if (vb4 <= 0x03) {
        *q++ = 0xf8 + vb4;
        goto out4;
    }

    /*
     * Note: this is invalid even for "classic" (pre-UTF16) 31-bit
     * UTF-8 if the value is >= 0x8000000. This at least tries to do
     * something vaguely sensible with it. Caveat programmer.
     * The __utf*__ string transform functions do reject these
     * as invalid input.
     *
     * vb5 cannot be more than 3, as a 32-bit value has been shifted
     * right by 5*6 = 30 bits already.
     */
    vb5 = vb4 >> 6;
    *q++ = 0xfc + vb5;
    goto out5;

    /* Emit extension bytes as appropriate */
out5: *q++ = 0x80 + (vb4 & 63);
out4: *q++ = 0x80 + (vb3 & 63);
out3: *q++ = 0x80 + (vb2 & 63);
out2: *q++ = 0x80 + (vb1 & 63);
out1: *q++ = 0x80 + (v & 63);
out0: return q;
}

static inline uint32_t ctlbit(uint32_t v)
{
    return unlikely(v < 32) ? UINT32_C(1) << v : 0;
}

#define CTL_ERR(c)						\
    (badctl & (ctlmask |= ctlbit(c)))

#define EMIT_UTF8(c)						\
    do {							\
        uint32_t ec = (c);                                      \
        if (!CTL_ERR(ec))                                       \
            q = emit_utf8(q, ec);                               \
    } while (0)

#define EMIT(c)                                                 \
    do {                                                        \
        unsigned char ec = (c);                                 \
        if (!CTL_ERR(ec))                                       \
            *q++ = ec;                                          \
    } while (0)

/*
 * Same as nasm_quote, but take the length of a C string;
 * the lenp argument is optional.
 */
char *nasm_quote_cstr(const char *str, size_t *lenp)
{
    size_t len = strlen(str);
    char *qstr = nasm_quote(str, &len);
    if (lenp)
        *lenp = len;
    return qstr;
}

/*
 * Do an *in-place* dequoting of the specified string, returning the
 * resulting length (which may be containing embedded nulls.)
 *
 * In-place replacement is possible since the unquoted length is always
 * shorter than or equal to the quoted length.
 *
 * *ep points to the final quote, or to the null if improperly quoted.
 *
 * Issue an error if the string contains control characters
 * corresponding to bits set in badctl; in that case, the output
 * string, but not *ep, is truncated before the first invalid
 * character.
 *
 * badctl is a bitmask of control characters (0-31) which are forbidden
 * from appearing in the final output.
 *
 * The qstart character can be either '`' (NASM style) or '\"' (C style),
 * to indicate the lead marker of a quoted string. If it is '\"', then
 * '`' is not a special character at all.
 */

size_t nasm_unquote_anystr(char *str, char **ep, const uint32_t badctl,
                           const char qstart)
{
    unsigned char bq;
    const unsigned char *p;
    const unsigned char *escp = NULL;
    unsigned char *q;
    unsigned char c;
    uint32_t ctlmask = 0;       /* Mask of control characters seen */
    enum unq_state {
	st_start,
	st_backslash,
	st_hex,
	st_oct,
	st_ucs,
        st_done
    } state;
    int ndig = 0;
    uint32_t nval = 0;

    p = q = (unsigned char *)str;

    bq = *p++;
    if (!bq)
	return 0;

    if (bq == (unsigned char)qstart) {
	/* `...` string */
	state = st_start;

	while (state != st_done) {
	    c = *p++;
	    switch (state) {
	    case st_start:
                if (c == '\\') {
		    state = st_backslash;
                } else if ((c == '\0') | (c == bq)) {
                    state = st_done;
                } else {
                    EMIT(c);
                }
		break;

	    case st_backslash:
		state = st_start;
		escp = p;	/* Beginning of argument sequence */
		nval = 0;
		switch (c) {
		case 'a':
		    nval = 7;
		    break;
		case 'b':
		    nval = 8;
		    break;
		case 'e':
		    nval = 27;
		    break;
		case 'f':
		    nval = 12;
		    break;
		case 'n':
		    nval = 10;
		    break;
		case 'r':
		    nval = 13;
		    break;
		case 't':
		    nval = 9;
		    break;
		case 'u':
		    state = st_ucs;
		    ndig = 4;
		    break;
		case 'U':
		    state = st_ucs;
		    ndig = 8;
		    break;
		case 'v':
		    nval = 11;
		    break;
		case 'x':
		case 'X':
		    state = st_hex;
		    ndig = 2;
		    break;
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		    state = st_oct;
		    ndig = 2;	/* Up to two more digits */
		    nval = c - '0';
		    break;
                case '\0':
                    nval = '\\';
                    p--;        /* Reprocess; terminates string */
                    break;
		default:
		    nval = c;
		    break;
		}
                if (state == st_start)
                    EMIT(nval);
		break;

	    case st_oct:
		if (c >= '0' && c <= '7') {
		    nval = (nval << 3) + (c - '0');
                    if (--ndig)
                        break;  /* Might have more digits */
                } else {
		    p--;	/* Process this character again */
                }
                EMIT(nval);
                state = st_start;
                break;

	    case st_hex:
            case st_ucs:
		if (nasm_isxdigit(c)) {
		    nval = (nval << 4) + numvalue(c);
                    if (--ndig)
                        break;  /* Might have more digits */
                } else {
		    p--;	/* Process this character again */
                }

                if (unlikely(p <= escp))
                    EMIT(escp[-1]);
                else if (state == st_ucs)
                    EMIT_UTF8(nval);
                else
                    EMIT(nval);

                state = st_start;
		break;

            default:
                panic();
            }
        }
    } else if (bq == '\'' || bq == '\"') {
	/*
         * '...' or "..." string, NASM legacy style (no escapes of
         * * any kind, including collapsing double quote marks.)
         * We obviously can't get here if qstart == '\"'.
         */
        while ((c = *p++) && (c != bq))
            EMIT(c);
    } else {
	/* Not a quoted string, just return the input... */
        while ((c = *p++))
            EMIT(c);
    }

    /* Zero-terminate the output */
    *q = '\0';

    if (ctlmask & badctl)
        nasm_nonfatal("control character in string not allowed here");

    if (ep)
	*ep = (char *)p - 1;
    return (char *)q - str;
}
#undef EMIT

/*
 * Unquote any arbitrary string; may produce any bytes, including embedded
 * control- and NUL characters.
 */
size_t nasm_unquote(char *str, char **ep)
{
    return nasm_unquote_anystr(str, ep, 0, STR_NASM);
}

/*
 * Unquote a string intended to be used as a C string; most control
 * characters are rejected, including whitespace characters that
 * would imply line endings and so on.
 */
size_t nasm_unquote_cstr(char *str, char **ep)
{
    return nasm_unquote_anystr(str, ep, BADCTL, STR_NASM);
}

/*
 * Find the end of a quoted string; returns the pointer to the terminating
 * character (either the ending quote or the null character, if unterminated.)
 * If the input is not a quoted string, return NULL.
 * This applies to NASM style strings only.
 */
char *nasm_skip_string(const char *str)
{
    char bq;
    const char *p;
    char c;
    enum unq_state {
	st_start,
	st_backslash,
        st_done
    } state;

    bq = str[0];
    p = str+1;
    switch (bq) {
    case '\'':
    case '\"':
	/* '...' or "..." string */
        while ((c = *p++) && (c != bq))
            ;
        break;

    case '`':
	/* `...` string */
	state = st_start;
	while (state != st_done) {
            c = *p++;
	    switch (state) {
	    case st_start:
		switch (c) {
		case '\\':
		    state = st_backslash;
		    break;
		case '`':
                case '\0':
                    state = st_done;
                    break;
		default:
		    break;
		}
		break;

	    case st_backslash:
		/*
		 * Note: for the purpose of finding the end of the string,
		 * all successor states to st_backslash are functionally
		 * equivalent to st_start, since either a backslash or
		 * a backquote will force a return to the st_start state,
                 * and any possible multi-character state will terminate
                 * for any non-alphanumeric character.
		 */
		state = c ? st_start : st_done;
		break;

            default:
                panic();
	    }
	}
        break;

    default:
        /* Not a string at all... */
        return NULL;
    }
    return (char *)p - 1;
}
