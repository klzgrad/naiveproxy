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
 * The Netwide Assembler main program module
 */

#include "compiler.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#include "nasm.h"
#include "nasmlib.h"
#include "error.h"
#include "saa.h"
#include "raa.h"
#include "float.h"
#include "stdscan.h"
#include "insns.h"
#include "preproc.h"
#include "parser.h"
#include "eval.h"
#include "assemble.h"
#include "labels.h"
#include "outform.h"
#include "listing.h"
#include "iflag.h"
#include "ver.h"

/*
 * This is the maximum number of optimization passes to do.  If we ever
 * find a case where the optimizer doesn't naturally converge, we might
 * have to drop this value so the assembler doesn't appear to just hang.
 */
#define MAX_OPTIMIZE (INT_MAX >> 1)

struct forwrefinfo {            /* info held on forward refs. */
    int lineno;
    int operand;
};

static void parse_cmdline(int, char **, int);
static void assemble_file(const char *, StrList *);
static bool is_suppressed_warning(int severity);
static bool skip_this_pass(int severity);
static void nasm_verror_gnu(int severity, const char *fmt, va_list args);
static void nasm_verror_vc(int severity, const char *fmt, va_list args);
static void nasm_verror_common(int severity, const char *fmt, va_list args);
static void usage(void);
static void help(char xopt);

static bool using_debug_info, opt_verbose_info;
static const char *debug_format;

#ifndef ABORT_ON_PANIC
# define ABORT_ON_PANIC 0
#endif
static bool abort_on_panic = ABORT_ON_PANIC;
static bool keep_all;

bool tasm_compatible_mode = false;
int pass0;
int64_t passn;
static int pass1, pass2;	/* XXX: Get rid of these, they are redundant */
int globalrel = 0;
int globalbnd = 0;

struct compile_time official_compile_time;

const char *inname;
const char *outname;
static const char *listname;
static const char *errname;

static int64_t globallineno;    /* for forward-reference tracking */

/* static int pass = 0; */
const struct ofmt *ofmt = &OF_DEFAULT;
const struct ofmt_alias *ofmt_alias = NULL;
const struct dfmt *dfmt;

static FILE *error_file;        /* Where to write error messages */

FILE *ofile = NULL;
struct optimization optimizing =
    { MAX_OPTIMIZE, OPTIM_ALL_ENABLED }; /* number of optimization passes to take */
static int cmd_sb = 16;    /* by default */

iflag_t cpu;
static iflag_t cmd_cpu;

struct location location;
bool in_absolute;                 /* Flag we are in ABSOLUTE seg */
struct location absolute;         /* Segment/offset inside ABSOLUTE */

static struct RAA *offsets;

static struct SAA *forwrefs;    /* keep track of forward references */
static const struct forwrefinfo *forwref;

static const struct preproc_ops *preproc;

#define OP_NORMAL           (1u << 0)
#define OP_PREPROCESS       (1u << 1)
#define OP_DEPEND           (1u << 2)

static unsigned int operating_mode;

/* Dependency flags */
static bool depend_emit_phony = false;
static bool depend_missing_ok = false;
static const char *depend_target = NULL;
static const char *depend_file = NULL;
StrList *depend_list;

static bool want_usage;
static bool terminate_after_phase;
bool user_nolist = false;

static char *quote_for_pmake(const char *str);
static char *quote_for_wmake(const char *str);
static char *(*quote_for_make)(const char *) = quote_for_pmake;

/*
 * Execution limits that can be set via a command-line option or %pragma
 */

#define LIMIT_MAX_VAL	(INT64_MAX >> 1) /* Effectively unlimited */

int64_t nasm_limit[LIMIT_MAX+1] =
{ LIMIT_MAX_VAL, 1000, 1000000, 1000000, 1000000, 2000000000 };

struct limit_info {
    const char *name;
    const char *help;
};
static const struct limit_info limit_info[LIMIT_MAX+1] = {
    { "passes", "total number of passes" },
    { "stalled-passes", "number of passes without forward progress" },
    { "macro-levels", "levels of macro expansion"},
    { "rep", "%rep count" },
    { "eval", "expression evaluation descent"},
    { "lines", "total source lines processed"}
};

enum directive_result
nasm_set_limit(const char *limit, const char *valstr)
{
    int i;
    int64_t val;
    bool rn_error;
    int errlevel;

    for (i = 0; i <= LIMIT_MAX; i++) {
        if (!nasm_stricmp(limit, limit_info[i].name))
            break;
    }
    if (i > LIMIT_MAX) {
        if (passn == 0)
            errlevel = ERR_WARNING|ERR_NOFILE|ERR_USAGE;
        else
            errlevel = ERR_WARNING|ERR_PASS1|ERR_WARN_UNKNOWN_PRAGMA;
        nasm_error(errlevel, "unknown limit: `%s'", limit);
        return DIRR_ERROR;
    }

    if (!nasm_stricmp(valstr, "unlimited")) {
        val = LIMIT_MAX_VAL;
    } else {
        val = readnum(valstr, &rn_error);
        if (rn_error || val < 0) {
            if (passn == 0)
                errlevel = ERR_WARNING|ERR_NOFILE|ERR_USAGE;
            else
                errlevel = ERR_WARNING|ERR_PASS1|ERR_WARN_BAD_PRAGMA;
            nasm_error(errlevel, "invalid limit value: `%s'", limit);
            return DIRR_ERROR;
        }
        if (val > LIMIT_MAX_VAL)
            val = LIMIT_MAX_VAL;
    }

    nasm_limit[i] = val;
    return DIRR_OK;
}

int64_t switch_segment(int32_t segment)
{
    location.segment = segment;
    if (segment == NO_SEG) {
        location.offset = absolute.offset;
        in_absolute = true;
    } else {
        location.offset = raa_read(offsets, segment);
        in_absolute = false;
    }
    return location.offset;
}

static void set_curr_offs(int64_t l_off)
{
        if (in_absolute)
            absolute.offset = l_off;
        else
            offsets = raa_write(offsets, location.segment, l_off);
}

static void increment_offset(int64_t delta)
{
    if (unlikely(delta == 0))
        return;

    location.offset += delta;
    set_curr_offs(location.offset);
}

static void nasm_fputs(const char *line, FILE * outfile)
{
    if (outfile) {
        fputs(line, outfile);
        putc('\n', outfile);
    } else
        puts(line);
}

