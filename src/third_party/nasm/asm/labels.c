/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2018 The NASM Authors - All Rights Reserved
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
 * labels.c  label handling for the Netwide Assembler
 */

#include "compiler.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "nasm.h"
#include "nasmlib.h"
#include "error.h"
#include "hashtbl.h"
#include "labels.h"

/*
 * A dot-local label is one that begins with exactly one period. Things
 * that begin with _two_ periods are NASM-specific things.
 *
 * If TASM compatibility is enabled, a local label can also begin with
 * @@.
 */
static bool islocal(const char *l)
{
    if (tasm_compatible_mode) {
        if (l[0] == '@' && l[1] == '@')
            return true;
    }

    return (l[0] == '.' && l[1] != '.');
}

/*
 * Return true if this falls into NASM's '..' namespace
 */
static bool ismagic(const char *l)
{
    return l[0] == '.' && l[1] == '.' && l[2] != '@';
}

/*
 * Return true if we should update the local label base
 * as a result of this symbol.  We must exclude local labels
 * as well as any kind of special labels, including ..@ ones.
 */
static bool set_prevlabel(const char *l)
{
    if (tasm_compatible_mode) {
        if (l[0] == '@' && l[1] == '@')
            return false;
    }

    return l[0] != '.';
}

#define LABEL_BLOCK     128     /* no. of labels/block */
#define LBLK_SIZE       (LABEL_BLOCK * sizeof(union label))

#define END_LIST        -3      /* don't clash with NO_SEG! */
#define END_BLOCK       -2

#define PERMTS_SIZE     16384   /* size of text blocks */
#if (PERMTS_SIZE < IDLEN_MAX)
 #error "IPERMTS_SIZE must be greater than or equal to IDLEN_MAX"
#endif

/* string values for enum label_type */
static const char * const types[] =
{"local", "global", "static", "extern", "common", "special",
 "output format special"};

union label {                   /* actual label structures */
    struct {
        int32_t segment;
        int32_t subsection;     /* Available for ofmt->herelabel() */
        int64_t offset;
        int64_t size;
        char *label, *mangled, *special;
        enum label_type type, mangled_type;
        bool defined;
    } defn;
    struct {
        int32_t movingon;
        int64_t dummy;
        union label *next;
    } admin;
};

struct permts {                 /* permanent text storage */
    struct permts *next;        /* for the linked list */
    unsigned int size, usage;   /* size and used space in ... */
    char data[PERMTS_SIZE];     /* ... the data block itself */
};
#define PERMTS_HEADER offsetof(struct permts, data)

uint64_t global_offset_changed;		/* counter for global offset changes */

static struct hash_table ltab;          /* labels hash table */
static union label *ldata;              /* all label data blocks */
static union label *lfree;              /* labels free block */
static struct permts *perm_head;        /* start of perm. text storage */
static struct permts *perm_tail;        /* end of perm. text storage */

static void init_block(union label *blk);
static char *perm_alloc(size_t len);
static char *perm_copy(const char *string);
static char *perm_copy3(const char *s1, const char *s2, const char *s3);
static const char *mangle_label_name(union label *lptr);

static const char *prevlabel;

static bool initialized = false;

/*
 * Emit a symdef to the output and the debug format backends.
 */
static void out_symdef(union label *lptr)
{
    int backend_type;
    int64_t backend_offset;

    /* Backend-defined special segments are passed to symdef immediately */
    if (pass0 == 2) {
        /* Emit special fixups for globals and commons */
        switch (lptr->defn.type) {
        case LBL_GLOBAL:
        case LBL_EXTERN:
        case LBL_COMMON:
            if (lptr->defn.special)
                ofmt->symdef(lptr->defn.mangled, 0, 0, 3, lptr->defn.special);
            break;
        default:
            break;
        }
        return;
    }

    if (pass0 != 1 && lptr->defn.type != LBL_BACKEND)
        return;

    /* Clean up this hack... */
    switch(lptr->defn.type) {
    case LBL_GLOBAL:
    case LBL_EXTERN:
        backend_type = 1;
        backend_offset = lptr->defn.offset;
        break;
    case LBL_COMMON:
        backend_type = 2;
        backend_offset = lptr->defn.size;
        break;
    default:
        backend_type = 0;
        backend_offset = lptr->defn.offset;
        break;
    }

    /* Might be necessary for a backend symbol */
    mangle_label_name(lptr);

    ofmt->symdef(lptr->defn.mangled, lptr->defn.segment,
                 backend_offset, backend_type,
                 lptr->defn.special);

    /*
     * NASM special symbols are not passed to the debug format; none
     * of the current backends want to see them.
     */
    if (lptr->defn.type == LBL_SPECIAL || lptr->defn.type == LBL_BACKEND)
        return;

    dfmt->debug_deflabel(lptr->defn.mangled, lptr->defn.segment,
                         lptr->defn.offset, backend_type,
                         lptr->defn.special);
}

