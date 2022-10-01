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
 * The Netwide Assembler main program module
 */

#include "compiler.h"


#include "nasm.h"
#include "nasmlib.h"
#include "nctype.h"
#include "error.h"
#include "saa.h"
#include "raa.h"
#include "floats.h"
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
#include "quote.h"
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

const char *_progname;

static void parse_cmdline(int, char **, int);
static void assemble_file(const char *, struct strlist *);
static bool skip_this_pass(errflags severity);
static void usage(void);
static void help(FILE *);

struct error_format {
    const char *beforeline;     /* Before line number, if present */
    const char *afterline;      /* After line number, if present */
    const char *beforemsg;      /* Before actual message */
};

static const struct error_format errfmt_gnu  = { ":", "",  ": "  };
static const struct error_format errfmt_msvc = { "(", ")", " : " };
static const struct error_format *errfmt = &errfmt_gnu;
static struct strlist *warn_list;
static struct nasm_errhold *errhold_stack;

unsigned int debug_nasm;        /* Debugging messages? */

static bool using_debug_info, opt_verbose_info;
static const char *debug_format;

#ifndef ABORT_ON_PANIC
# define ABORT_ON_PANIC 0
#endif
static bool abort_on_panic = ABORT_ON_PANIC;
static bool keep_all;

bool tasm_compatible_mode = false;
enum pass_type _pass_type;
const char * const _pass_types[] =
{
    "init", "preproc-only", "first", "optimize", "stabilize", "final"
};
int64_t _passn;
int globalrel = 0;
int globalbnd = 0;

struct compile_time official_compile_time;

const char *inname;
const char *outname;
static const char *listname;
static const char *errname;

static int64_t globallineno;    /* for forward-reference tracking */

const struct ofmt *ofmt = &OF_DEFAULT;
const struct ofmt_alias *ofmt_alias = NULL;
const struct dfmt *dfmt;

FILE *error_file;               /* Where to write error messages */

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
static struct strlist *include_path;
bool pp_noline;                 /* Ignore %line directives */

#define OP_NORMAL           (1U << 0)
#define OP_PREPROCESS       (1U << 1)
#define OP_DEPEND           (1U << 2)

static unsigned int operating_mode;

/* Dependency flags */
static bool depend_emit_phony = false;
static bool depend_missing_ok = false;
static const char *depend_target = NULL;
static const char *depend_file = NULL;
struct strlist *depend_list;

static bool want_usage;
static bool terminate_after_phase;
bool user_nolist = false;

static char *quote_for_pmake(const char *str);
static char *quote_for_wmake(const char *str);
static char *(*quote_for_make)(const char *) = quote_for_pmake;

#if defined(OF_MACHO) || defined(OF_MACHO64)
extern bool macho_set_min_os(const char *str);
#endif

/*
 * Execution limits that can be set via a command-line option or %pragma
 */

/*
 * This is really unlimited; it would take far longer than the
 * current age of the universe for this limit to be reached even on
 * much faster CPUs than currently exist.
*/
#define LIMIT_MAX_VAL	(INT64_MAX >> 1)

int64_t nasm_limit[LIMIT_MAX+1];

struct limit_info {
    const char *name;
    const char *help;
    int64_t default_val;
};
/* The order here must match enum nasm_limit in nasm.h */
static const struct limit_info limit_info[LIMIT_MAX+1] = {
    { "passes", "total number of passes", LIMIT_MAX_VAL },
    { "stalled-passes", "number of passes without forward progress", 1000 },
    { "macro-levels", "levels of macro expansion", 10000 },
    { "macro-tokens", "tokens processed during single-lime macro expansion", 10000000 },
    { "mmacros", "multi-line macros before final return", 100000 },
    { "rep", "%rep count", 1000000 },
    { "eval", "expression evaluation descent", 8192 },
    { "lines", "total source lines processed", 2000000000 }
};

static void set_default_limits(void)
{
    int i;
    size_t rl;
    int64_t new_limit;

    for (i = 0; i <= LIMIT_MAX; i++)
        nasm_limit[i] = limit_info[i].default_val;

    /*
     * Try to set a sensible default value for the eval depth based
     * on the limit of the stack size, if knowable...
     */
    rl = nasm_get_stack_size_limit();
    new_limit = rl / (128 * sizeof(void *)); /* Sensible heuristic */
    if (new_limit < nasm_limit[LIMIT_EVAL])
        nasm_limit[LIMIT_EVAL] = new_limit;
}

