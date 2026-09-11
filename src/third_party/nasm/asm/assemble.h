/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2025 The NASM Authors - All Rights Reserved */

/*
 * assemble.h - header file for stuff private to the assembler
 */

#ifndef NASM_ASSEMBLE_H
#define NASM_ASSEMBLE_H

#include "nasm.h"
#include "iflag.h"
#include "asmutil.h"

extern iflag_t cpu, cmd_cpu;
void set_cpu(const char *cpuspec);

extern bool in_absolute;        /* Are we in an absolute segment? */
extern struct location absolute;

int64_t increment_offset(int64_t delta);
void process_insn(insn *instruction);

bool directive_valid(const char *);
bool process_directives(char *);
void process_pragma(char *);

/* Is this a compile-time absolute constant? */
static inline bool op_compile_abs(const struct operand * const op)
{
    if (op->opflags & OPFLAG_UNKNOWN)
        return true;            /* Be optimistic in pass 1 */
    if (op->opflags & OPFLAG_RELATIVE)
        return false;
    if (op->wrt != NO_SEG)
        return false;

    return op->segment == NO_SEG;
}

/* Is this a compile-time relative constant? */
static inline bool op_compile_rel(const insn * const ins,
                                  const struct operand * const op)
{
    if (op->opflags & OPFLAG_UNKNOWN)
        return true;            /* Be optimistic in pass 1 */
    if (!(op->opflags & OPFLAG_RELATIVE))
        return false;
    if (op->wrt != NO_SEG)      /* Is this correct?! */
        return false;

    return op->segment == ins->loc.segment;
}

#endif
