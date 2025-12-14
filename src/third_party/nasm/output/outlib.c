/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2020 The NASM Authors - All Rights Reserved
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
 * outlib.c
 *
 * Common routines for the output backends.
 */

#include "outlib.h"
#include "raa.h"

uint64_t realsize(enum out_type type, uint64_t size)
{
    switch (type) {
    case OUT_REL1ADR:
	return 1;
    case OUT_REL2ADR:
	return 2;
    case OUT_REL4ADR:
	return 4;
    case OUT_REL8ADR:
	return 8;
    default:
	return size;
    }
}

/* Common section/symbol handling */

struct ol_sect *_ol_sect_list;
uint64_t _ol_nsects;             /* True sections, not external symbols */
static struct ol_sect **ol_sect_tail = &_ol_sect_list;
static struct hash_table ol_secthash;
static struct RAA *ol_sect_index_tbl;

struct ol_sym *_ol_sym_list;
uint64_t _ol_nsyms;
static struct ol_sym **ol_sym_tail = &_ol_sym_list;
static struct hash_table ol_symhash;

void ol_init(void)
{
}

static void ol_free_symbols(void)
{
    struct ol_sym *s, *stmp;

    hash_free(&ol_symhash);

    list_for_each_safe(s, stmp, _ol_sym_list) {
        nasm_free((char *)s->name);
        nasm_free(s);
    }

    _ol_nsyms = 0;
    _ol_sym_list = NULL;
    ol_sym_tail = &_ol_sym_list;
}

static void ol_free_sections(void)
{
    struct ol_sect *s, *stmp;

    hash_free(&ol_secthash);
    raa_free(ol_sect_index_tbl);
    ol_sect_index_tbl = NULL;

    list_for_each_safe(s, stmp, _ol_sect_list) {
        saa_free(s->data);
        saa_free(s->reloc);
        nasm_free((char *)s->name);
        nasm_free(s);
    }

    _ol_nsects = 0;
    _ol_sect_list = NULL;
    ol_sect_tail = &_ol_sect_list;
}

void ol_cleanup(void)
{
    ol_free_symbols();
    ol_free_sections();
}

/*
 * Allocate a section index and add a section, subsection, or external
 * symbol to the section-by-index table. If the index provided is zero,
 * allocate a new index via seg_alloc().
 */
static uint32_t ol_seg_alloc(void *s, uint32_t ix)
{
    if (!ix)
        ix = seg_alloc();
    ol_sect_index_tbl = raa_write_ptr(ol_sect_index_tbl, ix >> 1, s);
    return ix;
}

/*
 * Find a section or create a new section structure if it does not exist
 * and allocate it an index value via seg_alloc().
 */
struct ol_sect *_ol_get_sect(const char *name, size_t ssize, size_t rsize)
{
    struct ol_sect *s, **sp;
    struct hash_insert hi;

    sp = (struct ol_sect **)hash_find(&ol_secthash, name, &hi);
    if (sp)
        return *sp;

    s             = nasm_zalloc(ssize);
    s->syml.tail  = &s->syml.head;
    s->name       = nasm_strdup(name);
    s->data       = saa_init(1);
    s->reloc      = saa_init(rsize);
    *ol_sect_tail = s;
    ol_sect_tail  = &s->next;
    _ol_nsects++;
    s->index     = s->subindex = ol_seg_alloc(s, 0);

    hash_add(&hi, s->name, s);
    return s;
}

/* Find a section by name without creating one */
struct ol_sect *_ol_sect_by_name(const char *name)
{
    struct ol_sect **sp;

    sp = (struct ol_sect **)hash_find(&ol_secthash, name, NULL);
    return sp ? *sp : NULL;
}

/* Find a section or external symbol by index; NULL if not valid */
struct ol_sect *_ol_sect_by_index(int32_t index)
{
    uint32_t ix = index;

    if (unlikely(ix >= SEG_ABS))
        return NULL;

    return raa_read_ptr(ol_sect_index_tbl, ix >> 1);
}

/*
 * Start a new subsection for the given section. At the moment, once a
 * subsection has been created, it is not possible to revert to an
 * earlier subsection. ol_sect_by_index() will return the main section
 * structure. Returns the new section index.  This is used to prevent
 * the front end from optimizing across subsection boundaries.
 */
