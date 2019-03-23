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
 * ldrdf.c - RDOFF Object File linker/loader main program.
 */

/*
 * TODO:
 * - enhance search of required export symbols in libraries (now depends
 *   on modules order in library)
 * - keep a cache of symbol names in each library module so
 *   we don't have to constantly recheck the file
 * - general performance improvements
 *
 * BUGS & LIMITATIONS: this program doesn't support multiple code, data
 * or bss segments, therefore for 16 bit programs whose code, data or BSS
 * segment exceeds 64K in size, it will not work. This program probably
 * won't work if compiled by a 16 bit compiler. Try DJGPP if you're running
 * under DOS. '#define STINGY_MEMORY' may help a little.
 */

#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rdfutils.h"
#include "symtab.h"
#include "collectn.h"
#include "rdlib.h"
#include "segtab.h"
#include "nasmlib.h"

#define LDRDF_VERSION "1.08"

/* #define STINGY_MEMORY */

/* =======================================================================
 * Types & macros that are private to this program
 */

struct segment_infonode {
    int dest_seg;               /* output segment to be placed into, -1 to
                                   skip linking this segment */
    int32_t reloc;                 /* segment's relocation factor */
};

struct modulenode {
    rdffile f;                  /* the RDOFF file structure */
    struct segment_infonode seginfo[RDF_MAXSEGS];       /* what are we doing
                                                           with each segment? */
    void *header;
    char *name;
    struct modulenode *next;
    int32_t bss_reloc;
};

#include "ldsegs.h"

/* ==========================================================================
 * Function prototypes of private utility functions
 */

void processmodule(const char *filename, struct modulenode *mod);
int allocnewseg(uint16_t type, uint16_t reserved);
int findsegment(uint16_t type, uint16_t reserved);
void symtab_add(const char *symbol, int segment, int32_t offset);
int symtab_get(const char *symbol, int *segment, int32_t *offset);

/* =========================================================================
 * Global data structures.
 */

/* a linked list of modules that will be included in the output */
struct modulenode *modules = NULL;
struct modulenode *lastmodule = NULL;

/* a linked list of libraries to be searched for unresolved imported symbols */
struct librarynode *libraries = NULL;
struct librarynode *lastlib = NULL;

/* the symbol table */
void *symtab = NULL;

/* objects search path */
char *objpath = NULL;

/* libraries search path */
char *libpath = NULL;

/* file to embed as a generic record */
char *generic_rec_file = NULL;

/* module name to be added at the beginning of output file */
char *modname_specified = NULL;

/* error file */
static FILE *error_file;

/* the header of the output file, built up stage by stage */
rdf_headerbuf *newheader = NULL;

/* The current state of segment allocation, including information about
 * which output segment numbers have been allocated, and their types and
 * amount of data which has already been allocated inside them.
 */
struct SegmentHeaderRec outputseg[RDF_MAXSEGS];
int nsegs = 0;
int32_t bss_length;

/* global options which affect how the program behaves */
struct ldrdfoptions {
    int verbose;
    int align;
    int dynalink;
    int strip;
    int respfile;
    int stderr_redir;
    int objpath;
    int libpath;
} options;

int errorcount = 0;             /* determines main program exit status */

/* =========================================================================
 * Utility functions
 */

/*
 * initsegments()
 *
 * sets up segments 0, 1, and 2, the initial code data and bss segments
 */
static void initsegments(void)
{
    nsegs = 3;
    outputseg[0].type = 1;
    outputseg[0].number = 0;
    outputseg[0].reserved = 0;
    outputseg[0].length = 0;
    outputseg[1].type = 2;
    outputseg[1].number = 1;
    outputseg[1].reserved = 0;
    outputseg[1].length = 0;
    outputseg[2].type = 0xFFFF; /* reserved segment type */
    outputseg[2].number = 2;
    outputseg[2].reserved = 0;
    outputseg[2].length = 0;
    bss_length = 0;
}

/*
 * loadmodule()
 *
 * Determine the characteristics of a module, and decide what to do with
 * each segment it contains (including determining destination segments and
 * relocation factors for segments that	are kept).
 */
static void loadmodule(const char *filename)
{
    if (options.verbose)
        printf("loading `%s'\n", filename);

    /* allocate a new module entry on the end of the modules list */
    if (!modules) {
        modules = nasm_malloc(sizeof(*modules));
        lastmodule = modules;
    } else {
        lastmodule->next = nasm_malloc(sizeof(*modules));
        lastmodule = lastmodule->next;
    }

    if (!lastmodule) {
        fprintf(stderr, "ldrdf: out of memory\n");
        exit(1);
    }

    /* open the file using 'rdfopen', which returns nonzero on error */
    if (rdfopen(&lastmodule->f, filename) != 0) {
        rdfperror("ldrdf", filename);
        exit(1);
    }

    /*
     * store information about the module, and determine what segments
     * it contains, and what we should do with them (determine relocation
     * factor if we decide to keep them)
     */
    lastmodule->header = NULL;
    lastmodule->name = nasm_strdup(filename);
    lastmodule->next = NULL;

    processmodule(filename, lastmodule);
}

