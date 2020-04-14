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
 * Parse and handle assembler directives
 */

#include "compiler.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#include "nasm.h"
#include "nasmlib.h"
#include "ilog2.h"
#include "error.h"
#include "float.h"
#include "stdscan.h"
#include "preproc.h"
#include "eval.h"
#include "assemble.h"
#include "outform.h"
#include "listing.h"
#include "labels.h"
#include "iflag.h"

struct cpunames {
    const char *name;
    unsigned int level;
    /* Eventually a table of features */
};

static iflag_t get_cpu(const char *value)
{
    iflag_t r;
    const struct cpunames *cpu;
    static const struct cpunames cpunames[] = {
        { "8086", IF_8086 },
        { "186",  IF_186  },
        { "286",  IF_286  },
        { "386",  IF_386  },
        { "486",  IF_486  },
        { "586",  IF_PENT },
        { "pentium", IF_PENT },
        { "pentiummmx", IF_PENT },
        { "686",  IF_P6 },
        { "p6",   IF_P6 },
        { "ppro", IF_P6 },
        { "pentiumpro", IF_P6 },
        { "p2", IF_P6 },        /* +MMX */
        { "pentiumii", IF_P6 },
        { "p3", IF_KATMAI },
        { "katmai", IF_KATMAI },
        { "p4", IF_WILLAMETTE },
        { "willamette", IF_WILLAMETTE },
        { "prescott", IF_PRESCOTT },
        { "x64", IF_X86_64 },
        { "x86-64", IF_X86_64 },
        { "ia64", IF_IA64 },
        { "ia-64", IF_IA64 },
        { "itanium", IF_IA64 },
        { "itanic", IF_IA64 },
        { "merced", IF_IA64 },
        { "any", IF_PLEVEL },
        { "default", IF_PLEVEL },
        { "all", IF_PLEVEL },
        { NULL, IF_PLEVEL }     /* Error and final default entry */
    };

    iflag_clear_all(&r);

    for (cpu = cpunames; cpu->name; cpu++) {
        if (!nasm_stricmp(value, cpu->name))
            break;
    }

    if (!cpu->name) {
        nasm_error(pass0 < 2 ? ERR_NONFATAL : ERR_FATAL,
                   "unknown 'cpu' type '%s'", value);
    }

    iflag_set_cpu(&r, cpu->level);
    return r;
}

static int get_bits(const char *value)
{
    int i = atoi(value);

    switch (i) {
    case 16:
        break;                  /* Always safe */
    case 32:
        if (!iflag_cpu_level_ok(&cpu, IF_386)) {
            nasm_error(ERR_NONFATAL,
                       "cannot specify 32-bit segment on processor below a 386");
            i = 16;
        }
        break;
    case 64:
        if (!iflag_cpu_level_ok(&cpu, IF_X86_64)) {
            nasm_error(ERR_NONFATAL,
                       "cannot specify 64-bit segment on processor below an x86-64");
            i = 16;
        }
        break;
    default:
        nasm_error(pass0 < 2 ? ERR_NONFATAL : ERR_FATAL,
                   "`%s' is not a valid segment size; must be 16, 32 or 64",
                   value);
        i = 16;
        break;
    }
    return i;
}

static enum directive parse_directive_line(char **directive, char **value)
{
    char *p, *q, *buf;

    buf = nasm_skip_spaces(*directive);

    /*
     * It should be enclosed in [ ].
     * XXX: we don't check there is nothing else on the remainder of the
     * line, except a possible comment.
     */
    if (*buf != '[')
        return D_none;
    q = strchr(buf, ']');
    if (!q)
        return D_corrupt;

    /*
     * Strip off the comments.  XXX: this doesn't account for quoted
     * strings inside a directive.  We should really strip the
     * comments in generic code, not here.  While we're at it, it
     * would be better to pass the backend a series of tokens instead
     * of a raw string, and actually process quoted strings for it,
     * like of like argv is handled in C.
     */
    p = strchr(buf, ';');
    if (p) {
        if (p < q) /* ouch! somewhere inside */
            return D_corrupt;
        *p = '\0';
    }

    /* no brace, no trailing spaces */
    *q = '\0';
    nasm_zap_spaces_rev(--q);

