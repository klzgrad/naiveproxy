/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2014 The NASM Authors - All Rights Reserved
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
 * rdoff.c	library of routines for manipulating rdoff files
 */

/* TODO:	The functions in this module assume they are running
 *		on a little-endian machine. This should be fixed to
 *		make it portable.
 */

#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "rdfutils.h"

/*
 * Comment this out to allow the module to read & write header record types
 * that it isn't aware of. With this defined, unrecognised header records
 * will generate error number 8, reported as 'unknown extended header record'.
 */

#define STRICT_ERRORS

/* ========================================================================
 * Code for memory buffers (for delayed writing of header until we know
 * how int32_t it is).
 * ======================================================================== */

static memorybuffer *newmembuf(void)
{
    memorybuffer *t;

    t = nasm_malloc(sizeof(memorybuffer));
    if (!t)
        return NULL;

    t->length = 0;
    t->next = NULL;
    return t;
}

static void membufwrite(memorybuffer * const b, void *data, int bytes)
{
    uint16_t w;
    int32_t l;
    char *c;

    if (b->next) {              /* memory buffer full - use next buffer */
        membufwrite(b->next, data, bytes);
        return;
    }

    if ((bytes < 0 && b->length - bytes > BUF_BLOCK_LEN)
        || (bytes > 0 && b->length + bytes > BUF_BLOCK_LEN)) {

        /* buffer full and no next allocated... allocate and initialise next
         * buffer */
        b->next = newmembuf();
        membufwrite(b->next, data, bytes);
        return;
    }

    switch (bytes) {
    case -4:                   /* convert to little-endian */
        l = *(int32_t *)data;
        b->buffer[b->length++] = l & 0xFF;
        l >>= 8;
        b->buffer[b->length++] = l & 0xFF;
        l >>= 8;
        b->buffer[b->length++] = l & 0xFF;
        l >>= 8;
        b->buffer[b->length++] = l & 0xFF;
        break;

    case -2:
        w = *(uint16_t *) data;
        b->buffer[b->length++] = w & 0xFF;
        w >>= 8;
        b->buffer[b->length++] = w & 0xFF;
        break;

    default:
        c = data;
        while (bytes--)
            b->buffer[b->length++] = *c++;
        break;
    }
}

static void membufdump(memorybuffer * b, FILE * fp)
{
    if (!b)
        return;

    nasm_write(b->buffer, b->length, fp);
    membufdump(b->next, fp);
}

static int membuflength(memorybuffer * b)
{
    if (!b)
        return 0;
    return b->length + membuflength(b->next);
}

static void freemembuf(memorybuffer * b)
{
    if (!b)
        return;
    freemembuf(b->next);
    nasm_free(b);
}

/* =========================================================================
   General purpose routines and variables used by the library functions
   ========================================================================= */

/*
 * translateint32_t() and translateint16_t()
 *
 * translate from little endian to local representation
 */
int32_t translateint32_t(int32_t in)
{
    int32_t r;
    uint8_t *i;

    i = (uint8_t *)&in;
    r = i[3];
    r = (r << 8) + i[2];
    r = (r << 8) + i[1];
    r = (r << 8) + *i;

    return r;
}

uint16_t translateint16_t(uint16_t in)
{
    uint16_t r;
    uint8_t *i;

    i = (uint8_t *)&in;
    r = (i[1] << 8) + i[0];

    return r;
}

/* Segment types */
static char *knownsegtypes[8] = {
    "NULL", "text", "data", "object comment",
    "linked comment", "loader comment",
    "symbolic debug", "line number debug"
};

/* Get a textual string describing the segment type */
char *translatesegmenttype(uint16_t type)
{
    if (type < 8)
        return knownsegtypes[type];
    if (type < 0x0020)
        return "reserved";
    if (type < 0x1000)
        return "reserved - Moscow";
    if (type < 0x8000)
        return "reserved - system dependant";
    if (type < 0xFFFF)
        return "reserved - other";
    if (type == 0xFFFF)
        return "invalid type code";
    return "type code out of range";
}

/* This signature is written to the start of RDOFF files */
const char *RDOFFId = RDOFF2_SIGNATURE;