/*
 * processmodule()
 *
 * step through each segment, determine what exactly we're doing with
 * it, and if we intend to keep it, determine (a) which segment to
 * put it in and (b) whereabouts in that segment it will end up.
 * (b) is fairly easy, because we're now keeping track of how big each
 * segment in our output file is...
 */
void processmodule(const char *filename, struct modulenode *mod)
{
    struct segconfig sconf;
    int seg, outseg;
    void *header;
    rdfheaderrec *hr;
    int32_t bssamount = 0;
    int bss_was_referenced = 0;

    memset(&sconf, 0, sizeof sconf);

    for (seg = 0; seg < mod->f.nsegs; seg++) {
        /*
         * get the segment configuration for this type from the segment
         * table. getsegconfig() is a macro, defined in ldsegs.h.
         */
        getsegconfig(sconf, mod->f.seg[seg].type);

        if (options.verbose > 1) {
            printf("%s %04x [%04x:%10s] ", filename,
                   mod->f.seg[seg].number, mod->f.seg[seg].type,
                   sconf.typedesc);
        }
        /*
         * sconf->dowhat tells us what to do with a segment of this type.
         */
        switch (sconf.dowhat) {
        case SEG_IGNORE:
            /*
             * Set destination segment to -1, to indicate that this segment
             * should be ignored for the purpose of output, ie it is left
             * out of the linked executable.
             */
            mod->seginfo[seg].dest_seg = -1;
            if (options.verbose > 1)
                printf("IGNORED\n");
            break;

        case SEG_NEWSEG:
            /*
             * The configuration tells us to create a new segment for
             * each occurrence of this segment type.
             */
            outseg = allocnewseg(sconf.mergetype,
                                 mod->f.seg[seg].reserved);
            mod->seginfo[seg].dest_seg = outseg;
            mod->seginfo[seg].reloc = 0;
            outputseg[outseg].length = mod->f.seg[seg].length;
            if (options.verbose > 1)
                printf("=> %04x:%08"PRIx32" (+%04"PRIx32")\n", outseg,
                       mod->seginfo[seg].reloc, mod->f.seg[seg].length);
            break;

        case SEG_MERGE:
            /*
             * The configuration tells us to merge the segment with
             * a previously existing segment of type 'sconf.mergetype',
             * if one exists. Otherwise a new segment is created.
             * This is handled transparently by 'findsegment()'.
             */
            outseg = findsegment(sconf.mergetype,
                                 mod->f.seg[seg].reserved);
            mod->seginfo[seg].dest_seg = outseg;

            /*
             * We need to add alignment to these segments.
             */
            if (outputseg[outseg].length % options.align != 0)
                outputseg[outseg].length +=
                    options.align -
                    (outputseg[outseg].length % options.align);

            mod->seginfo[seg].reloc = outputseg[outseg].length;
            outputseg[outseg].length += mod->f.seg[seg].length;

            if (options.verbose > 1)
                printf("=> %04x:%08"PRIx32" (+%04"PRIx32")\n", outseg,
                       mod->seginfo[seg].reloc, mod->f.seg[seg].length);
        }

    }

    /*
     * extract symbols from the header, and dump them into the
     * symbol table
     */
    header = nasm_malloc(mod->f.header_len);
    if (!header) {
        fprintf(stderr, "ldrdf: not enough memory\n");
        exit(1);
    }
    if (rdfloadseg(&mod->f, RDOFF_HEADER, header)) {
        rdfperror("ldrdf", filename);
        exit(1);
    }

    while ((hr = rdfgetheaderrec(&mod->f))) {
        switch (hr->type) {
        case RDFREC_IMPORT:    /* imported symbol */
        case RDFREC_FARIMPORT:
            /* Define with seg = -1 */
            symtab_add(hr->i.label, -1, 0);
            break;

        case RDFREC_GLOBAL:{   /* exported symbol */
                int destseg;
                int32_t destreloc;

                if (hr->e.segment == 2) {
                    bss_was_referenced = 1;
                    destreloc = bss_length;
                    if (destreloc % options.align != 0)
                        destreloc +=
                            options.align - (destreloc % options.align);
                    destseg = 2;
                } else {
                    if ((destseg =
                         mod->seginfo[(int)hr->e.segment].dest_seg) == -1)
                        continue;
                    destreloc = mod->seginfo[(int)hr->e.segment].reloc;
                }
                symtab_add(hr->e.label, destseg, destreloc + hr->e.offset);
                break;
            }

        case RDFREC_BSS:       /* BSS reservation */
            /*
             * first, amalgamate all BSS reservations in this module
             * into one, because we allow this in the output format.
             */
            bssamount += hr->b.amount;
            break;

        case RDFREC_COMMON:{   /* Common variable */
                symtabEnt *ste = symtabFind(symtab, hr->c.label);

                /* Is the symbol already in the table? */
                if (ste)
                    break;

                /* Align the variable */
                if (bss_length % hr->c.align != 0)
                    bss_length += hr->c.align - (bss_length % hr->c.align);
                if (options.verbose > 1) {
                    printf("%s %04x common '%s' => 0002:%08"PRIx32" (+%04"PRIx32")\n",
                           filename, hr->c.segment, hr->c.label,
                           bss_length, hr->c.size);
                }

                symtab_add(hr->c.label, 2, bss_length);
                mod->bss_reloc = bss_length;
                bss_length += hr->c.size;
                break;
            }
        }
    }

    if (bssamount != 0 || bss_was_referenced) {
        /*
         * handle the BSS segment - first pad the existing bss length
         * to the correct alignment, then store the length in bss_reloc
         * for this module. Then add this module's BSS length onto
         * bss_length.
         */
        if (bss_length % options.align != 0)
            bss_length += options.align - (bss_length % options.align);

        mod->bss_reloc = bss_length;
        if (options.verbose > 1) {
            printf("%s 0002 [            BSS] => 0002:%08"PRIx32" (+%04"PRIx32")\n",
                   filename, bss_length, bssamount);
        }
        bss_length += bssamount;
    }
#ifdef STINGY_MEMORY
    /*
     * we free the header buffer here, to save memory later.
     * this isn't efficient, but probably halves the memory usage
     * of this program...
     */
    mod->f.header_loc = NULL;
    nasm_free(header);

#endif

}

