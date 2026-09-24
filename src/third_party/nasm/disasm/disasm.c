/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2025 The NASM Authors - All Rights Reserved */

/*
 * disasm.c   where all the _work_ gets done in the Netwide Disassembler
 *
 * See x86/bytecode.txt for the definition of the instruction encoding
 * byte codes.
 */

#include "compiler.h"

#include "disasm.h"
#include "sync.h"
#include "insns.h"
#include "tables.h"
#include "regdis.h"
#include "bytesex.h"
#include "disp8.h"

#define fetch_or_return(_start, _ptr, _size, _need)         \
    fetch_safe(_start, _ptr, _size, _need, return 0)

/*
 * Flags that go into the `segment' field of `operand' structures
 */
#define SEG_RELATIVE    1
#define SEG_RMREG       2
#define SEG_NODISP      4
#define SEG_DISP8       8
#define SEG_DISP16     16
#define SEG_DISP32     32
#define SEG_DISP64     64
#define SEG_DISPMASK  (SEG_NODISP|SEG_DISP8|SEG_DISP16|SEG_DISP32|SEG_DISP64)
#define SEG_SIGNED    128
#define SEG_RMMEM     256
#define SEG_DFV       512
#define SEG_16BIT     (16 << 8)
#define SEG_32BIT     (32 << 8)
#define SEG_64BIT     (64 << 8)
#define SEG_BITMASK   (SEG_16BIT|SEG_32BIT|SEG_64BIT)

/* These get the address size associated with *one particular operand* */
static inline uint32_t seg_set_asize(uint32_t seg, unsigned int asize)
{
    return (seg & ~SEG_BITMASK) | (asize << 8);
}
static inline unsigned int seg_get_asize(uint32_t seg)
{
    return (seg & SEG_BITMASK) >> 8;
}

/*
 * Important: regval must already have been adjusted for rex extensions;
 * the rex flags are only used to determine which 8-bit register decoding
 * to use.
 */
static enum reg_enum whichreg(opflags_t regflags, int regval, uint32_t rex)
{
    /* Unsigned value suitable for array lookup */
    const size_t r = regval;
    size_t i;

    static const struct {
        opflags_t flags;
        const enum reg_enum *regs;
    } regclasses[] = {
        {REG16, nasm_rd_reg16},
        {REG32, nasm_rd_reg32},
        {REG64, nasm_rd_reg64},
        {REG_SREG, nasm_rd_sreg},
        {REG_CREG, nasm_rd_creg},
        {REG_DREG, nasm_rd_dreg},
        {REG_TREG, nasm_rd_treg},
        {FPUREG, nasm_rd_fpureg},
        {MMXREG, nasm_rd_mmxreg},
        {XMMREG, nasm_rd_xmmreg},
        {YMMREG, nasm_rd_ymmreg},
        {ZMMREG, nasm_rd_zmmreg},
        {OPMASKREG, nasm_rd_opmaskreg},
        {BNDREG, nasm_rd_bndreg},
        {TMMREG, nasm_rd_tmmreg},
    };

    /* All the entries below look up regval in 32-entry arrays */
    if (r >= DISREGTBLSZ)
        return 0;

    if (!(regflags & (REGISTER|REGMEM)))
        return 0;        /* Registers not permissible?! */

    if (!(regflags & REG_CLASS_GPR))
        regflags &= ~SIZE_MASK;

    if (is_class(REG_CLASS_GPR|BITS8, regflags)) {
        if (rex & (REX_P|REX_NH))
            return nasm_rd_reg8_rex[r];
        else
            return nasm_rd_reg8[r];
    }

    for (i = 0; i < ARRAY_SIZE(regclasses); i++) {
        enum reg_enum reg = regclasses[i].regs[r];
        if (is_class(regflags, nasm_reg_flags[reg]))
            return reg;
    }

    return 0;                   /* Unknown register type */
}

/*
 * An implicit register operand
 */
static enum reg_enum implicit_reg(opflags_t regflags)
{
#ifndef __WATCOMC__
    switch (regflags) {
    case REG_AL:  return R_AL;
    case REG_AX:  return R_AX;
    case REG_EAX: return R_EAX;
    case REG_RAX: return R_RAX;
    case REG_DL:  return R_DL;
    case REG_DX:  return R_DX;
    case REG_EDX: return R_EDX;
    case REG_RDX: return R_RDX;
    case REG_CL:  return R_CL;
    case REG_CX:  return R_CX;
    case REG_ECX: return R_ECX;
    case REG_RCX: return R_RCX;
    case FPU0:    return R_ST0;
    case XMM0:    return R_XMM0;
    case YMM0:    return R_YMM0;
    case ZMM0:    return R_ZMM0;
    case REG_ES:  return R_ES;
    case REG_CS:  return R_CS;
    case REG_SS:  return R_SS;
    case REG_DS:  return R_DS;
    case REG_FS:  return R_FS;
    case REG_GS:  return R_GS;
    case OPMASK0: return R_K0;
    default:      return 0;
    }
#else
    /* Open Watcom does not support 64-bit constants at *case*. */
    if (regflags == REG_AL)
        return R_AL;
    if (regflags == REG_AX)
        return R_AX;
    if (regflags == REG_EAX)
        return R_EAX;
    if (regflags == REG_RAX)
        return R_RAX;
    if (regflags == REG_DL)
        return R_DL;
    if (regflags == REG_DX)
        return R_DX;
    if (regflags == REG_EDX)
        return R_EDX;
    if (regflags == REG_RDX)
        return R_RDX;
    if (regflags == REG_CL)
        return R_CL;
    if (regflags == REG_CX)
        return R_CX;
    if (regflags == REG_ECX)
        return R_ECX;
    if (regflags == REG_RCX)
        return R_RCX;
    if (regflags == FPU0)
        return R_ST0;
    if (regflags == XMM0)
        return R_XMM0;
    if (regflags == YMM0)
        return R_YMM0;
    if (regflags == ZMM0)
        return R_ZMM0;
    if (regflags == REG_ES)
        return R_ES;
    if (regflags == REG_CS)
        return R_CS;
    if (regflags == REG_SS)
        return R_SS;
    if (regflags == REG_DS)
        return R_DS;
    if (regflags == REG_FS)
        return R_FS;
    if (regflags == REG_GS)
        return R_GS;
    if (regflags == OPMASK0)
        return R_K0;
    return 0;
#endif
}

/*
 * Pick the correct REX.B prefix to add to this base register and
 * get the corresponding register number.
 */
