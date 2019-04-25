/* ----------------------------------------------------------------------- *
 *   
 *   Copyright 1996-2013 The NASM Authors - All Rights Reserved
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
 * outaout.c	output routines for the Netwide Assembler to produce
 *		Linux a.out object files
 */

#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "nasm.h"
#include "nasmlib.h"
#include "error.h"
#include "saa.h"
#include "raa.h"
#include "stdscan.h"
#include "eval.h"
#include "outform.h"
#include "outlib.h"

#if defined OF_AOUT || defined OF_AOUTB

#define RELTYPE_ABSOLUTE 0x00
#define RELTYPE_RELATIVE 0x01
#define RELTYPE_GOTPC    0x01   /* no explicit GOTPC in a.out */
#define RELTYPE_GOTOFF   0x10
#define RELTYPE_GOT      0x10   /* distinct from GOTOFF bcos sym not sect */
#define RELTYPE_PLT      0x21
#define RELTYPE_SYMFLAG  0x08

struct Reloc {
    struct Reloc *next;
    int32_t address;               /* relative to _start_ of section */
    int32_t symbol;                /* symbol number or -ve section id */
    int bytes;                  /* 2 or 4 */
    int reltype;                /* see above */
};

struct Symbol {
    int32_t strpos;                /* string table position of name */
    int type;                   /* symbol type - see flags below */
    int32_t value;                 /* address, or COMMON variable size */
    int32_t size;                  /* size for data or function exports */
    int32_t segment;               /* back-reference used by gsym_reloc */
    struct Symbol *next;        /* list of globals in each section */
    struct Symbol *nextfwd;     /* list of unresolved-size symbols */
    char *name;                 /* for unresolved-size symbols */
    int32_t symnum;                /* index into symbol table */
};

/*
 * Section IDs - used in Reloc.symbol when negative, and in
 * Symbol.type when positive.
 */
#define SECT_ABS 2              /* absolute value */
#define SECT_TEXT 4             /* text section */
#define SECT_DATA 6             /* data section */
#define SECT_BSS 8              /* bss section */
#define SECT_MASK 0xE           /* mask out any of the above */

/*
 * More flags used in Symbol.type.
 */
#define SYM_GLOBAL 1            /* it's a global symbol */
#define SYM_DATA 0x100          /* used for shared libs */
#define SYM_FUNCTION 0x200      /* used for shared libs */
#define SYM_WITH_SIZE 0x4000    /* not output; internal only */

/*
 * Bit more explanation of symbol types: SECT_xxx denotes a local
 * symbol. SECT_xxx|SYM_GLOBAL denotes a global symbol, defined in
 * this module. Just SYM_GLOBAL, with zero value, denotes an
 * external symbol referenced in this module. And just SYM_GLOBAL,
 * but with a non-zero value, declares a C `common' variable, of
 * size `value'.
 */

struct Section {
    struct SAA *data;
    uint32_t len, size, nrelocs;
    int32_t index;
    struct Reloc *head, **tail;
    struct Symbol *gsyms, *asym;
};

static struct Section stext, sdata, sbss;

static struct SAA *syms;
static uint32_t nsyms;

static struct RAA *bsym;

static struct SAA *strs;
static uint32_t strslen;

static struct Symbol *fwds;

static int bsd;
static int is_pic;

static void aout_write(void);
static void aout_write_relocs(struct Reloc *);
static void aout_write_syms(void);
static void aout_sect_write(struct Section *, const uint8_t *,
                            uint32_t);
static void aout_pad_sections(void);
static void aout_fixup_relocs(struct Section *);

/*
 * Special section numbers which are used to define special
 * symbols, which can be used with WRT to provide PIC relocation
 * types.
 */
static int32_t aout_gotpc_sect, aout_gotoff_sect;
static int32_t aout_got_sect, aout_plt_sect;
static int32_t aout_sym_sect;

