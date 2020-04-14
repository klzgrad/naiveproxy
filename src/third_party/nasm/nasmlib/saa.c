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

#include "compiler.h"
#include "nasmlib.h"
#include "saa.h"

/* Aggregate SAA components smaller than this */
#define SAA_BLKSHIFT	16
#define SAA_BLKLEN	((size_t)1 << SAA_BLKSHIFT)

struct SAA *saa_init(size_t elem_len)
{
    struct SAA *s;
    char *data;

    s = nasm_zalloc(sizeof(struct SAA));

    if (elem_len >= SAA_BLKLEN)
        s->blk_len = elem_len;
    else
        s->blk_len = SAA_BLKLEN - (SAA_BLKLEN % elem_len);

    s->elem_len = elem_len;
    s->length = s->blk_len;
    data = nasm_malloc(s->blk_len);
    s->nblkptrs = s->nblks = 1;
    s->blk_ptrs = nasm_malloc(sizeof(char *));
    s->blk_ptrs[0] = data;
    s->wblk = s->rblk = &s->blk_ptrs[0];

    return s;
}

void saa_free(struct SAA *s)
{
    char **p;
    size_t n;

    for (p = s->blk_ptrs, n = s->nblks; n; p++, n--)
        nasm_free(*p);

    nasm_free(s->blk_ptrs);
    nasm_free(s);
}

/* Add one allocation block to an SAA */
static void saa_extend(struct SAA *s)
{
    size_t blkn = s->nblks++;

    if (blkn >= s->nblkptrs) {
        size_t rindex = s->rblk - s->blk_ptrs;
        size_t windex = s->wblk - s->blk_ptrs;

        s->nblkptrs <<= 1;
        s->blk_ptrs =
            nasm_realloc(s->blk_ptrs, s->nblkptrs * sizeof(char *));

        s->rblk = s->blk_ptrs + rindex;
        s->wblk = s->blk_ptrs + windex;
    }

    s->blk_ptrs[blkn] = nasm_malloc(s->blk_len);
    s->length += s->blk_len;
}

void *saa_wstruct(struct SAA *s)
{
    void *p;

    nasm_assert((s->wpos % s->elem_len) == 0);

    if (s->wpos + s->elem_len > s->blk_len) {
        nasm_assert(s->wpos == s->blk_len);
        if (s->wptr + s->elem_len > s->length)
            saa_extend(s);
        s->wblk++;
        s->wpos = 0;
    }

    p = *s->wblk + s->wpos;
    s->wpos += s->elem_len;
    s->wptr += s->elem_len;

    if (s->wptr > s->datalen)
        s->datalen = s->wptr;

    return p;
}

void saa_wbytes(struct SAA *s, const void *data, size_t len)
{
    const char *d = data;

    while (len) {
        size_t l = s->blk_len - s->wpos;
        if (l > len)
            l = len;
        if (l) {
            if (d) {
                memcpy(*s->wblk + s->wpos, d, l);
                d += l;
            } else
                memset(*s->wblk + s->wpos, 0, l);
            s->wpos += l;
            s->wptr += l;
            len -= l;

            if (s->datalen < s->wptr)
                s->datalen = s->wptr;
        }
        if (len) {
            if (s->wptr >= s->length)
                saa_extend(s);
            s->wblk++;
            s->wpos = 0;
        }
    }
}

/*
 * Writes a string, *including* the final null, to the specified SAA,
 * and return the number of bytes written.
 */
size_t saa_wcstring(struct SAA *s, const char *str)
{
    size_t bytes = strlen(str) + 1;

    saa_wbytes(s, str, bytes);

    return bytes;
}

void saa_rewind(struct SAA *s)
{
    s->rblk = s->blk_ptrs;
    s->rpos = s->rptr = 0;
}

void *saa_rstruct(struct SAA *s)
{
    void *p;

    if (s->rptr + s->elem_len > s->datalen)
        return NULL;

    nasm_assert((s->rpos % s->elem_len) == 0);

    if (s->rpos + s->elem_len > s->blk_len) {
        s->rblk++;
        s->rpos = 0;
    }

    p = *s->rblk + s->rpos;
    s->rpos += s->elem_len;
    s->rptr += s->elem_len;

    return p;
}

