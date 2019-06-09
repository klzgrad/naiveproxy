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
 * outcoff.c    output routines for the Netwide Assembler to produce
 *              COFF object files (for DJGPP and Win32)
 */

#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "nasm.h"
#include "nasmlib.h"
#include "ilog2.h"
#include "error.h"
#include "saa.h"
#include "raa.h"
#include "eval.h"
#include "outform.h"
#include "outlib.h"
#include "pecoff.h"

#if defined(OF_COFF) || defined(OF_WIN32) || defined(OF_WIN64)

/*
 * Notes on COFF:
 *
 * (0) When I say `standard COFF' below, I mean `COFF as output and
 * used by DJGPP'. I assume DJGPP gets it right.
 *
 * (1) Win32 appears to interpret the term `relative relocation'
 * differently from standard COFF. Standard COFF understands a
 * relative relocation to mean that during relocation you add the
 * address of the symbol you're referencing, and subtract the base
 * address of the section you're in. Win32 COFF, by contrast, seems
 * to add the address of the symbol and then subtract the address
 * of THE BYTE AFTER THE RELOCATED DWORD. Hence the two formats are
 * subtly incompatible.
 *
 * (2) Win32 doesn't bother putting any flags in the header flags
 * field (at offset 0x12 into the file).
 *
 * (3) Win32 uses some extra flags into the section header table:
 * it defines flags 0x80000000 (writable), 0x40000000 (readable)
 * and 0x20000000 (executable), and uses them in the expected
 * combinations. It also defines 0x00100000 through 0x00700000 for
 * section alignments of 1 through 64 bytes.
 *
 * (4) Both standard COFF and Win32 COFF seem to use the DWORD
 * field directly after the section name in the section header
 * table for something strange: they store what the address of the
 * section start point _would_ be, if you laid all the sections end
 * to end starting at zero. Dunno why. Microsoft's documentation
 * lists this field as "Virtual Size of Section", which doesn't
 * seem to fit at all. In fact, Win32 even includes non-linked
 * sections such as .drectve in this calculation.
 *
 * Newer versions of MASM seem to have changed this to be zero, and
 * that apparently matches the COFF spec, so go with that.
 *
 * (5) Standard COFF does something very strange to common
 * variables: the relocation point for a common variable is as far
 * _before_ the variable as its size stretches out _after_ it. So
 * we must fix up common variable references. Win32 seems to be
 * sensible on this one.
 */

/* Flag which version of COFF we are currently outputting. */
bool win32, win64;

static int32_t imagebase_sect;
#define WRT_IMAGEBASE "..imagebase"

/*
 * Some common section flags by default
 */
#define TEXT_FLAGS_WIN                                  \
        (IMAGE_SCN_CNT_CODE                     |       \
         IMAGE_SCN_ALIGN_16BYTES                |       \
         IMAGE_SCN_MEM_EXECUTE                  |       \
         IMAGE_SCN_MEM_READ)
#define TEXT_FLAGS_DOS                                  \
        (IMAGE_SCN_CNT_CODE)

#define DATA_FLAGS_WIN                                  \
        (IMAGE_SCN_CNT_INITIALIZED_DATA         |       \
         IMAGE_SCN_ALIGN_4BYTES                 |       \
         IMAGE_SCN_MEM_READ                     |       \
         IMAGE_SCN_MEM_WRITE)
#define DATA_FLAGS_DOS                                  \
        (IMAGE_SCN_CNT_INITIALIZED_DATA)

#define BSS_FLAGS_WIN                                   \
        (IMAGE_SCN_CNT_UNINITIALIZED_DATA       |       \
         IMAGE_SCN_ALIGN_4BYTES                 |       \
         IMAGE_SCN_MEM_READ                     |       \
         IMAGE_SCN_MEM_WRITE)
#define BSS_FLAGS_DOS                                   \
        (IMAGE_SCN_CNT_UNINITIALIZED_DATA)

#define RDATA_FLAGS_WIN                                 \
        (IMAGE_SCN_CNT_INITIALIZED_DATA         |       \
         IMAGE_SCN_ALIGN_8BYTES                 |       \
         IMAGE_SCN_MEM_READ)

#define RDATA_FLAGS_DOS                                 \
        (IMAGE_SCN_CNT_INITIALIZED_DATA)

#define PDATA_FLAGS                                     \
        (IMAGE_SCN_CNT_INITIALIZED_DATA         |       \
         IMAGE_SCN_ALIGN_4BYTES                 |       \
         IMAGE_SCN_MEM_READ)

#define XDATA_FLAGS                                     \
        (IMAGE_SCN_CNT_INITIALIZED_DATA         |       \
         IMAGE_SCN_ALIGN_8BYTES                 |       \
         IMAGE_SCN_MEM_READ)