/*
 * Internal routine: finds the `union label' corresponding to the
 * given label name. Creates a new one, if it isn't found, and if
 * `create' is true.
 */
static union label *find_label(const char *label, bool create, bool *created)
{
    union label *lptr, **lpp;
    char *label_str = NULL;
    struct hash_insert ip;

    nasm_assert(label != NULL);

    if (islocal(label))
        label = label_str = nasm_strcat(prevlabel, label);

    lpp = (union label **) hash_find(&ltab, label, &ip);
    lptr = lpp ? *lpp : NULL;

    if (lptr || !create) {
        if (created)
            *created = false;
        return lptr;
    }

    /* Create a new label... */
    if (lfree->admin.movingon == END_BLOCK) {
        /*
         * must allocate a new block
         */
        lfree->admin.next = nasm_malloc(LBLK_SIZE);
        lfree = lfree->admin.next;
        init_block(lfree);
    }

    if (created)
        *created = true;

    nasm_zero(*lfree);
    lfree->defn.label     = perm_copy(label);
    lfree->defn.subsection = NO_SEG;
    if (label_str)
        nasm_free(label_str);

    hash_add(&ip, lfree->defn.label, lfree);
    return lfree++;
}

bool lookup_label(const char *label, int32_t *segment, int64_t *offset)
{
    union label *lptr;

    if (!initialized)
        return false;

    lptr = find_label(label, false, NULL);
    if (lptr && lptr->defn.defined) {
        *segment = lptr->defn.segment;
        *offset = lptr->defn.offset;
        return true;
    }

    return false;
}

bool is_extern(const char *label)
{
    union label *lptr;

    if (!initialized)
        return false;

    lptr = find_label(label, false, NULL);
    return lptr && lptr->defn.type == LBL_EXTERN;
}

static const char *mangle_strings[] = {"", "", "", ""};
static bool mangle_string_set[ARRAY_SIZE(mangle_strings)];

/*
 * Set a prefix or suffix
 */
void set_label_mangle(enum mangle_index which, const char *what)
{
    if (mangle_string_set[which])
        return;                 /* Once set, do not change */

    mangle_strings[which] = perm_copy(what);
    mangle_string_set[which] = true;
}

/*
 * Format a label name with appropriate prefixes and suffixes
 */
static const char *mangle_label_name(union label *lptr)
{
    const char *prefix;
    const char *suffix;

    if (likely(lptr->defn.mangled &&
               lptr->defn.mangled_type == lptr->defn.type))
        return lptr->defn.mangled; /* Already mangled */

    switch (lptr->defn.type) {
    case LBL_GLOBAL:
    case LBL_STATIC:
    case LBL_EXTERN:
        prefix = mangle_strings[LM_GPREFIX];
        suffix = mangle_strings[LM_GSUFFIX];
        break;
    case LBL_BACKEND:
    case LBL_SPECIAL:
        prefix = suffix = "";
        break;
    default:
        prefix = mangle_strings[LM_LPREFIX];
        suffix = mangle_strings[LM_LSUFFIX];
        break;
    }

    lptr->defn.mangled_type = lptr->defn.type;

    if (!(*prefix) && !(*suffix))
        lptr->defn.mangled = lptr->defn.label;
    else
        lptr->defn.mangled = perm_copy3(prefix, lptr->defn.label, suffix);

    return lptr->defn.mangled;
}