    /* directive */
    p = nasm_skip_spaces(++buf);
    q = nasm_skip_word(p);
    if (!q)
        return D_corrupt; /* sigh... no value there */
    *q = '\0';
    *directive = p;

    /* and value finally */
    p = nasm_skip_spaces(++q);
    *value = p;

    return directive_find(*directive);
}

/*
 * Process a line from the assembler and try to handle it if it
 * is a directive.  Return true if the line was handled (including
 * if it was an error), false otherwise.
 */
bool process_directives(char *directive)
{
    enum directive d;
    char *value, *p, *q, *special;
    struct tokenval tokval;
    bool bad_param = false;
    int pass2 = passn > 1 ? 2 : 1;
    enum label_type type;

    d = parse_directive_line(&directive, &value);

    switch (d) {
    case D_none:
        return D_none;      /* Not a directive */

    case D_corrupt:
	nasm_error(ERR_NONFATAL, "invalid directive line");
	break;

    default:			/* It's a backend-specific directive */
        switch (ofmt->directive(d, value, pass2)) {
        case DIRR_UNKNOWN:
            goto unknown;
        case DIRR_OK:
        case DIRR_ERROR:
            break;
        case DIRR_BADPARAM:
            bad_param = true;
            break;
        default:
            panic();
        }
        break;

    case D_unknown:
    unknown:
        nasm_error(pass0 < 2 ? ERR_NONFATAL : ERR_PANIC,
                   "unrecognised directive [%s]", directive);
        break;

    case D_SEGMENT:         /* [SEGMENT n] */
    case D_SECTION:
    {
	int sb = globalbits;
        int32_t seg = ofmt->section(value, pass2, &sb);

        if (seg == NO_SEG) {
            nasm_error(pass0 < 2 ? ERR_NONFATAL : ERR_PANIC,
                       "segment name `%s' not recognized", value);
        } else {
            globalbits = sb;
            switch_segment(seg);
        }
        break;
    }

    case D_SECTALIGN:       /* [SECTALIGN n] */
    {
	expr *e;

        if (*value) {
            stdscan_reset();
            stdscan_set(value);
            tokval.t_type = TOKEN_INVALID;
            e = evaluate(stdscan, NULL, &tokval, NULL, pass2, NULL);
            if (e) {
                uint64_t align = e->value;

		if (!is_power2(e->value)) {
                    nasm_error(ERR_NONFATAL,
                               "segment alignment `%s' is not power of two",
                               value);
		} else if (align > UINT64_C(0x7fffffff)) {
                    /*
                     * FIXME: Please make some sane message here
                     * ofmt should have some 'check' method which
                     * would report segment alignment bounds.
                     */
		    nasm_error(ERR_NONFATAL,
			       "absurdly large segment alignment `%s' (2^%d)",
			       value, ilog2_64(align));
                }

                /* callee should be able to handle all details */
                if (location.segment != NO_SEG)
                    ofmt->sectalign(location.segment, align);
            }
        }
        break;
    }

    case D_BITS:            /* [BITS bits] */
        globalbits = get_bits(value);
        break;

    case D_GLOBAL:          /* [GLOBAL|STATIC|EXTERN|COMMON symbol:special] */
        type = LBL_GLOBAL;
        goto symdef;
    case D_STATIC:
        type = LBL_STATIC;
        goto symdef;
    case D_EXTERN:
        type = LBL_EXTERN;
        goto symdef;
    case D_COMMON:
        type = LBL_COMMON;
        goto symdef;

    symdef:
    {
        bool validid = true;
        int64_t size = 0;
        char *sizestr;
        bool rn_error;

        if (*value == '$')
            value++;        /* skip initial $ if present */

        q = value;
        if (!isidstart(*q)) {
            validid = false;
        } else {
            q++;
            while (*q && *q != ':' && !nasm_isspace(*q)) {
                if (!isidchar(*q))
                    validid = false;
                q++;
            }
        }
        if (!validid) {
            nasm_error(ERR_NONFATAL,
                       "identifier expected after %s, got `%s'",
                       directive, value);
            break;
        }

        if (nasm_isspace(*q)) {
            *q++ = '\0';
            sizestr = q = nasm_skip_spaces(q);
            q = strchr(q, ':');
        } else {
            sizestr = NULL;
        }

        if (q && *q == ':') {
            *q++ = '\0';
            special = q;
        } else {
            special = NULL;
        }

        if (type == LBL_COMMON) {
            if (sizestr)
                size = readnum(sizestr, &rn_error);
            if (!sizestr || rn_error)
                nasm_error(ERR_NONFATAL,
                           "%s size specified in common declaration",
                           sizestr ? "invalid" : "no");
        } else if (sizestr) {
            nasm_error(ERR_NONFATAL, "invalid syntax in %s declaration",
                       directive);
        }

        if (!declare_label(value, type, special))
            break;
        
        if (type == LBL_COMMON || type == LBL_EXTERN)
            define_label(value, 0, size, false);

    	break;
    }

    case D_ABSOLUTE:        /* [ABSOLUTE address] */
    {
	expr *e;

        stdscan_reset();
        stdscan_set(value);
        tokval.t_type = TOKEN_INVALID;
        e = evaluate(stdscan, NULL, &tokval, NULL, pass2, NULL);
        if (e) {
            if (!is_reloc(e))
                nasm_error(pass0 ==
                           1 ? ERR_NONFATAL : ERR_PANIC,
                           "cannot use non-relocatable expression as "
                           "ABSOLUTE address");
            else {
                absolute.segment = reloc_seg(e);
                absolute.offset = reloc_value(e);
            }
        } else if (passn == 1)
            absolute.offset = 0x100;     /* don't go near zero in case of / */
        else
            nasm_panic("invalid ABSOLUTE address "
                       "in pass two");
        in_absolute = true;
        location.segment = NO_SEG;
        location.offset = absolute.offset;
        break;
    }

    case D_DEBUG:           /* [DEBUG] */
    {
        bool badid, overlong;
	char debugid[128];

        p = value;
        q = debugid;
        badid = overlong = false;
        if (!isidstart(*p)) {
            badid = true;
        } else {
            while (*p && !nasm_isspace(*p)) {
                if (q >= debugid + sizeof debugid - 1) {
                    overlong = true;
                    break;
                }
                if (!isidchar(*p))
                    badid = true;
                *q++ = *p++;
            }
            *q = 0;
        }
        if (badid) {
            nasm_error(passn == 1 ? ERR_NONFATAL : ERR_PANIC,
                       "identifier expected after DEBUG");
            break;
        }
        if (overlong) {
            nasm_error(passn == 1 ? ERR_NONFATAL : ERR_PANIC,
                       "DEBUG identifier too long");
            break;
        }
        p = nasm_skip_spaces(p);
        if (pass0 == 2)
            dfmt->debug_directive(debugid, p);
        break;
    }

    case D_WARNING:         /* [WARNING {+|-|*}warn-name] */
        if (!set_warning_status(value)) {
            nasm_error(ERR_WARNING|ERR_WARN_UNK_WARNING,
                       "unknown warning option: %s", value);
        }
        break;

    case D_CPU:         /* [CPU] */
        cpu = get_cpu(value);
        break;

    case D_LIST:        /* [LIST {+|-}] */
        value = nasm_skip_spaces(value);
        if (*value == '+') {
            user_nolist = false;
        } else {
            if (*value == '-') {
                user_nolist = true;
            } else {
                bad_param = true;
            }
        }
        break;

    case D_DEFAULT:         /* [DEFAULT] */
        stdscan_reset();
        stdscan_set(value);
        tokval.t_type = TOKEN_INVALID;
        if (stdscan(NULL, &tokval) != TOKEN_INVALID) {
            switch (tokval.t_integer) {
            case S_REL:
                globalrel = 1;
                break;
            case S_ABS:
                globalrel = 0;
                break;
            case P_BND:
                globalbnd = 1;
                break;
            case P_NOBND:
                globalbnd = 0;
                break;
            default:
                bad_param = true;
                break;
            }
        } else {
            bad_param = true;
        }
        break;

    case D_FLOAT:
        if (float_option(value)) {
            nasm_error(pass0 < 2 ? ERR_NONFATAL : ERR_PANIC,
                       "unknown 'float' directive: %s", value);
        }
        break;

    case D_PRAGMA:
        process_pragma(value);
        break;
    }


    /* A common error message */
    if (bad_param) {
        nasm_error(ERR_NONFATAL, "invalid parameter to [%s] directive",
                   directive);
    }

    return d != D_none;
}
