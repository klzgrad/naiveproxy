/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1996-2016 The NASM Authors - All Rights Reserved
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
 * outieee.c	output routines for the Netwide Assembler to produce
 *		IEEE-std object files
 */

/* notes: I have tried to make this correspond to the IEEE version
 * of the standard, specifically the primary ASCII version.  It should
 * be trivial to create the binary version given this source (which is
 * one of MANY things that have to be done to make this correspond to
 * the hp-microtek version of the standard).
 *
 * 16-bit support is assumed to use 24-bit addresses
 * The linker can sort out segmentation-specific stuff
 * if it keeps track of externals
 * in terms of being relative to section bases
 *
 * A non-standard variable type, the 'Yn' variable, has been introduced.
 * Basically it is a reference to extern 'n'- denoting the low limit
 * (L-variable) of the section that extern 'n' is defined in.  Like the
 * x variable, there may be no explicit assignment to it, it is derived
 * from the public definition corresponding to the extern name.  This
 * is required because the one thing the mufom guys forgot to do well was
 * take into account segmented architectures.
 *
 * I use comment classes for various things and these are undefined by
 * the standard.
 *
 * Debug info should be considered totally non-standard (local labels are
 * standard but linenum records are not covered by the standard.
 * Type defs have the standard format but absolute meanings for ordinal
 * types are not covered by the standard.)
 *
 * David Lindauer, LADsoft
 */
#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>             /* Note: we need the ANSI version of stdarg.h */
#include <ctype.h>

#include "nasm.h"
#include "nasmlib.h"
#include "error.h"
#include "ver.h"

#include "outform.h"
#include "outlib.h"

#ifdef OF_IEEE

#define ARRAY_BOT 0x1

static char ieee_infile[FILENAME_MAX];
static int ieee_uppercase;

static bool any_segs;
static int arrindex;

#define HUNKSIZE 1024           /* Size of the data hunk */
#define EXT_BLKSIZ 512
#define LDPERLINE 32            /* bytes per line in output */

struct ieeeSection;

struct LineNumber {
    struct LineNumber *next;
    struct ieeeSection *segment;
    int32_t offset;
    int32_t lineno;
};

static struct FileName {
    struct FileName *next;
    char *name;
    int32_t index;
} *fnhead, **fntail;

static struct Array {
    struct Array *next;
    unsigned size;
    int basetype;
} *arrhead, **arrtail;

static struct ieeePublic {
    struct ieeePublic *next;
    char *name;
    int32_t offset;
    int32_t segment;               /* only if it's far-absolute */
    int32_t index;
    int type;                   /* for debug purposes */
} *fpubhead, **fpubtail, *last_defined;

static struct ieeeExternal {
    struct ieeeExternal *next;
    char *name;
    int32_t commonsize;
} *exthead, **exttail;

static int externals;

static struct ExtBack {
    struct ExtBack *next;
    int index[EXT_BLKSIZ];
} *ebhead, **ebtail;

/* NOTE: the first segment MUST be the lineno segment */
static struct ieeeSection {
    struct ieeeSection *next;
    char *name;
    struct ieeeObjData *data, *datacurr;
    struct ieeeFixupp *fptr, *flptr;
    int32_t index;                 /* the NASM segment id */
    int32_t ieee_index;            /* the OBJ-file segment index */
    int32_t currentpos;
    int32_t align;                 /* can be SEG_ABS + absolute addr */
    int32_t startpos;
    int32_t use32;                 /* is this segment 32-bit? */
    struct ieeePublic *pubhead, **pubtail, *lochead, **loctail;
    enum {
        CMB_PRIVATE = 0,
        CMB_PUBLIC = 2,
        CMB_COMMON = 6
    } combine;
} *seghead, **segtail, *ieee_seg_needs_update;

struct ieeeObjData {
    struct ieeeObjData *next;
    uint8_t data[HUNKSIZE];
};

struct ieeeFixupp {
    struct ieeeFixupp *next;
    enum {
        FT_SEG = 0,
        FT_REL = 1,
        FT_OFS = 2,
        FT_EXT = 3,
        FT_WRT = 4,
        FT_EXTREL = 5,
        FT_EXTWRT = 6,
        FT_EXTSEG = 7
    } ftype;
    int16_t size;
    int32_t id1;
    int32_t id2;
    int32_t offset;
    int32_t addend;
};

static int32_t ieee_entry_seg, ieee_entry_ofs;
static int checksum;

extern const struct ofmt of_ieee;
static const struct dfmt ladsoft_debug_form;

static void ieee_data_new(struct ieeeSection *);
static void ieee_write_fixup(int32_t, int32_t, struct ieeeSection *,
                             int, uint64_t, int32_t);
static void ieee_install_fixup(struct ieeeSection *, struct ieeeFixupp *);
static int32_t ieee_segment(char *, int, int *);
static void ieee_write_file(void);
static void ieee_write_byte(struct ieeeSection *, int);
static void ieee_write_word(struct ieeeSection *, int);
static void ieee_write_dword(struct ieeeSection *, int32_t);
static void ieee_putascii(char *, ...);
static void ieee_putcs(int);
static int32_t ieee_putld(int32_t, int32_t, uint8_t *);
static int32_t ieee_putlr(struct ieeeFixupp *);
static void ieee_unqualified_name(char *, char *);

/*
 * pup init
 */
