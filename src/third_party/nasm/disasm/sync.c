/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2009 The NASM Authors - All Rights Reserved
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
 * sync.c   the Netwide Disassembler synchronisation processing module
 */

#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

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
