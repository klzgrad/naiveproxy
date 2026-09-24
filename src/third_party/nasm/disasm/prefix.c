/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2025 The NASM Authors - All Rights Reserved */

#include "disasm.h"
#include "nasmlib.h"

/*
 * Parse various register combinations. Note that this does NOT
 * generate the REX_[BXR][1V] flags, as they are not used by the
 * disassembler.
 */
static uint32_t
xbits(uint32_t val, unsigned int from, unsigned int count, unsigned int to)
{
    const uint32_t tomask = ((UINT32_C(1) << count)-1) << to;

    if (to < from)
        return (val >> (from-to)) & tomask;
    else
        return (val << (to-from)) & tomask;
}

/* ------ EVEX decoding ------ */

#define EVEX_INVERTED 0x087cf000

static uint32_t
evexbits(uint32_t val, unsigned int from, unsigned int count, unsigned int to)
{
    return xbits(val ^ EVEX_INVERTED, from, count, to);
}

static uint32_t evex_rreg(uint32_t evex)
{
    return evexbits(evex,15,1,3) + evexbits(evex,12,1,4);
}
static uint32_t evex_xreg(uint32_t evex, bool xv)
{
    uint32_t x = evexbits(evex,14,1,3);
    x += evexbits(evex,xv ? 27 : 18,1,4);
    return x;
}
static uint32_t evex_breg(uint32_t evex, bool bv)
{
    uint32_t b = evexbits(evex,13,1,3);
    b += evexbits(evex,bv ? 14 : 11,1,4);
    return b;
}
static uint32_t evex_vreg(uint32_t evex, bool xv)
{
    uint32_t v = evexbits(evex,19,4,0);
    if (!xv)
        v += evexbits(evex,27,1,4);
    return v;
}
static uint32_t evex_aaa(uint32_t evex)
{
    return evexbits(evex,24,3,0);
}
static uint32_t evex_z(uint32_t evex)
{
    return evexbits(evex,31,1,0);
}
static uint32_t evex_l(uint32_t evex)
{
    return evexbits(evex,29,2,0);
}
static uint32_t evex_b(uint32_t evex)
{
    return evexbits(evex,28,1,0);
}
static uint32_t evex_nf(uint32_t evex)
{
    return evexbits(evex,26,1,0);
}
static uint32_t evex_scc(uint32_t evex)
{
    return evexbits(evex,24,4,0);
}
static uint32_t evex_dfl(uint32_t evex)
{
    return evexbits(evex,19,4,0);
}
static uint32_t evex_map(uint32_t evex)
{
    return evexbits(evex,8,3,0);
}
static uint32_t evex_w(uint32_t evex)
{
    return evexbits(evex,23,1,0);
}
static uint32_t evex_pp(uint32_t evex)
{
    return evexbits(evex,16,2,0);
}

/* ------ VEX3 decoding ------ */

static uint32_t vex2to3(uint32_t vex2)
{
    return (vex2 & 0x80fe) + ((vex2 & 0x7f00) << 8) + 0x6100;
}

static uint32_t vex3_rreg(uint32_t vex)
{
    return xbits(~vex,15,1,3);
}
static uint32_t vex3_xreg(uint32_t vex)
{
    return xbits(~vex,14,1,3);
}
static uint32_t vex3_breg(uint32_t vex)
{
    return xbits(~vex,13,1,3);
}
static uint32_t vex3_map(uint32_t vex)
{
    return xbits(vex,8,5,0);
}
static uint32_t vex3_vreg(uint32_t vex)
{
    return xbits(~vex,19,4,0);
}
static uint32_t vex3_l(uint32_t vex)
{
    return xbits(vex,18,1,0);
}
static uint32_t vex3_pp(uint32_t vex)
{
    return xbits(vex,16,2,0);
}
static uint32_t vex3_w(uint32_t vex)
{
    return xbits(vex,23,1,0);
}

/* ------ REX2 decoding ------ */

static uint32_t rex2_rreg(uint32_t rex)
{
    return xbits(rex,14,1,4) + xbits(rex,10,1,3);
}
static uint32_t rex2_xreg(uint32_t rex)
{
    return xbits(rex,13,1,4) + xbits(rex,9,1,3);
}
static uint32_t rex2_breg(uint32_t rex)
{
    return xbits(rex,12,1,4) + xbits(rex,8,1,3);
}
static uint32_t rex2_w(uint32_t rex)
{
    return xbits(rex,11,1,0);
}
static uint32_t rex2_map(uint32_t rex)
{
    return xbits(rex,15,1,0);
}

