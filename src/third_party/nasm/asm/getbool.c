/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2025 The NASM Authors - All Rights Reserved */

#include "compiler.h"
#include "nasm.h"
#include "asmutil.h"
#include "stdscan.h"
#include "eval.h"

/*
 * 1. An expression (true if nonzero 0)
 * 2. The keywords true, on, yes for true
 * 3. The keywords false, off, no for false
 * 4. An empty line, for true
 *
 * This is equivalent to pp_get_boolean_option() outside of the
 * preprocessor.
 *
 * On error, return defval (usually the previous value)
 *
 * If str is NULL, return NULL without changing *val.
 */
char *get_boolean_option(const char *str, bool *val)
{
    static const char * const noyes[] = {
        "no", "yes",
        "false", "true",
        "off", "on"
    };
    struct tokenval tokval;
    expr *evalresult;
    char *p;
    int tt;

    if (!str)
        return NULL;

    str = nasm_skip_spaces(str);
    p = nasm_strdup(str);

    tokval.t_type  = TOKEN_INVALID;
    tokval.t_start = str;
    stdscan_reset(p);

    tt = stdscan(NULL, &tokval);
    if (tt == TOKEN_EOS || tt == ']' || tt == ',') {
        *val = true;
        goto done;
    }

    if (tt == TOKEN_ID) {
        size_t i;
        for (i = 0; i < ARRAY_SIZE(noyes); i++)
            if (!nasm_stricmp(tokval.t_charptr, noyes[i])) {
                *val = i & 1;
                goto done;
            }
    }

    evalresult = evaluate(stdscan, NULL, &tokval, NULL, true, NULL);

    if (!evalresult)
        goto done;

    if (!is_really_simple(evalresult)) {
        nasm_nonfatal("boolean flag expression must be a constant");
        goto done;
    }

    *val = reloc_value(evalresult) != 0;

done:
    str += nasm_skip_spaces(tokval.t_start) - p;
    nasm_free(p);
    return (char *)str;
}