#define INFO_FLAGS                                      \
        (IMAGE_SCN_ALIGN_1BYTES                 |       \
         IMAGE_SCN_LNK_INFO                     |       \
         IMAGE_SCN_LNK_REMOVE)

#define TEXT_FLAGS      ((win32 | win64) ? TEXT_FLAGS_WIN  : TEXT_FLAGS_DOS)
#define DATA_FLAGS      ((win32 | win64) ? DATA_FLAGS_WIN  : DATA_FLAGS_DOS)
#define BSS_FLAGS       ((win32 | win64) ? BSS_FLAGS_WIN   : BSS_FLAGS_DOS)
#define RDATA_FLAGS     ((win32 | win64) ? RDATA_FLAGS_WIN : RDATA_FLAGS_DOS)

#define SECT_DELTA 32
struct coff_Section **coff_sects;
static int sectlen;
int coff_nsects;

struct SAA *coff_syms;
uint32_t coff_nsyms;

static int32_t def_seg;

static int initsym;

static struct RAA *bsym, *symval;

struct SAA *coff_strs;
static uint32_t strslen;

static void coff_gen_init(void);
static void coff_sect_write(struct coff_Section *, const uint8_t *, uint32_t);
static void coff_write(void);
static void coff_section_header(char *, int32_t, int32_t, int32_t, int32_t, int32_t, int, int32_t);
static void coff_write_relocs(struct coff_Section *);
static void coff_write_symbols(void);

static void coff_win32_init(void)
{
    win32 = true;
    win64 = false;
    coff_gen_init();
}

static void coff_win64_init(void)
{
    win32 = false;
    win64 = true;
    coff_gen_init();
    imagebase_sect = seg_alloc()+1;
    backend_label(WRT_IMAGEBASE, imagebase_sect, 0);
}

static void coff_std_init(void)
{
    win32 = win64 = false;
    coff_gen_init();
}

static void coff_gen_init(void)
{

    coff_sects = NULL;
    coff_nsects = sectlen = 0;
    coff_syms = saa_init(sizeof(struct coff_Symbol));
    coff_nsyms = 0;
    bsym = raa_init();
    symval = raa_init();
    coff_strs = saa_init(1);
    strslen = 0;
    def_seg = seg_alloc();
}

static void coff_cleanup(void)
{
    struct coff_Reloc *r;
    int i;

    dfmt->cleanup();

    coff_write();
    for (i = 0; i < coff_nsects; i++) {
        if (coff_sects[i]->data)
            saa_free(coff_sects[i]->data);
        while (coff_sects[i]->head) {
            r = coff_sects[i]->head;
            coff_sects[i]->head = coff_sects[i]->head->next;
            nasm_free(r);
        }
        nasm_free(coff_sects[i]->name);
        nasm_free(coff_sects[i]);
    }
    nasm_free(coff_sects);
    saa_free(coff_syms);
    raa_free(bsym);
    raa_free(symval);
    saa_free(coff_strs);
}

int coff_make_section(char *name, uint32_t flags)
{
    struct coff_Section *s;
    size_t namelen;

    s = nasm_zalloc(sizeof(*s));

    if (flags != BSS_FLAGS)
        s->data = saa_init(1);
    s->tail = &s->head;
    if (!strcmp(name, ".text"))
        s->index = def_seg;
    else
        s->index = seg_alloc();
    s->namepos = -1;
    namelen = strlen(name);
    if (namelen > 8) {
        if (win32 || win64) {
            s->namepos = strslen + 4;
            saa_wbytes(coff_strs, name, namelen + 1);
            strslen += namelen + 1;
        } else {
            namelen = 8;
        }
    }
    s->name = nasm_malloc(namelen + 1);
    strncpy(s->name, name, namelen);
    s->name[namelen] = '\0';
    s->flags = flags;

    if (coff_nsects >= sectlen) {
        sectlen += SECT_DELTA;
        coff_sects = nasm_realloc(coff_sects, sectlen * sizeof(*coff_sects));
    }
    coff_sects[coff_nsects++] = s;

    return coff_nsects - 1;
}

static inline int32_t coff_sectalign_flags(unsigned int align)
{
    return (ilog2_32(align) + 1) << 20;
}