static void ieee_init(void)
{
    strlcpy(ieee_infile, inname, sizeof(ieee_infile));
    any_segs = false;
    fpubhead = NULL;
    fpubtail = &fpubhead;
    exthead = NULL;
    exttail = &exthead;
    externals = 1;
    ebhead = NULL;
    ebtail = &ebhead;
    seghead = ieee_seg_needs_update = NULL;
    segtail = &seghead;
    ieee_entry_seg = NO_SEG;
    ieee_uppercase = false;
    checksum = 0;
}

/*
 * Rundown
 */
static void ieee_cleanup(void)
{
    ieee_write_file();
    dfmt->cleanup();
    while (seghead) {
        struct ieeeSection *segtmp = seghead;
        seghead = seghead->next;
        while (segtmp->pubhead) {
            struct ieeePublic *pubtmp = segtmp->pubhead;
            segtmp->pubhead = pubtmp->next;
            nasm_free(pubtmp);
        }
        while (segtmp->fptr) {
            struct ieeeFixupp *fixtmp = segtmp->fptr;
            segtmp->fptr = fixtmp->next;
            nasm_free(fixtmp);
        }
        while (segtmp->data) {
            struct ieeeObjData *dattmp = segtmp->data;
            segtmp->data = dattmp->next;
            nasm_free(dattmp);
        }
        nasm_free(segtmp);
    }
    while (fpubhead) {
        struct ieeePublic *pubtmp = fpubhead;
        fpubhead = fpubhead->next;
        nasm_free(pubtmp);
    }
    while (exthead) {
        struct ieeeExternal *exttmp = exthead;
        exthead = exthead->next;
        nasm_free(exttmp);
    }
    while (ebhead) {
        struct ExtBack *ebtmp = ebhead;
        ebhead = ebhead->next;
        nasm_free(ebtmp);
    }
}

/*
 * callback for labels
 */
static void ieee_deflabel(char *name, int32_t segment,
                          int64_t offset, int is_global, char *special)
{
    /*
     * We have three cases:
     *
     * (i) `segment' is a segment-base. If so, set the name field
     * for the segment structure it refers to, and then
     * return.
     *
     * (ii) `segment' is one of our segments, or a SEG_ABS segment.
     * Save the label position for later output of a PUBDEF record.
     *
     *
     * (iii) `segment' is not one of our segments. Save the label
     * position for later output of an EXTDEF.
     */
    struct ieeeExternal *ext;
    struct ExtBack *eb;
    struct ieeeSection *seg;
    int i;

    if (special) {
        nasm_error(ERR_NONFATAL, "unrecognised symbol type `%s'", special);
    }
    /*
     * First check for the double-period, signifying something
     * unusual.
     */
    if (name[0] == '.' && name[1] == '.' && name[2] != '@') {
        if (!strcmp(name, "..start")) {
            ieee_entry_seg = segment;
            ieee_entry_ofs = offset;
        }
        return;
    }

    /*
     * Case (i):
     */
    if (ieee_seg_needs_update) {
        ieee_seg_needs_update->name = name;
        return;
    }
    if (segment < SEG_ABS && segment != NO_SEG && segment % 2)
        return;

    /*
     * case (ii)
     */
    if (segment >= SEG_ABS) {
        /*
         * SEG_ABS subcase of (ii).
         */
        if (is_global) {
            struct ieeePublic *pub;

            pub = *fpubtail = nasm_malloc(sizeof(*pub));
            fpubtail = &pub->next;
            pub->next = NULL;
            pub->name = name;
            pub->offset = offset;
            pub->segment = segment & ~SEG_ABS;
        }
        return;
    }

    for (seg = seghead; seg && is_global; seg = seg->next)
        if (seg->index == segment) {
            struct ieeePublic *pub;

            last_defined = pub = *seg->pubtail = nasm_malloc(sizeof(*pub));
            seg->pubtail = &pub->next;
            pub->next = NULL;
            pub->name = name;
            pub->offset = offset;
            pub->index = seg->ieee_index;
            pub->segment = -1;
            return;
        }

    /*
     * Case (iii).
     */
    if (is_global) {
        ext = *exttail = nasm_malloc(sizeof(*ext));
        ext->next = NULL;
        exttail = &ext->next;
        ext->name = name;
        if (is_global == 2)
            ext->commonsize = offset;
        else
            ext->commonsize = 0;
        i = segment / 2;
        eb = ebhead;
        if (!eb) {
            eb = *ebtail = nasm_zalloc(sizeof(*eb));
            eb->next = NULL;
            ebtail = &eb->next;
        }
        while (i > EXT_BLKSIZ) {
            if (eb && eb->next)
                eb = eb->next;
            else {
                eb = *ebtail = nasm_zalloc(sizeof(*eb));
                eb->next = NULL;
                ebtail = &eb->next;
            }
            i -= EXT_BLKSIZ;
        }
        eb->index[i] = externals++;
    }

}

/*
 * Put data out
 */