static void
handle_herelabel(union label *lptr, int32_t *segment, int64_t *offset)
{
    int32_t oldseg;

    if (likely(!ofmt->herelabel))
        return;

    if (unlikely(location.segment == NO_SEG))
        return;

    oldseg = *segment;

    if (oldseg == location.segment && *offset == location.offset) {
        /* This label is defined at this location */
        int32_t newseg;
        bool copyoffset = false;

        nasm_assert(lptr->defn.mangled);
        newseg = ofmt->herelabel(lptr->defn.mangled, lptr->defn.type,
                                 oldseg, &lptr->defn.subsection, &copyoffset);
        if (likely(newseg == oldseg))
            return;

        *segment = newseg;
        if (copyoffset) {
            /* Maintain the offset from the old to the new segment */
            switch_segment(newseg);
            location.offset = *offset;
        } else {
            /* Keep a separate offset for the new segment */
            *offset  = switch_segment(newseg);
        }
    }
}

static bool declare_label_lptr(union label *lptr,
                               enum label_type type, const char *special)
{
    if (special && !special[0])
        special = NULL;

    if (lptr->defn.type == type ||
        (pass0 == 0 && lptr->defn.type == LBL_LOCAL)) {
        lptr->defn.type = type;
        if (special) {
            if (!lptr->defn.special)
                lptr->defn.special = perm_copy(special);
            else if (nasm_stricmp(lptr->defn.special, special))
                nasm_error(ERR_NONFATAL,
                           "symbol `%s' has inconsistent attributes `%s' and `%s'",
                           lptr->defn.label, lptr->defn.special, special);
        }
        return true;
    }

    /* EXTERN can be replaced with GLOBAL or COMMON */
    if (lptr->defn.type == LBL_EXTERN &&
        (type == LBL_GLOBAL || type == LBL_COMMON)) {
        lptr->defn.type = type;
        /* Override special unconditionally */
        if (special)
            lptr->defn.special = perm_copy(special);
        return true;
    }

    /* GLOBAL or COMMON ignore subsequent EXTERN */
    if ((lptr->defn.type == LBL_GLOBAL || lptr->defn.type == LBL_COMMON) &&
        type == LBL_EXTERN) {
        if (!lptr->defn.special)
            lptr->defn.special = perm_copy(special);
        return false;           /* Don't call define_label() after this! */
    }

    nasm_error(ERR_NONFATAL, "symbol `%s' declared both as %s and %s",
               lptr->defn.label, types[lptr->defn.type], types[type]);

    return false;
}

bool declare_label(const char *label, enum label_type type, const char *special)
{
    union label *lptr = find_label(label, true, NULL);
    return declare_label_lptr(lptr, type, special);
}

/*
 * The "normal" argument decides if we should update the local segment
 * base name or not.
 */
void define_label(const char *label, int32_t segment,
                  int64_t offset, bool normal)
{
    union label *lptr;
    bool created, changed;
    int64_t size;

    /*
     * Phase errors here can be one of two types: a new label appears,
     * or the offset changes. Increment global_offset_changed when that
     * happens, to tell the assembler core to make another pass.
     */
    lptr = find_label(label, true, &created);

    if (segment) {
        /* We are actually defining this label */
        if (lptr->defn.type == LBL_EXTERN) /* auto-promote EXTERN to GLOBAL */
            lptr->defn.type = LBL_GLOBAL;
    } else {
        /* It's a pseudo-segment (extern, common) */
        segment = lptr->defn.segment ? lptr->defn.segment : seg_alloc();
    }

    if (lptr->defn.defined || lptr->defn.type == LBL_BACKEND) {
        /* We have seen this on at least one previous pass */
        mangle_label_name(lptr);
        handle_herelabel(lptr, &segment, &offset);
    }

    if (ismagic(label) && lptr->defn.type == LBL_LOCAL)
        lptr->defn.type = LBL_SPECIAL;

    if (set_prevlabel(label) && normal)
        prevlabel = lptr->defn.label;

    if (lptr->defn.type == LBL_COMMON) {
        size = offset;
        offset = 0;
    } else {
        size = 0;               /* This is a hack... */
    }

    changed = created || !lptr->defn.defined ||
        lptr->defn.segment != segment ||
        lptr->defn.offset != offset || lptr->defn.size != size;
    global_offset_changed += changed;

    /*
     * This probably should be ERR_NONFATAL, but not quite yet.  As a
     * special case, LBL_SPECIAL symbols are allowed to be changed
     * even during the last pass.
     */
    if (changed && pass0 > 1 && lptr->defn.type != LBL_SPECIAL) {
        nasm_error(ERR_WARNING, "label `%s' %s during code generation",
                   lptr->defn.label,
                   created ? "defined" : "changed");
    }

    lptr->defn.segment = segment;
    lptr->defn.offset  = offset;
    lptr->defn.size    = size;
    lptr->defn.defined = true;

    out_symdef(lptr);
}

