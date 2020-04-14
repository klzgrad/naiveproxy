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
 * rdfutils.h
 *
 * Internal header file for RDOFF utilities
 */

#ifndef RDOFF_RDFUTILS_H
#define RDOFF_RDFUTILS_H 1

#include "compiler.h"
#include "nasmlib.h"
#include "error.h"
#include "rdoff.h"

#include <stdlib.h>
#include <stdio.h>

typedef union RDFHeaderRec {
    char type;                  /* invariant throughout all below */
    struct GenericRec g;        /* type 0 */
    struct RelocRec r;          /* type == 1 / 6 */
    struct ImportRec i;         /* type == 2 / 7 */
    struct ExportRec e;         /* type == 3 */
    struct DLLRec d;            /* type == 4 */
    struct BSSRec b;            /* type == 5 */
    struct ModRec m;            /* type == 8 */
    struct CommonRec c;         /* type == 10 */
} rdfheaderrec;

struct SegmentHeaderRec {
    /* information from file */
    uint16_t type;
    uint16_t number;
    uint16_t reserved;
    int32_t length;

    /* information built up here */
    int32_t offset;
    uint8_t *data;                 /* pointer to segment data if it exists in memory */
};

typedef struct RDFFileInfo {
    FILE *fp;                   /* file descriptor; must be open to use this struct */
    int rdoff_ver;              /* should be 1; any higher => not guaranteed to work */
    int32_t header_len;
    int32_t header_ofs;

    uint8_t *header_loc;           /* keep location of header */
    int32_t header_fp;             /* current location within header for reading */

    struct SegmentHeaderRec seg[RDF_MAXSEGS];
    int nsegs;

    int32_t eof_offset;            /* offset of the first uint8_t beyond the end of this
                                   module */

    char *name;                 /* name of module in libraries */
    int *refcount;              /* pointer to reference count on file, or NULL */
} rdffile;

#define BUF_BLOCK_LEN 4088      /* selected to match page size (4096)
                                 * on 80x86 machines for efficiency */
typedef struct memorybuffer {
    int length;
    uint8_t buffer[BUF_BLOCK_LEN];
    struct memorybuffer *next;
} memorybuffer;

typedef struct {
    memorybuffer *buf;          /* buffer containing header records */
    int nsegments;              /* number of segments to be written */
    int32_t seglength;             /* total length of all the segments */
} rdf_headerbuf;

/* segments used by RDOFF, understood by rdoffloadseg */
#define RDOFF_CODE	0
#define RDOFF_DATA	1
#define RDOFF_HEADER	-1
/* mask for 'segment' in relocation records to find if relative relocation */
#define RDOFF_RELATIVEMASK 64
/* mask to find actual segment value in relocation records */
#define RDOFF_SEGMENTMASK 63

extern int rdf_errno;

/* rdf_errno can hold these error codes */
enum {
    /* 0 */ RDF_OK,
    /* 1 */ RDF_ERR_OPEN,
    /* 2 */ RDF_ERR_FORMAT,
    /* 3 */ RDF_ERR_READ,
    /* 4 */ RDF_ERR_UNKNOWN,
    /* 5 */ RDF_ERR_HEADER,
    /* 6 */ RDF_ERR_NOMEM,
    /* 7 */ RDF_ERR_VER,
    /* 8 */ RDF_ERR_RECTYPE,
    /* 9 */ RDF_ERR_RECLEN,
    /* 10 */ RDF_ERR_SEGMENT
};

/* library init */
void rdoff_init(void);

/* utility functions */
int32_t translateint32_t(int32_t in);
uint16_t translateint16_t(uint16_t in);
char *translatesegmenttype(uint16_t type);

/* RDOFF file manipulation functions */
int rdfopen(rdffile * f, const char *name);
int rdfopenhere(rdffile * f, FILE * fp, int *refcount, const char *name);
int rdfclose(rdffile * f);
int rdffindsegment(rdffile * f, int segno);
int rdfloadseg(rdffile * f, int segment, void *buffer);
rdfheaderrec *rdfgetheaderrec(rdffile * f);     /* returns static storage */
void rdfheaderrewind(rdffile * f);      /* back to start of header */
void rdfperror(const char *app, const char *name);

/* functions to write a new RDOFF header to a file -
   use rdfnewheader to allocate a header, rdfaddheader to add records to it,
   rdfaddsegment to notify the header routines that a segment exists, and
   to tell it how int32_t the segment will be.
   rdfwriteheader to write the file id, object length, and header
   to a file, and then rdfdoneheader to dispose of the header */

rdf_headerbuf *rdfnewheader(void);
int rdfaddheader(rdf_headerbuf * h, rdfheaderrec * r);
int rdfaddsegment(rdf_headerbuf * h, int32_t seglength);
int rdfwriteheader(FILE * fp, rdf_headerbuf * h);
void rdfdoneheader(rdf_headerbuf * h);

#endif                          /* RDOFF_RDFUTILS_H */