/* ------ REX decoding ------ */

static uint32_t rex_rreg(uint32_t rex)
{
    return xbits(rex,2,1,3);
}
static uint32_t rex_xreg(uint32_t rex)
{
    return xbits(rex,1,1,3);
}
static uint32_t rex_breg(uint32_t rex)
{
    return xbits(rex,0,1,3);
}
static uint32_t rex_w(uint32_t rex)
{
    return xbits(rex,3,1,0);
}

/*
 * Parse an EVEX prefix.
 */
static void parse_evex(struct rexfields *rf, uint32_t val)
{
    rf->type   = REX_EVEX;
    rf->len    = 4;
    rf->raw    = val;
    rf->opc    = (uint8_t)val;

    rf->breg   = evex_breg(val, false);
    rf->bregbv = evex_breg(val, true);
    rf->xreg   = evex_xreg(val, false);
    rf->xregxv = evex_xreg(val, true);
    rf->vreg   = evex_vreg(val, false);
    rf->vregxv = evex_vreg(val, true);

    rf->rreg   = evex_rreg(val);
    rf->map    = evex_map(val);
    rf->xmap   = rf->map + MAP_BASE_EVEX;
    rf->pp     = evex_pp(val);
    rf->l      = evex_l(val);
    rf->w      = evex_w(val);
    rf->z      = evex_z(val);
    rf->b      = evex_b(val);
    rf->nd     = rf->b;
    rf->zu     = rf->b;
    rf->aaa    = evex_aaa(val);
    rf->scc    = evex_scc(val);
    rf->dfl    = evex_dfl(val);
    rf->nf     = evex_nf(val);

    rf->flags  = REX_EV | REX_P | ((~val >> 13) & 7) | (rf->w << 3);
}

/* ------ Set value for all moptypes ------ */

/* case statements for original REX */
#define CASE_REX \
    case 0x40: case 0x41: case 0x42: case 0x43: \
    case 0x44: case 0x45: case 0x46: case 0x47: \
    case 0x48: case 0x49: case 0x4a: case 0x4b: \
    case 0x4c: case 0x4d: case 0x4e: case 0x4f

static const uint8_t *
parse_rex(struct rexfields *rf, uint8_t op, const uint8_t *p)
{
    uint32_t breg = 0;
    uint32_t xreg = 0;
    uint32_t vreg = 0;
    uint32_t val = op;

    rf->opc = op;

    switch (op) {
    CASE_REX:
        rf->type  = REX_REX;
        rf->flags = op;
        rf->len   = 1;
        rf->raw   = op;
        rf->opc   = 0x40;       /* Mask out payload bits */
        breg      = rex_breg(op);
        xreg      = rex_xreg(op);
        rf->rreg  = rex_rreg(op);
        rf->w     = rex_w(op);
        break;

    case 0xd5:
        rf->type  = REX_REX2;
        rf->len   = 2;
        rf->raw   = val = getu16(p);
        rf->flags = REX_2 | REX_P | ((val >> 8) & 15);
        rf->opc   = (uint8_t)val;
        breg      = rex2_breg(val);
        xreg      = rex2_xreg(val);
        rf->rreg  = rex2_rreg(val);
        rf->w     = rex2_w(val);
        rf->map   = rex2_map(val);
        rf->xmap  = rf->map + MAP_BASE_REX2;
        break;

    case 0xc5:
        rf->raw  = val = getu16(p);
        val      = vex2to3(val);
        rf->map  = 1;
        rf->xmap = rf->map + MAP_BASE_VEX;
        rf->len  = 2;
        goto vex_common;

    case 0x8f:
        rf->raw  = val = op + (getu16(p+1) << 8);
        rf->map  = vex3_map(val);
        if (rf->map < 8)
            return NULL;
        rf->xmap = rf->map + MAP_BASE_XOP;
        rf->len  = 3;
        goto vex_common;

    case 0xc4:
        rf->raw  = val = op + (getu16(p+1) << 8);
        rf->map  = vex3_map(val);
        rf->xmap = rf->map + MAP_BASE_VEX;
        rf->len  = 3;
    vex_common:
        rf->type = REX_VEX;
        breg     = vex3_breg(val);
        xreg     = vex3_xreg(val);
        vreg     = vex3_vreg(val);
        rf->rreg = vex3_rreg(val);
        rf->pp   = vex3_pp(val);
        rf->l    = vex3_l(val);
        rf->w    = vex3_w(val);
        rf->flags = REX_P | REX_V | ((~val >> 8) & 7) | (rf->w << 3);
        break;

    case 0x62:
        parse_evex(rf, getu32(p));
        return p + rf->len;

    default:
        return p;               /* Not a prefix */
    }

    rf->bregbv = rf->breg = breg;
    rf->xregxv = rf->xreg = xreg;
    rf->vregxv = rf->vreg = vreg;

    return p + rf->len;
}