static void ieee_out(int32_t segto, const void *data,
		     enum out_type type, uint64_t size,
                     int32_t segment, int32_t wrt)
{
    const uint8_t *ucdata;
    int32_t ldata;
    struct ieeeSection *seg;

    /*
     * If `any_segs' is still false, we must define a default
     * segment.
     */
    if (!any_segs) {
        int tempint;            /* ignored */
        if (segto != ieee_segment("__NASMDEFSEG", 2, &tempint))
            nasm_panic("strange segment conditions in IEEE driver");
    }

    /*
     * Find the segment we are targetting.
     */
    for (seg = seghead; seg; seg = seg->next)
        if (seg->index == segto)
            break;
    if (!seg)
        nasm_panic("code directed to nonexistent segment?");

    if (type == OUT_RAWDATA) {
        ucdata = data;
        while (size--)
            ieee_write_byte(seg, *ucdata++);
    } else if (type == OUT_ADDRESS || type == OUT_REL2ADR ||
               type == OUT_REL4ADR) {
        if (type == OUT_ADDRESS)
            size = abs((int)size);
        else if (segment == NO_SEG)
            nasm_error(ERR_NONFATAL, "relative call to absolute address not"
                  " supported by IEEE format");
        ldata = *(int64_t *)data;
        if (type == OUT_REL2ADR)
            ldata += (size - 2);
        if (type == OUT_REL4ADR)
            ldata += (size - 4);
        ieee_write_fixup(segment, wrt, seg, size, type, ldata);
        if (size == 2)
            ieee_write_word(seg, ldata);
        else
            ieee_write_dword(seg, ldata);
    } else if (type == OUT_RESERVE) {
        while (size--)
            ieee_write_byte(seg, 0);
    }
}

static void ieee_data_new(struct ieeeSection *segto)
{

    if (!segto->data)
        segto->data = segto->datacurr =
            nasm_malloc(sizeof(*(segto->datacurr)));
    else
        segto->datacurr = segto->datacurr->next =
            nasm_malloc(sizeof(*(segto->datacurr)));
    segto->datacurr->next = NULL;
}

/*
 * this routine is unalduterated bloatware.  I usually don't do this
 * but I might as well see what it is like on a harmless program.
 * If anyone wants to optimize this is a good canditate!
 */
static void ieee_write_fixup(int32_t segment, int32_t wrt,
                             struct ieeeSection *segto, int size,
                             uint64_t realtype, int32_t offset)
{
    struct ieeeSection *target;
    struct ieeeFixupp s;

    /* Don't put a fixup for things NASM can calculate */
    if (wrt == NO_SEG && segment == NO_SEG)
        return;

    s.ftype = -1;
    /* if it is a WRT offset */
    if (wrt != NO_SEG) {
        s.ftype = FT_WRT;
        s.addend = offset;
        if (wrt >= SEG_ABS)
            s.id1 = -(wrt - SEG_ABS);
        else {
            if (wrt % 2 && realtype != OUT_REL2ADR
                && realtype != OUT_REL4ADR) {
                wrt--;

                for (target = seghead; target; target = target->next)
                    if (target->index == wrt)
                        break;
                if (target) {
                    s.id1 = target->ieee_index;
                    for (target = seghead; target; target = target->next)
                        if (target->index == segment)
                            break;

                    if (target)
                        s.id2 = target->ieee_index;
                    else {
                        /*
                         * Now we assume the segment field is being used
                         * to hold an extern index
                         */
                        int32_t i = segment / 2;
                        struct ExtBack *eb = ebhead;
                        while (i > EXT_BLKSIZ) {
                            if (eb)
                                eb = eb->next;
                            else
                                break;
                            i -= EXT_BLKSIZ;
                        }
                        /* if we have an extern decide the type and make a record
                         */
                        if (eb) {
                            s.ftype = FT_EXTWRT;
                            s.addend = 0;
                            s.id2 = eb->index[i];
                        } else
                            nasm_error(ERR_NONFATAL,
                                  "Source of WRT must be an offset");
                    }

                } else
                    nasm_panic("unrecognised WRT value in ieee_write_fixup");
            } else
                nasm_error(ERR_NONFATAL, "target of WRT must be a section ");
        }
        s.size = size;
        ieee_install_fixup(segto, &s);
        return;
    }
    /* Pure segment fixup ? */
    if (segment != NO_SEG) {
        s.ftype = FT_SEG;
        s.id1 = 0;
        if (segment >= SEG_ABS) {
            /* absolute far segment fixup */
            s.id1 = -(segment - ~SEG_ABS);
        } else if (segment % 2) {
            /* fixup to named segment */
            /* look it up */
            for (target = seghead; target; target = target->next)
                if (target->index == segment - 1)
                    break;
            if (target)
                s.id1 = target->ieee_index;
            else {
                /*
                 * Now we assume the segment field is being used
                 * to hold an extern index
                 */
                int32_t i = segment / 2;
                struct ExtBack *eb = ebhead;
                while (i > EXT_BLKSIZ) {
                    if (eb)
                        eb = eb->next;
                    else
                        break;
                    i -= EXT_BLKSIZ;
                }
                /* if we have an extern decide the type and make a record
                 */
                if (eb) {
                    if (realtype == OUT_REL2ADR || realtype == OUT_REL4ADR) {
                        nasm_panic("Segment of a rel not supported in ieee_write_fixup");
                    } else {
                        /* If we want the segment */
                        s.ftype = FT_EXTSEG;
                        s.addend = 0;
                        s.id1 = eb->index[i];
                    }

                } else
                    /* If we get here the seg value doesn't make sense */
                    nasm_panic("unrecognised segment value in ieee_write_fixup");
            }

        } else {
            /* Assume we are offsetting directly from a section
             * So look up the target segment
             */
            for (target = seghead; target; target = target->next)
                if (target->index == segment)
                    break;
            if (target) {
                if (realtype == OUT_REL2ADR || realtype == OUT_REL4ADR) {
                    /* PC rel to a known offset */
                    s.id1 = target->ieee_index;
                    s.ftype = FT_REL;
                    s.size = size;
                    s.addend = offset;
                } else {
                    /* We were offsetting from a seg */
                    s.id1 = target->ieee_index;
                    s.ftype = FT_OFS;
                    s.size = size;
                    s.addend = offset;
                }
            } else {
                /*
                 * Now we assume the segment field is being used
                 * to hold an extern index
                 */
                int32_t i = segment / 2;
                struct ExtBack *eb = ebhead;
                while (i > EXT_BLKSIZ) {
                    if (eb)
                        eb = eb->next;
                    else
                        break;
                    i -= EXT_BLKSIZ;
                }
                /* if we have an extern decide the type and make a record
                 */
                if (eb) {
                    if (realtype == OUT_REL2ADR || realtype == OUT_REL4ADR) {
                        s.ftype = FT_EXTREL;
                        s.addend = 0;
                        s.id1 = eb->index[i];
                    } else {
                        /* else we want the external offset */
                        s.ftype = FT_EXT;
                        s.addend = 0;
                        s.id1 = eb->index[i];
                    }

                } else
                    /* If we get here the seg value doesn't make sense */
                    nasm_panic("unrecognised segment value in ieee_write_fixup");
            }
        }
        if (size != 2 && s.ftype == FT_SEG)
            nasm_error(ERR_NONFATAL, "IEEE format can only handle 2-byte"
                  " segment base references");
        s.size = size;
        ieee_install_fixup(segto, &s);
        return;
    }
    /* should never get here */
}
static void ieee_install_fixup(struct ieeeSection *seg,
                               struct ieeeFixupp *fix)
{
    struct ieeeFixupp *f;
    f = nasm_malloc(sizeof(struct ieeeFixupp));
    memcpy(f, fix, sizeof(struct ieeeFixupp));
    f->offset = seg->currentpos;
    seg->currentpos += fix->size;
    f->next = NULL;
    if (seg->fptr)
        seg->flptr = seg->flptr->next = f;
    else
        seg->fptr = seg->flptr = f;

}

