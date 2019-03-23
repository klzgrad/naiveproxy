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
 * assemble.c   code generation for the Netwide Assembler
 *
 * Bytecode specification
 * ----------------------
 *
 *
 * Codes            Mnemonic        Explanation
 *
 * \0                                       terminates the code. (Unless it's a literal of course.)
 * \1..\4                                   that many literal bytes follow in the code stream
 * \5                                       add 4 to the primary operand number (b, low octdigit)
 * \6                                       add 4 to the secondary operand number (a, middle octdigit)
 * \7                                       add 4 to both the primary and the secondary operand number
 * \10..\13                                 a literal byte follows in the code stream, to be added
 *                                          to the register value of operand 0..3
 * \14..\17                                 the position of index register operand in MIB (BND insns)
 * \20..\23         ib                      a byte immediate operand, from operand 0..3
 * \24..\27         ib,u                    a zero-extended byte immediate operand, from operand 0..3
 * \30..\33         iw                      a word immediate operand, from operand 0..3
 * \34..\37         iwd                     select between \3[0-3] and \4[0-3] depending on 16/32 bit
 *                                          assembly mode or the operand-size override on the operand
 * \40..\43         id                      a long immediate operand, from operand 0..3
 * \44..\47         iwdq                    select between \3[0-3], \4[0-3] and \5[4-7]
 *                                          depending on the address size of the instruction.
 * \50..\53         rel8                    a byte relative operand, from operand 0..3
 * \54..\57         iq                      a qword immediate operand, from operand 0..3
 * \60..\63         rel16                   a word relative operand, from operand 0..3
 * \64..\67         rel                     select between \6[0-3] and \7[0-3] depending on 16/32 bit
 *                                          assembly mode or the operand-size override on the operand
 * \70..\73         rel32                   a long relative operand, from operand 0..3
 * \74..\77         seg                     a word constant, from the _segment_ part of operand 0..3
 * \1ab                                     a ModRM, calculated on EA in operand a, with the spare
 *                                          field the register value of operand b.
 * \172\ab                                  the register number from operand a in bits 7..4, with
 *                                          the 4-bit immediate from operand b in bits 3..0.
 * \173\xab                                 the register number from operand a in bits 7..4, with
 *                                          the value b in bits 3..0.
 * \174..\177                               the register number from operand 0..3 in bits 7..4, and
 *                                          an arbitrary value in bits 3..0 (assembled as zero.)
 * \2ab                                     a ModRM, calculated on EA in operand a, with the spare
 *                                          field equal to digit b.
 *
 * \240..\243                               this instruction uses EVEX rather than REX or VEX/XOP, with the
 *                                          V field taken from operand 0..3.
 * \250                                     this instruction uses EVEX rather than REX or VEX/XOP, with the
 *                                          V field set to 1111b.
 *
 * EVEX prefixes are followed by the sequence:
 * \cm\wlp\tup    where cm is:
 *                  cc 00m mmm
 *                  c = 2 for EVEX and mmmm is the M field (EVEX.P0[3:0])
 *                and wlp is:
 *                  00 wwl lpp
 *                  [l0]  ll = 0 (.128, .lz)
 *                  [l1]  ll = 1 (.256)
 *                  [l2]  ll = 2 (.512)
 *                  [lig] ll = 3 for EVEX.L'L don't care (always assembled as 0)
 *
 *                  [w0]  ww = 0 for W = 0
 *                  [w1]  ww = 1 for W = 1
 *                  [wig] ww = 2 for W don't care (always assembled as 0)
 *                  [ww]  ww = 3 for W used as REX.W
 *
 *                  [p0]  pp = 0 for no prefix
 *                  [60]  pp = 1 for legacy prefix 60
 *                  [f3]  pp = 2
 *                  [f2]  pp = 3
 *
 *                tup is tuple type for Disp8*N from %tuple_codes in insns.pl
 *                    (compressed displacement encoding)
 *
 * \254..\257       id,s                        a signed 32-bit operand to be extended to 64 bits.
 * \260..\263                                   this instruction uses VEX/XOP rather than REX, with the
 *                                              V field taken from operand 0..3.
 * \270                                         this instruction uses VEX/XOP rather than REX, with the
 *                                              V field set to 1111b.
 *
 * VEX/XOP prefixes are followed by the sequence:
 * \tmm\wlp        where mm is the M field; and wlp is:
 *                 00 wwl lpp
 *                 [l0]  ll = 0 for L = 0 (.128, .lz)
 *                 [l1]  ll = 1 for L = 1 (.256)
 *                 [lig] ll = 2 for L don't care (always assembled as 0)
 *
 *                 [w0]  ww = 0 for W = 0
 *                 [w1 ] ww = 1 for W = 1
 *                 [wig] ww = 2 for W don't care (always assembled as 0)
 *                 [ww]  ww = 3 for W used as REX.W
 *
 * t = 0 for VEX (C4/C5), t = 1 for XOP (8F).
 *
 * \271             hlexr                       instruction takes XRELEASE (F3) with or without lock
 * \272             hlenl                       instruction takes XACQUIRE/XRELEASE with or without lock
 * \273             hle                         instruction takes XACQUIRE/XRELEASE with lock only
 * \274..\277       ib,s                        a byte immediate operand, from operand 0..3, sign-extended
 *                                              to the operand size (if o16/o32/o64 present) or the bit size
 * \310             a16                         indicates fixed 16-bit address size, i.e. optional 0x67.
 * \311             a32                         indicates fixed 32-bit address size, i.e. optional 0x67.
 * \312             adf                         (disassembler only) invalid with non-default address size.
 * \313             a64                         indicates fixed 64-bit address size, 0x67 invalid.
 * \314             norexb                      (disassembler only) invalid with REX.B
 * \315             norexx                      (disassembler only) invalid with REX.X
 * \316             norexr                      (disassembler only) invalid with REX.R
 * \317             norexw                      (disassembler only) invalid with REX.W
 * \320             o16                         indicates fixed 16-bit operand size, i.e. optional 0x66.
 * \321             o32                         indicates fixed 32-bit operand size, i.e. optional 0x66.
 * \322             odf                         indicates that this instruction is only valid when the
 *                                              operand size is the default (instruction to disassembler,
 *                                              generates no code in the assembler)
 * \323             o64nw                       indicates fixed 64-bit operand size, REX on extensions only.
 * \324             o64                         indicates 64-bit operand size requiring REX prefix.
 * \325             nohi                        instruction which always uses spl/bpl/sil/dil
 * \326             nof3                        instruction not valid with 0xF3 REP prefix.  Hint for
                                                disassembler only; for SSE instructions.
 * \330                                         a literal byte follows in the code stream, to be added
 *                                              to the condition code value of the instruction.
 * \331             norep                       instruction not valid with REP prefix.  Hint for
 *                                              disassembler only; for SSE instructions.
 * \332             f2i                         REP prefix (0xF2 byte) used as opcode extension.
 * \333             f3i                         REP prefix (0xF3 byte) used as opcode extension.
 * \334             rex.l                       LOCK prefix used as REX.R (used in non-64-bit mode)
 * \335             repe                        disassemble a rep (0xF3 byte) prefix as repe not rep.
 * \336             mustrep                     force a REP(E) prefix (0xF3) even if not specified.
 * \337             mustrepne                   force a REPNE prefix (0xF2) even if not specified.
 *                                              \336-\337 are still listed as prefixes in the disassembler.
 * \340             resb                        reserve <operand 0> bytes of uninitialized storage.
 *                                              Operand 0 had better be a segmentless constant.
 * \341             wait                        this instruction needs a WAIT "prefix"
 * \360             np                          no SSE prefix (== \364\331)
 * \361                                         66 SSE prefix (== \366\331)
 * \364             !osp                        operand-size prefix (0x66) not permitted
 * \365             !asp                        address-size prefix (0x67) not permitted
 * \366                                         operand-size prefix (0x66) used as opcode extension
 * \367                                         address-size prefix (0x67) used as opcode extension
 * \370,\371        jcc8                        match only if operand 0 meets byte jump criteria.
 *                  jmp8                        370 is used for Jcc, 371 is used for JMP.
 * \373             jlen                        assemble 0x03 if bits==16, 0x05 if bits==32;
 *                                              used for conditional jump over longer jump
 * \374             vsibx|vm32x|vm64x           this instruction takes an XMM VSIB memory EA
 * \375             vsiby|vm32y|vm64y           this instruction takes an YMM VSIB memory EA
 * \376             vsibz|vm32z|vm64z           this instruction takes an ZMM VSIB memory EA
 */

#include "compiler.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "nasm.h"
#include "nasmlib.h"
#include "error.h"
#include "assemble.h"
#include "insns.h"
#include "tables.h"
#include "disp8.h"
#include "listing.h"

enum match_result {
    /*
     * Matching errors.  These should be sorted so that more specific
     * errors come later in the sequence.
     */
    MERR_INVALOP,
    MERR_OPSIZEMISSING,
    MERR_OPSIZEMISMATCH,
    MERR_BRNOTHERE,
    MERR_BRNUMMISMATCH,
    MERR_MASKNOTHERE,
    MERR_DECONOTHERE,
    MERR_BADCPU,
    MERR_BADMODE,
    MERR_BADHLE,
    MERR_ENCMISMATCH,
    MERR_BADBND,
    MERR_BADREPNE,
    MERR_REGSETSIZE,
    MERR_REGSET,
    /*
     * Matching success; the conditional ones first
     */
    MOK_JUMP,   /* Matching OK but needs jmp_match() */
    MOK_GOOD    /* Matching unconditionally OK */
};

typedef struct {
    enum ea_type type;            /* what kind of EA is this? */
    int sib_present;              /* is a SIB byte necessary? */
    int bytes;                    /* # of bytes of offset needed */
    int size;                     /* lazy - this is sib+bytes+1 */
    uint8_t modrm, sib, rex, rip; /* the bytes themselves */
    int8_t disp8;                  /* compressed displacement for EVEX */
} ea;

#define GEN_SIB(scale, index, base)                 \
        (((scale) << 6) | ((index) << 3) | ((base)))

#define GEN_MODRM(mod, reg, rm)                     \
        (((mod) << 6) | (((reg) & 7) << 3) | ((rm) & 7))

static int64_t calcsize(int32_t, int64_t, int, insn *,
                        const struct itemplate *);
static int emit_prefix(struct out_data *data, const int bits, insn *ins);
static void gencode(struct out_data *data, insn *ins);
static enum match_result find_match(const struct itemplate **tempp,
                                    insn *instruction,
                                    int32_t segment, int64_t offset, int bits);
static enum match_result matches(const struct itemplate *, insn *, int bits);
static opflags_t regflag(const operand *);
static int32_t regval(const operand *);
static int rexflags(int, opflags_t, int);
static int op_rexflags(const operand *, int);
static int op_evexflags(const operand *, int, uint8_t);
static void add_asp(insn *, int);

static enum ea_type process_ea(operand *, ea *, int, int,
                               opflags_t, insn *, const char **);

static inline bool absolute_op(const struct operand *o)
{
    return o->segment == NO_SEG && o->wrt == NO_SEG &&
        !(o->opflags & OPFLAG_RELATIVE);
}

static int has_prefix(insn * ins, enum prefix_pos pos, int prefix)
{
    return ins->prefixes[pos] == prefix;
}

static void assert_no_prefix(insn * ins, enum prefix_pos pos)
{
    if (ins->prefixes[pos])
        nasm_error(ERR_NONFATAL, "invalid %s prefix",
		   prefix_name(ins->prefixes[pos]));
}

static const char *size_name(int size)
{
    switch (size) {
    case 1:
        return "byte";
    case 2:
        return "word";
    case 4:
        return "dword";
    case 8:
        return "qword";
    case 10:
        return "tword";
    case 16:
        return "oword";
    case 32:
        return "yword";
    case 64:
        return "zword";
    default:
        return "???";
    }
}

static void warn_overflow(int size)
{
    nasm_error(ERR_WARNING | ERR_PASS2 | ERR_WARN_NOV,
            "%s data exceeds bounds", size_name(size));
}

static void warn_overflow_const(int64_t data, int size)
{
    if (overflow_general(data, size))
        warn_overflow(size);
}

static void warn_overflow_out(int64_t data, int size, enum out_sign sign)
{
    bool err;

    switch (sign) {
    case OUT_WRAP:
        err = overflow_general(data, size);
        break;
    case OUT_SIGNED:
        err = overflow_signed(data, size);
        break;
    case OUT_UNSIGNED:
        err = overflow_unsigned(data, size);
        break;
    default:
        panic();
        break;
    }

    if (err)
        warn_overflow(size);
}

/*
 * This routine wrappers the real output format's output routine,
 * in order to pass a copy of the data off to the listing file
 * generator at the same time, flatten unnecessary relocations,
 * and verify backend compatibility.
 */
static void out(struct out_data *data)
{
    static int32_t lineno = 0;     /* static!!! */
    static const char *lnfname = NULL;
    union {
        uint8_t b[8];
        uint64_t q;
    } xdata;
    size_t asize, amax;
    uint64_t zeropad = 0;
    int64_t addrval;
    int32_t fixseg;             /* Segment for which to produce fixed data */

    if (!data->size)
        return;                 /* Nothing to do */

    /*
     * Convert addresses to RAWDATA if possible
     * XXX: not all backends want this for global symbols!!!!
     */
    switch (data->type) {
    case OUT_ADDRESS:
        addrval = data->toffset;
        fixseg = NO_SEG;        /* Absolute address is fixed data */
        goto address;

    case OUT_RELADDR:
        addrval = data->toffset - data->relbase;
        fixseg = data->segment; /* Our own segment is fixed data */
        goto address;

    address:
        nasm_assert(data->size <= 8);
        asize = data->size;
        amax = ofmt->maxbits >> 3; /* Maximum address size in bytes */
        if ((ofmt->flags & OFMT_KEEP_ADDR) == 0 && data->tsegment == fixseg &&
            data->twrt == NO_SEG) {
            warn_overflow_out(addrval, asize, data->sign);
            xdata.q = cpu_to_le64(addrval);
            data->data = xdata.b;
            data->type = OUT_RAWDATA;
            asize = amax = 0;   /* No longer an address */
        }
        break;

    case OUT_SEGMENT:
        nasm_assert(data->size <= 8);
        asize = data->size;
        amax = 2;
        break;

    default:
        asize = amax = 0;       /* Not an address */
        break;
    }

    /*
     * this call to src_get determines when we call the
     * debug-format-specific "linenum" function
     * it updates lineno and lnfname to the current values
     * returning 0 if "same as last time", -2 if lnfname
     * changed, and the amount by which lineno changed,
     * if it did. thus, these variables must be static
     */

    if (src_get(&lineno, &lnfname))
        dfmt->linenum(lnfname, lineno, data->segment);

    if (asize > amax) {
        if (data->type == OUT_RELADDR || data->sign == OUT_SIGNED) {
            nasm_error(ERR_NONFATAL,
                    "%u-bit signed relocation unsupported by output format %s",
                       (unsigned int)(asize << 3), ofmt->shortname);
        } else {
            nasm_error(ERR_WARNING | ERR_WARN_ZEXTRELOC,
                       "%u-bit %s relocation zero-extended from %u bits",
                       (unsigned int)(asize << 3),
                       data->type == OUT_SEGMENT ? "segment" : "unsigned",
                       (unsigned int)(amax << 3));
        }
        zeropad = data->size - amax;
        data->size = amax;
    }
    lfmt->output(data);

    if (likely(data->segment != NO_SEG)) {
        ofmt->output(data);
    } else {
        /* Outputting to ABSOLUTE section - only reserve is permitted */
        if (data->type != OUT_RESERVE) {
            nasm_error(ERR_NONFATAL, "attempt to assemble code in [ABSOLUTE]"
                       " space");
        }
        /* No need to push to the backend */
    }

    data->offset  += data->size;
    data->insoffs += data->size;

    if (zeropad) {
        data->type     = OUT_ZERODATA;
        data->size     = zeropad;
        lfmt->output(data);
        ofmt->output(data);
        data->offset  += zeropad;
        data->insoffs += zeropad;
        data->size    += zeropad;  /* Restore original size value */
    }
}

static inline void out_rawdata(struct out_data *data, const void *rawdata,
                               size_t size)
{
    data->type = OUT_RAWDATA;
    data->data = rawdata;
    data->size = size;
    out(data);
}

static void out_rawbyte(struct out_data *data, uint8_t byte)
{
    data->type = OUT_RAWDATA;
    data->data = &byte;
    data->size = 1;
    out(data);
}

static inline void out_reserve(struct out_data *data, uint64_t size)
{
    data->type = OUT_RESERVE;
    data->size = size;
    out(data);
}

static void out_segment(struct out_data *data, const struct operand *opx)
{
    if (opx->opflags & OPFLAG_RELATIVE)
        nasm_error(ERR_NONFATAL, "segment references cannot be relative");

    data->type = OUT_SEGMENT;
    data->sign = OUT_UNSIGNED;
    data->size = 2;
    data->toffset = opx->offset;
    data->tsegment = ofmt->segbase(opx->segment | 1);
    data->twrt = opx->wrt;
    out(data);
}

static void out_imm(struct out_data *data, const struct operand *opx,
                    int size, enum out_sign sign)
{
    if (opx->segment != NO_SEG && (opx->segment & 1)) {
        /*
         * This is actually a segment reference, but eval() has
         * already called ofmt->segbase() for us.  Sigh.
         */
        if (size < 2)
            nasm_error(ERR_NONFATAL, "segment reference must be 16 bits");

        data->type = OUT_SEGMENT;
    } else {
        data->type = (opx->opflags & OPFLAG_RELATIVE)
            ? OUT_RELADDR : OUT_ADDRESS;
    }
    data->sign = sign;
    data->toffset = opx->offset;
    data->tsegment = opx->segment;
    data->twrt = opx->wrt;
    /*
     * XXX: improve this if at some point in the future we can
     * distinguish the subtrahend in expressions like [foo - bar]
     * where bar is a symbol in the current segment.  However, at the
     * current point, if OPFLAG_RELATIVE is set that subtraction has
     * already occurred.
     */
    data->relbase = 0;
    data->size = size;
    out(data);
}

static void out_reladdr(struct out_data *data, const struct operand *opx,
                        int size)
{
    if (opx->opflags & OPFLAG_RELATIVE)
        nasm_error(ERR_NONFATAL, "invalid use of self-relative expression");

    data->type = OUT_RELADDR;
    data->sign = OUT_SIGNED;
    data->size = size;
    data->toffset = opx->offset;
    data->tsegment = opx->segment;
    data->twrt = opx->wrt;
    data->relbase = data->offset + (data->inslen - data->insoffs);
    out(data);
}

static bool jmp_match(int32_t segment, int64_t offset, int bits,
                      insn * ins, const struct itemplate *temp)
{
    int64_t isize;
    const uint8_t *code = temp->code;
    uint8_t c = code[0];
    bool is_byte;

    if (((c & ~1) != 0370) || (ins->oprs[0].type & STRICT))
        return false;
    if (!optimizing.level || (optimizing.flag & OPTIM_DISABLE_JMP_MATCH))
        return false;
    if (optimizing.level < 0 && c == 0371)
        return false;

    isize = calcsize(segment, offset, bits, ins, temp);

    if (ins->oprs[0].opflags & OPFLAG_UNKNOWN)
        /* Be optimistic in pass 1 */
        return true;

    if (ins->oprs[0].segment != segment)
        return false;

    isize = ins->oprs[0].offset - offset - isize; /* isize is delta */
    is_byte = (isize >= -128 && isize <= 127); /* is it byte size? */

    if (is_byte && c == 0371 && ins->prefixes[PPS_REP] == P_BND) {
        /* jmp short (opcode eb) cannot be used with bnd prefix. */
        ins->prefixes[PPS_REP] = P_none;
        nasm_error(ERR_WARNING | ERR_WARN_BND | ERR_PASS2 ,
                "jmp short does not init bnd regs - bnd prefix dropped.");
    }

    return is_byte;
}

/* This is totally just a wild guess what is reasonable... */
#define INCBIN_MAX_BUF (ZERO_BUF_SIZE * 16)

int64_t assemble(int32_t segment, int64_t start, int bits, insn *instruction)
{
    struct out_data data;
    const struct itemplate *temp;
    enum match_result m;
    int64_t wsize;              /* size for DB etc. */

    nasm_zero(data);
    data.offset = start;
    data.segment = segment;
    data.itemp = NULL;
    data.bits = bits;

    wsize = db_bytes(instruction->opcode);
    if (wsize == -1)
        return 0;

    if (wsize) {
        extop *e;

        list_for_each(e, instruction->eops) {
            if (e->type == EOT_DB_NUMBER) {
                if (wsize > 8) {
                    nasm_error(ERR_NONFATAL,
                               "integer supplied to a DT, DO, DY or DZ"
                               " instruction");
                } else {
                    data.insoffs = 0;
                    data.inslen = data.size = wsize;
                    data.toffset = e->offset;
                    data.twrt = e->wrt;
                    data.relbase = 0;
                    if (e->segment != NO_SEG && (e->segment & 1)) {
                        data.tsegment = e->segment;
                        data.type = OUT_SEGMENT;
                        data.sign = OUT_UNSIGNED;
                    } else {
                        data.tsegment = e->segment;
                        data.type = e->relative ? OUT_RELADDR : OUT_ADDRESS;
                        data.sign = OUT_WRAP;
                    }
                    out(&data);
                }
            } else if (e->type == EOT_DB_STRING ||
                       e->type == EOT_DB_STRING_FREE) {
                int align = e->stringlen % wsize;
                if (align)
                    align = wsize - align;

                data.insoffs = 0;
                data.inslen = e->stringlen + align;

                out_rawdata(&data, e->stringval, e->stringlen);
                out_rawdata(&data, zero_buffer, align);
            }
        }
    } else if (instruction->opcode == I_INCBIN) {
        const char *fname = instruction->eops->stringval;
        FILE *fp;
        size_t t = instruction->times; /* INCBIN handles TIMES by itself */
        off_t base = 0;
        off_t len;
        const void *map = NULL;
        char *buf = NULL;
        size_t blk = 0;         /* Buffered I/O block size */
        size_t m = 0;           /* Bytes last read */

        if (!t)
            goto done;

        fp = nasm_open_read(fname, NF_BINARY|NF_FORMAP);
        if (!fp) {
            nasm_error(ERR_NONFATAL, "`incbin': unable to open file `%s'",
                  fname);
            goto done;
        }

        len = nasm_file_size(fp);

        if (len == (off_t)-1) {
            nasm_error(ERR_NONFATAL, "`incbin': unable to get length of file `%s'",
                       fname);
            goto close_done;
        }

        if (instruction->eops->next) {
            base = instruction->eops->next->offset;
            if (base >= len) {
                len = 0;
            } else {
                len -= base;
                if (instruction->eops->next->next &&
                    len > (off_t)instruction->eops->next->next->offset)
                    len = (off_t)instruction->eops->next->next->offset;
            }
        }

        lfmt->set_offset(data.offset);
        lfmt->uplevel(LIST_INCBIN);

        if (!len)
            goto end_incbin;

        /* Try to map file data */
        map = nasm_map_file(fp, base, len);
        if (!map) {
            blk = len < (off_t)INCBIN_MAX_BUF ? (size_t)len : INCBIN_MAX_BUF;
            buf = nasm_malloc(blk);
        }

        while (t--) {
            /*
             * Consider these irrelevant for INCBIN, since it is fully
             * possible that these might be (way) bigger than an int
             * can hold; there is, however, no reason to widen these
             * types just for INCBIN.  data.inslen == 0 signals to the
             * backend that these fields are meaningless, if at all
             * needed.
             */
            data.insoffs = 0;
            data.inslen = 0;

            if (map) {
                out_rawdata(&data, map, len);
            } else if ((off_t)m == len) {
                out_rawdata(&data, buf, len);
            } else {
                off_t l = len;

                if (fseeko(fp, base, SEEK_SET) < 0 || ferror(fp)) {
                    nasm_error(ERR_NONFATAL,
                               "`incbin': unable to seek on file `%s'",
                               fname);
                    goto end_incbin;
                }
                while (l > 0) {
                    m = fread(buf, 1, l < (off_t)blk ? (size_t)l : blk, fp);
                    if (!m || feof(fp)) {
                        /*
                         * This shouldn't happen unless the file
                         * actually changes while we are reading
                         * it.
                         */
                        nasm_error(ERR_NONFATAL,
                                   "`incbin': unexpected EOF while"
                                   " reading file `%s'", fname);
                        goto end_incbin;
                    }
                    out_rawdata(&data, buf, m);
                    l -= m;
                }
            }
        }
    end_incbin:
        lfmt->downlevel(LIST_INCBIN);
        if (instruction->times > 1) {
            lfmt->uplevel(LIST_TIMES);
            lfmt->downlevel(LIST_TIMES);
        }
        if (ferror(fp)) {
            nasm_error(ERR_NONFATAL,
                       "`incbin': error while"
                       " reading file `%s'", fname);
        }
    close_done:
        if (buf)
            nasm_free(buf);
        if (map)
            nasm_unmap_file(map, len);
        fclose(fp);
    done:
        instruction->times = 1; /* Tell the upper layer not to iterate */
        ;
    } else {
        /* "Real" instruction */

        /* Check to see if we need an address-size prefix */
        add_asp(instruction, bits);

        m = find_match(&temp, instruction, data.segment, data.offset, bits);

        if (m == MOK_GOOD) {
            /* Matches! */
            int64_t insn_size = calcsize(data.segment, data.offset,
                                         bits, instruction, temp);
            nasm_assert(insn_size >= 0);

            data.itemp = temp;
            data.bits = bits;
            data.insoffs = 0;
            data.inslen = insn_size;

            gencode(&data, instruction);
            nasm_assert(data.insoffs == insn_size);
        } else {
            /* No match */
            switch (m) {
            case MERR_OPSIZEMISSING:
                nasm_error(ERR_NONFATAL, "operation size not specified");
                break;
            case MERR_OPSIZEMISMATCH:
                nasm_error(ERR_NONFATAL, "mismatch in operand sizes");
                break;
            case MERR_BRNOTHERE:
                nasm_error(ERR_NONFATAL,
                           "broadcast not permitted on this operand");
                break;
            case MERR_BRNUMMISMATCH:
                nasm_error(ERR_NONFATAL,
                           "mismatch in the number of broadcasting elements");
                break;
            case MERR_MASKNOTHERE:
                nasm_error(ERR_NONFATAL,
                           "mask not permitted on this operand");
                break;
            case MERR_DECONOTHERE:
                nasm_error(ERR_NONFATAL, "unsupported mode decorator for instruction");
                break;
            case MERR_BADCPU:
                nasm_error(ERR_NONFATAL, "no instruction for this cpu level");
                break;
            case MERR_BADMODE:
                nasm_error(ERR_NONFATAL, "instruction not supported in %d-bit mode",
                           bits);
                break;
            case MERR_ENCMISMATCH:
                nasm_error(ERR_NONFATAL, "specific encoding scheme not available");
                break;
            case MERR_BADBND:
                nasm_error(ERR_NONFATAL, "bnd prefix is not allowed");
                break;
            case MERR_BADREPNE:
                nasm_error(ERR_NONFATAL, "%s prefix is not allowed",
                           (has_prefix(instruction, PPS_REP, P_REPNE) ?
                            "repne" : "repnz"));
                break;
            case MERR_REGSETSIZE:
                nasm_error(ERR_NONFATAL, "invalid register set size");
                break;
            case MERR_REGSET:
                nasm_error(ERR_NONFATAL, "register set not valid for operand");
                break;
            default:
                nasm_error(ERR_NONFATAL,
                           "invalid combination of opcode and operands");
                break;
            }

            instruction->times = 1; /* Avoid repeated error messages */
        }
    }
    return data.offset - start;
}

int64_t insn_size(int32_t segment, int64_t offset, int bits, insn *instruction)
{
    const struct itemplate *temp;
    enum match_result m;

    if (instruction->opcode == I_none)
        return 0;

    if (opcode_is_db(instruction->opcode)) {
        extop *e;
        int32_t isize, osize, wsize;

        isize = 0;
        wsize = db_bytes(instruction->opcode);
        nasm_assert(wsize > 0);

        list_for_each(e, instruction->eops) {
            int32_t align;

            osize = 0;
            if (e->type == EOT_DB_NUMBER) {
                osize = 1;
                warn_overflow_const(e->offset, wsize);
            } else if (e->type == EOT_DB_STRING ||
                       e->type == EOT_DB_STRING_FREE)
                osize = e->stringlen;

            align = (-osize) % wsize;
            if (align < 0)
                align += wsize;
            isize += osize + align;
        }
        return isize;
    }

    if (instruction->opcode == I_INCBIN) {
        const char *fname = instruction->eops->stringval;
        off_t len;

        len = nasm_file_size_by_path(fname);
        if (len == (off_t)-1) {
            nasm_error(ERR_NONFATAL, "`incbin': unable to get length of file `%s'",
                       fname);
            return 0;
        }

        if (instruction->eops->next) {
            if (len <= (off_t)instruction->eops->next->offset) {
                len = 0;
            } else {
                len -= instruction->eops->next->offset;
                if (instruction->eops->next->next &&
                    len > (off_t)instruction->eops->next->next->offset) {
                    len = (off_t)instruction->eops->next->next->offset;
                }
            }
        }

        len *= instruction->times;
        instruction->times = 1; /* Tell the upper layer to not iterate */

        return len;
    }

    /* Check to see if we need an address-size prefix */
    add_asp(instruction, bits);

    m = find_match(&temp, instruction, segment, offset, bits);
    if (m == MOK_GOOD) {
        /* we've matched an instruction. */
        return calcsize(segment, offset, bits, instruction, temp);
    } else {
        return -1;                  /* didn't match any instruction */
    }
}

static void bad_hle_warn(const insn * ins, uint8_t hleok)
{
    enum prefixes rep_pfx = ins->prefixes[PPS_REP];
    enum whatwarn { w_none, w_lock, w_inval } ww;
    static const enum whatwarn warn[2][4] =
    {
        { w_inval, w_inval, w_none, w_lock }, /* XACQUIRE */
        { w_inval, w_none,  w_none, w_lock }, /* XRELEASE */
    };
    unsigned int n;

    n = (unsigned int)rep_pfx - P_XACQUIRE;
    if (n > 1)
        return;                 /* Not XACQUIRE/XRELEASE */

    ww = warn[n][hleok];
    if (!is_class(MEMORY, ins->oprs[0].type))
        ww = w_inval;           /* HLE requires operand 0 to be memory */

    switch (ww) {
    case w_none:
        break;

    case w_lock:
        if (ins->prefixes[PPS_LOCK] != P_LOCK) {
            nasm_error(ERR_WARNING | ERR_WARN_HLE | ERR_PASS2,
                    "%s with this instruction requires lock",
                    prefix_name(rep_pfx));
        }
        break;

    case w_inval:
        nasm_error(ERR_WARNING | ERR_WARN_HLE | ERR_PASS2,
                "%s invalid with this instruction",
                prefix_name(rep_pfx));
        break;
    }
}

/* Common construct */
#define case3(x) case (x): case (x)+1: case (x)+2
#define case4(x) case3(x): case (x)+3

static int64_t calcsize(int32_t segment, int64_t offset, int bits,
                        insn * ins, const struct itemplate *temp)
{
    const uint8_t *codes = temp->code;
    int64_t length = 0;
    uint8_t c;
    int rex_mask = ~0;
    int op1, op2;
    struct operand *opx;
    uint8_t opex = 0;
    enum ea_type eat;
    uint8_t hleok = 0;
    bool lockcheck = true;
    enum reg_enum mib_index = R_none;   /* For a separate index MIB reg form */
    const char *errmsg;

    ins->rex = 0;               /* Ensure REX is reset */
    eat = EA_SCALAR;            /* Expect a scalar EA */
    memset(ins->evex_p, 0, 3);  /* Ensure EVEX is reset */

    if (ins->prefixes[PPS_OSIZE] == P_O64)
        ins->rex |= REX_W;

    (void)segment;              /* Don't warn that this parameter is unused */
    (void)offset;               /* Don't warn that this parameter is unused */

    while (*codes) {
        c = *codes++;
        op1 = (c & 3) + ((opex & 1) << 2);
        op2 = ((c >> 3) & 3) + ((opex & 2) << 1);
        opx = &ins->oprs[op1];
        opex = 0;               /* For the next iteration */

        switch (c) {
        case4(01):
            codes += c, length += c;
            break;

        case3(05):
            opex = c;
            break;

        case4(010):
            ins->rex |=
                op_rexflags(opx, REX_B|REX_H|REX_P|REX_W);
            codes++, length++;
            break;

        case4(014):
            /* this is an index reg of MIB operand */
            mib_index = opx->basereg;
            break;

        case4(020):
        case4(024):
            length++;
            break;

        case4(030):
            length += 2;
            break;

        case4(034):
            if (opx->type & (BITS16 | BITS32 | BITS64))
                length += (opx->type & BITS16) ? 2 : 4;
            else
                length += (bits == 16) ? 2 : 4;
            break;

        case4(040):
            length += 4;
            break;

        case4(044):
            length += ins->addr_size >> 3;
            break;

        case4(050):
            length++;
            break;

        case4(054):
            length += 8; /* MOV reg64/imm */
            break;

        case4(060):
            length += 2;
            break;

        case4(064):
            if (opx->type & (BITS16 | BITS32 | BITS64))
                length += (opx->type & BITS16) ? 2 : 4;
            else
                length += (bits == 16) ? 2 : 4;
            break;

        case4(070):
            length += 4;
            break;

        case4(074):
            length += 2;
            break;

        case 0172:
        case 0173:
            codes++;
            length++;
            break;

        case4(0174):
            length++;
            break;

        case4(0240):
            ins->rex |= REX_EV;
            ins->vexreg = regval(opx);
            ins->evex_p[2] |= op_evexflags(opx, EVEX_P2VP, 2); /* High-16 NDS */
            ins->vex_cm = *codes++;
            ins->vex_wlp = *codes++;
            ins->evex_tuple = (*codes++ - 0300);
            break;

        case 0250:
            ins->rex |= REX_EV;
            ins->vexreg = 0;
            ins->vex_cm = *codes++;
            ins->vex_wlp = *codes++;
            ins->evex_tuple = (*codes++ - 0300);
            break;

        case4(0254):
            length += 4;
            break;

        case4(0260):
            ins->rex |= REX_V;
            ins->vexreg = regval(opx);
            ins->vex_cm = *codes++;
            ins->vex_wlp = *codes++;
            break;

        case 0270:
            ins->rex |= REX_V;
            ins->vexreg = 0;
            ins->vex_cm = *codes++;
            ins->vex_wlp = *codes++;
            break;

        case3(0271):
            hleok = c & 3;
            break;

        case4(0274):
            length++;
            break;

        case4(0300):
            break;

        case 0310:
            if (bits == 64)
                return -1;
            length += (bits != 16) && !has_prefix(ins, PPS_ASIZE, P_A16);
            break;

        case 0311:
            length += (bits != 32) && !has_prefix(ins, PPS_ASIZE, P_A32);
            break;

        case 0312:
            break;

        case 0313:
            if (bits != 64 || has_prefix(ins, PPS_ASIZE, P_A16) ||
                has_prefix(ins, PPS_ASIZE, P_A32))
                return -1;
            break;

        case4(0314):
            break;

        case 0320:
        {
            enum prefixes pfx = ins->prefixes[PPS_OSIZE];
            if (pfx == P_O16)
                break;
            if (pfx != P_none)
                nasm_error(ERR_WARNING | ERR_PASS2, "invalid operand size prefix");
            else
                ins->prefixes[PPS_OSIZE] = P_O16;
            break;
        }

        case 0321:
        {
            enum prefixes pfx = ins->prefixes[PPS_OSIZE];
            if (pfx == P_O32)
                break;
            if (pfx != P_none)
                nasm_error(ERR_WARNING | ERR_PASS2, "invalid operand size prefix");
            else
                ins->prefixes[PPS_OSIZE] = P_O32;
            break;
        }

        case 0322:
            break;

        case 0323:
            rex_mask &= ~REX_W;
            break;

        case 0324:
            ins->rex |= REX_W;
            break;

        case 0325:
            ins->rex |= REX_NH;
            break;

        case 0326:
            break;

        case 0330:
            codes++, length++;
            break;

        case 0331:
            break;

        case 0332:
        case 0333:
            length++;
            break;

        case 0334:
            ins->rex |= REX_L;
            break;

        case 0335:
            break;

        case 0336:
            if (!ins->prefixes[PPS_REP])
                ins->prefixes[PPS_REP] = P_REP;
            break;

        case 0337:
            if (!ins->prefixes[PPS_REP])
                ins->prefixes[PPS_REP] = P_REPNE;
            break;

        case 0340:
            if (!absolute_op(&ins->oprs[0]))
                nasm_error(ERR_NONFATAL, "attempt to reserve non-constant"
                        " quantity of BSS space");
            else if (ins->oprs[0].opflags & OPFLAG_FORWARD)
                nasm_error(ERR_WARNING | ERR_PASS1,
                           "forward reference in RESx can have unpredictable results");
            else
                length += ins->oprs[0].offset;
            break;

        case 0341:
            if (!ins->prefixes[PPS_WAIT])
                ins->prefixes[PPS_WAIT] = P_WAIT;
            break;

        case 0360:
            break;

        case 0361:
            length++;
            break;

        case 0364:
        case 0365:
            break;

        case 0366:
        case 0367:
            length++;
            break;

        case 0370:
        case 0371:
            break;

        case 0373:
            length++;
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

        case4(0100):
        case4(0110):
        case4(0120):
        case4(0130):
        case4(0200):
        case4(0204):
        case4(0210):
        case4(0214):
        case4(0220):
        case4(0224):
        case4(0230):
        case4(0234):
            {
                ea ea_data;
                int rfield;
                opflags_t rflags;
                struct operand *opy = &ins->oprs[op2];
                struct operand *op_er_sae;

                ea_data.rex = 0;           /* Ensure ea.REX is initially 0 */

                if (c <= 0177) {
                    /* pick rfield from operand b (opx) */
                    rflags = regflag(opx);
                    rfield = nasm_regvals[opx->basereg];
                } else {
                    rflags = 0;
                    rfield = c & 7;
                }

                /* EVEX.b1 : evex_brerop contains the operand position */
                op_er_sae = (ins->evex_brerop >= 0 ?
                             &ins->oprs[ins->evex_brerop] : NULL);

                if (op_er_sae && (op_er_sae->decoflags & (ER | SAE))) {
                    /* set EVEX.b */
                    ins->evex_p[2] |= EVEX_P2B;
                    if (op_er_sae->decoflags & ER) {
                        /* set EVEX.RC (rounding control) */
                        ins->evex_p[2] |= ((ins->evex_rm - BRC_RN) << 5)
                                          & EVEX_P2RC;
                    }
                } else {
                    /* set EVEX.L'L (vector length) */
                    ins->evex_p[2] |= ((ins->vex_wlp << (5 - 2)) & EVEX_P2LL);
                    ins->evex_p[1] |= ((ins->vex_wlp << (7 - 4)) & EVEX_P1W);
                    if (opy->decoflags & BRDCAST_MASK) {
                        /* set EVEX.b */
                        ins->evex_p[2] |= EVEX_P2B;
                    }
                }

                if (itemp_has(temp, IF_MIB)) {
                    opy->eaflags |= EAF_MIB;
                    /*
                     * if a separate form of MIB (ICC style) is used,
                     * the index reg info is merged into mem operand
                     */
                    if (mib_index != R_none) {
                        opy->indexreg = mib_index;
                        opy->scale = 1;
                        opy->hintbase = mib_index;
                        opy->hinttype = EAH_NOTBASE;
                    }
                }

                if (process_ea(opy, &ea_data, bits,
                               rfield, rflags, ins, &errmsg) != eat) {
                    nasm_error(ERR_NONFATAL, "%s", errmsg);
                    return -1;
                } else {
                    ins->rex |= ea_data.rex;
                    length += ea_data.size;
                }
            }
            break;

        default:
            nasm_panic("internal instruction table corrupt"
                    ": instruction code \\%o (0x%02X) given", c, c);
            break;
        }
    }

    ins->rex &= rex_mask;

    if (ins->rex & REX_NH) {
        if (ins->rex & REX_H) {
            nasm_error(ERR_NONFATAL, "instruction cannot use high registers");
            return -1;
        }
        ins->rex &= ~REX_P;        /* Don't force REX prefix due to high reg */
    }

    switch (ins->prefixes[PPS_VEX]) {
    case P_EVEX:
        if (!(ins->rex & REX_EV))
            return -1;
        break;
    case P_VEX3:
    case P_VEX2:
        if (!(ins->rex & REX_V))
            return -1;
        break;
    default:
        break;
    }

    if (ins->rex & (REX_V | REX_EV)) {
        int bad32 = REX_R|REX_W|REX_X|REX_B;

        if (ins->rex & REX_H) {
            nasm_error(ERR_NONFATAL, "cannot use high register in AVX instruction");
            return -1;
        }
        switch (ins->vex_wlp & 060) {
        case 000:
        case 040:
            ins->rex &= ~REX_W;
            break;
        case 020:
            ins->rex |= REX_W;
            bad32 &= ~REX_W;
            break;
        case 060:
            /* Follow REX_W */
            break;
        }

        if (bits != 64 && ((ins->rex & bad32) || ins->vexreg > 7)) {
            nasm_error(ERR_NONFATAL, "invalid operands in non-64-bit mode");
            return -1;
        } else if (!(ins->rex & REX_EV) &&
                   ((ins->vexreg > 15) || (ins->evex_p[0] & 0xf0))) {
            nasm_error(ERR_NONFATAL, "invalid high-16 register in non-AVX-512");
            return -1;
        }
        if (ins->rex & REX_EV)
            length += 4;
        else if (ins->vex_cm != 1 || (ins->rex & (REX_W|REX_X|REX_B)) ||
                 ins->prefixes[PPS_VEX] == P_VEX3)
            length += 3;
        else
            length += 2;
    } else if (ins->rex & REX_MASK) {
        if (ins->rex & REX_H) {
            nasm_error(ERR_NONFATAL, "cannot use high register in rex instruction");
            return -1;
        } else if (bits == 64) {
            length++;
        } else if ((ins->rex & REX_L) &&
                   !(ins->rex & (REX_P|REX_W|REX_X|REX_B)) &&
                   iflag_cpu_level_ok(&cpu, IF_X86_64)) {
            /* LOCK-as-REX.R */
            assert_no_prefix(ins, PPS_LOCK);
            lockcheck = false;  /* Already errored, no need for warning */
            length++;
        } else {
            nasm_error(ERR_NONFATAL, "invalid operands in non-64-bit mode");
            return -1;
        }
    }

    if (has_prefix(ins, PPS_LOCK, P_LOCK) && lockcheck &&
        (!itemp_has(temp,IF_LOCK) || !is_class(MEMORY, ins->oprs[0].type))) {
        nasm_error(ERR_WARNING | ERR_WARN_LOCK | ERR_PASS2 ,
                "instruction is not lockable");
    }

    bad_hle_warn(ins, hleok);

    /*
     * when BND prefix is set by DEFAULT directive,
     * BND prefix is added to every appropriate instruction line
     * unless it is overridden by NOBND prefix.
     */
    if (globalbnd &&
        (itemp_has(temp, IF_BND) && !has_prefix(ins, PPS_REP, P_NOBND)))
            ins->prefixes[PPS_REP] = P_BND;

    /*
     * Add length of legacy prefixes
     */
    length += emit_prefix(NULL, bits, ins);

    return length;
}

static inline void emit_rex(struct out_data *data, insn *ins)
{
    if (data->bits == 64) {
        if ((ins->rex & REX_MASK) &&
            !(ins->rex & (REX_V | REX_EV)) &&
            !ins->rex_done) {
            uint8_t rex = (ins->rex & REX_MASK) | REX_P;
            out_rawbyte(data, rex);
            ins->rex_done = true;
        }
    }
}

static int emit_prefix(struct out_data *data, const int bits, insn *ins)
{
    int bytes = 0;
    int j;

    for (j = 0; j < MAXPREFIX; j++) {
        uint8_t c = 0;
        switch (ins->prefixes[j]) {
        case P_WAIT:
            c = 0x9B;
            break;
        case P_LOCK:
            c = 0xF0;
            break;
        case P_REPNE:
        case P_REPNZ:
        case P_XACQUIRE:
        case P_BND:
            c = 0xF2;
            break;
        case P_REPE:
        case P_REPZ:
        case P_REP:
        case P_XRELEASE:
            c = 0xF3;
            break;
        case R_CS:
            if (bits == 64) {
                nasm_error(ERR_WARNING | ERR_PASS2,
                           "cs segment base generated, but will be ignored in 64-bit mode");
            }
            c = 0x2E;
            break;
        case R_DS:
            if (bits == 64) {
                nasm_error(ERR_WARNING | ERR_PASS2,
                           "ds segment base generated, but will be ignored in 64-bit mode");
            }
            c = 0x3E;
            break;
        case R_ES:
            if (bits == 64) {
                nasm_error(ERR_WARNING | ERR_PASS2,
                           "es segment base generated, but will be ignored in 64-bit mode");
            }
            c = 0x26;
            break;
        case R_FS:
            c = 0x64;
            break;
        case R_GS:
            c = 0x65;
            break;
        case R_SS:
            if (bits == 64) {
                nasm_error(ERR_WARNING | ERR_PASS2,
                           "ss segment base generated, but will be ignored in 64-bit mode");
            }
            c = 0x36;
            break;
        case R_SEGR6:
        case R_SEGR7:
            nasm_error(ERR_NONFATAL,
                       "segr6 and segr7 cannot be used as prefixes");
            break;
        case P_A16:
            if (bits == 64) {
                nasm_error(ERR_NONFATAL,
                           "16-bit addressing is not supported "
                           "in 64-bit mode");
            } else if (bits != 16)
                c = 0x67;
            break;
        case P_A32:
            if (bits != 32)
                c = 0x67;
            break;
        case P_A64:
            if (bits != 64) {
                nasm_error(ERR_NONFATAL,
                           "64-bit addressing is only supported "
                           "in 64-bit mode");
            }
            break;
        case P_ASP:
            c = 0x67;
            break;
        case P_O16:
            if (bits != 16)
                c = 0x66;
            break;
        case P_O32:
            if (bits == 16)
                c = 0x66;
            break;
        case P_O64:
            /* REX.W */
            break;
        case P_OSP:
            c = 0x66;
            break;
        case P_EVEX:
        case P_VEX3:
        case P_VEX2:
        case P_NOBND:
        case P_none:
            break;
        default:
            nasm_panic("invalid instruction prefix");
        }
        if (c) {
            if (data)
                out_rawbyte(data, c);
            bytes++;
        }
    }
    return bytes;
}

static void gencode(struct out_data *data, insn *ins)
{
    uint8_t c;
    uint8_t bytes[4];
    int64_t size;
    int op1, op2;
    struct operand *opx;
    const uint8_t *codes = data->itemp->code;
    uint8_t opex = 0;
    enum ea_type eat = EA_SCALAR;
    int r;
    const int bits = data->bits;
    const char *errmsg;

    ins->rex_done = false;

    emit_prefix(data, bits, ins);

    while (*codes) {
        c = *codes++;
        op1 = (c & 3) + ((opex & 1) << 2);
        op2 = ((c >> 3) & 3) + ((opex & 2) << 1);
        opx = &ins->oprs[op1];
        opex = 0;                /* For the next iteration */


        switch (c) {
        case 01:
        case 02:
        case 03:
        case 04:
            emit_rex(data, ins);
            out_rawdata(data, codes, c);
            codes += c;
            break;

        case 05:
        case 06:
        case 07:
            opex = c;
            break;

        case4(010):
            emit_rex(data, ins);
            out_rawbyte(data, *codes++ + (regval(opx) & 7));
            break;

        case4(014):
            break;

        case4(020):
            out_imm(data, opx, 1, OUT_WRAP);
            break;

        case4(024):
            out_imm(data, opx, 1, OUT_UNSIGNED);
            break;

        case4(030):
            out_imm(data, opx, 2, OUT_WRAP);
            break;

        case4(034):
            if (opx->type & (BITS16 | BITS32))
                size = (opx->type & BITS16) ? 2 : 4;
            else
                size = (bits == 16) ? 2 : 4;
            out_imm(data, opx, size, OUT_WRAP);
            break;

        case4(040):
            out_imm(data, opx, 4, OUT_WRAP);
            break;

        case4(044):
            size = ins->addr_size >> 3;
            out_imm(data, opx, size, OUT_WRAP);
            break;

        case4(050):
            if (opx->segment == data->segment) {
                int64_t delta = opx->offset - data->offset
                    - (data->inslen - data->insoffs);
                if (delta > 127 || delta < -128)
                    nasm_error(ERR_NONFATAL, "short jump is out of range");
            }
            out_reladdr(data, opx, 1);
            break;

        case4(054):
            out_imm(data, opx, 8, OUT_WRAP);
            break;

        case4(060):
            out_reladdr(data, opx, 2);
            break;

        case4(064):
            if (opx->type & (BITS16 | BITS32 | BITS64))
                size = (opx->type & BITS16) ? 2 : 4;
            else
                size = (bits == 16) ? 2 : 4;

            out_reladdr(data, opx, size);
            break;

        case4(070):
            out_reladdr(data, opx, 4);
            break;

        case4(074):
            if (opx->segment == NO_SEG)
                nasm_error(ERR_NONFATAL, "value referenced by FAR is not"
                        " relocatable");
            out_segment(data, opx);
            break;

        case 0172:
        {
            int mask = ins->prefixes[PPS_VEX] == P_EVEX ? 7 : 15;
            const struct operand *opy;

            c = *codes++;
            opx = &ins->oprs[c >> 3];
            opy = &ins->oprs[c & 7];
            if (!absolute_op(opy)) {
                nasm_error(ERR_NONFATAL,
                        "non-absolute expression not permitted as argument %d",
                        c & 7);
            } else if (opy->offset & ~mask) {
                nasm_error(ERR_WARNING | ERR_PASS2 | ERR_WARN_NOV,
                           "is4 argument exceeds bounds");
            }
            c = opy->offset & mask;
            goto emit_is4;
         }

        case 0173:
            c = *codes++;
            opx = &ins->oprs[c >> 4];
            c &= 15;
            goto emit_is4;

        case4(0174):
            c = 0;
        emit_is4:
            r = nasm_regvals[opx->basereg];
            out_rawbyte(data, (r << 4) | ((r & 0x10) >> 1) | c);
            break;

        case4(0254):
            if (absolute_op(opx) &&
                (int32_t)opx->offset != (int64_t)opx->offset) {
                nasm_error(ERR_WARNING | ERR_PASS2 | ERR_WARN_NOV,
                        "signed dword immediate exceeds bounds");
            }
            out_imm(data, opx, 4, OUT_SIGNED);
            break;

        case4(0240):
        case 0250:
            codes += 3;
            ins->evex_p[2] |= op_evexflags(&ins->oprs[0],
                                           EVEX_P2Z | EVEX_P2AAA, 2);
            ins->evex_p[2] ^= EVEX_P2VP;        /* 1's complement */
            bytes[0] = 0x62;
            /* EVEX.X can be set by either REX or EVEX for different reasons */
            bytes[1] = ((((ins->rex & 7) << 5) |
                         (ins->evex_p[0] & (EVEX_P0X | EVEX_P0RP))) ^ 0xf0) |
                       (ins->vex_cm & EVEX_P0MM);
            bytes[2] = ((ins->rex & REX_W) << (7 - 3)) |
                       ((~ins->vexreg & 15) << 3) |
                       (1 << 2) | (ins->vex_wlp & 3);
            bytes[3] = ins->evex_p[2];
            out_rawdata(data, bytes, 4);
            break;

        case4(0260):
        case 0270:
            codes += 2;
            if (ins->vex_cm != 1 || (ins->rex & (REX_W|REX_X|REX_B)) ||
                ins->prefixes[PPS_VEX] == P_VEX3) {
                bytes[0] = (ins->vex_cm >> 6) ? 0x8f : 0xc4;
                bytes[1] = (ins->vex_cm & 31) | ((~ins->rex & 7) << 5);
                bytes[2] = ((ins->rex & REX_W) << (7-3)) |
                    ((~ins->vexreg & 15)<< 3) | (ins->vex_wlp & 07);
                out_rawdata(data, bytes, 3);
            } else {
                bytes[0] = 0xc5;
                bytes[1] = ((~ins->rex & REX_R) << (7-2)) |
                    ((~ins->vexreg & 15) << 3) | (ins->vex_wlp & 07);
                out_rawdata(data, bytes, 2);
            }
            break;

        case 0271:
        case 0272:
        case 0273:
            break;

        case4(0274):
        {
            uint64_t uv, um;
            int s;

            if (absolute_op(opx)) {
                if (ins->rex & REX_W)
                    s = 64;
                else if (ins->prefixes[PPS_OSIZE] == P_O16)
                    s = 16;
                else if (ins->prefixes[PPS_OSIZE] == P_O32)
                    s = 32;
                else
                    s = bits;

                um = (uint64_t)2 << (s-1);
                uv = opx->offset;

                if (uv > 127 && uv < (uint64_t)-128 &&
                    (uv < um-128 || uv > um-1)) {
                    /* If this wasn't explicitly byte-sized, warn as though we
                     * had fallen through to the imm16/32/64 case.
                     */
                    nasm_error(ERR_WARNING | ERR_PASS2 | ERR_WARN_NOV,
                               "%s value exceeds bounds",
                               (opx->type & BITS8) ? "signed byte" :
                               s == 16 ? "word" :
                               s == 32 ? "dword" :
                               "signed dword");
                }

                /* Output as a raw byte to avoid byte overflow check */
                out_rawbyte(data, (uint8_t)uv);
            } else {
                out_imm(data, opx, 1, OUT_WRAP); /* XXX: OUT_SIGNED? */
            }
            break;
        }

        case4(0300):
            break;

        case 0310:
            if (bits == 32 && !has_prefix(ins, PPS_ASIZE, P_A16))
                out_rawbyte(data, 0x67);
            break;

        case 0311:
            if (bits != 32 && !has_prefix(ins, PPS_ASIZE, P_A32))
                out_rawbyte(data, 0x67);
            break;

        case 0312:
            break;

        case 0313:
            ins->rex = 0;
            break;

        case4(0314):
            break;

        case 0320:
        case 0321:
            break;

        case 0322:
        case 0323:
            break;

        case 0324:
            ins->rex |= REX_W;
            break;

        case 0325:
            break;

        case 0326:
            break;

        case 0330:
            out_rawbyte(data, *codes++ ^ get_cond_opcode(ins->condition));
            break;

        case 0331:
            break;

        case 0332:
        case 0333:
            out_rawbyte(data, c - 0332 + 0xF2);
            break;

        case 0334:
            if (ins->rex & REX_R)
                out_rawbyte(data, 0xF0);
            ins->rex &= ~(REX_L|REX_R);
            break;

        case 0335:
            break;

        case 0336:
        case 0337:
            break;

        case 0340:
            if (ins->oprs[0].segment != NO_SEG)
                nasm_panic("non-constant BSS size in pass two");

            out_reserve(data, ins->oprs[0].offset);
            break;

        case 0341:
            break;

        case 0360:
            break;

        case 0361:
            out_rawbyte(data, 0x66);
            break;

        case 0364:
        case 0365:
            break;

        case 0366:
        case 0367:
            out_rawbyte(data, c - 0366 + 0x66);
            break;

        case3(0370):
            break;

        case 0373:
            out_rawbyte(data, bits == 16 ? 3 : 5);
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

        case4(0100):
        case4(0110):
        case4(0120):
        case4(0130):
        case4(0200):
        case4(0204):
        case4(0210):
        case4(0214):
        case4(0220):
        case4(0224):
        case4(0230):
        case4(0234):
            {
                ea ea_data;
                int rfield;
                opflags_t rflags;
                uint8_t *p;
                struct operand *opy = &ins->oprs[op2];

                if (c <= 0177) {
                    /* pick rfield from operand b (opx) */
                    rflags = regflag(opx);
                    rfield = nasm_regvals[opx->basereg];
                } else {
                    /* rfield is constant */
                    rflags = 0;
                    rfield = c & 7;
                }

                if (process_ea(opy, &ea_data, bits,
                               rfield, rflags, ins, &errmsg) != eat)
                    nasm_error(ERR_NONFATAL, "%s", errmsg);

                p = bytes;
                *p++ = ea_data.modrm;
                if (ea_data.sib_present)
                    *p++ = ea_data.sib;
                out_rawdata(data, bytes, p - bytes);

                /*
                 * Make sure the address gets the right offset in case
                 * the line breaks in the .lst file (BR 1197827)
                 */

                if (ea_data.bytes) {
                    /* use compressed displacement, if available */
                    if (ea_data.disp8) {
                        out_rawbyte(data, ea_data.disp8);
                    } else if (ea_data.rip) {
                        out_reladdr(data, opy, ea_data.bytes);
                    } else {
                        int asize = ins->addr_size >> 3;

                        if (overflow_general(opy->offset, asize) ||
                            signed_bits(opy->offset, ins->addr_size) !=
                            signed_bits(opy->offset, ea_data.bytes << 3))
                            warn_overflow(ea_data.bytes);

                        out_imm(data, opy, ea_data.bytes,
                                (asize > ea_data.bytes)
                                ? OUT_SIGNED : OUT_WRAP);
                    }
                }
            }
            break;

        default:
            nasm_panic("internal instruction table corrupt"
                    ": instruction code \\%o (0x%02X) given", c, c);
            break;
        }
    }
}

static opflags_t regflag(const operand * o)
{
    if (!is_register(o->basereg))
        nasm_panic("invalid operand passed to regflag()");
    return nasm_reg_flags[o->basereg];
}

static int32_t regval(const operand * o)
{
    if (!is_register(o->basereg))
        nasm_panic("invalid operand passed to regval()");
    return nasm_regvals[o->basereg];
}

static int op_rexflags(const operand * o, int mask)
{
    opflags_t flags;
    int val;

    if (!is_register(o->basereg))
        nasm_panic("invalid operand passed to op_rexflags()");

    flags = nasm_reg_flags[o->basereg];
    val = nasm_regvals[o->basereg];

    return rexflags(val, flags, mask);
}

static int rexflags(int val, opflags_t flags, int mask)
{
    int rex = 0;

    if (val >= 0 && (val & 8))
        rex |= REX_B|REX_X|REX_R;
    if (flags & BITS64)
        rex |= REX_W;
    if (!(REG_HIGH & ~flags))                   /* AH, CH, DH, BH */
        rex |= REX_H;
    else if (!(REG8 & ~flags) && val >= 4)      /* SPL, BPL, SIL, DIL */
        rex |= REX_P;

    return rex & mask;
}

static int evexflags(int val, decoflags_t deco,
                     int mask, uint8_t byte)
{
    int evex = 0;

    switch (byte) {
    case 0:
        if (val >= 0 && (val & 16))
            evex |= (EVEX_P0RP | EVEX_P0X);
        break;
    case 2:
        if (val >= 0 && (val & 16))
            evex |= EVEX_P2VP;
        if (deco & Z)
            evex |= EVEX_P2Z;
        if (deco & OPMASK_MASK)
            evex |= deco & EVEX_P2AAA;
        break;
    }
    return evex & mask;
}

static int op_evexflags(const operand * o, int mask, uint8_t byte)
{
    int val;

    val = nasm_regvals[o->basereg];

    return evexflags(val, o->decoflags, mask, byte);
}

static enum match_result find_match(const struct itemplate **tempp,
                                    insn *instruction,
                                    int32_t segment, int64_t offset, int bits)
{
    const struct itemplate *temp;
    enum match_result m, merr;
    opflags_t xsizeflags[MAX_OPERANDS];
    bool opsizemissing = false;
    int8_t broadcast = instruction->evex_brerop;
    int i;

    /* broadcasting uses a different data element size */
    for (i = 0; i < instruction->operands; i++)
        if (i == broadcast)
            xsizeflags[i] = instruction->oprs[i].decoflags & BRSIZE_MASK;
        else
            xsizeflags[i] = instruction->oprs[i].type & SIZE_MASK;

    merr = MERR_INVALOP;

    for (temp = nasm_instructions[instruction->opcode];
         temp->opcode != I_none; temp++) {
        m = matches(temp, instruction, bits);
        if (m == MOK_JUMP) {
            if (jmp_match(segment, offset, bits, instruction, temp))
                m = MOK_GOOD;
            else
                m = MERR_INVALOP;
        } else if (m == MERR_OPSIZEMISSING && !itemp_has(temp, IF_SX)) {
            /*
             * Missing operand size and a candidate for fuzzy matching...
             */
            for (i = 0; i < temp->operands; i++)
                if (i == broadcast)
                    xsizeflags[i] |= temp->deco[i] & BRSIZE_MASK;
                else
                    xsizeflags[i] |= temp->opd[i] & SIZE_MASK;
            opsizemissing = true;
        }
        if (m > merr)
            merr = m;
        if (merr == MOK_GOOD)
            goto done;
    }

    /* No match, but see if we can get a fuzzy operand size match... */
    if (!opsizemissing)
        goto done;

    for (i = 0; i < instruction->operands; i++) {
        /*
         * We ignore extrinsic operand sizes on registers, so we should
         * never try to fuzzy-match on them.  This also resolves the case
         * when we have e.g. "xmmrm128" in two different positions.
         */
        if (is_class(REGISTER, instruction->oprs[i].type))
            continue;

        /* This tests if xsizeflags[i] has more than one bit set */
        if ((xsizeflags[i] & (xsizeflags[i]-1)))
            goto done;                /* No luck */

        if (i == broadcast) {
            instruction->oprs[i].decoflags |= xsizeflags[i];
            instruction->oprs[i].type |= (xsizeflags[i] == BR_BITS32 ?
                                          BITS32 : BITS64);
        } else {
            instruction->oprs[i].type |= xsizeflags[i]; /* Set the size */
        }
    }

    /* Try matching again... */
    for (temp = nasm_instructions[instruction->opcode];
         temp->opcode != I_none; temp++) {
        m = matches(temp, instruction, bits);
        if (m == MOK_JUMP) {
            if (jmp_match(segment, offset, bits, instruction, temp))
                m = MOK_GOOD;
            else
                m = MERR_INVALOP;
        }
        if (m > merr)
            merr = m;
        if (merr == MOK_GOOD)
            goto done;
    }

done:
    *tempp = temp;
    return merr;
}

static uint8_t get_broadcast_num(opflags_t opflags, opflags_t brsize)
{
    unsigned int opsize = (opflags & SIZE_MASK) >> SIZE_SHIFT;
    uint8_t brcast_num;

    if (brsize > BITS64)
        nasm_error(ERR_FATAL,
            "size of broadcasting element is greater than 64 bits");

    /*
     * The shift term is to take care of the extra BITS80 inserted
     * between BITS64 and BITS128.
     */
    brcast_num = ((opsize / (BITS64 >> SIZE_SHIFT)) * (BITS64 / brsize))
        >> (opsize > (BITS64 >> SIZE_SHIFT));

    return brcast_num;
}

static enum match_result matches(const struct itemplate *itemp,
                                 insn *instruction, int bits)
{
    opflags_t size[MAX_OPERANDS], asize;
    bool opsizemissing = false;
    int i, oprs;

    /*
     * Check the opcode
     */
    if (itemp->opcode != instruction->opcode)
        return MERR_INVALOP;

    /*
     * Count the operands
     */
    if (itemp->operands != instruction->operands)
        return MERR_INVALOP;

    /*
     * Is it legal?
     */
    if (!(optimizing.level > 0) && itemp_has(itemp, IF_OPT))
	return MERR_INVALOP;

    /*
     * {evex} available?
     */
    switch (instruction->prefixes[PPS_VEX]) {
    case P_EVEX:
        if (!itemp_has(itemp, IF_EVEX))
            return MERR_ENCMISMATCH;
        break;
    case P_VEX3:
    case P_VEX2:
        if (!itemp_has(itemp, IF_VEX))
            return MERR_ENCMISMATCH;
        break;
    default:
        break;
    }

    /*
     * Check that no spurious colons or TOs are present
     */
    for (i = 0; i < itemp->operands; i++)
        if (instruction->oprs[i].type & ~itemp->opd[i] & (COLON | TO))
            return MERR_INVALOP;

    /*
     * Process size flags
     */
    switch (itemp_smask(itemp)) {
    case IF_GENBIT(IF_SB):
        asize = BITS8;
        break;
    case IF_GENBIT(IF_SW):
        asize = BITS16;
        break;
    case IF_GENBIT(IF_SD):
        asize = BITS32;
        break;
    case IF_GENBIT(IF_SQ):
        asize = BITS64;
        break;
    case IF_GENBIT(IF_SO):
        asize = BITS128;
        break;
    case IF_GENBIT(IF_SY):
        asize = BITS256;
        break;
    case IF_GENBIT(IF_SZ):
        asize = BITS512;
        break;
    case IF_GENBIT(IF_SIZE):
        switch (bits) {
        case 16:
            asize = BITS16;
            break;
        case 32:
            asize = BITS32;
            break;
        case 64:
            asize = BITS64;
            break;
        default:
            asize = 0;
            break;
        }
        break;
    default:
        asize = 0;
        break;
    }

    if (itemp_armask(itemp)) {
        /* S- flags only apply to a specific operand */
        i = itemp_arg(itemp);
        memset(size, 0, sizeof size);
        size[i] = asize;
    } else {
        /* S- flags apply to all operands */
        for (i = 0; i < MAX_OPERANDS; i++)
            size[i] = asize;
    }

    /*
     * Check that the operand flags all match up,
     * it's a bit tricky so lets be verbose:
     *
     * 1) Find out the size of operand. If instruction
     *    doesn't have one specified -- we're trying to
     *    guess it either from template (IF_S* flag) or
     *    from code bits.
     *
     * 2) If template operand do not match the instruction OR
     *    template has an operand size specified AND this size differ
     *    from which instruction has (perhaps we got it from code bits)
     *    we are:
     *      a)  Check that only size of instruction and operand is differ
     *          other characteristics do match
     *      b)  Perhaps it's a register specified in instruction so
     *          for such a case we just mark that operand as "size
     *          missing" and this will turn on fuzzy operand size
     *          logic facility (handled by a caller)
     */
    for (i = 0; i < itemp->operands; i++) {
        opflags_t type = instruction->oprs[i].type;
        decoflags_t deco = instruction->oprs[i].decoflags;
        decoflags_t ideco = itemp->deco[i];
        bool is_broadcast = deco & BRDCAST_MASK;
        uint8_t brcast_num = 0;
        opflags_t template_opsize, insn_opsize;

        if (!(type & SIZE_MASK))
            type |= size[i];

        insn_opsize     = type & SIZE_MASK;
        if (!is_broadcast) {
            template_opsize = itemp->opd[i] & SIZE_MASK;
        } else {
            decoflags_t deco_brsize = ideco & BRSIZE_MASK;

            if (~ideco & BRDCAST_MASK)
                return MERR_BRNOTHERE;

            /*
             * when broadcasting, the element size depends on
             * the instruction type. decorator flag should match.
             */
            if (deco_brsize) {
                template_opsize = (deco_brsize == BR_BITS32 ? BITS32 : BITS64);
                /* calculate the proper number : {1to<brcast_num>} */
                brcast_num = get_broadcast_num(itemp->opd[i], template_opsize);
            } else {
                template_opsize = 0;
            }
        }

        if (~ideco & deco & OPMASK_MASK)
            return MERR_MASKNOTHERE;

        if (~ideco & deco & (Z_MASK|STATICRND_MASK|SAE_MASK))
            return MERR_DECONOTHERE;

        if (itemp->opd[i] & ~type & ~(SIZE_MASK|REGSET_MASK))
            return MERR_INVALOP;

        if (~itemp->opd[i] & type & REGSET_MASK)
            return (itemp->opd[i] & REGSET_MASK)
                ? MERR_REGSETSIZE : MERR_REGSET;

        if (template_opsize) {
            if (template_opsize != insn_opsize) {
                if (insn_opsize) {
                    return MERR_INVALOP;
                } else if (!is_class(REGISTER, type)) {
                    /*
                     * Note: we don't honor extrinsic operand sizes for registers,
                     * so "missing operand size" for a register should be
                     * considered a wildcard match rather than an error.
                     */
                    opsizemissing = true;
                }
            } else if (is_broadcast &&
                       (brcast_num !=
                        (2U << ((deco & BRNUM_MASK) >> BRNUM_SHIFT)))) {
                /*
                 * broadcasting opsize matches but the number of repeated memory
                 * element does not match.
                 * if 64b double precision float is broadcasted to ymm (256b),
                 * broadcasting decorator must be {1to4}.
                 */
                return MERR_BRNUMMISMATCH;
            }
        }
    }

    if (opsizemissing)
        return MERR_OPSIZEMISSING;

    /*
     * Check operand sizes
     */
    if (itemp_has(itemp, IF_SM) || itemp_has(itemp, IF_SM2)) {
        oprs = (itemp_has(itemp, IF_SM2) ? 2 : itemp->operands);
        for (i = 0; i < oprs; i++) {
            asize = itemp->opd[i] & SIZE_MASK;
            if (asize) {
                for (i = 0; i < oprs; i++)
                    size[i] = asize;
                break;
            }
        }
    } else {
        oprs = itemp->operands;
    }

    for (i = 0; i < itemp->operands; i++) {
        if (!(itemp->opd[i] & SIZE_MASK) &&
            (instruction->oprs[i].type & SIZE_MASK & ~size[i]))
            return MERR_OPSIZEMISMATCH;
    }

    /*
     * Check template is okay at the set cpu level
     */
    if (iflag_cmp_cpu_level(&insns_flags[itemp->iflag_idx], &cpu) > 0)
        return MERR_BADCPU;

    /*
     * Verify the appropriate long mode flag.
     */
    if (itemp_has(itemp, (bits == 64 ? IF_NOLONG : IF_LONG)))
        return MERR_BADMODE;

    /*
     * If we have a HLE prefix, look for the NOHLE flag
     */
    if (itemp_has(itemp, IF_NOHLE) &&
        (has_prefix(instruction, PPS_REP, P_XACQUIRE) ||
         has_prefix(instruction, PPS_REP, P_XRELEASE)))
        return MERR_BADHLE;

    /*
     * Check if special handling needed for Jumps
     */
    if ((itemp->code[0] & ~1) == 0370)
        return MOK_JUMP;

    /*
     * Check if BND prefix is allowed.
     * Other 0xF2 (REPNE/REPNZ) prefix is prohibited.
     */
    if (!itemp_has(itemp, IF_BND) &&
        (has_prefix(instruction, PPS_REP, P_BND) ||
         has_prefix(instruction, PPS_REP, P_NOBND)))
        return MERR_BADBND;
    else if (itemp_has(itemp, IF_BND) &&
             (has_prefix(instruction, PPS_REP, P_REPNE) ||
              has_prefix(instruction, PPS_REP, P_REPNZ)))
        return MERR_BADREPNE;

    return MOK_GOOD;
}

/*
 * Check if ModR/M.mod should/can be 01.
 * - EAF_BYTEOFFS is set
 * - offset can fit in a byte when EVEX is not used
 * - offset can be compressed when EVEX is used
 */
#define IS_MOD_01() (!(input->eaflags & EAF_WORDOFFS) &&               \
                    (ins->rex & REX_EV ? seg == NO_SEG && !forw_ref && \
                     is_disp8n(input, ins, &output->disp8) :           \
                     input->eaflags & EAF_BYTEOFFS || (o >= -128 &&    \
                     o <= 127 && seg == NO_SEG && !forw_ref)))

