/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2009 The NASM Authors - All Rights Reserved */

/*
 * sync.c   the Netwide Disassembler synchronisation processing module
 */

#include "compiler.h"


#include "nasmlib.h"
#include "sync.h"

#define SYNC_MAX_SHIFT          31
#define SYNC_MAX_SIZE           (1U << SYNC_MAX_SHIFT)

/* initial # of sync points (*must* be power of two)*/
#define SYNC_INITIAL_CHUNK      (1U << 12)

/*
 * This lot manages the current set of sync points by means of a
 * heap (priority queue) structure.
 */

static struct Sync {
    uint64_t pos;
    uint32_t length;
} *synx;

static uint32_t max_synx, nsynx;

static inline void swap_sync(uint32_t dst, uint32_t src)
{
    struct Sync t = synx[dst];
    synx[dst] = synx[src];
    synx[src] = t;
}

void init_sync(void)
{
    max_synx = SYNC_INITIAL_CHUNK;
    synx = nasm_malloc((max_synx + 1) * sizeof(*synx));
    nsynx = 0;
}

void add_sync(uint64_t pos, uint32_t length)
{
    uint32_t i;

    if (nsynx >= max_synx) {
        if (max_synx >= SYNC_MAX_SIZE) /* too many sync points! */
            return;
        max_synx = (max_synx << 1);
        synx = nasm_realloc(synx, (max_synx + 1) * sizeof(*synx));
    }

    nsynx++;
    synx[nsynx].pos = pos;
    synx[nsynx].length = length;

    for (i = nsynx; i > 1; i /= 2) {
        if (synx[i / 2].pos > synx[i].pos)
            swap_sync(i / 2, i);
    }
}

uint64_t next_sync(uint64_t position, uint32_t *length)
{
    while (nsynx > 0 && synx[1].pos + synx[1].length <= position) {
        uint32_t i, j;

        swap_sync(nsynx, 1);
        nsynx--;

        i = 1;
        while (i * 2 <= nsynx) {
            j = i * 2;
            if (synx[j].pos < synx[i].pos &&
                (j + 1 > nsynx || synx[j + 1].pos > synx[j].pos)) {
                swap_sync(j, i);
                i = j;
            } else if (j + 1 <= nsynx && synx[j + 1].pos < synx[i].pos) {
                swap_sync(j + 1, i);
                i = j + 1;
            } else
                break;
        }
    }

    if (nsynx > 0) {
        if (length)
            *length = synx[1].length;
        return synx[1].pos;
    } else {
        if (length)
            *length = 0L;
        return 0;
    }
}