static void define_macros_early(void)
{
    const struct compile_time * const oct = &official_compile_time;
    char temp[128];

    if (oct->have_local) {
        strftime(temp, sizeof temp, "__DATE__=\"%Y-%m-%d\"", &oct->local);
        preproc->pre_define(temp);
        strftime(temp, sizeof temp, "__DATE_NUM__=%Y%m%d", &oct->local);
        preproc->pre_define(temp);
        strftime(temp, sizeof temp, "__TIME__=\"%H:%M:%S\"", &oct->local);
        preproc->pre_define(temp);
        strftime(temp, sizeof temp, "__TIME_NUM__=%H%M%S", &oct->local);
        preproc->pre_define(temp);
    }

    if (oct->have_gm) {
        strftime(temp, sizeof temp, "__UTC_DATE__=\"%Y-%m-%d\"", &oct->gm);
        preproc->pre_define(temp);
        strftime(temp, sizeof temp, "__UTC_DATE_NUM__=%Y%m%d", &oct->gm);
        preproc->pre_define(temp);
        strftime(temp, sizeof temp, "__UTC_TIME__=\"%H:%M:%S\"", &oct->gm);
        preproc->pre_define(temp);
        strftime(temp, sizeof temp, "__UTC_TIME_NUM__=%H%M%S", &oct->gm);
        preproc->pre_define(temp);
    }

    if (oct->have_posix) {
        snprintf(temp, sizeof temp, "__POSIX_TIME__=%"PRId64, oct->posix);
        preproc->pre_define(temp);
    }
}

static void define_macros_late(void)
{
    char temp[128];

    /*
     * In case if output format is defined by alias
     * we have to put shortname of the alias itself here
     * otherwise ABI backward compatibility gets broken.
     */
    snprintf(temp, sizeof(temp), "__OUTPUT_FORMAT__=%s",
             ofmt_alias ? ofmt_alias->shortname : ofmt->shortname);
    preproc->pre_define(temp);
}

static void emit_dependencies(StrList *list)
{
    FILE *deps;
    int linepos, len;
    bool wmake = (quote_for_make == quote_for_wmake);
    const char *wrapstr, *nulltarget;
    struct strlist_entry *l;

    if (!list)
        return;

    wrapstr = wmake ? " &\n " : " \\\n ";
    nulltarget = wmake ? "\t%null\n" : "";

    if (depend_file && strcmp(depend_file, "-")) {
        deps = nasm_open_write(depend_file, NF_TEXT);
        if (!deps) {
            nasm_error(ERR_NONFATAL|ERR_NOFILE|ERR_USAGE,
                       "unable to write dependency file `%s'", depend_file);
            return;
        }
    } else {
        deps = stdout;
    }

    linepos = fprintf(deps, "%s :", depend_target);
    list_for_each(l, list->head) {
        char *file = quote_for_make(l->str);
        len = strlen(file);
        if (linepos + len > 62 && linepos > 1) {
            fputs(wrapstr, deps);
            linepos = 1;
        }
        fprintf(deps, " %s", file);
        linepos += len+1;
        nasm_free(file);
    }
    fprintf(deps, "\n\n");

    list_for_each(l, list->head) {
        if (depend_emit_phony) {
            char *file = quote_for_make(l->str);
            fprintf(deps, "%s :\n%s\n", file, nulltarget);
            nasm_free(file);
        }
    }

    strlist_free(list);

    if (deps != stdout)
        fclose(deps);
}

/* Convert a struct tm to a POSIX-style time constant */
static int64_t make_posix_time(const struct tm *tm)
{
    int64_t t;
    int64_t y = tm->tm_year;

    /* See IEEE 1003.1:2004, section 4.14 */

    t = (y-70)*365 + (y-69)/4 - (y-1)/100 + (y+299)/400;
    t += tm->tm_yday;
    t *= 24;
    t += tm->tm_hour;
    t *= 60;
    t += tm->tm_min;
    t *= 60;
    t += tm->tm_sec;

    return t;
}

static void timestamp(void)
{
    struct compile_time * const oct = &official_compile_time;
#if 1
    // Chromium patch: Builds should be deterministic and not embed timestamps.
    memset(oct, 0, sizeof(official_compile_time));
#else
    const struct tm *tp, *best_gm;

    time(&oct->t);

    best_gm = NULL;

    tp = localtime(&oct->t);
    if (tp) {
        oct->local = *tp;
        best_gm = &oct->local;
        oct->have_local = true;
    }

    tp = gmtime(&oct->t);
    if (tp) {
        oct->gm = *tp;
        best_gm = &oct->gm;
        oct->have_gm = true;
        if (!oct->have_local)
            oct->local = oct->gm;
    } else {
        oct->gm = oct->local;
    }

    if (best_gm) {
        oct->posix = make_posix_time(best_gm);
        oct->have_posix = true;
    }
#endif
}

int main(int argc, char **argv)
{
    timestamp();

    iflag_set_default_cpu(&cpu);
    iflag_set_default_cpu(&cmd_cpu);

    pass0 = 0;
    want_usage = terminate_after_phase = false;
    nasm_set_verror(nasm_verror_gnu);

    error_file = stderr;

    tolower_init();
    src_init();

    /*
     * We must call init_labels() before the command line parsing,
     * because we may be setting prefixes/suffixes from the command
     * line.
     */
    init_labels();

    offsets = raa_init();
    forwrefs = saa_init((int32_t)sizeof(struct forwrefinfo));

    preproc = &nasmpp;
    operating_mode = OP_NORMAL;

    parse_cmdline(argc, argv, 1);
    if (terminate_after_phase) {
        if (want_usage)
            usage();
        return 1;
    }

    /*
     * Define some macros dependent on the runtime, but not
     * on the command line (as those are scanned in cmdline pass 2.)
     */
    preproc->init();
    define_macros_early();

    parse_cmdline(argc, argv, 2);
    if (terminate_after_phase) {
        if (want_usage)
            usage();
        return 1;
    }

    /* Save away the default state of warnings */
    memcpy(warning_state_init, warning_state, sizeof warning_state);

    if (!using_debug_info) {
        /* No debug info, redirect to the null backend (empty stubs) */
        dfmt = &null_debug_form;
    } else if (!debug_format) {
        /* Default debug format for this backend */
	dfmt = ofmt->default_dfmt;
    } else {
        dfmt = dfmt_find(ofmt, debug_format);
        if (!dfmt) {
            nasm_fatal_fl(ERR_NOFILE | ERR_USAGE,
                       "unrecognized debug format `%s' for"
                       " output format `%s'",
                       debug_format, ofmt->shortname);
        }
    }

    if (ofmt->stdmac)
        preproc->extra_stdmac(ofmt->stdmac);

    /*
     * If no output file name provided and this
     * is a preprocess mode, we're perfectly
     * fine to output into stdout.
     */
    if (!outname) {
        if (!(operating_mode & OP_PREPROCESS))
            outname = filename_set_extension(inname, ofmt->extension);
    }

    /* define some macros dependent of command-line */
    define_macros_late();

    if (depend_file || (operating_mode & OP_DEPEND))
        depend_list = strlist_allocate();

    if (!depend_target)
        depend_target = quote_for_make(outname);

    if (operating_mode & OP_DEPEND) {
            char *line;

            if (depend_missing_ok)
                preproc->include_path(NULL);    /* "assume generated" */

            preproc->reset(inname, 0, depend_list);
            ofile = NULL;
            while ((line = preproc->getline()))
                nasm_free(line);
            preproc->cleanup(0);
    } else if (operating_mode & OP_PREPROCESS) {
            char *line;
            const char *file_name = NULL;
            int32_t prior_linnum = 0;
            int lineinc = 0;

            if (outname) {
                ofile = nasm_open_write(outname, NF_TEXT);
                if (!ofile)
                    nasm_fatal_fl(ERR_NOFILE,
                                 "unable to open output file `%s'",
                                 outname);
            } else
                ofile = NULL;

            location.known = false;

            /* pass = 1; */
            preproc->reset(inname, 3, depend_list);

	    /* Revert all warnings to the default state */
	    memcpy(warning_state, warning_state_init, sizeof warning_state);

            while ((line = preproc->getline())) {
                /*
                 * We generate %line directives if needed for later programs
                 */
                int32_t linnum = prior_linnum += lineinc;
                int altline = src_get(&linnum, &file_name);
                if (altline) {
                    if (altline == 1 && lineinc == 1)
                        nasm_fputs("", ofile);
                    else {
                        lineinc = (altline != -1 || lineinc != 1);
                        fprintf(ofile ? ofile : stdout,
                                "%%line %"PRId32"+%d %s\n", linnum, lineinc,
                                file_name);
                    }
                    prior_linnum = linnum;
                }
                nasm_fputs(line, ofile);
                nasm_free(line);
            }
            preproc->cleanup(0);
            if (ofile)
                fclose(ofile);
            if (ofile && terminate_after_phase && !keep_all)
                remove(outname);
            ofile = NULL;
    }

    if (operating_mode & OP_NORMAL) {
        ofile = nasm_open_write(outname, (ofmt->flags & OFMT_TEXT) ? NF_TEXT : NF_BINARY);
        if (!ofile)
            nasm_fatal_fl(ERR_NOFILE,
                       "unable to open output file `%s'", outname);

        ofmt->init();
        dfmt->init();

        assemble_file(inname, depend_list);

        if (!terminate_after_phase) {
            ofmt->cleanup();
            cleanup_labels();
            fflush(ofile);
            if (ferror(ofile)) {
                nasm_error(ERR_NONFATAL|ERR_NOFILE,
                           "write error on output file `%s'", outname);
                terminate_after_phase = true;
            }
        }

        if (ofile) {
            fclose(ofile);
            if (terminate_after_phase && !keep_all)
                remove(outname);
            ofile = NULL;
        }
    }

    if (depend_list && !terminate_after_phase)
        emit_dependencies(depend_list);

    if (want_usage)
        usage();

    raa_free(offsets);
    saa_free(forwrefs);
    eval_cleanup();
    stdscan_cleanup();
    src_free();

    return terminate_after_phase;
}

