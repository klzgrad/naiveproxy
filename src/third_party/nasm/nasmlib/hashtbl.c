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
 * hashtbl.c
 *
 * Efficient dictionary hash table class.
 */

#include "compiler.h"

#include "nasm.h"
#include "hashtbl.h"

#define HASH_MAX_LOAD   2	/* Higher = more memory-efficient, slower */
#define HASH_INIT_SIZE  16      /* Initial size (power of 2, min 4) */

#define hash_calc(key,keylen)   crc64b(CRC64_INIT, (key), (keylen))
#define hash_calci(key,keylen)  crc64ib(CRC64_INIT, (key), (keylen))
#define hash_max_load(size)     ((size) * (HASH_MAX_LOAD - 1) / HASH_MAX_LOAD)
#define hash_expand(size)       ((size) << 1)
#define hash_mask(size)         ((size) - 1)
#define hash_pos(hash, mask)    ((hash) & (mask))
#define hash_inc(hash, mask)    ((((hash) >> 32) & (mask)) | 1) /* always odd */
#define hash_pos_next(pos, inc, mask) (((pos) + (inc)) & (mask))

static void hash_init(struct hash_table *head)
{
    head->size     = HASH_INIT_SIZE;
    head->load     = 0;
    head->max_load = hash_max_load(head->size);
    nasm_newn(head->table, head->size);
}

/*
 * Find an entry in a hash table.  The key can be any binary object.
 *
 * On failure, if "insert" is non-NULL, store data in that structure
 * which can be used to insert that node using hash_add().
 * See hash_add() for constraints on the uses of the insert object.
 *
 * On success, return a pointer to the "data" element of the hash
 * structure.
 */
void **hash_findb(struct hash_table *head, const void *key,
                  size_t keylen, struct hash_insert *insert)
{
    struct hash_node *np = NULL;
    struct hash_node *tbl = head->table;
    uint64_t hash = hash_calc(key, keylen);
    size_t mask = hash_mask(head->size);
    size_t pos = hash_pos(hash, mask);
    size_t inc = hash_inc(hash, mask);

    if (likely(tbl)) {
        while ((np = &tbl[pos])->key) {
            if (hash == np->hash &&
                keylen == np->keylen &&
                !memcmp(key, np->key, keylen))
                return &np->data;
            pos = hash_pos_next(pos, inc, mask);
        }
    }

    /* Not found.  Store info for insert if requested. */
    if (insert) {
        insert->node.hash = hash;
        insert->node.key = key;
        insert->node.keylen = keylen;
        insert->node.data = NULL;
        insert->head  = head;
        insert->where = np;
    }
    return NULL;
}

/*
 * Same as hash_findb(), but for a C string.
 */
void **hash_find(struct hash_table *head, const char *key,
                 struct hash_insert *insert)
{
    return hash_findb(head, key, strlen(key)+1, insert);
}

/*
 * Same as hash_findb(), but for case-insensitive hashing.
 */
void **hash_findib(struct hash_table *head, const void *key, size_t keylen,
                   struct hash_insert *insert)
{
    struct hash_node *np = NULL;
    struct hash_node *tbl = head->table;
    uint64_t hash = hash_calci(key, keylen);
    size_t mask = hash_mask(head->size);
    size_t pos = hash_pos(hash, mask);
    size_t inc = hash_inc(hash, mask);

    if (likely(tbl)) {
        while ((np = &tbl[pos])->key) {
            if (hash == np->hash &&
                keylen == np->keylen &&
                !nasm_memicmp(key, np->key, keylen))
                return &np->data;
            pos = hash_pos_next(pos, inc, mask);
        }
    }

    /* Not found.  Store info for insert if requested. */
    if (insert) {
        insert->node.hash = hash;
        insert->node.key = key;
        insert->node.keylen = keylen;
        insert->node.data = NULL;
        insert->head  = head;
        insert->where = np;
    }
    return NULL;
}