static enum reg_enum whichbreg(opflags_t regflags, int regval, uint32_t rex,
                               const struct prefix_info *pf)
{
    if (regflags & (REG_CLASS_RM_XMM|REG_CLASS_RM_YMM|
                    REG_CLASS_RM_ZMM|REG_CLASS_RM_TMM))
        regval += pf->rex.bregbv;
    else
        regval += pf->rex.breg;

    return whichreg(regflags, regval, rex);
}
static enum reg_enum whichrreg(opflags_t regflags, int regval, uint32_t rex,
                               const struct prefix_info *pf)
{
    return whichreg(regflags, regval + pf->rex.rreg, rex);
}

static uint32_t append_evex_reg_deco(char *buf, uint32_t num,
                                     decoflags_t deco,
                                     const struct prefix_info *pf)
{
    const char * const er_names[] = {"rn-sae", "rd-sae", "ru-sae", "rz-sae"};
    uint32_t num_chars = 0;

    if ((deco & MASK) && pf->rex.aaa) {
        enum reg_enum opmasknum = nasm_rd_opmaskreg[pf->rex.aaa];
        const char * regname = nasm_reg_names[opmasknum - EXPR_REG_START];

        num_chars += snprintf(buf + num_chars, num - num_chars,
                              "{%s}", regname);

        if ((deco & Z) && pf->rex.z) {
            num_chars += snprintf(buf + num_chars, num - num_chars,
                                  "{z}");
        }
    }

    if (pf->rex.b) {
        if (deco & ER) {
            num_chars += snprintf(buf + num_chars, num - num_chars,
                                  ",{%s}", er_names[pf->rex.l]);
        } else if (deco & SAE) {
            num_chars += snprintf(buf + num_chars, num - num_chars,
                                  ",{sae}");
        }
    }

    return num_chars;
}

static uint32_t append_evex_mem_deco(char *buf, uint32_t num, opflags_t type,
                                     decoflags_t deco,
                                     const struct prefix_info *pf)
{
    uint32_t num_chars = 0;

    if (pf->rex.b && (deco & BRDCAST_MASK)) {
        decoflags_t deco_brsize = deco & BRSIZE_MASK;
        opflags_t template_opsize = brsize_to_size(deco_brsize);
        unsigned int br_num = (type & SIZE_MASK) / BITS128 *
            BITS64 / template_opsize * 2;

        num_chars += snprintf(buf + num_chars, num - num_chars,
                              "{1to%d}", br_num);
    }

    if ((deco & MASK) && pf->rex.aaa) {
        enum reg_enum opmasknum = nasm_rd_opmaskreg[pf->rex.aaa];
        const char * regname = nasm_reg_names[opmasknum - EXPR_REG_START];

        num_chars += snprintf(buf + num_chars, num - num_chars,
                              "{%s}", regname);

        if ((deco & Z) && pf->rex.z) {
            num_chars += snprintf(buf + num_chars, num - num_chars,
                                  "{z}");
        }
    }

    return num_chars;
}

/*
 * Process an effective address (ModRM) specification.
 */
static const uint8_t *do_ea(const uint8_t *data, int modrm,
                            int bits, enum ea_type type,
                            operand *op, insn *ins,
                            const struct prefix_info *prefix)
{
    int mod, rm, scale, index, base;
    int asize = ins->addr_size;

    mod = (modrm >> 6) & 03;
    rm = modrm & 07;

    if (mod == 3) {             /* pure register version */
        op->basereg = whichbreg(op->type, rm, ins->rex, prefix);
        if (!op->basereg)
            return NULL;
        op->segment |= SEG_RMREG;
        return data;
    }

    op->disp_size = 0;
    op->eaflags = 0;
    op->segment = seg_set_asize(op->segment, asize) | SEG_RMMEM;

    if (asize == 16) {
        /*
         * <mod> specifies the displacement size (none, byte or
         * word), and <rm> specifies the register combination.
         * Exception: mod=0,rm=6 does not specify [BP] as one might
         * expect, but instead specifies [disp16].
         */

        if (type != EA_SCALAR)
            return NULL;

        op->indexreg = op->basereg = 0;
        op->scale = 1;          /* always, in 16 bits */
        switch (rm) {
        case 0:
            op->basereg = R_BX;
            op->indexreg = R_SI;
            break;
        case 1:
            op->basereg = R_BX;
            op->indexreg = R_DI;
            break;
        case 2:
            op->basereg = R_BP;
            op->indexreg = R_SI;
            break;
        case 3:
            op->basereg = R_BP;
            op->indexreg = R_DI;
            break;
        case 4:
            op->basereg = R_SI;
            break;
        case 5:
            op->basereg = R_DI;
            break;
        case 6:
            op->basereg = R_BP;
            break;
        case 7:
            op->basereg = R_BX;
            break;
        }
        switch (mod) {
        case 0:
            if (rm != 6) {
                op->segment |= SEG_NODISP;
                op->disp_size = 0;
                break;
            } else {
                /* disp16 only, no base register */
                op->basereg = 0;
            }
            /* fall through */
        case 2:
            op->segment |= SEG_DISP16;
            op->disp_size = 16;
            op->offset = gets16(data);
            data += 2;
            break;
        case 1:
            op->segment |= SEG_DISP8;
            op->offset = gets8(data) << get_disp8_shift(ins);
            op->disp_size = 8;
            data++;
            break;
        }
        return data;
    } else {
        /*
         * Once again, <mod> specifies displacement size (this time
         * none, byte or *dword*), while <rm> specifies the base
         * register. Again, [EBP] is missing, replaced by a pure
         * disp32 (this time that's mod=0,rm=*5*) in 32-bit mode,
         * and RIP-relative addressing in 64-bit mode.
         *
         * However, rm=4
         * indicates not a single base register, but instead the
         * presence of a SIB byte...
         */
        const enum reg_enum *regs;
        regs = (asize == 64) ? nasm_rd_reg64 : nasm_rd_reg32;

        op->basereg = op->indexreg = 0;
        base = rm;

        if (rm == 4) {
            /* There is a SIB byte */
            uint8_t sib = *data++;

            scale = (sib >> 6) & 03;
            index = (sib >> 3) & 07;
            base = sib & 07;

            op->scale = 1 << scale;

            if (type != EA_SCALAR) {
                index += prefix->rex.xregxv;

                /* A VSIB always has an index register */
                if (type == EA_XMMVSIB)
                    op->indexreg = nasm_rd_xmmreg[index];
                else if (type == EA_YMMVSIB)
                    op->indexreg = nasm_rd_ymmreg[index];
                else if (type == EA_ZMMVSIB)
                    op->indexreg = nasm_rd_zmmreg[index];
            } else {
                /* Not a VSIB */
                index += prefix->rex.xreg;

                /* ESP/RSP cannot be an index */
                if (index != 4)
                    op->indexreg = regs[index];
            }
        } else {
            /* Can't have VSIB without SIB */
            if (type != EA_SCALAR)
                return NULL;

            base = rm;
        }

        if (base == 5 && mod == 0) {
            /* disp32 without anything else */
            if (rm != 4 && bits == 64) {
                /* If no SIB byte, this is IP-relative in 64-bit mode */
                op->eaflags |= EAF_REL;
                op->segment |= SEG_RELATIVE;
            }
            op->basereg = 0;
            mod = 2;            /* force disp32 */
        } else {
            /* "Normal" base encoding */
            op->basereg = regs[base + prefix->rex.breg];
        }

        switch (mod) {
        case 0:
            op->segment |= SEG_NODISP;
            op->disp_size = 0;
            break;
        case 1:
            op->segment |= SEG_DISP8;
            op->disp_size = 8;
            op->offset = gets8(data) << get_disp8_shift(ins);
            data++;
            break;
        case 2:
            op->segment |= SEG_DISP32;
            op->disp_size = 32;
            op->offset = gets32(data);
            data += 4;
            break;
        }
        return data;
    }
}

