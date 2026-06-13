/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2020 The NASM Authors - All Rights Reserved */

/*
 * strlist.h - list of unique, ordered strings
 */

#ifndef NASM_STRLIST_H
#define NASM_STRLIST_H

#include "compiler.h"
#include "nasmlib.h"
#include "hashtbl.h"

struct strlist_entry {
	struct strlist_entry	*next;
	size_t			offset;
	size_t			size;
	intorptr		pvt;
	char			str[1];
};

struct strlist {
	struct hash_table	hash;
	struct strlist_entry	*head, **tailp;
	size_t			nstr, size;
	bool			uniq;
};

static inline const struct strlist_entry *
strlist_head(const struct strlist *list)
{
	return list ? list->head : NULL;
}
static inline struct strlist_entry *strlist_tail(struct strlist *list)
{
	if (!list || !list->head)
		return NULL;
	return container_of(list->tailp, struct strlist_entry, next);
}
static inline size_t strlist_count(const struct strlist *list)
{
	return list ? list->nstr : 0;
}
static inline size_t strlist_size(const struct strlist *list)
{
	return list ? list->size : 0;
}

struct strlist * safe_alloc strlist_alloc(bool uniq);
const struct strlist_entry *strlist_add(struct strlist *list, const char *str);
const struct strlist_entry * printf_func(2, 3)
	strlist_printf(struct strlist *list, const char *fmt, ...);
const struct strlist_entry * vprintf_func(2)
	strlist_vprintf(struct strlist *list, const char *fmt, va_list ap);
const struct strlist_entry *
strlist_find(const struct strlist *list, const char *str);
void * safe_alloc strlist_linearize(const struct strlist *list, char sep);
void strlist_write(const struct strlist *list, const char *sep, FILE *f);
void strlist_free(struct strlist **listp);
#define strlist_for_each(p,h) list_for_each((p), strlist_head(h))

#endif /* NASM_STRLIST_H */