/*
 * Same as hash_find(), but for case-insensitive hashing.
 */
void **hash_findi(struct hash_table *head, const char *key,
                  struct hash_insert *insert)
{
    return hash_findib(head, key, strlen(key)+1, insert);
}

/*
 * Insert node.  Return a pointer to the "data" element of the newly
 * created hash node.
 *
 * The following constraints apply:
 * 1. A call to hash_add() invalidates all other outstanding hash_insert
 *    objects; attempting to use them causes a wild pointer reference.
 * 2. The key provided must exactly match the key passed to hash_find*(),
 *    but it does not have to point to the same storage address. The key
 *    buffer provided to this function must not be freed for the lifespan
 *    of the hash. NULL will use the same pointer that was passed to
 *    hash_find*().
 */
void **hash_add(struct hash_insert *insert, const void *key, void *data)
{
    struct hash_table *head  = insert->head;
    struct hash_node *np = insert->where;

    if (unlikely(!np)) {
        hash_init(head);
        /* The hash table is empty, so we don't need to iterate here */
        np = &head->table[hash_pos(insert->node.hash, hash_mask(head->size))];
    }

    /*
     * Insert node.  We can always do this, even if we need to
     * rebalance immediately after.
     */
    *np = insert->node;
    np->data = data;
    if (key)
        np->key = key;

    if (unlikely(++head->load > head->max_load)) {
        /* Need to expand the table */
        size_t newsize           = hash_expand(head->size);
        struct hash_node *newtbl;
        size_t mask              = hash_mask(newsize);
        struct hash_node *op, *xp;
        size_t i;

        nasm_newn(newtbl, newsize);

        /* Rebalance all the entries */
        for (i = 0, op = head->table; i < head->size; i++, op++) {
            if (op->key) {
                size_t pos = hash_pos(op->hash, mask);
                size_t inc = hash_inc(op->hash, mask);

                while ((xp = &newtbl[pos])->key)
                    pos = hash_pos_next(pos, inc, mask);

                *xp = *op;
                if (op == np)
                    np = xp;
            }
        }
        nasm_free(head->table);

        head->table    = newtbl;
        head->size     = newsize;
        head->max_load = hash_max_load(newsize);
    }

    return &np->data;
}

/*
 * Iterate over all members of a hash set. For the first call, iter
 * should be as initialized by hash_iterator_init(). Returns a struct
 * hash_node representing the current object, or NULL if we have
 * reached the end of the hash table.
 *
 * Calling hash_add() will invalidate the iterator.
 */
const struct hash_node *hash_iterate(struct hash_iterator *iter)
{
    const struct hash_table *head = iter->head;
    const struct hash_node *cp = iter->next;
    const struct hash_node *ep = head->table + head->size;

    /* For an empty table, cp == ep == NULL */
    while (cp < ep) {
        if (cp->key) {
            iter->next = cp+1;
            return cp;
        }
        cp++;
    }

    iter->next = head->table;
    return NULL;
}

/*
 * Free the hash itself.  Doesn't free the data elements; use
 * hash_iterate() to do that first, if needed.  This function is normally
 * used when the hash data entries are either freed separately, or
 * compound objects which can't be freed in a single operation.
 */
void hash_free(struct hash_table *head)
{
    void *p = head->table;
    memset(head, 0, sizeof *head);
    nasm_free(p);
}

/*
 * Frees the hash *and* all data elements.  This is applicable only in
 * the case where the data element is a single allocation.  If the
 * second argument is false, the key string is part of the data
 * allocation or belongs to an allocation which will be freed
 * separately, if it is true the keys are also freed.
 */
void hash_free_all(struct hash_table *head, bool free_keys)
{
    struct hash_iterator it;
    const struct hash_node *np;

    hash_for_each(head, it, np) {
        if (np->data)
            nasm_free(np->data);
        if (free_keys && np->key)
            nasm_free((void *)np->key);
    }

    hash_free(head);
}
