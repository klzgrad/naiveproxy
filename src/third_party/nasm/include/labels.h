/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2018 The NASM Authors - All Rights Reserved */

/*
 * labels.h  header file for labels.c
 */

#ifndef LABELS_H
#define LABELS_H

#include "compiler.h"

enum label_type {
    LBL_none = -1,              /* No label */
    LBL_LOCAL = 0,              /* Must be zero */
    LBL_STATIC,
    LBL_GLOBAL,
    LBL_EXTERN,
    LBL_REQUIRED,               /* Like extern but emit even if unused */
    LBL_COMMON,
    LBL_SPECIAL,                /* Magic symbols like ..start */
    LBL_BACKEND                 /* Backend-defined symbols like ..got */
};

enum label_type lookup_label(const char *label, int32_t *segment, int64_t *offset);
static inline bool is_extern(enum label_type type)
{
    return type == LBL_EXTERN || type == LBL_REQUIRED;
}
void define_label(const char *label, int32_t segment, int64_t offset,
                  bool normal);
void backend_label(const char *label, int32_t segment, int64_t offset);
bool declare_label(const char *label, enum label_type type,
                   const char *special);
void set_label_mangle(enum directive which, const char *what);
int init_labels(void);
void cleanup_labels(void);
const char *local_scope(const char *label);

extern uint64_t global_offset_changed;

#endif /* LABELS_H */
