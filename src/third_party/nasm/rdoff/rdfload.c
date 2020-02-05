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
 * rdfload.c	RDOFF Object File loader library
 */

/*
 * TODO: this has been modified from previous version only in very
 * simplistic ways. Needs to be improved drastically, especially:
 *   - support for more than the 2 standard segments
 *   - support for segment relocations (hard to do in ANSI C)
 */

#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rdfload.h"
#include "symtab.h"
#include "collectn.h"

extern int rdf_errno;

rdfmodule *rdfload(const char *filename)
{
    rdfmodule *f;
    int32_t bsslength = 0;
    char *hdr;
    rdfheaderrec *r;

    f = nasm_malloc(sizeof(rdfmodule));
    if (f == NULL) {
        rdf_errno = RDF_ERR_NOMEM;
        return NULL;
    }

    f->symtab = symtabNew();
    if (!f->symtab) {
        nasm_free(f);
        rdf_errno = RDF_ERR_NOMEM;
        return NULL;
    }

    /* open the file */
    if (rdfopen(&(f->f), filename)) {
        nasm_free(f);
        return NULL;
    }

    /* read in text and data segments, and header */

    f->t = nasm_malloc(f->f.seg[0].length);
    f->d = nasm_malloc(f->f.seg[1].length);  /* BSS seg allocated later */
    hdr = nasm_malloc(f->f.header_len);

    if (!f->t || !f->d || !hdr) {
        rdf_errno = RDF_ERR_NOMEM;
        rdfclose(&f->f);
        if (f->t)
            nasm_free(f->t);
        if (f->d)
            nasm_free(f->d);
        nasm_free(f);
        nasm_free(hdr);
        return NULL;
    }

    if (rdfloadseg(&f->f, RDOFF_HEADER, hdr) ||
        rdfloadseg(&f->f, RDOFF_CODE, f->t) ||
        rdfloadseg(&f->f, RDOFF_DATA, f->d)) {
        rdfclose(&f->f);
        nasm_free(f->t);
        nasm_free(f->d);
        nasm_free(f);
        nasm_free(hdr);
        return NULL;
    }

    rdfclose(&f->f);

    /* Allocate BSS segment; step through header and count BSS records */

    while ((r = rdfgetheaderrec(&f->f))) {
        if (r->type == 5)
            bsslength += r->b.amount;
    }

    f->b = nasm_malloc(bsslength);
    if (bsslength && (!f->b)) {
        nasm_free(f->t);
        nasm_free(f->d);
        nasm_free(f);
        nasm_free(hdr);
        rdf_errno = RDF_ERR_NOMEM;
        return NULL;
    }

    rdfheaderrewind(&f->f);

    f->textrel = (int32_t)(size_t)f->t;
    f->datarel = (int32_t)(size_t)f->d;
    f->bssrel  = (int32_t)(size_t)f->b;

    return f;
}

int rdf_relocate(rdfmodule * m)
{
    rdfheaderrec *r;
    Collection imports;
    symtabEnt e;
    int32_t rel;
    uint8_t *seg;

    rdfheaderrewind(&m->f);
    collection_init(&imports);

    while ((r = rdfgetheaderrec(&m->f))) {
        switch (r->type) {
        case 1:                /* Relocation record */

            /* calculate relocation factor */

            if (r->r.refseg == 0)
                rel = m->textrel;
            else if (r->r.refseg == 1)
                rel = m->datarel;
            else if (r->r.refseg == 2)
                rel = m->bssrel;
            else
                /* We currently do not support load-time linkage.
                   This should be added some time soon... */

                return 1;       /* return error code */

            if ((r->r.segment & 63) == 0)
                seg = m->t;
            else if ((r->r.segment & 63) == 1)
                seg = m->d;
            else
                continue;       /* relocation not in a loaded segment */

            /* it doesn't matter in this case that the code is non-portable,
               as the entire concept of executing a module like this is
               non-portable */
            switch (r->r.length) {
            case 1:
                seg[r->r.offset] += (char)rel;
                break;
            case 2:
                *(uint16_t *) (seg + r->r.offset) += (uint16_t) rel;
                break;
            case 4:
                *(int32_t *)(seg + r->r.offset) += rel;
                break;
            }
            break;

        case 3:                /* export record - add to symtab */
            e.segment = r->e.segment;
            e.offset = r->e.offset + (e.segment == 0 ? m->textrel :     /* 0 -> code */
                                      e.segment == 1 ? m->datarel :     /* 1 -> data */
                                      m->bssrel);       /* 2 -> bss  */
            e.flags = 0;
            e.name = nasm_malloc(strlen(r->e.label) + 1);
            if (!e.name)
                return 1;

            strcpy(e.name, r->e.label);
            symtabInsert(m->symtab, &e);
            break;

        case 6:                /* segment relocation */
            fprintf(stderr, "%s: segment relocation not supported by this "
                    "loader\n", m->f.name);
            return 1;
        }
    }
    return 0;
}