/*
 * Return 1 if a given module is in the list, 0 otherwise.
 */
static int lookformodule(const char *name)
{
    struct modulenode *curr = modules;

    while (curr) {
        if (!strcmp(name, curr->name))
            return 1;
        curr = curr->next;
    }
    return 0;
}

/*
 * allocnewseg()
 * findsegment()
 *
 * These functions manipulate the array of output segments, and are used
 * by processmodule(). allocnewseg() allocates a segment in the array,
 * initialising it to be empty. findsegment() first scans the array for
 * a segment of the type requested, and if one isn't found allocates a
 * new one.
 */
int allocnewseg(uint16_t type, uint16_t reserved)
{
    outputseg[nsegs].type = type;
    outputseg[nsegs].number = nsegs;
    outputseg[nsegs].reserved = reserved;
    outputseg[nsegs].length = 0;
    outputseg[nsegs].offset = 0;
    outputseg[nsegs].data = NULL;

    return nsegs++;
}

int findsegment(uint16_t type, uint16_t reserved)
{
    int i;

    for (i = 0; i < nsegs; i++)
        if (outputseg[i].type == type)
            return i;

    return allocnewseg(type, reserved);
}

/*
 * symtab_add()
 *
 * inserts a symbol into the global symbol table, which associates symbol
 * names either with addresses, or a marker that the symbol hasn't been
 * resolved yet, or possibly that the symbol has been defined as
 * contained in a dynamic [load time/run time] linked library.
 *
 * segment = -1 => not yet defined
 * segment = -2 => defined as dll symbol
 *
 * If the symbol is already defined, and the new segment >= 0, then
 * if the original segment was < 0 the symbol is redefined, otherwise
 * a duplicate symbol warning is issued. If new segment == -1, this
 * routine won't change a previously existing symbol. It will change
 * to segment = -2 only if the segment was previously < 0.
 */
void symtab_add(const char *symbol, int segment, int32_t offset)
{
    symtabEnt *ste;

    ste = symtabFind(symtab, symbol);
    if (ste) {
        if (ste->segment >= 0) {
            /*
             * symbol previously defined
             */
            if (segment < 0)
                return;
            fprintf(error_file, "warning: `%s' redefined\n", symbol);
            return;
        }

        /*
         * somebody wanted the symbol, and put an undefined symbol
         * marker into the table
         */
        if (segment == -1)
            return;
        /*
         * we have more information now - update the symbol's entry
         */
        ste->segment = segment;
        ste->offset = offset;
        ste->flags = 0;
        return;
    }
    /*
     * this is the first declaration of this symbol
     */
    ste = nasm_malloc(sizeof(symtabEnt));
    if (!ste) {
        fprintf(stderr, "ldrdf: out of memory\n");
        exit(1);
    }
    ste->name = nasm_strdup(symbol);
    ste->segment = segment;
    ste->offset = offset;
    ste->flags = 0;
    symtabInsert(symtab, ste);
}

/*
 * symtab_get()
 *
 * Retrieves the values associated with a symbol. Undefined symbols
 * are assumed to have -1:0 associated. Returns 1 if the symbol was
 * successfully located.
 */
int symtab_get(const char *symbol, int *segment, int32_t *offset)
{
    symtabEnt *ste = symtabFind(symtab, symbol);
    if (!ste) {
        *segment = -1;
        *offset = 0;
        return 0;
    } else {
        *segment = ste->segment;
        *offset = ste->offset;
        return 1;
    }
}

