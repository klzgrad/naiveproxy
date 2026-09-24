/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2016 The NASM Authors - All Rights Reserved */

/*
 * config/unknown.h
 *
 * Compiler definitions for an unknown compiler.  Assume the worst.
 */

#ifndef NASM_CONFIG_UNKNOWN_H
#define NASM_CONFIG_UNKNOWN_H

/* Assume these don't exist */
#ifndef inline
# define inline
#endif
#ifndef restrict
# define restrict
#endif

#endif /* NASM_CONFIG_UNKNOWN_H */
