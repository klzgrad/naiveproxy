/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2016 The NASM Authors - All Rights Reserved */

/*
 * tables.h
 *
 * Declarations for auto-generated tables
 */

#ifndef NASM_TABLES_H
#define NASM_TABLES_H

#include "compiler.h"
#include "insnsi.h"		/* For enum opcode */

/* --- From insns.dat via insns.pl: --- */

/* insnsn.c */
extern const char * const nasm_insn_names[];

/* --- From regs.dat via regs.pl: --- */

/* regs.c */
extern const char * const nasm_reg_names[];
/* regflags.c */
typedef uint64_t opflags_t;
typedef uint16_t  decoflags_t;
extern const opflags_t nasm_reg_flags[];
/* regvals.c */
extern const int nasm_regvals[];

#endif /* NASM_TABLES_H */
