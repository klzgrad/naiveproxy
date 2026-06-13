/* automatically generated from ./x86/regs.dat - do not edit */

#include "regdis.h"

const enum reg_enum nasm_rd_bndreg[DISREGTBLSZ] = {
    R_BND0, R_BND1, R_BND2, R_BND3, 0, 0, 0, 0,
    R_BND0, R_BND1, R_BND2, R_BND3, 0, 0, 0, 0,
    R_BND0, R_BND1, R_BND2, R_BND3, 0, 0, 0, 0,
    R_BND0, R_BND1, R_BND2, R_BND3, 0, 0, 0, 0
};
const enum reg_enum nasm_rd_creg[DISREGTBLSZ] = {
    R_CR0, R_CR1, R_CR2, R_CR3, R_CR4, R_CR5, R_CR6, R_CR7,
    R_CR8, R_CR9, R_CR10, R_CR11, R_CR12, R_CR13, R_CR14, R_CR15,
    R_CR16, R_CR17, R_CR18, R_CR19, R_CR20, R_CR21, R_CR22, R_CR23,
    R_CR24, R_CR25, R_CR26, R_CR27, R_CR28, R_CR29, R_CR30, R_CR31
};
const enum reg_enum nasm_rd_dreg[DISREGTBLSZ] = {
    R_DR0, R_DR1, R_DR2, R_DR3, R_DR4, R_DR5, R_DR6, R_DR7,
    R_DR8, R_DR9, R_DR10, R_DR11, R_DR12, R_DR13, R_DR14, R_DR15,
    R_DR16, R_DR17, R_DR18, R_DR19, R_DR20, R_DR21, R_DR22, R_DR23,
    R_DR24, R_DR25, R_DR26, R_DR27, R_DR28, R_DR29, R_DR30, R_DR31
};
const enum reg_enum nasm_rd_fpureg[DISREGTBLSZ] = {
    R_ST0, R_ST1, R_ST2, R_ST3, R_ST4, R_ST5, R_ST6, R_ST7,
    R_ST0, R_ST1, R_ST2, R_ST3, R_ST4, R_ST5, R_ST6, R_ST7,
    R_ST0, R_ST1, R_ST2, R_ST3, R_ST4, R_ST5, R_ST6, R_ST7,
    R_ST0, R_ST1, R_ST2, R_ST3, R_ST4, R_ST5, R_ST6, R_ST7
};
const enum reg_enum nasm_rd_mmxreg[DISREGTBLSZ] = {
    R_MM0, R_MM1, R_MM2, R_MM3, R_MM4, R_MM5, R_MM6, R_MM7,
    R_MM0, R_MM1, R_MM2, R_MM3, R_MM4, R_MM5, R_MM6, R_MM7,
    R_MM0, R_MM1, R_MM2, R_MM3, R_MM4, R_MM5, R_MM6, R_MM7,
    R_MM0, R_MM1, R_MM2, R_MM3, R_MM4, R_MM5, R_MM6, R_MM7
};
const enum reg_enum nasm_rd_opmaskreg[DISREGTBLSZ] = {
    R_K0, R_K1, R_K2, R_K3, R_K4, R_K5, R_K6, R_K7,
    R_K0, R_K1, R_K2, R_K3, R_K4, R_K5, R_K6, R_K7,
    R_K0, R_K1, R_K2, R_K3, R_K4, R_K5, R_K6, R_K7,
    R_K0, R_K1, R_K2, R_K3, R_K4, R_K5, R_K6, R_K7
};
const enum reg_enum nasm_rd_reg16[DISREGTBLSZ] = {
    R_AX, R_CX, R_DX, R_BX, R_SP, R_BP, R_SI, R_DI,
    R_R8W, R_R9W, R_R10W, R_R11W, R_R12W, R_R13W, R_R14W, R_R15W,
    R_R16W, R_R17W, R_R18W, R_R19W, R_R20W, R_R21W, R_R22W, R_R23W,
    R_R24W, R_R25W, R_R26W, R_R27W, R_R28W, R_R29W, R_R30W, R_R31W
};
const enum reg_enum nasm_rd_reg32[DISREGTBLSZ] = {
    R_EAX, R_ECX, R_EDX, R_EBX, R_ESP, R_EBP, R_ESI, R_EDI,
    R_R8D, R_R9D, R_R10D, R_R11D, R_R12D, R_R13D, R_R14D, R_R15D,
    R_R16D, R_R17D, R_R18D, R_R19D, R_R20D, R_R21D, R_R22D, R_R23D,
    R_R24D, R_R25D, R_R26D, R_R27D, R_R28D, R_R29D, R_R30D, R_R31D
};
const enum reg_enum nasm_rd_reg64[DISREGTBLSZ] = {
    R_RAX, R_RCX, R_RDX, R_RBX, R_RSP, R_RBP, R_RSI, R_RDI,
    R_R8, R_R9, R_R10, R_R11, R_R12, R_R13, R_R14, R_R15,
    R_R16, R_R17, R_R18, R_R19, R_R20, R_R21, R_R22, R_R23,
    R_R24, R_R25, R_R26, R_R27, R_R28, R_R29, R_R30, R_R31
};
const enum reg_enum nasm_rd_reg8[DISREGTBLSZ] = {
    R_AL, R_CL, R_DL, R_BL, R_AH, R_CH, R_DH, R_BH,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};