static int32_t coff_section_names(char *name, int pass, int *bits)
{
    char *p;
    uint32_t flags, align_and = ~0L, align_or = 0L;
    int i;

    /*
     * Set default bits.
     */
    if (!name) {
        if(win64)
            *bits = 64;
        else
            *bits = 32;

        return def_seg;
    }

    p = name;
    while (*p && !nasm_isspace(*p))
        p++;
    if (*p)
        *p++ = '\0';
    if (strlen(name) > 8) {
        if (!win32 && !win64) {
            nasm_error(ERR_WARNING,
                       "COFF section names limited to 8 characters:  truncating");
            name[8] = '\0';
        }
    }
    flags = 0;

    while (*p && nasm_isspace(*p))
        p++;
    while (*p) {
        char *q = p;
        while (*p && !nasm_isspace(*p))
            p++;
        if (*p)
            *p++ = '\0';
        while (*p && nasm_isspace(*p))
            p++;

        if (!nasm_stricmp(q, "code") || !nasm_stricmp(q, "text")) {
            flags = TEXT_FLAGS;
        } else if (!nasm_stricmp(q, "data")) {
            flags = DATA_FLAGS;
        } else if (!nasm_stricmp(q, "rdata")) {
            if (win32 | win64)
                flags = RDATA_FLAGS;
            else {
                flags = DATA_FLAGS;     /* gotta do something */
                nasm_error(ERR_NONFATAL, "standard COFF does not support"
                      " read-only data sections");
            }
        } else if (!nasm_stricmp(q, "bss")) {
            flags = BSS_FLAGS;
        } else if (!nasm_stricmp(q, "info")) {
            if (win32 | win64)
                flags = INFO_FLAGS;
            else {
                flags = DATA_FLAGS;     /* gotta do something */
                nasm_error(ERR_NONFATAL, "standard COFF does not support"
                      " informational sections");
            }
        } else if (!nasm_strnicmp(q, "align=", 6)) {
            if (!(win32 | win64))
                nasm_error(ERR_NONFATAL, "standard COFF does not support"
                      " section alignment specification");
            else {
                if (q[6 + strspn(q + 6, "0123456789")])
                    nasm_error(ERR_NONFATAL,
                          "argument to `align' is not numeric");
                else {
                    unsigned int align = atoi(q + 6);
                    if (!align || ((align - 1) & align))
                        nasm_error(ERR_NONFATAL, "argument to `align' is not a"
                              " power of two");
                    else if (align > 64)
                        nasm_error(ERR_NONFATAL, "Win32 cannot align sections"
                              " to better than 64-byte boundaries");
                    else {
                        align_and = ~0x00F00000L;
                        align_or  = coff_sectalign_flags(align);
                    }
                }
            }
        }
    }

    for (i = 0; i < coff_nsects; i++)
        if (!strcmp(name, coff_sects[i]->name))
            break;
    if (i == coff_nsects) {
        if (!flags) {
            if (!strcmp(name, ".data"))
                flags = DATA_FLAGS;
            else if (!strcmp(name, ".rdata"))
                flags = RDATA_FLAGS;
            else if (!strcmp(name, ".bss"))
                flags = BSS_FLAGS;
            else if (win64 && !strcmp(name, ".pdata"))
                flags = PDATA_FLAGS;
            else if (win64 && !strcmp(name, ".xdata"))
                flags = XDATA_FLAGS;
            else
                flags = TEXT_FLAGS;
        }
        i = coff_make_section(name, flags);
        if (flags)
            coff_sects[i]->flags = flags;
        coff_sects[i]->flags &= align_and;
        coff_sects[i]->flags |= align_or;
    } else if (pass == 1) {
        /* Check if any flags are specified */
        if (flags) {
            unsigned int align_flags = flags & IMAGE_SCN_ALIGN_MASK;

            /* Warn if non-alignment flags differ */
            if ((flags ^ coff_sects[i]->flags) & ~IMAGE_SCN_ALIGN_MASK) {
                nasm_error(ERR_WARNING, "section attributes ignored on"
                    " redeclaration of section `%s'", name);
            }
            /* Check if alignment might be needed */
            if (align_flags > IMAGE_SCN_ALIGN_1BYTES) {
                unsigned int sect_align_flags = coff_sects[i]->flags & IMAGE_SCN_ALIGN_MASK;

                /* Compute the actual alignment */
                unsigned int align = 1u << ((align_flags - IMAGE_SCN_ALIGN_1BYTES) >> 20);

                /* Update section header as needed */
                if (align_flags > sect_align_flags) {
                    coff_sects[i]->flags = (coff_sects[i]->flags & ~IMAGE_SCN_ALIGN_MASK) | align_flags;
                }
                /* Check if not already aligned */
                if (coff_sects[i]->len % align) {
                    unsigned int padding = (align - coff_sects[i]->len) % align;
                    /* We need to write at most 8095 bytes */
                    char buffer[8095];
                    if (coff_sects[i]->flags & IMAGE_SCN_CNT_CODE) {
                        /* Fill with INT 3 instructions */
                        memset(buffer, 0xCC, padding);
                    } else {
                        memset(buffer, 0x00, padding);
                    }
                    saa_wbytes(coff_sects[i]->data, buffer, padding);
                    coff_sects[i]->len += padding;
                }
            }
        }
    }

    return coff_sects[i]->index;
}