/*
 * segment registry
 */
static int32_t ieee_segment(char *name, int pass, int *bits)
{
    /*
     * We call the label manager here to define a name for the new
     * segment, and when our _own_ label-definition stub gets
     * called in return, it should register the new segment name
     * using the pointer it gets passed. That way we save memory,
     * by sponging off the label manager.
     */
    if (!name) {
        *bits = 16;
        if (!any_segs)
            return 0;
        return seghead->index;
    } else {
        struct ieeeSection *seg;
        int ieee_idx, attrs;
	bool rn_error;
        char *p;

        /*
         * Look for segment attributes.
         */
        attrs = 0;
        while (*name == '.')
            name++;             /* hack, but a documented one */
        p = name;
        while (*p && !nasm_isspace(*p))
            p++;
        if (*p) {
            *p++ = '\0';
            while (*p && nasm_isspace(*p))
                *p++ = '\0';
        }
        while (*p) {
            while (*p && !nasm_isspace(*p))
                p++;
            if (*p) {
                *p++ = '\0';
                while (*p && nasm_isspace(*p))
                    *p++ = '\0';
            }

            attrs++;
        }

        ieee_idx = 1;
        for (seg = seghead; seg; seg = seg->next) {
            ieee_idx++;
            if (!strcmp(seg->name, name)) {
                if (attrs > 0 && pass == 1)
                    nasm_error(ERR_WARNING, "segment attributes specified on"
                          " redeclaration of segment: ignoring");
                if (seg->use32)
                    *bits = 32;
                else
                    *bits = 16;
                return seg->index;
            }
        }

        *segtail = seg = nasm_malloc(sizeof(*seg));
        seg->next = NULL;
        segtail = &seg->next;
        seg->index = seg_alloc();
        seg->ieee_index = ieee_idx;
        any_segs = true;
        seg->name = NULL;
        seg->currentpos = 0;
        seg->align = 1;         /* default */
        seg->use32 = *bits == 32;       /* default to user spec */
        seg->combine = CMB_PUBLIC;      /* default */
        seg->pubhead = NULL;
        seg->pubtail = &seg->pubhead;
        seg->data = NULL;
        seg->fptr = NULL;
        seg->lochead = NULL;
        seg->loctail = &seg->lochead;

        /*
         * Process the segment attributes.
         */
        p = name;
        while (attrs--) {
            p += strlen(p);
            while (!*p)
                p++;

            /*
             * `p' contains a segment attribute.
             */
            if (!nasm_stricmp(p, "private"))
                seg->combine = CMB_PRIVATE;
            else if (!nasm_stricmp(p, "public"))
                seg->combine = CMB_PUBLIC;
            else if (!nasm_stricmp(p, "common"))
                seg->combine = CMB_COMMON;
            else if (!nasm_stricmp(p, "use16"))
                seg->use32 = false;
            else if (!nasm_stricmp(p, "use32"))
                seg->use32 = true;
            else if (!nasm_strnicmp(p, "align=", 6)) {
                seg->align = readnum(p + 6, &rn_error);
                if (seg->align == 0)
                    seg->align = 1;
                if (rn_error) {
                    seg->align = 1;
                    nasm_error(ERR_NONFATAL, "segment alignment should be"
                          " numeric");
                }
                switch (seg->align) {
                case 1:        /* BYTE */
                case 2:        /* WORD */
                case 4:        /* DWORD */
                case 16:       /* PARA */
                case 256:      /* PAGE */
                case 8:
                case 32:
                case 64:
                case 128:
                    break;
                default:
                    nasm_error(ERR_NONFATAL, "invalid alignment value %d",
                          seg->align);
                    seg->align = 1;
                    break;
                }
            } else if (!nasm_strnicmp(p, "absolute=", 9)) {
                seg->align = SEG_ABS + readnum(p + 9, &rn_error);
                if (rn_error)
                    nasm_error(ERR_NONFATAL, "argument to `absolute' segment"
                          " attribute should be numeric");
            }
        }

        ieee_seg_needs_update = seg;
        if (seg->align >= SEG_ABS)
            define_label(name, NO_SEG, seg->align - SEG_ABS, false);
        else
            define_label(name, seg->index + 1, 0L, false);
        ieee_seg_needs_update = NULL;

        if (seg->use32)
            *bits = 32;
        else
            *bits = 16;
        return seg->index;
    }
}