static void aoutg_init(void)
{
    stext.data = saa_init(1L);
    stext.head = NULL;
    stext.tail = &stext.head;
    sdata.data = saa_init(1L);
    sdata.head = NULL;
    sdata.tail = &sdata.head;
    stext.len = stext.size = sdata.len = sdata.size = sbss.len = 0;
    stext.nrelocs = sdata.nrelocs = 0;
    stext.gsyms = sdata.gsyms = sbss.gsyms = NULL;
    stext.index = seg_alloc();
    sdata.index = seg_alloc();
    sbss.index = seg_alloc();
    stext.asym = sdata.asym = sbss.asym = NULL;
    syms = saa_init((int32_t)sizeof(struct Symbol));
    nsyms = 0;
    bsym = raa_init();
    strs = saa_init(1L);
    strslen = 0;
    fwds = NULL;
}

#ifdef OF_AOUT

static void aout_init(void)
{
    bsd = false;
    aoutg_init();

    aout_gotpc_sect = aout_gotoff_sect = aout_got_sect =
        aout_plt_sect = aout_sym_sect = NO_SEG;
}

#endif

#ifdef OF_AOUTB

extern const struct ofmt of_aoutb;

static void aoutb_init(void)
{
    bsd = true;
    aoutg_init();

    is_pic = 0x00;              /* may become 0x40 */

    aout_gotpc_sect = seg_alloc();
    backend_label("..gotpc", aout_gotpc_sect + 1, 0L);
    aout_gotoff_sect = seg_alloc();
    backend_label("..gotoff", aout_gotoff_sect + 1, 0L);
    aout_got_sect = seg_alloc();
    backend_label("..got", aout_got_sect + 1, 0L);
    aout_plt_sect = seg_alloc();
    backend_label("..plt", aout_plt_sect + 1, 0L);
    aout_sym_sect = seg_alloc();
    backend_label("..sym", aout_sym_sect + 1, 0L);
}

#endif

static void aout_cleanup(void)
{
    struct Reloc *r;

    aout_pad_sections();
    aout_fixup_relocs(&stext);
    aout_fixup_relocs(&sdata);
    aout_write();
    saa_free(stext.data);
    while (stext.head) {
        r = stext.head;
        stext.head = stext.head->next;
        nasm_free(r);
    }
    saa_free(sdata.data);
    while (sdata.head) {
        r = sdata.head;
        sdata.head = sdata.head->next;
        nasm_free(r);
    }
    saa_free(syms);
    raa_free(bsym);
    saa_free(strs);
}

static int32_t aout_section_names(char *name, int pass, int *bits)
{

    (void)pass;

    /*
     * Default to 32 bits.
     */
    if (!name) {
        *bits = 32;
        return stext.index;
    }

    if (!strcmp(name, ".text"))
        return stext.index;
    else if (!strcmp(name, ".data"))
        return sdata.index;
    else if (!strcmp(name, ".bss"))
        return sbss.index;
    else
        return NO_SEG;
}