/*
 * Get a parameter for a command line option.
 * First arg must be in the form of e.g. -f...
 */
static char *get_param(char *p, char *q, bool *advance)
{
    *advance = false;
    if (p[2]) /* the parameter's in the option */
        return nasm_skip_spaces(p + 2);
    if (q && q[0]) {
        *advance = true;
        return q;
    }
    nasm_error(ERR_NONFATAL | ERR_NOFILE | ERR_USAGE,
                 "option `-%c' requires an argument", p[1]);
    return NULL;
}

/*
 * Copy a filename
 */
static void copy_filename(const char **dst, const char *src, const char *what)
{
    if (*dst)
        nasm_fatal("more than one %s file specified: %s\n", what, src);

    *dst = nasm_strdup(src);
}

/*
 * Convert a string to a POSIX make-safe form
 */
static char *quote_for_pmake(const char *str)
{
    const char *p;
    char *os, *q;

    size_t n = 1; /* Terminating zero */
    size_t nbs = 0;

    if (!str)
        return NULL;

    for (p = str; *p; p++) {
        switch (*p) {
        case ' ':
        case '\t':
            /* Convert N backslashes + ws -> 2N+1 backslashes + ws */
            n += nbs + 2;
            nbs = 0;
            break;
        case '$':
        case '#':
            nbs = 0;
            n += 2;
            break;
        case '\\':
            nbs++;
            n++;
            break;
        default:
            nbs = 0;
            n++;
            break;
        }
    }

    /* Convert N backslashes at the end of filename to 2N backslashes */
    if (nbs)
        n += nbs;

    os = q = nasm_malloc(n);

    nbs = 0;
    for (p = str; *p; p++) {
        switch (*p) {
        case ' ':
        case '\t':
            while (nbs--)
                *q++ = '\\';
            *q++ = '\\';
            *q++ = *p;
            break;
        case '$':
            *q++ = *p;
            *q++ = *p;
            nbs = 0;
            break;
        case '#':
            *q++ = '\\';
            *q++ = *p;
            nbs = 0;
            break;
        case '\\':
            *q++ = *p;
            nbs++;
            break;
        default:
            *q++ = *p;
            nbs = 0;
            break;
        }
    }
    while (nbs--)
        *q++ = '\\';

    *q = '\0';

    return os;
}

/*
 * Convert a string to a Watcom make-safe form
 */
static char *quote_for_wmake(const char *str)
{
    const char *p;
    char *os, *q;
    bool quote = false;

    size_t n = 1; /* Terminating zero */

    if (!str)
        return NULL;

    for (p = str; *p; p++) {
        switch (*p) {
        case ' ':
        case '\t':
        case '&':
            quote = true;
            n++;
            break;
        case '\"':
            quote = true;
            n += 2;
            break;
        case '$':
        case '#':
            n += 2;
            break;
        default:
            n++;
            break;
        }
    }

    if (quote)
        n += 2;

    os = q = nasm_malloc(n);

    if (quote)
        *q++ = '\"';

    for (p = str; *p; p++) {
        switch (*p) {
        case '$':
        case '#':
            *q++ = '$';
            *q++ = *p;
            break;
        case '\"':
            *q++ = *p;
            *q++ = *p;
            break;
        default:
            *q++ = *p;
            break;
        }
    }

    if (quote)
        *q++ = '\"';

    *q = '\0';

    return os;
}

enum text_options {
    OPT_BOGUS,
    OPT_VERSION,
    OPT_HELP,
    OPT_ABORT_ON_PANIC,
    OPT_MANGLE,
    OPT_INCLUDE,
    OPT_PRAGMA,
    OPT_BEFORE,
    OPT_LIMIT,
    OPT_KEEP_ALL
};
struct textargs {
    const char *label;
    enum text_options opt;
    bool need_arg;
    int pvt;
};
static const struct textargs textopts[] = {
    {"v", OPT_VERSION, false, 0},
    {"version", OPT_VERSION, false, 0},
    {"help",     OPT_HELP,  false, 0},
    {"abort-on-panic", OPT_ABORT_ON_PANIC, false, 0},
    {"prefix",   OPT_MANGLE, true, LM_GPREFIX},
    {"postfix",  OPT_MANGLE, true, LM_GSUFFIX},
    {"gprefix",  OPT_MANGLE, true, LM_GPREFIX},
    {"gpostfix", OPT_MANGLE, true, LM_GSUFFIX},
    {"lprefix",  OPT_MANGLE, true, LM_LPREFIX},
    {"lpostfix", OPT_MANGLE, true, LM_LSUFFIX},
    {"include",  OPT_INCLUDE, true, 0},
    {"pragma",   OPT_PRAGMA,  true, 0},
    {"before",   OPT_BEFORE,  true, 0},
    {"limit-",   OPT_LIMIT,   true, 0},
    {"keep-all", OPT_KEEP_ALL, false, 0},
    {NULL, OPT_BOGUS, false, 0}
};