/*
 * directives supported
 */
static enum directive_result
ieee_directive(enum directive directive, char *value, int pass)
{

    (void)value;
    (void)pass;

    switch (directive) {
    case D_UPPERCASE:
        ieee_uppercase = true;
        return DIRR_OK;

    default:
	return DIRR_UNKNOWN;
    }
}

static void ieee_sectalign(int32_t seg, unsigned int value)
{
    struct ieeeSection *s;

    list_for_each(s, seghead) {
        if (s->index == seg)
            break;
    }

    /*
     * 256 is maximum there, note it may happen
     * that align is issued on "absolute" segment
     * it's fine since SEG_ABS > 256 and we never
     * get escape this test
     */
    if (!s || !is_power2(value) || value > 256)
        return;

    if ((unsigned int)s->align < value)
        s->align = value;
}

/*
 * Return segment data
 */
static int32_t ieee_segbase(int32_t segment)
{
    struct ieeeSection *seg;

    /*
     * Find the segment in our list.
     */
    for (seg = seghead; seg; seg = seg->next)
        if (seg->index == segment - 1)
            break;

    if (!seg)
        return segment;         /* not one of ours - leave it alone */

    if (seg->align >= SEG_ABS)
        return seg->align;      /* absolute segment */

    return segment;             /* no special treatment */
}