/*
 * Return true if the following byte is a modr/m byte which is a memory
 * encoding (mod != 3).
 */
static bool is_mem_modrm(const uint8_t *bytecode, const uint8_t *data)
{
    uint8_t b = *bytecode & 0344;

    if (b == 0100 || b == 0200)
        return (*data & 0300) != 0300;
    else
        return false;           /* Not a modr/m per instruction template */
}

/*
 * Determine whether the instruction template in ins->itemp
 * corresponds to the data stream in data and parse it into "ins" if
 * so.  "ins" has already been partially initialized in disasm() to
 * point contain the address of the instruction (loc.offset), the mode
 * (bits), and the desired template (itemp); all other fields must be
 * initialized to zero.
 *
 * The "data" pointer should point to the first byte after parsed
 * prefixes (after call to parse_prefixes()), and must have enough
 * slack buffer space that the longest template cannot overrun the
 * data buffer, although the content beyond the end of any valid
 * instruction is allowed to be garbage.
 */
#define case4(x) case (x): case (x)+1: case (x)+2: case (x)+3

static int matches(const uint8_t *data, const struct prefix_info *prefix,
                   insn *ins)
{
    const struct itemplate * const t = ins->itemp;
    const uint8_t *r = t->code;
    const uint8_t * const origdata = data; /* First byte after prefixes */
    bool a_used = false, o_used = false;
    enum prefixes drep  = 0;
    enum prefixes dwait = 0;
    enum prefixes dlock = prefix->lock ? P_LOCK : 0;
    int osize = prefix->osize;
    int asize = prefix->asize;
    const int bits = ins->bits;
    const int defosize = (bits == 16) ? 16 : 32;
    int i, c;
    int op1, op2;
    struct operand *opx, *opy;
    uint8_t opex = 0;
    uint8_t regmask = (bits == 64) ? 31 : 7;
    enum ea_type eat = EA_SCALAR;
    decoflags_t decoflags = 0;

    if (bits == 64 && prefix->rex.w &&
        (prefix->rex.type < REX_VEX || itemp_has(t, IF_WW)))
        ins->op_size = osize = 64;

    ins->addr_size  = asize;
    ins->op_size    = osize;
    ins->itemp      = t;
    ins->opcode     = t->opcode;
    ins->operands   = t->operands;

    for (i = 0; i < ins->operands; i++) {
        struct operand *o = &ins->oprs[i];
        o->type  = t->opd[i];
        o->opidx = i;
        if (is_class(IMMEDIATE, t->opd[i])) {
            o->segment = seg_set_asize(bits, bits);
        } else if (is_class(MEM_OFFS, t->opd[i])) {
            a_used = true;
            o->segment = seg_set_asize(bits, asize) | SEG_RMMEM;
        } else {
            o->segment = seg_set_asize(bits, asize);
        }
        decoflags |= t->deco[i];
    }

    /* Decorators are only possible with EVEX */
    if (decoflags && prefix->rex.type != REX_EVEX)
        return 0;

    ins->rex = prefix->rex.flags;

    if (itemp_has(t, (bits == 64 ? IF_NOLONG : IF_LONG)))
        return 0;

    if (prefix->rep == 0xF2)
        drep = (itemp_has(t, IF_BND) ? P_BND : P_REPNE);
    else if (prefix->rep == 0xF3)
        drep = P_REP;

    dwait = prefix->wait ? P_WAIT : 0;

    while ((c = *r++) != 0) {
        op1 = (c & 3) + ((opex & 1) << 2);
        op2 = ((c >> 3) & 3) + ((opex & 2) << 1);
        opx = &ins->oprs[op1];
        opy = &ins->oprs[op2];
        opex = 0;

        switch (c) {
        case 01:
        case 02:
        case 03:
        case 04:
            while (c--)
                if (*r++ != *data++)
                    return 0;
            break;

        case 05:
        case 06:
        case 07:
            opex = c;
            break;

        case4(010):
        {
            int t = *r++, d = *data++;
            if (d < t || d > t + 7)
                return 0;
            else {
                opx->basereg = whichbreg(opx->type, d-t, ins->rex, prefix);
                opx->segment |= SEG_RMREG;
            }
            break;
        }

        case4(014):
            /* this is an separate index reg position of MIB operand (ICC) */
            /* Disassembler uses NASM's split EA form only                 */
            break;

        case4(0274):
            opx->offset = (int8_t)*data++;
            opx->disp_size = 8;
            opx->segment |= SEG_SIGNED;
            break;

        case4(020):
            opx->offset = *data++;
            opx->disp_size = 8;
            break;

        case4(024):
            opx->offset = *data++;
            opx->disp_size = 8;
            break;

        case4(030):
            opx->offset = getu16(data);
            opx->disp_size = 16;
            data += 2;
            break;

        case4(034):
            if (osize == 32) {
                opx->offset = getu32(data);
                opx->disp_size = 32;
                data += 4;
            } else {
                opx->offset = getu16(data);
                opx->disp_size = 16;
                data += 2;
            }
            break;

        case4(040):
            opx->offset = getu32(data);
            opx->disp_size = 32;
            data += 4;
            break;

        case4(0254):
            opx->offset = gets32(data);
            opx->disp_size = 32;
            data += 4;
            break;

        case4(044):
            opx->disp_size = asize;
            switch (asize) {
            case 16:
                opx->offset = getu16(data);
                data += 2;
                break;
            case 32:
                opx->offset = getu32(data);
                data += 4;
                break;
            case 64:
                opx->offset = getu64(data);
                data += 8;
                break;
            }
            break;

        case4(050):
            opx->offset = gets8(data++);
            opx->disp_size = 8;
            opx->segment |= SEG_RELATIVE;
            break;

        case4(054):
            opx->offset = getu64(data);
            opx->disp_size = 64;
            data += 8;
            break;

        case4(060):
            opx->offset = gets16(data);
            opx->disp_size = 16;
            data += 2;
            opx->segment = seg_set_asize(opx->segment, 16) | SEG_RELATIVE;
            break;

        case4(064):  /* rel */
            if (bits == 64) {
                /*
                 * In long mode rel is always 32 bits, sign extended.
                 * REX.W or 66 prefixes have no effect.
                 */
                opx->offset = gets32(data);
                opx->disp_size = 32;
                data += 4;
                opx->segment = seg_set_asize(opx->segment, 64) | SEG_RELATIVE;
            } else if (osize == 32) {
                opx->offset = gets32(data);
                opx->disp_size = 32;
                data += 4;
                opx->segment = seg_set_asize(opx->segment, 32) | SEG_RELATIVE;
            } else {
                opx->offset = gets16(data);
                opx->disp_size = 16;
                data += 2;
                opx->segment = seg_set_asize(opx->segment, 16) | SEG_RELATIVE;
            }
            break;

        case4(070):
            opx->offset = gets32(data);
            opx->disp_size = 32;
            data += 4;
            opx->disp_size = 32;
            opx->segment = seg_set_asize(opx->segment, bits == 64 ? 64 : 32)
                | SEG_RELATIVE;
            break;

        case4(0100):
        case4(0110):
        case4(0120):
        case4(0130):
        {
            int modrm = *data++;
            data = do_ea(data, modrm, bits, eat, opy, ins, prefix);
            if (!data)
                return 0;
            a_used |= !!(opy->segment & SEG_RMMEM);
            opx->basereg = whichrreg(opx->type, (modrm >> 3) & 7,
                                     ins->rex, prefix);
            if (!opx->basereg)
                return 0;
            opx->segment |= SEG_RMREG;
            break;
        }

        case 0171:
        {
            uint8_t t = *r++;
            uint8_t d = *data++;
            if ((d ^ t) & ~070) {
                return 0;
            } else {
                op2 = (op2 & ~3) | ((t >> 3) & 3);
                opy = &ins->oprs[op2];
                opy->basereg = whichrreg(opy->type, (d >> 3) & 7,
                                         ins->rex, prefix);
                if (!opy->basereg)
                    return 0;
                opy->segment |= SEG_RMREG;
            }
            break;
        }

        case 0172:
            c = *r++;

            opx = &ins->oprs[(c >> 3) & 7];
            ins->oprs[c & 7].offset = *data & 7;
            goto do_is4;

        case 0173:
            c = *r++;
            if ((c ^ *data) & 7)
                return 0;

            opx = &ins->oprs[(c >> 4) & 7];
            goto do_is4;

        case4(0174):
        do_is4:
        {
            uint8_t ximm = *data++;
            unsigned int nreg = ((ximm >> 4) + ((ximm & 8) << 1)) & regmask;
            opx->basereg = whichbreg(opx->type, nreg, ins->rex, prefix);
            if (!opx->basereg)
                return 0;
            opx->segment |= SEG_RMREG;
            break;
        }

        case4(0200):
        case4(0204):
        case4(0210):
        case4(0214):
        case4(0220):
        case4(0224):
        case4(0230):
        case4(0234):
        {
            int modrm = *data++;
            if (((modrm >> 3) & 07) != (c & 07))
                return 0;   /* spare field doesn't match up */
            data = do_ea(data, modrm, bits, eat, opy, ins, prefix);
            if (!data)
                return 0;
            a_used |= !!(opy->segment & SEG_RMMEM);
            break;
        }

        case4(0240):
        case 0250:
        {
            uint32_t evextemp, mismatch, matchmask;
            bool memop;
            unsigned int vmask;

            if (prefix->rex.type != REX_EVEX)
                return 0;

            ins->rex |= REX_EV;

            /* Get the evex prefix expected by the template */
            evextemp = (getu32(r-1) & ~0xff) + 0x62; r += 3;
            mismatch = prefix->rex.raw ^ evextemp;
            memop = is_mem_modrm(r, data+1);

            /* Now mask out "mismatched" bits that are proper parameters */
            matchmask = ~(EVEX_B4|EVEX_R4|EVEX_B3|EVEX_X3|EVEX_R3|EVEX_X4);

            if (c != 0250 || (memop && !itemp_has(t, IF_SCC))) {
                /*
                 * V4 unless X is a is a vector (which is only
                 * possible if this is a memory operation), in which
                 * case this is X4.  However, X4 is explicitly
                 * forbidden if this is not a memory operation,
                 * otherwise this bit is supposedly ignored if not
                 * used.
                 */
                matchmask &= ~EVEX_V4; /* Either V4 or X4 */
            }

            if (c == 0250) {
                vmask = 0;      /* No V register */
            } else if (is_class(IMMEDIATE, opx->type)) {
                vmask = 15;     /* V operand is used as immediate */
                opx->offset = (mismatch >> 19) & vmask;
                if (itemp_has(t, IF_DFV))
                    opx->segment |= SEG_DFV;
            } else {
                unsigned int regnum;
                vmask = regmask;

                opx->segment |= SEG_RMREG;
                if (eat >= EA_XMMVSIB)
                    regnum = prefix->rex.vregxv;
                else
                    regnum = prefix->rex.vreg;

                opx->basereg = whichreg(opx->type, regnum & vmask, ins->rex);
                if (!opx->basereg)
                    return 0;
            }

            matchmask &= ~((vmask & 15) << 19); /* VVVV field */

            if (prefix->rex.w)
                ins->rex |= REX_W;

            if (itemp_has(t, IF_WIG)) {
                ins->rex &= ~REX_W;
                ins->rex |=  REX_NW;
                matchmask &= ~EVEX_W;
            }

            if (itemp_has(t, IF_LIG) ||
                (prefix->rex.b && !memop)) /* LL used for rounding control */
                matchmask &= ~EVEX_LL;

            if (decoflags & MASK)
                mismatch &= ~EVEX_AAA;

            if (decoflags & Z)
                matchmask &= ~EVEX_Z;

            if (itemp_has(t, IF_NF_E)) {
                matchmask &= ~EVEX_NF;
                if (prefix->rex.nf)
                    ins->prefixes[PPS_NF] = P_NF;
            }

            if (itemp_has(t, IF_ZU_E)) {
                matchmask &= ~EVEX_ZU;
                if (prefix->rex.zu)
                    ins->prefixes[PPS_ZU] = P_ZU;
            }
#if 0
            if (mismatch & matchmask) {
                extern const struct itemplate instrux[];

                printf("EVEX mismatch: %4zd: raw %08x template %08x mask %08x -> %08x\n",
                       t - instrux, prefix->rex.raw, evextemp, matchmask,
                       mismatch & matchmask);
            }
#endif

            if (mismatch & matchmask)
                return 0;

            ins->evex_tuple = *r++ - 0300;

            if (itemp_has(t, IF_NF_E) && prefix->rex.nf)
                ins->prefixes[PPS_NF] = P_NF;

            if (itemp_has(t, IF_ZU_E) && prefix->rex.zu)
                ins->prefixes[PPS_ZU] = P_ZU;
            break;
        }

        case4(0260):
        case 0270:
        {
            uint8_t vexm   = *r++;
            uint8_t vexwlp = *r++;

            if (prefix->rex.type != REX_VEX)
                return 0;

            ins->rex |= REX_V;

            if ((vexm & 0x1f) != prefix->rex.map)
                return 0;

            if (!itemp_has(t, IF_WIG)) {
                if (prefix->rex.w != !!(vexwlp & 0x80))
                    return 0;
            }

            if ((vexwlp & 3) != prefix->rex.pp)
                return 0;

            if (!itemp_has(t, IF_LIG)) {
                if (((vexwlp >> 2) & 3) != prefix->rex.l)
                    return 0;
            }

            if (c == 0270) {
                if (prefix->rex.vreg != 0)
                    return 0;
            } else {
                opx->segment |= SEG_RMREG;
                opx->basereg =
                    whichreg(opx->type, prefix->rex.vreg, ins->rex);
                if (!opx->basereg)
                    return 0;
            }
            break;
        }

        case 0271:
            if (prefix->rep == 0xF3)
                drep = P_XRELEASE;
            break;

        case 0272:
            if (prefix->rep == 0xF2)
                drep = P_XACQUIRE;
            else if (prefix->rep == 0xF3)
                drep = P_XRELEASE;
            break;

        case 0273:
            if (prefix->lock == 0xF0) {
                if (prefix->rep == 0xF2)
                    drep = P_XACQUIRE;
                else if (prefix->rep == 0xF3)
                    drep = P_XRELEASE;
            }
            break;

        case 0310:
            if (asize != 16)
                return 0;
            else
                a_used = true;
            break;

        case 0311:
            if (asize != 32)
                return 0;
            else
                a_used = true;
            break;

        case 0312:
            if (asize != bits)
                return 0;
            else
                a_used = true;
            break;

        case 0313:
            if (asize != 64)
                return 0;
            else
                a_used = true;
            break;

        case 0314:
            if (prefix->rex.flags & REX_B)
                return 0;
            break;

        case 0315:
            if (prefix->rex.flags & REX_X)
                return 0;
            break;

        case 0316:
            if (prefix->rex.flags & REX_R)
                return 0;
            break;

        case 0317:
            if (prefix->rex.flags & REX_W)
                return 0;
            break;

        case 0320:
            if (ins->rex & (REX_V | REX_EV))
                osize = 16;
            else if (osize != 16)
                return 0;
            o_used = true;
            break;

        case 0321:
            if (ins->rex & (REX_V | REX_EV))
                osize = 32;
            else if (osize != 32)
                return 0;
            o_used = true;
            break;

        case 0322:
            if (ins->rex & (REX_V | REX_EV))
                osize = defosize;
            else if (osize != defosize)
                return 0;
            o_used = true;
            break;

        case 0327:
            if (bits != 64)
                break;
            /* else fall through */

        case 0323:
            /* No REX.W required for 64-bit osize */
            if (osize != 16)
                ins->op_size = osize = 64;
            ins->rex |= REX_NW;
            break;

        case 0324:
            if (osize != 64)
                return 0;
            o_used = true;
            break;

        case 0325:
            ins->rex |= REX_NH;
            break;

        case 0326:
            if (prefix->rep == 0xF3)
                return 0;
            break;

        case 0330:
            break;

        case 0331:
            if (prefix->rep)
                return 0;
            break;

        case 0332:
            if (prefix->rep != 0xF2)
                return 0;
            drep = 0;
            break;

        case 0333:
            if (prefix->rep != 0xF3)
                return 0;
            drep = 0;
            break;

        case 0334:
            if (bits != 64 && prefix->lock) {
                ins->rex |= REX_R;
                dlock = 0;
            }
            break;

        case 0335:
            if (drep == P_REP)
                drep = P_REPE;
            break;

        case 0336:
        case 0337:
            break;

        case 0340:
            return 0;

        case 0341:
            if (prefix->wait != 0x9B)
                return 0;
            dwait = 0;
            break;

        case 0342:
            if (osize != bits)
                return 0;
            o_used = true;
            break;

        case 0344:
            ins->rex |= REX_B;
            break;

        case 0345:
            ins->rex |= REX_X;
            break;

        case 0346:
            ins->rex |= REX_R;
            break;

        case 0347:
            ins->rex |= REX_W;
            break;

        case 0350:
            if (prefix->rex.type != REX_REX2)
                return 0;
            ins->rex |= REX_2;
            break;

        case 0351:
            if (prefix->rex.type != REX_REX2)
                return 0;
            ins->rex |= REX_2 | REX_W;
            break;

        case 0355:
        case 0356:
        case 0357:
            nasm_assert(prefix->rex.map == c - 0354);
            break;


        case 0360:
            if (prefix->osp || prefix->rep)
                return 0;
            break;

        case 0361:
            if (!prefix->osp || prefix->rep)
                return 0;
            o_used = true;
            break;

        case 0364:
            if (prefix->osp)
                return 0;
            break;

        case 0365:
            if (prefix->asp)
                return 0;
            break;

        case 0366:
            if (!prefix->osp)
                return 0;
            o_used = true;
            break;

        case 0367:
            if (!prefix->asp)
                return 0;
            a_used = true;
            break;

        case 0370:
        case 0371:
            break;

        case 0374:
            eat = EA_XMMVSIB;
            break;

        case 0375:
            eat = EA_YMMVSIB;
            break;

        case 0376:
            eat = EA_ZMMVSIB;
            break;

        default:
            fprintf(stderr, "ndisasm: unknown byte code: 0%03o\n", c);
            return 0;    /* Unknown code */
        }
    }

    /*
     * VEX or EVEX encoding in byte code, but no such prefix present,
     * or vice versa?
     */
    if ((prefix->rex.flags ^ ins->rex) & (REX_V | REX_EV))
        return 0;

    /* Required REX bits not present? */
    if ((ins->rex & ~prefix->rex.flags) &
        (REX_2 | REX_P | REX_B | REX_X | REX_R | REX_W))
        return 0;

    /*
     * Check for unused rep or a/o prefixes.
     */
    if (dlock) {
        if ((ins->rex & (REX_V | REX_EV)) || !itemp_has(t, IF_LOCK))
            return 0;           /* Instruction not lockable */
        ins->prefixes[PPS_LOCK] = dlock;
    }
    if (drep) {
        if (ins->prefixes[PPS_REP])
            return 0;
        ins->prefixes[PPS_REP] = drep;
    }
    ins->prefixes[PPS_WAIT] = dwait;

    if (prefix->rex.w && osize == 64 && (ins->rex & REX_NW))
        o_used = false;

    if (!o_used) {
        enum prefixes pfx = 0;

        if (prefix->osp) {
            if (osize == 16 && bits != 16)
                pfx = P_O16;
            else if (osize == 32 && bits == 16)
                pfx = P_O32;
            else
                pfx = P_OSP;
        } else if (prefix->rex.w && osize == 64) {
            pfx = P_O64;
        }

        if (ins->prefixes[PPS_OSIZE])
            return 0;
        ins->prefixes[PPS_OSIZE] = pfx;
    }

    if (itemp_has(t, IF_JCC_HINT)) {
        if ((prefix->seg & ~0x10) == 0x2e)
            ins->prefixes[PPS_SEG] = prefix->seg & 0x10 ? P_PT : P_PN;
    }

    if (!a_used) {
        /* Emit any possible segment override prefix explicitly */
        if (!ins->prefixes[PPS_SEG])
            ins->prefixes[PPS_SEG] = prefix->segover;

        if (prefix->asp) {
            if (ins->prefixes[PPS_ASIZE])
                return 0;
            ins->prefixes[PPS_ASIZE] = asize == 16 ? P_A16 : P_A32;
        }
    }

    if (itemp_has(t, IF_NF_R))
        ins->prefixes[PPS_NF] = P_NF;
    if (itemp_has(t, IF_ZU_R))
        ins->prefixes[PPS_ZU] = P_ZU;

    if (itemp_has(t, IF_LATEVEX) && prefix->rex.type == REX_VEX) {
            ins->prefixes[PPS_REX] = P_VEX;
    }

    if ((prefix->rex.raw & 0x807fff) == 0x0061c4) {
        /* A VEX3-prefixed instruction which could have been VEX2-encoded */
        ins->prefixes[PPS_REX] = P_VEX3;
    }

    /* Final fixup of operands */
    for (i = 0; i < ins->operands; i++) {
        struct operand *o = &ins->oprs[i];
        if (o->segment & SEG_RMREG) {
            o->type = nasm_reg_flags[o->basereg];
        } else if (o->segment & SEG_RMMEM) {
            o->type &= ~(REGISTER | IMMEDIATE);
            o->type |= MEMORY;
        } else if (is_class(REGISTER, o->type) && !o->basereg) {
            /* An implicit register operand? */
           o->basereg = implicit_reg(o->type);
            if (!o->basereg)
                return 0;
            o->segment |= SEG_RMREG;
        }
    }
    return data - origdata;
}

