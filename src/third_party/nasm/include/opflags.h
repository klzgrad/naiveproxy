/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 1996-2024 The NASM Authors - All Rights Reserved */

/*
 * opflags.h - operand flags
 */

#ifndef NASM_OPFLAGS_H
#define NASM_OPFLAGS_H

#include "compiler.h"
#include "tables.h"
#include "regs.h"

/*
 * Here we define the operand types. These are implemented as bit
 * masks, since some are subsets of others; e.g. AX in a MOV
 * instruction is a special operand type, whereas AX in other
 * contexts is just another 16-bit register. (Also, consider CL in
 * shift instructions, DX in OUT, etc.)
 *
 * The basic concept here is that
 *    (class & ~operand) == 0
 *
 * if and only if "operand" belongs to class type "class".
 */

#define OP_GENMASK(bits, shift)         (((UINT64_C(1) << (bits)) - 1) << (shift))
#define OP_GENBIT(bit, shift)           (UINT64_C(1) << ((shift) + (bit)))

/*
 * Type of operand: memory reference, register, etc.
 *
 * Bits: 0 - 3
 */
#define OPTYPE_SHIFT            (0)
#define OPTYPE_BITS             (4)
#define OPTYPE_MASK             OP_GENMASK(OPTYPE_BITS, OPTYPE_SHIFT)
#define GEN_OPTYPE(bit)         OP_GENBIT(bit, OPTYPE_SHIFT)

/*
 * Modifiers.
 *
 * Bits: 4 - 6
 */
#define MODIFIER_SHIFT          (OPTYPE_SHIFT + OPTYPE_BITS)
#define MODIFIER_BITS           (3)
#define MODIFIER_MASK           OP_GENMASK(MODIFIER_BITS, MODIFIER_SHIFT)
#define GEN_MODIFIER(bit)       OP_GENBIT(bit, MODIFIER_SHIFT)

/*
 * Register classes.
 *
 * Bits: 7 - 17
 */
#define REG_CLASS_SHIFT         (MODIFIER_SHIFT + MODIFIER_BITS)
#define REG_CLASS_BITS          (11)
#define REG_CLASS_MASK          OP_GENMASK(REG_CLASS_BITS, REG_CLASS_SHIFT)
#define GEN_REG_CLASS(bit)      OP_GENBIT(bit, REG_CLASS_SHIFT)

/*
 * Subclasses. Depends on type of operand.
 *
 * Bits: 18 - 31
 */
#define SUBCLASS_SHIFT          (REG_CLASS_SHIFT + REG_CLASS_BITS)
#define SUBCLASS_BITS           (14)
#define SUBCLASS_MASK           OP_GENMASK(SUBCLASS_BITS, SUBCLASS_SHIFT)
#define GEN_SUBCLASS(bit)       OP_GENBIT(bit, SUBCLASS_SHIFT)

/*
 * Sizes of the operands and attributes.
 *
 * Bits: 32 - 47
 */
#define SIZE_SHIFT              (SUBCLASS_SHIFT + SUBCLASS_BITS)
#define SIZE_BITS               (16)
#define SIZE_MASK               OP_GENMASK(SIZE_BITS, SIZE_SHIFT)
#define GEN_SIZE(bit)           OP_GENBIT(bit, SIZE_SHIFT)

/*
 * Register set count
 *
 * Bits: 48 - 52
 */
#define REGSET_SHIFT            (SIZE_SHIFT + SIZE_BITS)
#define REGSET_BITS             (5)
#define REGSET_MASK             OP_GENMASK(REGSET_BITS, REGSET_SHIFT)
#define GEN_REGSET(bit)         OP_GENBIT(bit, REGSET_SHIFT)

/*
 * Remaining bits
 */
#define OPFLAGS_USED_BITS	(REGSET_SHIFT + REGSET_BITS)
#define OPFLAGS_UNUSED_BITS	(64-OPFLAGS_USED_BITS)

#define REGISTER                GEN_OPTYPE(0)                   /* register number in 'basereg' */
#define IMMEDIATE               GEN_OPTYPE(1)
#define REGMEM                  GEN_OPTYPE(2)                   /* for r/m, ie EA, operands */
#define MEMORY                  (GEN_OPTYPE(3) | REGMEM)

/* First, the actual sizes */
#define BITS8                   GEN_SIZE(0)                     /*   8 bits (BYTE) */
#define BITS16                  GEN_SIZE(1)                     /*  16 bits (WORD) */
#define BITS32                  GEN_SIZE(2)                     /*  32 bits (DWORD) */
#define BITS64                  GEN_SIZE(3)                     /*  64 bits (QWORD), x64 and FPU only */
#define BITS80                  GEN_SIZE(4)                     /*  80 bits (TWORD), FPU only */
#define BITS128                 GEN_SIZE(5)                     /* 128 bits (OWORD) */
#define BITS256                 GEN_SIZE(6)                     /* 256 bits (YWORD) */
#define BITS512                 GEN_SIZE(7)                     /* 512 bits (ZWORD) */