static void ieee_write_file(void)
{
    const struct tm * const thetime = &official_compile_time.local;
    struct FileName *fn;
    struct ieeeSection *seg;
    struct ieeePublic *pub, *loc;
    struct ieeeExternal *ext;
    struct ieeeObjData *data;
    struct ieeeFixupp *fix;
    struct Array *arr;
    int i;
    const bool debuginfo = (dfmt == &ladsoft_debug_form);

    /*
     * Write the module header
     */
    ieee_putascii("MBFNASM,%02X%s.\n", strlen(ieee_infile), ieee_infile);

    /*
     * Write the NASM boast comment.
     */
    ieee_putascii("CO0,%02X%s.\n", strlen(nasm_comment), nasm_comment);

    /*
     * write processor-specific information
     */
    ieee_putascii("AD8,4,L.\n");

    /*
     * date and time
     */
    ieee_putascii("DT%04d%02d%02d%02d%02d%02d.\n",
                  1900 + thetime->tm_year, thetime->tm_mon + 1,
                  thetime->tm_mday, thetime->tm_hour, thetime->tm_min,
                  thetime->tm_sec);
    /*
     * if debugging, dump file names
     */
    for (fn = fnhead; fn && debuginfo; fn = fn->next) {
        ieee_putascii("C0105,%02X%s.\n", strlen(fn->name), fn->name);
    }

    ieee_putascii("CO101,07ENDHEAD.\n");
    /*
     * the standard doesn't specify when to put checksums,
     * we'll just do it periodically.
     */
    ieee_putcs(false);

    /*
     * Write the section headers
     */
    seg = seghead;
    if (!debuginfo && !strcmp(seg->name, "??LINE"))
        seg = seg->next;
    while (seg) {
        char buf[256];
        char attrib;
        switch (seg->combine) {
        case CMB_PUBLIC:
        default:
            attrib = 'C';
            break;
        case CMB_PRIVATE:
            attrib = 'S';
            break;
        case CMB_COMMON:
            attrib = 'M';
            break;
        }
        ieee_unqualified_name(buf, seg->name);
        if (seg->align >= SEG_ABS) {
            ieee_putascii("ST%X,A,%02X%s.\n", seg->ieee_index,
                          strlen(buf), buf);
            ieee_putascii("ASL%X,%lX.\n", seg->ieee_index,
                          (seg->align - SEG_ABS) * 16);
        } else {
            ieee_putascii("ST%X,%c,%02X%s.\n", seg->ieee_index, attrib,
                          strlen(buf), buf);
            ieee_putascii("SA%X,%lX.\n", seg->ieee_index, seg->align);
            ieee_putascii("ASS%X,%X.\n", seg->ieee_index,
                          seg->currentpos);
        }
        seg = seg->next;
    }
    /*
     * write the start address if there is one
     */
    if (ieee_entry_seg) {
        for (seg = seghead; seg; seg = seg->next)
            if (seg->index == ieee_entry_seg)
                break;
        if (!seg)
            nasm_panic("Start address records are incorrect");
        else
            ieee_putascii("ASG,R%X,%lX,+.\n", seg->ieee_index,
                          ieee_entry_ofs);
    }

    ieee_putcs(false);
    /*
     * Write the publics
     */
    i = 1;
    for (seg = seghead; seg; seg = seg->next) {
        for (pub = seg->pubhead; pub; pub = pub->next) {
            char buf[256];
            ieee_unqualified_name(buf, pub->name);
            ieee_putascii("NI%X,%02X%s.\n", i, strlen(buf), buf);
            if (pub->segment == -1)
                ieee_putascii("ASI%X,R%X,%lX,+.\n", i, pub->index,
                              pub->offset);
            else
                ieee_putascii("ASI%X,%lX,%lX,+.\n", i, pub->segment * 16,
                              pub->offset);
            if (debuginfo) {
                if (pub->type >= 0x100)
                    ieee_putascii("ATI%X,T%X.\n", i, pub->type - 0x100);
                else
                    ieee_putascii("ATI%X,%X.\n", i, pub->type);
            }
            i++;
        }
    }
    pub = fpubhead;
    i = 1;
    while (pub) {
        char buf[256];
        ieee_unqualified_name(buf, pub->name);
        ieee_putascii("NI%X,%02X%s.\n", i, strlen(buf), buf);
        if (pub->segment == -1)
            ieee_putascii("ASI%X,R%X,%lX,+.\n", i, pub->index,
                          pub->offset);
        else
            ieee_putascii("ASI%X,%lX,%lX,+.\n", i, pub->segment * 16,
                          pub->offset);
        if (debuginfo) {
            if (pub->type >= 0x100)
                ieee_putascii("ATI%X,T%X.\n", i, pub->type - 0x100);
            else
                ieee_putascii("ATI%X,%X.\n", i, pub->type);
        }
        i++;
        pub = pub->next;
    }
    /*
     * Write the externals
     */
    ext = exthead;
    i = 1;
    while (ext) {
        char buf[256];
        ieee_unqualified_name(buf, ext->name);
        ieee_putascii("NX%X,%02X%s.\n", i++, strlen(buf), buf);
        ext = ext->next;
    }
    ieee_putcs(false);

    /*
     * IEEE doesn't have a standard pass break record
     * so use the ladsoft variant
     */
    ieee_putascii("CO100,06ENDSYM.\n");

    /*
     * now put types
     */
    i = ARRAY_BOT;
    for (arr = arrhead; arr && debuginfo; arr = arr->next) {
        ieee_putascii("TY%X,20,%X,%lX.\n", i++, arr->basetype,
                      arr->size);
    }
    /*
     * now put locals
     */
    i = 1;
    for (seg = seghead; seg && debuginfo; seg = seg->next) {
        for (loc = seg->lochead; loc; loc = loc->next) {
            char buf[256];
            ieee_unqualified_name(buf, loc->name);
            ieee_putascii("NN%X,%02X%s.\n", i, strlen(buf), buf);
            if (loc->segment == -1)
                ieee_putascii("ASN%X,R%X,%lX,+.\n", i, loc->index,
                              loc->offset);
            else
                ieee_putascii("ASN%X,%lX,%lX,+.\n", i, loc->segment * 16,
                              loc->offset);
            if (debuginfo) {
                if (loc->type >= 0x100)
                    ieee_putascii("ATN%X,T%X.\n", i, loc->type - 0x100);
                else
                    ieee_putascii("ATN%X,%X.\n", i, loc->type);
            }
            i++;
        }
    }

    /*
     *  put out section data;
     */
    seg = seghead;
    if (!debuginfo && !strcmp(seg->name, "??LINE"))
        seg = seg->next;
    while (seg) {
        if (seg->currentpos) {
            int32_t size, org = 0;
            data = seg->data;
            ieee_putascii("SB%X.\n", seg->ieee_index);
            fix = seg->fptr;
            while (fix) {
                size = HUNKSIZE - (org % HUNKSIZE);
                size =
                    size + org >
                    seg->currentpos ? seg->currentpos - org : size;
                size = fix->offset - org > size ? size : fix->offset - org;
                org = ieee_putld(org, org + size, data->data);
                if (org % HUNKSIZE == 0)
                    data = data->next;
                if (org == fix->offset) {
                    org += ieee_putlr(fix);
                    fix = fix->next;
                }
            }
            while (org < seg->currentpos && data) {
                size =
                    seg->currentpos - org >
                    HUNKSIZE ? HUNKSIZE : seg->currentpos - org;
                org = ieee_putld(org, org + size, data->data);
                data = data->next;
            }
            ieee_putcs(false);

        }
        seg = seg->next;
    }
    /*
     * module end record
     */
    ieee_putascii("ME.\n");
}

static void ieee_write_byte(struct ieeeSection *seg, int data)
{
    int temp;
    if (!(temp = seg->currentpos++ % HUNKSIZE))
        ieee_data_new(seg);
    seg->datacurr->data[temp] = data;
}

static void ieee_write_word(struct ieeeSection *seg, int data)
{
    ieee_write_byte(seg, data & 0xFF);
    ieee_write_byte(seg, (data >> 8) & 0xFF);
}

static void ieee_write_dword(struct ieeeSection *seg, int32_t data)
{
    ieee_write_byte(seg, data & 0xFF);
    ieee_write_byte(seg, (data >> 8) & 0xFF);
    ieee_write_byte(seg, (data >> 16) & 0xFF);
    ieee_write_byte(seg, (data >> 24) & 0xFF);
}
static void ieee_putascii(char *format, ...)
{
    char buffer[256];
    int i, l;
    va_list ap;

    va_start(ap, format);
    vsnprintf(buffer, sizeof(buffer), format, ap);
    l = strlen(buffer);
    for (i = 0; i < l; i++)
        if ((uint8_t)buffer[i] > 31)
            checksum += buffer[i];
    va_end(ap);
    fputs(buffer, ofile);
}