static void show_version(void)
{
    printf("NASM version %s%s\n",
           nasm_version, nasm_compile_options);
    exit(0);
}

static bool stopoptions = false;
static bool process_arg(char *p, char *q, int pass)
{
    char *param;
    bool advance = false;

    if (!p || !p[0])
        return false;

    if (p[0] == '-' && !stopoptions) {
        if (strchr("oOfpPdDiIlFXuUZwW", p[1])) {
            /* These parameters take values */
            if (!(param = get_param(p, q, &advance)))
                return advance;
        }

        switch (p[1]) {
        case 's':
            if (pass == 1)
                error_file = stdout;
            break;

        case 'o':       /* output file */
            if (pass == 2)
                copy_filename(&outname, param, "output");
            break;

        case 'f':       /* output format */
            if (pass == 1) {
                ofmt = ofmt_find(param, &ofmt_alias);
                if (!ofmt) {
                    nasm_fatal_fl(ERR_NOFILE | ERR_USAGE,
                               "unrecognised output format `%s' - "
                               "use -hf for a list", param);
                }
            }
            break;

        case 'O':       /* Optimization level */
            if (pass == 2) {
                int opt;

                if (!*param) {
                    /* Naked -O == -Ox */
                    optimizing.level = MAX_OPTIMIZE;
                } else {
                    while (*param) {
                        switch (*param) {
                        case '0': case '1': case '2': case '3': case '4':
                        case '5': case '6': case '7': case '8': case '9':
                            opt = strtoul(param, &param, 10);

                            /* -O0 -> optimizing.level == -1, 0.98 behaviour */
                            /* -O1 -> optimizing.level == 0, 0.98.09 behaviour */
                            if (opt < 2)
                                optimizing.level = opt - 1;
                            else
                                optimizing.level = opt;
                            break;

                        case 'v':
                        case '+':
                        param++;
                        opt_verbose_info = true;
                        break;

                        case 'x':
                            param++;
                            optimizing.level = MAX_OPTIMIZE;
                            break;

                        default:
                            nasm_fatal("unknown optimization option -O%c\n",
                                       *param);
                            break;
                        }
                    }
                    if (optimizing.level > MAX_OPTIMIZE)
                        optimizing.level = MAX_OPTIMIZE;
                }
            }
            break;

        case 'p':       /* pre-include */
        case 'P':
            if (pass == 2)
                preproc->pre_include(param);
            break;

        case 'd':       /* pre-define */
        case 'D':
            if (pass == 2)
                preproc->pre_define(param);
            break;

        case 'u':       /* un-define */
        case 'U':
            if (pass == 2)
                preproc->pre_undefine(param);
            break;

        case 'i':       /* include search path */
        case 'I':
            if (pass == 2)
                preproc->include_path(param);
            break;

        case 'l':       /* listing file */
            if (pass == 2)
                copy_filename(&listname, param, "listing");
            break;

        case 'Z':       /* error messages file */
            if (pass == 1)
                copy_filename(&errname, param, "error");
            break;

        case 'F':       /* specify debug format */
            if (pass == 2) {
                using_debug_info = true;
                debug_format = param;
            }
            break;

        case 'X':       /* specify error reporting format */
            if (pass == 1) {
                if (nasm_stricmp("vc", param) == 0)
                    nasm_set_verror(nasm_verror_vc);
                else if (nasm_stricmp("gnu", param) == 0)
                    nasm_set_verror(nasm_verror_gnu);
                else
                    nasm_fatal_fl(ERR_NOFILE | ERR_USAGE,
                               "unrecognized error reporting format `%s'",
                               param);
            }
            break;

        case 'g':
            if (pass == 2) {
                using_debug_info = true;
                if (p[2])
                    debug_format = nasm_skip_spaces(p + 2);
            }
            break;

        case 'h':
            help(p[2]);
            exit(0);    /* never need usage message here */
            break;

        case 'y':
            printf("\nvalid debug formats for '%s' output format are"
                   " ('*' denotes default):\n", ofmt->shortname);
            dfmt_list(ofmt, stdout);
            exit(0);
            break;

        case 't':
            if (pass == 2)
                tasm_compatible_mode = true;
            break;

        case 'v':
            show_version();
            break;

        case 'e':       /* preprocess only */
        case 'E':
            if (pass == 1)
                operating_mode = OP_PREPROCESS;
            break;

        case 'a':       /* assemble only - don't preprocess */
            if (pass == 1)
                preproc = &preproc_nop;
            break;

        case 'w':
        case 'W':
            if (pass == 2) {
                if (!set_warning_status(param)) {
                    nasm_error(ERR_WARNING|ERR_NOFILE|ERR_WARN_UNK_WARNING,
			       "unknown warning option: %s", param);
                }
            }
        break;

        case 'M':
            if (pass == 1) {
                switch (p[2]) {
                case 'W':
                    quote_for_make = quote_for_wmake;
                    break;
                case 'D':
                case 'F':
                case 'T':
                case 'Q':
                    advance = true;
                    break;
                default:
                    break;
                }
            } else {
                switch (p[2]) {
                case 0:
                    operating_mode = OP_DEPEND;
                    break;
                case 'G':
                    operating_mode = OP_DEPEND;
                    depend_missing_ok = true;
                    break;
                case 'P':
                    depend_emit_phony = true;
                    break;
                case 'D':
                    operating_mode = OP_NORMAL;
                    depend_file = q;
                    advance = true;
                    break;
                case 'F':
                    depend_file = q;
                    advance = true;
                    break;
                case 'T':
                    depend_target = q;
                    advance = true;
                    break;
                case 'Q':
                    depend_target = quote_for_make(q);
                    advance = true;
                    break;
                case 'W':
                    /* handled in pass 1 */
                    break;
                default:
                    nasm_error(ERR_NONFATAL|ERR_NOFILE|ERR_USAGE,
                               "unknown dependency option `-M%c'", p[2]);
                    break;
                }
            }
            if (advance && (!q || !q[0])) {
                nasm_error(ERR_NONFATAL|ERR_NOFILE|ERR_USAGE,
                           "option `-M%c' requires a parameter", p[2]);
                break;
            }
            break;

        case '-':
            {
                const struct textargs *tx;
                size_t olen, plen;
                char *eqsave;

                p += 2;

                if (!*p) {        /* -- => stop processing options */
                    stopoptions = true;
                    break;
                }

                plen = strlen(p);
                for (tx = textopts; tx->label; tx++) {
                    olen = strlen(tx->label);

                    if (olen > plen)
                        continue;

                    if (nasm_memicmp(p, tx->label, olen))
                        continue;

                    if (tx->label[olen-1] == '-')
                        break;  /* Incomplete option */

                    if (!p[olen] || p[olen] == '=')
                        break;  /* Complete option */
                }

                if (!tx->label) {
                    nasm_error(ERR_NONFATAL | ERR_NOFILE | ERR_USAGE,
                               "unrecognized option `--%s'", p);
                }

                eqsave = param = strchr(p+olen, '=');
                if (param)
                    *param++ = '\0';

                if (tx->need_arg) {
                    if (!param) {
                        param = q;
                        advance = true;
                    }

                    /* Note: a null string is a valid parameter */
                    if (!param) {
                        nasm_error(ERR_NONFATAL | ERR_NOFILE | ERR_USAGE,
                                   "option `--%s' requires an argument",
                                   p);
                        break;
                    }
                } else {
                    if (param) {
                        nasm_error(ERR_NONFATAL | ERR_NOFILE | ERR_USAGE,
                                   "option `--%s' does not take an argument",
                                   p);

                    }
                }

                switch (tx->opt) {
                case OPT_VERSION:
                    show_version();
                    break;
                case OPT_ABORT_ON_PANIC:
                    abort_on_panic = true;
                    break;
                case OPT_MANGLE:
                    if (pass == 2)
                        set_label_mangle(tx->pvt, param);
                    break;
                case OPT_INCLUDE:
                    if (pass == 2)
                        preproc->pre_include(q);
                    break;
                case OPT_PRAGMA:
                    if (pass == 2)
                        preproc->pre_command("pragma", param);
                    break;
                case OPT_BEFORE:
                    if (pass == 2)
                        preproc->pre_command(NULL, param);
                    break;
                case OPT_LIMIT:
                    if (pass == 2)
                        nasm_set_limit(p+olen, param);
                    break;
                case OPT_KEEP_ALL:
                    keep_all = true;
                    break;
                case OPT_HELP:
                    help(0);
                    exit(0);
                default:
                    panic();
                }

                if (eqsave)
                    *eqsave = '='; /* Restore = argument separator */

                break;
            }

        default:
            nasm_error(ERR_NONFATAL | ERR_NOFILE | ERR_USAGE,
                       "unrecognised option `-%c'", p[1]);
            break;
        }
    } else if (pass == 2) {
        /* In theory we could allow multiple input files... */
        copy_filename(&inname, p, "input");
    }

    return advance;
}