/* Error messages. Must correspond to the codes defined in rdoff.h */
const char *rdf_errors[11] = {
    /* 0 */ "no error occurred",
    /* 1 */ "could not open file",
    /* 2 */ "invalid file format",
    /* 3 */ "error reading file",
    /* 4 */ "unknown error",
    /* 5 */ "header not read",
    /* 6 */ "out of memory",
    /* 7 */ "RDOFF v1 not supported",
    /* 8 */ "unknown extended header record",
    /* 9 */ "header record of known type but unknown length",
    /* 10 */ "no such segment"
};

int rdf_errno = 0;

/* ========================================================================
 * Hook for nasm_error() to work
 * ======================================================================== */
static void rdoff_verror(int severity, const char *fmt, va_list val)
{
    vfprintf(stderr, fmt, val);

    if ((severity & ERR_MASK) >= ERR_FATAL)
        exit(1);
}

void rdoff_init(void)
{
    nasm_set_verror(rdoff_verror);
}

/* ========================================================================
   The library functions
   ======================================================================== */

int rdfopen(rdffile * f, const char *name)
{
    FILE *fp;

    fp = fopen(name, "rb");
    if (!fp)
        return rdf_errno = RDF_ERR_OPEN;

    return rdfopenhere(f, fp, NULL, name);
}

int rdfopenhere(rdffile * f, FILE * fp, int *refcount, const char *name)
{
    char buf[8];
    int32_t initpos;
    int32_t l;
    uint16_t s;

    if (translateint32_t(0x01020304) != 0x01020304) {
        /* fix this to be portable! */
        fputs("*** this program requires a little endian machine\n",
              stderr);
        fprintf(stderr, "01020304h = %08"PRIx32"h\n", translateint32_t(0x01020304));
        exit(3);
    }

    f->fp = fp;
    initpos = ftell(fp);

    /* read header */
    if (fread(buf, 1, 6, f->fp) != 6) {
        fclose(f->fp);
        return rdf_errno = RDF_ERR_READ;
    }
    buf[6] = 0;

    if (strcmp(buf, RDOFFId)) {
        fclose(f->fp);
        if (!strcmp(buf, "RDOFF1"))
            return rdf_errno = RDF_ERR_VER;
        return rdf_errno = RDF_ERR_FORMAT;
    }

    if (fread(&l, 1, 4, f->fp) != 4
        || fread(&f->header_len, 1, 4, f->fp) != 4) {
        fclose(f->fp);
        return rdf_errno = RDF_ERR_READ;
    }

    f->header_ofs = ftell(f->fp);
    f->eof_offset = f->header_ofs + translateint32_t(l) - 4;

    if (fseek(f->fp, f->header_len, SEEK_CUR)) {
        fclose(f->fp);
        return rdf_errno = RDF_ERR_FORMAT;      /* seek past end of file...? */
    }

    if (fread(&s, 1, 2, f->fp) != 2) {
        fclose(f->fp);
        return rdf_errno = RDF_ERR_READ;
    }

    f->nsegs = 0;

    while (s != 0) {
        f->seg[f->nsegs].type = s;
        if (fread(&f->seg[f->nsegs].number, 1, 2, f->fp) != 2 ||
            fread(&f->seg[f->nsegs].reserved, 1, 2, f->fp) != 2 ||
            fread(&f->seg[f->nsegs].length, 1, 4, f->fp) != 4) {
            fclose(f->fp);
            return rdf_errno = RDF_ERR_READ;
        }

        f->seg[f->nsegs].offset = ftell(f->fp);
        if (fseek(f->fp, f->seg[f->nsegs].length, SEEK_CUR)) {
            fclose(f->fp);
            return rdf_errno = RDF_ERR_FORMAT;
        }
        f->nsegs++;

        if (fread(&s, 1, 2, f->fp) != 2) {
            fclose(f->fp);
            return rdf_errno = RDF_ERR_READ;
        }
    }

    if (f->eof_offset != ftell(f->fp) + 8) {    /* +8 = skip null segment header */
        fprintf(stderr, "warning: eof_offset [%"PRId32"] and actual eof offset "
                "[%ld] don't match\n", f->eof_offset, ftell(f->fp) + 8);
    }
    fseek(f->fp, initpos, SEEK_SET);
    f->header_loc = NULL;

    f->name = nasm_strdup(name);
    f->refcount = refcount;
    if (refcount)
        (*refcount)++;
    return RDF_OK;
}

