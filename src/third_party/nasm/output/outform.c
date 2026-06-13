/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2011 The NASM Authors - All Rights Reserved */

/*
 * outform.c	manages a list of output formats, and associates
 *		them with their relevant drivers. Also has a
 *		routine to find the correct driver given a name
 *		for it
 */

#include "compiler.h"


#define BUILD_DRIVERS_ARRAY
#include "outform.h"
#include "outlib.h"

const struct ofmt *ofmt_find(const char *name,
			     const struct ofmt_alias **ofmt_alias)
{
    const struct ofmt * const *ofp;
    const struct ofmt *of;
    unsigned int i;

    *ofmt_alias = NULL;

    /* primary targets first */
    for (ofp = drivers; (of = *ofp); ofp++) {
        if (!nasm_stricmp(name, of->shortname))
            return of;
    }

    /* lets walk thru aliases then */
    for (i = 0; i < ARRAY_SIZE(ofmt_aliases); i++) {
        if (ofmt_aliases[i].shortname &&
            !nasm_stricmp(name, ofmt_aliases[i].shortname)) {
            *ofmt_alias = &ofmt_aliases[i];
            return ofmt_aliases[i].ofmt;
        }
    }

    return NULL;
}

const struct dfmt *dfmt_find(const struct ofmt *ofmt, const char *name)
{
    const struct dfmt * const *dfp;
    const struct dfmt *df;

    for (dfp = ofmt->debug_formats; (df = *dfp); dfp++) {
        if (!nasm_stricmp(name, df->shortname))
            return df;
    }
    return NULL;
}

void ofmt_list(const struct ofmt *deffmt, FILE * fp)
{
    const struct ofmt * const *ofp, *of;
    unsigned int i;

    /* primary targets first */
    for (ofp = drivers; (of = *ofp); ofp++) {
        fprintf(fp, "       %-20s %s%s\n",
                of->shortname,
                of->fullname,
                of == deffmt ? " [default]" : "");
    }

    /* lets walk through aliases then */
    for (i = 0; i < ARRAY_SIZE(ofmt_aliases); i++) {
        if (!ofmt_aliases[i].shortname)
            continue;
        fprintf(fp, "       %-20s Legacy alias for \"%s\"\n",
                ofmt_aliases[i].shortname,
                ofmt_aliases[i].ofmt->shortname);
    }
}

void dfmt_list(FILE *fp)
{
    const struct ofmt * const *ofp;
    const struct ofmt *of;
    const struct dfmt * const *dfp;
    const struct dfmt *df;
    char prefixbuf[32];
    const char *prefix;

    for (ofp = drivers; (of = *ofp); ofp++) {
        if (of->debug_formats && of->debug_formats != null_debug_arr) {
            snprintf(prefixbuf, sizeof prefixbuf, "%s:",
                     of->shortname);
            prefix = prefixbuf;

            for (dfp = of->debug_formats; (df = *dfp); dfp++) {
                if (df != &null_debug_form)
                    fprintf(fp, "       %-10s %-9s %s%s\n",
                            prefix,
                            df->shortname, df->fullname,
                            df == of->default_dfmt ? " [default]" : "");
                prefix = "";
            }
        }
    }
}
