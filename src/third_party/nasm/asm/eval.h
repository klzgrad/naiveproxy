/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2009 The NASM Authors - All Rights Reserved */

/*
 * eval.h   header file for eval.c
 */

#ifndef NASM_EVAL_H
#define NASM_EVAL_H

/*
 * The evaluator itself.
 */
expr *evaluate(scanner sc, void *scprivate, struct tokenval *tv,
               int *fwref, bool critical, struct eval_hints *hints);

void eval_cleanup(void);

#endif