static void aout_deflabel(char *name, int32_t segment, int64_t offset,
                          int is_global, char *special)
{
    int pos = strslen + 4;
    struct Symbol *sym;
    int special_used = false;

    if (name[0] == '.' && name[1] == '.' && name[2] != '@') {
        /*
         * This is a NASM special symbol. We never allow it into
         * the a.out symbol table, even if it's a valid one. If it
         * _isn't_ a valid one, we should barf immediately.
         */
        if (strcmp(name, "..gotpc") && strcmp(name, "..gotoff") &&
            strcmp(name, "..got") && strcmp(name, "..plt") &&
            strcmp(name, "..sym"))
            nasm_error(ERR_NONFATAL, "unrecognised special symbol `%s'", name);
        return;
    }

    if (is_global == 3) {
        struct Symbol **s;
        /*
         * Fix up a forward-reference symbol size from the first
         * pass.
         */
        for (s = &fwds; *s; s = &(*s)->nextfwd)
            if (!strcmp((*s)->name, name)) {
                struct tokenval tokval;
                expr *e;
                char *p = special;

                p = nasm_skip_spaces(nasm_skip_word(p));
                stdscan_reset();
                stdscan_set(p);
                tokval.t_type = TOKEN_INVALID;
                e = evaluate(stdscan, NULL, &tokval, NULL, 1, NULL);
                if (e) {
                    if (!is_simple(e))
                        nasm_error(ERR_NONFATAL, "cannot use relocatable"
                              " expression as symbol size");
                    else
                        (*s)->size = reloc_value(e);
                }

                /*
                 * Remove it from the list of unresolved sizes.
                 */
                nasm_free((*s)->name);
                *s = (*s)->nextfwd;
                return;
            }
        return;                 /* it wasn't an important one */
    }

    saa_wbytes(strs, name, (int32_t)(1 + strlen(name)));
    strslen += 1 + strlen(name);

    sym = saa_wstruct(syms);

    sym->strpos = pos;
    sym->type = is_global ? SYM_GLOBAL : 0;
    sym->segment = segment;
    if (segment == NO_SEG)
        sym->type |= SECT_ABS;
    else if (segment == stext.index) {
        sym->type |= SECT_TEXT;
        if (is_global) {
            sym->next = stext.gsyms;
            stext.gsyms = sym;
        } else if (!stext.asym)
            stext.asym = sym;
    } else if (segment == sdata.index) {
        sym->type |= SECT_DATA;
        if (is_global) {
            sym->next = sdata.gsyms;
            sdata.gsyms = sym;
        } else if (!sdata.asym)
            sdata.asym = sym;
    } else if (segment == sbss.index) {
        sym->type |= SECT_BSS;
        if (is_global) {
            sym->next = sbss.gsyms;
            sbss.gsyms = sym;
        } else if (!sbss.asym)
            sbss.asym = sym;
    } else
        sym->type = SYM_GLOBAL;
    if (is_global == 2)
        sym->value = offset;
    else
        sym->value = (sym->type == SYM_GLOBAL ? 0 : offset);

    if (is_global && sym->type != SYM_GLOBAL) {
        /*
         * Global symbol exported _from_ this module. We must check
         * the special text for type information.
         */

        if (special) {
            int n = strcspn(special, " ");

            if (!nasm_strnicmp(special, "function", n))
                sym->type |= SYM_FUNCTION;
            else if (!nasm_strnicmp(special, "data", n) ||
                     !nasm_strnicmp(special, "object", n))
                sym->type |= SYM_DATA;
            else
                nasm_error(ERR_NONFATAL, "unrecognised symbol type `%.*s'",
                      n, special);
            if (special[n]) {
                struct tokenval tokval;
                expr *e;
                int fwd = false;
                char *saveme = stdscan_get();

                if (!bsd) {
                    nasm_error(ERR_NONFATAL, "Linux a.out does not support"
                          " symbol size information");
                } else {
                    while (special[n] && nasm_isspace(special[n]))
                        n++;
                    /*
                     * We have a size expression; attempt to
                     * evaluate it.
                     */
                    sym->type |= SYM_WITH_SIZE;
                    stdscan_reset();
                    stdscan_set(special + n);
                    tokval.t_type = TOKEN_INVALID;
                    e = evaluate(stdscan, NULL, &tokval, &fwd, 0, NULL);
                    if (fwd) {
                        sym->nextfwd = fwds;
                        fwds = sym;
                        sym->name = nasm_strdup(name);
                    } else if (e) {
                        if (!is_simple(e))
                            nasm_error(ERR_NONFATAL, "cannot use relocatable"
                                  " expression as symbol size");
                        else
                            sym->size = reloc_value(e);
                    }
                }
                stdscan_set(saveme);
            }
            special_used = true;
        }
    }

    /*
     * define the references from external-symbol segment numbers
     * to these symbol records.
     */
    if (segment != NO_SEG && segment != stext.index &&
        segment != sdata.index && segment != sbss.index)
        bsym = raa_write(bsym, segment, nsyms);
    sym->symnum = nsyms;

    nsyms++;
    if (sym->type & SYM_WITH_SIZE)
        nsyms++;                /* and another for the size */

    if (special && !special_used)
        nasm_error(ERR_NONFATAL, "no special symbol features supported here");
}