static const char *regname(enum reg_enum reg)
{
    if (!is_register(reg))
        return "";

    return nasm_reg_names[reg - EXPR_REG_START];
}

/*
 * If there is a SM indicator in the instruction template, then:
 * - If any of the operands is a sized register, remove sizes from
 *   any other size-matched non-register operands;
 * - Otherwise remove the sizes from all but the first such operand.
 */
static void remove_redundant_sizes(insn *ins)
{
    unsigned int smmask = itemp_smx(ins->itemp);
    int i;
    opflags_t sized = 0;

    for (i = 0; i < ins->operands; i++) {
        const struct operand *o = &ins->oprs[i];

        if (!(smmask & (1 << i)))
            continue;

        if (o->type & REGISTER)
            sized |= o->type & SIZE_MASK;
    }

    for (i = 0; i < ins->operands; i++) {
        struct operand *o = &ins->oprs[i];

        /* This is true if more than one bit is set in "sized" */
        if (sized & (sized-1))
            break;              /* How the heck did this even match? */

        if (!(smmask & (1 << i)))
            continue;

        /* True if exactly 0 or 1 bits set in "sized" */
        if ((o->type & SIZE_MASK) == sized)
            o->type -= sized;   /* Remove size */
        else
            sized |= o->type & SIZE_MASK;
    }
}