/* Then, size modifiers */
#define FAR                     GEN_SIZE(8)                     /* grotty: this means 16:16 or 16:32, like in CALL/JMP */
#define NEAR                    GEN_SIZE(9)
#define SHORT                   GEN_SIZE(10)                    /* and this means what it says :) */
#define ABS			GEN_SIZE(11)                    /* JMP ABS, MOV ABS */

/* Special modifiers */
#define TO                      GEN_MODIFIER(0)                 /* reverse effect in FADD, FSUB &c */
#define COLON                   GEN_MODIFIER(1)                 /* operand is followed by a colon */
#define STRICT                  GEN_MODIFIER(2)                 /* do not optimize this operand */

#define REG_CLASS_CDT           GEN_REG_CLASS(0)
#define REG_CLASS_GPR           GEN_REG_CLASS(1)
#define REG_CLASS_SREG          GEN_REG_CLASS(2)
#define REG_CLASS_FPUREG        GEN_REG_CLASS(3)
#define REG_CLASS_RM_MMX        GEN_REG_CLASS(4)
#define REG_CLASS_RM_XMM        GEN_REG_CLASS(5)
#define REG_CLASS_RM_YMM        GEN_REG_CLASS(6)
#define REG_CLASS_RM_ZMM        GEN_REG_CLASS(7)
#define REG_CLASS_OPMASK        GEN_REG_CLASS(8)
#define REG_CLASS_BND           GEN_REG_CLASS(9)
#define REG_CLASS_RM_TMM	GEN_REG_CLASS(10)

/* Register classes treated as vectors for EVEX/REX2 encoding purposes */
#define REG_CLASS_VECTOR	(REG_CLASS_RM_XMM|REG_CLASS_RM_YMM|\
                                 REG_CLASS_RM_ZMM|REG_CLASS_RM_TMM)

/* Helper function to test for a value matching a class */
static inline bool is_class(opflags_t class, opflags_t op)
{
    return !(class & ~op);
}

/* Verify a token value is a valid register */
static inline bool is_register(int reg)
{
    return reg >= EXPR_REG_START && reg < REG_ENUM_LIMIT;
}

/* Helper function to test if a token is a register matching a class */
static inline bool is_reg_class(opflags_t class, int reg)
{
    if (!is_register(reg))
        return false;

    return is_class(class, nasm_reg_flags[reg]);
}

#define IS_SREG(reg)                is_reg_class(REG_SREG, (reg))
#define IS_FSGS(reg)                is_reg_class(REG_FSGS, (reg))

/*
 * These are used across the RM classes so cannot conflict with neither EA
 * nor REG subtypes! x86/insns.pl automatically adds RN_FLAGS(x) to the
 * defintion of each register type.
 */
#define RN_L16			GEN_SUBCLASS(10)		/* Register number 0-15 */
#define RN_ZERO			(GEN_SUBCLASS(11) | RN_L16)	/* Register number 0 */
#define RN_NZERO		GEN_SUBCLASS(12)		/* Register number 1+ */
#define RN_1_15			(RN_NZERO | RN_L16)		/* Register number 1-15 */

#define RN_FLAGS(n)                             \
    (((n) == 0 ? RN_ZERO : RN_NZERO) |          \
     ((n) < 16 ? RN_L16 : 0))

#define RM_L16			(RN_L16   | REGMEM)
#define RM_ZERO			(RN_ZERO  | REGMEM)
#define RM_NZERO		(RN_NZERO | REGMEM)
#define RM_1_15			(RM_1_15  | REGMEM)
#define RM_SEL			(GEN_SUBCLASS(13) | REGMEM)	/* valid segment selector */

/* Register classes */

#define REG_L16			(RN_L16   | REGISTER)
#define REG_ZERO		(RN_ZERO  | REGISTER)
#define REG_NZERO		(RN_NZERO | REGISTER)
#define REG_1_15		(RN_1_15  | REGISTER)

#define REG_SMASK               SUBCLASS_MASK