#define ARG_BUF_DELTA 128

static void process_respfile(FILE * rfile, int pass)
{
    char *buffer, *p, *q, *prevarg;
    int bufsize, prevargsize;

    bufsize = prevargsize = ARG_BUF_DELTA;
    buffer = nasm_malloc(ARG_BUF_DELTA);
    prevarg = nasm_malloc(ARG_BUF_DELTA);
    prevarg[0] = '\0';

    while (1) {                 /* Loop to handle all lines in file */
        p = buffer;
        while (1) {             /* Loop to handle long lines */
            q = fgets(p, bufsize - (p - buffer), rfile);
            if (!q)
                break;
            p += strlen(p);
            if (p > buffer && p[-1] == '\n')
                break;
            if (p - buffer > bufsize - 10) {
                int offset;
                offset = p - buffer;
                bufsize += ARG_BUF_DELTA;
                buffer = nasm_realloc(buffer, bufsize);
                p = buffer + offset;
            }
        }

        if (!q && p == buffer) {
            if (prevarg[0])
                process_arg(prevarg, NULL, pass);
            nasm_free(buffer);
            nasm_free(prevarg);
            return;
        }

        /*
         * Play safe: remove CRs, LFs and any spurious ^Zs, if any of
         * them are present at the end of the line.
         */
        *(p = &buffer[strcspn(buffer, "\r\n\032")]) = '\0';

        while (p > buffer && nasm_isspace(p[-1]))
            *--p = '\0';

        p = nasm_skip_spaces(buffer);

        if (process_arg(prevarg, p, pass))
            *p = '\0';

        if ((int) strlen(p) > prevargsize - 10) {
            prevargsize += ARG_BUF_DELTA;
            prevarg = nasm_realloc(prevarg, prevargsize);
        }
        strncpy(prevarg, p, prevargsize);
    }
}

/* Function to process args from a string of args, rather than the
 * argv array. Used by the environment variable and response file
 * processing.
 */
static void process_args(char *args, int pass)
{
    char *p, *q, *arg, *prevarg;
    char separator = ' ';

    p = args;
    if (*p && *p != '-')
        separator = *p++;
    arg = NULL;
    while (*p) {
        q = p;
        while (*p && *p != separator)
            p++;
        while (*p == separator)
            *p++ = '\0';
        prevarg = arg;
        arg = q;
        if (process_arg(prevarg, arg, pass))
            arg = NULL;
    }
    if (arg)
        process_arg(arg, NULL, pass);
}

static void process_response_file(const char *file, int pass)
{
    char str[2048];
    FILE *f = nasm_open_read(file, NF_TEXT);
    if (!f) {
        perror(file);
        exit(-1);
    }
    while (fgets(str, sizeof str, f)) {
        process_args(str, pass);
    }
    fclose(f);
}

static void parse_cmdline(int argc, char **argv, int pass)
{
    FILE *rfile;
    char *envreal, *envcopy = NULL, *p;
    int i;

    /* Initialize all the warnings to their default state */
    for (i = 0; i < ERR_WARN_ALL; i++) {
        warning_state_init[i] = warning_state[i] =
	    warnings[i].enabled ? WARN_ST_ENABLED : 0;
    }

    /*
     * First, process the NASMENV environment variable.
     */
    envreal = getenv("NASMENV");
    if (envreal) {
        envcopy = nasm_strdup(envreal);
        process_args(envcopy, pass);
        nasm_free(envcopy);
    }

    /*
     * Now process the actual command line.
     */
    while (--argc) {
        bool advance;
        argv++;
        if (argv[0][0] == '@') {
            /*
             * We have a response file, so process this as a set of
             * arguments like the environment variable. This allows us
             * to have multiple arguments on a single line, which is
             * different to the -@resp file processing below for regular
             * NASM.
             */
            process_response_file(argv[0]+1, pass);
            argc--;
            argv++;
        }
        if (!stopoptions && argv[0][0] == '-' && argv[0][1] == '@') {
            p = get_param(argv[0], argc > 1 ? argv[1] : NULL, &advance);
            if (p) {
                rfile = nasm_open_read(p, NF_TEXT);
                if (rfile) {
                    process_respfile(rfile, pass);
                    fclose(rfile);
                } else
                    nasm_error(ERR_NONFATAL | ERR_NOFILE | ERR_USAGE,
                                 "unable to open response file `%s'", p);
            }
        } else
            advance = process_arg(argv[0], argc > 1 ? argv[1] : NULL, pass);
        argv += advance, argc -= advance;
    }

    /*
     * Look for basic command line typos. This definitely doesn't
     * catch all errors, but it might help cases of fumbled fingers.
     */
    if (pass != 2)
        return;

    if (!inname)
        nasm_fatal_fl(ERR_NOFILE | ERR_USAGE, "no input file specified");

    else if ((errname && !strcmp(inname, errname)) ||
             (outname && !strcmp(inname, outname)) ||
             (listname &&  !strcmp(inname, listname))  ||
             (depend_file && !strcmp(inname, depend_file)))
        nasm_fatal_fl(ERR_USAGE, "will not overwrite input file");

    if (errname) {
        error_file = nasm_open_write(errname, NF_TEXT);
        if (!error_file) {
            error_file = stderr;        /* Revert to default! */
            nasm_fatal_fl(ERR_NOFILE | ERR_USAGE,
                       "cannot open file `%s' for error messages",
                       errname);
        }
    }
}