static void aout_add_reloc(struct Section *sect, int32_t segment,
                           int reltype, int bytes)
{
    struct Reloc *r;

    r = *sect->tail = nasm_malloc(sizeof(struct Reloc));
    sect->tail = &r->next;
    r->next = NULL;

    r->address = sect->len;
    r->symbol = (segment == NO_SEG ? -SECT_ABS :
                 segment == stext.index ? -SECT_TEXT :
                 segment == sdata.index ? -SECT_DATA :
                 segment == sbss.index ? -SECT_BSS :
                 raa_read(bsym, segment));
    r->reltype = reltype;
    if (r->symbol >= 0)
        r->reltype |= RELTYPE_SYMFLAG;
    r->bytes = bytes;

    sect->nrelocs++;
}

/*
 * This routine deals with ..got and ..sym relocations: the more
 * complicated kinds. In shared-library writing, some relocations
 * with respect to global symbols must refer to the precise symbol
 * rather than referring to an offset from the base of the section
 * _containing_ the symbol. Such relocations call to this routine,
 * which searches the symbol list for the symbol in question.
 *
 * RELTYPE_GOT references require the _exact_ symbol address to be
 * used; RELTYPE_ABSOLUTE references can be at an offset from the
 * symbol. The boolean argument `exact' tells us this.
 *
 * Return value is the adjusted value of `addr', having become an
 * offset from the symbol rather than the section. Should always be
 * zero when returning from an exact call.
 *
 * Limitation: if you define two symbols at the same place,
 * confusion will occur.
 *
 * Inefficiency: we search, currently, using a linked list which
 * isn't even necessarily sorted.
 */
static int32_t aout_add_gsym_reloc(struct Section *sect,
                                int32_t segment, int32_t offset,
                                int type, int bytes, int exact)
{
    struct Symbol *sym, *sm, *shead;
    struct Reloc *r;

    /*
     * First look up the segment to find whether it's text, data,
     * bss or an external symbol.
     */
    shead = NULL;
    if (segment == stext.index)
        shead = stext.gsyms;
    else if (segment == sdata.index)
        shead = sdata.gsyms;
    else if (segment == sbss.index)
        shead = sbss.gsyms;
    if (!shead) {
        if (exact && offset != 0)
            nasm_error(ERR_NONFATAL, "unable to find a suitable global symbol"
                  " for this reference");
        else
            aout_add_reloc(sect, segment, type, bytes);
        return offset;
    }

    if (exact) {
        /*
         * Find a symbol pointing _exactly_ at this one.
         */
        list_for_each(sym, shead)
            if (sym->value == offset)
                break;
    } else {
        /*
         * Find the nearest symbol below this one.
         */
        sym = NULL;
        list_for_each(sm, shead)
            if (sm->value <= offset && (!sym || sm->value > sym->value))
                sym = sm;
    }
    if (!sym && exact) {
        nasm_error(ERR_NONFATAL, "unable to find a suitable global symbol"
              " for this reference");
        return 0;
    }

    r = *sect->tail = nasm_malloc(sizeof(struct Reloc));
    sect->tail = &r->next;
    r->next = NULL;

    r->address = sect->len;
    r->symbol = sym->symnum;
    r->reltype = type | RELTYPE_SYMFLAG;
    r->bytes = bytes;

    sect->nrelocs++;

    return offset - sym->value;
}

/*
 * This routine deals with ..gotoff relocations. These _must_ refer
 * to a symbol, due to a perversity of *BSD's PIC implementation,
 * and it must be a non-global one as well; so we store `asym', the
 * first nonglobal symbol defined in each section, and always work
 * from that. Relocation type is always RELTYPE_GOTOFF.
 *
 * Return value is the adjusted value of `addr', having become an
 * offset from the `asym' symbol rather than the section.
 */
static int32_t aout_add_gotoff_reloc(struct Section *sect, int32_t segment,
                                  int32_t offset, int bytes)
{
    struct Reloc *r;
    struct Symbol *asym;

    /*
     * First look up the segment to find whether it's text, data,
     * bss or an external symbol.
     */
    asym = NULL;
    if (segment == stext.index)
        asym = stext.asym;
    else if (segment == sdata.index)
        asym = sdata.asym;
    else if (segment == sbss.index)
        asym = sbss.asym;
    if (!asym)
        nasm_error(ERR_NONFATAL, "`..gotoff' relocations require a non-global"
              " symbol in the section");

    r = *sect->tail = nasm_malloc(sizeof(struct Reloc));
    sect->tail = &r->next;
    r->next = NULL;

    r->address = sect->len;
    r->symbol = asym->symnum;
    r->reltype = RELTYPE_GOTOFF;
    r->bytes = bytes;

    sect->nrelocs++;

    return offset - asym->value;
}

