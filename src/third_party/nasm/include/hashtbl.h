/* ----------------------------------------------------------------------- *
 *   
 *   Copyright 1996-2017 The NASM Authors - All Rights Reserved
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
 * hashtbl.h
 *
 * Efficient dictionary hash table class.
 */

#ifndef NASM_HASHTBL_H
#define NASM_HASHTBL_H

#include <stddef.h>
#include "nasmlib.h"

struct hash_tbl_node {
    uint64_t hash;
    const char *key;
    void *data;
};

struct hash_table {
    struct hash_tbl_node *table;
    size_t load;
    size_t size;
    size_t max_load;
};

struct hash_insert {
    uint64_t hash;
    struct hash_table *head;
    struct hash_tbl_node *where;
};

uint64_t crc64(uint64_t crc, const char *string);
uint64_t crc64i(uint64_t crc, const char *string);
#define CRC64_INIT UINT64_C(0xffffffffffffffff)

/* Some reasonable initial sizes... */
#define HASH_SMALL	4
#define HASH_MEDIUM	16
#define HASH_LARGE	256

void hash_init(struct hash_table *head, size_t size);
void **hash_find(struct hash_table *head, const char *string,
		struct hash_insert *insert);
void **hash_findi(struct hash_table *head, const char *string,
		struct hash_insert *insert);
void **hash_add(struct hash_insert *insert, const char *string, void *data);
void *hash_iterate(const struct hash_table *head,
		   struct hash_tbl_node **iterator,
		   const char **key);
void hash_free(struct hash_table *head);
void hash_free_all(struct hash_table *head, bool free_keys);

#endif /* NASM_HASHTBL_H */