static void assemble_file(const char *fname, StrList *depend_list)
{
    char *line;
    insn output_ins;
    int i;
    uint64_t prev_offset_changed;
    int64_t stall_count = 0; /* Make sure we make forward progress... */

    switch (cmd_sb) {
    case 16:
        break;
    case 32:
        if (!iflag_cpu_level_ok(&cmd_cpu, IF_386))
            nasm_fatal("command line: 32-bit segment size requires a higher cpu");
        break;
    case 64:
        if (!iflag_cpu_level_ok(&cmd_cpu, IF_X86_64))
            nasm_fatal("command line: 64-bit segment size requires a higher cpu");
        break;
    default:
        panic();
        break;
    }

    prev_offset_changed = nasm_limit[LIMIT_PASSES];
    for (passn = 1; pass0 <= 2; passn++) {
        pass1 = pass0 == 2 ? 2 : 1;     /* 1, 1, 1, ..., 1, 2 */
        pass2 = passn > 1  ? 2 : 1;     /* 1, 2, 2, ..., 2, 2 */
        /* pass0                           0, 0, 0, ..., 1, 2 */

        globalbits = cmd_sb;  /* set 'bits' to command line default */
        cpu = cmd_cpu;
        if (pass0 == 2) {
	    lfmt->init(listname);
        } else if (passn == 1 && listname && !keep_all) {
            /* Remove the list file in case we die before the output pass */
            remove(listname);
        }
        in_absolute = false;
        global_offset_changed = 0;  /* set by redefine_label */
        if (passn > 1) {
            saa_rewind(forwrefs);
            forwref = saa_rstruct(forwrefs);
            raa_free(offsets);
            offsets = raa_init();
        }
        location.segment = NO_SEG;
        location.offset  = 0;
        if (passn == 1)
            location.known = true;
        ofmt->reset();
        switch_segment(ofmt->section(NULL, pass2, &globalbits));
        preproc->reset(fname, pass1, pass1 == 2 ? depend_list : NULL);

	/* Revert all warnings to the default state */
	memcpy(warning_state, warning_state_init, sizeof warning_state);

        globallineno = 0;

        while ((line = preproc->getline())) {
            if (++globallineno > nasm_limit[LIMIT_LINES])
                nasm_fatal("overall line count exceeds the maximum %"PRId64"\n",
			   nasm_limit[LIMIT_LINES]);

            /*
             * Here we parse our directives; this is not handled by the
             * main parser.
             */
            if (process_directives(line))
                goto end_of_line; /* Just do final cleanup */

            /* Not a directive, or even something that starts with [ */
            parse_line(pass1, line, &output_ins);

            if (optimizing.level > 0) {
                if (forwref != NULL && globallineno == forwref->lineno) {
                    output_ins.forw_ref = true;
                    do {
                        output_ins.oprs[forwref->operand].opflags |= OPFLAG_FORWARD;
                        forwref = saa_rstruct(forwrefs);
                    } while (forwref != NULL
                             && forwref->lineno == globallineno);
                } else
                    output_ins.forw_ref = false;

                if (output_ins.forw_ref) {
                    if (passn == 1) {
                        for (i = 0; i < output_ins.operands; i++) {
                            if (output_ins.oprs[i].opflags & OPFLAG_FORWARD) {
                                struct forwrefinfo *fwinf = (struct forwrefinfo *)saa_wstruct(forwrefs);
                                fwinf->lineno = globallineno;
                                fwinf->operand = i;
                            }
                        }
                    }
                }
            }

            /*  forw_ref */
            if (output_ins.opcode == I_EQU) {
                if (!output_ins.label) {
                    nasm_error(ERR_NONFATAL, "EQU not preceded by label");
                } else if (output_ins.operands == 1 &&
                           (output_ins.oprs[0].type & IMMEDIATE) &&
                           output_ins.oprs[0].wrt == NO_SEG) {
                    define_label(output_ins.label,
                                 output_ins.oprs[0].segment,
                                 output_ins.oprs[0].offset, false);
                } else if (output_ins.operands == 2
                           && (output_ins.oprs[0].type & IMMEDIATE)
                           && (output_ins.oprs[0].type & COLON)
                           && output_ins.oprs[0].segment == NO_SEG
                           && output_ins.oprs[0].wrt == NO_SEG
                           && (output_ins.oprs[1].type & IMMEDIATE)
                           && output_ins.oprs[1].segment == NO_SEG
                           && output_ins.oprs[1].wrt == NO_SEG) {
                    define_label(output_ins.label,
                                 output_ins.oprs[0].offset | SEG_ABS,
                                 output_ins.oprs[1].offset, false);
                } else {
                    nasm_error(ERR_NONFATAL, "bad syntax for EQU");
                }
            } else {        /* instruction isn't an EQU */
                int32_t n;

                nasm_assert(output_ins.times >= 0);

                for (n = 1; n <= output_ins.times; n++) {
                    if (pass1 == 1) {
                        int64_t l = insn_size(location.segment,
                                              location.offset,
                                              globalbits, &output_ins);

                        /* if (using_debug_info)  && output_ins.opcode != -1) */
                        if (using_debug_info)
                        {       /* fbk 03/25/01 */
                            /* this is done here so we can do debug type info */
                            int32_t typeinfo =
                                TYS_ELEMENTS(output_ins.operands);
                            switch (output_ins.opcode) {
                            case I_RESB:
                                typeinfo =
                                    TYS_ELEMENTS(output_ins.oprs[0].offset) | TY_BYTE;
                                break;
                            case I_RESW:
                                typeinfo =
                                    TYS_ELEMENTS(output_ins.oprs[0].offset) | TY_WORD;
                                break;
                            case I_RESD:
                                typeinfo =
                                    TYS_ELEMENTS(output_ins.oprs[0].offset) | TY_DWORD;
                                break;
                            case I_RESQ:
                                typeinfo =
                                    TYS_ELEMENTS(output_ins.oprs[0].offset) | TY_QWORD;
                                break;
                            case I_REST:
                                typeinfo =
                                    TYS_ELEMENTS(output_ins.oprs[0].offset) | TY_TBYTE;
                                break;
                            case I_RESO:
                                typeinfo =
                                    TYS_ELEMENTS(output_ins.oprs[0].offset) | TY_OWORD;
                                break;
                            case I_RESY:
                                typeinfo =
                                    TYS_ELEMENTS(output_ins.oprs[0].offset) | TY_YWORD;
                                break;
                            case I_RESZ:
                                typeinfo =
                                    TYS_ELEMENTS(output_ins.oprs[0].offset) | TY_ZWORD;
                                break;
                            case I_DB:
                                typeinfo |= TY_BYTE;
                                break;
                            case I_DW:
                                typeinfo |= TY_WORD;
                                break;
                            case I_DD:
                                if (output_ins.eops_float)
                                    typeinfo |= TY_FLOAT;
                                else
                                    typeinfo |= TY_DWORD;
                                break;
                            case I_DQ:
                                typeinfo |= TY_QWORD;
                                break;
                            case I_DT:
                                typeinfo |= TY_TBYTE;
                                break;
                            case I_DO:
                                typeinfo |= TY_OWORD;
                                break;
                            case I_DY:
                                typeinfo |= TY_YWORD;
                                break;
                            case I_DZ:
                                typeinfo |= TY_ZWORD;
                                break;
                            default:
                                typeinfo = TY_LABEL;
                                break;
                            }

                            dfmt->debug_typevalue(typeinfo);
                        }

                        /*
                         * For INCBIN, let the code in assemble
                         * handle TIMES, so we don't have to read the
                         * input file over and over.
                         */
                        if (l != -1) {
                            increment_offset(l);
                        }
                        /*
                         * else l == -1 => invalid instruction, which will be
                         * flagged as an error on pass 2
                         */
                    } else {
                        if (n == 2)
                            lfmt->uplevel(LIST_TIMES);
                        increment_offset(assemble(location.segment,
                                                  location.offset,
                                                  globalbits, &output_ins));
                    }
                }               /* not an EQU */
            }
            if (output_ins.times > 1)
                lfmt->downlevel(LIST_TIMES);

            cleanup_insn(&output_ins);

        end_of_line:
            nasm_free(line);
        }                       /* end while (line = preproc->getline... */

        if (global_offset_changed && !terminate_after_phase) {
            switch (pass0) {
            case 1:
                nasm_error(ERR_WARNING|ERR_WARN_PHASE,
                           "phase error during stabilization pass, hoping for the best");
                break;

            case 2:
                nasm_error(ERR_NONFATAL,
                           "phase error during code generation pass");
                break;

            default:
                /* This is normal, we'll keep going... */
                break;
            }
        }

        if (pass1 == 1)
            preproc->cleanup(1);

        /*
         * Always run at least two optimization passes (pass0 == 0);
         * things like subsections will fail miserably without that.
         * Once we commit to a stabilization pass (pass0 == 1), we can't
         * go back, and if something goes bad, we can only hope
         * that we don't end up with a phase error at the end.
         */
        if ((passn > 1 && !global_offset_changed) || pass0 > 0) {
            pass0++;
        } else if (global_offset_changed &&
                   global_offset_changed < prev_offset_changed) {
            prev_offset_changed = global_offset_changed;
            stall_count = 0;
        } else {
            stall_count++;
        }

        if (terminate_after_phase)
            break;

        if ((stall_count > nasm_limit[LIMIT_STALLED]) ||
            (passn >= nasm_limit[LIMIT_PASSES])) {
            /* We get here if the labels don't converge
             * Example: FOO equ FOO + 1
             */
             nasm_error(ERR_NONFATAL,
                          "Can't find valid values for all labels "
                          "after %"PRId64" passes, giving up.", passn);
             nasm_error(ERR_NONFATAL,
                        "Possible causes: recursive EQUs, macro abuse.");
             break;
        }
    }

    preproc->cleanup(0);
    lfmt->cleanup();
    if (!terminate_after_phase && opt_verbose_info) {
        /*  -On and -Ov switches */
        fprintf(stdout, "info: assembly required 1+%"PRId64"+1 passes\n",
                passn-3);
    }
}