static void aout_out(int32_t segto, const void *data,
		     enum out_type type, uint64_t size,
                     int32_t segment, int32_t wrt)
{
    struct Section *s;
    int32_t addr;
    uint8_t mydata[4], *p;

    if (segto == stext.index)
        s = &stext;
    else if (segto == sdata.index)
        s = &sdata;
    else if (segto == sbss.index)
        s = NULL;
    else {
        nasm_error(ERR_WARNING, "attempt to assemble code in"
              " segment %d: defaulting to `.text'", segto);
        s = &stext;
    }

    if (!s && type != OUT_RESERVE) {
        nasm_error(ERR_WARNING, "attempt to initialize memory in the"
              " BSS section: ignored");
        sbss.len += realsize(type, size);
        return;
    }

    memset(mydata, 0, sizeof(mydata));

    if (type == OUT_RESERVE) {
        if (s) {
            nasm_error(ERR_WARNING, "uninitialized space declared in"
                  " %s section: zeroing",
                  (segto == stext.index ? "code" : "data"));
            aout_sect_write(s, NULL, size);
        } else
            sbss.len += size;
    } else if (type == OUT_RAWDATA) {
        aout_sect_write(s, data, size);
    } else if (type == OUT_ADDRESS) {
        int asize = abs((int)size);
        addr = *(int64_t *)data;
        if (segment != NO_SEG) {
            if (segment % 2) {
                nasm_error(ERR_NONFATAL, "a.out format does not support"
                      " segment base references");
            } else {
                if (wrt == NO_SEG) {
                    aout_add_reloc(s, segment, RELTYPE_ABSOLUTE, asize);
                } else if (!bsd) {
                    nasm_error(ERR_NONFATAL,
                          "Linux a.out format does not support"
                          " any use of WRT");
                    wrt = NO_SEG;       /* we can at least _try_ to continue */
                } else if (wrt == aout_gotpc_sect + 1) {
                    is_pic = 0x40;
                    aout_add_reloc(s, segment, RELTYPE_GOTPC, asize);
                } else if (wrt == aout_gotoff_sect + 1) {
                    is_pic = 0x40;
                    addr = aout_add_gotoff_reloc(s, segment, addr, asize);
                } else if (wrt == aout_got_sect + 1) {
                    is_pic = 0x40;
                    addr = aout_add_gsym_reloc(s, segment, addr, RELTYPE_GOT,
                                               asize, true);
                } else if (wrt == aout_sym_sect + 1) {
                    addr = aout_add_gsym_reloc(s, segment, addr,
                                               RELTYPE_ABSOLUTE, asize,
                                               false);
                } else if (wrt == aout_plt_sect + 1) {
                    is_pic = 0x40;
                    nasm_error(ERR_NONFATAL,
                          "a.out format cannot produce non-PC-"
                          "relative PLT references");
                } else {
                    nasm_error(ERR_NONFATAL,
                          "a.out format does not support this"
                          " use of WRT");
                    wrt = NO_SEG;       /* we can at least _try_ to continue */
                }
            }
        }
        p = mydata;
        if (asize == 2)
            WRITESHORT(p, addr);
        else
            WRITELONG(p, addr);
        aout_sect_write(s, mydata, asize);
    } else if (type == OUT_REL2ADR) {
        if (segment != NO_SEG && segment % 2) {
            nasm_error(ERR_NONFATAL, "a.out format does not support"
                  " segment base references");
        } else {
            if (wrt == NO_SEG) {
                aout_add_reloc(s, segment, RELTYPE_RELATIVE, 2);
            } else if (!bsd) {
                nasm_error(ERR_NONFATAL, "Linux a.out format does not support"
                      " any use of WRT");
                wrt = NO_SEG;   /* we can at least _try_ to continue */
            } else if (wrt == aout_plt_sect + 1) {
                is_pic = 0x40;
                aout_add_reloc(s, segment, RELTYPE_PLT, 2);
            } else if (wrt == aout_gotpc_sect + 1 ||
                       wrt == aout_gotoff_sect + 1 ||
                       wrt == aout_got_sect + 1) {
                nasm_error(ERR_NONFATAL, "a.out format cannot produce PC-"
                      "relative GOT references");
            } else {
                nasm_error(ERR_NONFATAL, "a.out format does not support this"
                      " use of WRT");
                wrt = NO_SEG;   /* we can at least _try_ to continue */
            }
        }
        p = mydata;
        WRITESHORT(p, *(int64_t *)data - (size + s->len));
        aout_sect_write(s, mydata, 2L);
    } else if (type == OUT_REL4ADR) {
        if (segment != NO_SEG && segment % 2) {
            nasm_error(ERR_NONFATAL, "a.out format does not support"
                  " segment base references");
        } else {
            if (wrt == NO_SEG) {
                aout_add_reloc(s, segment, RELTYPE_RELATIVE, 4);
            } else if (!bsd) {
                nasm_error(ERR_NONFATAL, "Linux a.out format does not support"
                      " any use of WRT");
                wrt = NO_SEG;   /* we can at least _try_ to continue */
            } else if (wrt == aout_plt_sect + 1) {
                is_pic = 0x40;
                aout_add_reloc(s, segment, RELTYPE_PLT, 4);
            } else if (wrt == aout_gotpc_sect + 1 ||
                       wrt == aout_gotoff_sect + 1 ||
                       wrt == aout_got_sect + 1) {
                nasm_error(ERR_NONFATAL, "a.out format cannot produce PC-"
                      "relative GOT references");
            } else {
                nasm_error(ERR_NONFATAL, "a.out format does not support this"
                      " use of WRT");
                wrt = NO_SEG;   /* we can at least _try_ to continue */
            }
        }
        p = mydata;
        WRITELONG(p, *(int64_t *)data - (size + s->len));
        aout_sect_write(s, mydata, 4L);
    }
}