/*
 * The buffer must contain at least 20 readable bytes, although the
 * actual values beyond the end of the current valid instruction do
 * not matter. This function returns NULL if the instruction is
 * inherently invalid.
 */
const uint8_t *
parse_prefixes(struct prefix_info *pf, const uint8_t *data, int bits)
{
    bool end_prefix;
    const uint8_t *p = data;
    const uint8_t *maxp;

    nasm_zero(*pf);

    /*
     * The maximum instruction length is 15 bytes; fail in parse_prefixes()
     * unless there is at least one byte left for the actual opcode.
     */
    maxp = p + 15 - 1;

    pf->asize = bits;
    pf->osize = (bits == 64) ? 32 : bits;

    /*
     * WAIT is not really a prefix, but an instruction in its own
     * right.  Only decode it as the very first byte (otherwise
     * prefixes apply to the WAIT instruction, not to anything
     * following it!) so that in case WAIT actually is prefixed with
     * something, those prefixes will be separately emitted by
     * eat_byte().
     *
     * Since WAIT is really an instruction, it doesn't count towards
     * the length of the following instruction, either.
     */

    if (*p == 0x9b) {
        pf->wait = *p++;
        maxp++;                 /* Does not count toward instruction length */
    }

    end_prefix = false;
    while (!end_prefix) {
        uint8_t b = *p;

        switch (b) {
        case 0xf2:
        case 0xf3:
            pf->rep = b;
            pf->rex.pp = b ^ 0xfd; /* F2 = 3, F3 = 2 */
            p++;
            break;

        case 0xf0:
            pf->lock = b;
            p++;
            break;

        case 0x26:
            pf->segover = R_ES;
            goto isseg;
        case 0x2e:
            pf->segover = R_CS;
            goto isseg;
        case 0x36:
            pf->segover = R_SS;
            goto isseg;
        case 0x3e:
            pf->segover = R_DS;
            goto isseg;
        case 0x64:
            pf->segover = R_FS;
            goto isseg;
        case 0x65:
            pf->segover = R_GS;
        isseg:
            pf->seg = b;
            p++;
            break;

        case 0x66:
            pf->osize = (bits == 16) ? 32 : 16;
            pf->osp = b;
            pf->rex.pp = 1;
            p++;
            break;
        case 0x67:
            pf->asize = (bits == 32) ? 16 : 32;
            pf->asp = b;
            p++;
            break;

        CASE_REX:
        case 0xd5:              /* REX2 */
            if (bits == 64)
                p = parse_rex(&pf->rex, b, p);
            end_prefix = true;
            break;

        case 0x8f:              /* XOP */
            if (!(p[1] & 030)) {
                /* Only maps 8-31 valid to protect 8F /0 */
                end_prefix = true;
                break;
            }
            /* fall through */
        case 0xc4:              /* VEX2 */
        case 0xc5:              /* VEX3 */
        case 0x62:              /* EVEX */
            if (bits == 64 || (p[1] & 0xe0) == 0xe0)
                p = parse_rex(&pf->rex, b, p);
            end_prefix = true;
            break;

        default:
            end_prefix = true;
            break;
        }

        if (p > maxp)
            return NULL;        /* Invalid instruction */
    }

    switch (pf->rex.type) {
    case REX_VEX:
    case REX_EVEX:
        if (pf->osp || pf->rep)
            return NULL;        /* Invalid instruction (illegal prefix) */
        break;

    case REX_REX2:
        break;

    case REX_REX:
        /* Redundant REX prefixes are ignored */
        while ((*p & 0xf0) == 0x40) {
            p++;
            if (p > maxp)
                return NULL;
        }
        /* fall through */

    case REX_NONE:
        /*
         * Look for legacy map prefixes. These must come after all
         * possible REX prefixes.
         */
        if (*p == 0x0f) {
            pf->rex.map = 1;
            p++;
            switch (*p) {
            case 0x38:
                pf->rex.map = 2;
                p++;
                break;
            case 0x3a:
                pf->rex.map = 3;
                p++;
                break;
            default:
                break;
            }
        }
        pf->rex.xmap = pf->rex.map + MAP_BASE_NOVEX;
        break;
    }

    if (p > maxp)
        return NULL;

    return p;
}
