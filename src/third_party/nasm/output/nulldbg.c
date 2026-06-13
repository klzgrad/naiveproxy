/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2014 The NASM Authors - All Rights Reserved */

#include "nasm.h"
#include "nasmlib.h"
#include "outlib.h"

void null_debug_init(void)
{
}

void null_debug_linenum(const char *filename, int32_t linenumber, int32_t segto)
{
	(void)filename;
	(void)linenumber;
	(void)segto;
}

void null_debug_deflabel(char *name, int32_t segment, int64_t offset,
                         int is_global, char *special)
{
	(void)name;
	(void)segment;
	(void)offset;
	(void)is_global;
	(void)special;
}

void null_debug_directive(const char *directive, const char *params)
{
	(void)directive;
	(void)params;
}

void null_debug_typevalue(int32_t type)
{
	(void)type;
}

void null_debug_output(int type, void *param)
{
	(void)type;
	(void)param;
}

void null_debug_cleanup(void)
{
}

const struct dfmt null_debug_form = {
    "Null",
    "null",
    null_debug_init,
    null_debug_linenum,
    null_debug_deflabel,
    NULL,                       /* .debug_smacros */
    NULL,                       /* .debug_include */
    NULL,                       /* .debug_mmacros */
    null_debug_directive,
    null_debug_typevalue,
    null_debug_output,
    null_debug_cleanup,
    NULL                        /* pragma list */
};

const struct dfmt * const null_debug_arr[2] = { &null_debug_form, NULL };
