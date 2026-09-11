/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2020 The NASM Authors - All Rights Reserved */

/*
 * dbginfo.h - debugging info structures
 */

#ifndef NASM_DBGINFO_H
#define NASM_DBGINFO_H

#include "compiler.h"
#include "srcfile.h"
#include "rbtree.h"

struct debug_macro_def;         /* Definition */
struct debug_macro_inv;         /* Invocation */
struct debug_macro_addr;        /* Address range */

/*
 * Definitions structure, one for each non-.nolist macro invoked
 * anywhere in the program; unique for each macro, even if a macro is
 * redefined and/or overloaded.
 */
struct debug_macro_def {
    struct debug_macro_def *next; /* List of definitions */
    const char *name;            /* Macro name */
    struct src_location where;   /* Start of definition */
    size_t ninv;                 /* Call count */
};

/*
 * Invocation structure. One for each invocation of a non-.nolist macro.
 */
struct debug_macro_inv_list {
    struct debug_macro_inv *l;
    size_t n;
};

struct debug_macro_inv {
    struct debug_macro_inv *next; /* List of same-level invocations */
    struct debug_macro_inv_list down;
    struct debug_macro_inv *up;   /* Parent invocation */
    struct debug_macro_def *def;  /* Macro definition */
    struct src_location where;    /* Start of invocation */
    struct {                      /* Address range pointers */
        struct rbtree *tree;           /* rbtree of address ranges */
        struct debug_macro_addr *last; /* Quick lookup for latest section */
    } addr;
    uint32_t naddr;             /* Number of address ranges */
    int32_t  lastseg;           /* lastaddr segment number  */
};

/*
 * Address range structure. An rbtree containing one address range for each
 * section which this particular macro has generated code/data/space into.
 */
struct debug_macro_addr {
    struct rbtree tree;          /* rbtree; key = index, must be first */
    struct debug_macro_addr *up; /* same section in parent invocation */
    uint64_t start;              /* starting offset */
    uint64_t len;                /* length of range */
};

/*
 * Complete information structure */
struct debug_macro_info {
    struct debug_macro_inv_list inv;
    struct debug_macro_def_list {
        struct debug_macro_def *l;
        size_t n;
    } def;
};

static inline int32_t debug_macro_seg(const struct debug_macro_addr *dma)
{
    return dma->tree.key;
}

/* Get/create a addr structure for the macro we are emitting for */
struct debug_macro_addr *debug_macro_get_addr(int32_t seg);

/* The macro we are currently emitting for, if any */
extern struct debug_macro_inv *debug_current_macro;

#endif /* NASM_DBGINFO_H */