static void aout_pad_sections(void)
{
    static uint8_t pad[] = { 0x90, 0x90, 0x90, 0x90 };
    /*
     * Pad each of the text and data sections with NOPs until their
     * length is a multiple of four. (NOP == 0x90.) Also increase
     * the length of the BSS section similarly.
     */
    aout_sect_write(&stext, pad, (-(int32_t)stext.len) & 3);
    aout_sect_write(&sdata, pad, (-(int32_t)sdata.len) & 3);
    sbss.len = ALIGN(sbss.len, 4);
}

/*
 * a.out files have the curious property that all references to
 * things in the data or bss sections are done by addresses which
 * are actually relative to the start of the _text_ section, in the
 * _file_. (No relation to what happens after linking. No idea why
 * this should be so. It's very strange.) So we have to go through
 * the relocation table, _after_ the final size of each section is
 * known, and fix up the relocations pointed to.
 */
static void aout_fixup_relocs(struct Section *sect)
{
    struct Reloc *r;

    saa_rewind(sect->data);
    list_for_each(r, sect->head) {
        uint8_t *p, *q, blk[4];
        int32_t l;

        saa_fread(sect->data, r->address, blk, (int32_t)r->bytes);
        p = q = blk;
        l = *p++;
        if (r->bytes > 1) {
            l += ((int32_t)*p++) << 8;
            if (r->bytes == 4) {
                l += ((int32_t)*p++) << 16;
                l += ((int32_t)*p++) << 24;
            }
        }
        if (r->symbol == -SECT_DATA)
            l += stext.len;
        else if (r->symbol == -SECT_BSS)
            l += stext.len + sdata.len;
        if (r->bytes == 4)
            WRITELONG(q, l);
        else if (r->bytes == 2)
            WRITESHORT(q, l);
        else
            *q++ = l & 0xFF;
        saa_fwrite(sect->data, r->address, blk, (int32_t)r->bytes);
    }
}