/*
 * Define a special backend label
 */
void backend_label(const char *label, int32_t segment, int64_t offset)
{
    if (!declare_label(label, LBL_BACKEND, NULL))
        return;

    define_label(label, segment, offset, false);
}

int init_labels(void)
{
    hash_init(&ltab, HASH_LARGE);

    ldata = lfree = nasm_malloc(LBLK_SIZE);
    init_block(lfree);

    perm_head = perm_tail =
        nasm_malloc(sizeof(struct permts));

    perm_head->next = NULL;
    perm_head->size = PERMTS_SIZE;
    perm_head->usage = 0;

    prevlabel = "";

    initialized = true;

    return 0;
}

void cleanup_labels(void)
{
    union label *lptr, *lhold;

    initialized = false;

    hash_free(&ltab);

    lptr = lhold = ldata;
    while (lptr) {
        lptr = &lptr[LABEL_BLOCK-1];
        lptr = lptr->admin.next;
        nasm_free(lhold);
        lhold = lptr;
    }

    while (perm_head) {
        perm_tail = perm_head;
        perm_head = perm_head->next;
        nasm_free(perm_tail);
    }
}

static void init_block(union label *blk)
{
    int j;

    for (j = 0; j < LABEL_BLOCK - 1; j++)
        blk[j].admin.movingon = END_LIST;
    blk[LABEL_BLOCK - 1].admin.movingon = END_BLOCK;
    blk[LABEL_BLOCK - 1].admin.next = NULL;
}

static char * safe_alloc perm_alloc(size_t len)
{
    char *p;

    if (perm_tail->size - perm_tail->usage < len) {
        size_t alloc_len = (len > PERMTS_SIZE) ? len : PERMTS_SIZE;
        perm_tail->next = nasm_malloc(PERMTS_HEADER + alloc_len);
        perm_tail = perm_tail->next;
        perm_tail->next = NULL;
        perm_tail->size = alloc_len;
        perm_tail->usage = 0;
    }
    p = perm_tail->data + perm_tail->usage;
    perm_tail->usage += len;
    return p;
}

static char *perm_copy(const char *string)
{
    char *p;
    size_t len;

    if (!string)
        return NULL;

    len = strlen(string)+1; /* Include final NUL */

    p = perm_alloc(len);
    memcpy(p, string, len);

    return p;
}

static char *
perm_copy3(const char *s1, const char *s2, const char *s3)
{
    char *p;
    size_t l1 = strlen(s1);
    size_t l2 = strlen(s2);
    size_t l3 = strlen(s3)+1;   /* Include final NUL */

    p = perm_alloc(l1+l2+l3);
    memcpy(p, s1, l1);
    memcpy(p+l1, s2, l2);
    memcpy(p+l1+l2, s3, l3);

    return p;
}

const char *local_scope(const char *label)
{
   return islocal(label) ? prevlabel : "";
}

/*
 * Notes regarding bug involving redefinition of external segments.
 *
 * Up to and including v0.97, the following code didn't work. From 0.97
 * developers release 2 onwards, it will generate an error.
 *
 * EXTERN extlabel
 * newlabel EQU extlabel + 1
 *
 * The results of allowing this code through are that two import records
 * are generated, one for 'extlabel' and one for 'newlabel'.
 *
 * The reason for this is an inadequacy in the defined interface between
 * the label manager and the output formats. The problem lies in how the
 * output format driver tells that a label is an external label for which
 * a label import record must be produced. Most (all except bin?) produce
 * the record if the segment number of the label is not one of the internal
 * segments that the output driver is producing.
 *
 * A simple fix to this would be to make the output formats keep track of
 * which symbols they've produced import records for, and make them not
 * produce import records for segments that are already defined.
 *
 * The best way, which is slightly harder but reduces duplication of code
 * and should therefore make the entire system smaller and more stable is
 * to change the interface between assembler, define_label(), and
 * the output module. The changes that are needed are:
 *
 * The semantics of the 'isextern' flag passed to define_label() need
 * examining. This information may or may not tell us what we need to
 * know (ie should we be generating an import record at this point for this
 * label). If these aren't the semantics, the semantics should be changed
 * to this.
 *
 * The output module interface needs changing, so that the `isextern' flag
 * is passed to the module, so that it can be easily tested for.
 */