static enum ea_type process_ea(operand *input, ea *output, int bits,
                               int rfield, opflags_t rflags, insn *ins,
                               const char **errmsg)
{
    bool forw_ref = !!(input->opflags & OPFLAG_UNKNOWN);
    int addrbits = ins->addr_size;
    int eaflags = input->eaflags;

    *errmsg = "invalid effective address"; /* Default error message */

    output->type    = EA_SCALAR;
    output->rip     = false;
    output->disp8   = 0;

    /* REX flags for the rfield operand */
    output->rex     |= rexflags(rfield, rflags, REX_R | REX_P | REX_W | REX_H);
    /* EVEX.R' flag for the REG operand */
    ins->evex_p[0]  |= evexflags(rfield, 0, EVEX_P0RP, 0);

    if (is_class(REGISTER, input->type)) {
        /*
         * It's a direct register.
         */
        if (!is_register(input->basereg))
            goto err;

        if (!is_reg_class(REG_EA, input->basereg))
            goto err;

        /* broadcasting is not available with a direct register operand. */
        if (input->decoflags & BRDCAST_MASK) {
            *errmsg = "broadcast not allowed with register operand";
            goto err;
        }

        output->rex         |= op_rexflags(input, REX_B | REX_P | REX_W | REX_H);
        ins->evex_p[0]      |= op_evexflags(input, EVEX_P0X, 0);
        output->sib_present = false;    /* no SIB necessary */
        output->bytes       = 0;        /* no offset necessary either */
        output->modrm       = GEN_MODRM(3, rfield, nasm_regvals[input->basereg]);
    } else {
        /*
         * It's a memory reference.
         */

        /* Embedded rounding or SAE is not available with a mem ref operand. */
        if (input->decoflags & (ER | SAE)) {
            *errmsg = "embedded rounding is available only with "
                "register-register operations";
            goto err;
        }

        if (input->basereg == -1 &&
            (input->indexreg == -1 || input->scale == 0)) {
            /*
             * It's a pure offset.
             */
            if (bits == 64 && ((input->type & IP_REL) == IP_REL)) {
                if (input->segment == NO_SEG ||
                    (input->opflags & OPFLAG_RELATIVE)) {
                    nasm_error(ERR_WARNING | ERR_PASS2,
                               "absolute address can not be RIP-relative");
                    input->type &= ~IP_REL;
                    input->type |= MEMORY;
                }
            }

            if (bits == 64 &&
                !(IP_REL & ~input->type) && (eaflags & EAF_MIB)) {
                *errmsg = "RIP-relative addressing is prohibited for MIB";
                goto err;
            }

            if (eaflags & EAF_BYTEOFFS ||
                (eaflags & EAF_WORDOFFS &&
                 input->disp_size != (addrbits != 16 ? 32 : 16))) {
                nasm_error(ERR_WARNING | ERR_PASS1,
                           "displacement size ignored on absolute address");
            }

            if (bits == 64 && (~input->type & IP_REL)) {
                output->sib_present = true;
                output->sib         = GEN_SIB(0, 4, 5);
                output->bytes       = 4;
                output->modrm       = GEN_MODRM(0, rfield, 4);
                output->rip         = false;
            } else {
                output->sib_present = false;
                output->bytes       = (addrbits != 16 ? 4 : 2);
                output->modrm       = GEN_MODRM(0, rfield,
                                                (addrbits != 16 ? 5 : 6));
                output->rip         = bits == 64;
            }
        } else {
            /*
             * It's an indirection.
             */
            int i = input->indexreg, b = input->basereg, s = input->scale;
            int32_t seg = input->segment;
            int hb = input->hintbase, ht = input->hinttype;
            int t, it, bt;              /* register numbers */
            opflags_t x, ix, bx;        /* register flags */

            if (s == 0)
                i = -1;         /* make this easy, at least */

            if (is_register(i)) {
                it = nasm_regvals[i];
                ix = nasm_reg_flags[i];
            } else {
                it = -1;
                ix = 0;
            }

            if (is_register(b)) {
                bt = nasm_regvals[b];
                bx = nasm_reg_flags[b];
            } else {
                bt = -1;
                bx = 0;
            }

            /* if either one are a vector register... */
            if ((ix|bx) & (XMMREG|YMMREG|ZMMREG) & ~REG_EA) {
                opflags_t sok = BITS32 | BITS64;
                int32_t o = input->offset;
                int mod, scale, index, base;

                /*
                 * For a vector SIB, one has to be a vector and the other,
                 * if present, a GPR.  The vector must be the index operand.
                 */
                if (it == -1 || (bx & (XMMREG|YMMREG|ZMMREG) & ~REG_EA)) {
                    if (s == 0)
                        s = 1;
                    else if (s != 1)
                        goto err;

                    t = bt, bt = it, it = t;
                    x = bx, bx = ix, ix = x;
                }

                if (bt != -1) {
                    if (REG_GPR & ~bx)
                        goto err;
                    if (!(REG64 & ~bx) || !(REG32 & ~bx))
                        sok &= bx;
                    else
                        goto err;
                }

                /*
                 * While we're here, ensure the user didn't specify
                 * WORD or QWORD
                 */
                if (input->disp_size == 16 || input->disp_size == 64)
                    goto err;

                if (addrbits == 16 ||
                    (addrbits == 32 && !(sok & BITS32)) ||
                    (addrbits == 64 && !(sok & BITS64)))
                    goto err;

                output->type = ((ix & ZMMREG & ~REG_EA) ? EA_ZMMVSIB
                                : ((ix & YMMREG & ~REG_EA)
                                ? EA_YMMVSIB : EA_XMMVSIB));

                output->rex    |= rexflags(it, ix, REX_X);
                output->rex    |= rexflags(bt, bx, REX_B);
                ins->evex_p[2] |= evexflags(it, 0, EVEX_P2VP, 2);

                index = it & 7; /* it is known to be != -1 */

                switch (s) {
                case 1:
                    scale = 0;
                    break;
                case 2:
                    scale = 1;
                    break;
                case 4:
                    scale = 2;
                    break;
                case 8:
                    scale = 3;
                    break;
                default:   /* then what the smeg is it? */
                    goto err;    /* panic */
                }

                if (bt == -1) {
                    base = 5;
                    mod = 0;
                } else {
                    base = (bt & 7);
                    if (base != REG_NUM_EBP && o == 0 &&
                        seg == NO_SEG && !forw_ref &&
                        !(eaflags & (EAF_BYTEOFFS | EAF_WORDOFFS)))
                        mod = 0;
                    else if (IS_MOD_01())
                        mod = 1;
                    else
                        mod = 2;
                }

                output->sib_present = true;
                output->bytes       = (bt == -1 || mod == 2 ? 4 : mod);
                output->modrm       = GEN_MODRM(mod, rfield, 4);
                output->sib         = GEN_SIB(scale, index, base);
            } else if ((ix|bx) & (BITS32|BITS64)) {
                /*
                 * it must be a 32/64-bit memory reference. Firstly we have
                 * to check that all registers involved are type E/Rxx.
                 */
                opflags_t sok = BITS32 | BITS64;
                int32_t o = input->offset;

                if (it != -1) {
                    if (!(REG64 & ~ix) || !(REG32 & ~ix))
                        sok &= ix;
                    else
                        goto err;
                }

                if (bt != -1) {
                    if (REG_GPR & ~bx)
                        goto err; /* Invalid register */
                    if (~sok & bx & SIZE_MASK)
                        goto err; /* Invalid size */
                    sok &= bx;
                }

                /*
                 * While we're here, ensure the user didn't specify
                 * WORD or QWORD
                 */
                if (input->disp_size == 16 || input->disp_size == 64)
                    goto err;

                if (addrbits == 16 ||
                    (addrbits == 32 && !(sok & BITS32)) ||
                    (addrbits == 64 && !(sok & BITS64)))
                    goto err;

                /* now reorganize base/index */
                if (s == 1 && bt != it && bt != -1 && it != -1 &&
                    ((hb == b && ht == EAH_NOTBASE) ||
                     (hb == i && ht == EAH_MAKEBASE))) {
                    /* swap if hints say so */
                    t = bt, bt = it, it = t;
                    x = bx, bx = ix, ix = x;
                }

                if (bt == -1 && s == 1 && !(hb == i && ht == EAH_NOTBASE)) {
                    /* make single reg base, unless hint */
                    bt = it, bx = ix, it = -1, ix = 0;
                }
                if (eaflags & EAF_MIB) {
                    /* only for mib operands */
                    if (it == -1 && (hb == b && ht == EAH_NOTBASE)) {
                        /*
                         * make a single reg index [reg*1].
                         * gas uses this form for an explicit index register.
                         */
                        it = bt, ix = bx, bt = -1, bx = 0, s = 1;
                    }
                    if ((ht == EAH_SUMMED) && bt == -1) {
                        /* separate once summed index into [base, index] */
                        bt = it, bx = ix, s--;
                    }
                } else {
                    if (((s == 2 && it != REG_NUM_ESP &&
                          (!(eaflags & EAF_TIMESTWO) || (ht == EAH_SUMMED))) ||
                         s == 3 || s == 5 || s == 9) && bt == -1) {
                        /* convert 3*EAX to EAX+2*EAX */
                        bt = it, bx = ix, s--;
                    }
                    if (it == -1 && (bt & 7) != REG_NUM_ESP &&
                        (eaflags & EAF_TIMESTWO) &&
                        (hb == b && ht == EAH_NOTBASE)) {
                        /*
                         * convert [NOSPLIT EAX*1]
                         * to sib format with 0x0 displacement - [EAX*1+0].
                         */
                        it = bt, ix = bx, bt = -1, bx = 0, s = 1;
                    }
                }
                if (s == 1 && it == REG_NUM_ESP) {
                    /* swap ESP into base if scale is 1 */
                    t = it, it = bt, bt = t;
                    x = ix, ix = bx, bx = x;
                }
                if (it == REG_NUM_ESP ||
                    (s != 1 && s != 2 && s != 4 && s != 8 && it != -1))
                    goto err;        /* wrong, for various reasons */

                output->rex |= rexflags(it, ix, REX_X);
                output->rex |= rexflags(bt, bx, REX_B);

                if (it == -1 && (bt & 7) != REG_NUM_ESP) {
                    /* no SIB needed */
                    int mod, rm;

                    if (bt == -1) {
                        rm = 5;
                        mod = 0;
                    } else {
                        rm = (bt & 7);
                        if (rm != REG_NUM_EBP && o == 0 &&
                            seg == NO_SEG && !forw_ref &&
                            !(eaflags & (EAF_BYTEOFFS | EAF_WORDOFFS)))
                            mod = 0;
                        else if (IS_MOD_01())
                            mod = 1;
                        else
                            mod = 2;
                    }

                    output->sib_present = false;
                    output->bytes       = (bt == -1 || mod == 2 ? 4 : mod);
                    output->modrm       = GEN_MODRM(mod, rfield, rm);
                } else {
                    /* we need a SIB */
                    int mod, scale, index, base;

                    if (it == -1)
                        index = 4, s = 1;
                    else
                        index = (it & 7);

                    switch (s) {
                    case 1:
                        scale = 0;
                        break;
                    case 2:
                        scale = 1;
                        break;
                    case 4:
                        scale = 2;
                        break;
                    case 8:
                        scale = 3;
                        break;
                    default:   /* then what the smeg is it? */
                        goto err;    /* panic */
                    }

                    if (bt == -1) {
                        base = 5;
                        mod = 0;
                    } else {
                        base = (bt & 7);
                        if (base != REG_NUM_EBP && o == 0 &&
                            seg == NO_SEG && !forw_ref &&
                            !(eaflags & (EAF_BYTEOFFS | EAF_WORDOFFS)))
                            mod = 0;
                        else if (IS_MOD_01())
                            mod = 1;
                        else
                            mod = 2;
                    }

                    output->sib_present = true;
                    output->bytes       = (bt == -1 || mod == 2 ? 4 : mod);
                    output->modrm       = GEN_MODRM(mod, rfield, 4);
                    output->sib         = GEN_SIB(scale, index, base);
                }
            } else {            /* it's 16-bit */
                int mod, rm;
                int16_t o = input->offset;

                /* check for 64-bit long mode */
                if (addrbits == 64)
                    goto err;

                /* check all registers are BX, BP, SI or DI */
                if ((b != -1 && b != R_BP && b != R_BX && b != R_SI && b != R_DI) ||
                    (i != -1 && i != R_BP && i != R_BX && i != R_SI && i != R_DI))
                    goto err;

                /* ensure the user didn't specify DWORD/QWORD */
                if (input->disp_size == 32 || input->disp_size == 64)
                    goto err;

                if (s != 1 && i != -1)
                    goto err;        /* no can do, in 16-bit EA */
                if (b == -1 && i != -1) {
                    int tmp = b;
                    b = i;
                    i = tmp;
                }               /* swap */
                if ((b == R_SI || b == R_DI) && i != -1) {
                    int tmp = b;
                    b = i;
                    i = tmp;
                }
                /* have BX/BP as base, SI/DI index */
                if (b == i)
                    goto err;        /* shouldn't ever happen, in theory */
                if (i != -1 && b != -1 &&
                    (i == R_BP || i == R_BX || b == R_SI || b == R_DI))
                    goto err;        /* invalid combinations */
                if (b == -1)            /* pure offset: handled above */
                    goto err;        /* so if it gets to here, panic! */

                rm = -1;
                if (i != -1)
                    switch (i * 256 + b) {
                    case R_SI * 256 + R_BX:
                        rm = 0;
                        break;
                    case R_DI * 256 + R_BX:
                        rm = 1;
                        break;
                    case R_SI * 256 + R_BP:
                        rm = 2;
                        break;
                    case R_DI * 256 + R_BP:
                        rm = 3;
                        break;
                } else
                    switch (b) {
                    case R_SI:
                        rm = 4;
                        break;
                    case R_DI:
                        rm = 5;
                        break;
                    case R_BP:
                        rm = 6;
                        break;
                    case R_BX:
                        rm = 7;
                        break;
                    }
                if (rm == -1)           /* can't happen, in theory */
                    goto err;        /* so panic if it does */

                if (o == 0 && seg == NO_SEG && !forw_ref && rm != 6 &&
                    !(eaflags & (EAF_BYTEOFFS | EAF_WORDOFFS)))
                    mod = 0;
                else if (IS_MOD_01())
                    mod = 1;
                else
                    mod = 2;

                output->sib_present = false;    /* no SIB - it's 16-bit */
                output->bytes       = mod;      /* bytes of offset needed */
                output->modrm       = GEN_MODRM(mod, rfield, rm);
            }
        }
    }

    output->size = 1 + output->sib_present + output->bytes;
    return output->type;

err:
    return output->type = EA_INVALID;
}

