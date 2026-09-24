/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2024 The NASM Authors - All Rights Reserved */

/*
 * Error reporting functions for the assembler
 */

#ifndef NASM_ERROR_H
#define NASM_ERROR_H 1

#include "compiler.h"

/*
 * Typedef for the severity field
 */
typedef uint32_t errflags;

/*
 * An error reporting function should look like this.
 */
void printf_func(2, 3) nasm_error(errflags severity, const char *fmt, ...);
void printf_func(1, 2) nasm_listmsg(const char *fmt, ...);
void printf_func(2, 3) nasm_listmsgf(errflags flags, const char *fmt, ...);
void printf_func(2, 3) nasm_debug_(unsigned int level, const char *fmt, ...);
void printf_func(2, 3) nasm_info_(unsigned int level, const char *fmt, ...);
void printf_func(1, 2) nasm_note(const char *fmt, ...);
void printf_func(2, 3) nasm_notef(errflags flags, const char *fmt, ...);
void printf_func(2, 3) nasm_warn_(errflags flags, const char *fmt, ...);
void printf_func(1, 2) nasm_nonfatal(const char *fmt, ...);
void printf_func(2, 3) nasm_nonfatalf(errflags flags, const char *fmt, ...);
fatal_func printf_func(1, 2) nasm_fatal(const char *fmt, ...);
fatal_func printf_func(2, 3) nasm_fatalf(errflags flags, const char *fmt, ...);
fatal_func printf_func(1, 2) nasm_critical(const char *fmt, ...);
fatal_func printf_func(2, 3) nasm_criticalf(errflags flags, const char *fmt, ...);
fatal_func printf_func(1, 2) nasm_panic(const char *fmt, ...);
fatal_func printf_func(2, 3) nasm_panicf(errflags flags, const char *fmt, ...);
fatal_func nasm_panic_from_macro(const char *func, const char *file, int line);
#define panic() nasm_panic_from_macro(NASM_FUNC,__FILE__,__LINE__)

void vprintf_func(2) nasm_verror(errflags severity, const char *fmt, va_list ap);
fatal_func vprintf_func(2) nasm_verror_critical(errflags severity, const char *fmt, va_list ap);

const char *error_pfx(errflags severity);

/*
 * These are the error severity codes which get passed as the first
 * argument to an efunc. The order here matters!
 */

/* For the list file only */
#define ERR_LISTMSG		0x00000000      /* for the listing file only (comment prefix) */
#define ERR_NOTE		0x00000001      /* for the listing file only (with prefix) */

/* Non-terminating diagnostics; can be suppressed */
#define ERR_DEBUG		0x00000004	/* internal debugging message */
#define ERR_INFO		0x00000005	/* informational message */
#define ERR_WARNING		0x00000006	/* warning */

/* Errors which terminate assembly without output */
#define ERR_NONFATAL		0x00000008	/* terminate assembly after the current pass */

/*
 * From this point, errors cannot be suppressed, and the C compiler is
 * told that the call to nasm_verror() is terminating, to remove the
 * need to generate further code.
 */
#define ERR_FATAL		0x0000000c	/* terminate immediately, but perform cleanup */

/*
 * Abort conditions - terminate with minimal or no cleanup.
 *
 * ERR_CRITICAL is used for system errors like out of memory, where the normal
 * error and cleanup paths may impede informing the user of the nature of the failure.
 *
 * ERR_PANIC is used exclusively to trigger on bugs in the NASM code itself.
 */
#define ERR_CRITICAL		0x0000000e      /* fatal, but minimize code before exit */
#define ERR_PANIC		0x0000000f	/* internal error: panic instantly
						 * and call abort() to dump core for reference */

#define ERR_MASK		0x0000000f	/* mask off the above codes */
#define ERR_UNDEAD		0x00000010      /* skip if we already have errors */
#define ERR_NOFILE		0x00000020	/* don't give source file name/line */
#define ERR_HERE		0x00000040      /* point to a specific source location */
#define ERR_USAGE		0x00000080	/* print a usage message */
#define ERR_PASS2		0x00000100	/* ignore unless on pass_final */

#define ERR_NO_SEVERITY		0x00000200	/* suppress printing severity */
#define ERR_PP_PRECOND		0x00000400	/* for preprocessor use */
#define ERR_PP_LISTMACRO	0x00000800	/* from pp_error_list_macros() */
#define ERR_HOLD		0x00001000      /* this error/warning can be held */