static void coff_deflabel(char *name, int32_t segment, int64_t offset,
                          int is_global, char *special)
{
    int pos = strslen + 4;
    struct coff_Symbol *sym;

    if (special)
        nasm_error(ERR_NONFATAL, "COFF format does not support any"
              " special symbol types");

    if (name[0] == '.' && name[1] == '.' && name[2] != '@') {
        if (strcmp(name,WRT_IMAGEBASE))
            nasm_error(ERR_NONFATAL, "unrecognized special symbol `%s'", name);
        return;
    }

    if (strlen(name) > 8) {
        size_t nlen = strlen(name)+1;
        saa_wbytes(coff_strs, name, nlen);
        strslen += nlen;
    } else
        pos = -1;

    sym = saa_wstruct(coff_syms);

    sym->strpos = pos;
    sym->namlen = strlen(name);
    if (pos == -1)
        strcpy(sym->name, name);
    sym->is_global = !!is_global;
    sym->type = 0;              /* Default to T_NULL (no type) */
    if (segment == NO_SEG)
        sym->section = -1;      /* absolute symbol */
    else {
        int i;
        sym->section = 0;
        for (i = 0; i < coff_nsects; i++)
            if (segment == coff_sects[i]->index) {
                sym->section = i + 1;
                break;
            }
        if (!sym->section)
            sym->is_global = true;
    }
    if (is_global == 2)
        sym->value = offset;
    else
        sym->value = (sym->section == 0 ? 0 : offset);

    /*
     * define the references from external-symbol segment numbers
     * to these symbol records.
     */
    if (sym->section == 0)
        bsym = raa_write(bsym, segment, coff_nsyms);

    if (segment != NO_SEG)
        symval = raa_write(symval, segment, sym->section ? 0 : sym->value);

    coff_nsyms++;
}

static int32_t coff_add_reloc(struct coff_Section *sect, int32_t segment,
                              int16_t type)
{
    struct coff_Reloc *r;

    r = *sect->tail = nasm_malloc(sizeof(struct coff_Reloc));
    sect->tail = &r->next;
    r->next = NULL;

    r->address = sect->len;
    if (segment == NO_SEG) {
        r->symbol = 0, r->symbase = ABS_SYMBOL;
    } else {
        int i;
        r->symbase = REAL_SYMBOLS;
        for (i = 0; i < coff_nsects; i++) {
            if (segment == coff_sects[i]->index) {
                r->symbol = i * 2;
                r->symbase = SECT_SYMBOLS;
                break;
            }
        }
        if (r->symbase == REAL_SYMBOLS)
            r->symbol = raa_read(bsym, segment);
    }
    r->type = type;

    sect->nrelocs++;

    /*
     * Return the fixup for standard COFF common variables.
     */
    if (r->symbase == REAL_SYMBOLS && !(win32 | win64))
        return raa_read(symval, segment);

    return 0;
}