#define REG_EA                  (                             REGMEM | REGISTER )      /* 'normal' reg, qualifies as EA */
#define RM_GPR                  (REG_CLASS_GPR              | REGMEM            )      /* integer operand */
#define REG_GPR                 (REG_CLASS_GPR              | REG_EA            )      /* integer register */
#define FPUREG                  (REG_CLASS_FPUREG           | REGISTER          )      /* fp stack registers */
#define FPU0                    (REG_CLASS_FPUREG           | REG_ZERO          )      /* fp stack register zero */
#define RM_MMX                  (REG_CLASS_RM_MMX           | REGMEM		  )    /* MMX operand */
#define MMXREG                  (REG_CLASS_RM_MMX           | REG_EA		  )    /* MMX register */
#define RM_XMM                  (REG_CLASS_RM_XMM           | REGMEM            )      /* XMM operand */
#define XMMREG                  (REG_CLASS_RM_XMM           | REG_EA            )      /* XMM register */
#define RM_XMM_L16              (REG_CLASS_RM_XMM           | REGMEM | RN_L16   )      /* XMM operand reg 0-15 */
#define XMM_L16                 (REG_CLASS_RM_XMM           | REG_EA | RN_L16   )      /* XMM register 0-15 */
#define XMM0                    (REG_CLASS_RM_XMM           | REGMEM | REG_ZERO )      /* XMM register 0 */
#define RM_YMM                  (REG_CLASS_RM_YMM           | REGMEM            )      /* YMM operand */
#define YMMREG                  (REG_CLASS_RM_YMM           | REG_EA            )      /* YMM register */
#define RM_YMM_L16              (REG_CLASS_RM_YMM           | REGMEM | RN_L16   )      /* YMM operand reg 0-15 */
#define YMM_L16                 (REG_CLASS_RM_YMM           | REG_EA | RN_L16   )      /* YMM register 0-15 */
#define YMM0                    (REG_CLASS_RM_YMM           | REGMEM | REG_ZERO )      /* YMM register 0 */
#define RM_ZMM                  (REG_CLASS_RM_ZMM           | REGMEM            )      /* ZMM operand */
#define ZMMREG                  (REG_CLASS_RM_ZMM           | REG_EA            )      /* ZMM register */
#define RM_ZMM_L16              (REG_CLASS_RM_ZMM           | REGMEM | RN_L16   )      /* ZMM operand reg 0-15 */
#define ZMM_L16                 (REG_CLASS_RM_ZMM           | REG_EA | RN_L16   )      /* ZMM register 0-15 */
#define ZMM0                    (REG_CLASS_RM_ZMM           | REGMEM | REG_ZERO )      /* ZMM register 0 */
#define RM_OPMASK               (REG_CLASS_OPMASK           | REGMEM            )      /* Opmask operand */
#define OPMASKREG               (REG_CLASS_OPMASK           | REG_EA            )      /* Opmask register */
#define OPMASK0                 (REG_CLASS_OPMASK           | REG_EA            )      /* Opmask register zero (k0) */
#define RM_K                    RM_OPMASK
#define KREG                    OPMASKREG
#define RM_BND                  (REG_CLASS_BND              | REGMEM            )      /* Bounds operand */
#define BNDREG                  (REG_CLASS_BND              | REG_EA            )      /* Bounds register */
#define TMMREG                  (REG_CLASS_RM_TMM           | REG_EA            )      /* TMM (AMX) register */

#define REG_CDT                 (REG_CLASS_CDT   | REGISTER)      /* CRn, DRn and TRn */
#define REG_CREG                (GEN_SUBCLASS(0) | REG_CDT )      /* CRn */
#define REG_DREG                (GEN_SUBCLASS(1) | REG_CDT )      /* DRn */
#define REG_TREG                (GEN_SUBCLASS(2) | REG_CDT )      /* TRn */

#define REG_SREG                (REG_CLASS_SREG  | REG_L16 | BITS16)        /* segment register */

/* Segment registers */
#define REG_ES                  (GEN_SUBCLASS(0) | GEN_SUBCLASS(2) | REG_SREG)      /* ES */
#define REG_CS                  (GEN_SUBCLASS(1) | GEN_SUBCLASS(2) | REG_SREG)      /* CS */
#define REG_SS                  (GEN_SUBCLASS(0) | GEN_SUBCLASS(3) | REG_SREG)      /* SS */
#define REG_DS                  (GEN_SUBCLASS(1) | GEN_SUBCLASS(3) | REG_SREG)      /* DS */
#define REG_FS                  (GEN_SUBCLASS(0) | GEN_SUBCLASS(4) | REG_SREG)      /* FS */
#define REG_GS                  (GEN_SUBCLASS(1) | GEN_SUBCLASS(4) | REG_SREG)      /* GS */
#define REG_FSGS                (                  GEN_SUBCLASS(4) | REG_SREG)      /* FS or GS */
#define REG_SEG6                (GEN_SUBCLASS(0) | GEN_SUBCLASS(5) | REG_SREG)
#define REG_SEG7                (GEN_SUBCLASS(1) | GEN_SUBCLASS(5) | REG_SREG)
#define REG_SEG67               (                  GEN_SUBCLASS(5) | REG_SREG)

/* GPRs */