int rdfclose(rdffile * f)
{
    if (!f->refcount || !--(*f->refcount)) {
        fclose(f->fp);
        f->fp = NULL;
    }
    nasm_free(f->name);

    return 0;
}

/*
 * Print the message for last error (from rdf_errno)
 */
void rdfperror(const char *app, const char *name)
{
    fprintf(stderr, "%s:%s: %s\n", app, name, rdf_errors[rdf_errno]);
    if (rdf_errno == RDF_ERR_OPEN || rdf_errno == RDF_ERR_READ) {
        perror(app);
    }
}

/*
 * Find the segment by its number.
 * Returns segment array index, or -1 if segment with such number was not found.
 */
int rdffindsegment(rdffile * f, int segno)
{
    int i;
    for (i = 0; i < f->nsegs; i++)
        if (f->seg[i].number == segno)
            return i;
    return -1;
}

/*
 * Load the segment. Returns status.
 */
int rdfloadseg(rdffile * f, int segment, void *buffer)
{
    int32_t fpos;
    size_t slen;

    switch (segment) {
    case RDOFF_HEADER:
        fpos = f->header_ofs;
        slen = f->header_len;
        f->header_loc = (uint8_t *) buffer;
        f->header_fp = 0;
        break;
    default:
        if (segment < f->nsegs) {
            fpos = f->seg[segment].offset;
            slen = f->seg[segment].length;
            f->seg[segment].data = (uint8_t *) buffer;
        } else {
            return rdf_errno = RDF_ERR_SEGMENT;
        }
    }

    if (fseek(f->fp, fpos, SEEK_SET))
        return rdf_errno = RDF_ERR_UNKNOWN;

    if (fread(buffer, 1, slen, f->fp) != slen)
        return rdf_errno = RDF_ERR_READ;

    return RDF_OK;
}

/* Macros for reading integers from header in memory */

#define RI8(v) v = f->header_loc[f->header_fp++]
#define RI16(v) { v = (f->header_loc[f->header_fp] + \
		       (f->header_loc[f->header_fp+1] << 8)); \
		  f->header_fp += 2; }

#define RI32(v) { v = (f->header_loc[f->header_fp] + \
		       (f->header_loc[f->header_fp+1] << 8) + \
		       (f->header_loc[f->header_fp+2] << 16) + \
		       (f->header_loc[f->header_fp+3] << 24)); \
		  f->header_fp += 4; }

#define RS(str,max) { for(i=0;i<max;i++){\
  RI8(str[i]); if (!str[i]) break;} str[i]=0; }

/*
 * Read a header record.
 * Returns the address of record, or NULL in case of error.
 */
rdfheaderrec *rdfgetheaderrec(rdffile * f)
{
    static rdfheaderrec r;
    int i;

    if (!f->header_loc) {
        rdf_errno = RDF_ERR_HEADER;
        return NULL;
    }

    if (f->header_fp >= f->header_len)
        return 0;

    RI8(r.type);
    RI8(r.g.reclen);

    switch (r.type) {
    case RDFREC_RELOC:         /* Relocation record */
    case RDFREC_SEGRELOC:
        if (r.r.reclen != 8) {
            rdf_errno = RDF_ERR_RECLEN;
            return NULL;
        }
        RI8(r.r.segment);
        RI32(r.r.offset);
        RI8(r.r.length);
        RI16(r.r.refseg);
        break;

    case RDFREC_IMPORT:        /* Imported symbol record */
    case RDFREC_FARIMPORT:
        RI8(r.i.flags);
        RI16(r.i.segment);
        RS(r.i.label, EXIM_LABEL_MAX);
        break;

    case RDFREC_GLOBAL:        /* Exported symbol record */
        RI8(r.e.flags);
        RI8(r.e.segment);
        RI32(r.e.offset);
        RS(r.e.label, EXIM_LABEL_MAX);
        break;

    case RDFREC_DLL:           /* DLL record */
        RS(r.d.libname, MODLIB_NAME_MAX);
        break;

    case RDFREC_BSS:           /* BSS reservation record */
        if (r.r.reclen != 4) {
            rdf_errno = RDF_ERR_RECLEN;
            return NULL;
        }
        RI32(r.b.amount);
        break;

    case RDFREC_MODNAME:       /* Module name record */
        RS(r.m.modname, MODLIB_NAME_MAX);
        break;

    case RDFREC_COMMON:        /* Common variable */
        RI16(r.c.segment);
        RI32(r.c.size);
        RI16(r.c.align);
        RS(r.c.label, EXIM_LABEL_MAX);
        break;

    default:
#ifdef STRICT_ERRORS
        rdf_errno = RDF_ERR_RECTYPE;    /* unknown header record */
        return NULL;
#else
        for (i = 0; i < r.g.reclen; i++)
            RI8(r.g.data[i]);
#endif
    }
    return &r;
}