/*
 * add_library()
 *
 * checks that a library can be opened and is in the correct format,
 * then adds it to the linked list of libraries.
 */
static void add_library(const char *name)
{
    if (rdl_verify(name)) {
        rdl_perror("ldrdf", name);
        errorcount++;
        return;
    }
    if (!libraries) {
        lastlib = libraries = nasm_malloc(sizeof(*libraries));
        if (!libraries) {
            fprintf(stderr, "ldrdf: out of memory\n");
            exit(1);
        }
    } else {
        lastlib->next = nasm_malloc(sizeof(*libraries));
        if (!lastlib->next) {
            fprintf(stderr, "ldrdf: out of memory\n");
            exit(1);
        }
        lastlib = lastlib->next;
    }
    lastlib->next = NULL;
    if (rdl_open(lastlib, name)) {
        rdl_perror("ldrdf", name);
        errorcount++;
        return;
    }
}

/*
 * search_libraries()
 *
 * scans through the list of libraries, attempting to match symbols
 * defined in library modules against symbols that are referenced but
 * not defined (segment = -1 in the symbol table)
 *
 * returns 1 if any extra library modules are included, indicating that
 * another pass through the library list should be made (possibly).
 */
static int search_libraries(void)
{
    struct librarynode *cur;
    rdffile f;
    int i;
    void *header;
    int segment;
    int32_t offset;
    int doneanything = 0, pass = 1, keepfile;
    rdfheaderrec *hr;

    cur = libraries;

    while (cur) {
        if (options.verbose > 2)
            printf("scanning library `%s', pass %d...\n", cur->name, pass);

        for (i = 0; rdl_openmodule(cur, i, &f) == 0; i++) {
            if (pass == 2 && lookformodule(f.name))
                continue;

            if (options.verbose > 3)
                printf("  looking in module `%s'\n", f.name);

            header = nasm_malloc(f.header_len);
            if (!header) {
                fprintf(stderr, "ldrdf: not enough memory\n");
                exit(1);
            }
            if (rdfloadseg(&f, RDOFF_HEADER, header)) {
                rdfperror("ldrdf", f.name);
                errorcount++;
                return 0;
            }

            keepfile = 0;

            while ((hr = rdfgetheaderrec(&f))) {
                /* We're only interested in exports, so skip others */
                if (hr->type != RDFREC_GLOBAL)
                    continue;

                /*
                 * If the symbol is marked as SYM_GLOBAL, somebody will be
                 * definitely interested in it..
                 */
                if ((hr->e.flags & SYM_GLOBAL) == 0) {
                    /*
                     * otherwise the symbol is just public. Find it in
                     * the symbol table. If the symbol isn't defined, we
                     * aren't interested, so go on to the next.
                     * If it is defined as anything but -1, we're also not
                     * interested. But if it is defined as -1, insert this
                     * module into the list of modules to use, and go
                     * immediately on to the next module...
                     */
                    if (!symtab_get(hr->e.label, &segment, &offset)
                        || segment != -1)
                        continue;
                }

                doneanything = 1;
                keepfile = 1;

                /*
                 * as there are undefined symbols, we can assume that
                 * there are modules on the module list by the time
                 * we get here.
                 */
                lastmodule->next = nasm_malloc(sizeof(*lastmodule->next));
                if (!lastmodule->next) {
                    fprintf(stderr, "ldrdf: not enough memory\n");
                    exit(1);
                }
                lastmodule = lastmodule->next;
                memcpy(&lastmodule->f, &f, sizeof(f));
                lastmodule->name = nasm_strdup(f.name);
                lastmodule->next = NULL;
                processmodule(f.name, lastmodule);
                break;
            }
            if (!keepfile) {
                nasm_free(f.name);
                f.name = NULL;
                f.fp = NULL;
            }
        }
        if (rdl_error != 0 && rdl_error != RDL_ENOTFOUND)
            rdl_perror("ldrdf", cur->name);

        cur = cur->next;
        if (cur == NULL && pass == 1) {
            cur = libraries;
            pass++;
        }
    }

    return doneanything;
}

/*
 * write_output()
 *
 * this takes the linked list of modules, and walks through it, merging
 * all the modules into a single output module, and then writes this to a
 * file.
 */