int32_t disasm(const uint8_t *dp, int32_t data_size,
               char *output, int outbufsize,
               int bits, int64_t offset, int autosync,
               iflag_t *prefer)
{
    const struct itemplate * const * const *ix;
    const struct itemplate * const *p;
    const struct itemplate *itemp, *best_itemp;
    int length, best_length = 0;
    int maxlen = 15;
    int i, slen;
    char separator;
    const uint8_t *origdata = dp;
    int works;
    insn ins;
    iflag_t goodness, best;
    int best_pref;
    struct prefix_info prefix;

    /*
     * Scan for prefixes.
     */
    dp = parse_prefixes(&prefix, dp, bits);
    if (!dp)                    /* Invalid or no space for even one opcode */
        return 0;

    /*
     * WAIT is not really a prefix, it is an instruction,
     * so it doesn't count toward the maximum instruction
     * length. See comment in rexdis.c.
     */
    if (prefix.wait)
        maxlen++;

    maxlen -= dp - origdata;    /* Prefixes count toward instruction length */

    iflag_set_all(&best); /* Worst possible */
    best_itemp = NULL;
    best_pref = INT_MAX;

    ix = ndisasm_itable[prefix.rex.xmap];
    if (!ix)
        return 0;        /* Instruction map does not exist */

    fetch_or_return(origdata, dp, data_size, 1);
    p = ix[*dp];
    /* dp now points to the primary opcode byte */
    if (!p)
        return 0;               /* No instructions for this opcode */

    nasm_zero(ins);
    while ((itemp = *p++)) {
        insn tmp_ins;

        nasm_zero(tmp_ins);
        tmp_ins.loc.offset = offset;
        tmp_ins.bits       = bits;
        tmp_ins.itemp      = itemp;

        if ((length = matches(dp, &prefix, &tmp_ins))) {
            if (length > maxlen)
                continue;       /* Instruction too long */

            works = true;

            /*
             * Final check to make sure the operand types are valid
             */
            for (i = 0; i < tmp_ins.operands; i++) {
                opflags_t tt = itemp->opd[i];
                opflags_t it = tmp_ins.oprs[i].type;

                /* Strip flags that are not applicable to disassembler matching */
                if (!is_class(REG_GPR, it))
                    tt &= ~SIZE_MASK;
                if (is_class(IMMEDIATE, tt))
                    tt &= ~SUBCLASS_MASK;

                if (!is_class(tt, it)) {
#if 0
                    bool is_reg = !!(tmp_ins.oprs[i].segment & SEG_RMREG);
                    printf("flags 0x%lx%s%s do not match template 0x%lx\n",
                            it,
                            is_reg ? " for register " : "",
                            is_reg ? regname(tmp_ins.oprs[i].basereg) : "",
                            tt);
#endif
                    works = false;
                    break;
                }
            }

            /*
             * Note: we always prefer instructions which incorporate
             * prefixes in the instructions themselves.  This is to allow
             * e.g. PAUSE to be preferred to REP NOP, and deal with
             * MMX/SSE instructions where prefixes are used to select
             * between MMX and SSE register sets or outright opcode
             * selection.
             */
            if (works) {
                int i, nprefix;
                goodness = iflag_pfmask(itemp);
                goodness = iflag_xor(&goodness, prefer);
		nprefix = 0;
		for (i = 0; i < MAXPREFIX; i++)
		    if (tmp_ins.prefixes[i])
			nprefix++;
                if (nprefix < best_pref ||
		    (nprefix == best_pref &&
                     iflag_cmp(&goodness, &best) < 0)) {
                    /* This is the best one found so far */
                    best = goodness;
                    best_itemp = itemp;
                    best_pref = nprefix;
                    best_length = length;
                    ins = tmp_ins;
                }

                if (itemp_has(itemp, IF_BESTDIS))
                    break;      /* Don't search any further */
            }
        }
    }

    if (!best_itemp)
        return 0;               /* no instruction was matched */

    /* Pick the best match */
    itemp = best_itemp;
    length = best_length;

    slen = 0;

    for (i = 0; i < MAXPREFIX; i++) {
        const char *pfx = prefix_name(ins.prefixes[i]);
        if (pfx)
            slen += snprintf(output+slen, outbufsize-slen, "%s ", pfx);
    }

    slen += snprintf(output + slen, outbufsize - slen, "%s",
                     nasm_insn_names[ins.opcode]);

    remove_redundant_sizes(&ins);

    separator = ' ';
    length += dp - origdata;  /* fix up for prefixes */
    for (i = 0; i < ins.operands; i++) {
        decoflags_t deco = itemp->deco[i];
        const operand *o = &ins.oprs[i];
        opflags_t t = o->type;
        int64_t offs = o->offset;
        int asize = seg_get_asize(o->segment);
        int nasize = 64 - asize; /* Address bits to mask off */

        output[slen++] = separator;

        if (o->segment & SEG_RELATIVE) {
            /*
             * sort out wraparound
             */
            offs += ins.loc.offset + length;
            offs = (uint64_t)offs << nasize >> nasize;
            if ((t & (IMMEDIATE|SIZE_MASK)) == IMMEDIATE) {
                if (asize != bits) {
                    switch (asize) {
                    case 16:
                        t |= BITS16;
                        break;
                    case 32:
                        t |= BITS32;
                        break;
                    case 64:
                        t |= BITS64;
                        break;
                    }
                }
            }

            /*
             * add sync marker, if autosync is on
             */
            if (autosync)
                add_sync(offs, 0L);
        }

        separator = (t & COLON) ? ':' : ',';

        if ((t & (REGISTER | FPUREG)) ||
                (o->segment & SEG_RMREG)) {
            enum reg_enum reg = o->basereg;
            if (t & TO)
                slen += snprintf(output + slen, outbufsize - slen, "to ");
            slen += snprintf(output + slen, outbufsize - slen, "%s",
                             regname(reg));
            if (t & REGSET_MASK)
                slen += snprintf(output + slen, outbufsize - slen, "+%d",
                                 (int)((t & REGSET_MASK) >> (REGSET_SHIFT-1))-1);
            if (deco)
                slen += append_evex_reg_deco(output + slen, outbufsize - slen,
                                             deco, &prefix);
        } else if (t & IMMEDIATE) {
            if (is_class(t, UNITY)) {
                output[slen++] = '1';
            } else if (o->segment & SEG_DFV) {
                int fl;
                static const char dfv_flags[] = "czso";
                const char *flsep = "";
                memcpy(&output[slen], "{dfv=", 5);
                slen += 5;
                for (fl = 0; fl < 4; fl++) {
                    if (offs & (1 << fl)) {
                        slen += snprintf(output + slen, outbufsize - slen,
                                         "%s%cf", flsep, dfv_flags[fl]);
                        flsep = ",";
                    }
                }
                output[slen++] = '}';
                separator = ' '; /* No comma after dfv */
            } else {
                /* Immediates that will actually be printed as numbers */

                if (o->segment & SEG_RELATIVE) {
                    /* Don't print any sizes for near jump targets */
                } else if (t & BITS8) {
                    slen +=
                        snprintf(output + slen, outbufsize - slen, "byte ");
                    if (o->segment & SEG_SIGNED) {
                        if (offs < 0) {
                            offs = -offs;
                            output[slen++] = '-';
                        } else {
                            output[slen++] = '+';
                        }
                    }
                } else if (t & BITS16) {
                    slen +=
                        snprintf(output + slen, outbufsize - slen, "word ");
                } else if (t & BITS32) {
                    slen +=
                        snprintf(output + slen, outbufsize - slen, "dword ");
                } else if (t & BITS64) {
                    slen +=
                        snprintf(output + slen, outbufsize - slen, "qword ");
                } else if (t & NEAR) {
                    slen +=
                        snprintf(output + slen, outbufsize - slen, "near ");
                } else if (t & SHORT) {
                    slen +=
                        snprintf(output + slen, outbufsize - slen, "short ");
                }
                slen +=
                    snprintf(output + slen, outbufsize - slen, "0x%"PRIx64"",
                             offs);
            }
        } else if (is_class(REGMEM, t)) {
            int started = false;

            if (t & BITS8)
                slen +=
                    snprintf(output + slen, outbufsize - slen, "byte ");
            if (t & BITS16)
                slen +=
                    snprintf(output + slen, outbufsize - slen, "word ");
            if (t & BITS32)
                slen +=
                    snprintf(output + slen, outbufsize - slen, "dword ");
            if (t & BITS64)
                slen +=
                    snprintf(output + slen, outbufsize - slen, "qword ");
            if (t & BITS80)
                slen +=
                    snprintf(output + slen, outbufsize - slen, "tword ");
            if (prefix.rex.b && (deco & BRDCAST_MASK)) {
                /* when broadcasting, each element size should be used */
                if (deco & BR_BITS16)
                    slen +=
                        snprintf(output + slen, outbufsize - slen, "word ");
                else if (deco & BR_BITS32)
                    slen +=
                        snprintf(output + slen, outbufsize - slen, "dword ");
                else if (deco & BR_BITS64)
                    slen +=
                        snprintf(output + slen, outbufsize - slen, "qword ");
            } else {
                if (t & BITS128)
                    slen +=
                        snprintf(output + slen, outbufsize - slen, "oword ");
                if (t & BITS256)
                    slen +=
                        snprintf(output + slen, outbufsize - slen, "yword ");
                if (t & BITS512)
                    slen +=
                        snprintf(output + slen, outbufsize - slen, "zword ");
            }
            if (t & FAR)
                slen += snprintf(output + slen, outbufsize - slen, "far ");
            if (t & NEAR)
                slen +=
                    snprintf(output + slen, outbufsize - slen, "near ");
            output[slen++] = '[';

            if (prefix.segover) {
                slen +=
                    snprintf(output + slen, outbufsize - slen, "%s:",
                             prefix_name(prefix.segover));
            }
            if (o->basereg) {
                slen += snprintf(output + slen, outbufsize - slen, "%s",
                                 regname(o->basereg));
                started = true;
            }
            if (o->indexreg && !itemp_has(best_itemp, IF_MIB)) {
                if (started)
                    output[slen++] = '+';
                slen += snprintf(output + slen, outbufsize - slen, "%s",
                                 regname(o->indexreg));
                if (o->scale > 1)
                    slen +=
                        snprintf(output + slen, outbufsize - slen, "*%d",
                                o->scale);
                started = true;
            }


            /* Show a displacement */
            if (o->disp_size) {
                unsigned int defdisp = asize == 16 ? 16 : 32;
                bool do_sign = started || (o->segment & SEG_SIGNED);
                bool do_sext = do_sign || o->disp_size < asize;
                uint64_t offset;
                const char *rel = "";
                const char *sizename = "";
                const char *sign = "";

                if (o->eaflags & EAF_REL)
                    rel = "rel ";
                else if (!started && bits == 64)
                    rel = "abs ";

                if (o->disp_size != defdisp ||
                    (!started && asize != bits)) {
                    switch (o->disp_size) {
                    case 16:
                        sizename = "word ";
                        break;
                    case 32:
                        sizename = "dword ";
                        break;
                    case 64:
                        sizename = "qword ";
                        break;
                    default:
                        break;  /* This includes 8 bits */
                    }
                }

                if (do_sext)
                    offset = (int64_t)offs << nasize >> nasize;
                else
                    offset = (uint64_t)offs << nasize >> nasize;

                if (do_sign) {
                    if (started)
                        sign = "+";
                    if ((int64_t)offset < 0) {
                        sign = "-";
                        offset = -offset;
                    }
                }

                slen += snprintf(output + slen, outbufsize - slen,
                                 "%s%s%s0x%"PRIx64"",
                                 rel, sizename, sign, offset);
            }

            if (o->indexreg && itemp_has(best_itemp, IF_MIB)) {
                output[slen++] = ',';
                slen += snprintf(output + slen, outbufsize - slen, "%s",
                                 regname(o->indexreg));
                if (o->scale > 1)
                    slen +=
                        snprintf(output + slen, outbufsize - slen, "*%d",
                                o->scale);
                started = true;
            }

            output[slen++] = ']';

            if (deco)
                slen += append_evex_mem_deco(output + slen, outbufsize - slen,
                                             t, deco, &prefix);
        } else {
            slen +=
                snprintf(output + slen, outbufsize - slen, "<operand%d>",
                        i);
        }
    }
    output[slen] = '\0';
    return length;
}

