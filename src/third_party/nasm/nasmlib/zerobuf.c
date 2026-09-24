/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2016 The NASM Authors - All Rights Reserved */

/*
 * nasmlib.c	library routines for the Netwide Assembler
 */

#include "compiler.h"
#include "nasmlib.h"

/* Uninitialized -> all zero by C spec */
const uint8_t zero_buffer[ZERO_BUF_SIZE];