int32_t _ol_new_subsection(struct ol_sect *sect)
{
    if (unlikely(!sect))
        return NO_SEG;

    return sect->subindex = ol_seg_alloc(sect, 0);
}

/*
 * Insert a symbol into a list; need to use upcasting using container_of()
 * to walk the list later.
 */
static void ol_add_sym_to(struct ol_symlist *syml, struct ol_symhead *head,
                          uint64_t offset)
{
    syml->tree.key = offset;
    head->tree     = rb_insert(head->tree, &syml->tree);
    *head->tail    = syml;
    head->tail     = &syml->next;
    head->n++;
}

/*
 * Create a location structure from seg:offs
 */
void ol_mkloc(struct ol_loc *loc, int64_t offs, int32_t seg)
{
    nasm_zero(*loc);
    loc->offs = offs;

    if (unlikely((uint32_t)seg >= SEG_ABS)) {
        if (likely(seg == NO_SEG)) {
            loc->seg.t     = OS_NOSEG;
        } else {
            loc->seg.t     = OS_ABS;
            loc->seg.index = seg - SEG_ABS;
        }
    } else {
        loc->seg.index  = seg & ~1;
        loc->seg.t      = OS_SECT | (seg & 1);
        loc->seg.s.sect = _ol_sect_by_index(loc->seg.index);
    }
}

/*
 * Create a new symbol. If this symbol is OS_OFFS, add it to the relevant
 * section, too. If the symbol already exists, return NULL; this is
 * different from ol_get_section() as a single section may be invoked
 * many times. On the contrary, the front end will prevent a single symbol
 * from being defined more than once.
 *
 * If flags has OF_GLOBAL set, add it to the global symbol hash for
 * the containing section if applicable.
 *
 * If flags has OF_IMPSEC set, allocate a segment index for it via
 * seg_alloc() unless v->index is already set, and add it to the
 * section by index list.
 */
struct ol_sym *_ol_new_sym(const char *name, const struct ol_loc *v,
                           uint32_t flags, size_t size)
{
    struct hash_insert hi;
    struct ol_sym *sym;

    if (hash_find(&ol_symhash, name, &hi))
        return NULL;            /* Symbol already exists */

    flags     |= OF_SYMBOL;

    sym        = nasm_zalloc(size);
    sym->name  = nasm_strdup(name);
    sym->v     = *v;

    if (sym->v.seg.t & OS_SECT) {
        struct ol_sect *sect = sym->v.seg.s.sect;

        if (!sect || (sect->flags & OF_SYMBOL))
            /* Must be an external or common reference */
            flags |= OF_IMPSEC;

        if (flags & OF_IMPSEC) {
            /* Metasection */
            if (!sym->v.seg.s.sym) {
                sym->v.seg.s.sym = sym;
                sym->v.seg.index = ol_seg_alloc(sym, sym->v.seg.index);
            }
        } else if (sym->v.seg.t == OS_OFFS) {
            struct ol_sect * const sect = sym->v.seg.s.sect;
            const uint64_t offs = sym->v.offs;

            ol_add_sym_to(&sym->syml, &sect->syml, offs);
            if (flags & OF_GLOBAL)
                ol_add_sym_to(&sym->symg, &sect->symg, offs);
        }
    }
    sym->flags = flags;

    *ol_sym_tail = sym;
    ol_sym_tail  = &sym->next;
    _ol_nsyms++;

    hash_add(&hi, sym->name, sym);
    return sym;
}

/* Find a symbol in the global namespace */
struct ol_sym *_ol_sym_by_name(const char *name)
{
    struct ol_sym **symp;

    symp = (struct ol_sym **)hash_find(&ol_symhash, name, NULL);
    return symp ? *symp : NULL;
}

/*
 * Find a symbol by address in a specific section. If no symbol is defined
 * at that exact address, return the immediately previously defined one.
 * If global is set, then only return global symbols.
 */
struct ol_sym *_ol_sym_by_address(struct ol_sect *sect, int64_t addr,
                                  bool global)
{
    struct ol_symhead *head;
    size_t t_offs;
    struct rbtree *t;

    if (global) {
        head = &sect->symg;
        t_offs = offsetof(struct ol_sym, symg.tree);
    } else {
        head = &sect->syml;
        t_offs = offsetof(struct ol_sym, syml.tree);
    }

    t = rb_search(head->tree, addr);
    if (!t)
        return NULL;

    return (struct ol_sym *)((char *)t - t_offs);
}