/**
 * gnu style error reporting
 * This function prints an error message to error_file in the
 * style used by GNU. An example would be:
 * file.asm:50: error: blah blah blah
 * where file.asm is the name of the file, 50 is the line number on
 * which the error occurs (or is detected) and "error:" is one of
 * the possible optional diagnostics -- it can be "error" or "warning"
 * or something else.  Finally the line terminates with the actual
 * error message.
 *
 * @param severity the severity of the warning or error
 * @param fmt the printf style format string
 */
static void nasm_verror_gnu(int severity, const char *fmt, va_list ap)
{
    const char *currentfile = NULL;
    int32_t lineno = 0;

    if (is_suppressed_warning(severity))
        return;

    if (!(severity & ERR_NOFILE)) {
	src_get(&lineno, &currentfile);
        if (!currentfile || (severity & ERR_TOPFILE)) {
            currentfile = inname[0] ? inname : outname[0] ? outname : NULL;
            lineno = 0;
        }
    }

    if (!skip_this_pass(severity)) {
        if (!lineno)
            fprintf(error_file, "%s:", currentfile ? currentfile : "nasm");
        else
            fprintf(error_file, "%s:%"PRId32": ", currentfile, lineno);
    }

    nasm_verror_common(severity, fmt, ap);
}

/**
 * MS style error reporting
 * This function prints an error message to error_file in the
 * style used by Visual C and some other Microsoft tools. An example
 * would be:
 * file.asm(50) : error: blah blah blah
 * where file.asm is the name of the file, 50 is the line number on
 * which the error occurs (or is detected) and "error:" is one of
 * the possible optional diagnostics -- it can be "error" or "warning"
 * or something else.  Finally the line terminates with the actual
 * error message.
 *
 * @param severity the severity of the warning or error
 * @param fmt the printf style format string
 */
static void nasm_verror_vc(int severity, const char *fmt, va_list ap)
{
    const char *currentfile = NULL;
    int32_t lineno = 0;

    if (is_suppressed_warning(severity))
        return;

    if (!(severity & ERR_NOFILE))
        src_get(&lineno, &currentfile);

    if (!skip_this_pass(severity)) {
        if (currentfile) {
	    fprintf(error_file, "%s(%"PRId32") : ", currentfile, lineno);
	} else {
	    fputs("nasm: ", error_file);
	}
    }

    nasm_verror_common(severity, fmt, ap);
}

/*
 * check to see if this is a suppressable warning
 */
static inline bool is_valid_warning(int severity)
{
    /* Not a warning at all */
    if ((severity & ERR_MASK) != ERR_WARNING)
        return false;

    return WARN_IDX(severity) < ERR_WARN_ALL;
}

/**
 * check for suppressed warning
 * checks for suppressed warning or pass one only warning and we're
 * not in pass 1
 *
 * @param severity the severity of the warning or error
 * @return true if we should abort error/warning printing
 */
static bool is_suppressed_warning(int severity)
{
    /* Might be a warning but suppresed explicitly */
    if (is_valid_warning(severity) && !(severity & ERR_USAGE))
        return !(warning_state[WARN_IDX(severity)] & WARN_ST_ENABLED);
    else
        return false;
}

static bool warning_is_error(int severity)
{
    if (is_valid_warning(severity))
        return !!(warning_state[WARN_IDX(severity)] & WARN_ST_ERROR);
    else
        return false;
}