enum directive_result
nasm_set_limit(const char *limit, const char *valstr)
{
    int i;
    int64_t val;
    bool rn_error;
    int errlevel;

    if (!limit)
        limit = "";
    if (!valstr)
        valstr = "";

    for (i = 0; i <= LIMIT_MAX; i++) {
        if (!nasm_stricmp(limit, limit_info[i].name))
            break;
    }
    if (i > LIMIT_MAX) {
        if (not_started())
            errlevel = ERR_WARNING|WARN_OTHER|ERR_USAGE;
        else
            errlevel = ERR_WARNING|WARN_PRAGMA_UNKNOWN;
        nasm_error(errlevel, "unknown limit: `%s'", limit);
        return DIRR_ERROR;
    }

    if (!nasm_stricmp(valstr, "unlimited")) {
        val = LIMIT_MAX_VAL;
    } else {
        val = readnum(valstr, &rn_error);
        if (rn_error || val < 0) {
            if (not_started())
                errlevel = ERR_WARNING|WARN_OTHER|ERR_USAGE;
            else
                errlevel = ERR_WARNING|WARN_PRAGMA_BAD;
            nasm_error(errlevel, "invalid limit value: `%s'", valstr);
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

/*
 * Define system-defined macros that are not part of
 * macros/standard.mac.
 */
static void define_macros(void)
{
    const struct compile_time * const oct = &official_compile_time;
    char temp[128];

    if (oct->have_local) {
        strftime(temp, sizeof temp, "__?DATE?__=\"%Y-%m-%d\"", &oct->local);
        preproc->pre_define(temp);
        strftime(temp, sizeof temp, "__?DATE_NUM?__=%Y%m%d", &oct->local);
        preproc->pre_define(temp);
        strftime(temp, sizeof temp, "__?TIME?__=\"%H:%M:%S\"", &oct->local);
        preproc->pre_define(temp);
        strftime(temp, sizeof temp, "__?TIME_NUM?__=%H%M%S", &oct->local);
        preproc->pre_define(temp);
    }

    if (oct->have_gm) {
        strftime(temp, sizeof temp, "__?UTC_DATE?__=\"%Y-%m-%d\"", &oct->gm);
        preproc->pre_define(temp);
        strftime(temp, sizeof temp, "__?UTC_DATE_NUM?__=%Y%m%d", &oct->gm);
        preproc->pre_define(temp);
        strftime(temp, sizeof temp, "__?UTC_TIME?__=\"%H:%M:%S\"", &oct->gm);
        preproc->pre_define(temp);
        strftime(temp, sizeof temp, "__?UTC_TIME_NUM?__=%H%M%S", &oct->gm);
        preproc->pre_define(temp);
    }

    if (oct->have_posix) {
        snprintf(temp, sizeof temp, "__?POSIX_TIME?__=%"PRId64, oct->posix);
        preproc->pre_define(temp);
    }

    /*
     * In case if output format is defined by alias
     * we have to put shortname of the alias itself here
     * otherwise ABI backward compatibility gets broken.
     */
    snprintf(temp, sizeof(temp), "__?OUTPUT_FORMAT?__=%s",
             ofmt_alias ? ofmt_alias->shortname : ofmt->shortname);
    preproc->pre_define(temp);

    /*
     * Output-format specific macros.
     */
    if (ofmt->stdmac)
        preproc->extra_stdmac(ofmt->stdmac);

    /*
     * Debug format, if any
     */
    if (dfmt != &null_debug_form) {
        snprintf(temp, sizeof(temp), "__?DEBUG_FORMAT?__=%s", dfmt->shortname);
        preproc->pre_define(temp);
    }
}

/*
 * Initialize the preprocessor, set up the include path, and define
 * the system-included macros.  This is called between passes 1 and 2
 * of parsing the command options; ofmt and dfmt are defined at this
 * point.
 *
 * Command-line specified preprocessor directives (-p, -d, -u,
 * --pragma, --before) are processed after this function.
 */
static void preproc_init(struct strlist *ipath)
{
    preproc->init();
    define_macros();
    preproc->include_path(ipath);
}

static void emit_dependencies(struct strlist *list)
{
    FILE *deps;
    int linepos, len;
    bool wmake = (quote_for_make == quote_for_wmake);
    const char *wrapstr, *nulltarget;
    const struct strlist_entry *l;

    if (!list)
        return;

    wrapstr = wmake ? " &\n " : " \\\n ";
    nulltarget = wmake ? "\t%null\n" : "";

    if (depend_file && strcmp(depend_file, "-")) {
        deps = nasm_open_write(depend_file, NF_TEXT);
        if (!deps) {
            nasm_nonfatal("unable to write dependency file `%s'", depend_file);
            return;
        }
    } else {
        deps = stdout;
    }

    linepos = fprintf(deps, "%s :", depend_target);
    strlist_for_each(l, list) {
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
    fputs("\n\n", deps);

    strlist_for_each(l, list) {
        if (depend_emit_phony) {
            char *file = quote_for_make(l->str);
            fprintf(deps, "%s :\n%s\n", file, nulltarget);
            nasm_free(file);
        }
    }

    strlist_free(&list);

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

/*
 * Quote a filename string if and only if it is necessary.
 * It is considered necessary if any one of these is true:
 * 1. The filename contains control characters;
 * 2. The filename starts or ends with a space or quote mark;
 * 3. The filename contains more than one space in a row;
 * 4. The filename is empty.
 *
 * The filename is returned in a newly allocated buffer.
 */
static char *nasm_quote_filename(const char *fn)
{
    const unsigned char *p =
        (const unsigned char *)fn;
    size_t len;

    if (!p || !*p)
        return nasm_strdup("\"\"");

    if (*p <= ' ' || nasm_isquote(*p)) {
        goto quote;
    } else {
        unsigned char cutoff = ' ';

        while (*p) {
            if (*p < cutoff)
                goto quote;
            cutoff = ' ' + (*p == ' ');
            p++;
        }
        if (p[-1] <= ' ' || nasm_isquote(p[-1]))
            goto quote;
    }

    /* Quoting not necessary */
    return nasm_strdup(fn);

quote:
    len = strlen(fn);
    return nasm_quote(fn, &len);
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
    /* Do these as early as possible */
    error_file = stderr;
    _progname = argv[0];
    if (!_progname || !_progname[0])
        _progname = "nasm";

    timestamp();

    iflag_set_default_cpu(&cpu);
    iflag_set_default_cpu(&cmd_cpu);

    set_default_limits();

    include_path = strlist_alloc(true);

    _pass_type = PASS_INIT;
    _passn = 0;

    want_usage = terminate_after_phase = false;

    nasm_ctype_init();
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

    /* At this point we have ofmt and the name of the desired debug format */
    if (!using_debug_info) {
        /* No debug info, redirect to the null backend (empty stubs) */
        dfmt = &null_debug_form;
    } else if (!debug_format) {
        /* Default debug format for this backend */
        dfmt = ofmt->default_dfmt;
    } else {
        dfmt = dfmt_find(ofmt, debug_format);
        if (!dfmt) {
            nasm_fatalf(ERR_USAGE, "unrecognized debug format `%s' for output format `%s'",
                       debug_format, ofmt->shortname);
        }
    }

    preproc_init(include_path);

    parse_cmdline(argc, argv, 2);
    if (terminate_after_phase) {
        if (want_usage)
            usage();
        return 1;
    }

    /* Save away the default state of warnings */
    init_warnings();

    /* Dependency filename if we are also doing other things */
    if (!depend_file && (operating_mode & ~OP_DEPEND)) {
        if (outname)
            depend_file = nasm_strcat(outname, ".d");
        else
            depend_file = filename_set_extension(inname, ".d");
    }

    /*
     * If no output file name provided and this
     * is preprocess mode, we're perfectly
     * fine to output into stdout.
     */
    if (!outname && !(operating_mode & OP_PREPROCESS)) {
        outname = filename_set_extension(inname, ofmt->extension);
        if (!strcmp(outname, inname)) {
            outname = "nasm.out";
            nasm_warn(WARN_OTHER, "default output file same as input, using `%s' for output\n", outname);
        }
    }

    depend_list = (operating_mode & OP_DEPEND) ? strlist_alloc(true) : NULL;

    if (!depend_target)
        depend_target = quote_for_make(outname);

    if (!(operating_mode & (OP_PREPROCESS|OP_NORMAL))) {
            char *line;

            if (depend_missing_ok)
                preproc->include_path(NULL);    /* "assume generated" */

            preproc->reset(inname, PP_DEPS, depend_list);
            ofile = NULL;
            while ((line = preproc->getline()))
                nasm_free(line);
            preproc->cleanup_pass();
            reset_warnings();
    } else if (operating_mode & OP_PREPROCESS) {
            char *line;
            const char *file_name = NULL;
            char *quoted_file_name = nasm_quote_filename(file_name);
            int32_t linnum  = 0;
            int32_t lineinc = 0;
            FILE *out;

            if (outname) {
                ofile = nasm_open_write(outname, NF_TEXT);
                if (!ofile)
                    nasm_fatal("unable to open output file `%s'", outname);
                out = ofile;
            } else {
                ofile = NULL;
                out = stdout;
            }

            location.known = false;

            _pass_type = PASS_PREPROC;
            preproc->reset(inname, PP_PREPROC, depend_list);

            while ((line = preproc->getline())) {
                /*
                 * We generate %line directives if needed for later programs
                 */
                struct src_location where = src_where();
                if (file_name != where.filename) {
                    file_name = where.filename;
                    linnum = -1; /* Force a new %line statement */
                    lineinc = file_name ? 1 : 0;
                    nasm_free(quoted_file_name);
                    quoted_file_name = nasm_quote_filename(file_name);
                } else if (lineinc) {
                    if (linnum + lineinc == where.lineno) {
                        /* Add one blank line to account for increment */
                        fputc('\n', out);
                        linnum += lineinc;
                    } else if (linnum - lineinc == where.lineno) {
                        /*
                         * Standing still, probably a macro. Set increment
                         * to zero.
                         */
                        lineinc = 0;
                    }
                } else {
                    /* lineinc == 0 */
                    if (linnum + 1 == where.lineno)
                        lineinc = 1;
                }

                /* Skip blank lines if we will need a %line anyway */
                if (linnum == -1 && !line[0])
                    continue;

                if (linnum != where.lineno) {
                    fprintf(out, "%%line %"PRId32"%+"PRId32" %s\n",
                            where.lineno, lineinc, quoted_file_name);
                }
                linnum = where.lineno + lineinc;

                fputs(line, out);
                fputc('\n', out);
            }

            nasm_free(quoted_file_name);

            preproc->cleanup_pass();
            reset_warnings();
            if (ofile)
                fclose(ofile);
            if (ofile && terminate_after_phase && !keep_all)
                remove(outname);
            ofile = NULL;
    }

    if (operating_mode & OP_NORMAL) {
        ofile = nasm_open_write(outname, (ofmt->flags & OFMT_TEXT) ? NF_TEXT : NF_BINARY);
        if (!ofile)
            nasm_fatal("unable to open output file `%s'", outname);

        ofmt->init();
        dfmt->init();

        assemble_file(inname, depend_list);

        if (!terminate_after_phase) {
            ofmt->cleanup();
            cleanup_labels();
            fflush(ofile);
            if (ferror(ofile))
                nasm_nonfatal("write error on output file `%s'", outname);
        }

        if (ofile) {
            fclose(ofile);
            if (terminate_after_phase && !keep_all)
                remove(outname);
            ofile = NULL;
        }
    }

    preproc->cleanup_session();

    if (depend_list && !terminate_after_phase)
        emit_dependencies(depend_list);

    if (want_usage)
        usage();

    raa_free(offsets);
    saa_free(forwrefs);
    eval_cleanup();
    stdscan_cleanup();
    src_free();
    strlist_free(&include_path);

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
    nasm_nonfatalf(ERR_USAGE, "option `-%c' requires an argument", p[1]);
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
    OPT_KEEP_ALL,
    OPT_NO_LINE,
    OPT_DEBUG,
    OPT_MACHO_MIN_OS
};
enum need_arg {
    ARG_NO,
    ARG_YES,
    ARG_MAYBE
};

struct textargs {
    const char *label;
    enum text_options opt;
    enum need_arg need_arg;
    int pvt;
};
static const struct textargs textopts[] = {
    {"v", OPT_VERSION, ARG_NO, 0},
    {"version", OPT_VERSION, ARG_NO, 0},
    {"help",     OPT_HELP,  ARG_NO, 0},
    {"abort-on-panic", OPT_ABORT_ON_PANIC, ARG_NO, 0},
    {"prefix",   OPT_MANGLE, ARG_YES, LM_GPREFIX},
    {"postfix",  OPT_MANGLE, ARG_YES, LM_GSUFFIX},
    {"gprefix",  OPT_MANGLE, ARG_YES, LM_GPREFIX},
    {"gpostfix", OPT_MANGLE, ARG_YES, LM_GSUFFIX},
    {"lprefix",  OPT_MANGLE, ARG_YES, LM_LPREFIX},
    {"lpostfix", OPT_MANGLE, ARG_YES, LM_LSUFFIX},
    {"include",  OPT_INCLUDE, ARG_YES, 0},
    {"pragma",   OPT_PRAGMA,  ARG_YES, 0},
    {"before",   OPT_BEFORE,  ARG_YES, 0},
    {"limit-",   OPT_LIMIT,   ARG_YES, 0},
    {"keep-all", OPT_KEEP_ALL, ARG_NO, 0},
    {"no-line",  OPT_NO_LINE, ARG_NO, 0},
    {"debug",    OPT_DEBUG, ARG_MAYBE, 0},
    {"macho-min-os", OPT_MACHO_MIN_OS, ARG_YES, 0},
    {NULL, OPT_BOGUS, ARG_NO, 0}
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
        if (strchr("oOfpPdDiIlLFXuUZwW", p[1])) {
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
                    nasm_fatalf(ERR_USAGE, "unrecognised output format `%s' - use -hf for a list", param);
                }
            }
            break;

        case 'O':       /* Optimization level */
            if (pass == 1) {
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
            if (pass == 1)
                strlist_add(include_path, param);
            break;

        case 'l':       /* listing file */
            if (pass == 2)
                copy_filename(&listname, param, "listing");
            break;

        case 'L':        /* listing options */
            if (pass == 2) {
                while (*param)
                    list_options |= list_option_mask(*param++);
            }
            break;

        case 'Z':       /* error messages file */
            if (pass == 1)
                copy_filename(&errname, param, "error");
            break;

        case 'F':       /* specify debug format */
            if (pass == 1) {
                using_debug_info = true;
                debug_format = param;
            }
            break;

        case 'X':       /* specify error reporting format */
            if (pass == 1) {
                if (!nasm_stricmp("vc", param) || !nasm_stricmp("msvc", param) || !nasm_stricmp("ms", param))
                    errfmt = &errfmt_msvc;
                else if (!nasm_stricmp("gnu", param) || !nasm_stricmp("gcc", param))
                    errfmt = &errfmt_gnu;
                else
                    nasm_fatalf(ERR_USAGE, "unrecognized error reporting format `%s'", param);
            }
            break;

        case 'g':
            if (pass == 1) {
                using_debug_info = true;
                if (p[2])
                    debug_format = nasm_skip_spaces(p + 2);
            }
            break;

        case 'h':
            help(stdout);
            exit(0);    /* never need usage message here */
            break;

        case 'y':
            /* legacy option */
            dfmt_list(stdout);
            exit(0);
            break;

        case 't':
            if (pass == 2) {
                tasm_compatible_mode = true;
                nasm_ctype_tasm_mode();
            }
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
            if (pass == 2)
                set_warning_status(param);
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
                    operating_mode |= OP_DEPEND;
                    if (q && (q[0] != '-' || q[1] == '\0')) {
                        depend_file = q;
                        advance = true;
                    }
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
                    nasm_nonfatalf(ERR_USAGE, "unknown dependency option `-M%c'", p[2]);
                    break;
                }
            }
            if (advance && (!q || !q[0])) {
                nasm_nonfatalf(ERR_USAGE, "option `-M%c' requires a parameter", p[2]);
                break;
            }
            break;

        case '-':
            {
                const struct textargs *tx;
                size_t olen, plen;
                char *eqsave;
                enum text_options opt;

                p += 2;

                if (!*p) {        /* -- => stop processing options */
                    stopoptions = true;
                    break;
                }

                olen = 0;       /* Placate gcc at lower optimization levels */
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
                    nasm_nonfatalf(ERR_USAGE, "unrecognized option `--%s'", p);
                }

                opt = tx->opt;

                eqsave = param = strchr(p+olen, '=');
                if (param)
                    *param++ = '\0';

                switch (tx->need_arg) {
                case ARG_YES:   /* Argument required, and may be standalone */
                    if (!param) {
                        param = q;
                        advance = true;
                    }

                    /* Note: a null string is a valid parameter */
                    if (!param) {
                        nasm_nonfatalf(ERR_USAGE, "option `--%s' requires an argument", p);
                        opt = OPT_BOGUS;
                    }
                    break;

                case ARG_NO:    /* Argument prohibited */
                    if (param) {
                        nasm_nonfatalf(ERR_USAGE, "option `--%s' does not take an argument", p);
                        opt = OPT_BOGUS;
                    }
                    break;

                case ARG_MAYBE: /* Argument permitted, but must be attached with = */
                    break;
                }

                switch (opt) {
                case OPT_BOGUS:
                    break;      /* We have already errored out */
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
                    if (pass == 1)
                        nasm_set_limit(p+olen, param);
                    break;
                case OPT_KEEP_ALL:
                    keep_all = true;
                    break;
                case OPT_NO_LINE:
                    pp_noline = true;
                    break;
                case OPT_DEBUG:
                    debug_nasm = param ? strtoul(param, NULL, 10) : debug_nasm+1;
                    break;
                case OPT_MACHO_MIN_OS:
                    if (pass == 2) {
                        if (strstr(ofmt->shortname, "macho") != ofmt->shortname) {
                            nasm_error(
                                ERR_WARNING | WARN_OTHER | ERR_USAGE,
                                "macho-min-os is only valid for macho format, current: %s",
                                ofmt->shortname);
                            break;
                        }
#if defined(OF_MACHO) || defined(OF_MACHO64)
                        if (!macho_set_min_os(param)) {
                            nasm_fatalf(ERR_USAGE, "failed to set minimum os for mach-o '%s'",
                                        param);
                        }
#endif
                    }
                    break;
                case OPT_HELP:
                    help(stdout);
                    exit(0);
                default:
                    panic();
                }

                if (eqsave)
                    *eqsave = '='; /* Restore = argument separator */

                break;
            }

        default:
            nasm_nonfatalf(ERR_USAGE, "unrecognised option `-%c'", p[1]);
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

    /*
     * Initialize all the warnings to their default state, including
     * warning index 0 used for "always on".
     */
    memcpy(warning_state, warning_default, sizeof warning_state);

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
                } else {
                    nasm_nonfatalf(ERR_USAGE, "unable to open response file `%s'", p);
                }
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
        nasm_fatalf(ERR_USAGE, "no input file specified");
    else if ((errname && !strcmp(inname, errname)) ||
             (outname && !strcmp(inname, outname)) ||
             (listname &&  !strcmp(inname, listname))  ||
             (depend_file && !strcmp(inname, depend_file)))
        nasm_fatalf(ERR_USAGE, "will not overwrite input file");

    if (errname) {
        error_file = nasm_open_write(errname, NF_TEXT);
        if (!error_file) {
            error_file = stderr;        /* Revert to default! */
            nasm_fatalf(ERR_USAGE, "cannot open file `%s' for error messages", errname);
        }
    }
}

static void forward_refs(insn *instruction)
{
    int i;
    struct forwrefinfo *fwinf;

    instruction->forw_ref = false;

    if (!optimizing.level)
        return;                 /* For -O0 don't bother */

    if (!forwref)
        return;

    if (forwref->lineno != globallineno)
        return;

    instruction->forw_ref = true;
    do {
        instruction->oprs[forwref->operand].opflags |= OPFLAG_FORWARD;
        forwref = saa_rstruct(forwrefs);
    } while (forwref && forwref->lineno == globallineno);

    if (!pass_first())
        return;

    for (i = 0; i < instruction->operands; i++) {
        if (instruction->oprs[i].opflags & OPFLAG_FORWARD) {
            fwinf = saa_wstruct(forwrefs);
            fwinf->lineno = globallineno;
            fwinf->operand = i;
        }
    }
}

static void process_insn(insn *instruction)
{
    int32_t n;
    int64_t l;

    if (!instruction->times)
        return;                 /* Nothing to do... */

    nasm_assert(instruction->times > 0);

    /*
     * NOTE: insn_size() can change instruction->times
     * (usually to 1) when called.
     */
    if (!pass_final()) {
        int64_t start = location.offset;
        for (n = 1; n <= instruction->times; n++) {
            l = insn_size(location.segment, location.offset,
                          globalbits, instruction);
            /* l == -1 -> invalid instruction */
            if (l != -1)
                increment_offset(l);
        }
        if (list_option('p')) {
            struct out_data dummy;
            memset(&dummy, 0, sizeof dummy);
            dummy.type   = OUT_RAWDATA; /* Handled specially with .data NULL */
            dummy.offset = start;
            dummy.size   = location.offset - start;
            lfmt->output(&dummy);
        }
    } else {
        l = assemble(location.segment, location.offset,
                     globalbits, instruction);
                /* We can't get an invalid instruction here */
        increment_offset(l);

        if (instruction->times > 1) {
            lfmt->uplevel(LIST_TIMES, instruction->times);
            for (n = 2; n <= instruction->times; n++) {
                l = assemble(location.segment, location.offset,
                             globalbits, instruction);
                increment_offset(l);
            }
            lfmt->downlevel(LIST_TIMES);
        }
    }
}

static void assemble_file(const char *fname, struct strlist *depend_list)
{
    char *line;
    insn output_ins;
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

    prev_offset_changed = INT64_MAX;

    if (listname && !keep_all) {
        /* Remove the list file in case we die before the output pass */
        remove(listname);
    }

    while (!terminate_after_phase && !pass_final()) {
        _passn++;
        switch (pass_type()) {
        case PASS_INIT:
            _pass_type = PASS_FIRST;
            break;
        case PASS_OPT:
            if (global_offset_changed)
                break;          /* One more optimization pass */
            /* fall through */
        default:
            _pass_type++;
            break;
        }

        global_offset_changed = 0;

	/*
	 * Create a warning buffer list unless we are in
         * pass 2 (everything will be emitted immediately in pass 2.)
	 */
	if (warn_list) {
            if (warn_list->nstr || pass_final())
                strlist_free(&warn_list);
        }

	if (!pass_final() && !warn_list)
            warn_list = strlist_alloc(false);

        globalbits = cmd_sb;  /* set 'bits' to command line default */
        cpu = cmd_cpu;
        if (listname) {
            if (pass_final() || list_on_every_pass()) {
                active_list_options = list_options;
                lfmt->init(listname);
            } else if (active_list_options) {
                /*
                 * Looks like we used the list engine on a previous pass,
                 * but now it is turned off, presumably via %pragma -p
                 */
                lfmt->cleanup();
                if (!keep_all)
                    remove(listname);
                active_list_options = 0;
            }
        }

        in_absolute = false;
        if (!pass_first()) {
            saa_rewind(forwrefs);
            forwref = saa_rstruct(forwrefs);
            raa_free(offsets);
            offsets = raa_init();
        }
        location.segment = NO_SEG;
        location.offset  = 0;
        if (pass_first())
            location.known = true;
        ofmt->reset();
        switch_segment(ofmt->section(NULL, &globalbits));
        preproc->reset(fname, PP_NORMAL, pass_final() ? depend_list : NULL);

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
            parse_line(line, &output_ins);
            forward_refs(&output_ins);
            process_insn(&output_ins);
            cleanup_insn(&output_ins);

        end_of_line:
            nasm_free(line);
        }                       /* end while (line = preproc->getline... */

        preproc->cleanup_pass();

        /* We better not be having an error hold still... */
        nasm_assert(!errhold_stack);

        if (global_offset_changed) {
            switch (pass_type()) {
            case PASS_OPT:
                /*
                 * This is the only pass type that can be executed more
                 * than once, and therefore has the ability to stall.
                 */
                if (global_offset_changed < prev_offset_changed) {
                    prev_offset_changed = global_offset_changed;
                    stall_count = 0;
                } else {
                    stall_count++;
                }

                if (stall_count > nasm_limit[LIMIT_STALLED] ||
                    pass_count() >= nasm_limit[LIMIT_PASSES]) {
                    /* No convergence, almost certainly dead */
                    nasm_nonfatalf(ERR_UNDEAD,
                                   "unable to find valid values for all labels "
                                   "after %"PRId64" passes; "
                                   "stalled for %"PRId64", giving up.",
                                   pass_count(), stall_count);
                    nasm_nonfatalf(ERR_UNDEAD,
                               "Possible causes: recursive EQUs, macro abuse.");
                }
                break;

            case PASS_STAB:
                /*!
                 *!phase [off] phase error during stabilization
                 *!  warns about symbols having changed values during
                 *!  the second-to-last assembly pass. This is not
                 *!  inherently fatal, but may be a source of bugs.
                 */
                nasm_warn(WARN_PHASE|ERR_UNDEAD,
                          "phase error during stabilization "
                          "pass, hoping for the best");
                break;

            case PASS_FINAL:
                nasm_nonfatalf(ERR_UNDEAD,
                               "phase error during code generation pass");
                break;

            default:
                /* This is normal, we'll keep going... */
                break;
            }
        }

        reset_warnings();
    }

    if (opt_verbose_info && pass_final()) {
        /*  -On and -Ov switches */
        nasm_info("assembly required 1+%"PRId64"+2 passes\n", pass_count()-3);
    }

    lfmt->cleanup();
    strlist_free(&warn_list);
}