static void coff_out(int32_t segto, const void *data,
                     enum out_type type, uint64_t size,
                     int32_t segment, int32_t wrt)
{
    struct coff_Section *s;
    uint8_t mydata[8], *p;
    int i;

    if (wrt != NO_SEG && !win64) {
        wrt = NO_SEG;           /* continue to do _something_ */
        nasm_error(ERR_NONFATAL, "WRT not supported by COFF output formats");
    }

    s = NULL;
    for (i = 0; i < coff_nsects; i++) {
        if (segto == coff_sects[i]->index) {
            s = coff_sects[i];
            break;
        }
    }
    if (!s) {
        int tempint;            /* ignored */
        if (segto != coff_section_names(".text", 2, &tempint))
            nasm_panic("strange segment conditions in COFF driver");
        else
            s = coff_sects[coff_nsects - 1];
    }

    /* magically default to 'wrt ..imagebase' in .pdata and .xdata */
    if (win64 && wrt == NO_SEG) {
        if (!strcmp(s->name,".pdata") || !strcmp(s->name,".xdata"))
            wrt = imagebase_sect;
    }

    if (!s->data && type != OUT_RESERVE) {
        nasm_error(ERR_WARNING, "attempt to initialize memory in"
              " BSS section `%s': ignored", s->name);
        s->len += realsize(type, size);
        return;
    }

    memset(mydata, 0, sizeof(mydata));

    if (dfmt && dfmt->debug_output) {
        struct coff_DebugInfo dinfo;
        dinfo.segto = segto;
        dinfo.seg = segment;
        dinfo.section = s;

        if (type == OUT_ADDRESS)
            dinfo.size = abs((int)size);
        else
            dinfo.size = realsize(type, size);

        dfmt->debug_output(type, &dinfo);
    }

    if (type == OUT_RESERVE) {
        if (s->data) {
            nasm_error(ERR_WARNING, "uninitialised space declared in"
                  " non-BSS section `%s': zeroing", s->name);
            coff_sect_write(s, NULL, size);
        } else
            s->len += size;
    } else if (type == OUT_RAWDATA) {
        coff_sect_write(s, data, size);
    } else if (type == OUT_ADDRESS) {
        int asize = abs((int)size);
        if (!win64) {
            if (asize != 4 && (segment != NO_SEG || wrt != NO_SEG)) {
                nasm_error(ERR_NONFATAL, "COFF format does not support non-32-bit"
                      " relocations");
            } else {
                int32_t fix = 0;
                if (segment != NO_SEG || wrt != NO_SEG) {
                    if (wrt != NO_SEG) {
                        nasm_error(ERR_NONFATAL, "COFF format does not support"
                              " WRT types");
                    } else if (segment % 2) {
                        nasm_error(ERR_NONFATAL, "COFF format does not support"
                              " segment base references");
                    } else
                        fix = coff_add_reloc(s, segment, IMAGE_REL_I386_DIR32);
                }
                p = mydata;
                WRITELONG(p, *(int64_t *)data + fix);
                coff_sect_write(s, mydata, asize);
            }
        } else {
            int32_t fix = 0;
            p = mydata;
            if (asize == 8) {
                if (wrt == imagebase_sect) {
                    nasm_error(ERR_NONFATAL, "operand size mismatch: 'wrt "
                               WRT_IMAGEBASE "' is a 32-bit operand");
                }
                fix = coff_add_reloc(s, segment, IMAGE_REL_AMD64_ADDR64);
                WRITEDLONG(p, *(int64_t *)data + fix);
                coff_sect_write(s, mydata, asize);
            } else {
                fix = coff_add_reloc(s, segment,
                        wrt == imagebase_sect ?	IMAGE_REL_AMD64_ADDR32NB:
                                                IMAGE_REL_AMD64_ADDR32);
                WRITELONG(p, *(int64_t *)data + fix);
                coff_sect_write(s, mydata, asize);
            }
        }
    } else if (type == OUT_REL2ADR) {
        nasm_error(ERR_NONFATAL, "COFF format does not support 16-bit"
              " relocations");
    } else if (type == OUT_REL4ADR) {
        if (segment == segto && !(win64))  /* Acceptable for RIP-relative */
            nasm_panic("intra-segment OUT_REL4ADR");
        else if (segment == NO_SEG && win32)
            nasm_error(ERR_NONFATAL, "Win32 COFF does not correctly support"
                  " relative references to absolute addresses");
        else {
            int32_t fix = 0;
            if (segment != NO_SEG && segment % 2) {
                nasm_error(ERR_NONFATAL, "COFF format does not support"
                      " segment base references");
            } else
                fix = coff_add_reloc(s, segment,
                        win64 ? IMAGE_REL_AMD64_REL32 : IMAGE_REL_I386_REL32);
            p = mydata;
            if (win32 | win64) {
                WRITELONG(p, *(int64_t *)data + 4 - size + fix);
            } else {
                WRITELONG(p, *(int64_t *)data - (size + s->len) + fix);
            }
            coff_sect_write(s, mydata, 4L);
        }

    }
}

static void coff_sect_write(struct coff_Section *sect,
                            const uint8_t *data, uint32_t len)
{
    saa_wbytes(sect->data, data, len);
    sect->len += len;
}

typedef struct tagString {
    struct tagString *next;
    int len;
    char *String;
} STRING;

#define EXPORT_SECTION_NAME ".drectve"
#define EXPORT_SECTION_FLAGS INFO_FLAGS
/*
 * #define EXPORT_SECTION_NAME ".text"
 * #define EXPORT_SECTION_FLAGS TEXT_FLAGS
 */

static STRING *Exports = NULL;
static struct coff_Section *directive_sec;
static void AddExport(char *name)
{
    STRING *rvp = Exports, *newS;

    newS = (STRING *) nasm_malloc(sizeof(STRING));
    newS->len = strlen(name);
    newS->next = NULL;
    newS->String = (char *)nasm_malloc(newS->len + 1);
    strcpy(newS->String, name);
    if (rvp == NULL) {
        int i;

        for (i = 0; i < coff_nsects; i++) {
            if (!strcmp(EXPORT_SECTION_NAME, coff_sects[i]->name))
                break;
        }

        if (i == coff_nsects)
            i = coff_make_section(EXPORT_SECTION_NAME, EXPORT_SECTION_FLAGS);

        directive_sec = coff_sects[i];
        Exports = newS;
    } else {
        while (rvp->next) {
            if (!strcmp(rvp->String, name))
                return;
            rvp = rvp->next;
        }
        rvp->next = newS;
    }
}

