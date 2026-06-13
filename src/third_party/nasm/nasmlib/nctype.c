/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2018 The NASM Authors - All Rights Reserved */

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