/**
 * get warning index; 0 if this is non-suppressible.
 */
static size_t warn_index(errflags severity)
{
    size_t index;

    if ((severity & ERR_MASK) >= ERR_FATAL)
        return 0;               /* Fatal errors are never suppressible */

    /* Warnings MUST HAVE a warning category specifier! */
    nasm_assert((severity & (ERR_MASK|WARN_MASK)) != ERR_WARNING);

    index = WARN_IDX(severity);
    nasm_assert(index < WARN_IDX_ALL);

    return index;
}

static bool skip_this_pass(errflags severity)
{
    errflags type = severity & ERR_MASK;

    /*
     * See if it's a pass-specific error or warning which should be skipped.
     * We can never skip fatal errors as by definition they cannot be
     * resumed from.
     */
    if (type >= ERR_FATAL)
        return false;

    /*
     * ERR_LISTMSG messages are always skipped; the list file
     * receives them anyway as this function is not consulted
     * for sending to the list file.
     */
    if (type == ERR_LISTMSG)
        return true;

    /*
     * This message not applicable unless it is the last pass we are going
     * to execute; this can be either the final code-generation pass or
     * the single pass executed in preproc-only mode.
     */
    return (severity & ERR_PASS2) && !pass_final_or_preproc();
}

/**
 * check for suppressed message (usually warnings or notes)
 *
 * @param severity the severity of the warning or error
 * @return true if we should abort error/warning printing
 */