static void BuildExportTable(STRING **rvp)
{
    STRING *p, *t;

    if (!rvp || !*rvp)
        return;

    list_for_each_safe(p, t, *rvp) {
        coff_sect_write(directive_sec, (uint8_t *)"-export:", 8);
        coff_sect_write(directive_sec, (uint8_t *)p->String, p->len);
        coff_sect_write(directive_sec, (uint8_t *)" ", 1);
        nasm_free(p->String);
        nasm_free(p);
    }

    *rvp = NULL;
}

static enum directive_result
coff_directives(enum directive directive, char *value, int pass)
{
    switch (directive) {
    case D_EXPORT:
    {
        char *q, *name;

        if (pass == 2)
            return DIRR_OK;           /* ignore in pass two */
        name = q = value;
        while (*q && !nasm_isspace(*q))
            q++;
        if (nasm_isspace(*q)) {
            *q++ = '\0';
            while (*q && nasm_isspace(*q))
                q++;
        }

        if (!*name) {
            nasm_error(ERR_NONFATAL, "`export' directive requires export name");
            return DIRR_ERROR;
        }
        if (*q) {
            nasm_error(ERR_NONFATAL, "unrecognized export qualifier `%s'", q);
            return DIRR_ERROR;
        }
        AddExport(name);
        return DIRR_OK;
    }
    case D_SAFESEH:
    {
        static int sxseg=-1;
        int i;

        if (!win32) /* Only applicable for -f win32 */
            return 0;

        if (sxseg == -1) {
            for (i = 0; i < coff_nsects; i++)
                if (!strcmp(".sxdata",coff_sects[i]->name))
                    break;
            if (i == coff_nsects)
                sxseg = coff_make_section(".sxdata", IMAGE_SCN_LNK_INFO);
            else
                sxseg = i;
        }
        /*
         * pass0 == 2 is the only time when the full set of symbols are
         * guaranteed to be present; it is the final output pass.
         */
        if (pass0 == 2) {
            uint32_t n;
            saa_rewind(coff_syms);
            for (n = 0; n < coff_nsyms; n++) {
                struct coff_Symbol *sym = saa_rstruct(coff_syms);
                bool equals;

                /*
                 * sym->strpos is biased by 4, because symbol
                 * table is prefixed with table length
                 */
                if (sym->strpos >=4) {
                    char *name = nasm_malloc(sym->namlen+1);
                    saa_fread(coff_strs, sym->strpos-4, name, sym->namlen);
                    name[sym->namlen] = '\0';
                    equals = !strcmp(value,name);
                    nasm_free(name);
                } else {
                    equals = !strcmp(value,sym->name);
                }

                if (equals) {
                    /*
                     * this value arithmetics effectively reflects
                     * initsym in coff_write(): 2 for file, 1 for
                     * .absolute and two per each section
                     */
                    unsigned char value[4],*p=value;
                    WRITELONG(p,n + 2 + 1 + coff_nsects*2);
                    coff_sect_write(coff_sects[sxseg],value,4);
                    sym->type = 0x20;
                    break;
                }
            }
            if (n == coff_nsyms) {
                nasm_error(ERR_NONFATAL,
                           "`safeseh' directive requires valid symbol");
                return DIRR_ERROR;
            }
        }
        return DIRR_OK;
    }
    default:
        return DIRR_UNKNOWN;
    }
}

/* handle relocations storm, valid for win32/64 only */
static inline void coff_adjust_relocs(struct coff_Section *s)
{
    if (s->nrelocs < IMAGE_SCN_MAX_RELOC)
        return;
#ifdef OF_COFF
    else
    {
        if (ofmt == &of_coff)
            nasm_fatal("Too many relocations (%d) for section `%s'",
                       s->nrelocs, s->name);
    }
#endif

    s->flags |= IMAGE_SCN_LNK_NRELOC_OVFL;
    s->nrelocs++;
}

