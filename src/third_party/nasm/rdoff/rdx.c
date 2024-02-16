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
 * rdx.c	RDOFF Object File loader program
 */

/* note: most of the actual work of this program is done by the modules
   "rdfload.c", which loads and relocates the object file, and by "rdoff.c",
   which contains general purpose routines to manipulate RDOFF object
   files. You can use these files in your own program to load RDOFF objects
   and execute the code in them in a similar way to what is shown here. */

#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>

#include "rdfload.h"
#include "symtab.h"

typedef int (*main_fn) (int, char **);  /* Main function prototype */

int main(int argc, char **argv)
{
    rdfmodule *m;
    main_fn code;
    symtabEnt *s;

    if (argc < 2) {
        puts("usage: rdx <rdoff-executable> [params]\n");
        exit(255);
    }

    rdoff_init();

    m = rdfload(argv[1]);

    if (!m) {
        rdfperror("rdx", argv[1]);
        exit(255);
    }

    rdf_relocate(m);            /* in this instance, the default relocation
                                   values will work fine, but they may need changing
                                   in other cases... */

    s = symtabFind(m->symtab, "_main");
    if (!s) {
        fprintf(stderr, "rdx: could not find symbol '_main' in '%s'\n",
                argv[1]);
        exit(255);
    }

    code = (main_fn)(size_t) s->offset;

    argv++, argc--;             /* remove 'rdx' from command line */

    return code(argc, argv);    /* execute */
}