static void write_output(const char *filename)
{
    FILE *f;
    rdf_headerbuf *rdfheader;
    struct modulenode *cur;
    int i, n, availableseg, seg, localseg, isrelative;
    void *header;
    rdfheaderrec *hr, newrec;
    symtabEnt *se;
    segtab segs;
    int32_t offset;
    uint8_t *data;

    if ((f = fopen(filename, "wb")) == NULL) {
        fprintf(stderr, "ldrdf: couldn't open %s for output\n", filename);
        exit(1);
    }
    if ((rdfheader = rdfnewheader()) == NULL) {
        fprintf(stderr, "ldrdf: out of memory\n");
        exit(1);
    }

    /*
     * If '-g' option was given, first record in output file will be a
     * `generic' record, filled with a given file content.
     * This can be useful, for example, when constructing multiboot
     * compliant kernels.
     */
    if (generic_rec_file) {
        FILE *ff;

        if (options.verbose)
            printf("\nadding generic record from binary file %s\n",
                   generic_rec_file);

        hr = (rdfheaderrec *) nasm_malloc(sizeof(struct GenericRec));
        if ((ff = fopen(generic_rec_file, "r")) == NULL) {
            fprintf(stderr, "ldrdf: couldn't open %s for input\n",
                    generic_rec_file);
            exit(1);
        }
        n = fread(hr->g.data, 1, sizeof(hr->g.data), ff);
        fseek(ff, 0, SEEK_END);
        if (ftell(ff) > (long)sizeof(hr->g.data)) {
            fprintf(error_file,
                    "warning: maximum generic record size is %u, "
		    "rest of file ignored\n",
                    (unsigned int)sizeof(hr->g.data));
        }
        fclose(ff);

        hr->g.type = RDFREC_GENERIC;
        hr->g.reclen = n;
        rdfaddheader(rdfheader, hr);
        nasm_free(hr);
    }

    /*
     * Add module name record if `-mn' option was given
     */
    if (modname_specified) {
	n = strlen(modname_specified);

	if ((n < 1) || (n >= MODLIB_NAME_MAX)) {
            fprintf(stderr, "ldrdf: invalid length of module name `%s'\n",
		modname_specified);
            exit(1);
        }

        if (options.verbose)
            printf("\nadding module name record %s\n", modname_specified);

        hr = (rdfheaderrec *) nasm_malloc(sizeof(struct ModRec));
	hr->m.type = RDFREC_MODNAME;
        hr->m.reclen = n + 1;
        strcpy(hr->m.modname, modname_specified);
        rdfaddheader(rdfheader, hr);
        nasm_free(hr);
    }


    if (options.verbose)
        printf("\nbuilding output module (%d segments)\n", nsegs);

    /*
     * Allocate the memory for the segments. We may be better off
     * building the output module one segment at a time when running
     * under 16 bit DOS, but that would be a slower way of doing this.
     * And you could always use DJGPP...
     */
    for (i = 0; i < nsegs; i++) {
        outputseg[i].data = NULL;
        if (!outputseg[i].length)
            continue;
        outputseg[i].data = nasm_malloc(outputseg[i].length);
        if (!outputseg[i].data) {
            fprintf(stderr, "ldrdf: out of memory\n");
            exit(1);
        }
    }

    /*
     * initialise availableseg, used to allocate segment numbers for
     * imported and exported labels...
     */
    availableseg = nsegs;

    /*
     * Step through the modules, performing required actions on each one
     */
    for (cur = modules; cur; cur = cur->next) {
        /*
         * Read the actual segment contents into the correct places in
         * the newly allocated segments
         */

        for (i = 0; i < cur->f.nsegs; i++) {
            int dest = cur->seginfo[i].dest_seg;

            if (dest == -1)
                continue;
            if (rdfloadseg(&cur->f, i,
                           outputseg[dest].data + cur->seginfo[i].reloc)) {
                rdfperror("ldrdf", cur->name);
                exit(1);
            }
        }

        /*
         * Perform fixups, and add new header records where required
         */

        header = nasm_malloc(cur->f.header_len);
        if (!header) {
            fprintf(stderr, "ldrdf: out of memory\n");
            exit(1);
        }

        if (cur->f.header_loc)
            rdfheaderrewind(&cur->f);
        else if (rdfloadseg(&cur->f, RDOFF_HEADER, header)) {
            rdfperror("ldrdf", cur->name);
            exit(1);
        }

        /*
         * we need to create a local segment number -> location
         * table for the segments in this module.
         */
        init_seglocations(&segs);
        for (i = 0; i < cur->f.nsegs; i++) {
            add_seglocation(&segs, cur->f.seg[i].number,
                            cur->seginfo[i].dest_seg,
                            cur->seginfo[i].reloc);
        }
        /*
         * and the BSS segment (doh!)
         */
        add_seglocation(&segs, 2, 2, cur->bss_reloc);

        while ((hr = rdfgetheaderrec(&cur->f))) {
            switch (hr->type) {
            case RDFREC_RELOC: /* relocation record - need to do a fixup */
                /*
                 * First correct the offset stored in the segment from
                 * the start of the segment (which may well have changed).
                 *
                 * To do this we add to the number stored the relocation
                 * factor associated with the segment that contains the
                 * target segment.
                 *
                 * The relocation could be a relative relocation, in which
                 * case we have to first subtract the amount we've relocated
                 * the containing segment by.
                 */
                if (!get_seglocation(&segs, hr->r.refseg, &seg, &offset)) {
                    fprintf(stderr,
                            "%s: reloc to undefined segment %04x\n",
                            cur->name, (int)hr->r.refseg);
                    errorcount++;
                    break;
                }

                isrelative =
                    (hr->r.segment & RDOFF_RELATIVEMASK) ==
                    RDOFF_RELATIVEMASK;
                hr->r.segment &= (RDOFF_RELATIVEMASK - 1);

                if (hr->r.segment == 2 ||
                    (localseg =
                     rdffindsegment(&cur->f, hr->r.segment)) == -1) {
                    fprintf(stderr, "%s: reloc from %s segment (%d)\n",
                            cur->name,
                            hr->r.segment == 2 ? "BSS" : "unknown",
                            hr->r.segment);
                    errorcount++;
                    break;
                }

                if (hr->r.length != 1 && hr->r.length != 2 &&
                    hr->r.length != 4) {
                    fprintf(stderr, "%s: nonstandard length reloc "
                            "(%d bytes)\n", cur->name, hr->r.length);
                    errorcount++;
                    break;
                }

                /*
                 * okay, now the relocation is in the segment pointed to by
                 * cur->seginfo[localseg], and we know everything else is
                 * okay to go ahead and do the relocation
                 */
                data = outputseg[cur->seginfo[localseg].dest_seg].data;
                data += cur->seginfo[localseg].reloc + hr->r.offset;

                /*
                 * data now points to the reference that needs
                 * relocation. Calculate the relocation factor.
                 * Factor is:
                 *      offset of referred object in segment [in offset]
                 *      (- relocation of localseg, if ref is relative)
                 * For simplicity, the result is stored in 'offset'.
                 * Then add 'offset' onto the value at data.
                 */

                if (isrelative)
                    offset -= cur->seginfo[localseg].reloc;
                switch (hr->r.length) {
                case 1:
                    offset += *data;
                    if (offset < -127 || offset > 128)
                        fprintf(error_file,
                                "warning: relocation out of range "
                                "at %s(%02x:%08"PRIx32")\n", cur->name,
                                (int)hr->r.segment, hr->r.offset);
                    *data = (char)offset;
                    break;
                case 2:
                    offset += *(int16_t *)data;
                    if (offset < -32767 || offset > 32768)
                        fprintf(error_file,
                                "warning: relocation out of range "
                                "at %s(%02x:%08"PRIx32")\n", cur->name,
                                (int)hr->r.segment, hr->r.offset);
                    *(int16_t *)data = (int16_t)offset;
                    break;
                case 4:
                    *(int32_t *)data += offset;
                    /* we can't easily detect overflow on this one */
                    break;
                }

                /*
                 * If the relocation was relative between two symbols in
                 * the same segment, then we're done.
                 *
                 * Otherwise, we need to output a new relocation record
                 * with the references updated segment and offset...
                 */
                if (!isrelative || cur->seginfo[localseg].dest_seg != seg) {
                    hr->r.segment = cur->seginfo[localseg].dest_seg;
                    hr->r.offset += cur->seginfo[localseg].reloc;
                    hr->r.refseg = seg;
                    if (isrelative)
                        hr->r.segment += RDOFF_RELATIVEMASK;
                    rdfaddheader(rdfheader, hr);
                }
                break;

            case RDFREC_IMPORT:        /* import symbol */
            case RDFREC_FARIMPORT:
                /*
                 * scan the global symbol table for the symbol
                 * and associate its location with the segment number
                 * for this module
                 */
                se = symtabFind(symtab, hr->i.label);
                if (!se || se->segment == -1) {
                    if (!options.dynalink && !(hr->i.flags & SYM_IMPORT)) {
                        fprintf(error_file,
                                "error: unresolved reference to `%s'"
                                " in module `%s'\n", hr->i.label,
                                cur->name);
                        errorcount++;
                    }
                    /*
                     * we need to allocate a segment number for this
                     * symbol, and store it in the symbol table for
                     * future reference
                     */
                    if (!se) {
                        se = nasm_malloc(sizeof(*se));
                        if (!se) {
                            fprintf(stderr, "ldrdf: out of memory\n");
                            exit(1);
                        }
                        se->name = nasm_strdup(hr->i.label);
                        se->flags = 0;
                        se->segment = availableseg++;
                        se->offset = 0;
                        symtabInsert(symtab, se);
                    } else {
                        se->segment = availableseg++;
                        se->offset = 0;
                    }
                    /*
                     * output a header record that imports it to the
                     * recently allocated segment number...
                     */
                    newrec = *hr;
                    newrec.i.segment = se->segment;
                    rdfaddheader(rdfheader, &newrec);
                }

                add_seglocation(&segs, hr->i.segment, se->segment,
                                se->offset);
                break;

            case RDFREC_GLOBAL:        /* export symbol */
                /*
                 * need to insert an export for this symbol into the new
                 * header, unless we're stripping symbols. Even if we're
                 * stripping, put the symbol if it's marked as SYM_GLOBAL.
                 */
                if (options.strip && !(hr->e.flags & SYM_GLOBAL))
                    break;

                if (hr->e.segment == 2) {
                    seg = 2;
                    offset = cur->bss_reloc;
                } else {
                    localseg = rdffindsegment(&cur->f, hr->e.segment);
                    if (localseg == -1) {
                        fprintf(stderr, "%s: exported symbol `%s' from "
                                "unrecognised segment\n", cur->name,
                                hr->e.label);
                        errorcount++;
                        break;
                    }
                    offset = cur->seginfo[localseg].reloc;
                    seg = cur->seginfo[localseg].dest_seg;
                }

                hr->e.segment = seg;
                hr->e.offset += offset;
                rdfaddheader(rdfheader, hr);
                break;

            case RDFREC_MODNAME:       /* module name */
                /*
                 * Insert module name record if export symbols
                 * are not stripped.
                 * If module name begins with '$' - insert it anyway.
                 */
                if (options.strip && hr->m.modname[0] != '$')
                    break;
                rdfaddheader(rdfheader, hr);
                break;

            case RDFREC_DLL:   /* DLL name */
                /*
                 * Insert DLL name if it begins with '$'
                 */
                if (hr->d.libname[0] != '$')
                    break;
                rdfaddheader(rdfheader, hr);
                break;

            case RDFREC_SEGRELOC:      /* segment fixup */
                /*
                 * modify the segment numbers if necessary, and
                 * pass straight through to the output module header
                 *
                 * *** FIXME ***
                 */
                if (hr->r.segment == 2) {
                    fprintf(stderr, "%s: segment fixup in BSS section\n",
                            cur->name);
                    errorcount++;
                    break;
                }
                localseg = rdffindsegment(&cur->f, hr->r.segment);
                if (localseg == -1) {
                    fprintf(stderr, "%s: segment fixup in unrecognised"
                            " segment (%d)\n", cur->name, hr->r.segment);
                    errorcount++;
                    break;
                }
                hr->r.segment = cur->seginfo[localseg].dest_seg;
                hr->r.offset += cur->seginfo[localseg].reloc;

                if (!get_seglocation(&segs, hr->r.refseg, &seg, &offset)) {
                    fprintf(stderr, "%s: segment fixup to undefined "
                            "segment %04x\n", cur->name,
                            (int)hr->r.refseg);
                    errorcount++;
                    break;
                }
                hr->r.refseg = seg;
                rdfaddheader(rdfheader, hr);
                break;

            case RDFREC_COMMON:        /* Common variable */
                /* Is this symbol already in the table? */
                se = symtabFind(symtab, hr->c.label);
                if (!se) {
                    printf("%s is not in symtab yet\n", hr->c.label);
                    break;
                }
                /* Add segment location */
                add_seglocation(&segs, hr->c.segment, se->segment,
                                se->offset);
                break;
            }
        }

        nasm_free(header);
        done_seglocations(&segs);

    }

    /*
     * combined BSS reservation for the entire results
     */
    newrec.type = RDFREC_BSS;
    newrec.b.reclen = 4;
    newrec.b.amount = bss_length;
    rdfaddheader(rdfheader, &newrec);

    /*
     * Write the header
     */
    for (i = 0; i < nsegs; i++) {
        if (i == 2)
            continue;
        rdfaddsegment(rdfheader, outputseg[i].length);
    }

    rdfwriteheader(f, rdfheader);
    rdfdoneheader(rdfheader);

    /*
     * Step through the segments, one at a time, writing out into
     * the output file
     */
    for (i = 0; i < nsegs; i++) {
        if (i == 2)
            continue;

        fwriteint16_t(outputseg[i].type, f);
        fwriteint16_t(outputseg[i].number, f);
        fwriteint16_t(outputseg[i].reserved, f);
        fwriteint32_t(outputseg[i].length, f);
        nasm_write(outputseg[i].data, outputseg[i].length, f);
    }

    fwritezero(10, f);
}