#define REG8                    (REG_GPR   | BITS8 )
#define REG16                   (REG_GPR   | BITS16 | RM_SEL)
#define REG32                   (REG_GPR   | BITS32 | RM_SEL)
#define REG64                   (REG_GPR   | BITS64 | RM_SEL)

/* accumulator: AL, AX, EAX, RAX */
#define REG_ACCUM               (REG_GPR   | REG_ZERO)
#define REG_AL                  (REG_ACCUM | REG8 )
#define REG_AX                  (REG_ACCUM | REG16)
#define REG_EAX                 (REG_ACCUM | REG32)
#define REG_RAX			(REG_ACCUM | REG64)

/* counter: CL, CX, ECX, RDX */
#define REG_COUNT               (REG_GPR   | REG_1_15 | GEN_SUBCLASS(0))
#define REG_CL                  (REG_COUNT | REG8 )
#define REG_CX                  (REG_COUNT | REG16)
#define REG_ECX                 (REG_COUNT | REG32)
#define REG_RCX			(REG_COUNT | REG64)

/* data: DL, DX, EDX, RDX */
#define REG_DATA                (REG_GPR   | REG_1_15 | GEN_SUBCLASS(1))
#define REG_DL                  (REG_DATA  | REG8 )
#define REG_DX                  (REG_DATA  | REG16)
#define REG_EDX                 (REG_DATA  | REG32)
#define REG_RDX			(REG_DATA  | REG64)

/* base: BL, BX, EBX, RBX */
#define REG_BASE                (REG_GPR   | REG_1_15 | GEN_SUBCLASS(2))
#define REG_BL                  (REG_BASE  | REG8 )
#define REG_BX                  (REG_BASE  | REG16)
#define REG_EBX                 (REG_BASE  | REG32)
#define REG_RBX			(REG_BASE  | REG64)

/* high 8-bit regs: AH, CH, DH, BH */
#define REG_HIGH                (REG8      | REG_1_15 | GEN_SUBCLASS(3))

/* Non-accumulator registers */
#define REG_NA			(REG_GPR   | REG_NZERO)
#define REG8NA                  (REG_NA    | REG8 )
#define REG16NA                 (REG_NA    | REG16)
#define REG32NA                 (REG_NA    | REG32)
#define REG64NA                 (REG_NA    | REG64)

/* special types of EAs */
#define MEM_OFFS                (GEN_SUBCLASS(0) | MEMORY)      /* simple [address] offset - absolute! */
#define IP_REL                  (GEN_SUBCLASS(1) | MEMORY)      /* IP-relative offset */
#define XMEM                    (GEN_SUBCLASS(2) | MEMORY)      /* 128-bit vector SIB */
#define YMEM                    (GEN_SUBCLASS(3) | MEMORY)      /* 256-bit vector SIB */
#define ZMEM                    (GEN_SUBCLASS(4) | MEMORY)      /* 512-bit vector SIB */

/*
 * operand bits to match an instruction carrying any type of memory r/m operand
 */
#define MEMORY_ANY              (MEMORY | RM_GPR | RM_MMX | RM_XMM | RM_YMM | RM_ZMM | \
                                 RM_L16 | RM_ZERO | RM_NZERO | RM_OPMASK | RM_BND)

/* special immediate values */
#define IMM_NORMAL		(GEN_SUBCLASS(0) | IMMEDIATE)   /* operand is NOT a brcconst */
#define UNITY                   (GEN_SUBCLASS(1) | IMMEDIATE)   /* operand equals 1 */
#define SBYTEWORD               (GEN_SUBCLASS(2) | IMMEDIATE)   /* operand is in the range -128..127 mod 2^16 */
#define SBYTEDWORD              (GEN_SUBCLASS(3) | IMMEDIATE)   /* operand is in the range -128..127 mod 2^32 */
#define SDWORD                  (GEN_SUBCLASS(4) | IMMEDIATE)   /* operand is in the range -0x80000000..0x7FFFFFFF */
#define UDWORD                  (GEN_SUBCLASS(5) | IMMEDIATE)   /* operand is in the range 0..0xFFFFFFFF */
#define FOURBITS		(GEN_SUBCLASS(6) | IMMEDIATE)   /* operand is in the range 0-15 */
#define IMM_KNOWN		(GEN_SUBCLASS(7) | IMMEDIATE)   /* operand value is known at compile time */

#define BYTEEXTMASK		(GEN_SUBCLASS(2) | GEN_SUBCLASS(3))
#define DWORDEXTMASK		(GEN_SUBCLASS(4) | GEN_SUBCLASS(5))

/* Register set sizes */
#define RS2                     GEN_REGSET(0)
#define RS4                     GEN_REGSET(1)
#define RS8                     GEN_REGSET(2)
#define RS16                    GEN_REGSET(3)
#define RS32                    GEN_REGSET(4)

#endif /* NASM_OPFLAGS_H */
