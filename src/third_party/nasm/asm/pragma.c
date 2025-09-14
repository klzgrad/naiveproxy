/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2019 The NASM Authors - All Rights Reserved
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

#include "nctype.h"

#include "nasm.h"
#include "nasmlib.h"
#include "assemble.h"
#include "error.h"
#include "listing.h"

static enum directive_result ignore_pragma(const struct pragma *pragma);
static enum directive_result output_pragma(const struct pragma *pragma);
static enum directive_result debug_pragma(const struct pragma *pragma);
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
    { "list",		list_pragma },
    { "file",		NULL },
    { "input",		NULL },
    { "output",		output_pragma },
    { "debug",	        debug_pragma },
    { "ignore",		ignore_pragma },

    /* This will never actually get this far... */
    { "preproc",	NULL }, /* Handled in the preprocessor by necessity */
    { NULL, NULL }
};

/*
 * Invoke a pragma handler
 */
static enum directive_result
call_pragma(const struct pragma_facility *pf, struct pragma *pragma)
{
    if (!pf || !pf->handler)
        return DIRR_UNKNOWN;

    pragma->facility = pf;
    return pf->handler(pragma);
}

/*
 * Search a pragma list for a known pragma facility and if so, invoke
 * the handler.  Return true if processing is complete.  The "default
 * name", *or def->name*, if set, matches the final NULL entry (used
 * for backends, so multiple backends can share the same list under
 * some circumstances, and the backends can implement common operations.)
 */
static enum directive_result
search_pragma_list(const struct pragma_facility *list,
                   const char *defaultname,
                   const struct pragma_facility *def,
                   const struct pragma *cpragma)
{
    const struct pragma_facility *pf = NULL;
    enum directive_result rv;
    bool facility_match, is_default;
    struct pragma pragma = *cpragma;
    const char *facname = pragma.facility_name;

    /* Is there a default facility and we match its name? */
    is_default = def && def->name && !nasm_stricmp(facname, def->name);
    facility_match = is_default;

    /*
     * Promote def->name to defaultname if both are set. This handles
     * e.g. output -> elf32 so that we can handle elf32-specific
     * directives in that handler.
     */
    if (defaultname) {
        if (is_default)
            facname = defaultname;
        else
            facility_match = !nasm_stricmp(facname, defaultname);
    }

    if (facname && list) {
        for (pf = list; pf->name; pf++) {
            if (!nasm_stricmp(facname, pf->name)) {
                facility_match = true;
                rv = call_pragma(pf, &pragma);
                if (rv != DIRR_UNKNOWN)
                    goto found_it;
            }
        }

        if (facility_match) {
            /*
             * Facility name match but no matching directive; handler in NULL
             * entry at end of list?
             */
            rv = call_pragma(pf, &pragma);
            if (rv != DIRR_UNKNOWN)
                goto found_it;
        }
    }

    if (facility_match) {
        /*
         * Facility match but still nothing: def->handler if it exists
         */
        rv = call_pragma(def, &pragma);
    } else {
        /*
         * No facility matched
         */
        return DIRR_UNKNOWN;
    }

    /*
     * Otherwise we found the facility but not any supported directive,
     * fall through...
     */

found_it:
    switch (rv) {
    case DIRR_UNKNOWN:
        switch (pragma.opcode) {
        case D_none:
            /*!
             *!pragma-bad [off] malformed \c{%pragma}
             *!=bad-pragma
             *!  warns about a malformed or otherwise unparsable
             *!  \c{%pragma} directive.
             */
            nasm_warn(ERR_PASS2|WARN_PRAGMA_BAD,
                       "empty %%pragma %s", pragma.facility_name);
            break;
        default:
            /*!
             *!pragma-unknown [off] unknown \c{%pragma} facility or directive
             *!=unknown-pragma
             *!  warns about an unknown \c{%pragma} directive.
             *!  This is not yet implemented for most cases.
             */
            nasm_warn(ERR_PASS2|WARN_PRAGMA_UNKNOWN,
                       "unknown %%pragma %s %s",
                       pragma.facility_name, pragma.opname);
            break;
        }
        rv = DIRR_ERROR;        /* Already printed an error message */
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
                   pragma.facility_name, pragma.opname);
        break;

    default:
        panic();
    }
    return rv;
}

/* This warning message is intended for future use */
/*!
 *!pragma-na [off] \c{%pragma} not applicable to this compilation
 *!=not-my-pragma
 *!  warns about a \c{%pragma} directive which is not applicable to
 *!  this particular assembly session.  This is not yet implemented.
 */

/* Naked %pragma */
/*!
 *!pragma-empty [off] empty \c{%pragma} directive
 *!  warns about a \c{%pragma} directive containing nothing.
 *!  This is treated identically to \c{%pragma ignore} except
 *!  for this optional warning.
 */
void process_pragma(char *str)
{
    const struct pragma_facility *pf;
    struct pragma pragma;
    char *p;

    nasm_zero(pragma);

    pragma.facility_name = nasm_get_word(str, &p);
    if (!pragma.facility_name) {
        /* Empty %pragma */
	nasm_warn(ERR_PASS2|WARN_PRAGMA_EMPTY,
		   "empty %%pragma directive, ignored");
        return;
    }

    pragma.opname = nasm_get_word(p, &p);
    if (!pragma.opname)
        pragma.opcode = D_none;
    else
        pragma.opcode = directive_find(pragma.opname);

    pragma.tail = nasm_trim_spaces(p);

    /*
     * Search the global pragma namespaces. This is done
     * as a loop rather than letting search_pragma_list()
     * just run, because we don't want to keep searching if
     * we have a facility match, thus we want to call
     * search_pragma_list() individually for each namespace.
     */
    for (pf = global_pragmas; pf->name; pf++) {
        if (search_pragma_list(NULL, NULL, pf, &pragma) != DIRR_UNKNOWN)
            return;
    }

    /* Is it an output pragma? */
    if (output_pragma(&pragma) != DIRR_UNKNOWN)
        return;

    /* Is it a debug pragma */
    if (debug_pragma(&pragma) != DIRR_UNKNOWN)
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

/* %pragma ignore */
static enum directive_result ignore_pragma(const struct pragma *pragma)
{
    (void)pragma;
    return DIRR_OK;             /* Even for D_none! */
}

/*
 * Process output and debug pragmas, by either list name or generic
 * name. Note that the output/debug format list can hook the default
 * names if they so choose.
 */
static enum directive_result output_pragma_common(const struct pragma *);
static enum directive_result output_pragma(const struct pragma *pragma)
{
    static const struct pragma_facility
        output_pragma_def = { "output", output_pragma_common };

    return search_pragma_list(ofmt->pragmas, ofmt->shortname,
                              &output_pragma_def, pragma);
}

/* Generic pragmas that apply to all output backends */
static enum directive_result output_pragma_common(const struct pragma *pragma)
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

static enum directive_result debug_pragma(const struct pragma *pragma)
{
    static const struct pragma_facility
        debug_pragma_def = { "debug", NULL };

    return search_pragma_list(dfmt->pragmas, dfmt->shortname,
                              &debug_pragma_def, pragma);
}

/*
 * %pragma limit to set resource limits
 */
static enum directive_result limit_pragma(const struct pragma *pragma)
{
    return nasm_set_limit(pragma->opname, pragma->tail);
}