/* =========================================================================
 * Main program
 */

static void usage(void)
{
    printf("usage:\n"
           "   ldrdf [options] object modules ... [-llibrary ...]\n"
           "   ldrdf -r\n"
           "options:\n"
           "   -v[=n]          increase verbosity by 1, or set it to n\n"
           "   -a nn           set segment alignment value (default 16)\n"
           "   -s              strip public symbols\n"
           "   -dy             Unix-style dynamic linking\n"
           "   -o name         write output in file 'name'\n"
           "   -j path         specify objects search path\n"
           "   -L path         specify libraries search path\n"
           "   -g file         embed 'file' as a first header record with type 'generic'\n"
           "   -mn name        add module name record at the beginning of output file\n");
    exit(0);
}

int main(int argc, char **argv)
{
    char *outname = "aout.rdf";
    int moduleloaded = 0;
    char *respstrings[128] = { 0, };

    rdoff_init();

    options.verbose = 0;
    options.align = 16;
    options.dynalink = 0;
    options.strip = 0;

    error_file = stderr;

    argc--, argv++;
    if (argc == 0)
        usage();
    while (argc && *argv && **argv == '-' && argv[0][1] != 'l') {
        switch (argv[0][1]) {
        case 'r':
            printf("ldrdf (linker for RDF files) version " LDRDF_VERSION
                   "\n");
            printf("RDOFF2 revision %s\n", RDOFF2_REVISION);
            exit(0);
        case 'v':
            if (argv[0][2] == '=') {
                options.verbose = argv[0][3] - '0';
                if (options.verbose < 0 || options.verbose > 9) {
                    fprintf(stderr,
                            "ldrdf: verbosity level must be a number"
                            " between 0 and 9\n");
                    exit(1);
                }
            } else
                options.verbose++;
            break;
        case 'a':
            options.align = atoi(argv[1]);
            if (options.align <= 0) {
                fprintf(stderr,
                        "ldrdf: -a expects a positive number argument\n");
                exit(1);
            }
            argv++, argc--;
            break;
        case 's':
            options.strip = 1;
            break;
        case 'd':
            if (argv[0][2] == 'y')
                options.dynalink = 1;
            break;
        case 'm':
            if (argv[0][2] == 'n') {
                modname_specified = argv[1];
		argv++, argc--;
		if (!argc) {
		    fprintf(stderr, "ldrdf: -mn expects a module name\n");
		    exit(1);
		}
	    }
            break;
        case 'o':
            outname = argv[1];
            argv++, argc--;
            break;
        case 'j':
            if (!objpath) {
                options.objpath = 1;
                objpath = argv[1];
                argv++, argc--;
                break;
            } else {
                fprintf(stderr,
                        "ldrdf: more than one objects search path specified\n");
                exit(1);
            }
        case 'L':
            if (!libpath) {
                options.libpath = 1;
                libpath = argv[1];
                argv++, argc--;
                break;
            } else {
                fprintf(stderr,
                        "ldrdf: more than one libraries search path specified\n");
                exit(1);
            }
        case '@':{
                int i = 0;
                char buf[256];
                FILE *f;

                options.respfile = 1;
                if (argv[1] != NULL)
                    f = fopen(argv[1], "r");
                else {
                    fprintf(stderr,
                            "ldrdf: no response file name specified\n");
                    exit(1);
                }

                if (f == NULL) {
                    fprintf(stderr,
                            "ldrdf: unable to open response file\n");
                    exit(1);
                }

                argv++, argc--;
                while (fgets(buf, sizeof(buf), f) != NULL) {
                    char *p;
                    if (buf[0] == '\n')
                        continue;
                    if ((p = strchr(buf, '\n')) != NULL)
                        *p = '\0';
                    if (i >= 128) {
                        fclose(f);
                        fprintf(stderr, "ldrdf: too many input files\n");
                        exit(1);
                    }
                    *(respstrings + i) = nasm_strdup(buf);
                    argc++, i++;
                }
                fclose(f);
                break;
            }
        case '2':
            options.stderr_redir = 1;
            error_file = stdout;
            break;
        case 'g':
            generic_rec_file = argv[1];
            argv++, argc--;
            if (!argc) {
		fprintf(stderr, "ldrdf: -g expects a file name\n");
		exit(1);
	    }
            break;
        default:
            usage();
        }
        argv++, argc--;
    }

    if (options.verbose > 4) {
        printf("ldrdf invoked with options:\n");
        printf("    section alignment: %d bytes\n", options.align);
        printf("    output name: `%s'\n", outname);
        if (options.strip)
            printf("    strip symbols\n");
        if (options.dynalink)
            printf("    Unix-style dynamic linking\n");
        if (options.objpath)
            printf("    objects search path: %s\n", objpath);
        if (options.libpath)
            printf("    libraries search path: %s\n", libpath);
        printf("\n");
    }

    symtab = symtabNew();
    initsegments();

    if (!symtab) {
        fprintf(stderr, "ldrdf: out of memory\n");
        exit(1);
    }

    while (argc) {
        if (!*argv)
            argv = respstrings;
        if (!*argv)
            break;
        if (!strncmp(*argv, "-l", 2)) {
            if (libpath && (argv[0][2] != '/'))
                add_library(nasm_strcat(libpath, *argv + 2));
            else
                add_library(*argv + 2);
        } else {
            if (objpath && (argv[0][0] != '/'))
                loadmodule(nasm_strcat(objpath, *argv));
            else
                loadmodule(*argv);
            moduleloaded = 1;
        }
        argv++, argc--;
    }

    if (!moduleloaded) {
        printf("ldrdf: nothing to do. ldrdf -h for usage\n");
        return 0;
    }

    search_libraries();

    if (options.verbose > 2) {
        printf("symbol table:\n");
        symtabDump(symtab, stdout);
    }

    write_output(outname);

    if (errorcount > 0) {
        remove(outname);
        exit(1);
    }
    return 0;
}
