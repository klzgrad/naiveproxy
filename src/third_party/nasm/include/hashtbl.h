/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2018 The NASM Authors - All Rights Reserved */

/*
 * hashtbl.h
 *
 * Efficient dictionary hash table class.
 */

#ifndef NASM_HASHTBL_H
#define NASM_HASHTBL_H

#include "nasmlib.h"

struct hash_node {
    uint64_t hash;
    const void *key;
    size_t keylen;
    void *data;
};

struct hash_table {
    struct hash_node *table;
    size_t load;
    size_t size;
    size_t max_load;
};

struct hash_insert {
    struct hash_table *head;
    struct hash_node *where;
    struct hash_node node;
};

struct hash_iterator {
    const struct hash_table *head;
    const struct hash_node *next;
};

uint64_t pure_func crc64(uint64_t crc, const char *string);
uint64_t pure_func crc64i(uint64_t crc, const char *string);
uint64_t pure_func crc64b(uint64_t crc, const void *data, size_t len);
uint64_t pure_func crc64ib(uint64_t crc, const void *data, size_t len);
#define CRC64_INIT UINT64_C(0xffffffffffffffff)

static inline uint64_t crc64_byte(uint64_t crc, uint8_t v)
{
    extern const uint64_t crc64_tab[256];
    return crc64_tab[(uint8_t)(v ^ crc)] ^ (crc >> 8);
}

uint32_t pure_func crc32b(uint32_t crc, const void *data, size_t len);

void **hash_find(struct hash_table *head, const char *string,
		struct hash_insert *insert);
void **hash_findb(struct hash_table *head, const void *key, size_t keylen,
		struct hash_insert *insert);
void **hash_findi(struct hash_table *head, const char *string,
		struct hash_insert *insert);
void **hash_findib(struct hash_table *head, const void *key, size_t keylen,
                   struct hash_insert *insert);
void **hash_add(struct hash_insert *insert, const void *key, void *data);
static inline void hash_iterator_init(const struct hash_table *head,
                                      struct hash_iterator *iterator)
{
    iterator->head = head;
    iterator->next = head->table;
}
const struct hash_node *hash_iterate(struct hash_iterator *iterator);

#define hash_for_each(_head,_it,_np) \
    for (hash_iterator_init((_head), &(_it)), (_np) = hash_iterate(&(_it)) ; \
         (_np) ; (_np) = hash_iterate(&(_it)))

void hash_free(struct hash_table *head);
void hash_free_all(struct hash_table *head, bool free_keys);

#endif /* NASM_HASHTBL_H */