/*
 * Rewind to the beginning of the file
 */
void rdfheaderrewind(rdffile * f)
{
    f->header_fp = 0;
}

rdf_headerbuf *rdfnewheader(void)
{
    rdf_headerbuf *hb = nasm_malloc(sizeof(rdf_headerbuf));
    if (hb == NULL)
        return NULL;

    hb->buf = newmembuf();
    hb->nsegments = 0;
    hb->seglength = 0;

    return hb;
}

int rdfaddheader(rdf_headerbuf * h, rdfheaderrec * r)
{
#ifndef STRICT_ERRORS
    int i;
#endif
    membufwrite(h->buf, &r->type, 1);
    membufwrite(h->buf, &r->g.reclen, 1);

    switch (r->type) {
    case RDFREC_GENERIC:       /* generic */
        membufwrite(h->buf, &r->g.data, r->g.reclen);
        break;
    case RDFREC_RELOC:
    case RDFREC_SEGRELOC:
        membufwrite(h->buf, &r->r.segment, 1);
        membufwrite(h->buf, &r->r.offset, -4);
        membufwrite(h->buf, &r->r.length, 1);
        membufwrite(h->buf, &r->r.refseg, -2);  /* 9 bytes written */
        break;

    case RDFREC_IMPORT:        /* import */
    case RDFREC_FARIMPORT:
        membufwrite(h->buf, &r->i.flags, 1);
        membufwrite(h->buf, &r->i.segment, -2);
        membufwrite(h->buf, &r->i.label, strlen(r->i.label) + 1);
        break;

    case RDFREC_GLOBAL:        /* export */
        membufwrite(h->buf, &r->e.flags, 1);
        membufwrite(h->buf, &r->e.segment, 1);
        membufwrite(h->buf, &r->e.offset, -4);
        membufwrite(h->buf, &r->e.label, strlen(r->e.label) + 1);
        break;

    case RDFREC_DLL:           /* DLL */
        membufwrite(h->buf, &r->d.libname, strlen(r->d.libname) + 1);
        break;

    case RDFREC_BSS:           /* BSS */
        membufwrite(h->buf, &r->b.amount, -4);
        break;

    case RDFREC_MODNAME:       /* Module name */
        membufwrite(h->buf, &r->m.modname, strlen(r->m.modname) + 1);
        break;

    default:
#ifdef STRICT_ERRORS
        return rdf_errno = RDF_ERR_RECTYPE;
#else
        for (i = 0; i < r->g.reclen; i++)
            membufwrite(h->buf, r->g.data[i], 1);
#endif
    }
    return 0;
}

int rdfaddsegment(rdf_headerbuf * h, int32_t seglength)
{
    h->nsegments++;
    h->seglength += seglength;
    return 0;
}

int rdfwriteheader(FILE * fp, rdf_headerbuf * h)
{
    int32_t l, l2;

    nasm_write(RDOFFId, strlen(RDOFFId), fp);

    l = membuflength(h->buf);
    l2 = l + 14 + 10 * h->nsegments + h->seglength;
    fwriteint32_t(l, fp);
    fwriteint32_t(l2, fp);

    membufdump(h->buf, fp);

    return 0;                   /* no error handling in here... CHANGE THIS! */
}

void rdfdoneheader(rdf_headerbuf * h)
{
    freemembuf(h->buf);
    nasm_free(h);
}