const enum reg_enum nasm_rd_reg8_rex[DISREGTBLSZ] = {
    R_AL, R_CL, R_DL, R_BL, R_SPL, R_BPL, R_SIL, R_DIL,
    R_R8B, R_R9B, R_R10B, R_R11B, R_R12B, R_R13B, R_R14B, R_R15B,
    R_R16B, R_R17B, R_R18B, R_R19B, R_R20B, R_R21B, R_R22B, R_R23B,
    R_R24B, R_R25B, R_R26B, R_R27B, R_R28B, R_R29B, R_R30B, R_R31B
};
const enum reg_enum nasm_rd_sreg[DISREGTBLSZ] = {
    R_ES, R_CS, R_SS, R_DS, R_FS, R_GS, R_SEGR6, R_SEGR7,
    R_ES, R_CS, R_SS, R_DS, R_FS, R_GS, R_SEGR6, R_SEGR7,
    R_ES, R_CS, R_SS, R_DS, R_FS, R_GS, R_SEGR6, R_SEGR7,
    R_ES, R_CS, R_SS, R_DS, R_FS, R_GS, R_SEGR6, R_SEGR7
};
const enum reg_enum nasm_rd_tmmreg[DISREGTBLSZ] = {
    R_TMM0, R_TMM1, R_TMM2, R_TMM3, R_TMM4, R_TMM5, R_TMM6, R_TMM7,
    R_TMM0, R_TMM1, R_TMM2, R_TMM3, R_TMM4, R_TMM5, R_TMM6, R_TMM7,
    R_TMM0, R_TMM1, R_TMM2, R_TMM3, R_TMM4, R_TMM5, R_TMM6, R_TMM7,
    R_TMM0, R_TMM1, R_TMM2, R_TMM3, R_TMM4, R_TMM5, R_TMM6, R_TMM7
};
const enum reg_enum nasm_rd_treg[DISREGTBLSZ] = {
    R_TR0, R_TR1, R_TR2, R_TR3, R_TR4, R_TR5, R_TR6, R_TR7,
    R_TR0, R_TR1, R_TR2, R_TR3, R_TR4, R_TR5, R_TR6, R_TR7,
    R_TR0, R_TR1, R_TR2, R_TR3, R_TR4, R_TR5, R_TR6, R_TR7,
    R_TR0, R_TR1, R_TR2, R_TR3, R_TR4, R_TR5, R_TR6, R_TR7
};
const enum reg_enum nasm_rd_xmmreg[DISREGTBLSZ] = {
    R_XMM0, R_XMM1, R_XMM2, R_XMM3, R_XMM4, R_XMM5, R_XMM6, R_XMM7,
    R_XMM8, R_XMM9, R_XMM10, R_XMM11, R_XMM12, R_XMM13, R_XMM14, R_XMM15,
    R_XMM16, R_XMM17, R_XMM18, R_XMM19, R_XMM20, R_XMM21, R_XMM22, R_XMM23,
    R_XMM24, R_XMM25, R_XMM26, R_XMM27, R_XMM28, R_XMM29, R_XMM30, R_XMM31
};
const enum reg_enum nasm_rd_ymmreg[DISREGTBLSZ] = {
    R_YMM0, R_YMM1, R_YMM2, R_YMM3, R_YMM4, R_YMM5, R_YMM6, R_YMM7,
    R_YMM8, R_YMM9, R_YMM10, R_YMM11, R_YMM12, R_YMM13, R_YMM14, R_YMM15,
    R_YMM16, R_YMM17, R_YMM18, R_YMM19, R_YMM20, R_YMM21, R_YMM22, R_YMM23,
    R_YMM24, R_YMM25, R_YMM26, R_YMM27, R_YMM28, R_YMM29, R_YMM30, R_YMM31
};
const enum reg_enum nasm_rd_zmmreg[DISREGTBLSZ] = {
    R_ZMM0, R_ZMM1, R_ZMM2, R_ZMM3, R_ZMM4, R_ZMM5, R_ZMM6, R_ZMM7,
    R_ZMM8, R_ZMM9, R_ZMM10, R_ZMM11, R_ZMM12, R_ZMM13, R_ZMM14, R_ZMM15,
    R_ZMM16, R_ZMM17, R_ZMM18, R_ZMM19, R_ZMM20, R_ZMM21, R_ZMM22, R_ZMM23,
    R_ZMM24, R_ZMM25, R_ZMM26, R_ZMM27, R_ZMM28, R_ZMM29, R_ZMM30, R_ZMM31
};
