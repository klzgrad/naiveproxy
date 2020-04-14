/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2018 The NASM Authors - All Rights Reserved
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
 * strlist.c - list of unique, ordered strings
 */

#include "strlist.h"

/*
 * Create a string list
 */
StrList *strlist_allocate(void)
{
    StrList *list;

    nasm_new(list);
    hash_init(&list->hash, HASH_MEDIUM);
    list->tailp = &list->head;

    return list;
}

/*
 * Append a string to a string list if and only if it isn't
 * already there.  Return true if it was added.
 */
bool strlist_add_string(StrList *list, const char *str)
{
    struct hash_insert hi;
    struct strlist_entry *sl;
    size_t l;

    if (!list)
        return false;

    if (hash_find(&list->hash, str, &hi))
        return false;           /* Already present */

    l = strlen(str);

    sl = nasm_malloc(sizeof(struct strlist_entry) + l);
    sl->len = l;
    memcpy(sl->str, str, l+1);
    sl->next = NULL;
    *list->tailp = sl;
    list->tailp = &sl->next;

    hash_add(&hi, sl->str, (void *)sl);
    return true;
}

/*
 * Free a string list
 */
void strlist_free(StrList *list)
{
    if (!list)
        return;

    hash_free_all(&list->hash, false);
    nasm_free(list);
}