static void coff_write(void)
{
    int32_t pos, sympos, vsize;
    int i;

    /* fill in the .drectve section with -export's */
    BuildExportTable(&Exports);

    if (win32) {
        /* add default value for @feat.00, this allows to 'link /safeseh' */
        uint32_t n;

        saa_rewind(coff_syms);
        for (n = 0; n < coff_nsyms; n++) {
            struct coff_Symbol *sym = saa_rstruct(coff_syms);
            if (sym->strpos == -1 && !strcmp("@feat.00",sym->name))
                break;
        }
        if (n == coff_nsyms)
            coff_deflabel("@feat.00", NO_SEG, 1, 0, NULL);
    }

    /*
     * Work out how big the file will get.
     * Calculate the start of the `real' symbols at the same time.
     * Check for massive relocations.
     */
    pos = 0x14 + 0x28 * coff_nsects;
    initsym = 3;                /* two for the file, one absolute */
    for (i = 0; i < coff_nsects; i++) {
        if (coff_sects[i]->data) {
            coff_adjust_relocs(coff_sects[i]);
            coff_sects[i]->pos = pos;
            pos += coff_sects[i]->len;
            coff_sects[i]->relpos = pos;
            pos += 10 * coff_sects[i]->nrelocs;
        } else
            coff_sects[i]->pos = coff_sects[i]->relpos = 0L;
        initsym += 2;           /* two for each section */
    }
    sympos = pos;

    /*
     * Output the COFF header.
     */
    if (win64)
        i = IMAGE_FILE_MACHINE_AMD64;
    else
        i = IMAGE_FILE_MACHINE_I386;
    fwriteint16_t(i,                    ofile); /* machine type */
    fwriteint16_t(coff_nsects,               ofile); /* number of sections */
    // Chromium patch: Builds should be deterministic and not embed timestamps.
    fwriteint32_t(0,                    ofile); /* time stamp */
    fwriteint32_t(sympos,               ofile);
    fwriteint32_t(coff_nsyms + initsym,      ofile);
    fwriteint16_t(0,                    ofile); /* no optional header */
    /* Flags: 32-bit, no line numbers. Win32 doesn't even bother with them. */
    fwriteint16_t((win32 | win64) ? 0 : 0x104, ofile);

    /*
     * Output the section headers.
     */
    vsize = 0L;
    for (i = 0; i < coff_nsects; i++) {
        coff_section_header(coff_sects[i]->name, coff_sects[i]->namepos, vsize, coff_sects[i]->len,
                            coff_sects[i]->pos, coff_sects[i]->relpos,
                            coff_sects[i]->nrelocs, coff_sects[i]->flags);
        vsize += coff_sects[i]->len;
    }

    /*
     * Output the sections and their relocations.
     */
    for (i = 0; i < coff_nsects; i++)
        if (coff_sects[i]->data) {
            saa_fpwrite(coff_sects[i]->data, ofile);
            coff_write_relocs(coff_sects[i]);
        }

    /*
     * Output the symbol and string tables.
     */
    coff_write_symbols();
    fwriteint32_t(strslen + 4, ofile);     /* length includes length count */
    saa_fpwrite(coff_strs, ofile);
}

static void coff_section_header(char *name, int32_t namepos, int32_t vsize,
                                int32_t datalen, int32_t datapos,
                                int32_t relpos, int nrelocs, int32_t flags)
{
    char padname[8];

    (void)vsize;

    if (namepos == -1) {
        strncpy(padname, name, 8);
        nasm_write(padname, 8, ofile);
    } else {
        /*
         * If name is longer than 8 bytes, write '/' followed
         * by offset into the strings table represented as
         * decimal number.
         */
        namepos = namepos % 100000000;
        padname[0] = '/';
        padname[1] = '0' + (namepos / 1000000);
        namepos = namepos % 1000000;
        padname[2] = '0' + (namepos / 100000);
        namepos = namepos % 100000;
        padname[3] = '0' + (namepos / 10000);
        namepos = namepos % 10000;
        padname[4] = '0' + (namepos / 1000);
        namepos = namepos % 1000;
        padname[5] = '0' + (namepos / 100);
        namepos = namepos % 100;
        padname[6] = '0' + (namepos / 10);
        namepos = namepos % 10;
        padname[7] = '0' + (namepos);
        nasm_write(padname, 8, ofile);
    }

    fwriteint32_t(0,            ofile); /* Virtual size field - set to 0 or vsize */
    fwriteint32_t(0L,           ofile); /* RVA/offset - we ignore */
    fwriteint32_t(datalen,      ofile);
    fwriteint32_t(datapos,      ofile);
    fwriteint32_t(relpos,       ofile);
    fwriteint32_t(0L,           ofile); /* no line numbers - we don't do 'em */

    /*
     * a special case -- if there are too many relocs
     * we have to put IMAGE_SCN_MAX_RELOC here and write
     * the real relocs number into VirtualAddress of first
     * relocation
     */
    if (flags & IMAGE_SCN_LNK_NRELOC_OVFL)
        fwriteint16_t(IMAGE_SCN_MAX_RELOC, ofile);
    else
        fwriteint16_t(nrelocs,  ofile);

    fwriteint16_t(0,            ofile); /* again, no line numbers */
    fwriteint32_t(flags,        ofile);
}

static void coff_write_relocs(struct coff_Section *s)
{
    struct coff_Reloc *r;

    /* a real number of relocations if needed */
    if (s->flags & IMAGE_SCN_LNK_NRELOC_OVFL) {
        fwriteint32_t(s->nrelocs, ofile);
        fwriteint32_t(0, ofile);
        fwriteint16_t(0, ofile);
    }

    for (r = s->head; r; r = r->next) {
        fwriteint32_t(r->address, ofile);
        fwriteint32_t(r->symbol + (r->symbase == REAL_SYMBOLS ? initsym :
                                   r->symbase == ABS_SYMBOL   ? initsym - 1 :
                                   r->symbase == SECT_SYMBOLS ? 2 : 0),
                      ofile);
        fwriteint16_t(r->type, ofile);
    }
}

