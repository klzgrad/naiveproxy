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
 * rdlib.h	Functions for manipulating libraries of RDOFF object files.
 */

#ifndef RDOFF_RDLIB_H
#define RDOFF_RDLIB_H 1

struct librarynode {
    char *name;
    FILE *fp;                   /* initialised to NULL - always check */
    int referenced;             /* & open if required. Close afterwards */
    struct librarynode *next;   /* if ! referenced. */
};

extern int rdl_error;

#define RDL_EOPEN     1
#define RDL_EINVALID  2
#define RDL_EVERSION  3
#define RDL_ENOTFOUND 4

int rdl_verify(const char *filename);
int rdl_open(struct librarynode *lib, const char *filename);
int rdl_searchlib(struct librarynode *lib, const char *label, rdffile * f);
int rdl_openmodule(struct librarynode *lib, int module, rdffile * f);

void rdl_perror(const char *apname, const char *filename);

#endif