/*
 * These codes define specific types of suppressible warning.
 * They are assumed to occupy the most significant bits of the
 * severity code.
 */
#define WARN_SHR		16              /* how far to shift right */
#define WARN_IDX(x)		(((errflags)(x)) >> WARN_SHR)
#define WARN_MASK		((~(errflags)0) << WARN_SHR)
#define WARNING(x)		((errflags)(x) << WARN_SHR)

/* The same field is used for debug and info levels */
#define LEVEL_SHR		WARN_SHR
#define LEVEL(x)		WARNING(x)

/* This is a bitmask */
#define WARN_ST_ENABLED		1 /* Warning is currently enabled */
#define WARN_ST_ERROR		2 /* Treat this warning as an error */

/* Possible initial state for warnings */
#define WARN_INIT_OFF		0
#define WARN_INIT_ON		WARN_ST_ENABLED
#define WARN_INIT_ERR		(WARN_ST_ENABLED|WARN_ST_ERROR)

/* Process a warning option or directive */
bool set_warning_status(const char *value);

/* Warning stack management */
void push_warnings(void);
void pop_warnings(void);
void init_warnings(void);
void reset_warnings(void);

/*
 * Tentative error hold for warnings/errors indicated with ERR_HOLD.
 *
 * This is a stack; the "hold" argument *must*
 * match the value returned from nasm_error_hold_push().
 * If "issue" is true the errors are committed (or promoted to the next
 * higher stack level), if false then they are discarded.
 *
 * Return the highest severity level issued or discarded; note that if
 * promoted, the severity level will be reported at the time the
 * messages are issued, when the top level stack is popped. Fix this if
 * this ever becomes a problem, but it would come at a cost.
 *
 * Errors stronger than ERR_NONFATAL cannot be held.
 */
struct nasm_errhold;
typedef struct nasm_errhold *errhold;
errhold nasm_error_hold_push(void);
errflags nasm_error_hold_pop(errhold hold, bool issue);

/* Should be included from within error.h only */
#include "warnings.h"

/* True if a warning is enabled, either as a warning or an error */
extern errflags errflags_never;
static inline bool warn_active(errflags warn)
{
    if (warn & errflags_never)
        return false;

    return !!(warning_state[WARN_IDX(warn)] & WARN_ST_ENABLED);
}

/*
 * By defining MAX_DEBUG or MAX_INFO, it is possible to
 * compile out messages entirely.
 */
#ifndef MAX_DEBUG
# define MAX_DEBUG UINT_MAX
#endif
#ifndef MAX_INFO
# define MAX_INFO UINT_MAX
#endif

/* Debug level checks */
extern unsigned int debug_nasm;
static inline bool debug_level(unsigned int level)
{
    if (is_constant(level) && level > MAX_DEBUG)
        return false;
    return unlikely(level <= debug_nasm);
}

/* Info level checks */
extern unsigned int opt_verbose_info;
static inline bool info_level(unsigned int level)
{
    if (is_constant(level) && level > MAX_INFO)
        return false;
    return unlikely(level <= opt_verbose_info);
}

#ifdef HAVE_VARIADIC_MACROS

/*
 * Marked unlikely() to avoid excessive speed penalties on disabled warnings;
 * if the warning is issued then the performance penalty is substantial
 * anyway.
 */
#define nasm_warn(w, ...)                               \
    do {                                                \
        const errflags _w = (w);                        \
        if (unlikely(warn_active(_w))) {                \
            nasm_warn_(_w, __VA_ARGS__);                \
        }                                               \
    } while (0)

#define nasm_info(l, ...)                           \
    do {                                            \
            const unsigned int _l = (l);            \
            if (unlikely(info_level(_l)))           \
                nasm_info_(_l, __VA_ARGS__);        \
    } while (0)

#define nasm_debug(l, ...)                          \
    do {                                            \
            const unsigned int _l = (l);            \
            if (unlikely(debug_level(_l)))          \
                nasm_debug_(_l, __VA_ARGS__);       \
    } while (0)

#else

#define nasm_warn  nasm_warn_
#if MAX_DEBUG
# define nasm_debug nasm_debug_
#else
# define nasm_debug (void)
#endif
#if MAX_INFO
# define nasm_info nasm_info_
#else
# define nasm_info (void)
#endif

#endif


#endif /* NASM_ERROR_H */
