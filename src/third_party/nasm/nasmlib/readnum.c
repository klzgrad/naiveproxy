/* ----------------------------------------------------------------------- *
 *   
 *   Copyright 1996-2016 The NASM Authors - All Rights Reserved
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
 * nasmlib.c	library routines for the Netwide Assembler
 */

#include "compiler.h"

#include <ctype.h>

#include "nasmlib.h"
#include "error.h"
#include "nasm.h"               /* For globalbits */

#define lib_isnumchar(c)    (nasm_isalnum(c) || (c) == '$' || (c) == '_')

static int radix_letter(char c)
{
    switch (c) {
    case 'b': case 'B':
    case 'y': case 'Y':
	return 2;		/* Binary */
    case 'o': case 'O':
    case 'q': case 'Q':
	return 8;		/* Octal */
    case 'h': case 'H':
    case 'x': case 'X':
	return 16;		/* Hexadecimal */
    case 'd': case 'D':
    case 't': case 'T':
	return 10;		/* Decimal */
    default:
	return 0;		/* Not a known radix letter */
    }
}

int64_t readnum(const char *str, bool *error)
{
    const char *r = str, *q;
    int32_t pradix, sradix, radix;
    int plen, slen, len;
    uint64_t result, checklimit;
    int digit, last;
    bool warn = false;
    int sign = 1;

    *error = false;

    while (nasm_isspace(*r))
        r++;                    /* find start of number */

    /*
     * If the number came from make_tok_num (as a result of an %assign), it
     * might have a '-' built into it (rather than in a preceeding token).
     */
    if (*r == '-') {
        r++;
        sign = -1;
    }

    q = r;

    while (lib_isnumchar(*q))
        q++;                    /* find end of number */

    len = q-r;
    if (!len) {
	/* Not numeric */
	*error = true;
	return 0;
    }

    /*
     * Handle radix formats:
     *
     * 0<radix-letter><string>
     * $<string>		(hexadecimal)
     * <string><radix-letter>
     */
    pradix = sradix = 0;
    plen = slen = 0;

    if (len > 2 && *r == '0' && (pradix = radix_letter(r[1])) != 0)
	plen = 2;
    else if (len > 1 && *r == '$')
	pradix = 16, plen = 1;

    if (len > 1 && (sradix = radix_letter(q[-1])) != 0)
	slen = 1;

    if (pradix > sradix) {
	radix = pradix;
	r += plen;
    } else if (sradix > pradix) {
	radix = sradix;
	q -= slen;
    } else {
	/* Either decimal, or invalid -- if invalid, we'll trip up
	   further down. */
	radix = 10;
    }

    /*
     * `checklimit' must be 2**64 / radix. We can't do that in
     * 64-bit arithmetic, which we're (probably) using, so we
     * cheat: since we know that all radices we use are even, we
     * can divide 2**63 by radix/2 instead.
     */
    checklimit = UINT64_C(0x8000000000000000) / (radix >> 1);

    /*
     * Calculate the highest allowable value for the last digit of a
     * 64-bit constant... in radix 10, it is 6, otherwise it is 0
     */
    last = (radix == 10 ? 6 : 0);

    result = 0;
    while (*r && r < q) {
	if (*r != '_') {
	    if (*r < '0' || (*r > '9' && *r < 'A')
		|| (digit = numvalue(*r)) >= radix) {
		*error = true;
		return 0;
	    }
	    if (result > checklimit ||
		(result == checklimit && digit >= last)) {
		warn = true;
	    }

	    result = radix * result + digit;
	}
        r++;
    }

    if (warn)
        nasm_error(ERR_WARNING | ERR_PASS1 | ERR_WARN_NOV,
		   "numeric constant %s does not fit in 64 bits",
		   str);

    return result * sign;
}
