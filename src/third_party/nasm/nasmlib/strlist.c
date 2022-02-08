/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2020 The NASM Authors - All Rights Reserved
 *   See the file AUTHORS included with the NASM distribution for
 *   the specific copyright holders.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following
 *   conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *
 *     THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 *     CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *     INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *     MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *     DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 *     CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *     SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *     NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *     LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *     HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 *     OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 *     EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ----------------------------------------------------------------------- */

/*
 * strlist.c - list of ordered strings, optionally made unique
 */

#include "strlist.h"

/*
 * Create a string list. The list can be uniqizing or not.
 */
struct strlist *strlist_alloc(bool uniq)
{
	struct strlist *list = nasm_zalloc(sizeof(*list));
	list->tailp = &list->head;
        list->uniq = uniq;
	return list;
}

/*
 * Append a string to a string list. Return the entry pointer, which
 * may be a pre-existing entry for a uniqizing list.
 */

static const struct strlist_entry *
strlist_add_common(struct strlist *list, struct strlist_entry *e,
		   struct hash_insert *hi)
{
        e->offset = list->size;
        e->next = NULL;

	*list->tailp = e;
	list->tailp = &e->next;
	list->nstr++;
	list->size += e->size;

        if (list->uniq)
		hash_add(hi, e->str, (void *)e);

	return e;
}

const struct strlist_entry *
strlist_add(struct strlist *list, const char *str)
{
	struct strlist_entry *e;
	struct hash_insert hi;
	size_t size;

	if (!list)
		return NULL;

	size = strlen(str) + 1;
	if (list->uniq) {
		void **dp = hash_findb(&list->hash, str, size, &hi);
		if (dp)
			return *dp;
	}

	/* Structure already has char[1] as EOS */
	e = nasm_malloc(sizeof(*e) - 1 + size);
	e->size = size;
	memcpy(e->str, str, size);

	return strlist_add_common(list, e, &hi);
}

/*
 * printf() to a string list
 */
const struct strlist_entry *
strlist_vprintf(struct strlist *list, const char *fmt, va_list ap)
{
	/* clang miscompiles offsetin() unless e is initialized here */
	struct strlist_entry *e = NULL;
	struct hash_insert hi;

	if (!list)
		return NULL;

	e = nasm_vaxprintf(offsetin(*e, str), fmt, ap);
	e->size = nasm_last_string_size();

	if (list->uniq) {
		void **dp = hash_findb(&list->hash, e->str, e->size, &hi);
		if (dp) {
			nasm_free(e);
			return *dp;
		}
	}

	return strlist_add_common(list, e, &hi);
}

const struct strlist_entry *
strlist_printf(struct strlist *list, const char *fmt, ...)
{
	va_list ap;
	const struct strlist_entry *e;

	va_start(ap, fmt);
	e = strlist_vprintf(list, fmt, ap);
	va_end(ap);

	return e;
}

/*
 * Free a string list. Sets the pointed to pointer to NULL.
 */
void strlist_free(struct strlist **listp)
{
	struct strlist *list = *listp;
	struct strlist_entry *e, *tmp;

	if (!list)
		return;

	if (list->uniq)
		hash_free(&list->hash);

	list_for_each_safe(e, tmp, list->head)
		nasm_free(e);

	nasm_free(list);
	*listp = NULL;
}

/*
 * Search the string list for an entry. If found, return the entry pointer.
 * Only possible on a uniqizing list.
 */
const struct strlist_entry *
strlist_find(const struct strlist *list, const char *str)
{
	void **hf;

        nasm_assert(list->uniq);

	hf = hash_find((struct hash_table *)&list->hash, str, NULL);
	return hf ? *hf : NULL;
}

/*
 * Produce a linearized buffer containing the whole list, in order;
 * The character "sep" is the separator between strings; this is
 * typically either 0 or '\n'. strlist_size() will give the size of
 * the returned buffer.
 */
void *strlist_linearize(const struct strlist *list, char sep)
{
	const struct strlist_entry *sl;
	char *buf = nasm_malloc(list->size);
	char *p = buf;

	strlist_for_each(sl, list) {
		p = mempcpy(p, sl->str, sl->size);
		p[-1] = sep;
	}

	return buf;
}

/*
 * Output a string list to a file. The separator can be any string.
 */
void strlist_write(const struct strlist *list, const char *sep, FILE *f)
{
	const struct strlist_entry *sl;
	size_t seplen = strlen(sep);

	strlist_for_each(sl, list) {
		fwrite(sl->str, 1, sl->size - 1, f);
		fwrite(sep, 1, seplen, f);
	}
}