/*
 * put out a checksum record */
static void ieee_putcs(int toclear)
{
    if (toclear) {
        ieee_putascii("CS.\n");
    } else {
        checksum += 'C';
        checksum += 'S';
        ieee_putascii("CS%02X.\n", checksum & 127);
    }
    checksum = 0;
}

static int32_t ieee_putld(int32_t start, int32_t end, uint8_t *buf)
{
    int32_t val;
    if (start == end)
        return (start);
    val = start % HUNKSIZE;
    /* fill up multiple lines */
    while (end - start >= LDPERLINE) {
        int i;
        ieee_putascii("LD");
        for (i = 0; i < LDPERLINE; i++) {
            ieee_putascii("%02X", buf[val++]);
            start++;
        }
        ieee_putascii(".\n");
    }
    /* if no partial lines */
    if (start == end)
        return (start);
    /* make a partial line */
    ieee_putascii("LD");
    while (start < end) {
        ieee_putascii("%02X", buf[val++]);
        start++;
    }
    ieee_putascii(".\n");
    return (start);
}
static int32_t ieee_putlr(struct ieeeFixupp *p)
{
/*
 * To deal with the vagaries of segmentation the LADsoft linker
 * defines two types of segments: absolute and virtual.  Note that
 * 'absolute' in this context is a different thing from the IEEE
 * definition of an absolute segment type, which is also supported. If a
 * sement is linked in virtual mode the low limit (L-var) is
 * subtracted from each R,X, and P variable which appears in an
 * expression, so that we can have relative offsets.  Meanwhile
 * in the ABSOLUTE mode this subtraction is not done and
 * so we can use absolute offsets from 0.  In the LADsoft linker
 * this configuration is not done in the assemblker source but in
 * a source the linker reads.  Generally this type of thing only
 * becomes an issue if real mode code is used.  A pure 32-bit linker could
 * get away without defining the virtual mode...
 */
    char buf[40];
    int32_t size = p->size;
    switch (p->ftype) {
    case FT_SEG:
        if (p->id1 < 0)
            sprintf(buf, "%"PRIX32"", -p->id1);
        else
            sprintf(buf, "L%"PRIX32",10,/", p->id1);
        break;
    case FT_OFS:
        sprintf(buf, "R%"PRIX32",%"PRIX32",+", p->id1, p->addend);
        break;
    case FT_REL:
        sprintf(buf, "R%"PRIX32",%"PRIX32",+,P,-,%X,-", p->id1, p->addend, p->size);
        break;

    case FT_WRT:
        if (p->id2 < 0)
            sprintf(buf, "R%"PRIX32",%"PRIX32",+,L%"PRIX32",+,%"PRIX32",-", p->id2, p->addend,
                    p->id2, -p->id1 * 16);
        else
            sprintf(buf, "R%"PRIX32",%"PRIX32",+,L%"PRIX32",+,L%"PRIX32",-", p->id2, p->addend,
                    p->id2, p->id1);
        break;
    case FT_EXT:
        sprintf(buf, "X%"PRIX32"", p->id1);
        break;
    case FT_EXTREL:
        sprintf(buf, "X%"PRIX32",P,-,%"PRIX32",-", p->id1, size);
        break;
    case FT_EXTSEG:
        /* We needed a non-ieee hack here.
         * We introduce the Y variable, which is the low
         * limit of the native segment the extern resides in
         */
        sprintf(buf, "Y%"PRIX32",10,/", p->id1);
        break;
    case FT_EXTWRT:
        if (p->id2 < 0)
            sprintf(buf, "X%"PRIX32",Y%"PRIX32",+,%"PRIX32",-", p->id2, p->id2,
                    -p->id1 * 16);
        else
            sprintf(buf, "X%"PRIX32",Y%"PRIX32",+,L%"PRIX32",-", p->id2, p->id2, p->id1);
        break;
    }
    ieee_putascii("LR(%s,%"PRIX32").\n", buf, size);

    return (size);
}

/* Dump all segment data (text and fixups )*/

static void ieee_unqualified_name(char *dest, char *source)
{
    if (ieee_uppercase) {
        while (*source)
            *dest++ = toupper(*source++);
        *dest = 0;
    } else
        strcpy(dest, source);
}
static void dbgls_init(void)
{
    int tempint;

    fnhead = NULL;
    fntail = &fnhead;
    arrindex = ARRAY_BOT;
    arrhead = NULL;
    arrtail = &arrhead;
    ieee_segment("??LINE", 2, &tempint);
    any_segs = false;
}
static void dbgls_cleanup(void)
{
    struct ieeeSection *segtmp;
    while (fnhead) {
        struct FileName *fntemp = fnhead;
        fnhead = fnhead->next;
        nasm_free(fntemp->name);
        nasm_free(fntemp);
    }
    for (segtmp = seghead; segtmp; segtmp = segtmp->next) {
        while (segtmp->lochead) {
            struct ieeePublic *loctmp = segtmp->lochead;
            segtmp->lochead = loctmp->next;
            nasm_free(loctmp->name);
            nasm_free(loctmp);
        }
    }
    while (arrhead) {
        struct Array *arrtmp = arrhead;
        arrhead = arrhead->next;
        nasm_free(arrtmp);
    }
}

