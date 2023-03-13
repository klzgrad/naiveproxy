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
 * error.c - error message handling routines for the assembler
 */

#include "compiler.h"


#include "nasmlib.h"
#include "error.h"

/* Common function body */
#define nasm_do_error(_sev,_flags)				\
	va_list ap;						\
        va_start(ap, fmt);					\
	if ((_sev) >= ERR_CRITICAL)				\
		nasm_verror_critical((_sev)|(_flags), fmt, ap);	\
	else							\
		nasm_verror((_sev)|(_flags), fmt, ap);		\
	va_end(ap);						\
	if ((_sev) >= ERR_FATAL)                                \
		abort();


void nasm_error(errflags severity, const char *fmt, ...)
{
	nasm_do_error(severity & ERR_MASK, severity & ~ERR_MASK);
}

#define nasm_err_helpers(_type, _name, _sev)				\
_type nasm_ ## _name ## f (errflags flags, const char *fmt, ...)	\
{									\
	nasm_do_error(_sev, flags);					\
}									\
_type nasm_ ## _name (const char *fmt, ...)				\
{									\
	nasm_do_error(_sev, 0);						\
}

nasm_err_helpers(void,       listmsg,  ERR_LISTMSG)
nasm_err_helpers(void,       debug,    ERR_DEBUG)
nasm_err_helpers(void,       info,     ERR_INFO)
nasm_err_helpers(void,       nonfatal, ERR_NONFATAL)
nasm_err_helpers(fatal_func, fatal,    ERR_FATAL)
nasm_err_helpers(fatal_func, critical, ERR_CRITICAL)
nasm_err_helpers(fatal_func, panic,    ERR_PANIC)

/*
 * Strongly discourage warnings without level by require flags on warnings.
 * This means nasm_warn() is the equivalent of the -f variants of the
 * other ones.
 */
void nasm_warn(errflags flags, const char *fmt, ...)
{
	nasm_do_error(ERR_WARNING, flags);
}

fatal_func nasm_panic_from_macro(const char *file, int line)
{
	nasm_panic("internal error at %s:%d\n", file, line);
}

fatal_func nasm_assert_failed(const char *file, int line, const char *msg)
{
	nasm_panic("assertion %s failed at %s:%d", msg, file, line);
}


/*
 * Warning stack management. Note that there is an implicit "push"
 * after the command line has been parsed, but this particular push
 * cannot be popped.
 */
struct warning_stack {
	struct warning_stack *next;
	uint8_t state[sizeof warning_state];
};
static struct warning_stack *warning_stack, *warning_state_init;

/* Push the warning status onto the warning stack */
void push_warnings(void)
{
	struct warning_stack *ws;

	ws = nasm_malloc(sizeof *ws);
	memcpy(ws->state, warning_state, sizeof warning_state);
	ws->next = warning_stack;
	warning_stack = ws;
}

/* Pop the warning status off the warning stack */
void pop_warnings(void)
{
	struct warning_stack *ws = warning_stack;

	memcpy(warning_state, ws->state, sizeof warning_state);
	if (!ws->next) {
		/*!
		 *!warn-stack-empty [on] warning stack empty
		 *!  a [WARNING POP] directive was executed when
		 *!  the warning stack is empty. This is treated
		 *!  as a [WARNING *all] directive.
		 */
		nasm_warn(WARN_WARN_STACK_EMPTY, "warning stack empty");
	} else {
		warning_stack = ws->next;
		nasm_free(ws);
	}
}

/* Call after the command line is parsed, but before the first pass */
void init_warnings(void)
{
	push_warnings();
	warning_state_init = warning_stack;
}


/* Call after each pass */
void reset_warnings(void)
{
	struct warning_stack *ws = warning_stack;

	/* Unwind the warning stack. We do NOT delete the last entry! */
	while (ws->next) {
		struct warning_stack *wst = ws;
		ws = ws->next;
		nasm_free(wst);
	}
	warning_stack = ws;
	memcpy(warning_state, ws->state, sizeof warning_state);
}

/*
 * This is called when processing a -w or -W option, or a warning directive.
 * Returns on if if the action was successful.
 *
 * Special pseudo-warnings:
 *
 *!other [on] any warning not specifially mentioned above
 *!  specifies any warning not included in any specific warning class.
 *
 *!all [all] all possible warnings
 *!  is an group alias for \e{all} warning classes.  Thus, \c{-w+all}
 *!  enables all available warnings, and \c{-w-all} disables warnings
 *!  entirely (since NASM 2.13).
 */
bool set_warning_status(const char *value)
{
	enum warn_action { WID_OFF, WID_ON, WID_RESET };
	enum warn_action action;
        const struct warning_alias *wa;
        size_t vlen;
	bool ok = false;
	uint8_t mask;

	value = nasm_skip_spaces(value);

	switch (*value) {
	case '-':
		action = WID_OFF;
		value++;
		break;
	case '+':
		action = WID_ON;
		value++;
		break;
	case '*':
		action = WID_RESET;
		value++;
		break;
	case 'N':
	case 'n':
		if (!nasm_strnicmp(value, "no-", 3)) {
			action = WID_OFF;
			value += 3;
			break;
		} else if (!nasm_stricmp(value, "none")) {
			action = WID_OFF;
			value = NULL;
			break;
		}
		/* else fall through */
	default:
		action = WID_ON;
		break;
	}

	mask = WARN_ST_ENABLED;

	if (value && !nasm_strnicmp(value, "error", 5)) {
		switch (value[5]) {
		case '=':
			mask = WARN_ST_ERROR;
			value += 6;
			break;
		case '\0':
			mask = WARN_ST_ERROR;
			value = NULL;
			break;
		default:
			/* Just an accidental prefix? */
			break;
		}
	}

	if (value && !nasm_stricmp(value, "all"))
		value = NULL;

        vlen = value ? strlen(value) : 0;

	/*
         * This is inefficient, but it shouldn't matter.
         * Note: warning_alias[0] is "all".
         */
	for (wa = warning_alias+1;
             wa < &warning_alias[NUM_WARNING_ALIAS]; wa++) {
            enum warn_index i = wa->warning;

            if (value) {
                char sep;

                if (nasm_strnicmp(value, wa->name, vlen))
                    continue;   /* Not a prefix */

                sep = wa->name[vlen];
                if (sep != '\0' && sep != '-')
                    continue;   /* Not a valid prefix */
            }

            ok = true; /* At least one action taken */
            switch (action) {
            case WID_OFF:
                warning_state[i] &= ~mask;
                break;
            case WID_ON:
                warning_state[i] |= mask;
                break;
            case WID_RESET:
                warning_state[i] &= ~mask;
                warning_state[i] |= warning_state_init->state[i] & mask;
                break;
            }
        }

        if (!ok && value) {
            /*!
             *!unknown-warning [off] unknown warning in -W/-w or warning directive
             *!  warns about a \c{-w} or \c{-W} option or a \c{[WARNING]} directive
             *!  that contains an unknown warning name or is otherwise not possible to process.
             */
            nasm_warn(WARN_UNKNOWN_WARNING, "unknown warning name: %s", value);
	}

	return ok;
}
