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

#include "nasmlib.h"
#include "raa.h"
#include "ilog2.h"

/*
 * Routines to manage a dynamic random access array of int64_ts which
 * may grow in size to be more than the largest single malloc'able
 * chunk.
 */

#define RAA_LAYERSHIFT	11      /* 2^this many items per layer */
#define RAA_LAYERSIZE	((size_t)1 << RAA_LAYERSHIFT)
#define RAA_LAYERMASK	(RAA_LAYERSIZE-1)

typedef struct RAA RAA;
typedef union RAA_UNION RAA_UNION;
typedef struct RAA_LEAF RAA_LEAF;
typedef struct RAA_BRANCH RAA_BRANCH;

union intorptr {
    int64_t i;
    void *p;
};

struct RAA {
    /* Last position in this RAA */
    raaindex endposn;

    /*
     * Number of layers below this one to get to the real data. 0
     * means this structure is a leaf, holding RAA_LAYERSIZE real
     * data items; 1 and above mean it's a branch, holding
     * RAA_LAYERSIZE pointers to the next level branch or leaf
     * structures.
     */
    unsigned int layers;

    /*
     * Number of real data items spanned by one position in the
     * `data' array at this level. This number is 0 trivially, for
     * a leaf (level 0): for a level n branch it should be
     * n*RAA_LAYERSHIFT.
     */
    unsigned int shift;

    /*
     * The actual data
     */
    union RAA_UNION {
        struct RAA_LEAF {
            union intorptr data[RAA_LAYERSIZE];
        } l;
        struct RAA_BRANCH {
            struct RAA *data[RAA_LAYERSIZE];
        } b;
    } u;
};

#define LEAFSIZ (sizeof(RAA)-sizeof(RAA_UNION)+sizeof(RAA_LEAF))
#define BRANCHSIZ (sizeof(RAA)-sizeof(RAA_UNION)+sizeof(RAA_BRANCH))

static struct RAA *raa_init_layer(raaindex posn, unsigned int layers)
{
    struct RAA *r;
    raaindex posmask;

    r = nasm_zalloc((layers == 0) ? LEAFSIZ : BRANCHSIZ);
    r->shift = layers * RAA_LAYERSHIFT;
    r->layers    = layers;
    posmask = ((raaindex)RAA_LAYERSIZE << r->shift) - 1;
    r->endposn   = posn | posmask;
    return r;
}

void raa_free(struct RAA *r)
{
    if (!r)
        return;

    if (r->layers) {
        struct RAA **p = r->u.b.data;
        size_t i;
        for (i = 0; i < RAA_LAYERSIZE; i++)
            raa_free(*p++);
    }
    nasm_free(r);
}

static const union intorptr *real_raa_read(struct RAA *r, raaindex posn)
{
    nasm_assert(posn <= (~(raaindex)0 >> 1));

    if (unlikely(!r || posn > r->endposn))
        return NULL;            /* Beyond the end */

    while (r->layers) {
        size_t l = (posn >> r->shift) & RAA_LAYERMASK;
        r = r->u.b.data[l];
        if (!r)
            return NULL;        /* Not present */
    }
    return &r->u.l.data[posn & RAA_LAYERMASK];
}

int64_t raa_read(struct RAA *r, raaindex pos)
{
    const union intorptr *ip;

    ip = real_raa_read(r, pos);
    return ip ? ip->i : 0;
}

void *raa_read_ptr(struct RAA *r, raaindex pos)
{
    const union intorptr *ip;

    ip = real_raa_read(r, pos);
    return ip ? ip->p : NULL;
}


static struct RAA *
real_raa_write(struct RAA *r, raaindex posn, union intorptr value)
{
    struct RAA *result;

    nasm_assert(posn <= (~(raaindex)0 >> 1));

    if (unlikely(!r)) {
        /* Create a new top-level RAA */
        r = raa_init_layer(posn, ilog2_64(posn)/RAA_LAYERSHIFT);
    } else {
        while (unlikely(r->endposn < posn)) {
            /* We need to add layers to an existing RAA */
            struct RAA *s = raa_init_layer(r->endposn, r->layers + 1);
            s->u.b.data[0] = r;
            r = s;
        }
    }

    result = r;

    while (r->layers) {
        struct RAA **s;
        size_t l = (posn >> r->shift) & RAA_LAYERMASK;
        s = &r->u.b.data[l];
        if (unlikely(!*s))
            *s = raa_init_layer(posn, r->layers - 1);
        r = *s;
    }
    r->u.l.data[posn & RAA_LAYERMASK] = value;

    return result;
}

struct RAA *raa_write(struct RAA *r, raaindex posn, int64_t value)
{
    union intorptr ip;

    ip.i = value;
    return real_raa_write(r, posn, ip);
}

struct RAA *raa_write_ptr(struct RAA *r, raaindex posn, void *value)
{
    union intorptr ip;

    ip.p = value;
    return real_raa_write(r, posn, ip);
}