static bool is_suppressed(errflags severity)
{
    /* Fatal errors must never be suppressed */
    if ((severity & ERR_MASK) >= ERR_FATAL)
        return false;

    /* This error/warning is pointless if we are dead anyway */
    if ((severity & ERR_UNDEAD) && terminate_after_phase)
        return true;

    if (!(warning_state[warn_index(severity)] & WARN_ST_ENABLED))
        return true;

    if (preproc && !(severity & ERR_PP_LISTMACRO))
        return preproc->suppress_error(severity);

    return false;
}

/**
 * Return the true error type (the ERR_MASK part) of the given
 * severity, accounting for warnings that may need to be promoted to
 * error.
 *
 * @param severity the severity of the warning or error
 * @return true if we should error out
 */
static errflags true_error_type(errflags severity)
{
    const uint8_t warn_is_err = WARN_ST_ENABLED|WARN_ST_ERROR;
    int type;

    type = severity & ERR_MASK;

    /* Promote warning to error? */
    if (type == ERR_WARNING) {
        uint8_t state = warning_state[warn_index(severity)];
        if ((state & warn_is_err) == warn_is_err)
            type = ERR_NONFATAL;
    }

    return type;
}

/*
 * The various error type prefixes
 */
static const char * const error_pfx_table[ERR_MASK+1] = {
    ";;; ", "debug: ", "info: ", "warning: ",
        "error: ", "fatal: ", "critical: ", "panic: "
};
static const char no_file_name[] = "nasm"; /* What to print if no file name */