static void coff_symbol(char *name, int32_t strpos, int32_t value,
                        int section, int type, int storageclass, int aux)
{
    char padname[8];

    if (name) {
        strncpy(padname, name, 8);
        nasm_write(padname, 8, ofile);
    } else {
        fwriteint32_t(0, ofile);
        fwriteint32_t(strpos, ofile);
    }

    fwriteint32_t(value,        ofile);
    fwriteint16_t(section,      ofile);
    fwriteint16_t(type,         ofile);

    fputc(storageclass, ofile);
    fputc(aux, ofile);
}

static void coff_write_symbols(void)
{
    char filename[18];
    uint32_t i;

    /*
     * The `.file' record, and the file name auxiliary record.
     */
    coff_symbol(".file", 0L, 0L, -2, 0, 0x67, 1);
    strncpy(filename, inname, 18);
    nasm_write(filename, 18, ofile);

    /*
     * The section records, with their auxiliaries.
     */
    memset(filename, 0, 18);    /* useful zeroed buffer */

    for (i = 0; i < (uint32_t) coff_nsects; i++) {
        coff_symbol(coff_sects[i]->name, 0L, 0L, i + 1, 0, 3, 1);
        fwriteint32_t(coff_sects[i]->len,    ofile);
        fwriteint16_t(coff_sects[i]->nrelocs,ofile);
        nasm_write(filename, 12, ofile);
    }

    /*
     * The absolute symbol, for relative-to-absolute relocations.
     */
    coff_symbol(".absolut", 0L, 0L, -1, 0, 3, 0);

    /*
     * The real symbols.
     */
    saa_rewind(coff_syms);
    for (i = 0; i < coff_nsyms; i++) {
        struct coff_Symbol *sym = saa_rstruct(coff_syms);
        coff_symbol(sym->strpos == -1 ? sym->name : NULL,
                    sym->strpos, sym->value, sym->section,
                    sym->type, sym->is_global ? 2 : 3, 0);
    }
}

static void coff_sectalign(int32_t seg, unsigned int value)
{
    struct coff_Section *s = NULL;
    uint32_t align;
    int i;

    for (i = 0; i < coff_nsects; i++) {
        if (coff_sects[i]->index == seg) {
            s = coff_sects[i];
            break;
        }
    }

    if (!s || !is_power2(value))
        return;

    /* DOS has limitation on 64 bytes */
    if (!(win32 | win64) && value > 64)
        return;

    align = (s->flags & IMAGE_SCN_ALIGN_MASK);
    value = coff_sectalign_flags(value);
    if (value > align)
        s->flags = (s->flags & ~IMAGE_SCN_ALIGN_MASK) | value;
}

extern macros_t coff_stdmac[];

#endif /* defined(OF_COFF) || defined(OF_WIN32) */

#ifdef OF_COFF

const struct ofmt of_coff = {
    "COFF (i386) object files (e.g. DJGPP for DOS)",
    "coff",
    ".o",
    0,
    32,
    null_debug_arr,
    &null_debug_form,
    coff_stdmac,
    coff_std_init,
    null_reset,
    nasm_do_legacy_output,
    coff_out,
    coff_deflabel,
    coff_section_names,
    NULL,
    coff_sectalign,
    null_segbase,
    coff_directives,
    coff_cleanup,
    NULL                        /* pragma list */
};

#endif

extern const struct dfmt df_cv8;

#ifdef OF_WIN32

static const struct dfmt * const win32_debug_arr[2] = { &df_cv8, NULL };

const struct ofmt of_win32 = {
    "Microsoft Win32 (i386) object files",
    "win32",
    ".obj",
    0,
    32,
    win32_debug_arr,
    &df_cv8,
    coff_stdmac,
    coff_win32_init,
    null_reset,
    nasm_do_legacy_output,
    coff_out,
    coff_deflabel,
    coff_section_names,
    NULL,
    coff_sectalign,
    null_segbase,
    coff_directives,
    coff_cleanup,
    NULL                        /* pragma list */
};

#endif

#ifdef OF_WIN64

static const struct dfmt * const win64_debug_arr[2] = { &df_cv8, NULL };

const struct ofmt of_win64 = {
    "Microsoft Win64 (x86-64) object files",
    "win64",
    ".obj",
    0,
    64,
    win64_debug_arr,
    &df_cv8,
    coff_stdmac,
    coff_win64_init,
    null_reset,
    nasm_do_legacy_output,
    coff_out,
    coff_deflabel,
    coff_section_names,
    NULL,
    coff_sectalign,
    null_segbase,
    coff_directives,
    coff_cleanup,
    NULL                        /* pragma list */
};

#endif
