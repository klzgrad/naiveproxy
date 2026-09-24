/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2017 The NASM Authors - All Rights Reserved */

#include "perfhash.h"
#include "hashtbl.h"            /* For crc64i() */

int perfhash_find(const struct perfect_hash *hash, const char *str)
{
    uint32_t k1, k2;
    uint64_t crc;
    uint16_t ix;

    crc = crc64i(hash->crcinit, str);
    k1 = (uint32_t)crc & hash->hashmask;
    k2 = ((uint32_t)(crc >> 32) & hash->hashmask) + 1;

    ix = hash->hashvals[k1] + hash->hashvals[k2];

    if (ix >= hash->tbllen ||
        !hash->strings[ix] ||
        nasm_stricmp(str, hash->strings[ix]))
        return hash->errval;

    return hash->tbloffs + ix;
}