/*
 * For fatal/critical/panic errors, kill this process.
 */
static fatal_func die_hard(errflags true_type, errflags severity)
{
    fflush(NULL);

    if (true_type == ERR_PANIC && abort_on_panic)
        abort();

    if (ofile) {
        fclose(ofile);
        if (!keep_all)
            remove(outname);
        ofile = NULL;
    }

    if (severity & ERR_USAGE)
        usage();

    /* Terminate immediately */
    exit(true_type - ERR_FATAL + 1);
}

/*
 * Returns the struct src_location appropriate for use, after some
 * potential filename mangling.
 */
static struct src_location error_where(errflags severity)
{
    struct src_location where;

    if (severity & ERR_NOFILE) {
        where.filename = NULL;
        where.lineno = 0;
    } else {
        where = src_where_error();

        if (!where.filename) {
            where.filename =
            inname && inname[0] ? inname :
                outname && outname[0] ? outname :
                NULL;
            where.lineno = 0;
        }
    }

    return where;
}

/*
 * error reporting for critical and panic errors: minimize
 * the amount of system dependencies for getting a message out,
 * and in particular try to avoid memory allocations.
 */
fatal_func nasm_verror_critical(errflags severity, const char *fmt, va_list args)
{
    struct src_location where;
    errflags true_type = severity & ERR_MASK;
    static bool been_here = false;

    if (unlikely(been_here))
        abort();                /* Recursive error... just die */

    been_here = true;

    where = error_where(severity);
    if (!where.filename)
        where.filename = no_file_name;

    fputs(error_pfx_table[severity], error_file);
    fputs(where.filename, error_file);
    if (where.lineno) {
        fprintf(error_file, "%s%"PRId32"%s",
                errfmt->beforeline, where.lineno, errfmt->afterline);
    }
    fputs(errfmt->beforemsg, error_file);
    vfprintf(error_file, fmt, args);
    fputc('\n', error_file);

    die_hard(true_type, severity);
}