static bool skip_this_pass(int severity)
{
    /*
     * See if it's a pass-specific error or warning which should be skipped.
     * We cannot skip errors stronger than ERR_NONFATAL as by definition
     * they cannot be resumed from.
     */
    if ((severity & ERR_MASK) > ERR_NONFATAL)
	return false;

    /*
     * passn is 1 on the very first pass only.
     * pass0 is 2 on the code-generation (final) pass only.
     * These are the passes we care about in this case.
     */
    return (((severity & ERR_PASS1) && passn != 1) ||
	    ((severity & ERR_PASS2) && pass0 != 2));
}

/**
 * common error reporting
 * This is the common back end of the error reporting schemes currently
 * implemented.  It prints the nature of the warning and then the
 * specific error message to error_file and may or may not return.  It
 * doesn't return if the error severity is a "panic" or "debug" type.
 *
 * @param severity the severity of the warning or error
 * @param fmt the printf style format string
 */
static void nasm_verror_common(int severity, const char *fmt, va_list args)
{
    char msg[1024];
    const char *pfx;

    switch (severity & (ERR_MASK|ERR_NO_SEVERITY)) {
    case ERR_WARNING:
        pfx = "warning: ";
        break;
    case ERR_NONFATAL:
        pfx = "error: ";
        break;
    case ERR_FATAL:
        pfx = "fatal: ";
        break;
    case ERR_PANIC:
        pfx = "panic: ";
        break;
    case ERR_DEBUG:
        pfx = "debug: ";
        break;
    default:
        pfx = "";
        break;
    }

    vsnprintf(msg, sizeof msg - 64, fmt, args);
    if (is_valid_warning(severity) && WARN_IDX(severity) != ERR_WARN_OTHER) {
        char *p = strchr(msg, '\0');
	snprintf(p, 64, " [-w+%s]", warnings[WARN_IDX(severity)].name);
    }

    if (!skip_this_pass(severity))
	fprintf(error_file, "%s%s\n", pfx, msg);

    /* Are we recursing from error_list_macros? */
    if (severity & ERR_PP_LISTMACRO)
	return;

    /*
     * Don't suppress this with skip_this_pass(), or we don't get
     * pass1 or preprocessor warnings in the list file
     */
    lfmt->error(severity, pfx, msg);

    if (skip_this_pass(severity))
        return;

    if (severity & ERR_USAGE)
        want_usage = true;

    preproc->error_list_macros(severity);

    switch (severity & ERR_MASK) {
    case ERR_DEBUG:
        /* no further action, by definition */
        break;
    case ERR_WARNING:
        /* Treat warnings as errors */
        if (warning_is_error(severity))
            terminate_after_phase = true;
        break;
    case ERR_NONFATAL:
        terminate_after_phase = true;
        break;
    case ERR_FATAL:
        if (ofile) {
            fclose(ofile);
            if (!keep_all)
                remove(outname);
            ofile = NULL;
        }
        if (want_usage)
            usage();
        exit(1);                /* instantly die */
        break;                  /* placate silly compilers */
    case ERR_PANIC:
        fflush(NULL);

        if (abort_on_panic)
            abort();		/* halt, catch fire, dump core/stop debugger */

        if (ofile) {
            fclose(ofile);
            if (!keep_all)
                remove(outname);
            ofile = NULL;
        }
        exit(3);
        break;
    }
}

static void usage(void)
{
    fputs("type `nasm -h' for help\n", error_file);
}

static void help(const char xopt)
{
    int i;

    printf
        ("usage: nasm [-@ response file] [-o outfile] [-f format] "
         "[-l listfile]\n"
         "            [options...] [--] filename\n"
         "    or nasm -v (or --v) for version info\n\n"
         "\n"
         "Response files should contain command line parameters,\n"
         "one per line.\n"
         "\n"
         "    -t            assemble in SciTech TASM compatible mode\n");
    printf
        ("    -E (or -e)    preprocess only (writes output to stdout by default)\n"
         "    -a            don't preprocess (assemble only)\n"
         "    -M            generate Makefile dependencies on stdout\n"
         "    -MG           d:o, missing files assumed generated\n"
         "    -MF file      set Makefile dependency file\n"
         "    -MD file      assemble and generate dependencies\n"
         "    -MT file      dependency target name\n"
         "    -MQ file      dependency target name (quoted)\n"
         "    -MP           emit phony target\n\n"
         "    -Zfile        redirect error messages to file\n"
         "    -s            redirect error messages to stdout\n\n"
         "    -g            generate debugging information\n\n"
         "    -F format     select a debugging format\n\n"
         "    -gformat      same as -g -F format\n\n"
         "    -o outfile    write output to an outfile\n\n"
         "    -f format     select an output format\n\n"
         "    -l listfile   write listing to a listfile\n\n"
         "    -Ipath        add a pathname to the include file path\n");
    printf
        ("    -Olevel       optimize opcodes, immediates and branch offsets\n"
         "       -O0        no optimization\n"
         "       -O1        minimal optimization\n"
         "       -Ox        multipass optimization (default)\n"
         "    -Pfile        pre-include a file (also --include)\n"
         "    -Dmacro[=str] pre-define a macro\n"
         "    -Umacro       undefine a macro\n"
         "    -Xformat      specifiy error reporting format (gnu or vc)\n"
         "    -w+foo        enable warning foo (equiv. -Wfoo)\n"
         "    -w-foo        disable warning foo (equiv. -Wno-foo)\n"
         "    -w[+-]error[=foo]\n"
         "                  promote [specific] warnings to errors\n"
         "    -h            show invocation summary and exit (also --help)\n\n"
         "   --pragma str   pre-executes a specific %%pragma\n"
         "   --before str   add line (usually a preprocessor statement) before the input\n"
         "   --prefix str   prepend the given string to all the given string\n"
         "                  to all extern, common and global symbols (also --gprefix)\n"
         "   --postfix str  append the given string to all the given string\n"
         "                  to all extern, common and global symbols (also --gpostfix)\n"
         "   --lprefix str  prepend the given string to all other symbols\n"
         "   --lpostfix str append the given string to all other symbols\n"
         "   --keep-all     output files will not be removed even if an error happens\n"
         "   --limit-X val  set execution limit X\n");

    for (i = 0; i <= LIMIT_MAX; i++) {
        printf("                     %-15s %s (default ",
               limit_info[i].name, limit_info[i].help);
        if (nasm_limit[i] < LIMIT_MAX_VAL) {
            printf("%"PRId64")\n", nasm_limit[i]);
        } else {
            printf("unlimited)\n");
        }
    }

    printf("\nWarnings for the -W/-w options:\n");

    for (i = 0; i <= ERR_WARN_ALL; i++)
        printf("    %-23s %s%s\n",
               warnings[i].name, warnings[i].help,
               i == ERR_WARN_ALL ? "\n" :
               warnings[i].enabled ? " (default on)" :
               " (default off)");

    if (xopt == 'f') {
        printf("valid output formats for -f are"
               " (`*' denotes default):\n");
        ofmt_list(ofmt, stdout);
    } else {
        printf("For a list of valid output formats, use -hf.\n");
        printf("For a list of debug formats, use -f <format> -y.\n");
    }
}