/*
 * This is called when we don't have a complete instruction.  If it
 * is a standalone *single-byte* prefix show it as such, otherwise
 * print it as a literal.
 */
int32_t eatbyte(uint8_t byte, char *output, int outbufsize, int bits)
{
    const char *str = NULL;

    switch (byte) {
    case 0xF2:
        str = "repne";
        break;
    case 0xF3:
        str = "rep";
        break;
    case 0x9B:
        str = "wait";
        break;
    case 0xF0:
        str = "lock";
        break;
    case 0x2E:
        str = "cs";
        break;
    case 0x36:
        str = "ss";
        break;
    case 0x3E:
        str = "ds";
        break;
    case 0x26:
        str = "es";
        break;
    case 0x64:
        str = "fs";
        break;
    case 0x65:
        str = "gs";
        break;
    case 0x66:
        str = (bits == 16) ? "o32" : "o16";
        break;
    case 0x67:
        str = (bits == 32) ? "a16" : "a32";
        break;
    case REX_P + 0x0:
    case REX_P + 0x1:
    case REX_P + 0x2:
    case REX_P + 0x3:
    case REX_P + 0x4:
    case REX_P + 0x5:
    case REX_P + 0x6:
    case REX_P + 0x7:
    case REX_P + 0x8:
    case REX_P + 0x9:
    case REX_P + 0xA:
    case REX_P + 0xB:
    case REX_P + 0xC:
    case REX_P + 0xD:
    case REX_P + 0xE:
    case REX_P + 0xF:
        if (bits == 64) {
            snprintf(output, outbufsize, "rex%s%s%s%s%s",
                    (byte == REX_P) ? "" : ".",
                    (byte & REX_W) ? "w" : "",
                    (byte & REX_R) ? "r" : "",
                    (byte & REX_X) ? "x" : "",
                    (byte & REX_B) ? "b" : "");
            break;
        }
        /* else fall through */
    default:
        snprintf(output, outbufsize, "db 0x%02x", byte);
        break;
    }

    if (str)
        snprintf(output, outbufsize, "%s", str);

    return 1;
}