/**
 * Stack of tentative error hold lists.
 */
struct nasm_errtext {
    struct nasm_errtext *next;
    char *msg;                  /* Owned by this structure */
    struct src_location where;  /* Owned by the srcfile system */
    errflags severity;
    errflags true_type;
};
struct nasm_errhold {
    struct nasm_errhold *up;
    struct nasm_errtext *head, **tail;
};

static void nasm_free_error(struct nasm_errtext *et)
{
    nasm_free(et->msg);
    nasm_free(et);
}

static void nasm_issue_error(struct nasm_errtext *et);

struct nasm_errhold *nasm_error_hold_push(void)
{
    struct nasm_errhold *eh;

    nasm_new(eh);
    eh->up = errhold_stack;
    eh->tail = &eh->head;
    errhold_stack = eh;

    return eh;
}

void nasm_error_hold_pop(struct nasm_errhold *eh, bool issue)
{
    struct nasm_errtext *et, *etmp;

    /* Allow calling with a null argument saying no hold in the first place */
    if (!eh)
        return;

    /* This *must* be the current top of the errhold stack */
    nasm_assert(eh == errhold_stack);

    if (eh->head) {
        if (issue) {
            if (eh->up) {
                /* Commit the current hold list to the previous level */
                *eh->up->tail = eh->head;
                eh->up->tail = eh->tail;
            } else {
                /* Issue errors */
                list_for_each_safe(et, etmp, eh->head)
                    nasm_issue_error(et);
            }
        } else {
            /* Free the list, drop errors */
            list_for_each_safe(et, etmp, eh->head)
                nasm_free_error(et);
        }
    }

    errhold_stack = eh->up;
    nasm_free(eh);
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
void nasm_verror(errflags severity, const char *fmt, va_list args)
{
    struct nasm_errtext *et;
    errflags true_type = true_error_type(severity);

    if (true_type >= ERR_CRITICAL)
        nasm_verror_critical(severity, fmt, args);

    if (is_suppressed(severity))
        return;

    nasm_new(et);
    et->severity = severity;
    et->true_type = true_type;
    et->msg = nasm_vasprintf(fmt, args);
    et->where = error_where(severity);

    if (errhold_stack && true_type <= ERR_NONFATAL) {
        /* It is a tentative error */
        *errhold_stack->tail = et;
        errhold_stack->tail = &et->next;
    } else {
        nasm_issue_error(et);
    }

    /*
     * Don't do this before then, if we do, we lose messages in the list
     * file, as the list file is only generated in the last pass.
     */
    if (skip_this_pass(severity))
        return;

    if (!(severity & (ERR_HERE|ERR_PP_LISTMACRO)))
        if (preproc)
            preproc->error_list_macros(severity);
}

/*
 * Actually print, list and take action on an error
 */
static void nasm_issue_error(struct nasm_errtext *et)
{
    const char *pfx;
    char warnsuf[64];           /* Warning suffix */
    char linestr[64];           /* Formatted line number if applicable */
    const errflags severity  = et->severity;
    const errflags true_type = et->true_type;
    const struct src_location where = et->where;

    if (severity & ERR_NO_SEVERITY)
        pfx = "";
    else
        pfx = error_pfx_table[true_type];

    *warnsuf = 0;
    if ((severity & (ERR_MASK|ERR_HERE|ERR_PP_LISTMACRO)) == ERR_WARNING) {
        /*
         * It's a warning without ERR_HERE defined, and we are not already
         * unwinding the macros that led us here.
         */
        snprintf(warnsuf, sizeof warnsuf, " [-w+%s%s]",
                 (true_type >= ERR_NONFATAL) ? "error=" : "",
                 warning_name[warn_index(severity)]);
    }

    *linestr = 0;
    if (where.lineno) {
        snprintf(linestr, sizeof linestr, "%s%"PRId32"%s",
                 errfmt->beforeline, where.lineno, errfmt->afterline);
    }

    if (!skip_this_pass(severity)) {
        const char *file = where.filename ? where.filename : no_file_name;
        const char *here = "";

        if (severity & ERR_HERE) {
            here = where.filename ? " here" : " in an unknown location";
        }

        if (warn_list && true_type < ERR_NONFATAL &&
            !(pass_first() && (severity & ERR_PASS1))) {
            /*
             * Buffer up warnings until we either get an error
             * or we are on the code-generation pass.
             */
            strlist_printf(warn_list, "%s%s%s%s%s%s%s",
                           file, linestr, errfmt->beforemsg,
                           pfx, et->msg, here, warnsuf);
        } else {
            /*
             * Actually output an error.  If we have buffered
             * warnings, and this is a non-warning, output them now.
             */
            if (true_type >= ERR_NONFATAL && warn_list) {
                strlist_write(warn_list, "\n", error_file);
                strlist_free(&warn_list);
            }

            fprintf(error_file, "%s%s%s%s%s%s%s\n",
                    file, linestr, errfmt->beforemsg,
                    pfx, et->msg, here, warnsuf);
        }
    }

    /* Are we recursing from error_list_macros? */
    if (severity & ERR_PP_LISTMACRO)
        goto done;

    /*
     * Don't suppress this with skip_this_pass(), or we don't get
     * pass1 or preprocessor warnings in the list file
     */
    if (severity & ERR_HERE) {
        if (where.lineno)
            lfmt->error(severity, "%s%s at %s:%"PRId32"%s",
                        pfx, et->msg, where.filename, where.lineno, warnsuf);
        else if (where.filename)
            lfmt->error(severity, "%s%s in file %s%s",
                        pfx, et->msg, where.filename, warnsuf);
        else
            lfmt->error(severity, "%s%s in an unknown location%s",
                        pfx, et->msg, warnsuf);
    } else {
        lfmt->error(severity, "%s%s%s", pfx, et->msg, warnsuf);
    }

    if (skip_this_pass(severity))
        goto done;

    if (true_type >= ERR_FATAL)
        die_hard(true_type, severity);
    else if (true_type >= ERR_NONFATAL)
        terminate_after_phase = true;

done:
    nasm_free_error(et);
}

static void usage(void)
{
    fprintf(error_file, "Type %s -h for help.\n", _progname);
}

static void help(FILE *out)
{
    int i;

    fprintf(out,
            "Usage: %s [-@ response_file] [options...] [--] filename\n"
            "       %s -v (or --v)\n",
            _progname, _progname);
    fputs(
        "\n"
        "Options (values in brackets indicate defaults):\n"
        "\n"
        "    -h            show this text and exit (also --help)\n"
        "    -v (or --v)   print the NASM version number and exit\n"
        "    -@ file       response file; one command line option per line\n"
        "\n"
        "    -o outfile    write output to outfile\n"
        "    --keep-all    output files will not be removed even if an error happens\n"
        "\n"
        "    -Xformat      specifiy error reporting format (gnu or vc)\n"
        "    -s            redirect error messages to stdout\n"
        "    -Zfile        redirect error messages to file\n"
        "\n"
        "    -M            generate Makefile dependencies on stdout\n"
        "    -MG           d:o, missing files assumed generated\n"
        "    -MF file      set Makefile dependency file\n"
        "    -MD file      assemble and generate dependencies\n"
        "    -MT file      dependency target name\n"
        "    -MQ file      dependency target name (quoted)\n"
        "    -MP           emit phony targets\n"
        "\n"
        "    -f format     select output file format\n"
        , out);
    ofmt_list(ofmt, out);
    fputs(
        "\n"
        "    -g            generate debugging information\n"
        "    -F format     select a debugging format (output format dependent)\n"
        "    -gformat      same as -g -F format\n"
        , out);
    dfmt_list(out);
    fputs(
        "\n"
        "    -l listfile   write listing to a list file\n"
        "    -Lflags...    add optional information to the list file\n"
        "       -Lb        show builtin macro packages (standard and %use)\n"
        "       -Ld        show byte and repeat counts in decimal, not hex\n"
        "       -Le        show the preprocessed output\n"
        "       -Lf        ignore .nolist (force output)\n"
        "       -Lm        show multi-line macro calls with expanded parmeters\n"
        "       -Lp        output a list file every pass, in case of errors\n"
        "       -Ls        show all single-line macro definitions\n"
        "       -Lw        flush the output after every line\n"
        "       -L+        enable all listing options (very verbose!)\n"
        "\n"
        "    -Oflags...    optimize opcodes, immediates and branch offsets\n"
        "       -O0        no optimization\n"
        "       -O1        minimal optimization\n"
        "       -Ox        multipass optimization (default)\n"
        "       -Ov        display the number of passes executed at the end\n"
        "    -t            assemble in limited SciTech TASM compatible mode\n"
        "\n"
        "    -E (or -e)    preprocess only (writes output to stdout by default)\n"
        "    -a            don't preprocess (assemble only)\n"
        "    -Ipath        add a pathname to the include file path\n"
        "    -Pfile        pre-include a file (also --include)\n"
        "    -Dmacro[=str] pre-define a macro\n"
        "    -Umacro       undefine a macro\n"
        "   --pragma str   pre-executes a specific %%pragma\n"
        "   --before str   add line (usually a preprocessor statement) before the input\n"
        "   --no-line      ignore %line directives in input\n"
        "\n"
        "   --prefix str   prepend the given string to the names of all extern,\n"
        "                  common and global symbols (also --gprefix)\n"
        "   --suffix str   append the given string to the names of all extern,\n"
        "                  common and global symbols (also --gprefix)\n"
        "   --lprefix str  prepend the given string to local symbols\n"
        "   --lpostfix str append the given string to local symbols\n"
        "\n"
        "   --macho-min-os minos minimum os version for mach-o format(example: macos-11.0)\n"
        "\n"
        "    -w+x          enable warning x (also -Wx)\n"
        "    -w-x          disable warning x (also -Wno-x)\n"
        "    -w[+-]error   promote all warnings to errors (also -Werror)\n"
        "    -w[+-]error=x promote warning x to errors (also -Werror=x)\n"
        , out);

    fprintf(out, "       %-20s %s\n",
            warning_name[WARN_IDX_ALL], warning_help[WARN_IDX_ALL]);

    for (i = 1; i < WARN_IDX_ALL; i++) {
        const char *me   = warning_name[i];
        const char *prev = warning_name[i-1];
        const char *next = warning_name[i+1];

        if (prev) {
            int prev_len = strlen(prev);
            const char *dash = me;

            while ((dash = strchr(dash+1, '-'))) {
                int prefix_len = dash - me; /* Not including final dash */
                if (strncmp(next, me, prefix_len+1)) {
                    /* Only one or last option with this prefix */
                    break;
                }
                if (prefix_len >= prev_len ||
                    strncmp(prev, me, prefix_len) ||
                    (prev[prefix_len] != '-' && prev[prefix_len] != '\0')) {
                    /* This prefix is different from the previous option */
                    fprintf(out, "       %-20.*s all warnings prefixed with \"%.*s\"\n",
                            prefix_len, me, prefix_len+1, me);
                }
            }
        }

        fprintf(out, "       %-20s %s%s\n",
                warning_name[i], warning_help[i],
                (warning_default[i] & WARN_ST_ERROR) ? " [error]" :
                (warning_default[i] & WARN_ST_ENABLED) ? " [on]" : " [off]");
    }

    fputs(
        "\n"
        "   --limit-X val  set execution limit X\n"
        , out);


    for (i = 0; i <= LIMIT_MAX; i++) {
        fprintf(out, "       %-20s %s [",
                limit_info[i].name, limit_info[i].help);
        if (nasm_limit[i] < LIMIT_MAX_VAL) {
            fprintf(out, "%"PRId64"]\n", nasm_limit[i]);
        } else {
            fputs("unlimited]\n", out);
        }
    }
}
