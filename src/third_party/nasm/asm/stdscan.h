/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2009 The NASM Authors - All Rights Reserved */

/*
 * stdscan.h	header file for stdscan.c
 */

#ifndef NASM_STDSCAN_H
#define NASM_STDSCAN_H

/* Standard scanner */
struct stdscan_state;

void stdscan_set(const struct stdscan_state *);
const struct stdscan_state *stdscan_get(void);
char * pure_func stdscan_tell(void);
void stdscan_reset(char *buffer);
int stdscan(void *pvt, struct tokenval *tv);
void stdscan_pushback(const struct tokenval *tv);
int nasm_token_hash(const char *token, struct tokenval *tv);
void stdscan_cleanup(void);

#endif
