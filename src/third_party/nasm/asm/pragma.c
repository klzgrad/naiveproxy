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
 * Parse and handle [pragma] directives.  The preprocessor handles
 * %pragma preproc directives separately, all other namespaces are
 * simply converted to [pragma].
 */

#include "compiler.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#include "nasm.h"
#include "nasmlib.h"
#include "assemble.h"
#include "error.h"

static enum directive_result output_pragma(const struct pragma *pragma);
static enum directive_result limit_pragma(const struct pragma *pragma);

/*
 * Handle [pragma] directives.  [pragma] is generally produced by
 * the %pragma preprocessor directive, which simply passes on any
 * string that it finds *except* %pragma preproc.  The idea is
 * that pragmas are of the form:
 *
 * %pragma <facility> <opname> [<options>...]
 *
 * ... where "facility" can be either a generic facility or a backend
 * name.
 *
 * The following names are currently reserved for global facilities;
 * so far none of these have any defined pragmas at all:
 *
 * preproc	- preprocessor
 * limit	- limit setting
 * asm		- assembler
 * list		- listing generator
 * file		- generic file handling
 * input	- input file handling
 * output	- backend-independent output handling
 * debug	- backend-independent debug handling
 * ignore	- dummy pragma (can be used to "comment out")
 *
 * This function should generally not error out if it doesn't understand
 * what a pragma is for, for unknown arguments, etc; the whole point of
 * a pragma is that future releases might add new ones that should be
 * ignored rather than be an error.  Erroring out is acceptable for
 * known pragmas suffering from parsing errors and so on.
 *
 * Adding default-suppressed warnings would, however, be a good idea
 * at some point.
 */
static struct pragma_facility global_pragmas[] =
{
    { "asm",		NULL },
    { "limit",          limit_pragma },
    { "list",		NULL },
    { "file",		NULL },
    { "input",		NULL },

    /* None of these should actually happen due to special handling */
    { "preproc",	NULL }, /* Handled in the preprocessor by necessity */
    { "output",		NULL },
    { "debug",	        NULL },
    { "ignore",		NULL },
    { NULL, NULL }
};

/*
 * Search a pragma list for a known pragma facility and if so, invoke
 * the handler.  Return true if processing is complete.
 * The "default name", if set, matches the final NULL entry (used
 * for backends, so multiple backends can share the same list under
 * some circumstances.)
 */
static bool search_pragma_list(const struct pragma_facility *list,
                               const char *default_name,
                               pragma_handler generic_handler,
			       struct pragma *pragma)
{
    const struct pragma_facility *pf;
    enum directive_result rv;

    if (!list)
	return false;

    for (pf = list; pf->name; pf++) {
        if (!nasm_stricmp(pragma->facility_name, pf->name))
            goto found_it;
    }

    if (default_name && !nasm_stricmp(pragma->facility_name, default_name))
        goto found_it;

    return false;

found_it:
    pragma->facility = pf;

    /* If the handler is NULL all pragmas are unknown... */
    if (pf->handler)
        rv = pf->handler(pragma);
    else
        rv = DIRR_UNKNOWN;

    /* Is there an additional, applicable generic handler? */
    if (rv == DIRR_UNKNOWN && generic_handler)
        rv = generic_handler(pragma);

    switch (rv) {
    case DIRR_UNKNOWN:
        switch (pragma->opcode) {
        case D_none:
            nasm_error(ERR_WARNING|ERR_PASS2|ERR_WARN_BAD_PRAGMA,
                       "empty %%pragma %s", pragma->facility_name);
            break;
        default:
            nasm_error(ERR_WARNING|ERR_PASS2|ERR_WARN_UNKNOWN_PRAGMA,
                       "unknown %%pragma %s %s",
                       pragma->facility_name, pragma->opname);
            break;
        }
        break;

    case DIRR_OK:
    case DIRR_ERROR:
        break;                  /* Nothing to do */

    case DIRR_BADPARAM:
        /*
         * This one is an error.  Don't use it if forward compatibility
         * would be compromised, as opposed to an inherent error.
         */
        nasm_error(ERR_NONFATAL, "bad argument to %%pragma %s %s",
                   pragma->facility_name, pragma->opname);
        break;

    default:
        panic();
    }
    return true;
}

void process_pragma(char *str)
{
    struct pragma pragma;
    char *p;

    nasm_zero(pragma);

    pragma.facility_name = nasm_get_word(str, &p);
    if (!pragma.facility_name) {
	nasm_error(ERR_WARNING|ERR_PASS2|ERR_WARN_BAD_PRAGMA,
		   "empty pragma directive");
        return;                 /* Empty pragma */
    }

    /*
     * The facility "ignore" means just that; don't even complain of
     * the absence of an operation.
     */
    if (!nasm_stricmp(pragma.facility_name, "ignore"))
        return;

    /*
     * The "output" and "debug" facilities are aliases for the
     * current output and debug formats, respectively.
     */
    if (!nasm_stricmp(pragma.facility_name, "output"))
        pragma.facility_name = ofmt->shortname;
    if (!nasm_stricmp(pragma.facility_name, "debug"))
        pragma.facility_name = dfmt->shortname;

    pragma.opname = nasm_get_word(p, &p);
    if (!pragma.opname)
        pragma.opcode = D_none;
    else
        pragma.opcode = directive_find(pragma.opname);

    pragma.tail = nasm_trim_spaces(p);

    /* Look for a global pragma namespace */
    if (search_pragma_list(global_pragmas, NULL, NULL, &pragma))
	return;

    /* Look to see if it is an output backend pragma */
    if (search_pragma_list(ofmt->pragmas, ofmt->shortname,
                           output_pragma, &pragma))
	return;

    /* Look to see if it is a debug format pragma */
    if (search_pragma_list(dfmt->pragmas, dfmt->shortname, NULL, &pragma))
	return;

    /*
     * Note: it would be nice to warn for an unknown namespace,
     * but in order to do so we need to walk *ALL* the backends
     * in order to make sure we aren't dealing with a pragma that
     * is for another backend.  On the other hand, that could
     * also be a warning with a separate warning flag.
     *
     * Leave this for the future, however, the warning classes are
     * already defined for future compatibility.
     */
}

/*
 * Generic pragmas that apply to all output backends; these are handled
 * specially so they can be made selective based on the output format.
 */
static enum directive_result output_pragma(const struct pragma *pragma)
{
    switch (pragma->opcode) {
    case D_PREFIX:
    case D_GPREFIX:
        set_label_mangle(LM_GPREFIX, pragma->tail);
        return DIRR_OK;
    case D_SUFFIX:
    case D_GSUFFIX:
        set_label_mangle(LM_GSUFFIX, pragma->tail);
        return DIRR_OK;
    case D_LPREFIX:
        set_label_mangle(LM_LPREFIX, pragma->tail);
        return DIRR_OK;
    case D_LSUFFIX:
        set_label_mangle(LM_LSUFFIX, pragma->tail);
        return DIRR_OK;
    default:
        return DIRR_UNKNOWN;
    }
}

/*
 * %pragma limit to set resource limits
 */
static enum directive_result limit_pragma(const struct pragma *pragma)
{
    return nasm_set_limit(pragma->opname, pragma->tail);
}