const void *saa_rbytes(struct SAA *s, size_t * lenp)
{
    const void *p;
    size_t len;

    if (s->rptr >= s->datalen) {
        *lenp = 0;
        return NULL;
    }

    if (s->rpos >= s->blk_len) {
        s->rblk++;
        s->rpos = 0;
    }

    len = *lenp;
    if (len > s->datalen - s->rptr)
        len = s->datalen - s->rptr;
    if (len > s->blk_len - s->rpos)
        len = s->blk_len - s->rpos;

    *lenp = len;
    p = *s->rblk + s->rpos;

    s->rpos += len;
    s->rptr += len;

    return p;
}

void saa_rnbytes(struct SAA *s, void *data, size_t len)
{
    char *d = data;

    nasm_assert(s->rptr + len <= s->datalen);

    while (len) {
        size_t l;
        const void *p;

        l = len;
        p = saa_rbytes(s, &l);

        memcpy(d, p, l);
        d += l;
        len -= l;
    }
}

/* Same as saa_rnbytes, except position the counter first */
void saa_fread(struct SAA *s, size_t posn, void *data, size_t len)
{
    size_t ix;

    nasm_assert(posn + len <= s->datalen);

    if (likely(s->blk_len == SAA_BLKLEN)) {
        ix = posn >> SAA_BLKSHIFT;
        s->rpos = posn & (SAA_BLKLEN - 1);
    } else {
        ix = posn / s->blk_len;
        s->rpos = posn % s->blk_len;
    }
    s->rptr = posn;
    s->rblk = &s->blk_ptrs[ix];

    saa_rnbytes(s, data, len);
}

/* Same as saa_wbytes, except position the counter first */
void saa_fwrite(struct SAA *s, size_t posn, const void *data, size_t len)
{
    size_t ix;

    /* Seek beyond the end of the existing array not supported */
    nasm_assert(posn <= s->datalen);

    if (likely(s->blk_len == SAA_BLKLEN)) {
        ix = posn >> SAA_BLKSHIFT;
        s->wpos = posn & (SAA_BLKLEN - 1);
    } else {
        ix = posn / s->blk_len;
        s->wpos = posn % s->blk_len;
    }
    s->wptr = posn;
    s->wblk = &s->blk_ptrs[ix];

    if (!s->wpos) {
        s->wpos = s->blk_len;
        s->wblk--;
    }

    saa_wbytes(s, data, len);
}

void saa_fpwrite(struct SAA *s, FILE * fp)
{
    const char *data;
    size_t len;

    saa_rewind(s);
    while (len = s->datalen, (data = saa_rbytes(s, &len)) != NULL)
        nasm_write(data, len, fp);
}

void saa_write8(struct SAA *s, uint8_t v)
{
    saa_wbytes(s, &v, 1);
}

void saa_write16(struct SAA *s, uint16_t v)
{
    v = cpu_to_le16(v);
    saa_wbytes(s, &v, 2);
}

void saa_write32(struct SAA *s, uint32_t v)
{
    v = cpu_to_le32(v);
    saa_wbytes(s, &v, 4);
}

void saa_write64(struct SAA *s, uint64_t v)
{
    v = cpu_to_le64(v);
    saa_wbytes(s, &v, 8);
}

void saa_writeaddr(struct SAA *s, uint64_t v, size_t len)
{
    v = cpu_to_le64(v);
    saa_wbytes(s, &v, len);
}

/* write unsigned LEB128 value to SAA */
void saa_wleb128u(struct SAA *psaa, int value)
{
    char temp[64], *ptemp;
    uint8_t byte;
    int len;

    ptemp = temp;
    len = 0;
    do {
        byte = value & 127;
        value >>= 7;
        if (value != 0)         /* more bytes to come */
            byte |= 0x80;
        *ptemp = byte;
        ptemp++;
        len++;
    } while (value != 0);
    saa_wbytes(psaa, temp, len);
}

/* write signed LEB128 value to SAA */
void saa_wleb128s(struct SAA *psaa, int value)
{
    char temp[64], *ptemp;
    uint8_t byte;
    bool more, negative;
    int size, len;

    ptemp = temp;
    more = 1;
    negative = (value < 0);
    size = sizeof(int) * 8;
    len = 0;
    while (more) {
        byte = value & 0x7f;
        value >>= 7;
        if (negative)
            /* sign extend */
            value |= -(1 << (size - 7));
        /* sign bit of byte is second high order bit (0x40) */
        if ((value == 0 && !(byte & 0x40)) ||
            ((value == -1) && (byte & 0x40)))
            more = 0;
        else
            byte |= 0x80;
        *ptemp = byte;
        ptemp++;
        len++;
    }
    saa_wbytes(psaa, temp, len);
}
