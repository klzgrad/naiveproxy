/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2009 The NASM Authors - All Rights Reserved */

/*
 * parser.h   header file for the parser module of the Netwide
 *            Assembler
 */

#ifndef NASM_PARSER_H
#define NASM_PARSER_H

insn *parse_line(char *buffer, insn *result, const int bits);
void cleanup_insn(insn *instruction);

#endif