/*
 * because this routine is not bracketed in
 * the main program, this routine will be called even if there
 * is no request for debug info
 * so, we have to make sure the ??LINE segment is avaialbe
 * as the first segment when this debug format is selected
 */
static void dbgls_linnum(const char *lnfname, int32_t lineno, int32_t segto)
{
    struct FileName *fn;
    struct ieeeSection *seg;
    int i = 0;
    if (segto == NO_SEG)
        return;

    /*
     * If `any_segs' is still false, we must define a default
     * segment.
     */
    if (!any_segs) {
        int tempint;            /* ignored */
        if (segto != ieee_segment("__NASMDEFSEG", 2, &tempint))
            nasm_panic("strange segment conditions in OBJ driver");
    }

    /*
     * Find the segment we are targetting.
     */
    for (seg = seghead; seg; seg = seg->next)
        if (seg->index == segto)
            break;
    if (!seg)
        nasm_panic("lineno directed to nonexistent segment?");

    for (fn = fnhead; fn; fn = fn->next) {
        if (!nasm_stricmp(lnfname, fn->name))
            break;
        i++;
    }
    if (!fn) {
        fn = nasm_malloc(sizeof(*fn));
        fn->name = nasm_malloc(strlen(lnfname) + 1);
        fn->index = i;
        strcpy(fn->name, lnfname);
        fn->next = NULL;
        *fntail = fn;
        fntail = &fn->next;
    }
    ieee_write_byte(seghead, fn->index);
    ieee_write_word(seghead, lineno);
    ieee_write_fixup(segto, NO_SEG, seghead, 4, OUT_ADDRESS,
                     seg->currentpos);

}
static void dbgls_deflabel(char *name, int32_t segment,
                           int64_t offset, int is_global, char *special)
{
    struct ieeeSection *seg;

    /* Keep compiler from warning about special */
    (void)special;

    /*
     * Note: ..[^@] special symbols are filtered in labels.c
     */

    /*
     * If it's a special-retry from pass two, discard it.
     */
    if (is_global == 3)
        return;

    /*
     * Case (i):
     */
    if (ieee_seg_needs_update)
        return;
    if (segment < SEG_ABS && segment != NO_SEG && segment % 2)
        return;

    if (segment >= SEG_ABS || segment == NO_SEG) {
        return;
    }

    /*
     * If `any_segs' is still false, we might need to define a
     * default segment, if they're trying to declare a label in
     * `first_seg'.  But the label should exist due to a prior
     * call to ieee_deflabel so we can skip that.
     */

    for (seg = seghead; seg; seg = seg->next)
        if (seg->index == segment) {
            struct ieeePublic *loc;
            /*
             * Case (ii). Maybe MODPUB someday?
             */
            if (!is_global) {
                last_defined = loc = nasm_malloc(sizeof(*loc));
                *seg->loctail = loc;
                seg->loctail = &loc->next;
                loc->next = NULL;
                loc->name = nasm_strdup(name);
                loc->offset = offset;
                loc->segment = -1;
                loc->index = seg->ieee_index;
            }
        }
}
static void dbgls_typevalue(int32_t type)
{
    int elem = TYM_ELEMENTS(type);
    type = TYM_TYPE(type);

    if (!last_defined)
        return;

    switch (type) {
    case TY_BYTE:
        last_defined->type = 1; /* uint8_t */
        break;
    case TY_WORD:
        last_defined->type = 3; /* unsigned word */
        break;
    case TY_DWORD:
        last_defined->type = 5; /* unsigned dword */
        break;
    case TY_FLOAT:
        last_defined->type = 9; /* float */
        break;
    case TY_QWORD:
        last_defined->type = 10;        /* qword */
        break;
    case TY_TBYTE:
        last_defined->type = 11;        /* TBYTE */
        break;
    default:
        last_defined->type = 0x10;      /* near label */
        break;
    }

    if (elem > 1) {
        struct Array *arrtmp = nasm_malloc(sizeof(*arrtmp));
        int vtype = last_defined->type;
        arrtmp->size = elem;
        arrtmp->basetype = vtype;
        arrtmp->next = NULL;
        last_defined->type = arrindex++ + 0x100;
        *arrtail = arrtmp;
        arrtail = &(arrtmp->next);
    }
    last_defined = NULL;
}
static void dbgls_output(int output_type, void *param)
{
    (void)output_type;
    (void)param;
}
static const struct dfmt ladsoft_debug_form = {
    "LADsoft Debug Records",
    "ladsoft",
    dbgls_init,
    dbgls_linnum,
    dbgls_deflabel,
    null_debug_directive,
    dbgls_typevalue,
    dbgls_output,
    dbgls_cleanup,
    NULL                        /* pragma list */
};
static const struct dfmt * const ladsoft_debug_arr[3] = {
    &ladsoft_debug_form,
    &null_debug_form,
    NULL
};
const struct ofmt of_ieee = {
    "IEEE-695 (LADsoft variant) object file format",
    "ieee",
    ".o",
    OFMT_TEXT,
    32,
    ladsoft_debug_arr,
    &ladsoft_debug_form,
    NULL,
    ieee_init,
    null_reset,
    nasm_do_legacy_output,
    ieee_out,
    ieee_deflabel,
    ieee_segment,
    NULL,
    ieee_sectalign,
    ieee_segbase,
    ieee_directive,
    ieee_cleanup,
    NULL                        /* pragma list */
};

#endif                          /* OF_IEEE */
