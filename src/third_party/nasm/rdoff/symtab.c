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
 * symtab.c     Routines to maintain and manipulate a symbol table
 *
 *   These routines donated to the NASM effort by Graeme Defty.
 */

#include "rdfutils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "symtab.h"
#include "hash.h"

#define SYMTABSIZE 64
#define slotnum(x) (hash((x)) % SYMTABSIZE)

/* ------------------------------------- */
/* Private data types */

typedef struct tagSymtabNode {
    struct tagSymtabNode *next;
    symtabEnt ent;
} symtabNode;

typedef symtabNode *(symtabTab[SYMTABSIZE]);

typedef symtabTab *symtab;

/* ------------------------------------- */
void *symtabNew(void)
{
    symtab mytab;

    mytab = (symtabTab *) nasm_calloc(SYMTABSIZE, sizeof(symtabNode *));
    if (mytab == NULL) {
        fprintf(stderr, "symtab: out of memory\n");
        exit(3);
    }

    return mytab;
}

/* ------------------------------------- */
void symtabDone(void *stab)
{
    symtab mytab = (symtab) stab;
    int i;
    symtabNode *this, *next;

    for (i = 0; i < SYMTABSIZE; ++i) {

        for (this = (*mytab)[i]; this; this = next) {
            next = this->next;
            nasm_free(this);
        }

    }
    nasm_free(*mytab);
}

/* ------------------------------------- */
void symtabInsert(void *stab, symtabEnt * ent)
{
    symtab mytab = (symtab) stab;
    symtabNode *node;
    int slot;

    node = nasm_malloc(sizeof(symtabNode));
    if (node == NULL) {
        fprintf(stderr, "symtab: out of memory\n");
        exit(3);
    }

    slot = slotnum(ent->name);

    node->ent = *ent;
    node->next = (*mytab)[slot];
    (*mytab)[slot] = node;
}

/* ------------------------------------- */
symtabEnt *symtabFind(void *stab, const char *name)
{
    symtab mytab = (symtab) stab;
    int slot = slotnum(name);
    symtabNode *node = (*mytab)[slot];

    while (node) {
        if (!strcmp(node->ent.name, name)) {
            return &(node->ent);
        }
        node = node->next;
    }

    return NULL;
}

/* ------------------------------------- */
void symtabDump(void *stab, FILE * of)
{
    symtab mytab = (symtab) stab;
    int i;
    char *SegNames[3] = { "code", "data", "bss" };

    fprintf(of, "Symbol table is ...\n");
    for (i = 0; i < SYMTABSIZE; ++i) {
        symtabNode *l = (symtabNode *) (*mytab)[i];

        if (l) {
            fprintf(of, " ... slot %d ...\n", i);
        }
        while (l) {
            if ((l->ent.segment) == -1) {
                fprintf(of, "%-32s Unresolved reference\n", l->ent.name);
            } else {
                fprintf(of, "%-32s %s:%08"PRIx32" (%"PRId32")\n", l->ent.name,
                        SegNames[l->ent.segment],
                        l->ent.offset, l->ent.flags);
            }
            l = l->next;
        }
    }
    fprintf(of, "........... end of Symbol table.\n");
}