static void aout_write(void)
{
    /*
     * Emit the a.out header.
     */
    /* OMAGIC, M_386 or MID_I386, no flags */
    fwriteint32_t(bsd ? 0x07018600 | is_pic : 0x640107L, ofile);
    fwriteint32_t(stext.len, ofile);
    fwriteint32_t(sdata.len, ofile);
    fwriteint32_t(sbss.len, ofile);
    fwriteint32_t(nsyms * 12, ofile);     /* length of symbol table */
    fwriteint32_t(0L, ofile);     /* object files have no entry point */
    fwriteint32_t(stext.nrelocs * 8, ofile);      /* size of text relocs */
    fwriteint32_t(sdata.nrelocs * 8, ofile);      /* size of data relocs */

    /*
     * Write out the code section and the data section.
     */
    saa_fpwrite(stext.data, ofile);
    saa_fpwrite(sdata.data, ofile);

    /*
     * Write out the relocations.
     */
    aout_write_relocs(stext.head);
    aout_write_relocs(sdata.head);

    /*
     * Write the symbol table.
     */
    aout_write_syms();

    /*
     * And the string table.
     */
    fwriteint32_t(strslen + 4, ofile);    /* length includes length count */
    saa_fpwrite(strs, ofile);
}

static void aout_write_relocs(struct Reloc *r)
{
    list_for_each(r, r) {
        uint32_t word2;

        fwriteint32_t(r->address, ofile);

        if (r->symbol >= 0)
            word2 = r->symbol;
        else
            word2 = -r->symbol;
        word2 |= r->reltype << 24;
        word2 |= (r->bytes == 1 ? 0 :
                  r->bytes == 2 ? 0x2000000L : 0x4000000L);
        fwriteint32_t(word2, ofile);
    }
}

static void aout_write_syms(void)
{
    uint32_t i;

    saa_rewind(syms);
    for (i = 0; i < nsyms; i++) {
        struct Symbol *sym = saa_rstruct(syms);
        fwriteint32_t(sym->strpos, ofile);
        fwriteint32_t((int32_t)sym->type & ~SYM_WITH_SIZE, ofile);
        /*
         * Fix up the symbol value now we know the final section
         * sizes.
         */
        if ((sym->type & SECT_MASK) == SECT_DATA)
            sym->value += stext.len;
        if ((sym->type & SECT_MASK) == SECT_BSS)
            sym->value += stext.len + sdata.len;
        fwriteint32_t(sym->value, ofile);
        /*
         * Output a size record if necessary.
         */
        if (sym->type & SYM_WITH_SIZE) {
            fwriteint32_t(sym->strpos, ofile);
            fwriteint32_t(0x0DL, ofile);  /* special value: means size */
            fwriteint32_t(sym->size, ofile);
            i++;                /* use up another of `nsyms' */
        }
    }
}

static void aout_sect_write(struct Section *sect,
                            const uint8_t *data, uint32_t len)
{
    saa_wbytes(sect->data, data, len);
    sect->len += len;
}

extern macros_t aout_stdmac[];

#endif                          /* OF_AOUT || OF_AOUTB */

#ifdef OF_AOUT

const struct ofmt of_aout = {
    "Linux a.out object files",
    "aout",
    ".o",
    0,
    32,
    null_debug_arr,
    &null_debug_form,
    aout_stdmac,
    aout_init,
    null_reset,
    nasm_do_legacy_output,
    aout_out,
    aout_deflabel,
    aout_section_names,
    NULL,
    null_sectalign,
    null_segbase,
    null_directive,
    aout_cleanup,
    NULL                        /* pragma list */
};

#endif

#ifdef OF_AOUTB

const struct ofmt of_aoutb = {
    "NetBSD/FreeBSD a.out object files",
    "aoutb",
    ".o",
    0,
    32,
    null_debug_arr,
    &null_debug_form,
    aout_stdmac,
    aoutb_init,
    null_reset,
    nasm_do_legacy_output,
    aout_out,
    aout_deflabel,
    aout_section_names,
    NULL,
    null_sectalign,
    null_segbase,
    null_directive,
    aout_cleanup,
    NULL                        /* pragma list */
};

#endif