static void add_asp(insn *ins, int addrbits)
{
    int j, valid;
    int defdisp;

    valid = (addrbits == 64) ? 64|32 : 32|16;

    switch (ins->prefixes[PPS_ASIZE]) {
    case P_A16:
        valid &= 16;
        break;
    case P_A32:
        valid &= 32;
        break;
    case P_A64:
        valid &= 64;
        break;
    case P_ASP:
        valid &= (addrbits == 32) ? 16 : 32;
        break;
    default:
        break;
    }

    for (j = 0; j < ins->operands; j++) {
        if (is_class(MEMORY, ins->oprs[j].type)) {
            opflags_t i, b;

            /* Verify as Register */
            if (!is_register(ins->oprs[j].indexreg))
                i = 0;
            else
                i = nasm_reg_flags[ins->oprs[j].indexreg];

            /* Verify as Register */
            if (!is_register(ins->oprs[j].basereg))
                b = 0;
            else
                b = nasm_reg_flags[ins->oprs[j].basereg];

            if (ins->oprs[j].scale == 0)
                i = 0;

            if (!i && !b) {
                int ds = ins->oprs[j].disp_size;
                if ((addrbits != 64 && ds > 8) ||
                    (addrbits == 64 && ds == 16))
                    valid &= ds;
            } else {
                if (!(REG16 & ~b))
                    valid &= 16;
                if (!(REG32 & ~b))
                    valid &= 32;
                if (!(REG64 & ~b))
                    valid &= 64;

                if (!(REG16 & ~i))
                    valid &= 16;
                if (!(REG32 & ~i))
                    valid &= 32;
                if (!(REG64 & ~i))
                    valid &= 64;
            }
        }
    }

    if (valid & addrbits) {
        ins->addr_size = addrbits;
    } else if (valid & ((addrbits == 32) ? 16 : 32)) {
        /* Add an address size prefix */
        ins->prefixes[PPS_ASIZE] = (addrbits == 32) ? P_A16 : P_A32;;
        ins->addr_size = (addrbits == 32) ? 16 : 32;
    } else {
        /* Impossible... */
        nasm_error(ERR_NONFATAL, "impossible combination of address sizes");
        ins->addr_size = addrbits; /* Error recovery */
    }

    defdisp = ins->addr_size == 16 ? 16 : 32;

    for (j = 0; j < ins->operands; j++) {
        if (!(MEM_OFFS & ~ins->oprs[j].type) &&
            (ins->oprs[j].disp_size ? ins->oprs[j].disp_size : defdisp) != ins->addr_size) {
            /*
             * mem_offs sizes must match the address size; if not,
             * strip the MEM_OFFS bit and match only EA instructions
             */
            ins->oprs[j].type &= ~(MEM_OFFS & ~MEMORY);
        }
    }
}
