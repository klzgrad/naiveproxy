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

#include "nctype.h"
#include <ctype.h>

/*
 * Table of tolower() results.  This avoids function calls
 * on some platforms.
 */
unsigned char nasm_tolower_tab[256];

static void tolower_tab_init(void)
{
    int i;

    for (i = 0; i < 256; i++)
	nasm_tolower_tab[i] = tolower(i);
}

/*
 * Table of character type flags; some are simply <ctype.h>,
 * some are NASM-specific.
 */

uint16_t nasm_ctype_tab[256];

#if !defined(HAVE_ISCNTRL) && !defined(iscntrl)
# define iscntrl(x) ((x) < 32)
#endif
#if !defined(HAVE_ISASCII) && !defined(isascii)
# define isascii(x) ((x) < 128)
#endif

static void ctype_tab_init(void)
{
    int i;

    for (i = 0; i < 256; i++) {
        enum nasm_ctype ct = 0;

        if (iscntrl(i))
            ct |= NCT_CTRL;

        if (isascii(i))
            ct |= NCT_ASCII;

        if (isspace(i) && i != '\n')
            ct |= NCT_SPACE;

        if (isalpha(i)) {
            ct |= (nasm_tolower(i) == i) ? NCT_LOWER : NCT_UPPER;
            ct |= NCT_ID|NCT_IDSTART;
        }

        if (isdigit(i))
            ct |= NCT_DIGIT|NCT_ID;

        if (isxdigit(i))
            ct |= NCT_HEX;

        /* Non-ASCII character, but no ctype returned (e.g. Unicode) */
        if (!ct && !ispunct(i))
            ct |= NCT_ID|NCT_IDSTART;

        nasm_ctype_tab[i] = ct;
    }

    nasm_ctype_tab['-']  |= NCT_MINUS;
    nasm_ctype_tab['$']  |= NCT_DOLLAR|NCT_ID;
    nasm_ctype_tab['_']  |= NCT_UNDER|NCT_ID|NCT_IDSTART;
    nasm_ctype_tab['.']  |= NCT_ID|NCT_IDSTART;
    nasm_ctype_tab['@']  |= NCT_ID|NCT_IDSTART;
    nasm_ctype_tab['?']  |= NCT_ID|NCT_IDSTART;
    nasm_ctype_tab['#']  |= NCT_ID;
    nasm_ctype_tab['~']  |= NCT_ID;
    nasm_ctype_tab['\''] |= NCT_QUOTE;
    nasm_ctype_tab['\"'] |= NCT_QUOTE;
    nasm_ctype_tab['`']  |= NCT_QUOTE;
}

void nasm_ctype_init(void)
{
    tolower_tab_init();
    ctype_tab_init();
}
