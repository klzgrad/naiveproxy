/* This file auto-generated from insns.dat by insns.pl - don't edit it */

#include "nasm.h"
#include "insns.h"

static const struct itemplate instrux_DB[] = {
    ITEMPLATE_END
};

static const struct itemplate instrux_DW[] = {
    ITEMPLATE_END
};

static const struct itemplate instrux_DD[] = {
    ITEMPLATE_END
};

static const struct itemplate instrux_DQ[] = {
    ITEMPLATE_END
};

static const struct itemplate instrux_DT[] = {
    ITEMPLATE_END
};

static const struct itemplate instrux_DO[] = {
    ITEMPLATE_END
};

static const struct itemplate instrux_DY[] = {
    ITEMPLATE_END
};

static const struct itemplate instrux_DZ[] = {
    ITEMPLATE_END
};

static const struct itemplate instrux_RESB[] = {
    {I_RESB, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40664, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_RESW[] = {
    ITEMPLATE_END
};

static const struct itemplate instrux_RESD[] = {
    ITEMPLATE_END
};

static const struct itemplate instrux_RESQ[] = {
    ITEMPLATE_END
};

static const struct itemplate instrux_REST[] = {
    ITEMPLATE_END
};

static const struct itemplate instrux_RESO[] = {
    ITEMPLATE_END
};

static const struct itemplate instrux_RESY[] = {
    ITEMPLATE_END
};

static const struct itemplate instrux_RESZ[] = {
    ITEMPLATE_END
};

static const struct itemplate instrux_INCBIN[] = {
    ITEMPLATE_END
};

static const struct itemplate instrux_AAA[] = {
    {I_AAA, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41526, 1},
    ITEMPLATE_END
};

static const struct itemplate instrux_AAD[] = {
    {I_AAD, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40526, 1},
    {I_AAD, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40530, 2},
    ITEMPLATE_END
};

static const struct itemplate instrux_AAM[] = {
    {I_AAM, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40534, 1},
    {I_AAM, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40538, 2},
    ITEMPLATE_END
};

static const struct itemplate instrux_AAS[] = {
    {I_AAS, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41529, 1},
    ITEMPLATE_END
};

static const struct itemplate instrux_ADC[] = {
    {I_ADC, 2, {MEMORY,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+38716, 3},
    {I_ADC, 2, {REG_GPR|BITS8,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+38717, 0},
    {I_ADC, 2, {MEMORY,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35008, 3},
    {I_ADC, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35009, 0},
    {I_ADC, 2, {MEMORY,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35014, 4},
    {I_ADC, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35015, 5},
    {I_ADC, 2, {MEMORY,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+35020, 6},
    {I_ADC, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+35021, 7},
    {I_ADC, 2, {REG_GPR|BITS8,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+29201, 8},
    {I_ADC, 2, {REG_GPR|BITS8,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+29201, 0},
    {I_ADC, 2, {REG_GPR|BITS16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+38721, 8},
    {I_ADC, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+38721, 0},
    {I_ADC, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+38726, 9},
    {I_ADC, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+38726, 5},
    {I_ADC, 2, {REG_GPR|BITS64,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+38731, 10},
    {I_ADC, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+38731, 7},
    {I_ADC, 2, {RM_GPR|BITS16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+25740, 11},
    {I_ADC, 2, {RM_GPR|BITS32,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+25747, 12},
    {I_ADC, 2, {RM_GPR|BITS64,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+25754, 13},
    {I_ADC, 2, {REG_AL,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+40542, 8},
    {I_ADC, 2, {REG_AX,SBYTEWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+25741, 8},
    {I_ADC, 2, {REG_AX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+38736, 8},
    {I_ADC, 2, {REG_EAX,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+25748, 9},
    {I_ADC, 2, {REG_EAX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+38741, 9},
    {I_ADC, 2, {REG_RAX,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+25755, 10},
    {I_ADC, 2, {REG_RAX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+38746, 10},
    {I_ADC, 2, {RM_GPR|BITS8,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+35026, 3},
    {I_ADC, 2, {RM_GPR|BITS16,SBYTEWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+25740, 3},
    {I_ADC, 2, {RM_GPR|BITS16,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+25761, 3},
    {I_ADC, 2, {RM_GPR|BITS32,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+25747, 4},
    {I_ADC, 2, {RM_GPR|BITS32,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+25768, 4},
    {I_ADC, 2, {RM_GPR|BITS64,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+25754, 6},
    {I_ADC, 2, {RM_GPR|BITS64,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+25775, 6},
    {I_ADC, 2, {MEMORY,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35026, 3},
    {I_ADC, 2, {MEMORY,SBYTEWORD|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+25740, 3},
    {I_ADC, 2, {MEMORY,IMMEDIATE|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+25761, 3},
    {I_ADC, 2, {MEMORY,SBYTEDWORD|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+25747, 4},
    {I_ADC, 2, {MEMORY,IMMEDIATE|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+25768, 4},
    {I_ADC, 2, {RM_GPR|BITS8,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+35032, 14},
    ITEMPLATE_END
};

static const struct itemplate instrux_ADD[] = {
    {I_ADD, 2, {MEMORY,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+38751, 3},
    {I_ADD, 2, {REG_GPR|BITS8,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+38752, 0},
    {I_ADD, 2, {MEMORY,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35038, 3},
    {I_ADD, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35039, 0},
    {I_ADD, 2, {MEMORY,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35044, 4},
    {I_ADD, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35045, 5},
    {I_ADD, 2, {MEMORY,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+35050, 6},
    {I_ADD, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+35051, 7},
    {I_ADD, 2, {REG_GPR|BITS8,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+32974, 8},
    {I_ADD, 2, {REG_GPR|BITS8,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+32974, 0},
    {I_ADD, 2, {REG_GPR|BITS16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+38756, 8},
    {I_ADD, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+38756, 0},
    {I_ADD, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+38761, 9},
    {I_ADD, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+38761, 5},
    {I_ADD, 2, {REG_GPR|BITS64,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+38766, 10},
    {I_ADD, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+38766, 7},
    {I_ADD, 2, {RM_GPR|BITS16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+25782, 11},
    {I_ADD, 2, {RM_GPR|BITS32,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+25789, 12},
    {I_ADD, 2, {RM_GPR|BITS64,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+25796, 13},
    {I_ADD, 2, {REG_AL,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+40546, 8},
    {I_ADD, 2, {REG_AX,SBYTEWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+25783, 8},
    {I_ADD, 2, {REG_AX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+38771, 8},
    {I_ADD, 2, {REG_EAX,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+25790, 9},
    {I_ADD, 2, {REG_EAX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+38776, 9},
    {I_ADD, 2, {REG_RAX,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+25797, 10},
    {I_ADD, 2, {REG_RAX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+38781, 10},
    {I_ADD, 2, {RM_GPR|BITS8,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+35056, 3},
    {I_ADD, 2, {RM_GPR|BITS16,SBYTEWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+25782, 3},
    {I_ADD, 2, {RM_GPR|BITS16,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+25803, 3},
    {I_ADD, 2, {RM_GPR|BITS32,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+25789, 4},
    {I_ADD, 2, {RM_GPR|BITS32,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+25810, 4},
    {I_ADD, 2, {RM_GPR|BITS64,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+25796, 6},
    {I_ADD, 2, {RM_GPR|BITS64,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+25817, 6},
    {I_ADD, 2, {MEMORY,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35056, 3},
    {I_ADD, 2, {MEMORY,SBYTEWORD|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+25782, 3},
    {I_ADD, 2, {MEMORY,IMMEDIATE|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+25803, 3},
    {I_ADD, 2, {MEMORY,SBYTEDWORD|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+25789, 4},
    {I_ADD, 2, {MEMORY,IMMEDIATE|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+25810, 4},
    {I_ADD, 2, {RM_GPR|BITS8,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+35062, 14},
    ITEMPLATE_END
};

static const struct itemplate instrux_AND[] = {
    {I_AND, 2, {MEMORY,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+38786, 3},
    {I_AND, 2, {REG_GPR|BITS8,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+38787, 0},
    {I_AND, 2, {MEMORY,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35068, 3},
    {I_AND, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35069, 0},
    {I_AND, 2, {MEMORY,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35074, 4},
    {I_AND, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35075, 5},
    {I_AND, 2, {MEMORY,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+35080, 6},
    {I_AND, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+35081, 7},
    {I_AND, 2, {REG_GPR|BITS8,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+33254, 8},
    {I_AND, 2, {REG_GPR|BITS8,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+33254, 0},
    {I_AND, 2, {REG_GPR|BITS16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+38791, 8},
    {I_AND, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+38791, 0},
    {I_AND, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+38796, 9},
    {I_AND, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+38796, 5},
    {I_AND, 2, {REG_GPR|BITS64,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+38801, 10},
    {I_AND, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+38801, 7},
    {I_AND, 2, {RM_GPR|BITS16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+25824, 11},
    {I_AND, 2, {RM_GPR|BITS32,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+25831, 12},
    {I_AND, 2, {RM_GPR|BITS64,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+25838, 13},
    {I_AND, 2, {REG_AL,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+40550, 8},
    {I_AND, 2, {REG_AX,SBYTEWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+25825, 8},
    {I_AND, 2, {REG_AX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+38806, 8},
    {I_AND, 2, {REG_EAX,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+25832, 9},
    {I_AND, 2, {REG_EAX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+38811, 9},
    {I_AND, 2, {REG_RAX,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+25839, 10},
    {I_AND, 2, {REG_RAX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+38816, 10},
    {I_AND, 2, {RM_GPR|BITS8,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+35086, 3},
    {I_AND, 2, {RM_GPR|BITS16,SBYTEWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+25824, 3},
    {I_AND, 2, {RM_GPR|BITS16,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+25845, 3},
    {I_AND, 2, {RM_GPR|BITS32,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+25831, 4},
    {I_AND, 2, {RM_GPR|BITS32,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+25852, 4},
    {I_AND, 2, {RM_GPR|BITS64,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+25838, 6},
    {I_AND, 2, {RM_GPR|BITS64,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+25859, 6},
    {I_AND, 2, {MEMORY,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35086, 3},
    {I_AND, 2, {MEMORY,SBYTEWORD|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+25824, 3},
    {I_AND, 2, {MEMORY,IMMEDIATE|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+25845, 3},
    {I_AND, 2, {MEMORY,SBYTEDWORD|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+25831, 4},
    {I_AND, 2, {MEMORY,IMMEDIATE|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+25852, 4},
    {I_AND, 2, {RM_GPR|BITS8,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+35092, 14},
    ITEMPLATE_END
};

static const struct itemplate instrux_ARPL[] = {
    {I_ARPL, 2, {MEMORY,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+25280, 15},
    {I_ARPL, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+25280, 16},
    ITEMPLATE_END
};

static const struct itemplate instrux_BB0_RESET[] = {
    {I_BB0_RESET, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40554, 17},
    ITEMPLATE_END
};

static const struct itemplate instrux_BB1_RESET[] = {
    {I_BB1_RESET, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40558, 17},
    ITEMPLATE_END
};

static const struct itemplate instrux_BOUND[] = {
    {I_BOUND, 2, {REG_GPR|BITS16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+38821, 18},
    {I_BOUND, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+38826, 19},
    ITEMPLATE_END
};

static const struct itemplate instrux_BSF[] = {
    {I_BSF, 2, {REG_GPR|BITS16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+25866, 9},
    {I_BSF, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+25866, 5},
    {I_BSF, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+25873, 9},
    {I_BSF, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+25873, 5},
    {I_BSF, 2, {REG_GPR|BITS64,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+25880, 10},
    {I_BSF, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+25880, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_BSR[] = {
    {I_BSR, 2, {REG_GPR|BITS16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+25887, 9},
    {I_BSR, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+25887, 5},
    {I_BSR, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+25894, 9},
    {I_BSR, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+25894, 5},
    {I_BSR, 2, {REG_GPR|BITS64,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+25901, 10},
    {I_BSR, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+25901, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_BSWAP[] = {
    {I_BSWAP, 1, {REG_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35098, 20},
    {I_BSWAP, 1, {REG_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35104, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_BT[] = {
    {I_BT, 2, {MEMORY,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35110, 9},
    {I_BT, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35110, 5},
    {I_BT, 2, {MEMORY,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35116, 9},
    {I_BT, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35116, 5},
    {I_BT, 2, {MEMORY,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+35122, 10},
    {I_BT, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+35122, 7},
    {I_BT, 2, {RM_GPR|BITS16,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+25908, 21},
    {I_BT, 2, {RM_GPR|BITS32,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+25915, 21},
    {I_BT, 2, {RM_GPR|BITS64,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+25922, 22},
    ITEMPLATE_END
};

static const struct itemplate instrux_BTC[] = {
    {I_BTC, 2, {MEMORY,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+25929, 4},
    {I_BTC, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+25930, 5},
    {I_BTC, 2, {MEMORY,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+25936, 4},
    {I_BTC, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+25937, 5},
    {I_BTC, 2, {MEMORY,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+25943, 6},
    {I_BTC, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+25944, 7},
    {I_BTC, 2, {RM_GPR|BITS16,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+8340, 23},
    {I_BTC, 2, {RM_GPR|BITS32,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+8348, 23},
    {I_BTC, 2, {RM_GPR|BITS64,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+8356, 24},
    ITEMPLATE_END
};

static const struct itemplate instrux_BTR[] = {
    {I_BTR, 2, {MEMORY,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+25950, 4},
    {I_BTR, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+25951, 5},
    {I_BTR, 2, {MEMORY,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+25957, 4},
    {I_BTR, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+25958, 5},
    {I_BTR, 2, {MEMORY,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+25964, 6},
    {I_BTR, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+25965, 7},
    {I_BTR, 2, {RM_GPR|BITS16,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+8364, 23},
    {I_BTR, 2, {RM_GPR|BITS32,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+8372, 23},
    {I_BTR, 2, {RM_GPR|BITS64,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+8380, 24},
    ITEMPLATE_END
};

static const struct itemplate instrux_BTS[] = {
    {I_BTS, 2, {MEMORY,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+25971, 4},
    {I_BTS, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+25972, 5},
    {I_BTS, 2, {MEMORY,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+25978, 4},
    {I_BTS, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+25979, 5},
    {I_BTS, 2, {MEMORY,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+25985, 6},
    {I_BTS, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+25986, 7},
    {I_BTS, 2, {RM_GPR|BITS16,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+8388, 23},
    {I_BTS, 2, {RM_GPR|BITS32,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+8396, 23},
    {I_BTS, 2, {RM_GPR|BITS64,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+8404, 24},
    ITEMPLATE_END
};

static const struct itemplate instrux_CALL[] = {
    {I_CALL, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38831, 25},
    {I_CALL, 1, {IMMEDIATE|NEAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38831, 25},
    {I_CALL, 1, {IMMEDIATE|FAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35128, 1},
    {I_CALL, 1, {IMMEDIATE|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38836, 26},
    {I_CALL, 1, {IMMEDIATE|BITS16|NEAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38836, 26},
    {I_CALL, 1, {IMMEDIATE|BITS16|FAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35134, 1},
    {I_CALL, 1, {IMMEDIATE|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38841, 27},
    {I_CALL, 1, {IMMEDIATE|BITS32|NEAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38841, 27},
    {I_CALL, 1, {IMMEDIATE|BITS32|FAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35140, 19},
    {I_CALL, 1, {IMMEDIATE|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38846, 28},
    {I_CALL, 1, {IMMEDIATE|BITS64|NEAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38846, 28},
    {I_CALL, 2, {IMMEDIATE|COLON,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+35146, 1},
    {I_CALL, 2, {IMMEDIATE|BITS16|COLON,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+35152, 1},
    {I_CALL, 2, {IMMEDIATE|COLON,IMMEDIATE|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35152, 1},
    {I_CALL, 2, {IMMEDIATE|BITS32|COLON,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+35158, 19},
    {I_CALL, 2, {IMMEDIATE|COLON,IMMEDIATE|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35158, 19},
    {I_CALL, 1, {MEMORY|FAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38851, 1},
    {I_CALL, 1, {MEMORY|FAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38856, 7},
    {I_CALL, 1, {MEMORY|BITS16|FAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38861, 0},
    {I_CALL, 1, {MEMORY|BITS32|FAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38866, 5},
    {I_CALL, 1, {MEMORY|BITS64|FAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38856, 7},
    {I_CALL, 1, {MEMORY|NEAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38871, 25},
    {I_CALL, 1, {RM_GPR|BITS16|NEAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38876, 26},
    {I_CALL, 1, {RM_GPR|BITS32|NEAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38881, 27},
    {I_CALL, 1, {RM_GPR|BITS64|NEAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38886, 28},
    {I_CALL, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38871, 25},
    {I_CALL, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38876, 26},
    {I_CALL, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38881, 27},
    {I_CALL, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38886, 28},
    ITEMPLATE_END
};

static const struct itemplate instrux_CBW[] = {
    {I_CBW, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40562, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_CDQ[] = {
    {I_CDQ, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40566, 5},
    ITEMPLATE_END
};

static const struct itemplate instrux_CDQE[] = {
    {I_CDQE, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40570, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_CLC[] = {
    {I_CLC, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40293, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_CLD[] = {
    {I_CLD, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40518, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_CLI[] = {
    {I_CLI, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39608, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_CLTS[] = {
    {I_CLTS, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40574, 29},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMC[] = {
    {I_CMC, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41532, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMP[] = {
    {I_CMP, 2, {MEMORY,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+40578, 8},
    {I_CMP, 2, {REG_GPR|BITS8,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+40578, 0},
    {I_CMP, 2, {MEMORY,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+38891, 8},
    {I_CMP, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+38891, 0},
    {I_CMP, 2, {MEMORY,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+38896, 9},
    {I_CMP, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+38896, 5},
    {I_CMP, 2, {MEMORY,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+38901, 10},
    {I_CMP, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+38901, 7},
    {I_CMP, 2, {REG_GPR|BITS8,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+33212, 8},
    {I_CMP, 2, {REG_GPR|BITS8,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+33212, 0},
    {I_CMP, 2, {REG_GPR|BITS16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+38906, 8},
    {I_CMP, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+38906, 0},
    {I_CMP, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+38911, 9},
    {I_CMP, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+38911, 5},
    {I_CMP, 2, {REG_GPR|BITS64,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+38916, 10},
    {I_CMP, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+38916, 7},
    {I_CMP, 2, {RM_GPR|BITS16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35164, 0},
    {I_CMP, 2, {RM_GPR|BITS32,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35170, 5},
    {I_CMP, 2, {RM_GPR|BITS64,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35176, 7},
    {I_CMP, 2, {REG_AL,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+40582, 8},
    {I_CMP, 2, {REG_AX,SBYTEWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+35164, 8},
    {I_CMP, 2, {REG_AX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+38921, 8},
    {I_CMP, 2, {REG_EAX,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+35170, 9},
    {I_CMP, 2, {REG_EAX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+38926, 9},
    {I_CMP, 2, {REG_RAX,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+35176, 10},
    {I_CMP, 2, {REG_RAX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+38931, 10},
    {I_CMP, 2, {RM_GPR|BITS8,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+38936, 8},
    {I_CMP, 2, {RM_GPR|BITS16,SBYTEWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+35164, 8},
    {I_CMP, 2, {RM_GPR|BITS16,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+35182, 8},
    {I_CMP, 2, {RM_GPR|BITS32,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+35170, 9},
    {I_CMP, 2, {RM_GPR|BITS32,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+35188, 9},
    {I_CMP, 2, {RM_GPR|BITS64,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+35176, 10},
    {I_CMP, 2, {RM_GPR|BITS64,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+35194, 10},
    {I_CMP, 2, {MEMORY,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+38936, 8},
    {I_CMP, 2, {MEMORY,SBYTEWORD|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35164, 8},
    {I_CMP, 2, {MEMORY,IMMEDIATE|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35182, 8},
    {I_CMP, 2, {MEMORY,SBYTEDWORD|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35170, 9},
    {I_CMP, 2, {MEMORY,IMMEDIATE|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35188, 9},
    {I_CMP, 2, {RM_GPR|BITS8,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+38941, 30},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPSB[] = {
    {I_CMPSB, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40586, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPSD[] = {
    {I_CMPSD, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38946, 5},
    {I_CMPSD, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+27112, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPSQ[] = {
    {I_CMPSQ, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38951, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPSW[] = {
    {I_CMPSW, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38956, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPXCHG[] = {
    {I_CMPXCHG, 2, {MEMORY,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35200, 31},
    {I_CMPXCHG, 2, {REG_GPR|BITS8,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35201, 32},
    {I_CMPXCHG, 2, {MEMORY,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+25992, 31},
    {I_CMPXCHG, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+25993, 32},
    {I_CMPXCHG, 2, {MEMORY,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+25999, 31},
    {I_CMPXCHG, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+26000, 32},
    {I_CMPXCHG, 2, {MEMORY,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+26006, 6},
    {I_CMPXCHG, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+26007, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPXCHG486[] = {
    {I_CMPXCHG486, 2, {MEMORY,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+38961, 33},
    {I_CMPXCHG486, 2, {REG_GPR|BITS8,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+38961, 34},
    {I_CMPXCHG486, 2, {MEMORY,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35206, 33},
    {I_CMPXCHG486, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35206, 34},
    {I_CMPXCHG486, 2, {MEMORY,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35212, 33},
    {I_CMPXCHG486, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35212, 34},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPXCHG8B[] = {
    {I_CMPXCHG8B, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+26013, 35},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPXCHG16B[] = {
    {I_CMPXCHG16B, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35218, 13},
    ITEMPLATE_END
};

static const struct itemplate instrux_CPUID[] = {
    {I_CPUID, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40590, 32},
    ITEMPLATE_END
};

static const struct itemplate instrux_CPU_READ[] = {
    {I_CPU_READ, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40594, 36},
    ITEMPLATE_END
};

static const struct itemplate instrux_CPU_WRITE[] = {
    {I_CPU_WRITE, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40598, 36},
    ITEMPLATE_END
};

static const struct itemplate instrux_CQO[] = {
    {I_CQO, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40602, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_CWD[] = {
    {I_CWD, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40606, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_CWDE[] = {
    {I_CWDE, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40610, 5},
    ITEMPLATE_END
};

static const struct itemplate instrux_DAA[] = {
    {I_DAA, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41535, 1},
    ITEMPLATE_END
};

static const struct itemplate instrux_DAS[] = {
    {I_DAS, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41538, 1},
    ITEMPLATE_END
};

static const struct itemplate instrux_DEC[] = {
    {I_DEC, 1, {REG_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40614, 1},
    {I_DEC, 1, {REG_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40618, 19},
    {I_DEC, 1, {RM_GPR|BITS8,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38966, 11},
    {I_DEC, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35224, 11},
    {I_DEC, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35230, 12},
    {I_DEC, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35236, 13},
    ITEMPLATE_END
};

static const struct itemplate instrux_DIV[] = {
    {I_DIV, 1, {RM_GPR|BITS8,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40622, 0},
    {I_DIV, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38971, 0},
    {I_DIV, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38976, 5},
    {I_DIV, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38981, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_DMINT[] = {
    {I_DMINT, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40626, 37},
    ITEMPLATE_END
};

static const struct itemplate instrux_EMMS[] = {
    {I_EMMS, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40630, 38},
    ITEMPLATE_END
};

static const struct itemplate instrux_ENTER[] = {
    {I_ENTER, 2, {IMMEDIATE,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+38986, 39},
    ITEMPLATE_END
};

static const struct itemplate instrux_EQU[] = {
    {I_EQU, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41570, 0},
    {I_EQU, 2, {IMMEDIATE|COLON,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+41570, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_F2XM1[] = {
    {I_F2XM1, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40634, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FABS[] = {
    {I_FABS, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40638, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FADD[] = {
    {I_FADD, 1, {MEMORY|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40642, 40},
    {I_FADD, 1, {MEMORY|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40646, 40},
    {I_FADD, 1, {FPUREG|TO,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38991, 40},
    {I_FADD, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38996, 40},
    {I_FADD, 2, {FPUREG,FPU0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38991, 40},
    {I_FADD, 2, {FPU0,FPUREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39001, 40},
    {I_FADD, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40650, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FADDP[] = {
    {I_FADDP, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39006, 40},
    {I_FADDP, 2, {FPUREG,FPU0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39006, 40},
    {I_FADDP, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40650, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FBLD[] = {
    {I_FBLD, 1, {MEMORY|BITS80,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40654, 40},
    {I_FBLD, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40654, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FBSTP[] = {
    {I_FBSTP, 1, {MEMORY|BITS80,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40658, 40},
    {I_FBSTP, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40658, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FCHS[] = {
    {I_FCHS, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40662, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FCLEX[] = {
    {I_FCLEX, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39011, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FCMOVB[] = {
    {I_FCMOVB, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39016, 41},
    {I_FCMOVB, 2, {FPU0,FPUREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39021, 41},
    {I_FCMOVB, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40666, 41},
    ITEMPLATE_END
};

static const struct itemplate instrux_FCMOVBE[] = {
    {I_FCMOVBE, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39026, 41},
    {I_FCMOVBE, 2, {FPU0,FPUREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39031, 41},
    {I_FCMOVBE, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40670, 41},
    ITEMPLATE_END
};

static const struct itemplate instrux_FCMOVE[] = {
    {I_FCMOVE, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39036, 41},
    {I_FCMOVE, 2, {FPU0,FPUREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39041, 41},
    {I_FCMOVE, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40674, 41},
    ITEMPLATE_END
};

static const struct itemplate instrux_FCMOVNB[] = {
    {I_FCMOVNB, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39046, 41},
    {I_FCMOVNB, 2, {FPU0,FPUREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39051, 41},
    {I_FCMOVNB, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40678, 41},
    ITEMPLATE_END
};

static const struct itemplate instrux_FCMOVNBE[] = {
    {I_FCMOVNBE, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39056, 41},
    {I_FCMOVNBE, 2, {FPU0,FPUREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39061, 41},
    {I_FCMOVNBE, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40682, 41},
    ITEMPLATE_END
};

static const struct itemplate instrux_FCMOVNE[] = {
    {I_FCMOVNE, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39066, 41},
    {I_FCMOVNE, 2, {FPU0,FPUREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39071, 41},
    {I_FCMOVNE, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40686, 41},
    ITEMPLATE_END
};

static const struct itemplate instrux_FCMOVNU[] = {
    {I_FCMOVNU, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39076, 41},
    {I_FCMOVNU, 2, {FPU0,FPUREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39081, 41},
    {I_FCMOVNU, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40690, 41},
    ITEMPLATE_END
};

static const struct itemplate instrux_FCMOVU[] = {
    {I_FCMOVU, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39086, 41},
    {I_FCMOVU, 2, {FPU0,FPUREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39091, 41},
    {I_FCMOVU, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40694, 41},
    ITEMPLATE_END
};

static const struct itemplate instrux_FCOM[] = {
    {I_FCOM, 1, {MEMORY|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40698, 40},
    {I_FCOM, 1, {MEMORY|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40702, 40},
    {I_FCOM, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39096, 40},
    {I_FCOM, 2, {FPU0,FPUREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39101, 40},
    {I_FCOM, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40706, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FCOMI[] = {
    {I_FCOMI, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39106, 41},
    {I_FCOMI, 2, {FPU0,FPUREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39111, 41},
    {I_FCOMI, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40710, 41},
    ITEMPLATE_END
};

static const struct itemplate instrux_FCOMIP[] = {
    {I_FCOMIP, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39116, 41},
    {I_FCOMIP, 2, {FPU0,FPUREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39121, 41},
    {I_FCOMIP, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40714, 41},
    ITEMPLATE_END
};

static const struct itemplate instrux_FCOMP[] = {
    {I_FCOMP, 1, {MEMORY|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40718, 40},
    {I_FCOMP, 1, {MEMORY|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40722, 40},
    {I_FCOMP, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39126, 40},
    {I_FCOMP, 2, {FPU0,FPUREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39131, 40},
    {I_FCOMP, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40726, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FCOMPP[] = {
    {I_FCOMPP, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40730, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FCOS[] = {
    {I_FCOS, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40734, 42},
    ITEMPLATE_END
};

static const struct itemplate instrux_FDECSTP[] = {
    {I_FDECSTP, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40738, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FDISI[] = {
    {I_FDISI, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39136, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FDIV[] = {
    {I_FDIV, 1, {MEMORY|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40742, 40},
    {I_FDIV, 1, {MEMORY|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40746, 40},
    {I_FDIV, 1, {FPUREG|TO,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39141, 40},
    {I_FDIV, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39146, 40},
    {I_FDIV, 2, {FPUREG,FPU0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39141, 40},
    {I_FDIV, 2, {FPU0,FPUREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39151, 40},
    {I_FDIV, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40750, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FDIVP[] = {
    {I_FDIVP, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39156, 40},
    {I_FDIVP, 2, {FPUREG,FPU0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39156, 40},
    {I_FDIVP, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40750, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FDIVR[] = {
    {I_FDIVR, 1, {MEMORY|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40754, 40},
    {I_FDIVR, 1, {MEMORY|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40758, 40},
    {I_FDIVR, 1, {FPUREG|TO,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39161, 40},
    {I_FDIVR, 2, {FPUREG,FPU0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39161, 40},
    {I_FDIVR, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39166, 40},
    {I_FDIVR, 2, {FPU0,FPUREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39171, 40},
    {I_FDIVR, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40762, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FDIVRP[] = {
    {I_FDIVRP, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39176, 40},
    {I_FDIVRP, 2, {FPUREG,FPU0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39176, 40},
    {I_FDIVRP, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40762, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FEMMS[] = {
    {I_FEMMS, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40766, 43},
    ITEMPLATE_END
};

static const struct itemplate instrux_FENI[] = {
    {I_FENI, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39181, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FFREE[] = {
    {I_FFREE, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39186, 40},
    {I_FFREE, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40770, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FFREEP[] = {
    {I_FFREEP, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39191, 44},
    {I_FFREEP, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40774, 44},
    ITEMPLATE_END
};

static const struct itemplate instrux_FIADD[] = {
    {I_FIADD, 1, {MEMORY|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40778, 40},
    {I_FIADD, 1, {MEMORY|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40782, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FICOM[] = {
    {I_FICOM, 1, {MEMORY|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40786, 40},
    {I_FICOM, 1, {MEMORY|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40790, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FICOMP[] = {
    {I_FICOMP, 1, {MEMORY|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40794, 40},
    {I_FICOMP, 1, {MEMORY|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40798, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FIDIV[] = {
    {I_FIDIV, 1, {MEMORY|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40802, 40},
    {I_FIDIV, 1, {MEMORY|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40806, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FIDIVR[] = {
    {I_FIDIVR, 1, {MEMORY|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40810, 40},
    {I_FIDIVR, 1, {MEMORY|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40814, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FILD[] = {
    {I_FILD, 1, {MEMORY|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40818, 40},
    {I_FILD, 1, {MEMORY|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40822, 40},
    {I_FILD, 1, {MEMORY|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40826, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FIMUL[] = {
    {I_FIMUL, 1, {MEMORY|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40830, 40},
    {I_FIMUL, 1, {MEMORY|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40834, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FINCSTP[] = {
    {I_FINCSTP, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40838, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FINIT[] = {
    {I_FINIT, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39196, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FIST[] = {
    {I_FIST, 1, {MEMORY|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40842, 40},
    {I_FIST, 1, {MEMORY|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40846, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FISTP[] = {
    {I_FISTP, 1, {MEMORY|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40850, 40},
    {I_FISTP, 1, {MEMORY|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40854, 40},
    {I_FISTP, 1, {MEMORY|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40858, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FISTTP[] = {
    {I_FISTTP, 1, {MEMORY|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40862, 45},
    {I_FISTTP, 1, {MEMORY|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40866, 45},
    {I_FISTTP, 1, {MEMORY|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40870, 45},
    ITEMPLATE_END
};

static const struct itemplate instrux_FISUB[] = {
    {I_FISUB, 1, {MEMORY|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40874, 40},
    {I_FISUB, 1, {MEMORY|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40878, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FISUBR[] = {
    {I_FISUBR, 1, {MEMORY|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40882, 40},
    {I_FISUBR, 1, {MEMORY|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40886, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FLD[] = {
    {I_FLD, 1, {MEMORY|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40890, 40},
    {I_FLD, 1, {MEMORY|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40894, 40},
    {I_FLD, 1, {MEMORY|BITS80,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40898, 40},
    {I_FLD, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39201, 40},
    {I_FLD, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40902, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FLD1[] = {
    {I_FLD1, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40906, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FLDCW[] = {
    {I_FLDCW, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40910, 46},
    ITEMPLATE_END
};

static const struct itemplate instrux_FLDENV[] = {
    {I_FLDENV, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40914, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FLDL2E[] = {
    {I_FLDL2E, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40918, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FLDL2T[] = {
    {I_FLDL2T, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40922, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FLDLG2[] = {
    {I_FLDLG2, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40926, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FLDLN2[] = {
    {I_FLDLN2, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40930, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FLDPI[] = {
    {I_FLDPI, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40934, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FLDZ[] = {
    {I_FLDZ, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40938, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FMUL[] = {
    {I_FMUL, 1, {MEMORY|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40942, 40},
    {I_FMUL, 1, {MEMORY|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40946, 40},
    {I_FMUL, 1, {FPUREG|TO,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39206, 40},
    {I_FMUL, 2, {FPUREG,FPU0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39206, 40},
    {I_FMUL, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39211, 40},
    {I_FMUL, 2, {FPU0,FPUREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39216, 40},
    {I_FMUL, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40950, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FMULP[] = {
    {I_FMULP, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39221, 40},
    {I_FMULP, 2, {FPUREG,FPU0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39221, 40},
    {I_FMULP, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40950, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FNCLEX[] = {
    {I_FNCLEX, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39012, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FNDISI[] = {
    {I_FNDISI, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39137, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FNENI[] = {
    {I_FNENI, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39182, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FNINIT[] = {
    {I_FNINIT, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39197, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FNOP[] = {
    {I_FNOP, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40954, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FNSAVE[] = {
    {I_FNSAVE, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39227, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FNSTCW[] = {
    {I_FNSTCW, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39237, 46},
    ITEMPLATE_END
};

static const struct itemplate instrux_FNSTENV[] = {
    {I_FNSTENV, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39242, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FNSTSW[] = {
    {I_FNSTSW, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39252, 46},
    {I_FNSTSW, 1, {REG_AX,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39257, 47},
    ITEMPLATE_END
};

static const struct itemplate instrux_FPATAN[] = {
    {I_FPATAN, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40958, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FPREM[] = {
    {I_FPREM, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40962, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FPREM1[] = {
    {I_FPREM1, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40966, 42},
    ITEMPLATE_END
};

static const struct itemplate instrux_FPTAN[] = {
    {I_FPTAN, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40970, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FRNDINT[] = {
    {I_FRNDINT, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40974, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FRSTOR[] = {
    {I_FRSTOR, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40978, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FSAVE[] = {
    {I_FSAVE, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39226, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FSCALE[] = {
    {I_FSCALE, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40982, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FSETPM[] = {
    {I_FSETPM, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40986, 47},
    ITEMPLATE_END
};

static const struct itemplate instrux_FSIN[] = {
    {I_FSIN, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40990, 42},
    ITEMPLATE_END
};

static const struct itemplate instrux_FSINCOS[] = {
    {I_FSINCOS, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40994, 42},
    ITEMPLATE_END
};

static const struct itemplate instrux_FSQRT[] = {
    {I_FSQRT, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40998, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FST[] = {
    {I_FST, 1, {MEMORY|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41002, 40},
    {I_FST, 1, {MEMORY|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41006, 40},
    {I_FST, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39231, 40},
    {I_FST, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41010, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FSTCW[] = {
    {I_FSTCW, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39236, 46},
    ITEMPLATE_END
};

static const struct itemplate instrux_FSTENV[] = {
    {I_FSTENV, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39241, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FSTP[] = {
    {I_FSTP, 1, {MEMORY|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41014, 40},
    {I_FSTP, 1, {MEMORY|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41018, 40},
    {I_FSTP, 1, {MEMORY|BITS80,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41022, 40},
    {I_FSTP, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39246, 40},
    {I_FSTP, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41026, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FSTSW[] = {
    {I_FSTSW, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39251, 46},
    {I_FSTSW, 1, {REG_AX,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39256, 47},
    ITEMPLATE_END
};

static const struct itemplate instrux_FSUB[] = {
    {I_FSUB, 1, {MEMORY|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41030, 40},
    {I_FSUB, 1, {MEMORY|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41034, 40},
    {I_FSUB, 1, {FPUREG|TO,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39261, 40},
    {I_FSUB, 2, {FPUREG,FPU0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39261, 40},
    {I_FSUB, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39266, 40},
    {I_FSUB, 2, {FPU0,FPUREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39271, 40},
    {I_FSUB, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41038, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FSUBP[] = {
    {I_FSUBP, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39276, 40},
    {I_FSUBP, 2, {FPUREG,FPU0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39276, 40},
    {I_FSUBP, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41038, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FSUBR[] = {
    {I_FSUBR, 1, {MEMORY|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41042, 40},
    {I_FSUBR, 1, {MEMORY|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41046, 40},
    {I_FSUBR, 1, {FPUREG|TO,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39281, 40},
    {I_FSUBR, 2, {FPUREG,FPU0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39281, 40},
    {I_FSUBR, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39286, 40},
    {I_FSUBR, 2, {FPU0,FPUREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39291, 40},
    {I_FSUBR, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41050, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FSUBRP[] = {
    {I_FSUBRP, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39296, 40},
    {I_FSUBRP, 2, {FPUREG,FPU0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39296, 40},
    {I_FSUBRP, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41050, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FTST[] = {
    {I_FTST, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41054, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FUCOM[] = {
    {I_FUCOM, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39301, 42},
    {I_FUCOM, 2, {FPU0,FPUREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39306, 42},
    {I_FUCOM, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41058, 42},
    ITEMPLATE_END
};

static const struct itemplate instrux_FUCOMI[] = {
    {I_FUCOMI, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39311, 41},
    {I_FUCOMI, 2, {FPU0,FPUREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39316, 41},
    {I_FUCOMI, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41062, 41},
    ITEMPLATE_END
};

static const struct itemplate instrux_FUCOMIP[] = {
    {I_FUCOMIP, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39321, 41},
    {I_FUCOMIP, 2, {FPU0,FPUREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39326, 41},
    {I_FUCOMIP, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41066, 41},
    ITEMPLATE_END
};

static const struct itemplate instrux_FUCOMP[] = {
    {I_FUCOMP, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39331, 42},
    {I_FUCOMP, 2, {FPU0,FPUREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39336, 42},
    {I_FUCOMP, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41070, 42},
    ITEMPLATE_END
};

static const struct itemplate instrux_FUCOMPP[] = {
    {I_FUCOMPP, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41074, 42},
    ITEMPLATE_END
};

static const struct itemplate instrux_FXAM[] = {
    {I_FXAM, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41078, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FXCH[] = {
    {I_FXCH, 1, {FPUREG,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39341, 40},
    {I_FXCH, 2, {FPUREG,FPU0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39341, 40},
    {I_FXCH, 2, {FPU0,FPUREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39346, 40},
    {I_FXCH, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41082, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FXTRACT[] = {
    {I_FXTRACT, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41086, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FYL2X[] = {
    {I_FYL2X, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41090, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_FYL2XP1[] = {
    {I_FYL2XP1, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41094, 40},
    ITEMPLATE_END
};

static const struct itemplate instrux_HLT[] = {
    {I_HLT, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41541, 48},
    ITEMPLATE_END
};

static const struct itemplate instrux_IBTS[] = {
    {I_IBTS, 2, {MEMORY,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35206, 49},
    {I_IBTS, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35206, 50},
    {I_IBTS, 2, {MEMORY,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35212, 51},
    {I_IBTS, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35212, 50},
    ITEMPLATE_END
};

static const struct itemplate instrux_ICEBP[] = {
    {I_ICEBP, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41544, 5},
    ITEMPLATE_END
};

static const struct itemplate instrux_IDIV[] = {
    {I_IDIV, 1, {RM_GPR|BITS8,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41098, 0},
    {I_IDIV, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39351, 0},
    {I_IDIV, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39356, 5},
    {I_IDIV, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39361, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_IMUL[] = {
    {I_IMUL, 1, {RM_GPR|BITS8,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41102, 0},
    {I_IMUL, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39366, 0},
    {I_IMUL, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39371, 5},
    {I_IMUL, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39376, 7},
    {I_IMUL, 2, {REG_GPR|BITS16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+35242, 9},
    {I_IMUL, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35242, 5},
    {I_IMUL, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+35248, 9},
    {I_IMUL, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35248, 5},
    {I_IMUL, 2, {REG_GPR|BITS64,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+35254, 10},
    {I_IMUL, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+35254, 7},
    {I_IMUL, 3, {REG_GPR|BITS16,MEMORY,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+35260, 52},
    {I_IMUL, 3, {REG_GPR|BITS16,MEMORY,SBYTEWORD,0,0}, NO_DECORATOR, nasm_bytecodes+35260, 52},
    {I_IMUL, 3, {REG_GPR|BITS16,MEMORY,IMMEDIATE|BITS16,0,0}, NO_DECORATOR, nasm_bytecodes+35266, 52},
    {I_IMUL, 3, {REG_GPR|BITS16,MEMORY,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+35266, 52},
    {I_IMUL, 3, {REG_GPR|BITS16,REG_GPR|BITS16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+35260, 39},
    {I_IMUL, 3, {REG_GPR|BITS16,REG_GPR|BITS16,SBYTEWORD,0,0}, NO_DECORATOR, nasm_bytecodes+35260, 52},
    {I_IMUL, 3, {REG_GPR|BITS16,REG_GPR|BITS16,IMMEDIATE|BITS16,0,0}, NO_DECORATOR, nasm_bytecodes+35266, 39},
    {I_IMUL, 3, {REG_GPR|BITS16,REG_GPR|BITS16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+35266, 52},
    {I_IMUL, 3, {REG_GPR|BITS32,MEMORY,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+35272, 9},
    {I_IMUL, 3, {REG_GPR|BITS32,MEMORY,SBYTEDWORD,0,0}, NO_DECORATOR, nasm_bytecodes+35272, 9},
    {I_IMUL, 3, {REG_GPR|BITS32,MEMORY,IMMEDIATE|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+35278, 9},
    {I_IMUL, 3, {REG_GPR|BITS32,MEMORY,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+35278, 9},
    {I_IMUL, 3, {REG_GPR|BITS32,REG_GPR|BITS32,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+35272, 5},
    {I_IMUL, 3, {REG_GPR|BITS32,REG_GPR|BITS32,SBYTEDWORD,0,0}, NO_DECORATOR, nasm_bytecodes+35272, 9},
    {I_IMUL, 3, {REG_GPR|BITS32,REG_GPR|BITS32,IMMEDIATE|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+35278, 5},
    {I_IMUL, 3, {REG_GPR|BITS32,REG_GPR|BITS32,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+35278, 9},
    {I_IMUL, 3, {REG_GPR|BITS64,MEMORY,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+35284, 10},
    {I_IMUL, 3, {REG_GPR|BITS64,MEMORY,SBYTEDWORD,0,0}, NO_DECORATOR, nasm_bytecodes+35284, 10},
    {I_IMUL, 3, {REG_GPR|BITS64,MEMORY,IMMEDIATE|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+35290, 10},
    {I_IMUL, 3, {REG_GPR|BITS64,MEMORY,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+35296, 10},
    {I_IMUL, 3, {REG_GPR|BITS64,REG_GPR|BITS64,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+35284, 7},
    {I_IMUL, 3, {REG_GPR|BITS64,REG_GPR|BITS64,SBYTEDWORD,0,0}, NO_DECORATOR, nasm_bytecodes+35284, 10},
    {I_IMUL, 3, {REG_GPR|BITS64,REG_GPR|BITS64,IMMEDIATE|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+35290, 7},
    {I_IMUL, 3, {REG_GPR|BITS64,REG_GPR|BITS64,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+35296, 10},
    {I_IMUL, 2, {REG_GPR|BITS16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35302, 39},
    {I_IMUL, 2, {REG_GPR|BITS16,SBYTEWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+35302, 52},
    {I_IMUL, 2, {REG_GPR|BITS16,IMMEDIATE|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35308, 39},
    {I_IMUL, 2, {REG_GPR|BITS16,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+35308, 52},
    {I_IMUL, 2, {REG_GPR|BITS32,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35314, 5},
    {I_IMUL, 2, {REG_GPR|BITS32,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+35314, 9},
    {I_IMUL, 2, {REG_GPR|BITS32,IMMEDIATE|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35320, 5},
    {I_IMUL, 2, {REG_GPR|BITS32,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+35320, 9},
    {I_IMUL, 2, {REG_GPR|BITS64,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35326, 7},
    {I_IMUL, 2, {REG_GPR|BITS64,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+35326, 10},
    {I_IMUL, 2, {REG_GPR|BITS64,IMMEDIATE|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35332, 7},
    {I_IMUL, 2, {REG_GPR|BITS64,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+35332, 10},
    ITEMPLATE_END
};

static const struct itemplate instrux_IN[] = {
    {I_IN, 2, {REG_AL,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+41106, 53},
    {I_IN, 2, {REG_AX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+39381, 53},
    {I_IN, 2, {REG_EAX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+39386, 21},
    {I_IN, 2, {REG_AL,REG_DX,0,0,0}, NO_DECORATOR, nasm_bytecodes+41547, 0},
    {I_IN, 2, {REG_AX,REG_DX,0,0,0}, NO_DECORATOR, nasm_bytecodes+41110, 0},
    {I_IN, 2, {REG_EAX,REG_DX,0,0,0}, NO_DECORATOR, nasm_bytecodes+41114, 5},
    ITEMPLATE_END
};

static const struct itemplate instrux_INC[] = {
    {I_INC, 1, {REG_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41118, 1},
    {I_INC, 1, {REG_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41122, 19},
    {I_INC, 1, {RM_GPR|BITS8,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39391, 11},
    {I_INC, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35338, 11},
    {I_INC, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35344, 12},
    {I_INC, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35350, 13},
    ITEMPLATE_END
};

static const struct itemplate instrux_INSB[] = {
    {I_INSB, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41550, 39},
    ITEMPLATE_END
};

static const struct itemplate instrux_INSD[] = {
    {I_INSD, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41126, 5},
    ITEMPLATE_END
};

static const struct itemplate instrux_INSW[] = {
    {I_INSW, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41130, 39},
    ITEMPLATE_END
};

static const struct itemplate instrux_INT[] = {
    {I_INT, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41134, 53},
    ITEMPLATE_END
};

static const struct itemplate instrux_INT01[] = {
    {I_INT01, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41544, 5},
    ITEMPLATE_END
};

static const struct itemplate instrux_INT1[] = {
    {I_INT1, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41544, 5},
    ITEMPLATE_END
};

static const struct itemplate instrux_INT03[] = {
    {I_INT03, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41553, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_INT3[] = {
    {I_INT3, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41553, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_INTO[] = {
    {I_INTO, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41556, 1},
    ITEMPLATE_END
};

static const struct itemplate instrux_INVD[] = {
    {I_INVD, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41138, 54},
    ITEMPLATE_END
};

static const struct itemplate instrux_INVPCID[] = {
    {I_INVPCID, 2, {REG_GPR|BITS32,MEMORY|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+26020, 55},
    {I_INVPCID, 2, {REG_GPR|BITS64,MEMORY|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+26020, 56},
    ITEMPLATE_END
};

static const struct itemplate instrux_INVLPG[] = {
    {I_INVLPG, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39396, 54},
    ITEMPLATE_END
};

static const struct itemplate instrux_INVLPGA[] = {
    {I_INVLPGA, 2, {REG_AX,REG_ECX,0,0,0}, NO_DECORATOR, nasm_bytecodes+35356, 57},
    {I_INVLPGA, 2, {REG_EAX,REG_ECX,0,0,0}, NO_DECORATOR, nasm_bytecodes+35362, 58},
    {I_INVLPGA, 2, {REG_RAX,REG_ECX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26027, 59},
    {I_INVLPGA, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35363, 58},
    ITEMPLATE_END
};

static const struct itemplate instrux_IRET[] = {
    {I_IRET, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41142, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_IRETD[] = {
    {I_IRETD, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41146, 5},
    ITEMPLATE_END
};

static const struct itemplate instrux_IRETQ[] = {
    {I_IRETQ, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41150, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_IRETW[] = {
    {I_IRETW, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41154, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_JCXZ[] = {
    {I_JCXZ, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39401, 1},
    ITEMPLATE_END
};

static const struct itemplate instrux_JECXZ[] = {
    {I_JECXZ, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39406, 5},
    ITEMPLATE_END
};

static const struct itemplate instrux_JRCXZ[] = {
    {I_JRCXZ, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39411, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_JMP[] = {
    {I_JMP, 1, {IMMEDIATE|SHORT,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39417, 0},
    {I_JMP, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39416, 0},
    {I_JMP, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39421, 25},
    {I_JMP, 1, {IMMEDIATE|NEAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39421, 25},
    {I_JMP, 1, {IMMEDIATE|FAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35368, 1},
    {I_JMP, 1, {IMMEDIATE|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39426, 26},
    {I_JMP, 1, {IMMEDIATE|BITS16|NEAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39426, 26},
    {I_JMP, 1, {IMMEDIATE|BITS16|FAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35374, 1},
    {I_JMP, 1, {IMMEDIATE|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39431, 27},
    {I_JMP, 1, {IMMEDIATE|BITS32|NEAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39431, 27},
    {I_JMP, 1, {IMMEDIATE|BITS32|FAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35380, 19},
    {I_JMP, 1, {IMMEDIATE|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39436, 28},
    {I_JMP, 1, {IMMEDIATE|BITS64|NEAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39436, 28},
    {I_JMP, 2, {IMMEDIATE|COLON,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+35386, 1},
    {I_JMP, 2, {IMMEDIATE|BITS16|COLON,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+35392, 1},
    {I_JMP, 2, {IMMEDIATE|COLON,IMMEDIATE|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35392, 1},
    {I_JMP, 2, {IMMEDIATE|BITS32|COLON,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+35398, 19},
    {I_JMP, 2, {IMMEDIATE|COLON,IMMEDIATE|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35398, 19},
    {I_JMP, 1, {MEMORY|FAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39441, 1},
    {I_JMP, 1, {MEMORY|FAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39446, 7},
    {I_JMP, 1, {MEMORY|BITS16|FAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39451, 0},
    {I_JMP, 1, {MEMORY|BITS32|FAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39456, 5},
    {I_JMP, 1, {MEMORY|BITS64|FAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39446, 7},
    {I_JMP, 1, {MEMORY|NEAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39461, 25},
    {I_JMP, 1, {RM_GPR|BITS16|NEAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39466, 26},
    {I_JMP, 1, {RM_GPR|BITS32|NEAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39471, 27},
    {I_JMP, 1, {RM_GPR|BITS64|NEAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39476, 28},
    {I_JMP, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39461, 25},
    {I_JMP, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39466, 26},
    {I_JMP, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39471, 27},
    {I_JMP, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39476, 28},
    ITEMPLATE_END
};

static const struct itemplate instrux_JMPE[] = {
    {I_JMPE, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35404, 60},
    {I_JMPE, 1, {IMMEDIATE|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35410, 60},
    {I_JMPE, 1, {IMMEDIATE|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35416, 60},
    {I_JMPE, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35422, 60},
    {I_JMPE, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35428, 60},
    ITEMPLATE_END
};

static const struct itemplate instrux_LAHF[] = {
    {I_LAHF, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41559, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_LAR[] = {
    {I_LAR, 2, {REG_GPR|BITS16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+35434, 61},
    {I_LAR, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35434, 62},
    {I_LAR, 2, {REG_GPR|BITS16,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35434, 63},
    {I_LAR, 2, {REG_GPR|BITS16,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+26034, 64},
    {I_LAR, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+35440, 65},
    {I_LAR, 2, {REG_GPR|BITS32,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35440, 63},
    {I_LAR, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35440, 63},
    {I_LAR, 2, {REG_GPR|BITS32,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+26041, 64},
    {I_LAR, 2, {REG_GPR|BITS64,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+35446, 66},
    {I_LAR, 2, {REG_GPR|BITS64,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35446, 64},
    {I_LAR, 2, {REG_GPR|BITS64,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35446, 64},
    {I_LAR, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+35446, 64},
    ITEMPLATE_END
};

static const struct itemplate instrux_LDS[] = {
    {I_LDS, 2, {REG_GPR|BITS16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+39481, 1},
    {I_LDS, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+39486, 19},
    ITEMPLATE_END
};

static const struct itemplate instrux_LEA[] = {
    {I_LEA, 2, {REG_GPR|BITS16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+39491, 0},
    {I_LEA, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+39496, 5},
    {I_LEA, 2, {REG_GPR|BITS64,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+39501, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_LEAVE[] = {
    {I_LEAVE, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39753, 39},
    ITEMPLATE_END
};

static const struct itemplate instrux_LES[] = {
    {I_LES, 2, {REG_GPR|BITS16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+39506, 1},
    {I_LES, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+39511, 19},
    ITEMPLATE_END
};

static const struct itemplate instrux_LFENCE[] = {
    {I_LFENCE, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35452, 59},
    {I_LFENCE, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35452, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_LFS[] = {
    {I_LFS, 2, {REG_GPR|BITS16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+35458, 5},
    {I_LFS, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+35464, 5},
    {I_LFS, 2, {REG_GPR|BITS64,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+35470, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_LGDT[] = {
    {I_LGDT, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39516, 29},
    ITEMPLATE_END
};

static const struct itemplate instrux_LGS[] = {
    {I_LGS, 2, {REG_GPR|BITS16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+35476, 5},
    {I_LGS, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+35482, 5},
    {I_LGS, 2, {REG_GPR|BITS64,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+35488, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_LIDT[] = {
    {I_LIDT, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39521, 29},
    ITEMPLATE_END
};

static const struct itemplate instrux_LLDT[] = {
    {I_LLDT, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39526, 67},
    {I_LLDT, 1, {MEMORY|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39526, 67},
    {I_LLDT, 1, {REG_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39526, 67},
    ITEMPLATE_END
};

static const struct itemplate instrux_LMSW[] = {
    {I_LMSW, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39531, 29},
    {I_LMSW, 1, {MEMORY|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39531, 29},
    {I_LMSW, 1, {REG_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39531, 29},
    ITEMPLATE_END
};

static const struct itemplate instrux_LOADALL[] = {
    {I_LOADALL, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41158, 50},
    ITEMPLATE_END
};

static const struct itemplate instrux_LOADALL286[] = {
    {I_LOADALL286, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41162, 68},
    ITEMPLATE_END
};

static const struct itemplate instrux_LODSB[] = {
    {I_LODSB, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41562, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_LODSD[] = {
    {I_LODSD, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41166, 5},
    ITEMPLATE_END
};

static const struct itemplate instrux_LODSQ[] = {
    {I_LODSQ, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41170, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_LODSW[] = {
    {I_LODSW, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41174, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_LOOP[] = {
    {I_LOOP, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39536, 0},
    {I_LOOP, 2, {IMMEDIATE,REG_CX,0,0,0}, NO_DECORATOR, nasm_bytecodes+39541, 1},
    {I_LOOP, 2, {IMMEDIATE,REG_ECX,0,0,0}, NO_DECORATOR, nasm_bytecodes+39546, 5},
    {I_LOOP, 2, {IMMEDIATE,REG_RCX,0,0,0}, NO_DECORATOR, nasm_bytecodes+39551, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_LOOPE[] = {
    {I_LOOPE, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39556, 0},
    {I_LOOPE, 2, {IMMEDIATE,REG_CX,0,0,0}, NO_DECORATOR, nasm_bytecodes+39561, 1},
    {I_LOOPE, 2, {IMMEDIATE,REG_ECX,0,0,0}, NO_DECORATOR, nasm_bytecodes+39566, 5},
    {I_LOOPE, 2, {IMMEDIATE,REG_RCX,0,0,0}, NO_DECORATOR, nasm_bytecodes+39571, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_LOOPNE[] = {
    {I_LOOPNE, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39576, 0},
    {I_LOOPNE, 2, {IMMEDIATE,REG_CX,0,0,0}, NO_DECORATOR, nasm_bytecodes+39581, 1},
    {I_LOOPNE, 2, {IMMEDIATE,REG_ECX,0,0,0}, NO_DECORATOR, nasm_bytecodes+39586, 5},
    {I_LOOPNE, 2, {IMMEDIATE,REG_RCX,0,0,0}, NO_DECORATOR, nasm_bytecodes+39591, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_LOOPNZ[] = {
    {I_LOOPNZ, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39576, 0},
    {I_LOOPNZ, 2, {IMMEDIATE,REG_CX,0,0,0}, NO_DECORATOR, nasm_bytecodes+39581, 1},
    {I_LOOPNZ, 2, {IMMEDIATE,REG_ECX,0,0,0}, NO_DECORATOR, nasm_bytecodes+39586, 5},
    {I_LOOPNZ, 2, {IMMEDIATE,REG_RCX,0,0,0}, NO_DECORATOR, nasm_bytecodes+39591, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_LOOPZ[] = {
    {I_LOOPZ, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39556, 0},
    {I_LOOPZ, 2, {IMMEDIATE,REG_CX,0,0,0}, NO_DECORATOR, nasm_bytecodes+39561, 1},
    {I_LOOPZ, 2, {IMMEDIATE,REG_ECX,0,0,0}, NO_DECORATOR, nasm_bytecodes+39566, 5},
    {I_LOOPZ, 2, {IMMEDIATE,REG_RCX,0,0,0}, NO_DECORATOR, nasm_bytecodes+39571, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_LSL[] = {
    {I_LSL, 2, {REG_GPR|BITS16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+35494, 61},
    {I_LSL, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35494, 62},
    {I_LSL, 2, {REG_GPR|BITS16,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35494, 63},
    {I_LSL, 2, {REG_GPR|BITS16,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+26048, 64},
    {I_LSL, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+35500, 65},
    {I_LSL, 2, {REG_GPR|BITS32,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35500, 63},
    {I_LSL, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35500, 63},
    {I_LSL, 2, {REG_GPR|BITS32,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+26055, 64},
    {I_LSL, 2, {REG_GPR|BITS64,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+35506, 66},
    {I_LSL, 2, {REG_GPR|BITS64,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35506, 64},
    {I_LSL, 2, {REG_GPR|BITS64,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35506, 64},
    {I_LSL, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+35506, 64},
    ITEMPLATE_END
};

static const struct itemplate instrux_LSS[] = {
    {I_LSS, 2, {REG_GPR|BITS16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+35512, 5},
    {I_LSS, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+35518, 5},
    {I_LSS, 2, {REG_GPR|BITS64,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+35524, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_LTR[] = {
    {I_LTR, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39596, 67},
    {I_LTR, 1, {MEMORY|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39596, 67},
    {I_LTR, 1, {REG_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39596, 67},
    ITEMPLATE_END
};

static const struct itemplate instrux_MFENCE[] = {
    {I_MFENCE, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35530, 59},
    {I_MFENCE, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35530, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_MONITOR[] = {
    {I_MONITOR, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39601, 69},
    {I_MONITOR, 3, {REG_EAX,REG_ECX,REG_EDX,0,0}, NO_DECORATOR, nasm_bytecodes+39601, 70},
    {I_MONITOR, 3, {REG_RAX,REG_ECX,REG_EDX,0,0}, NO_DECORATOR, nasm_bytecodes+39601, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_MONITORX[] = {
    {I_MONITORX, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39606, 71},
    {I_MONITORX, 3, {REG_RAX,REG_ECX,REG_EDX,0,0}, NO_DECORATOR, nasm_bytecodes+39606, 59},
    {I_MONITORX, 3, {REG_EAX,REG_ECX,REG_EDX,0,0}, NO_DECORATOR, nasm_bytecodes+39606, 71},
    {I_MONITORX, 3, {REG_AX,REG_ECX,REG_EDX,0,0}, NO_DECORATOR, nasm_bytecodes+39606, 71},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOV[] = {
    {I_MOV, 2, {MEMORY,REG_SREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39627, 72},
    {I_MOV, 2, {REG_GPR|BITS16,REG_SREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39611, 0},
    {I_MOV, 2, {REG_GPR|BITS32,REG_SREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39616, 5},
    {I_MOV, 2, {REG_GPR|BITS64,REG_SREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39621, 73},
    {I_MOV, 2, {RM_GPR|BITS64,REG_SREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39626, 7},
    {I_MOV, 2, {REG_SREG,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+39647, 72},
    {I_MOV, 2, {REG_SREG,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+39647, 74},
    {I_MOV, 2, {REG_SREG,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+39647, 75},
    {I_MOV, 2, {REG_SREG,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+39631, 73},
    {I_MOV, 2, {REG_SREG,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+39636, 0},
    {I_MOV, 2, {REG_SREG,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+39641, 5},
    {I_MOV, 2, {REG_SREG,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+39646, 7},
    {I_MOV, 2, {REG_AL,MEM_OFFS,0,0,0}, NO_DECORATOR, nasm_bytecodes+41178, 8},
    {I_MOV, 2, {REG_AX,MEM_OFFS,0,0,0}, NO_DECORATOR, nasm_bytecodes+39651, 8},
    {I_MOV, 2, {REG_EAX,MEM_OFFS,0,0,0}, NO_DECORATOR, nasm_bytecodes+39656, 9},
    {I_MOV, 2, {REG_RAX,MEM_OFFS,0,0,0}, NO_DECORATOR, nasm_bytecodes+39661, 10},
    {I_MOV, 2, {MEM_OFFS,REG_AL,0,0,0}, NO_DECORATOR, nasm_bytecodes+41182, 76},
    {I_MOV, 2, {MEM_OFFS,REG_AX,0,0,0}, NO_DECORATOR, nasm_bytecodes+39666, 76},
    {I_MOV, 2, {MEM_OFFS,REG_EAX,0,0,0}, NO_DECORATOR, nasm_bytecodes+39671, 77},
    {I_MOV, 2, {MEM_OFFS,REG_RAX,0,0,0}, NO_DECORATOR, nasm_bytecodes+39676, 78},
    {I_MOV, 2, {REG_GPR|BITS32,REG_CREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+35536, 79},
    {I_MOV, 2, {REG_GPR|BITS64,REG_CREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+35542, 80},
    {I_MOV, 2, {REG_CREG,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35548, 79},
    {I_MOV, 2, {REG_CREG,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+35554, 80},
    {I_MOV, 2, {REG_GPR|BITS32,REG_DREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+35561, 79},
    {I_MOV, 2, {REG_GPR|BITS64,REG_DREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+35560, 80},
    {I_MOV, 2, {REG_DREG,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35567, 79},
    {I_MOV, 2, {REG_DREG,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+35566, 80},
    {I_MOV, 2, {REG_GPR|BITS32,REG_TREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+39681, 19},
    {I_MOV, 2, {REG_TREG,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+39686, 19},
    {I_MOV, 2, {MEMORY,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+39691, 8},
    {I_MOV, 2, {REG_GPR|BITS8,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+39692, 0},
    {I_MOV, 2, {MEMORY,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35572, 8},
    {I_MOV, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35573, 0},
    {I_MOV, 2, {MEMORY,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35578, 9},
    {I_MOV, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35579, 5},
    {I_MOV, 2, {MEMORY,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+35584, 10},
    {I_MOV, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+35585, 7},
    {I_MOV, 2, {REG_GPR|BITS8,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+41186, 8},
    {I_MOV, 2, {REG_GPR|BITS8,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+41186, 0},
    {I_MOV, 2, {REG_GPR|BITS16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+39696, 8},
    {I_MOV, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+39696, 0},
    {I_MOV, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+39701, 9},
    {I_MOV, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+39701, 5},
    {I_MOV, 2, {REG_GPR|BITS64,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+39706, 10},
    {I_MOV, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+39706, 7},
    {I_MOV, 2, {REG_GPR|BITS8,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+41190, 8},
    {I_MOV, 2, {REG_GPR|BITS16,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+39711, 8},
    {I_MOV, 2, {REG_GPR|BITS32,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+39716, 9},
    {I_MOV, 2, {REG_GPR|BITS64,UDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+39721, 81},
    {I_MOV, 2, {REG_GPR|BITS64,SDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+26077, 81},
    {I_MOV, 2, {REG_GPR|BITS64,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+39726, 10},
    {I_MOV, 2, {RM_GPR|BITS8,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+35590, 8},
    {I_MOV, 2, {RM_GPR|BITS16,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+26062, 8},
    {I_MOV, 2, {RM_GPR|BITS32,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+26069, 9},
    {I_MOV, 2, {RM_GPR|BITS64,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+26076, 10},
    {I_MOV, 2, {RM_GPR|BITS64,IMMEDIATE|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+26076, 7},
    {I_MOV, 2, {MEMORY,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35590, 8},
    {I_MOV, 2, {MEMORY,IMMEDIATE|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+26062, 8},
    {I_MOV, 2, {MEMORY,IMMEDIATE|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+26069, 9},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVD[] = {
    {I_MOVD, 2, {MMXREG,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35596, 82},
    {I_MOVD, 2, {RM_GPR|BITS32,MMXREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+35602, 82},
    {I_MOVD, 2, {MMXREG,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+26083, 83},
    {I_MOVD, 2, {RM_GPR|BITS64,MMXREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+26090, 83},
    {I_MOVD, 2, {MEMORY,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+26958, 144},
    {I_MOVD, 2, {XMM_L16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+26965, 144},
    {I_MOVD, 2, {XMM_L16,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+26965, 140},
    {I_MOVD, 2, {RM_GPR|BITS32,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+26958, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVQ[] = {
    {I_MOVQ, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+35608, 84},
    {I_MOVQ, 2, {RM_MMX,MMXREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+35614, 84},
    {I_MOVQ, 2, {MMXREG,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+26083, 85},
    {I_MOVQ, 2, {RM_GPR|BITS64,MMXREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+26090, 85},
    {I_MOVQ, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36580, 140},
    {I_MOVQ, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36586, 140},
    {I_MOVQ, 2, {MEMORY,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36586, 145},
    {I_MOVQ, 2, {XMM_L16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+36580, 145},
    {I_MOVQ, 2, {XMM_L16,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+26972, 146},
    {I_MOVQ, 2, {RM_GPR|BITS64,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+26979, 146},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVSB[] = {
    {I_MOVSB, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+8473, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVSD[] = {
    {I_MOVSD, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41194, 5},
    {I_MOVSD, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37138, 140},
    {I_MOVSD, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37144, 140},
    {I_MOVSD, 2, {MEMORY|BITS64,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37144, 140},
    {I_MOVSD, 2, {XMM_L16,MEMORY|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+37138, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVSQ[] = {
    {I_MOVSQ, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41198, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVSW[] = {
    {I_MOVSW, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41202, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVSX[] = {
    {I_MOVSX, 2, {REG_GPR|BITS16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+35620, 21},
    {I_MOVSX, 2, {REG_GPR|BITS16,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35620, 5},
    {I_MOVSX, 2, {REG_GPR|BITS32,RM_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35626, 5},
    {I_MOVSX, 2, {REG_GPR|BITS32,RM_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35632, 5},
    {I_MOVSX, 2, {REG_GPR|BITS64,RM_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35638, 7},
    {I_MOVSX, 2, {REG_GPR|BITS64,RM_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35644, 7},
    {I_MOVSX, 2, {REG_GPR|BITS64,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+39731, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVSXD[] = {
    {I_MOVSXD, 2, {REG_GPR|BITS64,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+39731, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVZX[] = {
    {I_MOVZX, 2, {REG_GPR|BITS16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+35650, 21},
    {I_MOVZX, 2, {REG_GPR|BITS16,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35650, 5},
    {I_MOVZX, 2, {REG_GPR|BITS32,RM_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35656, 5},
    {I_MOVZX, 2, {REG_GPR|BITS32,RM_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35662, 5},
    {I_MOVZX, 2, {REG_GPR|BITS64,RM_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35668, 7},
    {I_MOVZX, 2, {REG_GPR|BITS64,RM_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35674, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_MUL[] = {
    {I_MUL, 1, {RM_GPR|BITS8,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41206, 0},
    {I_MUL, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39736, 0},
    {I_MUL, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39741, 5},
    {I_MUL, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39746, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_MWAIT[] = {
    {I_MWAIT, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39751, 69},
    {I_MWAIT, 2, {REG_EAX,REG_ECX,0,0,0}, NO_DECORATOR, nasm_bytecodes+39751, 69},
    ITEMPLATE_END
};

static const struct itemplate instrux_MWAITX[] = {
    {I_MWAITX, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39756, 71},
    {I_MWAITX, 2, {REG_EAX,REG_ECX,0,0,0}, NO_DECORATOR, nasm_bytecodes+39756, 71},
    ITEMPLATE_END
};

static const struct itemplate instrux_NEG[] = {
    {I_NEG, 1, {RM_GPR|BITS8,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39761, 11},
    {I_NEG, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35680, 11},
    {I_NEG, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35686, 12},
    {I_NEG, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35692, 13},
    ITEMPLATE_END
};

static const struct itemplate instrux_NOP[] = {
    {I_NOP, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39766, 0},
    {I_NOP, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35698, 86},
    {I_NOP, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35704, 86},
    {I_NOP, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35710, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_NOT[] = {
    {I_NOT, 1, {RM_GPR|BITS8,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39771, 11},
    {I_NOT, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35716, 11},
    {I_NOT, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35722, 12},
    {I_NOT, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35728, 13},
    ITEMPLATE_END
};

static const struct itemplate instrux_OR[] = {
    {I_OR, 2, {MEMORY,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+39776, 3},
    {I_OR, 2, {REG_GPR|BITS8,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+39777, 0},
    {I_OR, 2, {MEMORY,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35734, 3},
    {I_OR, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35735, 0},
    {I_OR, 2, {MEMORY,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35740, 4},
    {I_OR, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35741, 5},
    {I_OR, 2, {MEMORY,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+35746, 6},
    {I_OR, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+35747, 7},
    {I_OR, 2, {REG_GPR|BITS8,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+33499, 8},
    {I_OR, 2, {REG_GPR|BITS8,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+33499, 0},
    {I_OR, 2, {REG_GPR|BITS16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+39781, 8},
    {I_OR, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+39781, 0},
    {I_OR, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+39786, 9},
    {I_OR, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+39786, 5},
    {I_OR, 2, {REG_GPR|BITS64,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+39791, 10},
    {I_OR, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+39791, 7},
    {I_OR, 2, {RM_GPR|BITS16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+26097, 11},
    {I_OR, 2, {RM_GPR|BITS32,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+26104, 12},
    {I_OR, 2, {RM_GPR|BITS64,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+26111, 13},
    {I_OR, 2, {REG_AL,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+41210, 8},
    {I_OR, 2, {REG_AX,SBYTEWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+26098, 8},
    {I_OR, 2, {REG_AX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+39796, 8},
    {I_OR, 2, {REG_EAX,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+26105, 9},
    {I_OR, 2, {REG_EAX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+39801, 9},
    {I_OR, 2, {REG_RAX,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+26112, 10},
    {I_OR, 2, {REG_RAX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+39806, 10},
    {I_OR, 2, {RM_GPR|BITS8,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+35752, 3},
    {I_OR, 2, {RM_GPR|BITS16,SBYTEWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+26097, 3},
    {I_OR, 2, {RM_GPR|BITS16,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+26118, 3},
    {I_OR, 2, {RM_GPR|BITS32,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+26104, 4},
    {I_OR, 2, {RM_GPR|BITS32,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+26125, 4},
    {I_OR, 2, {RM_GPR|BITS64,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+26111, 6},
    {I_OR, 2, {RM_GPR|BITS64,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+26132, 6},
    {I_OR, 2, {MEMORY,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35752, 3},
    {I_OR, 2, {MEMORY,SBYTEWORD|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+26097, 3},
    {I_OR, 2, {MEMORY,IMMEDIATE|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+26118, 3},
    {I_OR, 2, {MEMORY,SBYTEDWORD|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+26104, 4},
    {I_OR, 2, {MEMORY,IMMEDIATE|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+26125, 4},
    {I_OR, 2, {RM_GPR|BITS8,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+35758, 14},
    ITEMPLATE_END
};

static const struct itemplate instrux_OUT[] = {
    {I_OUT, 2, {IMMEDIATE,REG_AL,0,0,0}, NO_DECORATOR, nasm_bytecodes+41214, 53},
    {I_OUT, 2, {IMMEDIATE,REG_AX,0,0,0}, NO_DECORATOR, nasm_bytecodes+39811, 53},
    {I_OUT, 2, {IMMEDIATE,REG_EAX,0,0,0}, NO_DECORATOR, nasm_bytecodes+39816, 21},
    {I_OUT, 2, {REG_DX,REG_AL,0,0,0}, NO_DECORATOR, nasm_bytecodes+40508, 0},
    {I_OUT, 2, {REG_DX,REG_AX,0,0,0}, NO_DECORATOR, nasm_bytecodes+41218, 0},
    {I_OUT, 2, {REG_DX,REG_EAX,0,0,0}, NO_DECORATOR, nasm_bytecodes+41222, 5},
    ITEMPLATE_END
};

static const struct itemplate instrux_OUTSB[] = {
    {I_OUTSB, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41565, 39},
    ITEMPLATE_END
};

static const struct itemplate instrux_OUTSD[] = {
    {I_OUTSD, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41226, 5},
    ITEMPLATE_END
};

static const struct itemplate instrux_OUTSW[] = {
    {I_OUTSW, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41230, 39},
    ITEMPLATE_END
};

static const struct itemplate instrux_PACKSSDW[] = {
    {I_PACKSSDW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26139, 84},
    {I_PACKSSDW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36604, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PACKSSWB[] = {
    {I_PACKSSWB, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26146, 84},
    {I_PACKSSWB, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36598, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PACKUSWB[] = {
    {I_PACKUSWB, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26153, 84},
    {I_PACKUSWB, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36610, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PADDB[] = {
    {I_PADDB, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26160, 84},
    {I_PADDB, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36616, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PADDD[] = {
    {I_PADDD, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26167, 84},
    {I_PADDD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36628, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PADDSB[] = {
    {I_PADDSB, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26174, 84},
    {I_PADDSB, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36646, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PADDSIW[] = {
    {I_PADDSIW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+35764, 87},
    ITEMPLATE_END
};

static const struct itemplate instrux_PADDSW[] = {
    {I_PADDSW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26181, 84},
    {I_PADDSW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36652, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PADDUSB[] = {
    {I_PADDUSB, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26188, 84},
    {I_PADDUSB, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36658, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PADDUSW[] = {
    {I_PADDUSW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26195, 84},
    {I_PADDUSW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36664, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PADDW[] = {
    {I_PADDW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26202, 84},
    {I_PADDW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36622, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PAND[] = {
    {I_PAND, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26209, 84},
    {I_PAND, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36670, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PANDN[] = {
    {I_PANDN, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26216, 84},
    {I_PANDN, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36676, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PAUSE[] = {
    {I_PAUSE, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41234, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_PAVEB[] = {
    {I_PAVEB, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+35770, 87},
    ITEMPLATE_END
};

static const struct itemplate instrux_PAVGUSB[] = {
    {I_PAVGUSB, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+8412, 88},
    ITEMPLATE_END
};

static const struct itemplate instrux_PCMPEQB[] = {
    {I_PCMPEQB, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26223, 84},
    {I_PCMPEQB, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36694, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PCMPEQD[] = {
    {I_PCMPEQD, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26230, 84},
    {I_PCMPEQD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36706, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PCMPEQW[] = {
    {I_PCMPEQW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26237, 84},
    {I_PCMPEQW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36700, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PCMPGTB[] = {
    {I_PCMPGTB, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26244, 84},
    {I_PCMPGTB, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36712, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PCMPGTD[] = {
    {I_PCMPGTD, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26251, 84},
    {I_PCMPGTD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36724, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PCMPGTW[] = {
    {I_PCMPGTW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26258, 84},
    {I_PCMPGTW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36718, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PDISTIB[] = {
    {I_PDISTIB, 2, {MMXREG,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+36971, 89},
    ITEMPLATE_END
};

static const struct itemplate instrux_PF2ID[] = {
    {I_PF2ID, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+8420, 88},
    ITEMPLATE_END
};

static const struct itemplate instrux_PFACC[] = {
    {I_PFACC, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+8428, 88},
    ITEMPLATE_END
};

static const struct itemplate instrux_PFADD[] = {
    {I_PFADD, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+8436, 88},
    ITEMPLATE_END
};

static const struct itemplate instrux_PFCMPEQ[] = {
    {I_PFCMPEQ, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+8444, 88},
    ITEMPLATE_END
};

static const struct itemplate instrux_PFCMPGE[] = {
    {I_PFCMPGE, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+8452, 88},
    ITEMPLATE_END
};

static const struct itemplate instrux_PFCMPGT[] = {
    {I_PFCMPGT, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+8460, 88},
    ITEMPLATE_END
};

static const struct itemplate instrux_PFMAX[] = {
    {I_PFMAX, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+8468, 88},
    ITEMPLATE_END
};

static const struct itemplate instrux_PFMIN[] = {
    {I_PFMIN, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+8476, 88},
    ITEMPLATE_END
};

static const struct itemplate instrux_PFMUL[] = {
    {I_PFMUL, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+8484, 88},
    ITEMPLATE_END
};

static const struct itemplate instrux_PFRCP[] = {
    {I_PFRCP, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+8492, 88},
    ITEMPLATE_END
};

static const struct itemplate instrux_PFRCPIT1[] = {
    {I_PFRCPIT1, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+8500, 88},
    ITEMPLATE_END
};

static const struct itemplate instrux_PFRCPIT2[] = {
    {I_PFRCPIT2, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+8508, 88},
    ITEMPLATE_END
};

static const struct itemplate instrux_PFRSQIT1[] = {
    {I_PFRSQIT1, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+8516, 88},
    ITEMPLATE_END
};

static const struct itemplate instrux_PFRSQRT[] = {
    {I_PFRSQRT, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+8524, 88},
    ITEMPLATE_END
};

static const struct itemplate instrux_PFSUB[] = {
    {I_PFSUB, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+8532, 88},
    ITEMPLATE_END
};

static const struct itemplate instrux_PFSUBR[] = {
    {I_PFSUBR, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+8540, 88},
    ITEMPLATE_END
};

static const struct itemplate instrux_PI2FD[] = {
    {I_PI2FD, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+8548, 88},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMACHRIW[] = {
    {I_PMACHRIW, 2, {MMXREG,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+37067, 89},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMADDWD[] = {
    {I_PMADDWD, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26265, 84},
    {I_PMADDWD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36730, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMAGW[] = {
    {I_PMAGW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+35776, 87},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMULHRIW[] = {
    {I_PMULHRIW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+35782, 87},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMULHRWA[] = {
    {I_PMULHRWA, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+8556, 88},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMULHRWC[] = {
    {I_PMULHRWC, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+35788, 87},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMULHW[] = {
    {I_PMULHW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26272, 84},
    {I_PMULHW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36772, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMULLW[] = {
    {I_PMULLW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26279, 84},
    {I_PMULLW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36778, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMVGEZB[] = {
    {I_PMVGEZB, 2, {MMXREG,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+37199, 87},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMVLZB[] = {
    {I_PMVLZB, 2, {MMXREG,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+37055, 87},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMVNZB[] = {
    {I_PMVNZB, 2, {MMXREG,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+37037, 87},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMVZB[] = {
    {I_PMVZB, 2, {MMXREG,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+36959, 87},
    ITEMPLATE_END
};

static const struct itemplate instrux_POP[] = {
    {I_POP, 1, {REG_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41238, 0},
    {I_POP, 1, {REG_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41242, 19},
    {I_POP, 1, {REG_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41246, 7},
    {I_POP, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39821, 0},
    {I_POP, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39826, 19},
    {I_POP, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39831, 7},
    {I_POP, 1, {REG_ES,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+8849, 1},
    {I_POP, 1, {REG_CS,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+3495, 90},
    {I_POP, 1, {REG_SS,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+3621, 1},
    {I_POP, 1, {REG_DS,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+3765, 1},
    {I_POP, 1, {REG_FS,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41250, 5},
    {I_POP, 1, {REG_GS,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41254, 5},
    ITEMPLATE_END
};

static const struct itemplate instrux_POPA[] = {
    {I_POPA, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41258, 18},
    ITEMPLATE_END
};

static const struct itemplate instrux_POPAD[] = {
    {I_POPAD, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41262, 19},
    ITEMPLATE_END
};

static const struct itemplate instrux_POPAW[] = {
    {I_POPAW, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41266, 18},
    ITEMPLATE_END
};

static const struct itemplate instrux_POPF[] = {
    {I_POPF, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41270, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_POPFD[] = {
    {I_POPFD, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41274, 19},
    ITEMPLATE_END
};

static const struct itemplate instrux_POPFQ[] = {
    {I_POPFQ, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41274, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_POPFW[] = {
    {I_POPFW, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41278, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_POR[] = {
    {I_POR, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26286, 84},
    {I_POR, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36790, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PREFETCH[] = {
    {I_PREFETCH, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39836, 88},
    ITEMPLATE_END
};

static const struct itemplate instrux_PREFETCHW[] = {
    {I_PREFETCHW, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39841, 88},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSLLD[] = {
    {I_PSLLD, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26293, 84},
    {I_PSLLD, 2, {MMXREG,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+26300, 38},
    {I_PSLLD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36808, 141},
    {I_PSLLD, 2, {XMM_L16,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+27042, 151},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSLLQ[] = {
    {I_PSLLQ, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26307, 84},
    {I_PSLLQ, 2, {MMXREG,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+26314, 38},
    {I_PSLLQ, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36814, 141},
    {I_PSLLQ, 2, {XMM_L16,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+27049, 151},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSLLW[] = {
    {I_PSLLW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26321, 84},
    {I_PSLLW, 2, {MMXREG,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+26328, 38},
    {I_PSLLW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36802, 141},
    {I_PSLLW, 2, {XMM_L16,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+27035, 151},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSRAD[] = {
    {I_PSRAD, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26335, 84},
    {I_PSRAD, 2, {MMXREG,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+26342, 38},
    {I_PSRAD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36826, 141},
    {I_PSRAD, 2, {XMM_L16,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+27063, 151},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSRAW[] = {
    {I_PSRAW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26349, 84},
    {I_PSRAW, 2, {MMXREG,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+26356, 38},
    {I_PSRAW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36820, 141},
    {I_PSRAW, 2, {XMM_L16,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+27056, 151},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSRLD[] = {
    {I_PSRLD, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26363, 84},
    {I_PSRLD, 2, {MMXREG,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+26370, 38},
    {I_PSRLD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36838, 141},
    {I_PSRLD, 2, {XMM_L16,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+27084, 151},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSRLQ[] = {
    {I_PSRLQ, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26377, 84},
    {I_PSRLQ, 2, {MMXREG,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+26384, 38},
    {I_PSRLQ, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36844, 141},
    {I_PSRLQ, 2, {XMM_L16,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+27091, 151},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSRLW[] = {
    {I_PSRLW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26391, 84},
    {I_PSRLW, 2, {MMXREG,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+26398, 38},
    {I_PSRLW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36832, 141},
    {I_PSRLW, 2, {XMM_L16,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+27077, 151},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSUBB[] = {
    {I_PSUBB, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26405, 84},
    {I_PSUBB, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36850, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSUBD[] = {
    {I_PSUBD, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26412, 84},
    {I_PSUBD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36862, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSUBSB[] = {
    {I_PSUBSB, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26419, 84},
    {I_PSUBSB, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36874, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSUBSIW[] = {
    {I_PSUBSIW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+35794, 87},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSUBSW[] = {
    {I_PSUBSW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26426, 84},
    {I_PSUBSW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36880, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSUBUSB[] = {
    {I_PSUBUSB, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26433, 84},
    {I_PSUBUSB, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36886, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSUBUSW[] = {
    {I_PSUBUSW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26440, 84},
    {I_PSUBUSW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36892, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSUBW[] = {
    {I_PSUBW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26447, 84},
    {I_PSUBW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36856, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PUNPCKHBW[] = {
    {I_PUNPCKHBW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26454, 84},
    {I_PUNPCKHBW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36898, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PUNPCKHDQ[] = {
    {I_PUNPCKHDQ, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26461, 84},
    {I_PUNPCKHDQ, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36910, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PUNPCKHWD[] = {
    {I_PUNPCKHWD, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26468, 84},
    {I_PUNPCKHWD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36904, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PUNPCKLBW[] = {
    {I_PUNPCKLBW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26475, 84},
    {I_PUNPCKLBW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36922, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PUNPCKLDQ[] = {
    {I_PUNPCKLDQ, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26482, 84},
    {I_PUNPCKLDQ, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36934, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PUNPCKLWD[] = {
    {I_PUNPCKLWD, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26489, 84},
    {I_PUNPCKLWD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36928, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PUSH[] = {
    {I_PUSH, 1, {REG_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41282, 0},
    {I_PUSH, 1, {REG_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41286, 19},
    {I_PUSH, 1, {REG_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41290, 7},
    {I_PUSH, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39846, 0},
    {I_PUSH, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39851, 19},
    {I_PUSH, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39856, 7},
    {I_PUSH, 1, {REG_ES,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+8817, 1},
    {I_PUSH, 1, {REG_CS,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+3477, 1},
    {I_PUSH, 1, {REG_SS,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+3603, 1},
    {I_PUSH, 1, {REG_DS,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+3747, 1},
    {I_PUSH, 1, {REG_FS,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41294, 5},
    {I_PUSH, 1, {REG_GS,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41298, 5},
    {I_PUSH, 1, {IMMEDIATE|BITS8,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39882, 39},
    {I_PUSH, 1, {SBYTEWORD|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39861, 91},
    {I_PUSH, 1, {IMMEDIATE|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39866, 91},
    {I_PUSH, 1, {SBYTEDWORD|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39871, 92},
    {I_PUSH, 1, {IMMEDIATE|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39876, 92},
    {I_PUSH, 1, {SBYTEDWORD|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39871, 93},
    {I_PUSH, 1, {IMMEDIATE|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39876, 93},
    {I_PUSH, 1, {SBYTEDWORD|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39881, 94},
    {I_PUSH, 1, {IMMEDIATE|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39886, 94},
    {I_PUSH, 1, {SBYTEDWORD|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39881, 94},
    {I_PUSH, 1, {IMMEDIATE|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39886, 94},
    ITEMPLATE_END
};

static const struct itemplate instrux_PUSHA[] = {
    {I_PUSHA, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41302, 18},
    ITEMPLATE_END
};

static const struct itemplate instrux_PUSHAD[] = {
    {I_PUSHAD, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41306, 19},
    ITEMPLATE_END
};

static const struct itemplate instrux_PUSHAW[] = {
    {I_PUSHAW, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41310, 18},
    ITEMPLATE_END
};

static const struct itemplate instrux_PUSHF[] = {
    {I_PUSHF, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41314, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_PUSHFD[] = {
    {I_PUSHFD, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41318, 19},
    ITEMPLATE_END
};

static const struct itemplate instrux_PUSHFQ[] = {
    {I_PUSHFQ, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41318, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_PUSHFW[] = {
    {I_PUSHFW, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41322, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_PXOR[] = {
    {I_PXOR, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26496, 84},
    {I_PXOR, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36946, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_RCL[] = {
    {I_RCL, 2, {RM_GPR|BITS8,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+41326, 0},
    {I_RCL, 2, {RM_GPR|BITS8,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+41330, 0},
    {I_RCL, 2, {RM_GPR|BITS8,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+39891, 39},
    {I_RCL, 2, {RM_GPR|BITS16,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+39896, 0},
    {I_RCL, 2, {RM_GPR|BITS16,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+39901, 0},
    {I_RCL, 2, {RM_GPR|BITS16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35800, 39},
    {I_RCL, 2, {RM_GPR|BITS32,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+39906, 5},
    {I_RCL, 2, {RM_GPR|BITS32,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+39911, 5},
    {I_RCL, 2, {RM_GPR|BITS32,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35806, 5},
    {I_RCL, 2, {RM_GPR|BITS64,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+39916, 7},
    {I_RCL, 2, {RM_GPR|BITS64,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+39921, 7},
    {I_RCL, 2, {RM_GPR|BITS64,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35812, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_RCR[] = {
    {I_RCR, 2, {RM_GPR|BITS8,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+41334, 0},
    {I_RCR, 2, {RM_GPR|BITS8,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+41338, 0},
    {I_RCR, 2, {RM_GPR|BITS8,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+39926, 39},
    {I_RCR, 2, {RM_GPR|BITS16,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+39931, 0},
    {I_RCR, 2, {RM_GPR|BITS16,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+39936, 0},
    {I_RCR, 2, {RM_GPR|BITS16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35818, 39},
    {I_RCR, 2, {RM_GPR|BITS32,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+39941, 5},
    {I_RCR, 2, {RM_GPR|BITS32,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+39946, 5},
    {I_RCR, 2, {RM_GPR|BITS32,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35824, 5},
    {I_RCR, 2, {RM_GPR|BITS64,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+39951, 7},
    {I_RCR, 2, {RM_GPR|BITS64,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+39956, 7},
    {I_RCR, 2, {RM_GPR|BITS64,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35830, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_RDSHR[] = {
    {I_RDSHR, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35836, 95},
    ITEMPLATE_END
};

static const struct itemplate instrux_RDMSR[] = {
    {I_RDMSR, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41342, 96},
    ITEMPLATE_END
};

static const struct itemplate instrux_RDPMC[] = {
    {I_RDPMC, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41346, 86},
    ITEMPLATE_END
};

static const struct itemplate instrux_RDTSC[] = {
    {I_RDTSC, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41350, 32},
    ITEMPLATE_END
};

static const struct itemplate instrux_RDTSCP[] = {
    {I_RDTSCP, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39961, 97},
    ITEMPLATE_END
};

static const struct itemplate instrux_RET[] = {
    {I_RET, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41371, 25},
    {I_RET, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39987, 98},
    ITEMPLATE_END
};

static const struct itemplate instrux_RETF[] = {
    {I_RETF, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41375, 0},
    {I_RETF, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39992, 72},
    ITEMPLATE_END
};

static const struct itemplate instrux_RETN[] = {
    {I_RETN, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41371, 25},
    {I_RETN, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39987, 98},
    ITEMPLATE_END
};

static const struct itemplate instrux_RETW[] = {
    {I_RETW, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41354, 25},
    {I_RETW, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39987, 98},
    ITEMPLATE_END
};

static const struct itemplate instrux_RETFW[] = {
    {I_RETFW, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41358, 0},
    {I_RETFW, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39966, 72},
    ITEMPLATE_END
};

static const struct itemplate instrux_RETNW[] = {
    {I_RETNW, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41354, 25},
    {I_RETNW, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39971, 98},
    ITEMPLATE_END
};

static const struct itemplate instrux_RETD[] = {
    {I_RETD, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41362, 26},
    {I_RETD, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39976, 99},
    ITEMPLATE_END
};

static const struct itemplate instrux_RETFD[] = {
    {I_RETFD, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41366, 0},
    {I_RETFD, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39981, 72},
    ITEMPLATE_END
};

static const struct itemplate instrux_RETND[] = {
    {I_RETND, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41362, 26},
    {I_RETND, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39976, 99},
    ITEMPLATE_END
};

static const struct itemplate instrux_RETQ[] = {
    {I_RETQ, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41370, 28},
    {I_RETQ, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39986, 100},
    ITEMPLATE_END
};

static const struct itemplate instrux_RETFQ[] = {
    {I_RETFQ, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41374, 7},
    {I_RETFQ, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39991, 101},
    ITEMPLATE_END
};

static const struct itemplate instrux_RETNQ[] = {
    {I_RETNQ, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41370, 28},
    {I_RETNQ, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39986, 100},
    ITEMPLATE_END
};

static const struct itemplate instrux_ROL[] = {
    {I_ROL, 2, {RM_GPR|BITS8,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+41378, 0},
    {I_ROL, 2, {RM_GPR|BITS8,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+41382, 0},
    {I_ROL, 2, {RM_GPR|BITS8,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+39996, 39},
    {I_ROL, 2, {RM_GPR|BITS16,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40001, 0},
    {I_ROL, 2, {RM_GPR|BITS16,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+40006, 0},
    {I_ROL, 2, {RM_GPR|BITS16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35842, 39},
    {I_ROL, 2, {RM_GPR|BITS32,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40011, 5},
    {I_ROL, 2, {RM_GPR|BITS32,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+40016, 5},
    {I_ROL, 2, {RM_GPR|BITS32,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35848, 5},
    {I_ROL, 2, {RM_GPR|BITS64,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40021, 7},
    {I_ROL, 2, {RM_GPR|BITS64,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+40026, 7},
    {I_ROL, 2, {RM_GPR|BITS64,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35854, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_ROR[] = {
    {I_ROR, 2, {RM_GPR|BITS8,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+41386, 0},
    {I_ROR, 2, {RM_GPR|BITS8,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+41390, 0},
    {I_ROR, 2, {RM_GPR|BITS8,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+40031, 39},
    {I_ROR, 2, {RM_GPR|BITS16,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40036, 0},
    {I_ROR, 2, {RM_GPR|BITS16,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+40041, 0},
    {I_ROR, 2, {RM_GPR|BITS16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35860, 39},
    {I_ROR, 2, {RM_GPR|BITS32,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40046, 5},
    {I_ROR, 2, {RM_GPR|BITS32,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+40051, 5},
    {I_ROR, 2, {RM_GPR|BITS32,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35866, 5},
    {I_ROR, 2, {RM_GPR|BITS64,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40056, 7},
    {I_ROR, 2, {RM_GPR|BITS64,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+40061, 7},
    {I_ROR, 2, {RM_GPR|BITS64,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35872, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_RDM[] = {
    {I_RDM, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40554, 37},
    ITEMPLATE_END
};

static const struct itemplate instrux_RSDC[] = {
    {I_RSDC, 2, {REG_SREG,MEMORY|BITS80,0,0,0}, NO_DECORATOR, nasm_bytecodes+37319, 102},
    ITEMPLATE_END
};

static const struct itemplate instrux_RSLDT[] = {
    {I_RSLDT, 1, {MEMORY|BITS80,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40066, 102},
    ITEMPLATE_END
};

static const struct itemplate instrux_RSM[] = {
    {I_RSM, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41394, 103},
    ITEMPLATE_END
};

static const struct itemplate instrux_RSTS[] = {
    {I_RSTS, 1, {MEMORY|BITS80,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40071, 102},
    ITEMPLATE_END
};

static const struct itemplate instrux_SAHF[] = {
    {I_SAHF, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+8441, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_SAL[] = {
    {I_SAL, 2, {RM_GPR|BITS8,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+41398, 0},
    {I_SAL, 2, {RM_GPR|BITS8,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+41402, 0},
    {I_SAL, 2, {RM_GPR|BITS8,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+40076, 39},
    {I_SAL, 2, {RM_GPR|BITS16,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40081, 0},
    {I_SAL, 2, {RM_GPR|BITS16,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+40086, 0},
    {I_SAL, 2, {RM_GPR|BITS16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35878, 39},
    {I_SAL, 2, {RM_GPR|BITS32,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40091, 5},
    {I_SAL, 2, {RM_GPR|BITS32,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+40096, 5},
    {I_SAL, 2, {RM_GPR|BITS32,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35884, 5},
    {I_SAL, 2, {RM_GPR|BITS64,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40101, 7},
    {I_SAL, 2, {RM_GPR|BITS64,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+40106, 7},
    {I_SAL, 2, {RM_GPR|BITS64,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35890, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_SALC[] = {
    {I_SALC, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40498, 104},
    ITEMPLATE_END
};

static const struct itemplate instrux_SAR[] = {
    {I_SAR, 2, {RM_GPR|BITS8,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+41406, 0},
    {I_SAR, 2, {RM_GPR|BITS8,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+41410, 0},
    {I_SAR, 2, {RM_GPR|BITS8,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+40111, 39},
    {I_SAR, 2, {RM_GPR|BITS16,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40116, 0},
    {I_SAR, 2, {RM_GPR|BITS16,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+40121, 0},
    {I_SAR, 2, {RM_GPR|BITS16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35896, 39},
    {I_SAR, 2, {RM_GPR|BITS32,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40126, 5},
    {I_SAR, 2, {RM_GPR|BITS32,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+40131, 5},
    {I_SAR, 2, {RM_GPR|BITS32,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35902, 5},
    {I_SAR, 2, {RM_GPR|BITS64,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40136, 7},
    {I_SAR, 2, {RM_GPR|BITS64,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+40141, 7},
    {I_SAR, 2, {RM_GPR|BITS64,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35908, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_SBB[] = {
    {I_SBB, 2, {MEMORY,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+40146, 3},
    {I_SBB, 2, {REG_GPR|BITS8,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+40147, 0},
    {I_SBB, 2, {MEMORY,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35914, 3},
    {I_SBB, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+35915, 0},
    {I_SBB, 2, {MEMORY,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35920, 4},
    {I_SBB, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+35921, 5},
    {I_SBB, 2, {MEMORY,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+35926, 6},
    {I_SBB, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+35927, 7},
    {I_SBB, 2, {REG_GPR|BITS8,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+28088, 8},
    {I_SBB, 2, {REG_GPR|BITS8,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+28088, 0},
    {I_SBB, 2, {REG_GPR|BITS16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40151, 8},
    {I_SBB, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+40151, 0},
    {I_SBB, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40156, 9},
    {I_SBB, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+40156, 5},
    {I_SBB, 2, {REG_GPR|BITS64,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40161, 10},
    {I_SBB, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+40161, 7},
    {I_SBB, 2, {RM_GPR|BITS16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+26503, 11},
    {I_SBB, 2, {RM_GPR|BITS32,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+26510, 12},
    {I_SBB, 2, {RM_GPR|BITS64,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+26517, 13},
    {I_SBB, 2, {REG_AL,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+41414, 8},
    {I_SBB, 2, {REG_AX,SBYTEWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+26504, 8},
    {I_SBB, 2, {REG_AX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+40166, 8},
    {I_SBB, 2, {REG_EAX,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+26511, 9},
    {I_SBB, 2, {REG_EAX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+40171, 9},
    {I_SBB, 2, {REG_RAX,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+26518, 10},
    {I_SBB, 2, {REG_RAX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+40176, 10},
    {I_SBB, 2, {RM_GPR|BITS8,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+35932, 3},
    {I_SBB, 2, {RM_GPR|BITS16,SBYTEWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+26503, 3},
    {I_SBB, 2, {RM_GPR|BITS16,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+26524, 3},
    {I_SBB, 2, {RM_GPR|BITS32,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+26510, 4},
    {I_SBB, 2, {RM_GPR|BITS32,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+26531, 4},
    {I_SBB, 2, {RM_GPR|BITS64,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+26517, 6},
    {I_SBB, 2, {RM_GPR|BITS64,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+26538, 6},
    {I_SBB, 2, {MEMORY,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35932, 3},
    {I_SBB, 2, {MEMORY,SBYTEWORD|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+26503, 3},
    {I_SBB, 2, {MEMORY,IMMEDIATE|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+26524, 3},
    {I_SBB, 2, {MEMORY,SBYTEDWORD|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+26510, 4},
    {I_SBB, 2, {MEMORY,IMMEDIATE|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+26531, 4},
    {I_SBB, 2, {RM_GPR|BITS8,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+35938, 14},
    ITEMPLATE_END
};

static const struct itemplate instrux_SCASB[] = {
    {I_SCASB, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41418, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_SCASD[] = {
    {I_SCASD, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40181, 5},
    ITEMPLATE_END
};

static const struct itemplate instrux_SCASQ[] = {
    {I_SCASQ, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40186, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_SCASW[] = {
    {I_SCASW, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40191, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_SFENCE[] = {
    {I_SFENCE, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35944, 59},
    {I_SFENCE, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35944, 135},
    ITEMPLATE_END
};

static const struct itemplate instrux_SGDT[] = {
    {I_SGDT, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40196, 105},
    ITEMPLATE_END
};

static const struct itemplate instrux_SHL[] = {
    {I_SHL, 2, {RM_GPR|BITS8,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+41398, 0},
    {I_SHL, 2, {RM_GPR|BITS8,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+41402, 0},
    {I_SHL, 2, {RM_GPR|BITS8,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+40076, 39},
    {I_SHL, 2, {RM_GPR|BITS16,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40081, 0},
    {I_SHL, 2, {RM_GPR|BITS16,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+40086, 0},
    {I_SHL, 2, {RM_GPR|BITS16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35878, 39},
    {I_SHL, 2, {RM_GPR|BITS32,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40091, 5},
    {I_SHL, 2, {RM_GPR|BITS32,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+40096, 5},
    {I_SHL, 2, {RM_GPR|BITS32,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35884, 5},
    {I_SHL, 2, {RM_GPR|BITS64,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40101, 7},
    {I_SHL, 2, {RM_GPR|BITS64,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+40106, 7},
    {I_SHL, 2, {RM_GPR|BITS64,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35890, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_SHLD[] = {
    {I_SHLD, 3, {MEMORY,REG_GPR|BITS16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+26545, 106},
    {I_SHLD, 3, {REG_GPR|BITS16,REG_GPR|BITS16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+26545, 106},
    {I_SHLD, 3, {MEMORY,REG_GPR|BITS32,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+26552, 106},
    {I_SHLD, 3, {REG_GPR|BITS32,REG_GPR|BITS32,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+26552, 106},
    {I_SHLD, 3, {MEMORY,REG_GPR|BITS64,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+26559, 107},
    {I_SHLD, 3, {REG_GPR|BITS64,REG_GPR|BITS64,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+26559, 107},
    {I_SHLD, 3, {MEMORY,REG_GPR|BITS16,REG_CL,0,0}, NO_DECORATOR, nasm_bytecodes+35950, 9},
    {I_SHLD, 3, {REG_GPR|BITS16,REG_GPR|BITS16,REG_CL,0,0}, NO_DECORATOR, nasm_bytecodes+35950, 5},
    {I_SHLD, 3, {MEMORY,REG_GPR|BITS32,REG_CL,0,0}, NO_DECORATOR, nasm_bytecodes+35956, 9},
    {I_SHLD, 3, {REG_GPR|BITS32,REG_GPR|BITS32,REG_CL,0,0}, NO_DECORATOR, nasm_bytecodes+35956, 5},
    {I_SHLD, 3, {MEMORY,REG_GPR|BITS64,REG_CL,0,0}, NO_DECORATOR, nasm_bytecodes+35962, 10},
    {I_SHLD, 3, {REG_GPR|BITS64,REG_GPR|BITS64,REG_CL,0,0}, NO_DECORATOR, nasm_bytecodes+35962, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_SHR[] = {
    {I_SHR, 2, {RM_GPR|BITS8,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+41422, 0},
    {I_SHR, 2, {RM_GPR|BITS8,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+41426, 0},
    {I_SHR, 2, {RM_GPR|BITS8,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+40201, 39},
    {I_SHR, 2, {RM_GPR|BITS16,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40206, 0},
    {I_SHR, 2, {RM_GPR|BITS16,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+40211, 0},
    {I_SHR, 2, {RM_GPR|BITS16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35968, 39},
    {I_SHR, 2, {RM_GPR|BITS32,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40216, 5},
    {I_SHR, 2, {RM_GPR|BITS32,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+40221, 5},
    {I_SHR, 2, {RM_GPR|BITS32,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35974, 5},
    {I_SHR, 2, {RM_GPR|BITS64,UNITY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40226, 7},
    {I_SHR, 2, {RM_GPR|BITS64,REG_CL,0,0,0}, NO_DECORATOR, nasm_bytecodes+40231, 7},
    {I_SHR, 2, {RM_GPR|BITS64,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+35980, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_SHRD[] = {
    {I_SHRD, 3, {MEMORY,REG_GPR|BITS16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+26566, 106},
    {I_SHRD, 3, {REG_GPR|BITS16,REG_GPR|BITS16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+26566, 106},
    {I_SHRD, 3, {MEMORY,REG_GPR|BITS32,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+26573, 106},
    {I_SHRD, 3, {REG_GPR|BITS32,REG_GPR|BITS32,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+26573, 106},
    {I_SHRD, 3, {MEMORY,REG_GPR|BITS64,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+26580, 107},
    {I_SHRD, 3, {REG_GPR|BITS64,REG_GPR|BITS64,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+26580, 107},
    {I_SHRD, 3, {MEMORY,REG_GPR|BITS16,REG_CL,0,0}, NO_DECORATOR, nasm_bytecodes+35986, 9},
    {I_SHRD, 3, {REG_GPR|BITS16,REG_GPR|BITS16,REG_CL,0,0}, NO_DECORATOR, nasm_bytecodes+35986, 5},
    {I_SHRD, 3, {MEMORY,REG_GPR|BITS32,REG_CL,0,0}, NO_DECORATOR, nasm_bytecodes+35992, 9},
    {I_SHRD, 3, {REG_GPR|BITS32,REG_GPR|BITS32,REG_CL,0,0}, NO_DECORATOR, nasm_bytecodes+35992, 5},
    {I_SHRD, 3, {MEMORY,REG_GPR|BITS64,REG_CL,0,0}, NO_DECORATOR, nasm_bytecodes+35998, 10},
    {I_SHRD, 3, {REG_GPR|BITS64,REG_GPR|BITS64,REG_CL,0,0}, NO_DECORATOR, nasm_bytecodes+35998, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_SIDT[] = {
    {I_SIDT, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40236, 105},
    ITEMPLATE_END
};

static const struct itemplate instrux_SLDT[] = {
    {I_SLDT, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+36023, 105},
    {I_SLDT, 1, {MEMORY|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+36023, 105},
    {I_SLDT, 1, {REG_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+36004, 105},
    {I_SLDT, 1, {REG_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+36010, 5},
    {I_SLDT, 1, {REG_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+36016, 7},
    {I_SLDT, 1, {REG_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+36022, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_SKINIT[] = {
    {I_SKINIT, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40241, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_SMI[] = {
    {I_SMI, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41544, 108},
    ITEMPLATE_END
};

static const struct itemplate instrux_SMINT[] = {
    {I_SMINT, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41430, 37},
    ITEMPLATE_END
};

static const struct itemplate instrux_SMINTOLD[] = {
    {I_SMINTOLD, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41434, 109},
    ITEMPLATE_END
};

static const struct itemplate instrux_SMSW[] = {
    {I_SMSW, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+36041, 105},
    {I_SMSW, 1, {MEMORY|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+36041, 105},
    {I_SMSW, 1, {REG_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+36028, 105},
    {I_SMSW, 1, {REG_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+36034, 5},
    {I_SMSW, 1, {REG_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+36040, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_STC[] = {
    {I_STC, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39963, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_STD[] = {
    {I_STD, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41568, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_STI[] = {
    {I_STI, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+39758, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_STOSB[] = {
    {I_STOSB, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+8545, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_STOSD[] = {
    {I_STOSD, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41438, 5},
    ITEMPLATE_END
};

static const struct itemplate instrux_STOSQ[] = {
    {I_STOSQ, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41442, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_STOSW[] = {
    {I_STOSW, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41446, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_STR[] = {
    {I_STR, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+36059, 62},
    {I_STR, 1, {MEMORY|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+36059, 62},
    {I_STR, 1, {REG_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+36046, 62},
    {I_STR, 1, {REG_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+36052, 63},
    {I_STR, 1, {REG_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+36058, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_SUB[] = {
    {I_SUB, 2, {MEMORY,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+40246, 3},
    {I_SUB, 2, {REG_GPR|BITS8,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+40247, 0},
    {I_SUB, 2, {MEMORY,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36064, 3},
    {I_SUB, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36065, 0},
    {I_SUB, 2, {MEMORY,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+36070, 4},
    {I_SUB, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+36071, 5},
    {I_SUB, 2, {MEMORY,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+36076, 6},
    {I_SUB, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+36077, 7},
    {I_SUB, 2, {REG_GPR|BITS8,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+33856, 8},
    {I_SUB, 2, {REG_GPR|BITS8,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+33856, 0},
    {I_SUB, 2, {REG_GPR|BITS16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40251, 8},
    {I_SUB, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+40251, 0},
    {I_SUB, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40256, 9},
    {I_SUB, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+40256, 5},
    {I_SUB, 2, {REG_GPR|BITS64,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40261, 10},
    {I_SUB, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+40261, 7},
    {I_SUB, 2, {RM_GPR|BITS16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+26587, 11},
    {I_SUB, 2, {RM_GPR|BITS32,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+26594, 12},
    {I_SUB, 2, {RM_GPR|BITS64,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+26601, 13},
    {I_SUB, 2, {REG_AL,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+41450, 8},
    {I_SUB, 2, {REG_AX,SBYTEWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+26588, 8},
    {I_SUB, 2, {REG_AX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+40266, 8},
    {I_SUB, 2, {REG_EAX,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+26595, 9},
    {I_SUB, 2, {REG_EAX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+40271, 9},
    {I_SUB, 2, {REG_RAX,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+26602, 10},
    {I_SUB, 2, {REG_RAX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+40276, 10},
    {I_SUB, 2, {RM_GPR|BITS8,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+36082, 3},
    {I_SUB, 2, {RM_GPR|BITS16,SBYTEWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+26587, 3},
    {I_SUB, 2, {RM_GPR|BITS16,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+26608, 3},
    {I_SUB, 2, {RM_GPR|BITS32,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+26594, 4},
    {I_SUB, 2, {RM_GPR|BITS32,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+26615, 4},
    {I_SUB, 2, {RM_GPR|BITS64,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+26601, 6},
    {I_SUB, 2, {RM_GPR|BITS64,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+26622, 6},
    {I_SUB, 2, {MEMORY,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+36082, 3},
    {I_SUB, 2, {MEMORY,SBYTEWORD|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+26587, 3},
    {I_SUB, 2, {MEMORY,IMMEDIATE|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+26608, 3},
    {I_SUB, 2, {MEMORY,SBYTEDWORD|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+26594, 4},
    {I_SUB, 2, {MEMORY,IMMEDIATE|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+26615, 4},
    {I_SUB, 2, {RM_GPR|BITS8,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+36088, 14},
    ITEMPLATE_END
};

static const struct itemplate instrux_SVDC[] = {
    {I_SVDC, 2, {MEMORY|BITS80,REG_SREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+27177, 102},
    ITEMPLATE_END
};

static const struct itemplate instrux_SVLDT[] = {
    {I_SVLDT, 1, {MEMORY|BITS80,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40281, 102},
    ITEMPLATE_END
};

static const struct itemplate instrux_SVTS[] = {
    {I_SVTS, 1, {MEMORY|BITS80,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40286, 102},
    ITEMPLATE_END
};

static const struct itemplate instrux_SWAPGS[] = {
    {I_SWAPGS, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40291, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_SYSCALL[] = {
    {I_SYSCALL, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41162, 110},
    ITEMPLATE_END
};

static const struct itemplate instrux_SYSENTER[] = {
    {I_SYSENTER, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41454, 86},
    ITEMPLATE_END
};

static const struct itemplate instrux_SYSEXIT[] = {
    {I_SYSEXIT, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41458, 111},
    ITEMPLATE_END
};

static const struct itemplate instrux_SYSRET[] = {
    {I_SYSRET, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41158, 112},
    ITEMPLATE_END
};

static const struct itemplate instrux_TEST[] = {
    {I_TEST, 2, {MEMORY,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+41462, 8},
    {I_TEST, 2, {REG_GPR|BITS8,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+41462, 0},
    {I_TEST, 2, {MEMORY,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+40296, 8},
    {I_TEST, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+40296, 0},
    {I_TEST, 2, {MEMORY,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+40301, 9},
    {I_TEST, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+40301, 5},
    {I_TEST, 2, {MEMORY,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+40306, 10},
    {I_TEST, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+40306, 7},
    {I_TEST, 2, {REG_GPR|BITS8,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+41466, 8},
    {I_TEST, 2, {REG_GPR|BITS16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40311, 8},
    {I_TEST, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40316, 9},
    {I_TEST, 2, {REG_GPR|BITS64,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40321, 10},
    {I_TEST, 2, {REG_AL,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+41470, 8},
    {I_TEST, 2, {REG_AX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+40326, 8},
    {I_TEST, 2, {REG_EAX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+40331, 9},
    {I_TEST, 2, {REG_RAX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+40336, 10},
    {I_TEST, 2, {RM_GPR|BITS8,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+40341, 8},
    {I_TEST, 2, {RM_GPR|BITS16,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+36094, 8},
    {I_TEST, 2, {RM_GPR|BITS32,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+36100, 9},
    {I_TEST, 2, {RM_GPR|BITS64,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+36106, 10},
    {I_TEST, 2, {MEMORY,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+40341, 8},
    {I_TEST, 2, {MEMORY,IMMEDIATE|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36094, 8},
    {I_TEST, 2, {MEMORY,IMMEDIATE|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+36100, 9},
    ITEMPLATE_END
};

static const struct itemplate instrux_UD0[] = {
    {I_UD0, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41474, 113},
    {I_UD0, 2, {REG_GPR|BITS16,RM_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36112, 39},
    {I_UD0, 2, {REG_GPR|BITS32,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+36118, 39},
    {I_UD0, 2, {REG_GPR|BITS64,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+36124, 39},
    ITEMPLATE_END
};

static const struct itemplate instrux_UD1[] = {
    {I_UD1, 2, {REG_GPR,RM_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36130, 39},
    {I_UD1, 2, {REG_GPR,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+36136, 39},
    {I_UD1, 2, {REG_GPR,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+36142, 39},
    {I_UD1, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41478, 39},
    ITEMPLATE_END
};

static const struct itemplate instrux_UD2B[] = {
    {I_UD2B, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41478, 39},
    {I_UD2B, 2, {REG_GPR,RM_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36130, 39},
    {I_UD2B, 2, {REG_GPR,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+36136, 39},
    {I_UD2B, 2, {REG_GPR,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+36142, 39},
    ITEMPLATE_END
};

static const struct itemplate instrux_UD2[] = {
    {I_UD2, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41482, 39},
    ITEMPLATE_END
};

static const struct itemplate instrux_UD2A[] = {
    {I_UD2A, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41482, 39},
    ITEMPLATE_END
};

static const struct itemplate instrux_UMOV[] = {
    {I_UMOV, 2, {MEMORY,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+36148, 114},
    {I_UMOV, 2, {REG_GPR|BITS8,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+36148, 108},
    {I_UMOV, 2, {MEMORY,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+26629, 114},
    {I_UMOV, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+26629, 108},
    {I_UMOV, 2, {MEMORY,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+26636, 114},
    {I_UMOV, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+26636, 108},
    {I_UMOV, 2, {REG_GPR|BITS8,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+36154, 114},
    {I_UMOV, 2, {REG_GPR|BITS8,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+36154, 108},
    {I_UMOV, 2, {REG_GPR|BITS16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+26643, 114},
    {I_UMOV, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+26643, 108},
    {I_UMOV, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+26650, 114},
    {I_UMOV, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+26650, 108},
    ITEMPLATE_END
};

static const struct itemplate instrux_VERR[] = {
    {I_VERR, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40346, 62},
    {I_VERR, 1, {MEMORY|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40346, 62},
    {I_VERR, 1, {REG_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40346, 62},
    ITEMPLATE_END
};

static const struct itemplate instrux_VERW[] = {
    {I_VERW, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40351, 62},
    {I_VERW, 1, {MEMORY|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40351, 62},
    {I_VERW, 1, {REG_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40351, 62},
    ITEMPLATE_END
};

static const struct itemplate instrux_FWAIT[] = {
    {I_FWAIT, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41060, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_WBINVD[] = {
    {I_WBINVD, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40522, 54},
    ITEMPLATE_END
};

static const struct itemplate instrux_WRSHR[] = {
    {I_WRSHR, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+36160, 95},
    ITEMPLATE_END
};

static const struct itemplate instrux_WRMSR[] = {
    {I_WRMSR, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41486, 96},
    ITEMPLATE_END
};

static const struct itemplate instrux_XADD[] = {
    {I_XADD, 2, {MEMORY,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+36166, 115},
    {I_XADD, 2, {REG_GPR|BITS8,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+36167, 20},
    {I_XADD, 2, {MEMORY,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+26657, 115},
    {I_XADD, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+26658, 20},
    {I_XADD, 2, {MEMORY,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+26664, 115},
    {I_XADD, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+26665, 20},
    {I_XADD, 2, {MEMORY,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+26671, 6},
    {I_XADD, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+26672, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_XBTS[] = {
    {I_XBTS, 2, {REG_GPR|BITS16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+36172, 116},
    {I_XBTS, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36172, 108},
    {I_XBTS, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+36178, 117},
    {I_XBTS, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+36178, 108},
    ITEMPLATE_END
};

static const struct itemplate instrux_XCHG[] = {
    {I_XCHG, 2, {REG_AX,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+41490, 0},
    {I_XCHG, 2, {REG_EAX,REG32NA,0,0,0}, NO_DECORATOR, nasm_bytecodes+41494, 5},
    {I_XCHG, 2, {REG_RAX,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+41498, 7},
    {I_XCHG, 2, {REG_GPR|BITS16,REG_AX,0,0,0}, NO_DECORATOR, nasm_bytecodes+41502, 0},
    {I_XCHG, 2, {REG32NA,REG_EAX,0,0,0}, NO_DECORATOR, nasm_bytecodes+41506, 5},
    {I_XCHG, 2, {REG_GPR|BITS64,REG_RAX,0,0,0}, NO_DECORATOR, nasm_bytecodes+41510, 7},
    {I_XCHG, 2, {REG_EAX,REG_EAX,0,0,0}, NO_DECORATOR, nasm_bytecodes+41514, 19},
    {I_XCHG, 2, {REG_GPR|BITS8,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40356, 3},
    {I_XCHG, 2, {REG_GPR|BITS8,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+40357, 0},
    {I_XCHG, 2, {REG_GPR|BITS16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+36184, 3},
    {I_XCHG, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36185, 0},
    {I_XCHG, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+36190, 4},
    {I_XCHG, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+36191, 5},
    {I_XCHG, 2, {REG_GPR|BITS64,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+36196, 6},
    {I_XCHG, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+36197, 7},
    {I_XCHG, 2, {MEMORY,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+40361, 3},
    {I_XCHG, 2, {REG_GPR|BITS8,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+40362, 0},
    {I_XCHG, 2, {MEMORY,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36202, 3},
    {I_XCHG, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36203, 0},
    {I_XCHG, 2, {MEMORY,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+36208, 4},
    {I_XCHG, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+36209, 5},
    {I_XCHG, 2, {MEMORY,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+36214, 6},
    {I_XCHG, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+36215, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_XLATB[] = {
    {I_XLATB, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37573, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_XLAT[] = {
    {I_XLAT, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37573, 0},
    ITEMPLATE_END
};

static const struct itemplate instrux_XOR[] = {
    {I_XOR, 2, {MEMORY,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+40366, 3},
    {I_XOR, 2, {REG_GPR|BITS8,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+40367, 0},
    {I_XOR, 2, {MEMORY,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36220, 3},
    {I_XOR, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36221, 0},
    {I_XOR, 2, {MEMORY,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+36226, 4},
    {I_XOR, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+36227, 5},
    {I_XOR, 2, {MEMORY,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+36232, 6},
    {I_XOR, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+36233, 7},
    {I_XOR, 2, {REG_GPR|BITS8,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+33296, 8},
    {I_XOR, 2, {REG_GPR|BITS8,REG_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+33296, 0},
    {I_XOR, 2, {REG_GPR|BITS16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40371, 8},
    {I_XOR, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+40371, 0},
    {I_XOR, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40376, 9},
    {I_XOR, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+40376, 5},
    {I_XOR, 2, {REG_GPR|BITS64,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+40381, 10},
    {I_XOR, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+40381, 7},
    {I_XOR, 2, {RM_GPR|BITS16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+26678, 11},
    {I_XOR, 2, {RM_GPR|BITS32,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+26685, 12},
    {I_XOR, 2, {RM_GPR|BITS64,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+26692, 13},
    {I_XOR, 2, {REG_AL,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+41518, 8},
    {I_XOR, 2, {REG_AX,SBYTEWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+26679, 8},
    {I_XOR, 2, {REG_AX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+40386, 8},
    {I_XOR, 2, {REG_EAX,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+26686, 9},
    {I_XOR, 2, {REG_EAX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+40391, 9},
    {I_XOR, 2, {REG_RAX,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+26693, 10},
    {I_XOR, 2, {REG_RAX,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+40396, 10},
    {I_XOR, 2, {RM_GPR|BITS8,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+36238, 3},
    {I_XOR, 2, {RM_GPR|BITS16,SBYTEWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+26678, 3},
    {I_XOR, 2, {RM_GPR|BITS16,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+26699, 3},
    {I_XOR, 2, {RM_GPR|BITS32,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+26685, 4},
    {I_XOR, 2, {RM_GPR|BITS32,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+26706, 4},
    {I_XOR, 2, {RM_GPR|BITS64,SBYTEDWORD,0,0,0}, NO_DECORATOR, nasm_bytecodes+26692, 6},
    {I_XOR, 2, {RM_GPR|BITS64,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+26713, 6},
    {I_XOR, 2, {MEMORY,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+36238, 3},
    {I_XOR, 2, {MEMORY,SBYTEWORD|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+26678, 3},
    {I_XOR, 2, {MEMORY,IMMEDIATE|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+26699, 3},
    {I_XOR, 2, {MEMORY,SBYTEDWORD|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+26685, 4},
    {I_XOR, 2, {MEMORY,IMMEDIATE|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+26706, 4},
    {I_XOR, 2, {RM_GPR|BITS8,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+36244, 14},
    ITEMPLATE_END
};

static const struct itemplate instrux_ADDPS[] = {
    {I_ADDPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+36256, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_ADDSS[] = {
    {I_ADDSS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+36262, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_ANDNPS[] = {
    {I_ANDNPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+36268, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_ANDPS[] = {
    {I_ANDPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+36274, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPEQPS[] = {
    {I_CMPEQPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+8564, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPEQSS[] = {
    {I_CMPEQSS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+8572, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPLEPS[] = {
    {I_CMPLEPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+8580, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPLESS[] = {
    {I_CMPLESS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+8588, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPLTPS[] = {
    {I_CMPLTPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+8596, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPLTSS[] = {
    {I_CMPLTSS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+8604, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPNEQPS[] = {
    {I_CMPNEQPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+8612, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPNEQSS[] = {
    {I_CMPNEQSS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+8620, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPNLEPS[] = {
    {I_CMPNLEPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+8628, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPNLESS[] = {
    {I_CMPNLESS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+8636, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPNLTPS[] = {
    {I_CMPNLTPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+8644, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPNLTSS[] = {
    {I_CMPNLTSS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+8652, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPORDPS[] = {
    {I_CMPORDPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+8660, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPORDSS[] = {
    {I_CMPORDSS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+8668, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPUNORDPS[] = {
    {I_CMPUNORDPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+8676, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPUNORDSS[] = {
    {I_CMPUNORDSS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+8684, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPPS[] = {
    {I_CMPPS, 3, {XMM_L16,MEMORY,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+26776, 121},
    {I_CMPPS, 3, {XMM_L16,XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+26776, 121},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPSS[] = {
    {I_CMPSS, 3, {XMM_L16,MEMORY,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+26783, 121},
    {I_CMPSS, 3, {XMM_L16,XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+26783, 121},
    ITEMPLATE_END
};

static const struct itemplate instrux_COMISS[] = {
    {I_COMISS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+36280, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_CVTPI2PS[] = {
    {I_CVTPI2PS, 2, {XMM_L16,RM_MMX|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+36286, 122},
    ITEMPLATE_END
};

static const struct itemplate instrux_CVTPS2PI[] = {
    {I_CVTPS2PI, 2, {MMXREG,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+36292, 122},
    ITEMPLATE_END
};

static const struct itemplate instrux_CVTSI2SS[] = {
    {I_CVTSI2SS, 2, {XMM_L16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+26791, 123},
    {I_CVTSI2SS, 2, {XMM_L16,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+26791, 123},
    {I_CVTSI2SS, 2, {XMM_L16,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+26790, 124},
    ITEMPLATE_END
};

static const struct itemplate instrux_CVTSS2SI[] = {
    {I_CVTSS2SI, 2, {REG_GPR|BITS32,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+26798, 123},
    {I_CVTSS2SI, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+26798, 123},
    {I_CVTSS2SI, 2, {REG_GPR|BITS64,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+26797, 125},
    {I_CVTSS2SI, 2, {REG_GPR|BITS64,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+26797, 125},
    ITEMPLATE_END
};

static const struct itemplate instrux_CVTTPS2PI[] = {
    {I_CVTTPS2PI, 2, {MMXREG,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36298, 126},
    ITEMPLATE_END
};

static const struct itemplate instrux_CVTTSS2SI[] = {
    {I_CVTTSS2SI, 2, {REG_GPR|BITS32,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+26805, 123},
    {I_CVTTSS2SI, 2, {REG_GPR|BITS64,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+26804, 125},
    ITEMPLATE_END
};

static const struct itemplate instrux_DIVPS[] = {
    {I_DIVPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+36304, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_DIVSS[] = {
    {I_DIVSS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+36310, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_LDMXCSR[] = {
    {I_LDMXCSR, 1, {MEMORY|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+36316, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_MAXPS[] = {
    {I_MAXPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+36322, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_MAXSS[] = {
    {I_MAXSS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+36328, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_MINPS[] = {
    {I_MINPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+36334, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_MINSS[] = {
    {I_MINSS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+36340, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVAPS[] = {
    {I_MOVAPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+36346, 120},
    {I_MOVAPS, 2, {RM_XMM_L16|BITS128,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36352, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVHPS[] = {
    {I_MOVHPS, 2, {XMM_L16,MEMORY|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+36358, 120},
    {I_MOVHPS, 2, {MEMORY|BITS64,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36364, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVLHPS[] = {
    {I_MOVLHPS, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36358, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVLPS[] = {
    {I_MOVLPS, 2, {XMM_L16,MEMORY|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+36154, 120},
    {I_MOVLPS, 2, {MEMORY|BITS64,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36370, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVHLPS[] = {
    {I_MOVHLPS, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36154, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVMSKPS[] = {
    {I_MOVMSKPS, 2, {REG_GPR|BITS32,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36376, 120},
    {I_MOVMSKPS, 2, {REG_GPR|BITS64,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+26811, 127},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVNTPS[] = {
    {I_MOVNTPS, 2, {MEMORY|BITS128,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36382, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVSS[] = {
    {I_MOVSS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+36388, 120},
    {I_MOVSS, 2, {MEMORY|BITS32,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36394, 120},
    {I_MOVSS, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36388, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVUPS[] = {
    {I_MOVUPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+36400, 120},
    {I_MOVUPS, 2, {RM_XMM_L16|BITS128,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36406, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_MULPS[] = {
    {I_MULPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+36412, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_MULSS[] = {
    {I_MULSS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+36418, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_ORPS[] = {
    {I_ORPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+36424, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_RCPPS[] = {
    {I_RCPPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+36430, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_RCPSS[] = {
    {I_RCPSS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+36436, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_RSQRTPS[] = {
    {I_RSQRTPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+36442, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_RSQRTSS[] = {
    {I_RSQRTSS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+36448, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_SHUFPS[] = {
    {I_SHUFPS, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+26818, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_SQRTPS[] = {
    {I_SQRTPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+36454, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_SQRTSS[] = {
    {I_SQRTSS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+36460, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_STMXCSR[] = {
    {I_STMXCSR, 1, {MEMORY|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+36466, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_SUBPS[] = {
    {I_SUBPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+36472, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_SUBSS[] = {
    {I_SUBSS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+36478, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_UCOMISS[] = {
    {I_UCOMISS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+36484, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_UNPCKHPS[] = {
    {I_UNPCKHPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+36490, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_UNPCKLPS[] = {
    {I_UNPCKLPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+36496, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_XORPS[] = {
    {I_XORPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+36502, 120},
    ITEMPLATE_END
};

static const struct itemplate instrux_FXRSTOR[] = {
    {I_FXRSTOR, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+26826, 128},
    ITEMPLATE_END
};

static const struct itemplate instrux_FXRSTOR64[] = {
    {I_FXRSTOR64, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+26825, 129},
    ITEMPLATE_END
};

static const struct itemplate instrux_FXSAVE[] = {
    {I_FXSAVE, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+26833, 128},
    ITEMPLATE_END
};

static const struct itemplate instrux_FXSAVE64[] = {
    {I_FXSAVE64, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+26832, 129},
    ITEMPLATE_END
};

static const struct itemplate instrux_XGETBV[] = {
    {I_XGETBV, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40406, 130},
    ITEMPLATE_END
};

static const struct itemplate instrux_XSETBV[] = {
    {I_XSETBV, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40411, 131},
    ITEMPLATE_END
};

static const struct itemplate instrux_XSAVE[] = {
    {I_XSAVE, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+26840, 130},
    ITEMPLATE_END
};

static const struct itemplate instrux_XSAVE64[] = {
    {I_XSAVE64, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+26839, 132},
    ITEMPLATE_END
};

static const struct itemplate instrux_XSAVEC[] = {
    {I_XSAVEC, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+26847, 133},
    ITEMPLATE_END
};

static const struct itemplate instrux_XSAVEC64[] = {
    {I_XSAVEC64, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+26846, 134},
    ITEMPLATE_END
};

static const struct itemplate instrux_XSAVEOPT[] = {
    {I_XSAVEOPT, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+26854, 133},
    ITEMPLATE_END
};

static const struct itemplate instrux_XSAVEOPT64[] = {
    {I_XSAVEOPT64, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+26853, 134},
    ITEMPLATE_END
};

static const struct itemplate instrux_XSAVES[] = {
    {I_XSAVES, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+26861, 133},
    ITEMPLATE_END
};

static const struct itemplate instrux_XSAVES64[] = {
    {I_XSAVES64, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+26860, 134},
    ITEMPLATE_END
};

static const struct itemplate instrux_XRSTOR[] = {
    {I_XRSTOR, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+26868, 130},
    ITEMPLATE_END
};

static const struct itemplate instrux_XRSTOR64[] = {
    {I_XRSTOR64, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+26867, 132},
    ITEMPLATE_END
};

static const struct itemplate instrux_XRSTORS[] = {
    {I_XRSTORS, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+26875, 133},
    ITEMPLATE_END
};

static const struct itemplate instrux_XRSTORS64[] = {
    {I_XRSTORS64, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+26874, 134},
    ITEMPLATE_END
};

static const struct itemplate instrux_PREFETCHNTA[] = {
    {I_PREFETCHNTA, 1, {MEMORY|BITS8,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37595, 135},
    ITEMPLATE_END
};

static const struct itemplate instrux_PREFETCHT0[] = {
    {I_PREFETCHT0, 1, {MEMORY|BITS8,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37613, 135},
    ITEMPLATE_END
};

static const struct itemplate instrux_PREFETCHT1[] = {
    {I_PREFETCHT1, 1, {MEMORY|BITS8,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37631, 135},
    ITEMPLATE_END
};

static const struct itemplate instrux_PREFETCHT2[] = {
    {I_PREFETCHT2, 1, {MEMORY|BITS8,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37649, 135},
    ITEMPLATE_END
};

static const struct itemplate instrux_MASKMOVQ[] = {
    {I_MASKMOVQ, 2, {MMXREG,MMXREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+36508, 136},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVNTQ[] = {
    {I_MOVNTQ, 2, {MEMORY,MMXREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+36514, 137},
    ITEMPLATE_END
};

static const struct itemplate instrux_PAVGB[] = {
    {I_PAVGB, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26881, 137},
    {I_PAVGB, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36682, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PAVGW[] = {
    {I_PAVGW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26888, 137},
    {I_PAVGW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36688, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PEXTRW[] = {
    {I_PEXTRW, 3, {REG_GPR|BITS32,MMXREG,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+26895, 138},
    {I_PEXTRW, 3, {REG_GPR|BITS32,XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+26986, 148},
    {I_PEXTRW, 3, {REG_GPR|BITS64,XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+26986, 149},
    {I_PEXTRW, 3, {REG_GPR|BITS32,XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+277, 169},
    {I_PEXTRW, 3, {MEMORY|BITS16,XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+277, 169},
    {I_PEXTRW, 3, {REG_GPR|BITS64,XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+276, 170},
    ITEMPLATE_END
};

static const struct itemplate instrux_PINSRW[] = {
    {I_PINSRW, 3, {MMXREG,MEMORY,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+26902, 138},
    {I_PINSRW, 3, {MMXREG,RM_GPR|BITS16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+26902, 138},
    {I_PINSRW, 3, {MMXREG,REG_GPR|BITS32,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+26902, 138},
    {I_PINSRW, 3, {XMM_L16,REG_GPR|BITS16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+26993, 148},
    {I_PINSRW, 3, {XMM_L16,REG_GPR|BITS32,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+26993, 148},
    {I_PINSRW, 3, {XMM_L16,REG_GPR|BITS64,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+26993, 149},
    {I_PINSRW, 3, {XMM_L16,MEMORY,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+26993, 148},
    {I_PINSRW, 3, {XMM_L16,MEMORY|BITS16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+26993, 148},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMAXSW[] = {
    {I_PMAXSW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26909, 137},
    {I_PMAXSW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36736, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMAXUB[] = {
    {I_PMAXUB, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26916, 137},
    {I_PMAXUB, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36742, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMINSW[] = {
    {I_PMINSW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26923, 137},
    {I_PMINSW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36748, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMINUB[] = {
    {I_PMINUB, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26930, 137},
    {I_PMINUB, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36754, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMOVMSKB[] = {
    {I_PMOVMSKB, 2, {REG_GPR|BITS32,MMXREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+36520, 136},
    {I_PMOVMSKB, 2, {REG_GPR|BITS32,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36760, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMULHUW[] = {
    {I_PMULHUW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26937, 137},
    {I_PMULHUW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36766, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSADBW[] = {
    {I_PSADBW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+26944, 137},
    {I_PSADBW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36796, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSHUFW[] = {
    {I_PSHUFW, 3, {MMXREG,RM_MMX,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+8692, 139},
    ITEMPLATE_END
};

static const struct itemplate instrux_PF2IW[] = {
    {I_PF2IW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+8700, 88},
    ITEMPLATE_END
};

static const struct itemplate instrux_PFNACC[] = {
    {I_PFNACC, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+8708, 88},
    ITEMPLATE_END
};

static const struct itemplate instrux_PFPNACC[] = {
    {I_PFPNACC, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+8716, 88},
    ITEMPLATE_END
};

static const struct itemplate instrux_PI2FW[] = {
    {I_PI2FW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+8724, 88},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSWAPD[] = {
    {I_PSWAPD, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+8732, 88},
    ITEMPLATE_END
};

static const struct itemplate instrux_MASKMOVDQU[] = {
    {I_MASKMOVDQU, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36526, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_CLFLUSH[] = {
    {I_CLFLUSH, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+36532, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVNTDQ[] = {
    {I_MOVNTDQ, 2, {MEMORY,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36538, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVNTI[] = {
    {I_MOVNTI, 2, {MEMORY,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+26952, 142},
    {I_MOVNTI, 2, {MEMORY,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+26951, 143},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVNTPD[] = {
    {I_MOVNTPD, 2, {MEMORY,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36544, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVDQA[] = {
    {I_MOVDQA, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36550, 140},
    {I_MOVDQA, 2, {MEMORY,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36556, 141},
    {I_MOVDQA, 2, {XMM_L16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+36550, 141},
    {I_MOVDQA, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36556, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVDQU[] = {
    {I_MOVDQU, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36562, 140},
    {I_MOVDQU, 2, {MEMORY,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36568, 141},
    {I_MOVDQU, 2, {XMM_L16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+36562, 141},
    {I_MOVDQU, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36568, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVDQ2Q[] = {
    {I_MOVDQ2Q, 2, {MMXREG,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36574, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVQ2DQ[] = {
    {I_MOVQ2DQ, 2, {XMM_L16,MMXREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+36592, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_PADDQ[] = {
    {I_PADDQ, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+36634, 147},
    {I_PADDQ, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36640, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMULUDQ[] = {
    {I_PMULUDQ, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+27000, 141},
    {I_PMULUDQ, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36784, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSHUFD[] = {
    {I_PSHUFD, 3, {XMM_L16,XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+27007, 148},
    {I_PSHUFD, 3, {XMM_L16,MEMORY,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+27007, 150},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSHUFHW[] = {
    {I_PSHUFHW, 3, {XMM_L16,XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+27014, 148},
    {I_PSHUFHW, 3, {XMM_L16,MEMORY,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+27014, 150},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSHUFLW[] = {
    {I_PSHUFLW, 3, {XMM_L16,XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+27021, 148},
    {I_PSHUFLW, 3, {XMM_L16,MEMORY,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+27021, 150},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSLLDQ[] = {
    {I_PSLLDQ, 2, {XMM_L16,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+27028, 151},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSRLDQ[] = {
    {I_PSRLDQ, 2, {XMM_L16,IMMEDIATE,0,0,0}, NO_DECORATOR, nasm_bytecodes+27070, 151},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSUBQ[] = {
    {I_PSUBQ, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+27098, 141},
    {I_PSUBQ, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36868, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PUNPCKHQDQ[] = {
    {I_PUNPCKHQDQ, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36916, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_PUNPCKLQDQ[] = {
    {I_PUNPCKLQDQ, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36940, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_ADDPD[] = {
    {I_ADDPD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36952, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_ADDSD[] = {
    {I_ADDSD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36958, 145},
    ITEMPLATE_END
};

static const struct itemplate instrux_ANDNPD[] = {
    {I_ANDNPD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36964, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_ANDPD[] = {
    {I_ANDPD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36970, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPEQPD[] = {
    {I_CMPEQPD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+8740, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPEQSD[] = {
    {I_CMPEQSD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+8748, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPLEPD[] = {
    {I_CMPLEPD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+8756, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPLESD[] = {
    {I_CMPLESD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+8764, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPLTPD[] = {
    {I_CMPLTPD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+8772, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPLTSD[] = {
    {I_CMPLTSD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+8780, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPNEQPD[] = {
    {I_CMPNEQPD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+8788, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPNEQSD[] = {
    {I_CMPNEQSD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+8796, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPNLEPD[] = {
    {I_CMPNLEPD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+8804, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPNLESD[] = {
    {I_CMPNLESD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+8812, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPNLTPD[] = {
    {I_CMPNLTPD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+8820, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPNLTSD[] = {
    {I_CMPNLTSD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+8828, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPORDPD[] = {
    {I_CMPORDPD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+8836, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPORDSD[] = {
    {I_CMPORDSD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+8844, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPUNORDPD[] = {
    {I_CMPUNORDPD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+8852, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPUNORDSD[] = {
    {I_CMPUNORDSD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+8860, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMPPD[] = {
    {I_CMPPD, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+27105, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_COMISD[] = {
    {I_COMISD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36976, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_CVTDQ2PD[] = {
    {I_CVTDQ2PD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36982, 145},
    ITEMPLATE_END
};

static const struct itemplate instrux_CVTDQ2PS[] = {
    {I_CVTDQ2PS, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36988, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_CVTPD2DQ[] = {
    {I_CVTPD2DQ, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+36994, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_CVTPD2PI[] = {
    {I_CVTPD2PI, 2, {MMXREG,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37000, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_CVTPD2PS[] = {
    {I_CVTPD2PS, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37006, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_CVTPI2PD[] = {
    {I_CVTPI2PD, 2, {XMM_L16,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+37012, 145},
    ITEMPLATE_END
};

static const struct itemplate instrux_CVTPS2DQ[] = {
    {I_CVTPS2DQ, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37018, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_CVTPS2PD[] = {
    {I_CVTPS2PD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37024, 145},
    ITEMPLATE_END
};

static const struct itemplate instrux_CVTSD2SI[] = {
    {I_CVTSD2SI, 2, {REG_GPR|BITS32,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27119, 152},
    {I_CVTSD2SI, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+27119, 152},
    {I_CVTSD2SI, 2, {REG_GPR|BITS64,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27126, 153},
    {I_CVTSD2SI, 2, {REG_GPR|BITS64,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+27126, 153},
    ITEMPLATE_END
};

static const struct itemplate instrux_CVTSD2SS[] = {
    {I_CVTSD2SS, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37030, 145},
    ITEMPLATE_END
};

static const struct itemplate instrux_CVTSI2SD[] = {
    {I_CVTSI2SD, 2, {XMM_L16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+27141, 154},
    {I_CVTSI2SD, 2, {XMM_L16,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+27133, 154},
    {I_CVTSI2SD, 2, {XMM_L16,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+27140, 153},
    ITEMPLATE_END
};

static const struct itemplate instrux_CVTSS2SD[] = {
    {I_CVTSS2SD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37036, 144},
    ITEMPLATE_END
};

static const struct itemplate instrux_CVTTPD2PI[] = {
    {I_CVTTPD2PI, 2, {MMXREG,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37042, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_CVTTPD2DQ[] = {
    {I_CVTTPD2DQ, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37048, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_CVTTPS2DQ[] = {
    {I_CVTTPS2DQ, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37054, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_CVTTSD2SI[] = {
    {I_CVTTSD2SI, 2, {REG_GPR|BITS32,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27147, 152},
    {I_CVTTSD2SI, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+27147, 152},
    {I_CVTTSD2SI, 2, {REG_GPR|BITS64,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27154, 153},
    {I_CVTTSD2SI, 2, {REG_GPR|BITS64,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+27154, 153},
    ITEMPLATE_END
};

static const struct itemplate instrux_DIVPD[] = {
    {I_DIVPD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37060, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_DIVSD[] = {
    {I_DIVSD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37066, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_MAXPD[] = {
    {I_MAXPD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37072, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_MAXSD[] = {
    {I_MAXSD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37078, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_MINPD[] = {
    {I_MINPD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37084, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_MINSD[] = {
    {I_MINSD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37090, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVAPD[] = {
    {I_MOVAPD, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37096, 140},
    {I_MOVAPD, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37102, 140},
    {I_MOVAPD, 2, {MEMORY,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37102, 141},
    {I_MOVAPD, 2, {XMM_L16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+37096, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVHPD[] = {
    {I_MOVHPD, 2, {MEMORY,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37108, 140},
    {I_MOVHPD, 2, {XMM_L16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+37114, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVLPD[] = {
    {I_MOVLPD, 2, {MEMORY|BITS64,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37120, 140},
    {I_MOVLPD, 2, {XMM_L16,MEMORY|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+37126, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVMSKPD[] = {
    {I_MOVMSKPD, 2, {REG_GPR|BITS32,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37132, 140},
    {I_MOVMSKPD, 2, {REG_GPR|BITS64,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27161, 146},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVUPD[] = {
    {I_MOVUPD, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37150, 140},
    {I_MOVUPD, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37156, 140},
    {I_MOVUPD, 2, {MEMORY,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37156, 141},
    {I_MOVUPD, 2, {XMM_L16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+37150, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_MULPD[] = {
    {I_MULPD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37162, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_MULSD[] = {
    {I_MULSD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37168, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_ORPD[] = {
    {I_ORPD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37174, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_SHUFPD[] = {
    {I_SHUFPD, 3, {XMM_L16,XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+27168, 148},
    {I_SHUFPD, 3, {XMM_L16,MEMORY,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+27168, 155},
    ITEMPLATE_END
};

static const struct itemplate instrux_SQRTPD[] = {
    {I_SQRTPD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37180, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_SQRTSD[] = {
    {I_SQRTSD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37186, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_SUBPD[] = {
    {I_SUBPD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37192, 141},
    ITEMPLATE_END
};

static const struct itemplate instrux_SUBSD[] = {
    {I_SUBSD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37198, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_UCOMISD[] = {
    {I_UCOMISD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37204, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_UNPCKHPD[] = {
    {I_UNPCKHPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+37210, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_UNPCKLPD[] = {
    {I_UNPCKLPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+37216, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_XORPD[] = {
    {I_XORPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+37222, 140},
    ITEMPLATE_END
};

static const struct itemplate instrux_ADDSUBPD[] = {
    {I_ADDSUBPD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37228, 156},
    ITEMPLATE_END
};

static const struct itemplate instrux_ADDSUBPS[] = {
    {I_ADDSUBPS, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37234, 156},
    ITEMPLATE_END
};

static const struct itemplate instrux_HADDPD[] = {
    {I_HADDPD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37240, 156},
    ITEMPLATE_END
};

static const struct itemplate instrux_HADDPS[] = {
    {I_HADDPS, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37246, 156},
    ITEMPLATE_END
};

static const struct itemplate instrux_HSUBPD[] = {
    {I_HSUBPD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37252, 156},
    ITEMPLATE_END
};

static const struct itemplate instrux_HSUBPS[] = {
    {I_HSUBPS, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37258, 156},
    ITEMPLATE_END
};

static const struct itemplate instrux_LDDQU[] = {
    {I_LDDQU, 2, {XMM_L16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+37264, 156},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVDDUP[] = {
    {I_MOVDDUP, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37270, 157},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVSHDUP[] = {
    {I_MOVSHDUP, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37276, 157},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVSLDUP[] = {
    {I_MOVSLDUP, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37282, 157},
    ITEMPLATE_END
};

static const struct itemplate instrux_CLGI[] = {
    {I_CLGI, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40416, 158},
    ITEMPLATE_END
};

static const struct itemplate instrux_STGI[] = {
    {I_STGI, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40421, 158},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMCALL[] = {
    {I_VMCALL, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40426, 159},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMCLEAR[] = {
    {I_VMCLEAR, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37288, 159},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMFUNC[] = {
    {I_VMFUNC, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40431, 159},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMLAUNCH[] = {
    {I_VMLAUNCH, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40436, 159},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMLOAD[] = {
    {I_VMLOAD, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40441, 158},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMMCALL[] = {
    {I_VMMCALL, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40446, 158},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMPTRLD[] = {
    {I_VMPTRLD, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37294, 159},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMPTRST[] = {
    {I_VMPTRST, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37300, 159},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMREAD[] = {
    {I_VMREAD, 2, {RM_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+27176, 160},
    {I_VMREAD, 2, {RM_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+27175, 161},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMRESUME[] = {
    {I_VMRESUME, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40451, 159},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMRUN[] = {
    {I_VMRUN, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40456, 158},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMSAVE[] = {
    {I_VMSAVE, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40461, 158},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMWRITE[] = {
    {I_VMWRITE, 2, {REG_GPR|BITS32,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+27183, 160},
    {I_VMWRITE, 2, {REG_GPR|BITS64,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+27182, 161},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMXOFF[] = {
    {I_VMXOFF, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40466, 159},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMXON[] = {
    {I_VMXON, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37306, 159},
    ITEMPLATE_END
};

static const struct itemplate instrux_INVEPT[] = {
    {I_INVEPT, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+8869, 162},
    {I_INVEPT, 2, {REG_GPR|BITS64,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+8868, 163},
    ITEMPLATE_END
};

static const struct itemplate instrux_INVVPID[] = {
    {I_INVVPID, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+8877, 162},
    {I_INVVPID, 2, {REG_GPR|BITS64,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+8876, 163},
    ITEMPLATE_END
};

static const struct itemplate instrux_PABSB[] = {
    {I_PABSB, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+27189, 164},
    {I_PABSB, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27196, 165},
    ITEMPLATE_END
};

static const struct itemplate instrux_PABSW[] = {
    {I_PABSW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+27203, 164},
    {I_PABSW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27210, 165},
    ITEMPLATE_END
};

static const struct itemplate instrux_PABSD[] = {
    {I_PABSD, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+27217, 164},
    {I_PABSD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27224, 165},
    ITEMPLATE_END
};

static const struct itemplate instrux_PALIGNR[] = {
    {I_PALIGNR, 3, {MMXREG,RM_MMX,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+8884, 164},
    {I_PALIGNR, 3, {XMM_L16,RM_XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+8892, 165},
    ITEMPLATE_END
};

static const struct itemplate instrux_PHADDW[] = {
    {I_PHADDW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+27231, 164},
    {I_PHADDW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27238, 165},
    ITEMPLATE_END
};

static const struct itemplate instrux_PHADDD[] = {
    {I_PHADDD, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+27245, 164},
    {I_PHADDD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27252, 165},
    ITEMPLATE_END
};

static const struct itemplate instrux_PHADDSW[] = {
    {I_PHADDSW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+27259, 164},
    {I_PHADDSW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27266, 165},
    ITEMPLATE_END
};

static const struct itemplate instrux_PHSUBW[] = {
    {I_PHSUBW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+27273, 164},
    {I_PHSUBW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27280, 165},
    ITEMPLATE_END
};

static const struct itemplate instrux_PHSUBD[] = {
    {I_PHSUBD, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+27287, 164},
    {I_PHSUBD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27294, 165},
    ITEMPLATE_END
};

static const struct itemplate instrux_PHSUBSW[] = {
    {I_PHSUBSW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+27301, 164},
    {I_PHSUBSW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27308, 165},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMADDUBSW[] = {
    {I_PMADDUBSW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+27315, 164},
    {I_PMADDUBSW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27322, 165},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMULHRSW[] = {
    {I_PMULHRSW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+27329, 164},
    {I_PMULHRSW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27336, 165},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSHUFB[] = {
    {I_PSHUFB, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+27343, 164},
    {I_PSHUFB, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27350, 165},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSIGNB[] = {
    {I_PSIGNB, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+27357, 164},
    {I_PSIGNB, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27364, 165},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSIGNW[] = {
    {I_PSIGNW, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+27371, 164},
    {I_PSIGNW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27378, 165},
    ITEMPLATE_END
};

static const struct itemplate instrux_PSIGND[] = {
    {I_PSIGND, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+27385, 164},
    {I_PSIGND, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27392, 165},
    ITEMPLATE_END
};

static const struct itemplate instrux_EXTRQ[] = {
    {I_EXTRQ, 3, {XMM_L16,IMMEDIATE,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+8900, 166},
    {I_EXTRQ, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37312, 166},
    ITEMPLATE_END
};

static const struct itemplate instrux_INSERTQ[] = {
    {I_INSERTQ, 4, {XMM_L16,XMM_L16,IMMEDIATE,IMMEDIATE,0}, NO_DECORATOR, nasm_bytecodes+8908, 166},
    {I_INSERTQ, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37318, 166},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVNTSD[] = {
    {I_MOVNTSD, 2, {MEMORY,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37324, 167},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVNTSS[] = {
    {I_MOVNTSS, 2, {MEMORY,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+37330, 168},
    ITEMPLATE_END
};

static const struct itemplate instrux_LZCNT[] = {
    {I_LZCNT, 2, {REG_GPR|BITS16,RM_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27399, 110},
    {I_LZCNT, 2, {REG_GPR|BITS32,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+27406, 110},
    {I_LZCNT, 2, {REG_GPR|BITS64,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+27413, 59},
    ITEMPLATE_END
};

static const struct itemplate instrux_BLENDPD[] = {
    {I_BLENDPD, 3, {XMM_L16,RM_XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+8916, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_BLENDPS[] = {
    {I_BLENDPS, 3, {XMM_L16,RM_XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+8924, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_BLENDVPD[] = {
    {I_BLENDVPD, 3, {XMM_L16,RM_XMM_L16,XMM0,0,0}, NO_DECORATOR, nasm_bytecodes+27420, 169},
    {I_BLENDVPD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27420, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_BLENDVPS[] = {
    {I_BLENDVPS, 3, {XMM_L16,RM_XMM_L16,XMM0,0,0}, NO_DECORATOR, nasm_bytecodes+27427, 169},
    {I_BLENDVPS, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27427, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_DPPD[] = {
    {I_DPPD, 3, {XMM_L16,RM_XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+8932, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_DPPS[] = {
    {I_DPPS, 3, {XMM_L16,RM_XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+8940, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_EXTRACTPS[] = {
    {I_EXTRACTPS, 3, {RM_GPR|BITS32,XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+241, 169},
    {I_EXTRACTPS, 3, {REG_GPR|BITS64,XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+240, 170},
    ITEMPLATE_END
};

static const struct itemplate instrux_INSERTPS[] = {
    {I_INSERTPS, 3, {XMM_L16,RM_XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+8948, 171},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVNTDQA[] = {
    {I_MOVNTDQA, 2, {XMM_L16,MEMORY|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+27434, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_MPSADBW[] = {
    {I_MPSADBW, 3, {XMM_L16,RM_XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+8956, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_PACKUSDW[] = {
    {I_PACKUSDW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27441, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_PBLENDVB[] = {
    {I_PBLENDVB, 3, {XMM_L16,RM_XMM_L16,XMM0,0,0}, NO_DECORATOR, nasm_bytecodes+27448, 169},
    {I_PBLENDVB, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27448, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_PBLENDW[] = {
    {I_PBLENDW, 3, {XMM_L16,RM_XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+8964, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_PCMPEQQ[] = {
    {I_PCMPEQQ, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27455, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_PEXTRB[] = {
    {I_PEXTRB, 3, {REG_GPR|BITS32,XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+250, 169},
    {I_PEXTRB, 3, {MEMORY|BITS8,XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+250, 169},
    {I_PEXTRB, 3, {REG_GPR|BITS64,XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+249, 170},
    ITEMPLATE_END
};

static const struct itemplate instrux_PEXTRD[] = {
    {I_PEXTRD, 3, {RM_GPR|BITS32,XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+258, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_PEXTRQ[] = {
    {I_PEXTRQ, 3, {RM_GPR|BITS64,XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+267, 170},
    ITEMPLATE_END
};

static const struct itemplate instrux_PHMINPOSUW[] = {
    {I_PHMINPOSUW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27462, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_PINSRB[] = {
    {I_PINSRB, 3, {XMM_L16,MEMORY,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+286, 172},
    {I_PINSRB, 3, {XMM_L16,RM_GPR|BITS8,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+285, 172},
    {I_PINSRB, 3, {XMM_L16,REG_GPR|BITS32,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+286, 172},
    ITEMPLATE_END
};

static const struct itemplate instrux_PINSRD[] = {
    {I_PINSRD, 3, {XMM_L16,MEMORY,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+294, 172},
    {I_PINSRD, 3, {XMM_L16,RM_GPR|BITS32,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+294, 172},
    ITEMPLATE_END
};

static const struct itemplate instrux_PINSRQ[] = {
    {I_PINSRQ, 3, {XMM_L16,MEMORY,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+303, 173},
    {I_PINSRQ, 3, {XMM_L16,RM_GPR|BITS64,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+303, 173},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMAXSB[] = {
    {I_PMAXSB, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27469, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMAXSD[] = {
    {I_PMAXSD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27476, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMAXUD[] = {
    {I_PMAXUD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27483, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMAXUW[] = {
    {I_PMAXUW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27490, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMINSB[] = {
    {I_PMINSB, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27497, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMINSD[] = {
    {I_PMINSD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27504, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMINUD[] = {
    {I_PMINUD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27511, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMINUW[] = {
    {I_PMINUW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27518, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMOVSXBW[] = {
    {I_PMOVSXBW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27525, 174},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMOVSXBD[] = {
    {I_PMOVSXBD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27532, 171},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMOVSXBQ[] = {
    {I_PMOVSXBQ, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27539, 175},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMOVSXWD[] = {
    {I_PMOVSXWD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27546, 174},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMOVSXWQ[] = {
    {I_PMOVSXWQ, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27553, 171},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMOVSXDQ[] = {
    {I_PMOVSXDQ, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27560, 174},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMOVZXBW[] = {
    {I_PMOVZXBW, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27567, 174},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMOVZXBD[] = {
    {I_PMOVZXBD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27574, 171},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMOVZXBQ[] = {
    {I_PMOVZXBQ, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27581, 175},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMOVZXWD[] = {
    {I_PMOVZXWD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27588, 174},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMOVZXWQ[] = {
    {I_PMOVZXWQ, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27595, 171},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMOVZXDQ[] = {
    {I_PMOVZXDQ, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27602, 174},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMULDQ[] = {
    {I_PMULDQ, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27609, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_PMULLD[] = {
    {I_PMULLD, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27616, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_PTEST[] = {
    {I_PTEST, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27623, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_ROUNDPD[] = {
    {I_ROUNDPD, 3, {XMM_L16,RM_XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+8972, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_ROUNDPS[] = {
    {I_ROUNDPS, 3, {XMM_L16,RM_XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+8980, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_ROUNDSD[] = {
    {I_ROUNDSD, 3, {XMM_L16,RM_XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+8988, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_ROUNDSS[] = {
    {I_ROUNDSS, 3, {XMM_L16,RM_XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+8996, 169},
    ITEMPLATE_END
};

static const struct itemplate instrux_CRC32[] = {
    {I_CRC32, 2, {REG_GPR|BITS32,RM_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+9021, 176},
    {I_CRC32, 2, {REG_GPR|BITS32,RM_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+9004, 176},
    {I_CRC32, 2, {REG_GPR|BITS32,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+9012, 176},
    {I_CRC32, 2, {REG_GPR|BITS64,RM_GPR|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+9020, 177},
    {I_CRC32, 2, {REG_GPR|BITS64,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+9028, 177},
    ITEMPLATE_END
};

static const struct itemplate instrux_PCMPESTRI[] = {
    {I_PCMPESTRI, 3, {XMM_L16,RM_XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+9036, 176},
    ITEMPLATE_END
};

static const struct itemplate instrux_PCMPESTRM[] = {
    {I_PCMPESTRM, 3, {XMM_L16,RM_XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+9044, 176},
    ITEMPLATE_END
};

static const struct itemplate instrux_PCMPISTRI[] = {
    {I_PCMPISTRI, 3, {XMM_L16,RM_XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+9052, 176},
    ITEMPLATE_END
};

static const struct itemplate instrux_PCMPISTRM[] = {
    {I_PCMPISTRM, 3, {XMM_L16,RM_XMM_L16,IMMEDIATE,0,0}, NO_DECORATOR, nasm_bytecodes+9060, 176},
    ITEMPLATE_END
};

static const struct itemplate instrux_PCMPGTQ[] = {
    {I_PCMPGTQ, 2, {XMM_L16,RM_XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27630, 176},
    ITEMPLATE_END
};

static const struct itemplate instrux_POPCNT[] = {
    {I_POPCNT, 2, {REG_GPR|BITS16,RM_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+27637, 178},
    {I_POPCNT, 2, {REG_GPR|BITS32,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+27644, 179},
    {I_POPCNT, 2, {REG_GPR|BITS64,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+27651, 180},
    ITEMPLATE_END
};

static const struct itemplate instrux_GETSEC[] = {
    {I_GETSEC, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+41522, 135},
    ITEMPLATE_END
};

static const struct itemplate instrux_PFRCPV[] = {
    {I_PFRCPV, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+9068, 181},
    ITEMPLATE_END
};

static const struct itemplate instrux_PFRSQRTV[] = {
    {I_PFRSQRTV, 2, {MMXREG,RM_MMX,0,0,0}, NO_DECORATOR, nasm_bytecodes+9076, 181},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVBE[] = {
    {I_MOVBE, 2, {REG_GPR|BITS16,MEMORY|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+9084, 182},
    {I_MOVBE, 2, {REG_GPR|BITS32,MEMORY|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+9092, 182},
    {I_MOVBE, 2, {REG_GPR|BITS64,MEMORY|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+9100, 182},
    {I_MOVBE, 2, {MEMORY|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+9108, 182},
    {I_MOVBE, 2, {MEMORY|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+9116, 182},
    {I_MOVBE, 2, {MEMORY|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+9124, 182},
    ITEMPLATE_END
};

static const struct itemplate instrux_AESENC[] = {
    {I_AESENC, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+27658, 183},
    ITEMPLATE_END
};

static const struct itemplate instrux_AESENCLAST[] = {
    {I_AESENCLAST, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+27665, 183},
    ITEMPLATE_END
};

static const struct itemplate instrux_AESDEC[] = {
    {I_AESDEC, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+27672, 183},
    ITEMPLATE_END
};

static const struct itemplate instrux_AESDECLAST[] = {
    {I_AESDECLAST, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+27679, 183},
    ITEMPLATE_END
};

static const struct itemplate instrux_AESIMC[] = {
    {I_AESIMC, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+27686, 183},
    ITEMPLATE_END
};

static const struct itemplate instrux_AESKEYGENASSIST[] = {
    {I_AESKEYGENASSIST, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9132, 183},
    ITEMPLATE_END
};

static const struct itemplate instrux_VAESENC[] = {
    {I_VAESENC, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+27693, 184},
    {I_VAESENC, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+27700, 184},
    {I_VAESENC, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+27756, 185},
    {I_VAESENC, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+27763, 185},
    {I_VAESENC, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+9148, 186},
    {I_VAESENC, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+9156, 186},
    {I_VAESENC, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+9164, 186},
    {I_VAESENC, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+9172, 186},
    {I_VAESENC, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, NO_DECORATOR, nasm_bytecodes+9276, 187},
    {I_VAESENC, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, NO_DECORATOR, nasm_bytecodes+9284, 187},
    ITEMPLATE_END
};

static const struct itemplate instrux_VAESENCLAST[] = {
    {I_VAESENCLAST, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+27707, 184},
    {I_VAESENCLAST, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+27714, 184},
    {I_VAESENCLAST, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+27770, 185},
    {I_VAESENCLAST, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+27777, 185},
    {I_VAESENCLAST, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+9180, 186},
    {I_VAESENCLAST, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+9188, 186},
    {I_VAESENCLAST, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+9196, 186},
    {I_VAESENCLAST, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+9204, 186},
    {I_VAESENCLAST, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, NO_DECORATOR, nasm_bytecodes+9292, 187},
    {I_VAESENCLAST, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, NO_DECORATOR, nasm_bytecodes+9300, 187},
    ITEMPLATE_END
};

static const struct itemplate instrux_VAESDEC[] = {
    {I_VAESDEC, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+27721, 184},
    {I_VAESDEC, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+27728, 184},
    {I_VAESDEC, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+27784, 185},
    {I_VAESDEC, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+27791, 185},
    {I_VAESDEC, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+9212, 186},
    {I_VAESDEC, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+9220, 186},
    {I_VAESDEC, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+9228, 186},
    {I_VAESDEC, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+9236, 186},
    {I_VAESDEC, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, NO_DECORATOR, nasm_bytecodes+9308, 187},
    {I_VAESDEC, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, NO_DECORATOR, nasm_bytecodes+9316, 187},
    ITEMPLATE_END
};

static const struct itemplate instrux_VAESDECLAST[] = {
    {I_VAESDECLAST, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+27735, 184},
    {I_VAESDECLAST, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+27742, 184},
    {I_VAESDECLAST, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+27798, 185},
    {I_VAESDECLAST, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+27805, 185},
    {I_VAESDECLAST, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+9244, 186},
    {I_VAESDECLAST, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+9252, 186},
    {I_VAESDECLAST, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+9260, 186},
    {I_VAESDECLAST, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+9268, 186},
    {I_VAESDECLAST, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, NO_DECORATOR, nasm_bytecodes+9324, 187},
    {I_VAESDECLAST, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, NO_DECORATOR, nasm_bytecodes+9332, 187},
    ITEMPLATE_END
};

static const struct itemplate instrux_VAESIMC[] = {
    {I_VAESIMC, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+27749, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VAESKEYGENASSIST[] = {
    {I_VAESKEYGENASSIST, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9140, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VADDPD[] = {
    {I_VADDPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+27812, 184},
    {I_VADDPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+27819, 184},
    {I_VADDPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+27826, 184},
    {I_VADDPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+27833, 184},
    {I_VADDPD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+12404, 225},
    {I_VADDPD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+12412, 225},
    {I_VADDPD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+12420, 225},
    {I_VADDPD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+12428, 225},
    {I_VADDPD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64|ER,0,0}, nasm_bytecodes+12436, 226},
    {I_VADDPD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64|ER,0,0,0}, nasm_bytecodes+12444, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VADDPS[] = {
    {I_VADDPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+27840, 184},
    {I_VADDPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+27847, 184},
    {I_VADDPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+27854, 184},
    {I_VADDPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+27861, 184},
    {I_VADDPS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+12452, 225},
    {I_VADDPS, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+12460, 225},
    {I_VADDPS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+12468, 225},
    {I_VADDPS, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+12476, 225},
    {I_VADDPS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32|ER,0,0}, nasm_bytecodes+12484, 226},
    {I_VADDPS, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32|ER,0,0,0}, nasm_bytecodes+12492, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VADDSD[] = {
    {I_VADDSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+27868, 184},
    {I_VADDSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+27875, 184},
    {I_VADDSD, 3, {XMMREG,XMMREG,RM_XMM|BITS64,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+12500, 226},
    {I_VADDSD, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,ER,0,0,0}, nasm_bytecodes+12508, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VADDSS[] = {
    {I_VADDSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+27882, 184},
    {I_VADDSS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+27889, 184},
    {I_VADDSS, 3, {XMMREG,XMMREG,RM_XMM|BITS32,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+12516, 226},
    {I_VADDSS, 2, {XMMREG,RM_XMM|BITS32,0,0,0}, {MASK|Z,ER,0,0,0}, nasm_bytecodes+12524, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VADDSUBPD[] = {
    {I_VADDSUBPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+27896, 184},
    {I_VADDSUBPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+27903, 184},
    {I_VADDSUBPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+27910, 184},
    {I_VADDSUBPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+27917, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VADDSUBPS[] = {
    {I_VADDSUBPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+27924, 184},
    {I_VADDSUBPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+27931, 184},
    {I_VADDSUBPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+27938, 184},
    {I_VADDSUBPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+27945, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VANDPD[] = {
    {I_VANDPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+27952, 184},
    {I_VANDPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+27959, 184},
    {I_VANDPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+27966, 184},
    {I_VANDPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+27973, 184},
    {I_VANDPD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+12628, 227},
    {I_VANDPD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+12636, 227},
    {I_VANDPD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+12644, 227},
    {I_VANDPD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+12652, 227},
    {I_VANDPD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+12660, 228},
    {I_VANDPD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+12668, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VANDPS[] = {
    {I_VANDPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+27980, 184},
    {I_VANDPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+27987, 184},
    {I_VANDPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+27994, 184},
    {I_VANDPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+28001, 184},
    {I_VANDPS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+12676, 227},
    {I_VANDPS, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+12684, 227},
    {I_VANDPS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+12692, 227},
    {I_VANDPS, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+12700, 227},
    {I_VANDPS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+12708, 228},
    {I_VANDPS, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+12716, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VANDNPD[] = {
    {I_VANDNPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+28008, 184},
    {I_VANDNPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+28015, 184},
    {I_VANDNPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+28022, 184},
    {I_VANDNPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+28029, 184},
    {I_VANDNPD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+12532, 227},
    {I_VANDNPD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+12540, 227},
    {I_VANDNPD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+12548, 227},
    {I_VANDNPD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+12556, 227},
    {I_VANDNPD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+12564, 228},
    {I_VANDNPD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+12572, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VANDNPS[] = {
    {I_VANDNPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+28036, 184},
    {I_VANDNPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+28043, 184},
    {I_VANDNPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+28050, 184},
    {I_VANDNPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+28057, 184},
    {I_VANDNPS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+12580, 227},
    {I_VANDNPS, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+12588, 227},
    {I_VANDNPS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+12596, 227},
    {I_VANDNPS, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+12604, 227},
    {I_VANDNPS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+12612, 228},
    {I_VANDNPS, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+12620, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VBLENDPD[] = {
    {I_VBLENDPD, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9340, 184},
    {I_VBLENDPD, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9348, 184},
    {I_VBLENDPD, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9356, 184},
    {I_VBLENDPD, 3, {YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9364, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VBLENDPS[] = {
    {I_VBLENDPS, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9372, 184},
    {I_VBLENDPS, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9380, 184},
    {I_VBLENDPS, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9388, 184},
    {I_VBLENDPS, 3, {YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9396, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VBLENDVPD[] = {
    {I_VBLENDVPD, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+9404, 184},
    {I_VBLENDVPD, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+9412, 184},
    {I_VBLENDVPD, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0}, NO_DECORATOR, nasm_bytecodes+9420, 184},
    {I_VBLENDVPD, 3, {YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+9428, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VBLENDVPS[] = {
    {I_VBLENDVPS, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+9436, 184},
    {I_VBLENDVPS, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+9444, 184},
    {I_VBLENDVPS, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0}, NO_DECORATOR, nasm_bytecodes+9452, 184},
    {I_VBLENDVPS, 3, {YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+9460, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VBROADCASTSS[] = {
    {I_VBROADCASTSS, 2, {XMM_L16,MEMORY|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+28064, 184},
    {I_VBROADCASTSS, 2, {YMM_L16,MEMORY|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+28071, 184},
    {I_VBROADCASTSS, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+28064, 203},
    {I_VBROADCASTSS, 2, {YMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+28071, 203},
    {I_VBROADCASTSS, 2, {XMMREG,MEMORY|BITS32,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+12940, 225},
    {I_VBROADCASTSS, 2, {YMMREG,MEMORY|BITS32,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+12948, 225},
    {I_VBROADCASTSS, 2, {ZMMREG,MEMORY|BITS32,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+12956, 226},
    {I_VBROADCASTSS, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+12964, 225},
    {I_VBROADCASTSS, 2, {YMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+12972, 225},
    {I_VBROADCASTSS, 2, {ZMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+12980, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VBROADCASTSD[] = {
    {I_VBROADCASTSD, 2, {YMM_L16,MEMORY|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+28078, 184},
    {I_VBROADCASTSD, 2, {YMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+28078, 203},
    {I_VBROADCASTSD, 2, {YMMREG,MEMORY|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+12908, 225},
    {I_VBROADCASTSD, 2, {ZMMREG,MEMORY|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+12916, 226},
    {I_VBROADCASTSD, 2, {YMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+12924, 225},
    {I_VBROADCASTSD, 2, {ZMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+12932, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VBROADCASTF128[] = {
    {I_VBROADCASTF128, 2, {YMM_L16,MEMORY|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+28085, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPEQ_OSPD[] = {
    {I_VCMPEQ_OSPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+312, 184},
    {I_VCMPEQ_OSPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+321, 184},
    {I_VCMPEQ_OSPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+330, 184},
    {I_VCMPEQ_OSPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+339, 184},
    {I_VCMPEQ_OSPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+312, 184},
    {I_VCMPEQ_OSPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+321, 184},
    {I_VCMPEQ_OSPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+330, 184},
    {I_VCMPEQ_OSPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+339, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPEQPD[] = {
    {I_VCMPEQPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+348, 184},
    {I_VCMPEQPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+357, 184},
    {I_VCMPEQPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+366, 184},
    {I_VCMPEQPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+375, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPLT_OSPD[] = {
    {I_VCMPLT_OSPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+384, 184},
    {I_VCMPLT_OSPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+393, 184},
    {I_VCMPLT_OSPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+402, 184},
    {I_VCMPLT_OSPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+411, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPLTPD[] = {
    {I_VCMPLTPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+384, 184},
    {I_VCMPLTPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+393, 184},
    {I_VCMPLTPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+402, 184},
    {I_VCMPLTPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+411, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPLE_OSPD[] = {
    {I_VCMPLE_OSPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+420, 184},
    {I_VCMPLE_OSPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+429, 184},
    {I_VCMPLE_OSPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+438, 184},
    {I_VCMPLE_OSPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+447, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPLEPD[] = {
    {I_VCMPLEPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+420, 184},
    {I_VCMPLEPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+429, 184},
    {I_VCMPLEPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+438, 184},
    {I_VCMPLEPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+447, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPUNORD_QPD[] = {
    {I_VCMPUNORD_QPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+456, 184},
    {I_VCMPUNORD_QPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+465, 184},
    {I_VCMPUNORD_QPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+474, 184},
    {I_VCMPUNORD_QPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+483, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPUNORDPD[] = {
    {I_VCMPUNORDPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+456, 184},
    {I_VCMPUNORDPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+465, 184},
    {I_VCMPUNORDPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+474, 184},
    {I_VCMPUNORDPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+483, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNEQ_UQPD[] = {
    {I_VCMPNEQ_UQPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+492, 184},
    {I_VCMPNEQ_UQPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+501, 184},
    {I_VCMPNEQ_UQPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+510, 184},
    {I_VCMPNEQ_UQPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+519, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNEQPD[] = {
    {I_VCMPNEQPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+492, 184},
    {I_VCMPNEQPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+501, 184},
    {I_VCMPNEQPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+510, 184},
    {I_VCMPNEQPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+519, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNLT_USPD[] = {
    {I_VCMPNLT_USPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+528, 184},
    {I_VCMPNLT_USPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+537, 184},
    {I_VCMPNLT_USPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+546, 184},
    {I_VCMPNLT_USPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+555, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNLTPD[] = {
    {I_VCMPNLTPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+528, 184},
    {I_VCMPNLTPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+537, 184},
    {I_VCMPNLTPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+546, 184},
    {I_VCMPNLTPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+555, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNLE_USPD[] = {
    {I_VCMPNLE_USPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+564, 184},
    {I_VCMPNLE_USPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+573, 184},
    {I_VCMPNLE_USPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+582, 184},
    {I_VCMPNLE_USPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+591, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNLEPD[] = {
    {I_VCMPNLEPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+564, 184},
    {I_VCMPNLEPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+573, 184},
    {I_VCMPNLEPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+582, 184},
    {I_VCMPNLEPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+591, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPORD_QPD[] = {
    {I_VCMPORD_QPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+600, 184},
    {I_VCMPORD_QPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+609, 184},
    {I_VCMPORD_QPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+618, 184},
    {I_VCMPORD_QPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+627, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPORDPD[] = {
    {I_VCMPORDPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+600, 184},
    {I_VCMPORDPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+609, 184},
    {I_VCMPORDPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+618, 184},
    {I_VCMPORDPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+627, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPEQ_UQPD[] = {
    {I_VCMPEQ_UQPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+636, 184},
    {I_VCMPEQ_UQPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+645, 184},
    {I_VCMPEQ_UQPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+654, 184},
    {I_VCMPEQ_UQPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+663, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNGE_USPD[] = {
    {I_VCMPNGE_USPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+672, 184},
    {I_VCMPNGE_USPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+681, 184},
    {I_VCMPNGE_USPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+690, 184},
    {I_VCMPNGE_USPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+699, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNGEPD[] = {
    {I_VCMPNGEPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+672, 184},
    {I_VCMPNGEPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+681, 184},
    {I_VCMPNGEPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+690, 184},
    {I_VCMPNGEPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+699, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNGT_USPD[] = {
    {I_VCMPNGT_USPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+708, 184},
    {I_VCMPNGT_USPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+717, 184},
    {I_VCMPNGT_USPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+726, 184},
    {I_VCMPNGT_USPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+735, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNGTPD[] = {
    {I_VCMPNGTPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+708, 184},
    {I_VCMPNGTPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+717, 184},
    {I_VCMPNGTPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+726, 184},
    {I_VCMPNGTPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+735, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPFALSE_OQPD[] = {
    {I_VCMPFALSE_OQPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+744, 184},
    {I_VCMPFALSE_OQPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+753, 184},
    {I_VCMPFALSE_OQPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+762, 184},
    {I_VCMPFALSE_OQPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+771, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPFALSEPD[] = {
    {I_VCMPFALSEPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+744, 184},
    {I_VCMPFALSEPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+753, 184},
    {I_VCMPFALSEPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+762, 184},
    {I_VCMPFALSEPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+771, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNEQ_OQPD[] = {
    {I_VCMPNEQ_OQPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+780, 184},
    {I_VCMPNEQ_OQPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+789, 184},
    {I_VCMPNEQ_OQPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+798, 184},
    {I_VCMPNEQ_OQPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+807, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPGE_OSPD[] = {
    {I_VCMPGE_OSPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+816, 184},
    {I_VCMPGE_OSPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+825, 184},
    {I_VCMPGE_OSPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+834, 184},
    {I_VCMPGE_OSPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+843, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPGEPD[] = {
    {I_VCMPGEPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+816, 184},
    {I_VCMPGEPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+825, 184},
    {I_VCMPGEPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+834, 184},
    {I_VCMPGEPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+843, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPGT_OSPD[] = {
    {I_VCMPGT_OSPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+852, 184},
    {I_VCMPGT_OSPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+861, 184},
    {I_VCMPGT_OSPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+870, 184},
    {I_VCMPGT_OSPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+879, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPGTPD[] = {
    {I_VCMPGTPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+852, 184},
    {I_VCMPGTPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+861, 184},
    {I_VCMPGTPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+870, 184},
    {I_VCMPGTPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+879, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPTRUE_UQPD[] = {
    {I_VCMPTRUE_UQPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+888, 184},
    {I_VCMPTRUE_UQPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+897, 184},
    {I_VCMPTRUE_UQPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+906, 184},
    {I_VCMPTRUE_UQPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+915, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPTRUEPD[] = {
    {I_VCMPTRUEPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+888, 184},
    {I_VCMPTRUEPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+897, 184},
    {I_VCMPTRUEPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+906, 184},
    {I_VCMPTRUEPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+915, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPLT_OQPD[] = {
    {I_VCMPLT_OQPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+924, 184},
    {I_VCMPLT_OQPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+933, 184},
    {I_VCMPLT_OQPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+942, 184},
    {I_VCMPLT_OQPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+951, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPLE_OQPD[] = {
    {I_VCMPLE_OQPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+960, 184},
    {I_VCMPLE_OQPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+969, 184},
    {I_VCMPLE_OQPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+978, 184},
    {I_VCMPLE_OQPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+987, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPUNORD_SPD[] = {
    {I_VCMPUNORD_SPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+996, 184},
    {I_VCMPUNORD_SPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1005, 184},
    {I_VCMPUNORD_SPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1014, 184},
    {I_VCMPUNORD_SPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1023, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNEQ_USPD[] = {
    {I_VCMPNEQ_USPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1032, 184},
    {I_VCMPNEQ_USPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1041, 184},
    {I_VCMPNEQ_USPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1050, 184},
    {I_VCMPNEQ_USPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1059, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNLT_UQPD[] = {
    {I_VCMPNLT_UQPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1068, 184},
    {I_VCMPNLT_UQPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1077, 184},
    {I_VCMPNLT_UQPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1086, 184},
    {I_VCMPNLT_UQPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1095, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNLE_UQPD[] = {
    {I_VCMPNLE_UQPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1104, 184},
    {I_VCMPNLE_UQPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1113, 184},
    {I_VCMPNLE_UQPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1122, 184},
    {I_VCMPNLE_UQPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1131, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPORD_SPD[] = {
    {I_VCMPORD_SPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1140, 184},
    {I_VCMPORD_SPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1149, 184},
    {I_VCMPORD_SPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1158, 184},
    {I_VCMPORD_SPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1167, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPEQ_USPD[] = {
    {I_VCMPEQ_USPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1176, 184},
    {I_VCMPEQ_USPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1185, 184},
    {I_VCMPEQ_USPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1194, 184},
    {I_VCMPEQ_USPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1203, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNGE_UQPD[] = {
    {I_VCMPNGE_UQPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1212, 184},
    {I_VCMPNGE_UQPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1221, 184},
    {I_VCMPNGE_UQPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1230, 184},
    {I_VCMPNGE_UQPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1239, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNGT_UQPD[] = {
    {I_VCMPNGT_UQPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1248, 184},
    {I_VCMPNGT_UQPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1257, 184},
    {I_VCMPNGT_UQPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1266, 184},
    {I_VCMPNGT_UQPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1275, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPFALSE_OSPD[] = {
    {I_VCMPFALSE_OSPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1284, 184},
    {I_VCMPFALSE_OSPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1293, 184},
    {I_VCMPFALSE_OSPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1302, 184},
    {I_VCMPFALSE_OSPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1311, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNEQ_OSPD[] = {
    {I_VCMPNEQ_OSPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1320, 184},
    {I_VCMPNEQ_OSPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1329, 184},
    {I_VCMPNEQ_OSPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1338, 184},
    {I_VCMPNEQ_OSPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1347, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPGE_OQPD[] = {
    {I_VCMPGE_OQPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1356, 184},
    {I_VCMPGE_OQPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1365, 184},
    {I_VCMPGE_OQPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1374, 184},
    {I_VCMPGE_OQPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1383, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPGT_OQPD[] = {
    {I_VCMPGT_OQPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1392, 184},
    {I_VCMPGT_OQPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1401, 184},
    {I_VCMPGT_OQPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1410, 184},
    {I_VCMPGT_OQPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1419, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPTRUE_USPD[] = {
    {I_VCMPTRUE_USPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1428, 184},
    {I_VCMPTRUE_USPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1437, 184},
    {I_VCMPTRUE_USPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1446, 184},
    {I_VCMPTRUE_USPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1455, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPPD[] = {
    {I_VCMPPD, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9468, 184},
    {I_VCMPPD, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9476, 184},
    {I_VCMPPD, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9484, 184},
    {I_VCMPPD, 3, {YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9492, 184},
    {I_VCMPPD, 4, {KREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK,0,B64,0,0}, nasm_bytecodes+4110, 225},
    {I_VCMPPD, 4, {KREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK,0,B64,0,0}, nasm_bytecodes+4119, 225},
    {I_VCMPPD, 4, {KREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK,0,B64|SAE,0,0}, nasm_bytecodes+4128, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPEQ_OSPS[] = {
    {I_VCMPEQ_OSPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1464, 184},
    {I_VCMPEQ_OSPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1473, 184},
    {I_VCMPEQ_OSPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1482, 184},
    {I_VCMPEQ_OSPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1491, 184},
    {I_VCMPEQ_OSPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1464, 184},
    {I_VCMPEQ_OSPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1473, 184},
    {I_VCMPEQ_OSPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1482, 184},
    {I_VCMPEQ_OSPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1491, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPEQPS[] = {
    {I_VCMPEQPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1500, 184},
    {I_VCMPEQPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1509, 184},
    {I_VCMPEQPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1518, 184},
    {I_VCMPEQPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1527, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPLT_OSPS[] = {
    {I_VCMPLT_OSPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1536, 184},
    {I_VCMPLT_OSPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1545, 184},
    {I_VCMPLT_OSPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1554, 184},
    {I_VCMPLT_OSPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1563, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPLTPS[] = {
    {I_VCMPLTPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1536, 184},
    {I_VCMPLTPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1545, 184},
    {I_VCMPLTPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1554, 184},
    {I_VCMPLTPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1563, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPLE_OSPS[] = {
    {I_VCMPLE_OSPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1572, 184},
    {I_VCMPLE_OSPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1581, 184},
    {I_VCMPLE_OSPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1590, 184},
    {I_VCMPLE_OSPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1599, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPLEPS[] = {
    {I_VCMPLEPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1572, 184},
    {I_VCMPLEPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1581, 184},
    {I_VCMPLEPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1590, 184},
    {I_VCMPLEPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1599, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPUNORD_QPS[] = {
    {I_VCMPUNORD_QPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1608, 184},
    {I_VCMPUNORD_QPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1617, 184},
    {I_VCMPUNORD_QPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1626, 184},
    {I_VCMPUNORD_QPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1635, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPUNORDPS[] = {
    {I_VCMPUNORDPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1608, 184},
    {I_VCMPUNORDPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1617, 184},
    {I_VCMPUNORDPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1626, 184},
    {I_VCMPUNORDPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1635, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNEQ_UQPS[] = {
    {I_VCMPNEQ_UQPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1644, 184},
    {I_VCMPNEQ_UQPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1653, 184},
    {I_VCMPNEQ_UQPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1662, 184},
    {I_VCMPNEQ_UQPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1671, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNEQPS[] = {
    {I_VCMPNEQPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1644, 184},
    {I_VCMPNEQPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1653, 184},
    {I_VCMPNEQPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1662, 184},
    {I_VCMPNEQPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1671, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNLT_USPS[] = {
    {I_VCMPNLT_USPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1680, 184},
    {I_VCMPNLT_USPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1689, 184},
    {I_VCMPNLT_USPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1698, 184},
    {I_VCMPNLT_USPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1707, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNLTPS[] = {
    {I_VCMPNLTPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1680, 184},
    {I_VCMPNLTPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1689, 184},
    {I_VCMPNLTPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1698, 184},
    {I_VCMPNLTPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1707, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNLE_USPS[] = {
    {I_VCMPNLE_USPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1716, 184},
    {I_VCMPNLE_USPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1725, 184},
    {I_VCMPNLE_USPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1734, 184},
    {I_VCMPNLE_USPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1743, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNLEPS[] = {
    {I_VCMPNLEPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1716, 184},
    {I_VCMPNLEPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1725, 184},
    {I_VCMPNLEPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1734, 184},
    {I_VCMPNLEPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1743, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPORD_QPS[] = {
    {I_VCMPORD_QPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1752, 184},
    {I_VCMPORD_QPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1761, 184},
    {I_VCMPORD_QPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1770, 184},
    {I_VCMPORD_QPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1779, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPORDPS[] = {
    {I_VCMPORDPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1752, 184},
    {I_VCMPORDPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1761, 184},
    {I_VCMPORDPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1770, 184},
    {I_VCMPORDPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1779, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPEQ_UQPS[] = {
    {I_VCMPEQ_UQPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1788, 184},
    {I_VCMPEQ_UQPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1797, 184},
    {I_VCMPEQ_UQPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1806, 184},
    {I_VCMPEQ_UQPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1815, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNGE_USPS[] = {
    {I_VCMPNGE_USPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1824, 184},
    {I_VCMPNGE_USPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1833, 184},
    {I_VCMPNGE_USPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1842, 184},
    {I_VCMPNGE_USPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1851, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNGEPS[] = {
    {I_VCMPNGEPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1824, 184},
    {I_VCMPNGEPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1833, 184},
    {I_VCMPNGEPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1842, 184},
    {I_VCMPNGEPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1851, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNGT_USPS[] = {
    {I_VCMPNGT_USPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1860, 184},
    {I_VCMPNGT_USPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1869, 184},
    {I_VCMPNGT_USPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1878, 184},
    {I_VCMPNGT_USPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1887, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNGTPS[] = {
    {I_VCMPNGTPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1860, 184},
    {I_VCMPNGTPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1869, 184},
    {I_VCMPNGTPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1878, 184},
    {I_VCMPNGTPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1887, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPFALSE_OQPS[] = {
    {I_VCMPFALSE_OQPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1896, 184},
    {I_VCMPFALSE_OQPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1905, 184},
    {I_VCMPFALSE_OQPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1914, 184},
    {I_VCMPFALSE_OQPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1923, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPFALSEPS[] = {
    {I_VCMPFALSEPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1896, 184},
    {I_VCMPFALSEPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1905, 184},
    {I_VCMPFALSEPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1914, 184},
    {I_VCMPFALSEPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1923, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNEQ_OQPS[] = {
    {I_VCMPNEQ_OQPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1932, 184},
    {I_VCMPNEQ_OQPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1941, 184},
    {I_VCMPNEQ_OQPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1950, 184},
    {I_VCMPNEQ_OQPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1959, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPGE_OSPS[] = {
    {I_VCMPGE_OSPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1968, 184},
    {I_VCMPGE_OSPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1977, 184},
    {I_VCMPGE_OSPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1986, 184},
    {I_VCMPGE_OSPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1995, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPGEPS[] = {
    {I_VCMPGEPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+1968, 184},
    {I_VCMPGEPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+1977, 184},
    {I_VCMPGEPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+1986, 184},
    {I_VCMPGEPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+1995, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPGT_OSPS[] = {
    {I_VCMPGT_OSPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+2004, 184},
    {I_VCMPGT_OSPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+2013, 184},
    {I_VCMPGT_OSPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+2022, 184},
    {I_VCMPGT_OSPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+2031, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPGTPS[] = {
    {I_VCMPGTPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+2004, 184},
    {I_VCMPGTPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+2013, 184},
    {I_VCMPGTPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+2022, 184},
    {I_VCMPGTPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+2031, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPTRUE_UQPS[] = {
    {I_VCMPTRUE_UQPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+2040, 184},
    {I_VCMPTRUE_UQPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+2049, 184},
    {I_VCMPTRUE_UQPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+2058, 184},
    {I_VCMPTRUE_UQPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+2067, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPTRUEPS[] = {
    {I_VCMPTRUEPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+2040, 184},
    {I_VCMPTRUEPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+2049, 184},
    {I_VCMPTRUEPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+2058, 184},
    {I_VCMPTRUEPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+2067, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPLT_OQPS[] = {
    {I_VCMPLT_OQPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+2076, 184},
    {I_VCMPLT_OQPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+2085, 184},
    {I_VCMPLT_OQPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+2094, 184},
    {I_VCMPLT_OQPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+2103, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPLE_OQPS[] = {
    {I_VCMPLE_OQPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+2112, 184},
    {I_VCMPLE_OQPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+2121, 184},
    {I_VCMPLE_OQPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+2130, 184},
    {I_VCMPLE_OQPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+2139, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPUNORD_SPS[] = {
    {I_VCMPUNORD_SPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+2148, 184},
    {I_VCMPUNORD_SPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+2157, 184},
    {I_VCMPUNORD_SPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+2166, 184},
    {I_VCMPUNORD_SPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+2175, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNEQ_USPS[] = {
    {I_VCMPNEQ_USPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+2184, 184},
    {I_VCMPNEQ_USPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+2193, 184},
    {I_VCMPNEQ_USPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+2202, 184},
    {I_VCMPNEQ_USPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+2211, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNLT_UQPS[] = {
    {I_VCMPNLT_UQPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+2220, 184},
    {I_VCMPNLT_UQPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+2229, 184},
    {I_VCMPNLT_UQPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+2238, 184},
    {I_VCMPNLT_UQPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+2247, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNLE_UQPS[] = {
    {I_VCMPNLE_UQPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+2256, 184},
    {I_VCMPNLE_UQPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+2265, 184},
    {I_VCMPNLE_UQPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+2274, 184},
    {I_VCMPNLE_UQPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+2283, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPORD_SPS[] = {
    {I_VCMPORD_SPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+2292, 184},
    {I_VCMPORD_SPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+2301, 184},
    {I_VCMPORD_SPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+2310, 184},
    {I_VCMPORD_SPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+2319, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPEQ_USPS[] = {
    {I_VCMPEQ_USPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+2328, 184},
    {I_VCMPEQ_USPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+2337, 184},
    {I_VCMPEQ_USPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+2346, 184},
    {I_VCMPEQ_USPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+2355, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNGE_UQPS[] = {
    {I_VCMPNGE_UQPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+2364, 184},
    {I_VCMPNGE_UQPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+2373, 184},
    {I_VCMPNGE_UQPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+2382, 184},
    {I_VCMPNGE_UQPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+2391, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNGT_UQPS[] = {
    {I_VCMPNGT_UQPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+2400, 184},
    {I_VCMPNGT_UQPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+2409, 184},
    {I_VCMPNGT_UQPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+2418, 184},
    {I_VCMPNGT_UQPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+2427, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPFALSE_OSPS[] = {
    {I_VCMPFALSE_OSPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+2436, 184},
    {I_VCMPFALSE_OSPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+2445, 184},
    {I_VCMPFALSE_OSPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+2454, 184},
    {I_VCMPFALSE_OSPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+2463, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNEQ_OSPS[] = {
    {I_VCMPNEQ_OSPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+2472, 184},
    {I_VCMPNEQ_OSPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+2481, 184},
    {I_VCMPNEQ_OSPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+2490, 184},
    {I_VCMPNEQ_OSPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+2499, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPGE_OQPS[] = {
    {I_VCMPGE_OQPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+2508, 184},
    {I_VCMPGE_OQPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+2517, 184},
    {I_VCMPGE_OQPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+2526, 184},
    {I_VCMPGE_OQPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+2535, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPGT_OQPS[] = {
    {I_VCMPGT_OQPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+2544, 184},
    {I_VCMPGT_OQPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+2553, 184},
    {I_VCMPGT_OQPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+2562, 184},
    {I_VCMPGT_OQPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+2571, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPTRUE_USPS[] = {
    {I_VCMPTRUE_USPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+2580, 184},
    {I_VCMPTRUE_USPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+2589, 184},
    {I_VCMPTRUE_USPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+2598, 184},
    {I_VCMPTRUE_USPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+2607, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPPS[] = {
    {I_VCMPPS, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9500, 184},
    {I_VCMPPS, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9508, 184},
    {I_VCMPPS, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9516, 184},
    {I_VCMPPS, 3, {YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9524, 184},
    {I_VCMPPS, 4, {KREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK,0,B32,0,0}, nasm_bytecodes+4137, 225},
    {I_VCMPPS, 4, {KREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK,0,B32,0,0}, nasm_bytecodes+4146, 225},
    {I_VCMPPS, 4, {KREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK,0,B32|SAE,0,0}, nasm_bytecodes+4155, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPEQ_OSSD[] = {
    {I_VCMPEQ_OSSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2616, 184},
    {I_VCMPEQ_OSSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2625, 184},
    {I_VCMPEQ_OSSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2616, 184},
    {I_VCMPEQ_OSSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2625, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPEQSD[] = {
    {I_VCMPEQSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2634, 184},
    {I_VCMPEQSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2643, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPLT_OSSD[] = {
    {I_VCMPLT_OSSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2652, 184},
    {I_VCMPLT_OSSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2661, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPLTSD[] = {
    {I_VCMPLTSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2652, 184},
    {I_VCMPLTSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2661, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPLE_OSSD[] = {
    {I_VCMPLE_OSSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2670, 184},
    {I_VCMPLE_OSSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2679, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPLESD[] = {
    {I_VCMPLESD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2670, 184},
    {I_VCMPLESD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2679, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPUNORD_QSD[] = {
    {I_VCMPUNORD_QSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2688, 184},
    {I_VCMPUNORD_QSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2697, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPUNORDSD[] = {
    {I_VCMPUNORDSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2688, 184},
    {I_VCMPUNORDSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2697, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNEQ_UQSD[] = {
    {I_VCMPNEQ_UQSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2706, 184},
    {I_VCMPNEQ_UQSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2715, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNEQSD[] = {
    {I_VCMPNEQSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2706, 184},
    {I_VCMPNEQSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2715, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNLT_USSD[] = {
    {I_VCMPNLT_USSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2724, 184},
    {I_VCMPNLT_USSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2733, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNLTSD[] = {
    {I_VCMPNLTSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2724, 184},
    {I_VCMPNLTSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2733, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNLE_USSD[] = {
    {I_VCMPNLE_USSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2742, 184},
    {I_VCMPNLE_USSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2751, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNLESD[] = {
    {I_VCMPNLESD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2742, 184},
    {I_VCMPNLESD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2751, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPORD_QSD[] = {
    {I_VCMPORD_QSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2760, 184},
    {I_VCMPORD_QSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2769, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPORDSD[] = {
    {I_VCMPORDSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2760, 184},
    {I_VCMPORDSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2769, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPEQ_UQSD[] = {
    {I_VCMPEQ_UQSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2778, 184},
    {I_VCMPEQ_UQSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2787, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNGE_USSD[] = {
    {I_VCMPNGE_USSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2796, 184},
    {I_VCMPNGE_USSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2805, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNGESD[] = {
    {I_VCMPNGESD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2796, 184},
    {I_VCMPNGESD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2805, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNGT_USSD[] = {
    {I_VCMPNGT_USSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2814, 184},
    {I_VCMPNGT_USSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2823, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNGTSD[] = {
    {I_VCMPNGTSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2814, 184},
    {I_VCMPNGTSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2823, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPFALSE_OQSD[] = {
    {I_VCMPFALSE_OQSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2832, 184},
    {I_VCMPFALSE_OQSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2841, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPFALSESD[] = {
    {I_VCMPFALSESD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2832, 184},
    {I_VCMPFALSESD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2841, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNEQ_OQSD[] = {
    {I_VCMPNEQ_OQSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2850, 184},
    {I_VCMPNEQ_OQSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2859, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPGE_OSSD[] = {
    {I_VCMPGE_OSSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2868, 184},
    {I_VCMPGE_OSSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2877, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPGESD[] = {
    {I_VCMPGESD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2868, 184},
    {I_VCMPGESD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2877, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPGT_OSSD[] = {
    {I_VCMPGT_OSSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2886, 184},
    {I_VCMPGT_OSSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2895, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPGTSD[] = {
    {I_VCMPGTSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2886, 184},
    {I_VCMPGTSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2895, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPTRUE_UQSD[] = {
    {I_VCMPTRUE_UQSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2904, 184},
    {I_VCMPTRUE_UQSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2913, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPTRUESD[] = {
    {I_VCMPTRUESD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2904, 184},
    {I_VCMPTRUESD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2913, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPLT_OQSD[] = {
    {I_VCMPLT_OQSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2922, 184},
    {I_VCMPLT_OQSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2931, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPLE_OQSD[] = {
    {I_VCMPLE_OQSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2940, 184},
    {I_VCMPLE_OQSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2949, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPUNORD_SSD[] = {
    {I_VCMPUNORD_SSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2958, 184},
    {I_VCMPUNORD_SSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2967, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNEQ_USSD[] = {
    {I_VCMPNEQ_USSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2976, 184},
    {I_VCMPNEQ_USSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+2985, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNLT_UQSD[] = {
    {I_VCMPNLT_UQSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+2994, 184},
    {I_VCMPNLT_UQSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3003, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNLE_UQSD[] = {
    {I_VCMPNLE_UQSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3012, 184},
    {I_VCMPNLE_UQSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3021, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPORD_SSD[] = {
    {I_VCMPORD_SSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3030, 184},
    {I_VCMPORD_SSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3039, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPEQ_USSD[] = {
    {I_VCMPEQ_USSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3048, 184},
    {I_VCMPEQ_USSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3057, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNGE_UQSD[] = {
    {I_VCMPNGE_UQSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3066, 184},
    {I_VCMPNGE_UQSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3075, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNGT_UQSD[] = {
    {I_VCMPNGT_UQSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3084, 184},
    {I_VCMPNGT_UQSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3093, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPFALSE_OSSD[] = {
    {I_VCMPFALSE_OSSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3102, 184},
    {I_VCMPFALSE_OSSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3111, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNEQ_OSSD[] = {
    {I_VCMPNEQ_OSSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3120, 184},
    {I_VCMPNEQ_OSSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3129, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPGE_OQSD[] = {
    {I_VCMPGE_OQSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3138, 184},
    {I_VCMPGE_OQSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3147, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPGT_OQSD[] = {
    {I_VCMPGT_OQSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3156, 184},
    {I_VCMPGT_OQSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3165, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPTRUE_USSD[] = {
    {I_VCMPTRUE_USSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3174, 184},
    {I_VCMPTRUE_USSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3183, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPSD[] = {
    {I_VCMPSD, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9532, 184},
    {I_VCMPSD, 3, {XMM_L16,RM_XMM_L16|BITS64,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9540, 184},
    {I_VCMPSD, 4, {KREG,XMMREG,RM_XMM|BITS64,IMMEDIATE|BITS8,0}, {MASK,0,SAE,0,0}, nasm_bytecodes+4164, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPEQ_OSSS[] = {
    {I_VCMPEQ_OSSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3192, 184},
    {I_VCMPEQ_OSSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3201, 184},
    {I_VCMPEQ_OSSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3192, 184},
    {I_VCMPEQ_OSSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3201, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPEQSS[] = {
    {I_VCMPEQSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3210, 184},
    {I_VCMPEQSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3219, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPLT_OSSS[] = {
    {I_VCMPLT_OSSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3228, 184},
    {I_VCMPLT_OSSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3237, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPLTSS[] = {
    {I_VCMPLTSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3228, 184},
    {I_VCMPLTSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3237, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPLE_OSSS[] = {
    {I_VCMPLE_OSSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3246, 184},
    {I_VCMPLE_OSSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3255, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPLESS[] = {
    {I_VCMPLESS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3246, 184},
    {I_VCMPLESS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3255, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPUNORD_QSS[] = {
    {I_VCMPUNORD_QSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3264, 184},
    {I_VCMPUNORD_QSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3273, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPUNORDSS[] = {
    {I_VCMPUNORDSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3264, 184},
    {I_VCMPUNORDSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3273, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNEQ_UQSS[] = {
    {I_VCMPNEQ_UQSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3282, 184},
    {I_VCMPNEQ_UQSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3291, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNEQSS[] = {
    {I_VCMPNEQSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3282, 184},
    {I_VCMPNEQSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3291, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNLT_USSS[] = {
    {I_VCMPNLT_USSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3300, 184},
    {I_VCMPNLT_USSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3309, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNLTSS[] = {
    {I_VCMPNLTSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3300, 184},
    {I_VCMPNLTSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3309, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNLE_USSS[] = {
    {I_VCMPNLE_USSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3318, 184},
    {I_VCMPNLE_USSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3327, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNLESS[] = {
    {I_VCMPNLESS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3318, 184},
    {I_VCMPNLESS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3327, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPORD_QSS[] = {
    {I_VCMPORD_QSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3336, 184},
    {I_VCMPORD_QSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3345, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPORDSS[] = {
    {I_VCMPORDSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3336, 184},
    {I_VCMPORDSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3345, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPEQ_UQSS[] = {
    {I_VCMPEQ_UQSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3354, 184},
    {I_VCMPEQ_UQSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3363, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNGE_USSS[] = {
    {I_VCMPNGE_USSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3372, 184},
    {I_VCMPNGE_USSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3381, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNGESS[] = {
    {I_VCMPNGESS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3372, 184},
    {I_VCMPNGESS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3381, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNGT_USSS[] = {
    {I_VCMPNGT_USSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3390, 184},
    {I_VCMPNGT_USSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3399, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNGTSS[] = {
    {I_VCMPNGTSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3390, 184},
    {I_VCMPNGTSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3399, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPFALSE_OQSS[] = {
    {I_VCMPFALSE_OQSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3408, 184},
    {I_VCMPFALSE_OQSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3417, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPFALSESS[] = {
    {I_VCMPFALSESS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3408, 184},
    {I_VCMPFALSESS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3417, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNEQ_OQSS[] = {
    {I_VCMPNEQ_OQSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3426, 184},
    {I_VCMPNEQ_OQSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3435, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPGE_OSSS[] = {
    {I_VCMPGE_OSSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3444, 184},
    {I_VCMPGE_OSSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3453, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPGESS[] = {
    {I_VCMPGESS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3444, 184},
    {I_VCMPGESS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3453, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPGT_OSSS[] = {
    {I_VCMPGT_OSSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3462, 184},
    {I_VCMPGT_OSSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3471, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPGTSS[] = {
    {I_VCMPGTSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3462, 184},
    {I_VCMPGTSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3471, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPTRUE_UQSS[] = {
    {I_VCMPTRUE_UQSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3480, 184},
    {I_VCMPTRUE_UQSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3489, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPTRUESS[] = {
    {I_VCMPTRUESS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3480, 184},
    {I_VCMPTRUESS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3489, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPLT_OQSS[] = {
    {I_VCMPLT_OQSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3498, 184},
    {I_VCMPLT_OQSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3507, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPLE_OQSS[] = {
    {I_VCMPLE_OQSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3516, 184},
    {I_VCMPLE_OQSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3525, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPUNORD_SSS[] = {
    {I_VCMPUNORD_SSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3534, 184},
    {I_VCMPUNORD_SSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3543, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNEQ_USSS[] = {
    {I_VCMPNEQ_USSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3552, 184},
    {I_VCMPNEQ_USSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3561, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNLT_UQSS[] = {
    {I_VCMPNLT_UQSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3570, 184},
    {I_VCMPNLT_UQSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3579, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNLE_UQSS[] = {
    {I_VCMPNLE_UQSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3588, 184},
    {I_VCMPNLE_UQSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3597, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPORD_SSS[] = {
    {I_VCMPORD_SSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3606, 184},
    {I_VCMPORD_SSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3615, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPEQ_USSS[] = {
    {I_VCMPEQ_USSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3624, 184},
    {I_VCMPEQ_USSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3633, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNGE_UQSS[] = {
    {I_VCMPNGE_UQSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3642, 184},
    {I_VCMPNGE_UQSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3651, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNGT_UQSS[] = {
    {I_VCMPNGT_UQSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3660, 184},
    {I_VCMPNGT_UQSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3669, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPFALSE_OSSS[] = {
    {I_VCMPFALSE_OSSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3678, 184},
    {I_VCMPFALSE_OSSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3687, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPNEQ_OSSS[] = {
    {I_VCMPNEQ_OSSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3696, 184},
    {I_VCMPNEQ_OSSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3705, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPGE_OQSS[] = {
    {I_VCMPGE_OQSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3714, 184},
    {I_VCMPGE_OQSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3723, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPGT_OQSS[] = {
    {I_VCMPGT_OQSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3732, 184},
    {I_VCMPGT_OQSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3741, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPTRUE_USSS[] = {
    {I_VCMPTRUE_USSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+3750, 184},
    {I_VCMPTRUE_USSS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+3759, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCMPSS[] = {
    {I_VCMPSS, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9548, 184},
    {I_VCMPSS, 3, {XMM_L16,RM_XMM_L16|BITS64,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9556, 184},
    {I_VCMPSS, 4, {KREG,XMMREG,RM_XMM|BITS32,IMMEDIATE|BITS8,0}, {MASK,0,SAE,0,0}, nasm_bytecodes+4173, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCOMISD[] = {
    {I_VCOMISD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+28092, 184},
    {I_VCOMISD, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {0,SAE,0,0,0}, nasm_bytecodes+12988, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCOMISS[] = {
    {I_VCOMISS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+28099, 184},
    {I_VCOMISS, 2, {XMMREG,RM_XMM|BITS32,0,0,0}, {0,SAE,0,0,0}, nasm_bytecodes+12996, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTDQ2PD[] = {
    {I_VCVTDQ2PD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+28106, 184},
    {I_VCVTDQ2PD, 2, {YMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+28113, 184},
    {I_VCVTDQ2PD, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+13100, 225},
    {I_VCVTDQ2PD, 2, {YMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+13108, 225},
    {I_VCVTDQ2PD, 2, {ZMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32|ER,0,0,0}, nasm_bytecodes+13116, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTDQ2PS[] = {
    {I_VCVTDQ2PS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+28120, 184},
    {I_VCVTDQ2PS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+28127, 184},
    {I_VCVTDQ2PS, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+13124, 225},
    {I_VCVTDQ2PS, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+13132, 225},
    {I_VCVTDQ2PS, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32|ER,0,0,0}, nasm_bytecodes+13140, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTPD2DQ[] = {
    {I_VCVTPD2DQ, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+28134, 184},
    {I_VCVTPD2DQ, 2, {XMM_L16,MEMORY|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+28134, 188},
    {I_VCVTPD2DQ, 2, {XMM_L16,YMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+28141, 184},
    {I_VCVTPD2DQ, 2, {XMM_L16,MEMORY|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+28141, 189},
    {I_VCVTPD2DQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+13148, 225},
    {I_VCVTPD2DQ, 2, {XMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+13156, 225},
    {I_VCVTPD2DQ, 2, {YMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64|ER,0,0,0}, nasm_bytecodes+13164, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTPD2PS[] = {
    {I_VCVTPD2PS, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+28148, 184},
    {I_VCVTPD2PS, 2, {XMM_L16,MEMORY|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+28148, 188},
    {I_VCVTPD2PS, 2, {XMM_L16,YMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+28155, 184},
    {I_VCVTPD2PS, 2, {XMM_L16,MEMORY|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+28155, 189},
    {I_VCVTPD2PS, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+13172, 225},
    {I_VCVTPD2PS, 2, {XMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+13180, 225},
    {I_VCVTPD2PS, 2, {YMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64|ER,0,0,0}, nasm_bytecodes+13188, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTPS2DQ[] = {
    {I_VCVTPS2DQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+28162, 184},
    {I_VCVTPS2DQ, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+28169, 184},
    {I_VCVTPS2DQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+13292, 225},
    {I_VCVTPS2DQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+13300, 225},
    {I_VCVTPS2DQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32|ER,0,0,0}, nasm_bytecodes+13308, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTPS2PD[] = {
    {I_VCVTPS2PD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+28176, 184},
    {I_VCVTPS2PD, 2, {YMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+28183, 184},
    {I_VCVTPS2PD, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+13316, 225},
    {I_VCVTPS2PD, 2, {YMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+13324, 225},
    {I_VCVTPS2PD, 2, {ZMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32|SAE,0,0,0}, nasm_bytecodes+13332, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTSD2SI[] = {
    {I_VCVTSD2SI, 2, {REG_GPR|BITS32,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+28190, 184},
    {I_VCVTSD2SI, 2, {REG_GPR|BITS64,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+28197, 190},
    {I_VCVTSD2SI, 2, {REG_GPR|BITS32,RM_XMM|BITS64,0,0,0}, {0,ER,0,0,0}, nasm_bytecodes+13460, 226},
    {I_VCVTSD2SI, 2, {REG_GPR|BITS64,RM_XMM|BITS64,0,0,0}, {0,ER,0,0,0}, nasm_bytecodes+13468, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTSD2SS[] = {
    {I_VCVTSD2SS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+28204, 184},
    {I_VCVTSD2SS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+28211, 184},
    {I_VCVTSD2SS, 3, {XMMREG,XMMREG,RM_XMM|BITS64,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+13476, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTSI2SD[] = {
    {I_VCVTSI2SD, 3, {XMM_L16,XMM_L16,RM_GPR|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+28218, 191},
    {I_VCVTSI2SD, 2, {XMM_L16,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+28225, 191},
    {I_VCVTSI2SD, 3, {XMM_L16,XMM_L16,MEMORY|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+28218, 191},
    {I_VCVTSI2SD, 2, {XMM_L16,MEMORY|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+28225, 191},
    {I_VCVTSI2SD, 3, {XMM_L16,XMM_L16,RM_GPR|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+28232, 192},
    {I_VCVTSI2SD, 2, {XMM_L16,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+28239, 192},
    {I_VCVTSI2SD, 3, {XMMREG,XMMREG,RM_GPR|BITS32,0,0}, {0,ER,0,0,0}, nasm_bytecodes+13500, 226},
    {I_VCVTSI2SD, 3, {XMMREG,XMMREG,RM_GPR|BITS64,0,0}, {0,ER,0,0,0}, nasm_bytecodes+13508, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTSI2SS[] = {
    {I_VCVTSI2SS, 3, {XMM_L16,XMM_L16,RM_GPR|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+28246, 191},
    {I_VCVTSI2SS, 2, {XMM_L16,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+28253, 191},
    {I_VCVTSI2SS, 3, {XMM_L16,XMM_L16,MEMORY|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+28246, 191},
    {I_VCVTSI2SS, 2, {XMM_L16,MEMORY|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+28253, 191},
    {I_VCVTSI2SS, 3, {XMM_L16,XMM_L16,RM_GPR|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+28260, 192},
    {I_VCVTSI2SS, 2, {XMM_L16,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+28267, 192},
    {I_VCVTSI2SS, 3, {XMMREG,XMMREG,RM_GPR|BITS32,0,0}, {0,ER,0,0,0}, nasm_bytecodes+13516, 226},
    {I_VCVTSI2SS, 3, {XMMREG,XMMREG,RM_GPR|BITS64,0,0}, {0,ER,0,0,0}, nasm_bytecodes+13524, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTSS2SD[] = {
    {I_VCVTSS2SD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+28274, 184},
    {I_VCVTSS2SD, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+28281, 184},
    {I_VCVTSS2SD, 3, {XMMREG,XMMREG,RM_XMM|BITS32,0,0}, {MASK|Z,0,SAE,0,0}, nasm_bytecodes+13532, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTSS2SI[] = {
    {I_VCVTSS2SI, 2, {REG_GPR|BITS32,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+28288, 184},
    {I_VCVTSS2SI, 2, {REG_GPR|BITS64,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+28295, 190},
    {I_VCVTSS2SI, 2, {REG_GPR|BITS32,RM_XMM|BITS32,0,0,0}, {0,ER,0,0,0}, nasm_bytecodes+13540, 226},
    {I_VCVTSS2SI, 2, {REG_GPR|BITS64,RM_XMM|BITS32,0,0,0}, {0,ER,0,0,0}, nasm_bytecodes+13548, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTTPD2DQ[] = {
    {I_VCVTTPD2DQ, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+28302, 184},
    {I_VCVTTPD2DQ, 2, {XMM_L16,MEMORY|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+28302, 188},
    {I_VCVTTPD2DQ, 2, {XMM_L16,YMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+28309, 184},
    {I_VCVTTPD2DQ, 2, {XMM_L16,MEMORY|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+28309, 189},
    {I_VCVTTPD2DQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+13572, 225},
    {I_VCVTTPD2DQ, 2, {XMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+13580, 225},
    {I_VCVTTPD2DQ, 2, {YMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64|SAE,0,0,0}, nasm_bytecodes+13588, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTTPS2DQ[] = {
    {I_VCVTTPS2DQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+28316, 184},
    {I_VCVTTPS2DQ, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+28323, 184},
    {I_VCVTTPS2DQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+13668, 225},
    {I_VCVTTPS2DQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+13676, 225},
    {I_VCVTTPS2DQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32|SAE,0,0,0}, nasm_bytecodes+13684, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTTSD2SI[] = {
    {I_VCVTTSD2SI, 2, {REG_GPR|BITS32,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+28330, 184},
    {I_VCVTTSD2SI, 2, {REG_GPR|BITS64,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+28337, 190},
    {I_VCVTTSD2SI, 2, {REG_GPR|BITS32,RM_XMM|BITS64,0,0,0}, {0,SAE,0,0,0}, nasm_bytecodes+13764, 226},
    {I_VCVTTSD2SI, 2, {REG_GPR|BITS64,RM_XMM|BITS64,0,0,0}, {0,SAE,0,0,0}, nasm_bytecodes+13772, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTTSS2SI[] = {
    {I_VCVTTSS2SI, 2, {REG_GPR|BITS32,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+28344, 184},
    {I_VCVTTSS2SI, 2, {REG_GPR|BITS64,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+28351, 190},
    {I_VCVTTSS2SI, 2, {REG_GPR|BITS32,RM_XMM|BITS32,0,0,0}, {0,SAE,0,0,0}, nasm_bytecodes+13796, 226},
    {I_VCVTTSS2SI, 2, {REG_GPR|BITS64,RM_XMM|BITS32,0,0,0}, {0,SAE,0,0,0}, nasm_bytecodes+13804, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VDIVPD[] = {
    {I_VDIVPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+28358, 184},
    {I_VDIVPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+28365, 184},
    {I_VDIVPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+28372, 184},
    {I_VDIVPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+28379, 184},
    {I_VDIVPD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+13956, 225},
    {I_VDIVPD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+13964, 225},
    {I_VDIVPD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+13972, 225},
    {I_VDIVPD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+13980, 225},
    {I_VDIVPD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64|ER,0,0}, nasm_bytecodes+13988, 226},
    {I_VDIVPD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64|ER,0,0,0}, nasm_bytecodes+13996, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VDIVPS[] = {
    {I_VDIVPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+28386, 184},
    {I_VDIVPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+28393, 184},
    {I_VDIVPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+28400, 184},
    {I_VDIVPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+28407, 184},
    {I_VDIVPS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14004, 225},
    {I_VDIVPS, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+14012, 225},
    {I_VDIVPS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14020, 225},
    {I_VDIVPS, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+14028, 225},
    {I_VDIVPS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32|ER,0,0}, nasm_bytecodes+14036, 226},
    {I_VDIVPS, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32|ER,0,0,0}, nasm_bytecodes+14044, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VDIVSD[] = {
    {I_VDIVSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+28414, 184},
    {I_VDIVSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+28421, 184},
    {I_VDIVSD, 3, {XMMREG,XMMREG,RM_XMM|BITS64,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+14052, 226},
    {I_VDIVSD, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,ER,0,0,0}, nasm_bytecodes+14060, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VDIVSS[] = {
    {I_VDIVSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+28428, 184},
    {I_VDIVSS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+28435, 184},
    {I_VDIVSS, 3, {XMMREG,XMMREG,RM_XMM|BITS32,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+14068, 226},
    {I_VDIVSS, 2, {XMMREG,RM_XMM|BITS32,0,0,0}, {MASK|Z,ER,0,0,0}, nasm_bytecodes+14076, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VDPPD[] = {
    {I_VDPPD, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9564, 184},
    {I_VDPPD, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9572, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VDPPS[] = {
    {I_VDPPS, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9580, 184},
    {I_VDPPS, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9588, 184},
    {I_VDPPS, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9596, 184},
    {I_VDPPS, 3, {YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9604, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VEXTRACTF128[] = {
    {I_VEXTRACTF128, 3, {RM_XMM_L16|BITS128,YMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9612, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VEXTRACTPS[] = {
    {I_VEXTRACTPS, 3, {RM_GPR|BITS32,XMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9620, 184},
    {I_VEXTRACTPS, 3, {REG_GPR|BITS32,XMMREG,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+4479, 226},
    {I_VEXTRACTPS, 3, {REG_GPR|BITS64,XMMREG,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+4479, 226},
    {I_VEXTRACTPS, 3, {MEMORY|BITS32,XMMREG,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+4479, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VHADDPD[] = {
    {I_VHADDPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+28442, 184},
    {I_VHADDPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+28449, 184},
    {I_VHADDPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+28456, 184},
    {I_VHADDPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+28463, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VHADDPS[] = {
    {I_VHADDPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+28470, 184},
    {I_VHADDPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+28477, 184},
    {I_VHADDPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+28484, 184},
    {I_VHADDPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+28491, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VHSUBPD[] = {
    {I_VHSUBPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+28498, 184},
    {I_VHSUBPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+28505, 184},
    {I_VHSUBPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+28512, 184},
    {I_VHSUBPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+28519, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VHSUBPS[] = {
    {I_VHSUBPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+28526, 184},
    {I_VHSUBPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+28533, 184},
    {I_VHSUBPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+28540, 184},
    {I_VHSUBPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+28547, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VINSERTF128[] = {
    {I_VINSERTF128, 4, {YMM_L16,YMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9628, 184},
    {I_VINSERTF128, 3, {YMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9636, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VINSERTPS[] = {
    {I_VINSERTPS, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9644, 184},
    {I_VINSERTPS, 3, {XMM_L16,RM_XMM_L16|BITS32,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9652, 184},
    {I_VINSERTPS, 4, {XMMREG,XMMREG,RM_XMM|BITS32,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+5172, 226},
    {I_VINSERTPS, 3, {XMMREG,RM_XMM|BITS32,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+5181, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VLDDQU[] = {
    {I_VLDDQU, 2, {XMM_L16,MEMORY|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+28554, 184},
    {I_VLDDQU, 2, {YMM_L16,MEMORY|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+28561, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VLDQQU[] = {
    {I_VLDQQU, 2, {YMM_L16,MEMORY|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+28561, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VLDMXCSR[] = {
    {I_VLDMXCSR, 1, {MEMORY|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+28568, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMASKMOVDQU[] = {
    {I_VMASKMOVDQU, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+28575, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMASKMOVPS[] = {
    {I_VMASKMOVPS, 3, {XMM_L16,XMM_L16,MEMORY|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+28582, 184},
    {I_VMASKMOVPS, 3, {YMM_L16,YMM_L16,MEMORY|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+28589, 184},
    {I_VMASKMOVPS, 3, {MEMORY|BITS128,XMM_L16,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+28596, 188},
    {I_VMASKMOVPS, 3, {MEMORY|BITS256,YMM_L16,YMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+28603, 189},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMASKMOVPD[] = {
    {I_VMASKMOVPD, 3, {XMM_L16,XMM_L16,MEMORY|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+28610, 184},
    {I_VMASKMOVPD, 3, {YMM_L16,YMM_L16,MEMORY|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+28617, 184},
    {I_VMASKMOVPD, 3, {MEMORY|BITS128,XMM_L16,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+28624, 184},
    {I_VMASKMOVPD, 3, {MEMORY|BITS256,YMM_L16,YMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+28631, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMAXPD[] = {
    {I_VMAXPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+28638, 184},
    {I_VMAXPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+28645, 184},
    {I_VMAXPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+28652, 184},
    {I_VMAXPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+28659, 184},
    {I_VMAXPD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+15268, 225},
    {I_VMAXPD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+15276, 225},
    {I_VMAXPD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+15284, 225},
    {I_VMAXPD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+15292, 225},
    {I_VMAXPD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64|SAE,0,0}, nasm_bytecodes+15300, 226},
    {I_VMAXPD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64|SAE,0,0,0}, nasm_bytecodes+15308, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMAXPS[] = {
    {I_VMAXPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+28666, 184},
    {I_VMAXPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+28673, 184},
    {I_VMAXPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+28680, 184},
    {I_VMAXPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+28687, 184},
    {I_VMAXPS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+15316, 225},
    {I_VMAXPS, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+15324, 225},
    {I_VMAXPS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+15332, 225},
    {I_VMAXPS, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+15340, 225},
    {I_VMAXPS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32|SAE,0,0}, nasm_bytecodes+15348, 226},
    {I_VMAXPS, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32|SAE,0,0,0}, nasm_bytecodes+15356, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMAXSD[] = {
    {I_VMAXSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+28694, 184},
    {I_VMAXSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+28701, 184},
    {I_VMAXSD, 3, {XMMREG,XMMREG,RM_XMM|BITS64,0,0}, {MASK|Z,0,SAE,0,0}, nasm_bytecodes+15364, 226},
    {I_VMAXSD, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,SAE,0,0,0}, nasm_bytecodes+15372, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMAXSS[] = {
    {I_VMAXSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+28708, 184},
    {I_VMAXSS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+28715, 184},
    {I_VMAXSS, 3, {XMMREG,XMMREG,RM_XMM|BITS32,0,0}, {MASK|Z,0,SAE,0,0}, nasm_bytecodes+15380, 226},
    {I_VMAXSS, 2, {XMMREG,RM_XMM|BITS32,0,0,0}, {MASK|Z,SAE,0,0,0}, nasm_bytecodes+15388, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMINPD[] = {
    {I_VMINPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+28722, 184},
    {I_VMINPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+28729, 184},
    {I_VMINPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+28736, 184},
    {I_VMINPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+28743, 184},
    {I_VMINPD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+15396, 225},
    {I_VMINPD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+15404, 225},
    {I_VMINPD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+15412, 225},
    {I_VMINPD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+15420, 225},
    {I_VMINPD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64|SAE,0,0}, nasm_bytecodes+15428, 226},
    {I_VMINPD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64|SAE,0,0,0}, nasm_bytecodes+15436, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMINPS[] = {
    {I_VMINPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+28750, 184},
    {I_VMINPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+28757, 184},
    {I_VMINPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+28764, 184},
    {I_VMINPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+28771, 184},
    {I_VMINPS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+15444, 225},
    {I_VMINPS, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+15452, 225},
    {I_VMINPS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+15460, 225},
    {I_VMINPS, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+15468, 225},
    {I_VMINPS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32|SAE,0,0}, nasm_bytecodes+15476, 226},
    {I_VMINPS, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32|SAE,0,0,0}, nasm_bytecodes+15484, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMINSD[] = {
    {I_VMINSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+28778, 184},
    {I_VMINSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+28785, 184},
    {I_VMINSD, 3, {XMMREG,XMMREG,RM_XMM|BITS64,0,0}, {MASK|Z,0,SAE,0,0}, nasm_bytecodes+15492, 226},
    {I_VMINSD, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,SAE,0,0,0}, nasm_bytecodes+15500, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMINSS[] = {
    {I_VMINSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+28792, 184},
    {I_VMINSS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+28799, 184},
    {I_VMINSS, 3, {XMMREG,XMMREG,RM_XMM|BITS32,0,0}, {MASK|Z,0,SAE,0,0}, nasm_bytecodes+15508, 226},
    {I_VMINSS, 2, {XMMREG,RM_XMM|BITS32,0,0,0}, {MASK|Z,SAE,0,0,0}, nasm_bytecodes+15516, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVAPD[] = {
    {I_VMOVAPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+28806, 184},
    {I_VMOVAPD, 2, {RM_XMM_L16|BITS128,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+28813, 184},
    {I_VMOVAPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+28820, 184},
    {I_VMOVAPD, 2, {RM_YMM_L16|BITS256,YMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+28827, 184},
    {I_VMOVAPD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15524, 225},
    {I_VMOVAPD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15532, 225},
    {I_VMOVAPD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15540, 226},
    {I_VMOVAPD, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15548, 225},
    {I_VMOVAPD, 2, {YMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15556, 225},
    {I_VMOVAPD, 2, {ZMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15564, 226},
    {I_VMOVAPD, 2, {MEMORY|BITS128,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+15572, 225},
    {I_VMOVAPD, 2, {MEMORY|BITS256,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+15580, 225},
    {I_VMOVAPD, 2, {MEMORY|BITS512,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+15588, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVAPS[] = {
    {I_VMOVAPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+28834, 184},
    {I_VMOVAPS, 2, {RM_XMM_L16|BITS128,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+28841, 184},
    {I_VMOVAPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+28848, 184},
    {I_VMOVAPS, 2, {RM_YMM_L16|BITS256,YMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+28855, 184},
    {I_VMOVAPS, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15596, 225},
    {I_VMOVAPS, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15604, 225},
    {I_VMOVAPS, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15612, 226},
    {I_VMOVAPS, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15620, 225},
    {I_VMOVAPS, 2, {YMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15628, 225},
    {I_VMOVAPS, 2, {ZMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15636, 226},
    {I_VMOVAPS, 2, {MEMORY|BITS128,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+15644, 225},
    {I_VMOVAPS, 2, {MEMORY|BITS256,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+15652, 225},
    {I_VMOVAPS, 2, {MEMORY|BITS512,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+15660, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVD[] = {
    {I_VMOVD, 2, {XMM_L16,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+28862, 184},
    {I_VMOVD, 2, {RM_GPR|BITS32,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+28869, 184},
    {I_VMOVD, 2, {XMMREG,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+15668, 226},
    {I_VMOVD, 2, {RM_GPR|BITS32,XMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+15676, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVQ[] = {
    {I_VMOVQ, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+28876, 193},
    {I_VMOVQ, 2, {RM_XMM_L16|BITS64,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+28883, 193},
    {I_VMOVQ, 2, {XMM_L16,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+28890, 192},
    {I_VMOVQ, 2, {RM_GPR|BITS64,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+28897, 192},
    {I_VMOVQ, 2, {XMMREG,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+16220, 226},
    {I_VMOVQ, 2, {RM_GPR|BITS64,XMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+16228, 226},
    {I_VMOVQ, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+16236, 226},
    {I_VMOVQ, 2, {RM_XMM|BITS64,XMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+16244, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVDDUP[] = {
    {I_VMOVDDUP, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+28904, 184},
    {I_VMOVDDUP, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+28911, 184},
    {I_VMOVDDUP, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15684, 225},
    {I_VMOVDDUP, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15692, 225},
    {I_VMOVDDUP, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15700, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVDQA[] = {
    {I_VMOVDQA, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+28918, 184},
    {I_VMOVDQA, 2, {RM_XMM_L16|BITS128,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+28925, 184},
    {I_VMOVDQA, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+28932, 184},
    {I_VMOVDQA, 2, {RM_YMM_L16|BITS256,YMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+28939, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVQQA[] = {
    {I_VMOVQQA, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+28932, 184},
    {I_VMOVQQA, 2, {RM_YMM_L16|BITS256,YMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+28939, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVDQU[] = {
    {I_VMOVDQU, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+28946, 184},
    {I_VMOVDQU, 2, {RM_XMM_L16|BITS128,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+28953, 184},
    {I_VMOVDQU, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+28960, 184},
    {I_VMOVDQU, 2, {RM_YMM_L16|BITS256,YMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+28967, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVQQU[] = {
    {I_VMOVQQU, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+28960, 184},
    {I_VMOVQQU, 2, {RM_YMM_L16|BITS256,YMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+28967, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVHLPS[] = {
    {I_VMOVHLPS, 3, {XMM_L16,XMM_L16,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+28974, 184},
    {I_VMOVHLPS, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+28981, 184},
    {I_VMOVHLPS, 3, {XMMREG,XMMREG,XMMREG,0,0}, NO_DECORATOR, nasm_bytecodes+15996, 226},
    {I_VMOVHLPS, 2, {XMMREG,XMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+16004, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVHPD[] = {
    {I_VMOVHPD, 3, {XMM_L16,XMM_L16,MEMORY|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+28988, 184},
    {I_VMOVHPD, 2, {XMM_L16,MEMORY|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+28995, 184},
    {I_VMOVHPD, 2, {MEMORY|BITS64,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29002, 184},
    {I_VMOVHPD, 3, {XMMREG,XMMREG,MEMORY|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+16012, 226},
    {I_VMOVHPD, 2, {XMMREG,MEMORY|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+16020, 226},
    {I_VMOVHPD, 2, {MEMORY|BITS64,XMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+16028, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVHPS[] = {
    {I_VMOVHPS, 3, {XMM_L16,XMM_L16,MEMORY|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+29009, 184},
    {I_VMOVHPS, 2, {XMM_L16,MEMORY|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+29016, 184},
    {I_VMOVHPS, 2, {MEMORY|BITS64,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29023, 184},
    {I_VMOVHPS, 3, {XMMREG,XMMREG,MEMORY|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+16036, 226},
    {I_VMOVHPS, 2, {XMMREG,MEMORY|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+16044, 226},
    {I_VMOVHPS, 2, {MEMORY|BITS64,XMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+16052, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVLHPS[] = {
    {I_VMOVLHPS, 3, {XMM_L16,XMM_L16,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+29009, 184},
    {I_VMOVLHPS, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29016, 184},
    {I_VMOVLHPS, 3, {XMMREG,XMMREG,XMMREG,0,0}, NO_DECORATOR, nasm_bytecodes+16060, 226},
    {I_VMOVLHPS, 2, {XMMREG,XMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+16068, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVLPD[] = {
    {I_VMOVLPD, 3, {XMM_L16,XMM_L16,MEMORY|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+29030, 184},
    {I_VMOVLPD, 2, {XMM_L16,MEMORY|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+29037, 184},
    {I_VMOVLPD, 2, {MEMORY|BITS64,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29044, 184},
    {I_VMOVLPD, 3, {XMMREG,XMMREG,MEMORY|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+16076, 226},
    {I_VMOVLPD, 2, {XMMREG,MEMORY|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+16084, 226},
    {I_VMOVLPD, 2, {MEMORY|BITS64,XMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+16092, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVLPS[] = {
    {I_VMOVLPS, 3, {XMM_L16,XMM_L16,MEMORY|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+28974, 184},
    {I_VMOVLPS, 2, {XMM_L16,MEMORY|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+28981, 184},
    {I_VMOVLPS, 2, {MEMORY|BITS64,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29051, 184},
    {I_VMOVLPS, 3, {XMMREG,XMMREG,MEMORY|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+16100, 226},
    {I_VMOVLPS, 2, {XMMREG,MEMORY|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+16108, 226},
    {I_VMOVLPS, 2, {MEMORY|BITS64,XMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+16116, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVMSKPD[] = {
    {I_VMOVMSKPD, 2, {REG_GPR|BITS64,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29058, 190},
    {I_VMOVMSKPD, 2, {REG_GPR|BITS32,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29058, 184},
    {I_VMOVMSKPD, 2, {REG_GPR|BITS64,YMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29065, 190},
    {I_VMOVMSKPD, 2, {REG_GPR|BITS32,YMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29065, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVMSKPS[] = {
    {I_VMOVMSKPS, 2, {REG_GPR|BITS64,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29072, 190},
    {I_VMOVMSKPS, 2, {REG_GPR|BITS32,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29072, 184},
    {I_VMOVMSKPS, 2, {REG_GPR|BITS64,YMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29079, 190},
    {I_VMOVMSKPS, 2, {REG_GPR|BITS32,YMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29079, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVNTDQ[] = {
    {I_VMOVNTDQ, 2, {MEMORY|BITS128,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29086, 184},
    {I_VMOVNTDQ, 2, {MEMORY|BITS256,YMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29093, 184},
    {I_VMOVNTDQ, 2, {MEMORY|BITS128,XMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+16124, 225},
    {I_VMOVNTDQ, 2, {MEMORY|BITS256,YMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+16132, 225},
    {I_VMOVNTDQ, 2, {MEMORY|BITS512,ZMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+16140, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVNTQQ[] = {
    {I_VMOVNTQQ, 2, {MEMORY|BITS256,YMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29093, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVNTDQA[] = {
    {I_VMOVNTDQA, 2, {XMM_L16,MEMORY|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29100, 184},
    {I_VMOVNTDQA, 2, {YMM_L16,MEMORY|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33853, 203},
    {I_VMOVNTDQA, 2, {XMMREG,MEMORY|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+16148, 225},
    {I_VMOVNTDQA, 2, {YMMREG,MEMORY|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+16156, 225},
    {I_VMOVNTDQA, 2, {ZMMREG,MEMORY|BITS512,0,0,0}, NO_DECORATOR, nasm_bytecodes+16164, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVNTPD[] = {
    {I_VMOVNTPD, 2, {MEMORY|BITS128,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29107, 184},
    {I_VMOVNTPD, 2, {MEMORY|BITS256,YMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29114, 184},
    {I_VMOVNTPD, 2, {MEMORY|BITS128,XMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+16172, 225},
    {I_VMOVNTPD, 2, {MEMORY|BITS256,YMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+16180, 225},
    {I_VMOVNTPD, 2, {MEMORY|BITS512,ZMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+16188, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVNTPS[] = {
    {I_VMOVNTPS, 2, {MEMORY|BITS128,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29121, 184},
    {I_VMOVNTPS, 2, {MEMORY|BITS256,YMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29128, 184},
    {I_VMOVNTPS, 2, {MEMORY|BITS128,XMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+16196, 225},
    {I_VMOVNTPS, 2, {MEMORY|BITS256,YMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+16204, 225},
    {I_VMOVNTPS, 2, {MEMORY|BITS512,ZMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+16212, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVSD[] = {
    {I_VMOVSD, 3, {XMM_L16,XMM_L16,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+29135, 184},
    {I_VMOVSD, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29142, 184},
    {I_VMOVSD, 2, {XMM_L16,MEMORY|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+29149, 184},
    {I_VMOVSD, 3, {XMM_L16,XMM_L16,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+29156, 184},
    {I_VMOVSD, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29163, 184},
    {I_VMOVSD, 2, {MEMORY|BITS64,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29170, 184},
    {I_VMOVSD, 2, {XMMREG,MEMORY|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16252, 226},
    {I_VMOVSD, 2, {MEMORY|BITS64,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+16260, 226},
    {I_VMOVSD, 3, {XMMREG,XMMREG,XMMREG,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16268, 226},
    {I_VMOVSD, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16276, 226},
    {I_VMOVSD, 3, {XMMREG,XMMREG,XMMREG,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16284, 226},
    {I_VMOVSD, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16292, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVSHDUP[] = {
    {I_VMOVSHDUP, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29177, 184},
    {I_VMOVSHDUP, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+29184, 184},
    {I_VMOVSHDUP, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16300, 225},
    {I_VMOVSHDUP, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16308, 225},
    {I_VMOVSHDUP, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16316, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVSLDUP[] = {
    {I_VMOVSLDUP, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29191, 184},
    {I_VMOVSLDUP, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+29198, 184},
    {I_VMOVSLDUP, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16324, 225},
    {I_VMOVSLDUP, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16332, 225},
    {I_VMOVSLDUP, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16340, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVSS[] = {
    {I_VMOVSS, 3, {XMM_L16,XMM_L16,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+29205, 184},
    {I_VMOVSS, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29212, 184},
    {I_VMOVSS, 2, {XMM_L16,MEMORY|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+29219, 184},
    {I_VMOVSS, 3, {XMM_L16,XMM_L16,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+29226, 184},
    {I_VMOVSS, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29233, 184},
    {I_VMOVSS, 2, {MEMORY|BITS32,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29240, 184},
    {I_VMOVSS, 2, {XMMREG,MEMORY|BITS32,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16348, 226},
    {I_VMOVSS, 2, {MEMORY|BITS32,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+16356, 226},
    {I_VMOVSS, 3, {XMMREG,XMMREG,XMMREG,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16364, 226},
    {I_VMOVSS, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16372, 226},
    {I_VMOVSS, 3, {XMMREG,XMMREG,XMMREG,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16380, 226},
    {I_VMOVSS, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16388, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVUPD[] = {
    {I_VMOVUPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29247, 184},
    {I_VMOVUPD, 2, {RM_XMM_L16|BITS128,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29254, 184},
    {I_VMOVUPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+29261, 184},
    {I_VMOVUPD, 2, {RM_YMM_L16|BITS256,YMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29268, 184},
    {I_VMOVUPD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16396, 225},
    {I_VMOVUPD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16404, 225},
    {I_VMOVUPD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16412, 226},
    {I_VMOVUPD, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16420, 225},
    {I_VMOVUPD, 2, {YMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16428, 225},
    {I_VMOVUPD, 2, {ZMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16436, 226},
    {I_VMOVUPD, 2, {MEMORY|BITS128,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+16444, 225},
    {I_VMOVUPD, 2, {MEMORY|BITS256,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+16452, 225},
    {I_VMOVUPD, 2, {MEMORY|BITS512,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+16460, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVUPS[] = {
    {I_VMOVUPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29275, 184},
    {I_VMOVUPS, 2, {RM_XMM_L16|BITS128,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29282, 184},
    {I_VMOVUPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+29289, 184},
    {I_VMOVUPS, 2, {RM_YMM_L16|BITS256,YMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+29296, 184},
    {I_VMOVUPS, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16468, 225},
    {I_VMOVUPS, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16476, 225},
    {I_VMOVUPS, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16484, 226},
    {I_VMOVUPS, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16492, 225},
    {I_VMOVUPS, 2, {YMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16500, 225},
    {I_VMOVUPS, 2, {ZMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16508, 226},
    {I_VMOVUPS, 2, {MEMORY|BITS128,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+16516, 225},
    {I_VMOVUPS, 2, {MEMORY|BITS256,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+16524, 225},
    {I_VMOVUPS, 2, {MEMORY|BITS512,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+16532, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMPSADBW[] = {
    {I_VMPSADBW, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9660, 184},
    {I_VMPSADBW, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9668, 184},
    {I_VMPSADBW, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+11844, 203},
    {I_VMPSADBW, 3, {YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+11852, 203},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMULPD[] = {
    {I_VMULPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29303, 184},
    {I_VMULPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29310, 184},
    {I_VMULPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+29317, 184},
    {I_VMULPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+29324, 184},
    {I_VMULPD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+16540, 225},
    {I_VMULPD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+16548, 225},
    {I_VMULPD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+16556, 225},
    {I_VMULPD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+16564, 225},
    {I_VMULPD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64|ER,0,0}, nasm_bytecodes+16572, 226},
    {I_VMULPD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64|ER,0,0,0}, nasm_bytecodes+16580, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMULPS[] = {
    {I_VMULPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29331, 184},
    {I_VMULPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29338, 184},
    {I_VMULPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+29345, 184},
    {I_VMULPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+29352, 184},
    {I_VMULPS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+16588, 225},
    {I_VMULPS, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+16596, 225},
    {I_VMULPS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+16604, 225},
    {I_VMULPS, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+16612, 225},
    {I_VMULPS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32|ER,0,0}, nasm_bytecodes+16620, 226},
    {I_VMULPS, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32|ER,0,0,0}, nasm_bytecodes+16628, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMULSD[] = {
    {I_VMULSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+29359, 184},
    {I_VMULSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+29366, 184},
    {I_VMULSD, 3, {XMMREG,XMMREG,RM_XMM|BITS64,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+16636, 226},
    {I_VMULSD, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,ER,0,0,0}, nasm_bytecodes+16644, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMULSS[] = {
    {I_VMULSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+29373, 184},
    {I_VMULSS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+29380, 184},
    {I_VMULSS, 3, {XMMREG,XMMREG,RM_XMM|BITS32,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+16652, 226},
    {I_VMULSS, 2, {XMMREG,RM_XMM|BITS32,0,0,0}, {MASK|Z,ER,0,0,0}, nasm_bytecodes+16660, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VORPD[] = {
    {I_VORPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29387, 184},
    {I_VORPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29394, 184},
    {I_VORPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+29401, 184},
    {I_VORPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+29408, 184},
    {I_VORPD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+16668, 227},
    {I_VORPD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+16676, 227},
    {I_VORPD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+16684, 227},
    {I_VORPD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+16692, 227},
    {I_VORPD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+16700, 228},
    {I_VORPD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+16708, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VORPS[] = {
    {I_VORPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29415, 184},
    {I_VORPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29422, 184},
    {I_VORPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+29429, 184},
    {I_VORPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+29436, 184},
    {I_VORPS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+16716, 227},
    {I_VORPS, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+16724, 227},
    {I_VORPS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+16732, 227},
    {I_VORPS, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+16740, 227},
    {I_VORPS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+16748, 228},
    {I_VORPS, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+16756, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPABSB[] = {
    {I_VPABSB, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29443, 184},
    {I_VPABSB, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32593, 203},
    {I_VPABSB, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16764, 229},
    {I_VPABSB, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16772, 229},
    {I_VPABSB, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16780, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPABSW[] = {
    {I_VPABSW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29450, 184},
    {I_VPABSW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32600, 203},
    {I_VPABSW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16836, 229},
    {I_VPABSW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16844, 229},
    {I_VPABSW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16852, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPABSD[] = {
    {I_VPABSD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29457, 184},
    {I_VPABSD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32607, 203},
    {I_VPABSD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+16788, 225},
    {I_VPABSD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+16796, 225},
    {I_VPABSD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+16804, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPACKSSWB[] = {
    {I_VPACKSSWB, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29464, 184},
    {I_VPACKSSWB, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29471, 184},
    {I_VPACKSSWB, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+32614, 203},
    {I_VPACKSSWB, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32621, 203},
    {I_VPACKSSWB, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16908, 229},
    {I_VPACKSSWB, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16916, 229},
    {I_VPACKSSWB, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16924, 229},
    {I_VPACKSSWB, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16932, 229},
    {I_VPACKSSWB, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16940, 230},
    {I_VPACKSSWB, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+16948, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPACKSSDW[] = {
    {I_VPACKSSDW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29478, 184},
    {I_VPACKSSDW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29485, 184},
    {I_VPACKSSDW, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+32628, 203},
    {I_VPACKSSDW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32635, 203},
    {I_VPACKSSDW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+16860, 229},
    {I_VPACKSSDW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+16868, 229},
    {I_VPACKSSDW, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+16876, 229},
    {I_VPACKSSDW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+16884, 229},
    {I_VPACKSSDW, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+16892, 230},
    {I_VPACKSSDW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+16900, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPACKUSWB[] = {
    {I_VPACKUSWB, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29492, 184},
    {I_VPACKUSWB, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29499, 184},
    {I_VPACKUSWB, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+32656, 203},
    {I_VPACKUSWB, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32663, 203},
    {I_VPACKUSWB, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17004, 229},
    {I_VPACKUSWB, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17012, 229},
    {I_VPACKUSWB, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17020, 229},
    {I_VPACKUSWB, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17028, 229},
    {I_VPACKUSWB, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17036, 230},
    {I_VPACKUSWB, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17044, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPACKUSDW[] = {
    {I_VPACKUSDW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29506, 184},
    {I_VPACKUSDW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29513, 184},
    {I_VPACKUSDW, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+32642, 203},
    {I_VPACKUSDW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32649, 203},
    {I_VPACKUSDW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+16956, 229},
    {I_VPACKUSDW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+16964, 229},
    {I_VPACKUSDW, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+16972, 229},
    {I_VPACKUSDW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+16980, 229},
    {I_VPACKUSDW, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+16988, 230},
    {I_VPACKUSDW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+16996, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPADDB[] = {
    {I_VPADDB, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29520, 184},
    {I_VPADDB, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29527, 184},
    {I_VPADDB, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+32670, 203},
    {I_VPADDB, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32677, 203},
    {I_VPADDB, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17052, 229},
    {I_VPADDB, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17060, 229},
    {I_VPADDB, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17068, 229},
    {I_VPADDB, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17076, 229},
    {I_VPADDB, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17084, 230},
    {I_VPADDB, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17092, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPADDW[] = {
    {I_VPADDW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29534, 184},
    {I_VPADDW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29541, 184},
    {I_VPADDW, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+32684, 203},
    {I_VPADDW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32691, 203},
    {I_VPADDW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17388, 229},
    {I_VPADDW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17396, 229},
    {I_VPADDW, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17404, 229},
    {I_VPADDW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17412, 229},
    {I_VPADDW, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17420, 230},
    {I_VPADDW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17428, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPADDD[] = {
    {I_VPADDD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29548, 184},
    {I_VPADDD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29555, 184},
    {I_VPADDD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+32698, 203},
    {I_VPADDD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32705, 203},
    {I_VPADDD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+17100, 225},
    {I_VPADDD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+17108, 225},
    {I_VPADDD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+17116, 225},
    {I_VPADDD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+17124, 225},
    {I_VPADDD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+17132, 226},
    {I_VPADDD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+17140, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPADDQ[] = {
    {I_VPADDQ, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29562, 184},
    {I_VPADDQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29569, 184},
    {I_VPADDQ, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+32712, 203},
    {I_VPADDQ, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32719, 203},
    {I_VPADDQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+17148, 225},
    {I_VPADDQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+17156, 225},
    {I_VPADDQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+17164, 225},
    {I_VPADDQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+17172, 225},
    {I_VPADDQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+17180, 226},
    {I_VPADDQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+17188, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPADDSB[] = {
    {I_VPADDSB, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29576, 184},
    {I_VPADDSB, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29583, 184},
    {I_VPADDSB, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+32726, 203},
    {I_VPADDSB, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32733, 203},
    {I_VPADDSB, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17196, 229},
    {I_VPADDSB, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17204, 229},
    {I_VPADDSB, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17212, 229},
    {I_VPADDSB, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17220, 229},
    {I_VPADDSB, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17228, 230},
    {I_VPADDSB, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17236, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPADDSW[] = {
    {I_VPADDSW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29590, 184},
    {I_VPADDSW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29597, 184},
    {I_VPADDSW, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+32740, 203},
    {I_VPADDSW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32747, 203},
    {I_VPADDSW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17244, 229},
    {I_VPADDSW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17252, 229},
    {I_VPADDSW, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17260, 229},
    {I_VPADDSW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17268, 229},
    {I_VPADDSW, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17276, 230},
    {I_VPADDSW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17284, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPADDUSB[] = {
    {I_VPADDUSB, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29604, 184},
    {I_VPADDUSB, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29611, 184},
    {I_VPADDUSB, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+32754, 203},
    {I_VPADDUSB, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32761, 203},
    {I_VPADDUSB, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17292, 229},
    {I_VPADDUSB, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17300, 229},
    {I_VPADDUSB, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17308, 229},
    {I_VPADDUSB, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17316, 229},
    {I_VPADDUSB, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17324, 230},
    {I_VPADDUSB, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17332, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPADDUSW[] = {
    {I_VPADDUSW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29618, 184},
    {I_VPADDUSW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29625, 184},
    {I_VPADDUSW, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+32768, 203},
    {I_VPADDUSW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32775, 203},
    {I_VPADDUSW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17340, 229},
    {I_VPADDUSW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17348, 229},
    {I_VPADDUSW, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17356, 229},
    {I_VPADDUSW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17364, 229},
    {I_VPADDUSW, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17372, 230},
    {I_VPADDUSW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17380, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPALIGNR[] = {
    {I_VPALIGNR, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9676, 184},
    {I_VPALIGNR, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9684, 184},
    {I_VPALIGNR, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+11860, 203},
    {I_VPALIGNR, 3, {YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+11868, 203},
    {I_VPALIGNR, 4, {XMMREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5190, 229},
    {I_VPALIGNR, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5199, 229},
    {I_VPALIGNR, 4, {YMMREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5208, 229},
    {I_VPALIGNR, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5217, 229},
    {I_VPALIGNR, 4, {ZMMREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5226, 230},
    {I_VPALIGNR, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5235, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPAND[] = {
    {I_VPAND, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29632, 184},
    {I_VPAND, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29639, 184},
    {I_VPAND, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+32782, 203},
    {I_VPAND, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32789, 203},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPANDN[] = {
    {I_VPANDN, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29646, 184},
    {I_VPANDN, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29653, 184},
    {I_VPANDN, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+32796, 203},
    {I_VPANDN, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32803, 203},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPAVGB[] = {
    {I_VPAVGB, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29660, 184},
    {I_VPAVGB, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29667, 184},
    {I_VPAVGB, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+32810, 203},
    {I_VPAVGB, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32817, 203},
    {I_VPAVGB, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17628, 229},
    {I_VPAVGB, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17636, 229},
    {I_VPAVGB, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17644, 229},
    {I_VPAVGB, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17652, 229},
    {I_VPAVGB, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17660, 230},
    {I_VPAVGB, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17668, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPAVGW[] = {
    {I_VPAVGW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29674, 184},
    {I_VPAVGW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29681, 184},
    {I_VPAVGW, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+32824, 203},
    {I_VPAVGW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32831, 203},
    {I_VPAVGW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17676, 229},
    {I_VPAVGW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17684, 229},
    {I_VPAVGW, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17692, 229},
    {I_VPAVGW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17700, 229},
    {I_VPAVGW, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17708, 230},
    {I_VPAVGW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17716, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPBLENDVB[] = {
    {I_VPBLENDVB, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+9692, 184},
    {I_VPBLENDVB, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+9700, 184},
    {I_VPBLENDVB, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0}, NO_DECORATOR, nasm_bytecodes+11876, 203},
    {I_VPBLENDVB, 3, {YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+11884, 203},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPBLENDW[] = {
    {I_VPBLENDW, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9708, 184},
    {I_VPBLENDW, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9716, 184},
    {I_VPBLENDW, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+11892, 203},
    {I_VPBLENDW, 3, {YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+11900, 203},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCMPESTRI[] = {
    {I_VPCMPESTRI, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9724, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCMPESTRM[] = {
    {I_VPCMPESTRM, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9732, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCMPISTRI[] = {
    {I_VPCMPISTRI, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9740, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCMPISTRM[] = {
    {I_VPCMPISTRM, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9748, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCMPEQB[] = {
    {I_VPCMPEQB, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29688, 184},
    {I_VPCMPEQB, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29695, 184},
    {I_VPCMPEQB, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+32838, 203},
    {I_VPCMPEQB, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32845, 203},
    {I_VPCMPEQB, 3, {KREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+18108, 229},
    {I_VPCMPEQB, 3, {KREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+18116, 229},
    {I_VPCMPEQB, 3, {KREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+18124, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCMPEQW[] = {
    {I_VPCMPEQW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29702, 184},
    {I_VPCMPEQW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29709, 184},
    {I_VPCMPEQW, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+32852, 203},
    {I_VPCMPEQW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32859, 203},
    {I_VPCMPEQW, 3, {KREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+18180, 229},
    {I_VPCMPEQW, 3, {KREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+18188, 229},
    {I_VPCMPEQW, 3, {KREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+18196, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCMPEQD[] = {
    {I_VPCMPEQD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29716, 184},
    {I_VPCMPEQD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29723, 184},
    {I_VPCMPEQD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+32866, 203},
    {I_VPCMPEQD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32873, 203},
    {I_VPCMPEQD, 3, {KREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK,0,B32,0,0}, nasm_bytecodes+18132, 225},
    {I_VPCMPEQD, 3, {KREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK,0,B32,0,0}, nasm_bytecodes+18140, 225},
    {I_VPCMPEQD, 3, {KREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK,0,B32,0,0}, nasm_bytecodes+18148, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCMPEQQ[] = {
    {I_VPCMPEQQ, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29730, 184},
    {I_VPCMPEQQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29737, 184},
    {I_VPCMPEQQ, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+32880, 203},
    {I_VPCMPEQQ, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32887, 203},
    {I_VPCMPEQQ, 3, {KREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK,0,B64,0,0}, nasm_bytecodes+18156, 225},
    {I_VPCMPEQQ, 3, {KREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK,0,B64,0,0}, nasm_bytecodes+18164, 225},
    {I_VPCMPEQQ, 3, {KREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK,0,B64,0,0}, nasm_bytecodes+18172, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCMPGTB[] = {
    {I_VPCMPGTB, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29744, 184},
    {I_VPCMPGTB, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29751, 184},
    {I_VPCMPGTB, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+32894, 203},
    {I_VPCMPGTB, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32901, 203},
    {I_VPCMPGTB, 3, {KREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+18204, 229},
    {I_VPCMPGTB, 3, {KREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+18212, 229},
    {I_VPCMPGTB, 3, {KREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+18220, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCMPGTW[] = {
    {I_VPCMPGTW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29758, 184},
    {I_VPCMPGTW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29765, 184},
    {I_VPCMPGTW, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+32908, 203},
    {I_VPCMPGTW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32915, 203},
    {I_VPCMPGTW, 3, {KREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+18276, 229},
    {I_VPCMPGTW, 3, {KREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+18284, 229},
    {I_VPCMPGTW, 3, {KREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+18292, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCMPGTD[] = {
    {I_VPCMPGTD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29772, 184},
    {I_VPCMPGTD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29779, 184},
    {I_VPCMPGTD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+32922, 203},
    {I_VPCMPGTD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32929, 203},
    {I_VPCMPGTD, 3, {KREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK,0,B32,0,0}, nasm_bytecodes+18228, 225},
    {I_VPCMPGTD, 3, {KREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK,0,B32,0,0}, nasm_bytecodes+18236, 225},
    {I_VPCMPGTD, 3, {KREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK,0,B32,0,0}, nasm_bytecodes+18244, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCMPGTQ[] = {
    {I_VPCMPGTQ, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29786, 184},
    {I_VPCMPGTQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29793, 184},
    {I_VPCMPGTQ, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+32936, 203},
    {I_VPCMPGTQ, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32943, 203},
    {I_VPCMPGTQ, 3, {KREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK,0,B64,0,0}, nasm_bytecodes+18252, 225},
    {I_VPCMPGTQ, 3, {KREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK,0,B64,0,0}, nasm_bytecodes+18260, 225},
    {I_VPCMPGTQ, 3, {KREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK,0,B64,0,0}, nasm_bytecodes+18268, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPERMILPD[] = {
    {I_VPERMILPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29800, 184},
    {I_VPERMILPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29807, 184},
    {I_VPERMILPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+29814, 184},
    {I_VPERMILPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+29821, 184},
    {I_VPERMILPD, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9756, 184},
    {I_VPERMILPD, 3, {YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9764, 184},
    {I_VPERMILPD, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+5460, 225},
    {I_VPERMILPD, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+5469, 225},
    {I_VPERMILPD, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+5478, 226},
    {I_VPERMILPD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+18668, 225},
    {I_VPERMILPD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+18676, 225},
    {I_VPERMILPD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+18684, 225},
    {I_VPERMILPD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+18692, 225},
    {I_VPERMILPD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+18700, 226},
    {I_VPERMILPD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+18708, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPERMILPS[] = {
    {I_VPERMILPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29828, 184},
    {I_VPERMILPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29835, 184},
    {I_VPERMILPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+29842, 184},
    {I_VPERMILPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+29849, 184},
    {I_VPERMILPS, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9772, 184},
    {I_VPERMILPS, 3, {YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9780, 184},
    {I_VPERMILPS, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+5487, 225},
    {I_VPERMILPS, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+5496, 225},
    {I_VPERMILPS, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+5505, 226},
    {I_VPERMILPS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+18716, 225},
    {I_VPERMILPS, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+18724, 225},
    {I_VPERMILPS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+18732, 225},
    {I_VPERMILPS, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+18740, 225},
    {I_VPERMILPS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+18748, 226},
    {I_VPERMILPS, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+18756, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPERM2F128[] = {
    {I_VPERM2F128, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9788, 184},
    {I_VPERM2F128, 3, {YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9796, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPEXTRB[] = {
    {I_VPEXTRB, 3, {REG_GPR|BITS64,XMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9804, 190},
    {I_VPEXTRB, 3, {REG_GPR|BITS32,XMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9804, 184},
    {I_VPEXTRB, 3, {MEMORY|BITS8,XMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9804, 184},
    {I_VPEXTRB, 3, {REG_GPR|BITS8,XMMREG,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+5550, 230},
    {I_VPEXTRB, 3, {REG_GPR|BITS16,XMMREG,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+5550, 230},
    {I_VPEXTRB, 3, {REG_GPR|BITS32,XMMREG,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+5550, 230},
    {I_VPEXTRB, 3, {REG_GPR|BITS64,XMMREG,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+5550, 230},
    {I_VPEXTRB, 3, {MEMORY|BITS8,XMMREG,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+5550, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPEXTRW[] = {
    {I_VPEXTRW, 3, {REG_GPR|BITS64,XMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9812, 190},
    {I_VPEXTRW, 3, {REG_GPR|BITS32,XMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9812, 184},
    {I_VPEXTRW, 3, {REG_GPR|BITS64,XMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9820, 190},
    {I_VPEXTRW, 3, {REG_GPR|BITS32,XMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9820, 184},
    {I_VPEXTRW, 3, {MEMORY|BITS16,XMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9820, 184},
    {I_VPEXTRW, 3, {REG_GPR|BITS16,XMMREG,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+5577, 230},
    {I_VPEXTRW, 3, {REG_GPR|BITS32,XMMREG,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+5577, 230},
    {I_VPEXTRW, 3, {REG_GPR|BITS64,XMMREG,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+5577, 230},
    {I_VPEXTRW, 3, {MEMORY|BITS16,XMMREG,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+5577, 230},
    {I_VPEXTRW, 3, {REG_GPR|BITS16,XMMREG,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+5586, 230},
    {I_VPEXTRW, 3, {REG_GPR|BITS32,XMMREG,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+5586, 230},
    {I_VPEXTRW, 3, {REG_GPR|BITS64,XMMREG,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+5586, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPEXTRD[] = {
    {I_VPEXTRD, 3, {REG_GPR|BITS64,XMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9828, 190},
    {I_VPEXTRD, 3, {RM_GPR|BITS32,XMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9828, 184},
    {I_VPEXTRD, 3, {RM_GPR|BITS32,XMMREG,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+5559, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPEXTRQ[] = {
    {I_VPEXTRQ, 3, {RM_GPR|BITS64,XMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9836, 190},
    {I_VPEXTRQ, 3, {RM_GPR|BITS64,XMMREG,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+5568, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPHADDW[] = {
    {I_VPHADDW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29856, 184},
    {I_VPHADDW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29863, 184},
    {I_VPHADDW, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+32950, 203},
    {I_VPHADDW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32957, 203},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPHADDD[] = {
    {I_VPHADDD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29870, 184},
    {I_VPHADDD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29877, 184},
    {I_VPHADDD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+32964, 203},
    {I_VPHADDD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32971, 203},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPHADDSW[] = {
    {I_VPHADDSW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29884, 184},
    {I_VPHADDSW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29891, 184},
    {I_VPHADDSW, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+32978, 203},
    {I_VPHADDSW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32985, 203},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPHMINPOSUW[] = {
    {I_VPHMINPOSUW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29898, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPHSUBW[] = {
    {I_VPHSUBW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29905, 184},
    {I_VPHSUBW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29912, 184},
    {I_VPHSUBW, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+32992, 203},
    {I_VPHSUBW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32999, 203},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPHSUBD[] = {
    {I_VPHSUBD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29919, 184},
    {I_VPHSUBD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29926, 184},
    {I_VPHSUBD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33006, 203},
    {I_VPHSUBD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33013, 203},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPHSUBSW[] = {
    {I_VPHSUBSW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29933, 184},
    {I_VPHSUBSW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29940, 184},
    {I_VPHSUBSW, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33020, 203},
    {I_VPHSUBSW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33027, 203},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPINSRB[] = {
    {I_VPINSRB, 4, {XMM_L16,XMM_L16,MEMORY|BITS8,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9844, 184},
    {I_VPINSRB, 3, {XMM_L16,MEMORY|BITS8,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9852, 184},
    {I_VPINSRB, 4, {XMM_L16,XMM_L16,RM_GPR|BITS8,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9844, 184},
    {I_VPINSRB, 3, {XMM_L16,RM_GPR|BITS8,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9852, 184},
    {I_VPINSRB, 4, {XMM_L16,XMM_L16,REG_GPR|BITS32,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9844, 184},
    {I_VPINSRB, 3, {XMM_L16,REG_GPR|BITS32,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9852, 184},
    {I_VPINSRB, 4, {XMMREG,XMMREG,REG_GPR|BITS32,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+5703, 230},
    {I_VPINSRB, 3, {XMMREG,REG_GPR|BITS32,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+5712, 230},
    {I_VPINSRB, 4, {XMMREG,XMMREG,MEMORY|BITS8,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+5703, 230},
    {I_VPINSRB, 3, {XMMREG,MEMORY|BITS8,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+5712, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPINSRW[] = {
    {I_VPINSRW, 4, {XMM_L16,XMM_L16,MEMORY|BITS16,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9860, 184},
    {I_VPINSRW, 3, {XMM_L16,MEMORY|BITS16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9868, 184},
    {I_VPINSRW, 4, {XMM_L16,XMM_L16,RM_GPR|BITS16,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9860, 184},
    {I_VPINSRW, 3, {XMM_L16,RM_GPR|BITS16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9868, 184},
    {I_VPINSRW, 4, {XMM_L16,XMM_L16,REG_GPR|BITS32,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9860, 184},
    {I_VPINSRW, 3, {XMM_L16,REG_GPR|BITS32,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9868, 184},
    {I_VPINSRW, 4, {XMMREG,XMMREG,REG_GPR|BITS32,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+5757, 230},
    {I_VPINSRW, 3, {XMMREG,REG_GPR|BITS32,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+5766, 230},
    {I_VPINSRW, 4, {XMMREG,XMMREG,MEMORY|BITS16,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+5757, 230},
    {I_VPINSRW, 3, {XMMREG,MEMORY|BITS16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+5766, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPINSRD[] = {
    {I_VPINSRD, 4, {XMM_L16,XMM_L16,MEMORY|BITS32,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9876, 184},
    {I_VPINSRD, 3, {XMM_L16,MEMORY|BITS32,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9884, 184},
    {I_VPINSRD, 4, {XMM_L16,XMM_L16,RM_GPR|BITS32,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9876, 184},
    {I_VPINSRD, 3, {XMM_L16,RM_GPR|BITS32,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9884, 184},
    {I_VPINSRD, 4, {XMMREG,XMMREG,RM_GPR|BITS32,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+5721, 228},
    {I_VPINSRD, 3, {XMMREG,RM_GPR|BITS32,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+5730, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPINSRQ[] = {
    {I_VPINSRQ, 4, {XMM_L16,XMM_L16,MEMORY|BITS64,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9892, 190},
    {I_VPINSRQ, 3, {XMM_L16,MEMORY|BITS64,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9900, 190},
    {I_VPINSRQ, 4, {XMM_L16,XMM_L16,RM_GPR|BITS64,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+9892, 190},
    {I_VPINSRQ, 3, {XMM_L16,RM_GPR|BITS64,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9900, 190},
    {I_VPINSRQ, 4, {XMMREG,XMMREG,RM_GPR|BITS64,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+5739, 228},
    {I_VPINSRQ, 3, {XMMREG,RM_GPR|BITS64,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+5748, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMADDWD[] = {
    {I_VPMADDWD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29947, 184},
    {I_VPMADDWD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29954, 184},
    {I_VPMADDWD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33048, 203},
    {I_VPMADDWD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33055, 203},
    {I_VPMADDWD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19244, 229},
    {I_VPMADDWD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19252, 229},
    {I_VPMADDWD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19260, 229},
    {I_VPMADDWD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19268, 229},
    {I_VPMADDWD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19276, 230},
    {I_VPMADDWD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19284, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMADDUBSW[] = {
    {I_VPMADDUBSW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29961, 184},
    {I_VPMADDUBSW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29968, 184},
    {I_VPMADDUBSW, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33034, 203},
    {I_VPMADDUBSW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33041, 203},
    {I_VPMADDUBSW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19196, 229},
    {I_VPMADDUBSW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19204, 229},
    {I_VPMADDUBSW, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19212, 229},
    {I_VPMADDUBSW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19220, 229},
    {I_VPMADDUBSW, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19228, 230},
    {I_VPMADDUBSW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19236, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMAXSB[] = {
    {I_VPMAXSB, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29975, 184},
    {I_VPMAXSB, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29982, 184},
    {I_VPMAXSB, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33062, 203},
    {I_VPMAXSB, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33069, 203},
    {I_VPMAXSB, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19292, 229},
    {I_VPMAXSB, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19300, 229},
    {I_VPMAXSB, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19308, 229},
    {I_VPMAXSB, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19316, 229},
    {I_VPMAXSB, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19324, 230},
    {I_VPMAXSB, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19332, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMAXSW[] = {
    {I_VPMAXSW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+29989, 184},
    {I_VPMAXSW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+29996, 184},
    {I_VPMAXSW, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33076, 203},
    {I_VPMAXSW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33083, 203},
    {I_VPMAXSW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19436, 229},
    {I_VPMAXSW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19444, 229},
    {I_VPMAXSW, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19452, 229},
    {I_VPMAXSW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19460, 229},
    {I_VPMAXSW, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19468, 230},
    {I_VPMAXSW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19476, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMAXSD[] = {
    {I_VPMAXSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30003, 184},
    {I_VPMAXSD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30010, 184},
    {I_VPMAXSD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33090, 203},
    {I_VPMAXSD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33097, 203},
    {I_VPMAXSD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+19340, 225},
    {I_VPMAXSD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+19348, 225},
    {I_VPMAXSD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+19356, 225},
    {I_VPMAXSD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+19364, 225},
    {I_VPMAXSD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+19372, 226},
    {I_VPMAXSD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+19380, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMAXUB[] = {
    {I_VPMAXUB, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30017, 184},
    {I_VPMAXUB, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30024, 184},
    {I_VPMAXUB, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33104, 203},
    {I_VPMAXUB, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33111, 203},
    {I_VPMAXUB, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19484, 229},
    {I_VPMAXUB, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19492, 229},
    {I_VPMAXUB, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19500, 229},
    {I_VPMAXUB, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19508, 229},
    {I_VPMAXUB, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19516, 230},
    {I_VPMAXUB, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19524, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMAXUW[] = {
    {I_VPMAXUW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30031, 184},
    {I_VPMAXUW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30038, 184},
    {I_VPMAXUW, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33118, 203},
    {I_VPMAXUW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33125, 203},
    {I_VPMAXUW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19628, 229},
    {I_VPMAXUW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19636, 229},
    {I_VPMAXUW, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19644, 229},
    {I_VPMAXUW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19652, 229},
    {I_VPMAXUW, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19660, 230},
    {I_VPMAXUW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19668, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMAXUD[] = {
    {I_VPMAXUD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30045, 184},
    {I_VPMAXUD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30052, 184},
    {I_VPMAXUD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33132, 203},
    {I_VPMAXUD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33139, 203},
    {I_VPMAXUD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+19532, 225},
    {I_VPMAXUD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+19540, 225},
    {I_VPMAXUD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+19548, 225},
    {I_VPMAXUD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+19556, 225},
    {I_VPMAXUD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+19564, 226},
    {I_VPMAXUD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+19572, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMINSB[] = {
    {I_VPMINSB, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30059, 184},
    {I_VPMINSB, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30066, 184},
    {I_VPMINSB, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33146, 203},
    {I_VPMINSB, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33153, 203},
    {I_VPMINSB, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19676, 229},
    {I_VPMINSB, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19684, 229},
    {I_VPMINSB, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19692, 229},
    {I_VPMINSB, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19700, 229},
    {I_VPMINSB, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19708, 230},
    {I_VPMINSB, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19716, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMINSW[] = {
    {I_VPMINSW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30073, 184},
    {I_VPMINSW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30080, 184},
    {I_VPMINSW, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33160, 203},
    {I_VPMINSW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33167, 203},
    {I_VPMINSW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19820, 229},
    {I_VPMINSW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19828, 229},
    {I_VPMINSW, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19836, 229},
    {I_VPMINSW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19844, 229},
    {I_VPMINSW, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19852, 230},
    {I_VPMINSW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19860, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMINSD[] = {
    {I_VPMINSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30087, 184},
    {I_VPMINSD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30094, 184},
    {I_VPMINSD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33174, 203},
    {I_VPMINSD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33181, 203},
    {I_VPMINSD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+19724, 225},
    {I_VPMINSD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+19732, 225},
    {I_VPMINSD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+19740, 225},
    {I_VPMINSD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+19748, 225},
    {I_VPMINSD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+19756, 226},
    {I_VPMINSD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+19764, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMINUB[] = {
    {I_VPMINUB, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30101, 184},
    {I_VPMINUB, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30108, 184},
    {I_VPMINUB, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33188, 203},
    {I_VPMINUB, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33195, 203},
    {I_VPMINUB, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19868, 229},
    {I_VPMINUB, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19876, 229},
    {I_VPMINUB, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19884, 229},
    {I_VPMINUB, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19892, 229},
    {I_VPMINUB, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19900, 230},
    {I_VPMINUB, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19908, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMINUW[] = {
    {I_VPMINUW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30115, 184},
    {I_VPMINUW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30122, 184},
    {I_VPMINUW, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33202, 203},
    {I_VPMINUW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33209, 203},
    {I_VPMINUW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20012, 229},
    {I_VPMINUW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20020, 229},
    {I_VPMINUW, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20028, 229},
    {I_VPMINUW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20036, 229},
    {I_VPMINUW, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20044, 230},
    {I_VPMINUW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20052, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMINUD[] = {
    {I_VPMINUD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30129, 184},
    {I_VPMINUD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30136, 184},
    {I_VPMINUD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33216, 203},
    {I_VPMINUD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33223, 203},
    {I_VPMINUD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+19916, 225},
    {I_VPMINUD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+19924, 225},
    {I_VPMINUD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+19932, 225},
    {I_VPMINUD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+19940, 225},
    {I_VPMINUD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+19948, 226},
    {I_VPMINUD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+19956, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVMSKB[] = {
    {I_VPMOVMSKB, 2, {REG_GPR|BITS64,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+30143, 190},
    {I_VPMOVMSKB, 2, {REG_GPR|BITS32,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+30143, 184},
    {I_VPMOVMSKB, 2, {REG_GPR|BITS32,YMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+33230, 203},
    {I_VPMOVMSKB, 2, {REG_GPR|BITS64,YMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+33230, 203},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVSXBW[] = {
    {I_VPMOVSXBW, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+30150, 184},
    {I_VPMOVSXBW, 2, {YMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+33237, 203},
    {I_VPMOVSXBW, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20804, 229},
    {I_VPMOVSXBW, 2, {YMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20812, 229},
    {I_VPMOVSXBW, 2, {ZMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20820, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVSXBD[] = {
    {I_VPMOVSXBD, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+30157, 184},
    {I_VPMOVSXBD, 2, {YMM_L16,MEMORY|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+33244, 203},
    {I_VPMOVSXBD, 2, {YMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+33244, 203},
    {I_VPMOVSXBD, 2, {XMMREG,RM_XMM|BITS32,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20756, 225},
    {I_VPMOVSXBD, 2, {YMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20764, 225},
    {I_VPMOVSXBD, 2, {ZMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20772, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVSXBQ[] = {
    {I_VPMOVSXBQ, 2, {XMM_L16,RM_XMM_L16|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+30164, 184},
    {I_VPMOVSXBQ, 2, {YMM_L16,MEMORY|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+33251, 203},
    {I_VPMOVSXBQ, 2, {YMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+33251, 203},
    {I_VPMOVSXBQ, 2, {XMMREG,RM_XMM|BITS16,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20780, 225},
    {I_VPMOVSXBQ, 2, {YMMREG,RM_XMM|BITS32,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20788, 225},
    {I_VPMOVSXBQ, 2, {ZMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20796, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVSXWD[] = {
    {I_VPMOVSXWD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+30171, 184},
    {I_VPMOVSXWD, 2, {YMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+33258, 203},
    {I_VPMOVSXWD, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20852, 225},
    {I_VPMOVSXWD, 2, {YMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20860, 225},
    {I_VPMOVSXWD, 2, {ZMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20868, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVSXWQ[] = {
    {I_VPMOVSXWQ, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+30178, 184},
    {I_VPMOVSXWQ, 2, {YMM_L16,MEMORY|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+33265, 203},
    {I_VPMOVSXWQ, 2, {YMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+33265, 203},
    {I_VPMOVSXWQ, 2, {XMMREG,RM_XMM|BITS32,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20876, 225},
    {I_VPMOVSXWQ, 2, {YMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20884, 225},
    {I_VPMOVSXWQ, 2, {ZMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20892, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVSXDQ[] = {
    {I_VPMOVSXDQ, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+30185, 184},
    {I_VPMOVSXDQ, 2, {YMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+33272, 203},
    {I_VPMOVSXDQ, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20828, 225},
    {I_VPMOVSXDQ, 2, {YMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20836, 225},
    {I_VPMOVSXDQ, 2, {ZMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20844, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVZXBW[] = {
    {I_VPMOVZXBW, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+30192, 184},
    {I_VPMOVZXBW, 2, {YMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+33279, 203},
    {I_VPMOVZXBW, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21308, 229},
    {I_VPMOVZXBW, 2, {YMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21316, 229},
    {I_VPMOVZXBW, 2, {ZMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21324, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVZXBD[] = {
    {I_VPMOVZXBD, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+30199, 184},
    {I_VPMOVZXBD, 2, {YMM_L16,MEMORY|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+33286, 203},
    {I_VPMOVZXBD, 2, {YMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+33286, 203},
    {I_VPMOVZXBD, 2, {XMMREG,RM_XMM|BITS32,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21260, 225},
    {I_VPMOVZXBD, 2, {YMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21268, 225},
    {I_VPMOVZXBD, 2, {ZMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21276, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVZXBQ[] = {
    {I_VPMOVZXBQ, 2, {XMM_L16,RM_XMM_L16|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+30206, 184},
    {I_VPMOVZXBQ, 2, {YMM_L16,MEMORY|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+33293, 203},
    {I_VPMOVZXBQ, 2, {YMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+33293, 203},
    {I_VPMOVZXBQ, 2, {XMMREG,RM_XMM|BITS16,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21284, 225},
    {I_VPMOVZXBQ, 2, {YMMREG,RM_XMM|BITS32,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21292, 225},
    {I_VPMOVZXBQ, 2, {ZMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21300, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVZXWD[] = {
    {I_VPMOVZXWD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+30213, 184},
    {I_VPMOVZXWD, 2, {YMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+33300, 203},
    {I_VPMOVZXWD, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21356, 225},
    {I_VPMOVZXWD, 2, {YMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21364, 225},
    {I_VPMOVZXWD, 2, {ZMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21372, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVZXWQ[] = {
    {I_VPMOVZXWQ, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+30220, 184},
    {I_VPMOVZXWQ, 2, {YMM_L16,MEMORY|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+33307, 203},
    {I_VPMOVZXWQ, 2, {YMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+33307, 203},
    {I_VPMOVZXWQ, 2, {XMMREG,RM_XMM|BITS32,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21380, 225},
    {I_VPMOVZXWQ, 2, {YMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21388, 225},
    {I_VPMOVZXWQ, 2, {ZMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21396, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVZXDQ[] = {
    {I_VPMOVZXDQ, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+30227, 184},
    {I_VPMOVZXDQ, 2, {YMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+33314, 203},
    {I_VPMOVZXDQ, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21332, 225},
    {I_VPMOVZXDQ, 2, {YMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21340, 225},
    {I_VPMOVZXDQ, 2, {ZMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21348, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMULHUW[] = {
    {I_VPMULHUW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30234, 184},
    {I_VPMULHUW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30241, 184},
    {I_VPMULHUW, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33349, 203},
    {I_VPMULHUW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33356, 203},
    {I_VPMULHUW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21500, 229},
    {I_VPMULHUW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21508, 229},
    {I_VPMULHUW, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21516, 229},
    {I_VPMULHUW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21524, 229},
    {I_VPMULHUW, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21532, 230},
    {I_VPMULHUW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21540, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMULHRSW[] = {
    {I_VPMULHRSW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30248, 184},
    {I_VPMULHRSW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30255, 184},
    {I_VPMULHRSW, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33335, 203},
    {I_VPMULHRSW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33342, 203},
    {I_VPMULHRSW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21452, 229},
    {I_VPMULHRSW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21460, 229},
    {I_VPMULHRSW, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21468, 229},
    {I_VPMULHRSW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21476, 229},
    {I_VPMULHRSW, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21484, 230},
    {I_VPMULHRSW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21492, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMULHW[] = {
    {I_VPMULHW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30262, 184},
    {I_VPMULHW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30269, 184},
    {I_VPMULHW, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33363, 203},
    {I_VPMULHW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33370, 203},
    {I_VPMULHW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21548, 229},
    {I_VPMULHW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21556, 229},
    {I_VPMULHW, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21564, 229},
    {I_VPMULHW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21572, 229},
    {I_VPMULHW, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21580, 230},
    {I_VPMULHW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21588, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMULLW[] = {
    {I_VPMULLW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30276, 184},
    {I_VPMULLW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30283, 184},
    {I_VPMULLW, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33377, 203},
    {I_VPMULLW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33384, 203},
    {I_VPMULLW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21692, 229},
    {I_VPMULLW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21700, 229},
    {I_VPMULLW, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21708, 229},
    {I_VPMULLW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21716, 229},
    {I_VPMULLW, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21724, 230},
    {I_VPMULLW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21732, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMULLD[] = {
    {I_VPMULLD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30290, 184},
    {I_VPMULLD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30297, 184},
    {I_VPMULLD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33391, 203},
    {I_VPMULLD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33398, 203},
    {I_VPMULLD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+21596, 225},
    {I_VPMULLD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+21604, 225},
    {I_VPMULLD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+21612, 225},
    {I_VPMULLD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+21620, 225},
    {I_VPMULLD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+21628, 226},
    {I_VPMULLD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+21636, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMULUDQ[] = {
    {I_VPMULUDQ, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30304, 184},
    {I_VPMULUDQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30311, 184},
    {I_VPMULUDQ, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33405, 203},
    {I_VPMULUDQ, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33412, 203},
    {I_VPMULUDQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+21788, 225},
    {I_VPMULUDQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+21796, 225},
    {I_VPMULUDQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+21804, 225},
    {I_VPMULUDQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+21812, 225},
    {I_VPMULUDQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+21820, 226},
    {I_VPMULUDQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+21828, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMULDQ[] = {
    {I_VPMULDQ, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30318, 184},
    {I_VPMULDQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30325, 184},
    {I_VPMULDQ, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33321, 203},
    {I_VPMULDQ, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33328, 203},
    {I_VPMULDQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+21404, 225},
    {I_VPMULDQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+21412, 225},
    {I_VPMULDQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+21420, 225},
    {I_VPMULDQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+21428, 225},
    {I_VPMULDQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+21436, 226},
    {I_VPMULDQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+21444, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPOR[] = {
    {I_VPOR, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30332, 184},
    {I_VPOR, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30339, 184},
    {I_VPOR, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33419, 203},
    {I_VPOR, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33426, 203},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSADBW[] = {
    {I_VPSADBW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30346, 184},
    {I_VPSADBW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30353, 184},
    {I_VPSADBW, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33433, 203},
    {I_VPSADBW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33440, 203},
    {I_VPSADBW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+22124, 229},
    {I_VPSADBW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+22132, 229},
    {I_VPSADBW, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+22140, 229},
    {I_VPSADBW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+22148, 229},
    {I_VPSADBW, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, NO_DECORATOR, nasm_bytecodes+22156, 230},
    {I_VPSADBW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, NO_DECORATOR, nasm_bytecodes+22164, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSHUFB[] = {
    {I_VPSHUFB, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30360, 184},
    {I_VPSHUFB, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30367, 184},
    {I_VPSHUFB, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33447, 203},
    {I_VPSHUFB, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33454, 203},
    {I_VPSHUFB, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22172, 229},
    {I_VPSHUFB, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22180, 229},
    {I_VPSHUFB, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22188, 229},
    {I_VPSHUFB, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22196, 229},
    {I_VPSHUFB, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22204, 230},
    {I_VPSHUFB, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22212, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSHUFD[] = {
    {I_VPSHUFD, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9908, 184},
    {I_VPSHUFD, 3, {YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+11908, 203},
    {I_VPSHUFD, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+6099, 225},
    {I_VPSHUFD, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+6108, 225},
    {I_VPSHUFD, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+6117, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSHUFHW[] = {
    {I_VPSHUFHW, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9916, 184},
    {I_VPSHUFHW, 3, {YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+11916, 203},
    {I_VPSHUFHW, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6126, 229},
    {I_VPSHUFHW, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6135, 229},
    {I_VPSHUFHW, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6144, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSHUFLW[] = {
    {I_VPSHUFLW, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9924, 184},
    {I_VPSHUFLW, 3, {YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+11924, 203},
    {I_VPSHUFLW, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6153, 229},
    {I_VPSHUFLW, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6162, 229},
    {I_VPSHUFLW, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6171, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSIGNB[] = {
    {I_VPSIGNB, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30374, 184},
    {I_VPSIGNB, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30381, 184},
    {I_VPSIGNB, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33461, 203},
    {I_VPSIGNB, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33468, 203},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSIGNW[] = {
    {I_VPSIGNW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30388, 184},
    {I_VPSIGNW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30395, 184},
    {I_VPSIGNW, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33475, 203},
    {I_VPSIGNW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33482, 203},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSIGND[] = {
    {I_VPSIGND, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30402, 184},
    {I_VPSIGND, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30409, 184},
    {I_VPSIGND, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33489, 203},
    {I_VPSIGND, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33496, 203},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSLLDQ[] = {
    {I_VPSLLDQ, 3, {XMM_L16,XMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9932, 184},
    {I_VPSLLDQ, 2, {XMM_L16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+9940, 184},
    {I_VPSLLDQ, 3, {YMM_L16,YMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+11932, 203},
    {I_VPSLLDQ, 2, {YMM_L16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+11940, 203},
    {I_VPSLLDQ, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+6234, 229},
    {I_VPSLLDQ, 2, {XMMREG,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+6243, 229},
    {I_VPSLLDQ, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+6252, 229},
    {I_VPSLLDQ, 2, {YMMREG,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+6261, 229},
    {I_VPSLLDQ, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+6270, 230},
    {I_VPSLLDQ, 2, {ZMMREG,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+6279, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSRLDQ[] = {
    {I_VPSRLDQ, 3, {XMM_L16,XMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9948, 184},
    {I_VPSRLDQ, 2, {XMM_L16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+9956, 184},
    {I_VPSRLDQ, 3, {YMM_L16,YMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+12028, 203},
    {I_VPSRLDQ, 2, {YMM_L16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+12036, 203},
    {I_VPSRLDQ, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+6612, 229},
    {I_VPSRLDQ, 2, {XMMREG,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+6621, 229},
    {I_VPSRLDQ, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+6630, 229},
    {I_VPSRLDQ, 2, {YMMREG,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+6639, 229},
    {I_VPSRLDQ, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+6648, 230},
    {I_VPSRLDQ, 2, {ZMMREG,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+6657, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSLLW[] = {
    {I_VPSLLW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30416, 184},
    {I_VPSLLW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30423, 184},
    {I_VPSLLW, 3, {XMM_L16,XMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9964, 184},
    {I_VPSLLW, 2, {XMM_L16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+9972, 184},
    {I_VPSLLW, 3, {YMM_L16,YMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+33503, 203},
    {I_VPSLLW, 2, {YMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+33510, 203},
    {I_VPSLLW, 3, {YMM_L16,YMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+11948, 203},
    {I_VPSLLW, 2, {YMM_L16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+11956, 203},
    {I_VPSLLW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22460, 229},
    {I_VPSLLW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22468, 229},
    {I_VPSLLW, 3, {YMMREG,YMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22476, 229},
    {I_VPSLLW, 2, {YMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22484, 229},
    {I_VPSLLW, 3, {ZMMREG,ZMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22492, 230},
    {I_VPSLLW, 2, {ZMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22500, 230},
    {I_VPSLLW, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6342, 229},
    {I_VPSLLW, 2, {XMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6351, 229},
    {I_VPSLLW, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6360, 229},
    {I_VPSLLW, 2, {YMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6369, 229},
    {I_VPSLLW, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6378, 230},
    {I_VPSLLW, 2, {ZMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6387, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSLLD[] = {
    {I_VPSLLD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30430, 184},
    {I_VPSLLD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30437, 184},
    {I_VPSLLD, 3, {XMM_L16,XMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9980, 184},
    {I_VPSLLD, 2, {XMM_L16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+9988, 184},
    {I_VPSLLD, 3, {YMM_L16,YMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+33517, 203},
    {I_VPSLLD, 2, {YMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+33524, 203},
    {I_VPSLLD, 3, {YMM_L16,YMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+11964, 203},
    {I_VPSLLD, 2, {YMM_L16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+11972, 203},
    {I_VPSLLD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22220, 225},
    {I_VPSLLD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22228, 225},
    {I_VPSLLD, 3, {YMMREG,YMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22236, 225},
    {I_VPSLLD, 2, {YMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22244, 225},
    {I_VPSLLD, 3, {ZMMREG,ZMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22252, 226},
    {I_VPSLLD, 2, {ZMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22260, 226},
    {I_VPSLLD, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+6180, 225},
    {I_VPSLLD, 2, {XMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6189, 225},
    {I_VPSLLD, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+6198, 225},
    {I_VPSLLD, 2, {YMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6207, 225},
    {I_VPSLLD, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+6216, 226},
    {I_VPSLLD, 2, {ZMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6225, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSLLQ[] = {
    {I_VPSLLQ, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30444, 184},
    {I_VPSLLQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30451, 184},
    {I_VPSLLQ, 3, {XMM_L16,XMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+9996, 184},
    {I_VPSLLQ, 2, {XMM_L16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+10004, 184},
    {I_VPSLLQ, 3, {YMM_L16,YMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+33531, 203},
    {I_VPSLLQ, 2, {YMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+33538, 203},
    {I_VPSLLQ, 3, {YMM_L16,YMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+11980, 203},
    {I_VPSLLQ, 2, {YMM_L16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+11988, 203},
    {I_VPSLLQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22268, 225},
    {I_VPSLLQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22276, 225},
    {I_VPSLLQ, 3, {YMMREG,YMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22284, 225},
    {I_VPSLLQ, 2, {YMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22292, 225},
    {I_VPSLLQ, 3, {ZMMREG,ZMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22300, 226},
    {I_VPSLLQ, 2, {ZMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22308, 226},
    {I_VPSLLQ, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+6288, 225},
    {I_VPSLLQ, 2, {XMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6297, 225},
    {I_VPSLLQ, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+6306, 225},
    {I_VPSLLQ, 2, {YMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6315, 225},
    {I_VPSLLQ, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+6324, 226},
    {I_VPSLLQ, 2, {ZMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6333, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSRAW[] = {
    {I_VPSRAW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30458, 184},
    {I_VPSRAW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30465, 184},
    {I_VPSRAW, 3, {XMM_L16,XMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+10012, 184},
    {I_VPSRAW, 2, {XMM_L16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+10020, 184},
    {I_VPSRAW, 3, {YMM_L16,YMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+33545, 203},
    {I_VPSRAW, 2, {YMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+33552, 203},
    {I_VPSRAW, 3, {YMM_L16,YMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+11996, 203},
    {I_VPSRAW, 2, {YMM_L16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+12004, 203},
    {I_VPSRAW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22748, 229},
    {I_VPSRAW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22756, 229},
    {I_VPSRAW, 3, {YMMREG,YMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22764, 229},
    {I_VPSRAW, 2, {YMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22772, 229},
    {I_VPSRAW, 3, {ZMMREG,ZMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22780, 230},
    {I_VPSRAW, 2, {ZMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22788, 230},
    {I_VPSRAW, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6504, 229},
    {I_VPSRAW, 2, {XMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6513, 229},
    {I_VPSRAW, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6522, 229},
    {I_VPSRAW, 2, {YMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6531, 229},
    {I_VPSRAW, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6540, 230},
    {I_VPSRAW, 2, {ZMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6549, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSRAD[] = {
    {I_VPSRAD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30472, 184},
    {I_VPSRAD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30479, 184},
    {I_VPSRAD, 3, {XMM_L16,XMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+10028, 184},
    {I_VPSRAD, 2, {XMM_L16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+10036, 184},
    {I_VPSRAD, 3, {YMM_L16,YMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+33559, 203},
    {I_VPSRAD, 2, {YMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+33566, 203},
    {I_VPSRAD, 3, {YMM_L16,YMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+12012, 203},
    {I_VPSRAD, 2, {YMM_L16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+12020, 203},
    {I_VPSRAD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22508, 225},
    {I_VPSRAD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22516, 225},
    {I_VPSRAD, 3, {YMMREG,YMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22524, 225},
    {I_VPSRAD, 2, {YMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22532, 225},
    {I_VPSRAD, 3, {ZMMREG,ZMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22540, 226},
    {I_VPSRAD, 2, {ZMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22548, 226},
    {I_VPSRAD, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+6396, 225},
    {I_VPSRAD, 2, {XMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6405, 225},
    {I_VPSRAD, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+6414, 225},
    {I_VPSRAD, 2, {YMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6423, 225},
    {I_VPSRAD, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+6432, 226},
    {I_VPSRAD, 2, {ZMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6441, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSRLW[] = {
    {I_VPSRLW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30486, 184},
    {I_VPSRLW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30493, 184},
    {I_VPSRLW, 3, {XMM_L16,XMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+10044, 184},
    {I_VPSRLW, 2, {XMM_L16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+10052, 184},
    {I_VPSRLW, 3, {YMM_L16,YMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+33573, 203},
    {I_VPSRLW, 2, {YMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+33580, 203},
    {I_VPSRLW, 3, {YMM_L16,YMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+12044, 203},
    {I_VPSRLW, 2, {YMM_L16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+12052, 203},
    {I_VPSRLW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23036, 229},
    {I_VPSRLW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23044, 229},
    {I_VPSRLW, 3, {YMMREG,YMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23052, 229},
    {I_VPSRLW, 2, {YMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23060, 229},
    {I_VPSRLW, 3, {ZMMREG,ZMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23068, 230},
    {I_VPSRLW, 2, {ZMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23076, 230},
    {I_VPSRLW, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6720, 229},
    {I_VPSRLW, 2, {XMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6729, 229},
    {I_VPSRLW, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6738, 229},
    {I_VPSRLW, 2, {YMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6747, 229},
    {I_VPSRLW, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6756, 230},
    {I_VPSRLW, 2, {ZMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6765, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSRLD[] = {
    {I_VPSRLD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30500, 184},
    {I_VPSRLD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30507, 184},
    {I_VPSRLD, 3, {XMM_L16,XMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+10060, 184},
    {I_VPSRLD, 2, {XMM_L16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+10068, 184},
    {I_VPSRLD, 3, {YMM_L16,YMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+33587, 203},
    {I_VPSRLD, 2, {YMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+33594, 203},
    {I_VPSRLD, 3, {YMM_L16,YMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+12060, 203},
    {I_VPSRLD, 2, {YMM_L16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+12068, 203},
    {I_VPSRLD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22796, 225},
    {I_VPSRLD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22804, 225},
    {I_VPSRLD, 3, {YMMREG,YMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22812, 225},
    {I_VPSRLD, 2, {YMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22820, 225},
    {I_VPSRLD, 3, {ZMMREG,ZMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22828, 226},
    {I_VPSRLD, 2, {ZMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22836, 226},
    {I_VPSRLD, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+6558, 225},
    {I_VPSRLD, 2, {XMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6567, 225},
    {I_VPSRLD, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+6576, 225},
    {I_VPSRLD, 2, {YMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6585, 225},
    {I_VPSRLD, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+6594, 226},
    {I_VPSRLD, 2, {ZMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6603, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSRLQ[] = {
    {I_VPSRLQ, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30514, 184},
    {I_VPSRLQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30521, 184},
    {I_VPSRLQ, 3, {XMM_L16,XMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+10076, 184},
    {I_VPSRLQ, 2, {XMM_L16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+10084, 184},
    {I_VPSRLQ, 3, {YMM_L16,YMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+33601, 203},
    {I_VPSRLQ, 2, {YMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+33608, 203},
    {I_VPSRLQ, 3, {YMM_L16,YMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+12076, 203},
    {I_VPSRLQ, 2, {YMM_L16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+12084, 203},
    {I_VPSRLQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22844, 225},
    {I_VPSRLQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22852, 225},
    {I_VPSRLQ, 3, {YMMREG,YMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22860, 225},
    {I_VPSRLQ, 2, {YMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22868, 225},
    {I_VPSRLQ, 3, {ZMMREG,ZMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22876, 226},
    {I_VPSRLQ, 2, {ZMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22884, 226},
    {I_VPSRLQ, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+6666, 225},
    {I_VPSRLQ, 2, {XMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6675, 225},
    {I_VPSRLQ, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+6684, 225},
    {I_VPSRLQ, 2, {YMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6693, 225},
    {I_VPSRLQ, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+6702, 226},
    {I_VPSRLQ, 2, {ZMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6711, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPTEST[] = {
    {I_VPTEST, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30528, 184},
    {I_VPTEST, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+30535, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSUBB[] = {
    {I_VPSUBB, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30542, 184},
    {I_VPSUBB, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30549, 184},
    {I_VPSUBB, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33615, 203},
    {I_VPSUBB, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33622, 203},
    {I_VPSUBB, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23084, 229},
    {I_VPSUBB, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23092, 229},
    {I_VPSUBB, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23100, 229},
    {I_VPSUBB, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23108, 229},
    {I_VPSUBB, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23116, 230},
    {I_VPSUBB, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23124, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSUBW[] = {
    {I_VPSUBW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30556, 184},
    {I_VPSUBW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30563, 184},
    {I_VPSUBW, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33629, 203},
    {I_VPSUBW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33636, 203},
    {I_VPSUBW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23420, 229},
    {I_VPSUBW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23428, 229},
    {I_VPSUBW, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23436, 229},
    {I_VPSUBW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23444, 229},
    {I_VPSUBW, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23452, 230},
    {I_VPSUBW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23460, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSUBD[] = {
    {I_VPSUBD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30570, 184},
    {I_VPSUBD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30577, 184},
    {I_VPSUBD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33643, 203},
    {I_VPSUBD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33650, 203},
    {I_VPSUBD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+23132, 225},
    {I_VPSUBD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+23140, 225},
    {I_VPSUBD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+23148, 225},
    {I_VPSUBD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+23156, 225},
    {I_VPSUBD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+23164, 226},
    {I_VPSUBD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+23172, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSUBQ[] = {
    {I_VPSUBQ, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30584, 184},
    {I_VPSUBQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30591, 184},
    {I_VPSUBQ, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33657, 203},
    {I_VPSUBQ, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33664, 203},
    {I_VPSUBQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+23180, 225},
    {I_VPSUBQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+23188, 225},
    {I_VPSUBQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+23196, 225},
    {I_VPSUBQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+23204, 225},
    {I_VPSUBQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+23212, 226},
    {I_VPSUBQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+23220, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSUBSB[] = {
    {I_VPSUBSB, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30598, 184},
    {I_VPSUBSB, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30605, 184},
    {I_VPSUBSB, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33671, 203},
    {I_VPSUBSB, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33678, 203},
    {I_VPSUBSB, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23228, 229},
    {I_VPSUBSB, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23236, 229},
    {I_VPSUBSB, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23244, 229},
    {I_VPSUBSB, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23252, 229},
    {I_VPSUBSB, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23260, 230},
    {I_VPSUBSB, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23268, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSUBSW[] = {
    {I_VPSUBSW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30612, 184},
    {I_VPSUBSW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30619, 184},
    {I_VPSUBSW, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33685, 203},
    {I_VPSUBSW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33692, 203},
    {I_VPSUBSW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23276, 229},
    {I_VPSUBSW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23284, 229},
    {I_VPSUBSW, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23292, 229},
    {I_VPSUBSW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23300, 229},
    {I_VPSUBSW, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23308, 230},
    {I_VPSUBSW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23316, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSUBUSB[] = {
    {I_VPSUBUSB, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30626, 184},
    {I_VPSUBUSB, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30633, 184},
    {I_VPSUBUSB, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33699, 203},
    {I_VPSUBUSB, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33706, 203},
    {I_VPSUBUSB, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23324, 229},
    {I_VPSUBUSB, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23332, 229},
    {I_VPSUBUSB, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23340, 229},
    {I_VPSUBUSB, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23348, 229},
    {I_VPSUBUSB, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23356, 230},
    {I_VPSUBUSB, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23364, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSUBUSW[] = {
    {I_VPSUBUSW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30640, 184},
    {I_VPSUBUSW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30647, 184},
    {I_VPSUBUSW, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33713, 203},
    {I_VPSUBUSW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33720, 203},
    {I_VPSUBUSW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23372, 229},
    {I_VPSUBUSW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23380, 229},
    {I_VPSUBUSW, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23388, 229},
    {I_VPSUBUSW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23396, 229},
    {I_VPSUBUSW, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23404, 230},
    {I_VPSUBUSW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23412, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPUNPCKHBW[] = {
    {I_VPUNPCKHBW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30654, 184},
    {I_VPUNPCKHBW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30661, 184},
    {I_VPUNPCKHBW, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33727, 203},
    {I_VPUNPCKHBW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33734, 203},
    {I_VPUNPCKHBW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23660, 229},
    {I_VPUNPCKHBW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23668, 229},
    {I_VPUNPCKHBW, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23676, 229},
    {I_VPUNPCKHBW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23684, 229},
    {I_VPUNPCKHBW, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23692, 230},
    {I_VPUNPCKHBW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23700, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPUNPCKHWD[] = {
    {I_VPUNPCKHWD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30668, 184},
    {I_VPUNPCKHWD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30675, 184},
    {I_VPUNPCKHWD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33741, 203},
    {I_VPUNPCKHWD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33748, 203},
    {I_VPUNPCKHWD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23804, 229},
    {I_VPUNPCKHWD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23812, 229},
    {I_VPUNPCKHWD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23820, 229},
    {I_VPUNPCKHWD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23828, 229},
    {I_VPUNPCKHWD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23836, 230},
    {I_VPUNPCKHWD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23844, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPUNPCKHDQ[] = {
    {I_VPUNPCKHDQ, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30682, 184},
    {I_VPUNPCKHDQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30689, 184},
    {I_VPUNPCKHDQ, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33755, 203},
    {I_VPUNPCKHDQ, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33762, 203},
    {I_VPUNPCKHDQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+23708, 225},
    {I_VPUNPCKHDQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+23716, 225},
    {I_VPUNPCKHDQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+23724, 225},
    {I_VPUNPCKHDQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+23732, 225},
    {I_VPUNPCKHDQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+23740, 226},
    {I_VPUNPCKHDQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+23748, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPUNPCKHQDQ[] = {
    {I_VPUNPCKHQDQ, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30696, 184},
    {I_VPUNPCKHQDQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30703, 184},
    {I_VPUNPCKHQDQ, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33769, 203},
    {I_VPUNPCKHQDQ, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33776, 203},
    {I_VPUNPCKHQDQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+23756, 225},
    {I_VPUNPCKHQDQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+23764, 225},
    {I_VPUNPCKHQDQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+23772, 225},
    {I_VPUNPCKHQDQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+23780, 225},
    {I_VPUNPCKHQDQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+23788, 226},
    {I_VPUNPCKHQDQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+23796, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPUNPCKLBW[] = {
    {I_VPUNPCKLBW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30710, 184},
    {I_VPUNPCKLBW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30717, 184},
    {I_VPUNPCKLBW, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33783, 203},
    {I_VPUNPCKLBW, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33790, 203},
    {I_VPUNPCKLBW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23852, 229},
    {I_VPUNPCKLBW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23860, 229},
    {I_VPUNPCKLBW, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23868, 229},
    {I_VPUNPCKLBW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23876, 229},
    {I_VPUNPCKLBW, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23884, 230},
    {I_VPUNPCKLBW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23892, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPUNPCKLWD[] = {
    {I_VPUNPCKLWD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30724, 184},
    {I_VPUNPCKLWD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30731, 184},
    {I_VPUNPCKLWD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33797, 203},
    {I_VPUNPCKLWD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33804, 203},
    {I_VPUNPCKLWD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23996, 229},
    {I_VPUNPCKLWD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+24004, 229},
    {I_VPUNPCKLWD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+24012, 229},
    {I_VPUNPCKLWD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+24020, 229},
    {I_VPUNPCKLWD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+24028, 230},
    {I_VPUNPCKLWD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+24036, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPUNPCKLDQ[] = {
    {I_VPUNPCKLDQ, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30738, 184},
    {I_VPUNPCKLDQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30745, 184},
    {I_VPUNPCKLDQ, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33811, 203},
    {I_VPUNPCKLDQ, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33818, 203},
    {I_VPUNPCKLDQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+23900, 225},
    {I_VPUNPCKLDQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+23908, 225},
    {I_VPUNPCKLDQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+23916, 225},
    {I_VPUNPCKLDQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+23924, 225},
    {I_VPUNPCKLDQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+23932, 226},
    {I_VPUNPCKLDQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+23940, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPUNPCKLQDQ[] = {
    {I_VPUNPCKLQDQ, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30752, 184},
    {I_VPUNPCKLQDQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30759, 184},
    {I_VPUNPCKLQDQ, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33825, 203},
    {I_VPUNPCKLQDQ, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33832, 203},
    {I_VPUNPCKLQDQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+23948, 225},
    {I_VPUNPCKLQDQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+23956, 225},
    {I_VPUNPCKLQDQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+23964, 225},
    {I_VPUNPCKLQDQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+23972, 225},
    {I_VPUNPCKLQDQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+23980, 226},
    {I_VPUNPCKLQDQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+23988, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPXOR[] = {
    {I_VPXOR, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30766, 184},
    {I_VPXOR, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30773, 184},
    {I_VPXOR, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33839, 203},
    {I_VPXOR, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33846, 203},
    ITEMPLATE_END
};

static const struct itemplate instrux_VRCPPS[] = {
    {I_VRCPPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30780, 184},
    {I_VRCPPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+30787, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VRCPSS[] = {
    {I_VRCPSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+30794, 184},
    {I_VRCPSS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+30801, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VRSQRTPS[] = {
    {I_VRSQRTPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30808, 184},
    {I_VRSQRTPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+30815, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VRSQRTSS[] = {
    {I_VRSQRTSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+30822, 184},
    {I_VRSQRTSS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+30829, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VROUNDPD[] = {
    {I_VROUNDPD, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+10092, 184},
    {I_VROUNDPD, 3, {YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+10100, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VROUNDPS[] = {
    {I_VROUNDPS, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+10108, 184},
    {I_VROUNDPS, 3, {YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+10116, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VROUNDSD[] = {
    {I_VROUNDSD, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+10124, 184},
    {I_VROUNDSD, 3, {XMM_L16,RM_XMM_L16|BITS64,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+10132, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VROUNDSS[] = {
    {I_VROUNDSS, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+10140, 184},
    {I_VROUNDSS, 3, {XMM_L16,RM_XMM_L16|BITS32,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+10148, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSHUFPD[] = {
    {I_VSHUFPD, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+10156, 184},
    {I_VSHUFPD, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+10164, 184},
    {I_VSHUFPD, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+10172, 184},
    {I_VSHUFPD, 3, {YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+10180, 184},
    {I_VSHUFPD, 4, {XMMREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+7476, 225},
    {I_VSHUFPD, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+7485, 225},
    {I_VSHUFPD, 4, {YMMREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+7494, 225},
    {I_VSHUFPD, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+7503, 225},
    {I_VSHUFPD, 4, {ZMMREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+7512, 226},
    {I_VSHUFPD, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+7521, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSHUFPS[] = {
    {I_VSHUFPS, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+10188, 184},
    {I_VSHUFPS, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+10196, 184},
    {I_VSHUFPS, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+10204, 184},
    {I_VSHUFPS, 3, {YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+10212, 184},
    {I_VSHUFPS, 4, {XMMREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+7530, 225},
    {I_VSHUFPS, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+7539, 225},
    {I_VSHUFPS, 4, {YMMREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+7548, 225},
    {I_VSHUFPS, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+7557, 225},
    {I_VSHUFPS, 4, {ZMMREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+7566, 226},
    {I_VSHUFPS, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+7575, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSQRTPD[] = {
    {I_VSQRTPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30836, 184},
    {I_VSQRTPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+30843, 184},
    {I_VSQRTPD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+24524, 225},
    {I_VSQRTPD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+24532, 225},
    {I_VSQRTPD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64|ER,0,0,0}, nasm_bytecodes+24540, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSQRTPS[] = {
    {I_VSQRTPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30850, 184},
    {I_VSQRTPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+30857, 184},
    {I_VSQRTPS, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+24548, 225},
    {I_VSQRTPS, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+24556, 225},
    {I_VSQRTPS, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32|ER,0,0,0}, nasm_bytecodes+24564, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSQRTSD[] = {
    {I_VSQRTSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+30864, 184},
    {I_VSQRTSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+30871, 184},
    {I_VSQRTSD, 3, {XMMREG,XMMREG,RM_XMM|BITS64,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+24572, 226},
    {I_VSQRTSD, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,ER,0,0,0}, nasm_bytecodes+24580, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSQRTSS[] = {
    {I_VSQRTSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+30878, 184},
    {I_VSQRTSS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+30885, 184},
    {I_VSQRTSS, 3, {XMMREG,XMMREG,RM_XMM|BITS32,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+24588, 226},
    {I_VSQRTSS, 2, {XMMREG,RM_XMM|BITS32,0,0,0}, {MASK|Z,ER,0,0,0}, nasm_bytecodes+24596, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSTMXCSR[] = {
    {I_VSTMXCSR, 1, {MEMORY|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+30892, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSUBPD[] = {
    {I_VSUBPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30899, 184},
    {I_VSUBPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30906, 184},
    {I_VSUBPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+30913, 184},
    {I_VSUBPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+30920, 184},
    {I_VSUBPD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+24604, 225},
    {I_VSUBPD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+24612, 225},
    {I_VSUBPD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+24620, 225},
    {I_VSUBPD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+24628, 225},
    {I_VSUBPD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64|ER,0,0}, nasm_bytecodes+24636, 226},
    {I_VSUBPD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64|ER,0,0,0}, nasm_bytecodes+24644, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSUBPS[] = {
    {I_VSUBPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+30927, 184},
    {I_VSUBPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30934, 184},
    {I_VSUBPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+30941, 184},
    {I_VSUBPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+30948, 184},
    {I_VSUBPS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+24652, 225},
    {I_VSUBPS, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+24660, 225},
    {I_VSUBPS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+24668, 225},
    {I_VSUBPS, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+24676, 225},
    {I_VSUBPS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32|ER,0,0}, nasm_bytecodes+24684, 226},
    {I_VSUBPS, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32|ER,0,0,0}, nasm_bytecodes+24692, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSUBSD[] = {
    {I_VSUBSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+30955, 184},
    {I_VSUBSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+30962, 184},
    {I_VSUBSD, 3, {XMMREG,XMMREG,RM_XMM|BITS64,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+24700, 226},
    {I_VSUBSD, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,ER,0,0,0}, nasm_bytecodes+24708, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSUBSS[] = {
    {I_VSUBSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+30969, 184},
    {I_VSUBSS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+30976, 184},
    {I_VSUBSS, 3, {XMMREG,XMMREG,RM_XMM|BITS32,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+24716, 226},
    {I_VSUBSS, 2, {XMMREG,RM_XMM|BITS32,0,0,0}, {MASK|Z,ER,0,0,0}, nasm_bytecodes+24724, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VTESTPS[] = {
    {I_VTESTPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30983, 184},
    {I_VTESTPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+30990, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VTESTPD[] = {
    {I_VTESTPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30997, 184},
    {I_VTESTPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+31004, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VUCOMISD[] = {
    {I_VUCOMISD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+31011, 184},
    {I_VUCOMISD, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {0,SAE,0,0,0}, nasm_bytecodes+24732, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VUCOMISS[] = {
    {I_VUCOMISS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+31018, 184},
    {I_VUCOMISS, 2, {XMMREG,RM_XMM|BITS32,0,0,0}, {0,SAE,0,0,0}, nasm_bytecodes+24740, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VUNPCKHPD[] = {
    {I_VUNPCKHPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31025, 184},
    {I_VUNPCKHPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+31032, 184},
    {I_VUNPCKHPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31039, 184},
    {I_VUNPCKHPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+31046, 184},
    {I_VUNPCKHPD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+24748, 225},
    {I_VUNPCKHPD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+24756, 225},
    {I_VUNPCKHPD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+24764, 225},
    {I_VUNPCKHPD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+24772, 225},
    {I_VUNPCKHPD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+24780, 226},
    {I_VUNPCKHPD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+24788, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VUNPCKHPS[] = {
    {I_VUNPCKHPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31053, 184},
    {I_VUNPCKHPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+31060, 184},
    {I_VUNPCKHPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31067, 184},
    {I_VUNPCKHPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+31074, 184},
    {I_VUNPCKHPS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+24796, 225},
    {I_VUNPCKHPS, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+24804, 225},
    {I_VUNPCKHPS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+24812, 225},
    {I_VUNPCKHPS, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+24820, 225},
    {I_VUNPCKHPS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+24828, 226},
    {I_VUNPCKHPS, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+24836, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VUNPCKLPD[] = {
    {I_VUNPCKLPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31081, 184},
    {I_VUNPCKLPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+31088, 184},
    {I_VUNPCKLPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31095, 184},
    {I_VUNPCKLPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+31102, 184},
    {I_VUNPCKLPD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+24844, 225},
    {I_VUNPCKLPD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+24852, 225},
    {I_VUNPCKLPD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+24860, 225},
    {I_VUNPCKLPD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+24868, 225},
    {I_VUNPCKLPD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+24876, 226},
    {I_VUNPCKLPD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+24884, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VUNPCKLPS[] = {
    {I_VUNPCKLPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31109, 184},
    {I_VUNPCKLPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+31116, 184},
    {I_VUNPCKLPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31123, 184},
    {I_VUNPCKLPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+31130, 184},
    {I_VUNPCKLPS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+24892, 225},
    {I_VUNPCKLPS, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+24900, 225},
    {I_VUNPCKLPS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+24908, 225},
    {I_VUNPCKLPS, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+24916, 225},
    {I_VUNPCKLPS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+24924, 226},
    {I_VUNPCKLPS, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+24932, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VXORPD[] = {
    {I_VXORPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31137, 184},
    {I_VXORPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+31144, 184},
    {I_VXORPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31151, 184},
    {I_VXORPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+31158, 184},
    {I_VXORPD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+24940, 227},
    {I_VXORPD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+24948, 227},
    {I_VXORPD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+24956, 227},
    {I_VXORPD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+24964, 227},
    {I_VXORPD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+24972, 228},
    {I_VXORPD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+24980, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VXORPS[] = {
    {I_VXORPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31165, 184},
    {I_VXORPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+31172, 184},
    {I_VXORPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31179, 184},
    {I_VXORPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+31186, 184},
    {I_VXORPS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+24988, 227},
    {I_VXORPS, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+24996, 227},
    {I_VXORPS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+25004, 227},
    {I_VXORPS, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+25012, 227},
    {I_VXORPS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+25020, 228},
    {I_VXORPS, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+25028, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VZEROALL[] = {
    {I_VZEROALL, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37336, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_VZEROUPPER[] = {
    {I_VZEROUPPER, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37342, 184},
    ITEMPLATE_END
};

static const struct itemplate instrux_PCLMULLQLQDQ[] = {
    {I_PCLMULLQLQDQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+3768, 183},
    ITEMPLATE_END
};

static const struct itemplate instrux_PCLMULHQLQDQ[] = {
    {I_PCLMULHQLQDQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+3777, 183},
    ITEMPLATE_END
};

static const struct itemplate instrux_PCLMULLQHQDQ[] = {
    {I_PCLMULLQHQDQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+3786, 183},
    ITEMPLATE_END
};

static const struct itemplate instrux_PCLMULHQHQDQ[] = {
    {I_PCLMULHQHQDQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+3795, 183},
    ITEMPLATE_END
};

static const struct itemplate instrux_PCLMULQDQ[] = {
    {I_PCLMULQDQ, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+10220, 183},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCLMULLQLQDQ[] = {
    {I_VPCLMULLQLQDQ, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+3804, 184},
    {I_VPCLMULLQLQDQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+3813, 184},
    {I_VPCLMULLQLQDQ, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+3876, 194},
    {I_VPCLMULLQLQDQ, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+3885, 194},
    {I_VPCLMULLQLQDQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+0, 195},
    {I_VPCLMULLQLQDQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+10, 195},
    {I_VPCLMULLQLQDQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+80, 195},
    {I_VPCLMULLQLQDQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+90, 195},
    {I_VPCLMULLQLQDQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, NO_DECORATOR, nasm_bytecodes+160, 196},
    {I_VPCLMULLQLQDQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, NO_DECORATOR, nasm_bytecodes+170, 196},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCLMULHQLQDQ[] = {
    {I_VPCLMULHQLQDQ, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+3822, 184},
    {I_VPCLMULHQLQDQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+3831, 184},
    {I_VPCLMULHQLQDQ, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+3894, 194},
    {I_VPCLMULHQLQDQ, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+3903, 194},
    {I_VPCLMULHQLQDQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+20, 195},
    {I_VPCLMULHQLQDQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+30, 195},
    {I_VPCLMULHQLQDQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+100, 195},
    {I_VPCLMULHQLQDQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+110, 195},
    {I_VPCLMULHQLQDQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, NO_DECORATOR, nasm_bytecodes+180, 196},
    {I_VPCLMULHQLQDQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, NO_DECORATOR, nasm_bytecodes+190, 196},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCLMULLQHQDQ[] = {
    {I_VPCLMULLQHQDQ, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+3840, 184},
    {I_VPCLMULLQHQDQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+3849, 184},
    {I_VPCLMULLQHQDQ, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+3912, 194},
    {I_VPCLMULLQHQDQ, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+3921, 194},
    {I_VPCLMULLQHQDQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+40, 195},
    {I_VPCLMULLQHQDQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+50, 195},
    {I_VPCLMULLQHQDQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+120, 195},
    {I_VPCLMULLQHQDQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+130, 195},
    {I_VPCLMULLQHQDQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, NO_DECORATOR, nasm_bytecodes+200, 196},
    {I_VPCLMULLQHQDQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, NO_DECORATOR, nasm_bytecodes+210, 196},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCLMULHQHQDQ[] = {
    {I_VPCLMULHQHQDQ, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+3858, 184},
    {I_VPCLMULHQHQDQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+3867, 184},
    {I_VPCLMULHQHQDQ, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+3930, 194},
    {I_VPCLMULHQHQDQ, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+3939, 194},
    {I_VPCLMULHQHQDQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+60, 195},
    {I_VPCLMULHQHQDQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+70, 195},
    {I_VPCLMULHQHQDQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+140, 195},
    {I_VPCLMULHQHQDQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+150, 195},
    {I_VPCLMULHQHQDQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, NO_DECORATOR, nasm_bytecodes+220, 196},
    {I_VPCLMULHQHQDQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, NO_DECORATOR, nasm_bytecodes+230, 196},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCLMULQDQ[] = {
    {I_VPCLMULQDQ, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+10228, 184},
    {I_VPCLMULQDQ, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+10236, 184},
    {I_VPCLMULQDQ, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+10244, 194},
    {I_VPCLMULQDQ, 3, {YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+10252, 194},
    {I_VPCLMULQDQ, 4, {XMMREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+3948, 195},
    {I_VPCLMULQDQ, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+3957, 195},
    {I_VPCLMULQDQ, 4, {YMMREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+3966, 195},
    {I_VPCLMULQDQ, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+3975, 195},
    {I_VPCLMULQDQ, 4, {ZMMREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+3984, 196},
    {I_VPCLMULQDQ, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+3993, 196},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADD132PS[] = {
    {I_VFMADD132PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31193, 197},
    {I_VFMADD132PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31200, 197},
    {I_VFMADD132PS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14172, 225},
    {I_VFMADD132PS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14180, 225},
    {I_VFMADD132PS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32|ER,0,0}, nasm_bytecodes+14188, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADD132PD[] = {
    {I_VFMADD132PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31207, 197},
    {I_VFMADD132PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31214, 197},
    {I_VFMADD132PD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14148, 225},
    {I_VFMADD132PD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14156, 225},
    {I_VFMADD132PD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64|ER,0,0}, nasm_bytecodes+14164, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADD312PS[] = {
    {I_VFMADD312PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31193, 197},
    {I_VFMADD312PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31200, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADD312PD[] = {
    {I_VFMADD312PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31207, 197},
    {I_VFMADD312PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31214, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADD213PS[] = {
    {I_VFMADD213PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31221, 197},
    {I_VFMADD213PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31228, 197},
    {I_VFMADD213PS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14236, 225},
    {I_VFMADD213PS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14244, 225},
    {I_VFMADD213PS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32|ER,0,0}, nasm_bytecodes+14252, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADD213PD[] = {
    {I_VFMADD213PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31235, 197},
    {I_VFMADD213PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31242, 197},
    {I_VFMADD213PD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14212, 225},
    {I_VFMADD213PD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14220, 225},
    {I_VFMADD213PD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64|ER,0,0}, nasm_bytecodes+14228, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADD123PS[] = {
    {I_VFMADD123PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31221, 197},
    {I_VFMADD123PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31228, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADD123PD[] = {
    {I_VFMADD123PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31235, 197},
    {I_VFMADD123PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31242, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADD231PS[] = {
    {I_VFMADD231PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31249, 197},
    {I_VFMADD231PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31256, 197},
    {I_VFMADD231PS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14300, 225},
    {I_VFMADD231PS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14308, 225},
    {I_VFMADD231PS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32|ER,0,0}, nasm_bytecodes+14316, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADD231PD[] = {
    {I_VFMADD231PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31263, 197},
    {I_VFMADD231PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31270, 197},
    {I_VFMADD231PD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14276, 225},
    {I_VFMADD231PD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14284, 225},
    {I_VFMADD231PD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64|ER,0,0}, nasm_bytecodes+14292, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADD321PS[] = {
    {I_VFMADD321PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31249, 197},
    {I_VFMADD321PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31256, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADD321PD[] = {
    {I_VFMADD321PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31263, 197},
    {I_VFMADD321PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31270, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADDSUB132PS[] = {
    {I_VFMADDSUB132PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31277, 197},
    {I_VFMADDSUB132PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31284, 197},
    {I_VFMADDSUB132PS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14364, 225},
    {I_VFMADDSUB132PS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14372, 225},
    {I_VFMADDSUB132PS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32|ER,0,0}, nasm_bytecodes+14380, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADDSUB132PD[] = {
    {I_VFMADDSUB132PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31291, 197},
    {I_VFMADDSUB132PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31298, 197},
    {I_VFMADDSUB132PD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14340, 225},
    {I_VFMADDSUB132PD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14348, 225},
    {I_VFMADDSUB132PD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64|ER,0,0}, nasm_bytecodes+14356, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADDSUB312PS[] = {
    {I_VFMADDSUB312PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31277, 197},
    {I_VFMADDSUB312PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31284, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADDSUB312PD[] = {
    {I_VFMADDSUB312PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31291, 197},
    {I_VFMADDSUB312PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31298, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADDSUB213PS[] = {
    {I_VFMADDSUB213PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31305, 197},
    {I_VFMADDSUB213PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31312, 197},
    {I_VFMADDSUB213PS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14412, 225},
    {I_VFMADDSUB213PS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14420, 225},
    {I_VFMADDSUB213PS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32|ER,0,0}, nasm_bytecodes+14428, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADDSUB213PD[] = {
    {I_VFMADDSUB213PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31319, 197},
    {I_VFMADDSUB213PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31326, 197},
    {I_VFMADDSUB213PD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14388, 225},
    {I_VFMADDSUB213PD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14396, 225},
    {I_VFMADDSUB213PD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64|ER,0,0}, nasm_bytecodes+14404, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADDSUB123PS[] = {
    {I_VFMADDSUB123PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31305, 197},
    {I_VFMADDSUB123PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31312, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADDSUB123PD[] = {
    {I_VFMADDSUB123PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31319, 197},
    {I_VFMADDSUB123PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31326, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADDSUB231PS[] = {
    {I_VFMADDSUB231PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31333, 197},
    {I_VFMADDSUB231PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31340, 197},
    {I_VFMADDSUB231PS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14460, 225},
    {I_VFMADDSUB231PS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14468, 225},
    {I_VFMADDSUB231PS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32|ER,0,0}, nasm_bytecodes+14476, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADDSUB231PD[] = {
    {I_VFMADDSUB231PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31347, 197},
    {I_VFMADDSUB231PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31354, 197},
    {I_VFMADDSUB231PD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14436, 225},
    {I_VFMADDSUB231PD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14444, 225},
    {I_VFMADDSUB231PD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64|ER,0,0}, nasm_bytecodes+14452, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADDSUB321PS[] = {
    {I_VFMADDSUB321PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31333, 197},
    {I_VFMADDSUB321PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31340, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADDSUB321PD[] = {
    {I_VFMADDSUB321PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31347, 197},
    {I_VFMADDSUB321PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31354, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUB132PS[] = {
    {I_VFMSUB132PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31361, 197},
    {I_VFMSUB132PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31368, 197},
    {I_VFMSUB132PS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14508, 225},
    {I_VFMSUB132PS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14516, 225},
    {I_VFMSUB132PS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32|ER,0,0}, nasm_bytecodes+14524, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUB132PD[] = {
    {I_VFMSUB132PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31375, 197},
    {I_VFMSUB132PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31382, 197},
    {I_VFMSUB132PD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14484, 225},
    {I_VFMSUB132PD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14492, 225},
    {I_VFMSUB132PD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64|ER,0,0}, nasm_bytecodes+14500, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUB312PS[] = {
    {I_VFMSUB312PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31361, 197},
    {I_VFMSUB312PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31368, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUB312PD[] = {
    {I_VFMSUB312PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31375, 197},
    {I_VFMSUB312PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31382, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUB213PS[] = {
    {I_VFMSUB213PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31389, 197},
    {I_VFMSUB213PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31396, 197},
    {I_VFMSUB213PS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14572, 225},
    {I_VFMSUB213PS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14580, 225},
    {I_VFMSUB213PS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32|ER,0,0}, nasm_bytecodes+14588, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUB213PD[] = {
    {I_VFMSUB213PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31403, 197},
    {I_VFMSUB213PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31410, 197},
    {I_VFMSUB213PD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14548, 225},
    {I_VFMSUB213PD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14556, 225},
    {I_VFMSUB213PD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64|ER,0,0}, nasm_bytecodes+14564, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUB123PS[] = {
    {I_VFMSUB123PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31389, 197},
    {I_VFMSUB123PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31396, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUB123PD[] = {
    {I_VFMSUB123PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31403, 197},
    {I_VFMSUB123PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31410, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUB231PS[] = {
    {I_VFMSUB231PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31417, 197},
    {I_VFMSUB231PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31424, 197},
    {I_VFMSUB231PS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14636, 225},
    {I_VFMSUB231PS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14644, 225},
    {I_VFMSUB231PS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32|ER,0,0}, nasm_bytecodes+14652, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUB231PD[] = {
    {I_VFMSUB231PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31431, 197},
    {I_VFMSUB231PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31438, 197},
    {I_VFMSUB231PD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14612, 225},
    {I_VFMSUB231PD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14620, 225},
    {I_VFMSUB231PD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64|ER,0,0}, nasm_bytecodes+14628, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUB321PS[] = {
    {I_VFMSUB321PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31417, 197},
    {I_VFMSUB321PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31424, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUB321PD[] = {
    {I_VFMSUB321PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31431, 197},
    {I_VFMSUB321PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31438, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUBADD132PS[] = {
    {I_VFMSUBADD132PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31445, 197},
    {I_VFMSUBADD132PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31452, 197},
    {I_VFMSUBADD132PS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14700, 225},
    {I_VFMSUBADD132PS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14708, 225},
    {I_VFMSUBADD132PS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32|ER,0,0}, nasm_bytecodes+14716, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUBADD132PD[] = {
    {I_VFMSUBADD132PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31459, 197},
    {I_VFMSUBADD132PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31466, 197},
    {I_VFMSUBADD132PD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14676, 225},
    {I_VFMSUBADD132PD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14684, 225},
    {I_VFMSUBADD132PD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64|ER,0,0}, nasm_bytecodes+14692, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUBADD312PS[] = {
    {I_VFMSUBADD312PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31445, 197},
    {I_VFMSUBADD312PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31452, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUBADD312PD[] = {
    {I_VFMSUBADD312PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31459, 197},
    {I_VFMSUBADD312PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31466, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUBADD213PS[] = {
    {I_VFMSUBADD213PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31473, 197},
    {I_VFMSUBADD213PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31480, 197},
    {I_VFMSUBADD213PS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14748, 225},
    {I_VFMSUBADD213PS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14756, 225},
    {I_VFMSUBADD213PS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32|ER,0,0}, nasm_bytecodes+14764, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUBADD213PD[] = {
    {I_VFMSUBADD213PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31487, 197},
    {I_VFMSUBADD213PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31494, 197},
    {I_VFMSUBADD213PD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14724, 225},
    {I_VFMSUBADD213PD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14732, 225},
    {I_VFMSUBADD213PD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64|ER,0,0}, nasm_bytecodes+14740, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUBADD123PS[] = {
    {I_VFMSUBADD123PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31473, 197},
    {I_VFMSUBADD123PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31480, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUBADD123PD[] = {
    {I_VFMSUBADD123PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31487, 197},
    {I_VFMSUBADD123PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31494, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUBADD231PS[] = {
    {I_VFMSUBADD231PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31501, 197},
    {I_VFMSUBADD231PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31508, 197},
    {I_VFMSUBADD231PS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14796, 225},
    {I_VFMSUBADD231PS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14804, 225},
    {I_VFMSUBADD231PS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32|ER,0,0}, nasm_bytecodes+14812, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUBADD231PD[] = {
    {I_VFMSUBADD231PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31515, 197},
    {I_VFMSUBADD231PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31522, 197},
    {I_VFMSUBADD231PD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14772, 225},
    {I_VFMSUBADD231PD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14780, 225},
    {I_VFMSUBADD231PD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64|ER,0,0}, nasm_bytecodes+14788, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUBADD321PS[] = {
    {I_VFMSUBADD321PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31501, 197},
    {I_VFMSUBADD321PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31508, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUBADD321PD[] = {
    {I_VFMSUBADD321PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31515, 197},
    {I_VFMSUBADD321PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31522, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMADD132PS[] = {
    {I_VFNMADD132PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31529, 197},
    {I_VFNMADD132PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31536, 197},
    {I_VFNMADD132PS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14844, 225},
    {I_VFNMADD132PS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14852, 225},
    {I_VFNMADD132PS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32|ER,0,0}, nasm_bytecodes+14860, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMADD132PD[] = {
    {I_VFNMADD132PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31543, 197},
    {I_VFNMADD132PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31550, 197},
    {I_VFNMADD132PD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14820, 225},
    {I_VFNMADD132PD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14828, 225},
    {I_VFNMADD132PD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64|ER,0,0}, nasm_bytecodes+14836, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMADD312PS[] = {
    {I_VFNMADD312PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31529, 197},
    {I_VFNMADD312PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31536, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMADD312PD[] = {
    {I_VFNMADD312PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31543, 197},
    {I_VFNMADD312PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31550, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMADD213PS[] = {
    {I_VFNMADD213PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31557, 197},
    {I_VFNMADD213PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31564, 197},
    {I_VFNMADD213PS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14908, 225},
    {I_VFNMADD213PS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14916, 225},
    {I_VFNMADD213PS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32|ER,0,0}, nasm_bytecodes+14924, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMADD213PD[] = {
    {I_VFNMADD213PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31571, 197},
    {I_VFNMADD213PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31578, 197},
    {I_VFNMADD213PD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14884, 225},
    {I_VFNMADD213PD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14892, 225},
    {I_VFNMADD213PD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64|ER,0,0}, nasm_bytecodes+14900, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMADD123PS[] = {
    {I_VFNMADD123PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31557, 197},
    {I_VFNMADD123PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31564, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMADD123PD[] = {
    {I_VFNMADD123PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31571, 197},
    {I_VFNMADD123PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31578, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMADD231PS[] = {
    {I_VFNMADD231PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31585, 197},
    {I_VFNMADD231PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31592, 197},
    {I_VFNMADD231PS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14972, 225},
    {I_VFNMADD231PS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+14980, 225},
    {I_VFNMADD231PS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32|ER,0,0}, nasm_bytecodes+14988, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMADD231PD[] = {
    {I_VFNMADD231PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31599, 197},
    {I_VFNMADD231PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31606, 197},
    {I_VFNMADD231PD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14948, 225},
    {I_VFNMADD231PD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+14956, 225},
    {I_VFNMADD231PD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64|ER,0,0}, nasm_bytecodes+14964, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMADD321PS[] = {
    {I_VFNMADD321PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31585, 197},
    {I_VFNMADD321PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31592, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMADD321PD[] = {
    {I_VFNMADD321PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31599, 197},
    {I_VFNMADD321PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31606, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMSUB132PS[] = {
    {I_VFNMSUB132PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31613, 197},
    {I_VFNMSUB132PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31620, 197},
    {I_VFNMSUB132PS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+15036, 225},
    {I_VFNMSUB132PS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+15044, 225},
    {I_VFNMSUB132PS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32|ER,0,0}, nasm_bytecodes+15052, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMSUB132PD[] = {
    {I_VFNMSUB132PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31627, 197},
    {I_VFNMSUB132PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31634, 197},
    {I_VFNMSUB132PD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+15012, 225},
    {I_VFNMSUB132PD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+15020, 225},
    {I_VFNMSUB132PD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64|ER,0,0}, nasm_bytecodes+15028, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMSUB312PS[] = {
    {I_VFNMSUB312PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31613, 197},
    {I_VFNMSUB312PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31620, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMSUB312PD[] = {
    {I_VFNMSUB312PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31627, 197},
    {I_VFNMSUB312PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31634, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMSUB213PS[] = {
    {I_VFNMSUB213PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31641, 197},
    {I_VFNMSUB213PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31648, 197},
    {I_VFNMSUB213PS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+15100, 225},
    {I_VFNMSUB213PS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+15108, 225},
    {I_VFNMSUB213PS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32|ER,0,0}, nasm_bytecodes+15116, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMSUB213PD[] = {
    {I_VFNMSUB213PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31655, 197},
    {I_VFNMSUB213PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31662, 197},
    {I_VFNMSUB213PD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+15076, 225},
    {I_VFNMSUB213PD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+15084, 225},
    {I_VFNMSUB213PD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64|ER,0,0}, nasm_bytecodes+15092, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMSUB123PS[] = {
    {I_VFNMSUB123PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31641, 197},
    {I_VFNMSUB123PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31648, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMSUB123PD[] = {
    {I_VFNMSUB123PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31655, 197},
    {I_VFNMSUB123PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31662, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMSUB231PS[] = {
    {I_VFNMSUB231PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31669, 197},
    {I_VFNMSUB231PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31676, 197},
    {I_VFNMSUB231PS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+15164, 225},
    {I_VFNMSUB231PS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+15172, 225},
    {I_VFNMSUB231PS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32|ER,0,0}, nasm_bytecodes+15180, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMSUB231PD[] = {
    {I_VFNMSUB231PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31683, 197},
    {I_VFNMSUB231PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31690, 197},
    {I_VFNMSUB231PD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+15140, 225},
    {I_VFNMSUB231PD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+15148, 225},
    {I_VFNMSUB231PD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64|ER,0,0}, nasm_bytecodes+15156, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMSUB321PS[] = {
    {I_VFNMSUB321PS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31669, 197},
    {I_VFNMSUB321PS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31676, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMSUB321PD[] = {
    {I_VFNMSUB321PD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+31683, 197},
    {I_VFNMSUB321PD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+31690, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADD132SS[] = {
    {I_VFMADD132SS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+31697, 197},
    {I_VFMADD132SS, 3, {XMMREG,XMMREG,RM_XMM|BITS32,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+14204, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADD132SD[] = {
    {I_VFMADD132SD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+31704, 197},
    {I_VFMADD132SD, 3, {XMMREG,XMMREG,RM_XMM|BITS64,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+14196, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADD312SS[] = {
    {I_VFMADD312SS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+31697, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADD312SD[] = {
    {I_VFMADD312SD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+31704, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADD213SS[] = {
    {I_VFMADD213SS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+31711, 197},
    {I_VFMADD213SS, 3, {XMMREG,XMMREG,RM_XMM|BITS32,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+14268, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADD213SD[] = {
    {I_VFMADD213SD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+31718, 197},
    {I_VFMADD213SD, 3, {XMMREG,XMMREG,RM_XMM|BITS64,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+14260, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADD123SS[] = {
    {I_VFMADD123SS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+31711, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADD123SD[] = {
    {I_VFMADD123SD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+31718, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADD231SS[] = {
    {I_VFMADD231SS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+31725, 197},
    {I_VFMADD231SS, 3, {XMMREG,XMMREG,RM_XMM|BITS32,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+14332, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADD231SD[] = {
    {I_VFMADD231SD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+31732, 197},
    {I_VFMADD231SD, 3, {XMMREG,XMMREG,RM_XMM|BITS64,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+14324, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADD321SS[] = {
    {I_VFMADD321SS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+31725, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADD321SD[] = {
    {I_VFMADD321SD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+31732, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUB132SS[] = {
    {I_VFMSUB132SS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+31739, 197},
    {I_VFMSUB132SS, 3, {XMMREG,XMMREG,RM_XMM|BITS32,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+14540, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUB132SD[] = {
    {I_VFMSUB132SD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+31746, 197},
    {I_VFMSUB132SD, 3, {XMMREG,XMMREG,RM_XMM|BITS64,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+14532, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUB312SS[] = {
    {I_VFMSUB312SS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+31739, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUB312SD[] = {
    {I_VFMSUB312SD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+31746, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUB213SS[] = {
    {I_VFMSUB213SS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+31753, 197},
    {I_VFMSUB213SS, 3, {XMMREG,XMMREG,RM_XMM|BITS32,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+14604, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUB213SD[] = {
    {I_VFMSUB213SD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+31760, 197},
    {I_VFMSUB213SD, 3, {XMMREG,XMMREG,RM_XMM|BITS64,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+14596, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUB123SS[] = {
    {I_VFMSUB123SS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+31753, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUB123SD[] = {
    {I_VFMSUB123SD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+31760, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUB231SS[] = {
    {I_VFMSUB231SS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+31767, 197},
    {I_VFMSUB231SS, 3, {XMMREG,XMMREG,RM_XMM|BITS32,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+14668, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUB231SD[] = {
    {I_VFMSUB231SD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+31774, 197},
    {I_VFMSUB231SD, 3, {XMMREG,XMMREG,RM_XMM|BITS64,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+14660, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUB321SS[] = {
    {I_VFMSUB321SS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+31767, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUB321SD[] = {
    {I_VFMSUB321SD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+31774, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMADD132SS[] = {
    {I_VFNMADD132SS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+31781, 197},
    {I_VFNMADD132SS, 3, {XMMREG,XMMREG,RM_XMM|BITS32,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+14876, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMADD132SD[] = {
    {I_VFNMADD132SD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+31788, 197},
    {I_VFNMADD132SD, 3, {XMMREG,XMMREG,RM_XMM|BITS64,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+14868, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMADD312SS[] = {
    {I_VFNMADD312SS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+31781, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMADD312SD[] = {
    {I_VFNMADD312SD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+31788, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMADD213SS[] = {
    {I_VFNMADD213SS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+31795, 197},
    {I_VFNMADD213SS, 3, {XMMREG,XMMREG,RM_XMM|BITS32,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+14940, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMADD213SD[] = {
    {I_VFNMADD213SD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+31802, 197},
    {I_VFNMADD213SD, 3, {XMMREG,XMMREG,RM_XMM|BITS64,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+14932, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMADD123SS[] = {
    {I_VFNMADD123SS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+31795, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMADD123SD[] = {
    {I_VFNMADD123SD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+31802, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMADD231SS[] = {
    {I_VFNMADD231SS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+31809, 197},
    {I_VFNMADD231SS, 3, {XMMREG,XMMREG,RM_XMM|BITS32,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+15004, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMADD231SD[] = {
    {I_VFNMADD231SD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+31816, 197},
    {I_VFNMADD231SD, 3, {XMMREG,XMMREG,RM_XMM|BITS64,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+14996, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMADD321SS[] = {
    {I_VFNMADD321SS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+31809, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMADD321SD[] = {
    {I_VFNMADD321SD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+31816, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMSUB132SS[] = {
    {I_VFNMSUB132SS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+31823, 197},
    {I_VFNMSUB132SS, 3, {XMMREG,XMMREG,RM_XMM|BITS32,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+15068, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMSUB132SD[] = {
    {I_VFNMSUB132SD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+31830, 197},
    {I_VFNMSUB132SD, 3, {XMMREG,XMMREG,RM_XMM|BITS64,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+15060, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMSUB312SS[] = {
    {I_VFNMSUB312SS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+31823, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMSUB312SD[] = {
    {I_VFNMSUB312SD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+31830, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMSUB213SS[] = {
    {I_VFNMSUB213SS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+31837, 197},
    {I_VFNMSUB213SS, 3, {XMMREG,XMMREG,RM_XMM|BITS32,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+15132, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMSUB213SD[] = {
    {I_VFNMSUB213SD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+31844, 197},
    {I_VFNMSUB213SD, 3, {XMMREG,XMMREG,RM_XMM|BITS64,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+15124, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMSUB123SS[] = {
    {I_VFNMSUB123SS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+31837, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMSUB123SD[] = {
    {I_VFNMSUB123SD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+31844, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMSUB231SS[] = {
    {I_VFNMSUB231SS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+31851, 197},
    {I_VFNMSUB231SS, 3, {XMMREG,XMMREG,RM_XMM|BITS32,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+15196, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMSUB231SD[] = {
    {I_VFNMSUB231SD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+31858, 197},
    {I_VFNMSUB231SD, 3, {XMMREG,XMMREG,RM_XMM|BITS64,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+15188, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMSUB321SS[] = {
    {I_VFNMSUB321SS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+31851, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMSUB321SD[] = {
    {I_VFNMSUB321SD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+31858, 197},
    ITEMPLATE_END
};

static const struct itemplate instrux_RDFSBASE[] = {
    {I_RDFSBASE, 1, {REG_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+31865, 134},
    {I_RDFSBASE, 1, {REG_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+31872, 134},
    ITEMPLATE_END
};

static const struct itemplate instrux_RDGSBASE[] = {
    {I_RDGSBASE, 1, {REG_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+31879, 134},
    {I_RDGSBASE, 1, {REG_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+31886, 134},
    ITEMPLATE_END
};

static const struct itemplate instrux_RDRAND[] = {
    {I_RDRAND, 1, {REG_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37348, 133},
    {I_RDRAND, 1, {REG_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37354, 133},
    {I_RDRAND, 1, {REG_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37360, 134},
    ITEMPLATE_END
};

static const struct itemplate instrux_WRFSBASE[] = {
    {I_WRFSBASE, 1, {REG_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+31893, 134},
    {I_WRFSBASE, 1, {REG_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+31900, 134},
    ITEMPLATE_END
};

static const struct itemplate instrux_WRGSBASE[] = {
    {I_WRGSBASE, 1, {REG_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+31907, 134},
    {I_WRGSBASE, 1, {REG_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+31914, 134},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTPH2PS[] = {
    {I_VCVTPH2PS, 2, {YMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+31921, 198},
    {I_VCVTPH2PS, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+31928, 198},
    {I_VCVTPH2PS, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+13268, 225},
    {I_VCVTPH2PS, 2, {YMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+13276, 225},
    {I_VCVTPH2PS, 2, {ZMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,SAE,0,0,0}, nasm_bytecodes+13284, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTPS2PH[] = {
    {I_VCVTPS2PH, 3, {RM_XMM_L16|BITS128,YMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+10260, 198},
    {I_VCVTPS2PH, 3, {RM_XMM_L16|BITS64,XMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+10268, 198},
    {I_VCVTPS2PH, 3, {XMMREG,XMMREG,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+4182, 225},
    {I_VCVTPS2PH, 3, {XMMREG,YMMREG,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+4191, 225},
    {I_VCVTPS2PH, 3, {YMMREG,ZMMREG,IMMEDIATE|BITS8,0,0}, {MASK|Z,SAE,0,0,0}, nasm_bytecodes+4200, 226},
    {I_VCVTPS2PH, 3, {MEMORY|BITS64,XMMREG,IMMEDIATE|BITS8,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4182, 225},
    {I_VCVTPS2PH, 3, {MEMORY|BITS128,YMMREG,IMMEDIATE|BITS8,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4191, 225},
    {I_VCVTPS2PH, 3, {MEMORY|BITS256,ZMMREG,IMMEDIATE|BITS8,0,0}, {MASK,SAE,0,0,0}, nasm_bytecodes+4200, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_ADCX[] = {
    {I_ADCX, 2, {REG_GPR|BITS32,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+10276, 133},
    {I_ADCX, 2, {REG_GPR|BITS64,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+10284, 134},
    ITEMPLATE_END
};

static const struct itemplate instrux_ADOX[] = {
    {I_ADOX, 2, {REG_GPR|BITS32,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+10292, 133},
    {I_ADOX, 2, {REG_GPR|BITS64,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+10300, 134},
    ITEMPLATE_END
};

static const struct itemplate instrux_RDSEED[] = {
    {I_RDSEED, 1, {REG_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37366, 133},
    {I_RDSEED, 1, {REG_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37372, 133},
    {I_RDSEED, 1, {REG_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37378, 134},
    ITEMPLATE_END
};

static const struct itemplate instrux_CLAC[] = {
    {I_CLAC, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40471, 199},
    ITEMPLATE_END
};

static const struct itemplate instrux_STAC[] = {
    {I_STAC, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40476, 199},
    ITEMPLATE_END
};

static const struct itemplate instrux_XSTORE[] = {
    {I_XSTORE, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40481, 36},
    ITEMPLATE_END
};

static const struct itemplate instrux_XCRYPTECB[] = {
    {I_XCRYPTECB, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37384, 36},
    ITEMPLATE_END
};

static const struct itemplate instrux_XCRYPTCBC[] = {
    {I_XCRYPTCBC, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37390, 36},
    ITEMPLATE_END
};

static const struct itemplate instrux_XCRYPTCTR[] = {
    {I_XCRYPTCTR, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37396, 36},
    ITEMPLATE_END
};

static const struct itemplate instrux_XCRYPTCFB[] = {
    {I_XCRYPTCFB, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37402, 36},
    ITEMPLATE_END
};

static const struct itemplate instrux_XCRYPTOFB[] = {
    {I_XCRYPTOFB, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37408, 36},
    ITEMPLATE_END
};

static const struct itemplate instrux_MONTMUL[] = {
    {I_MONTMUL, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37414, 36},
    ITEMPLATE_END
};

static const struct itemplate instrux_XSHA1[] = {
    {I_XSHA1, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37420, 36},
    ITEMPLATE_END
};

static const struct itemplate instrux_XSHA256[] = {
    {I_XSHA256, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37426, 36},
    ITEMPLATE_END
};

static const struct itemplate instrux_LLWPCB[] = {
    {I_LLWPCB, 1, {REG_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+31935, 200},
    {I_LLWPCB, 1, {REG_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+31942, 201},
    ITEMPLATE_END
};

static const struct itemplate instrux_SLWPCB[] = {
    {I_SLWPCB, 1, {REG_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+31949, 200},
    {I_SLWPCB, 1, {REG_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+31956, 201},
    ITEMPLATE_END
};

static const struct itemplate instrux_LWPVAL[] = {
    {I_LWPVAL, 3, {REG_GPR|BITS32,RM_GPR|BITS32,IMMEDIATE|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+10308, 200},
    {I_LWPVAL, 3, {REG_GPR|BITS64,RM_GPR|BITS32,IMMEDIATE|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+10316, 201},
    ITEMPLATE_END
};

static const struct itemplate instrux_LWPINS[] = {
    {I_LWPINS, 3, {REG_GPR|BITS32,RM_GPR|BITS32,IMMEDIATE|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+10324, 200},
    {I_LWPINS, 3, {REG_GPR|BITS64,RM_GPR|BITS32,IMMEDIATE|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+10332, 201},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADDPD[] = {
    {I_VFMADDPD, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+10340, 202},
    {I_VFMADDPD, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+10348, 202},
    {I_VFMADDPD, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0}, NO_DECORATOR, nasm_bytecodes+10356, 202},
    {I_VFMADDPD, 3, {YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+10364, 202},
    {I_VFMADDPD, 4, {XMM_L16,XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0}, NO_DECORATOR, nasm_bytecodes+10372, 202},
    {I_VFMADDPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+10380, 202},
    {I_VFMADDPD, 4, {YMM_L16,YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0}, NO_DECORATOR, nasm_bytecodes+10388, 202},
    {I_VFMADDPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+10396, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADDPS[] = {
    {I_VFMADDPS, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+10404, 202},
    {I_VFMADDPS, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+10412, 202},
    {I_VFMADDPS, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0}, NO_DECORATOR, nasm_bytecodes+10420, 202},
    {I_VFMADDPS, 3, {YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+10428, 202},
    {I_VFMADDPS, 4, {XMM_L16,XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0}, NO_DECORATOR, nasm_bytecodes+10436, 202},
    {I_VFMADDPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+10444, 202},
    {I_VFMADDPS, 4, {YMM_L16,YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0}, NO_DECORATOR, nasm_bytecodes+10452, 202},
    {I_VFMADDPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+10460, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADDSD[] = {
    {I_VFMADDSD, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+10468, 202},
    {I_VFMADDSD, 3, {XMM_L16,RM_XMM_L16|BITS64,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+10476, 202},
    {I_VFMADDSD, 4, {XMM_L16,XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0}, NO_DECORATOR, nasm_bytecodes+10484, 202},
    {I_VFMADDSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+10492, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADDSS[] = {
    {I_VFMADDSS, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+10500, 202},
    {I_VFMADDSS, 3, {XMM_L16,RM_XMM_L16|BITS32,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+10508, 202},
    {I_VFMADDSS, 4, {XMM_L16,XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0}, NO_DECORATOR, nasm_bytecodes+10516, 202},
    {I_VFMADDSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+10524, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADDSUBPD[] = {
    {I_VFMADDSUBPD, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+10532, 202},
    {I_VFMADDSUBPD, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+10540, 202},
    {I_VFMADDSUBPD, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0}, NO_DECORATOR, nasm_bytecodes+10548, 202},
    {I_VFMADDSUBPD, 3, {YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+10556, 202},
    {I_VFMADDSUBPD, 4, {XMM_L16,XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0}, NO_DECORATOR, nasm_bytecodes+10564, 202},
    {I_VFMADDSUBPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+10572, 202},
    {I_VFMADDSUBPD, 4, {YMM_L16,YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0}, NO_DECORATOR, nasm_bytecodes+10580, 202},
    {I_VFMADDSUBPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+10588, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMADDSUBPS[] = {
    {I_VFMADDSUBPS, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+10596, 202},
    {I_VFMADDSUBPS, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+10604, 202},
    {I_VFMADDSUBPS, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0}, NO_DECORATOR, nasm_bytecodes+10612, 202},
    {I_VFMADDSUBPS, 3, {YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+10620, 202},
    {I_VFMADDSUBPS, 4, {XMM_L16,XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0}, NO_DECORATOR, nasm_bytecodes+10628, 202},
    {I_VFMADDSUBPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+10636, 202},
    {I_VFMADDSUBPS, 4, {YMM_L16,YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0}, NO_DECORATOR, nasm_bytecodes+10644, 202},
    {I_VFMADDSUBPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+10652, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUBADDPD[] = {
    {I_VFMSUBADDPD, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+10660, 202},
    {I_VFMSUBADDPD, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+10668, 202},
    {I_VFMSUBADDPD, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0}, NO_DECORATOR, nasm_bytecodes+10676, 202},
    {I_VFMSUBADDPD, 3, {YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+10684, 202},
    {I_VFMSUBADDPD, 4, {XMM_L16,XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0}, NO_DECORATOR, nasm_bytecodes+10692, 202},
    {I_VFMSUBADDPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+10700, 202},
    {I_VFMSUBADDPD, 4, {YMM_L16,YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0}, NO_DECORATOR, nasm_bytecodes+10708, 202},
    {I_VFMSUBADDPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+10716, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUBADDPS[] = {
    {I_VFMSUBADDPS, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+10724, 202},
    {I_VFMSUBADDPS, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+10732, 202},
    {I_VFMSUBADDPS, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0}, NO_DECORATOR, nasm_bytecodes+10740, 202},
    {I_VFMSUBADDPS, 3, {YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+10748, 202},
    {I_VFMSUBADDPS, 4, {XMM_L16,XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0}, NO_DECORATOR, nasm_bytecodes+10756, 202},
    {I_VFMSUBADDPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+10764, 202},
    {I_VFMSUBADDPS, 4, {YMM_L16,YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0}, NO_DECORATOR, nasm_bytecodes+10772, 202},
    {I_VFMSUBADDPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+10780, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUBPD[] = {
    {I_VFMSUBPD, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+10788, 202},
    {I_VFMSUBPD, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+10796, 202},
    {I_VFMSUBPD, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0}, NO_DECORATOR, nasm_bytecodes+10804, 202},
    {I_VFMSUBPD, 3, {YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+10812, 202},
    {I_VFMSUBPD, 4, {XMM_L16,XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0}, NO_DECORATOR, nasm_bytecodes+10820, 202},
    {I_VFMSUBPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+10828, 202},
    {I_VFMSUBPD, 4, {YMM_L16,YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0}, NO_DECORATOR, nasm_bytecodes+10836, 202},
    {I_VFMSUBPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+10844, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUBPS[] = {
    {I_VFMSUBPS, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+10852, 202},
    {I_VFMSUBPS, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+10860, 202},
    {I_VFMSUBPS, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0}, NO_DECORATOR, nasm_bytecodes+10868, 202},
    {I_VFMSUBPS, 3, {YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+10876, 202},
    {I_VFMSUBPS, 4, {XMM_L16,XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0}, NO_DECORATOR, nasm_bytecodes+10884, 202},
    {I_VFMSUBPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+10892, 202},
    {I_VFMSUBPS, 4, {YMM_L16,YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0}, NO_DECORATOR, nasm_bytecodes+10900, 202},
    {I_VFMSUBPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+10908, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUBSD[] = {
    {I_VFMSUBSD, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+10916, 202},
    {I_VFMSUBSD, 3, {XMM_L16,RM_XMM_L16|BITS64,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+10924, 202},
    {I_VFMSUBSD, 4, {XMM_L16,XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0}, NO_DECORATOR, nasm_bytecodes+10932, 202},
    {I_VFMSUBSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+10940, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFMSUBSS[] = {
    {I_VFMSUBSS, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+10948, 202},
    {I_VFMSUBSS, 3, {XMM_L16,RM_XMM_L16|BITS32,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+10956, 202},
    {I_VFMSUBSS, 4, {XMM_L16,XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0}, NO_DECORATOR, nasm_bytecodes+10964, 202},
    {I_VFMSUBSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+10972, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMADDPD[] = {
    {I_VFNMADDPD, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+10980, 202},
    {I_VFNMADDPD, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+10988, 202},
    {I_VFNMADDPD, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0}, NO_DECORATOR, nasm_bytecodes+10996, 202},
    {I_VFNMADDPD, 3, {YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+11004, 202},
    {I_VFNMADDPD, 4, {XMM_L16,XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0}, NO_DECORATOR, nasm_bytecodes+11012, 202},
    {I_VFNMADDPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+11020, 202},
    {I_VFNMADDPD, 4, {YMM_L16,YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0}, NO_DECORATOR, nasm_bytecodes+11028, 202},
    {I_VFNMADDPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+11036, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMADDPS[] = {
    {I_VFNMADDPS, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+11044, 202},
    {I_VFNMADDPS, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+11052, 202},
    {I_VFNMADDPS, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0}, NO_DECORATOR, nasm_bytecodes+11060, 202},
    {I_VFNMADDPS, 3, {YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+11068, 202},
    {I_VFNMADDPS, 4, {XMM_L16,XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0}, NO_DECORATOR, nasm_bytecodes+11076, 202},
    {I_VFNMADDPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+11084, 202},
    {I_VFNMADDPS, 4, {YMM_L16,YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0}, NO_DECORATOR, nasm_bytecodes+11092, 202},
    {I_VFNMADDPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+11100, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMADDSD[] = {
    {I_VFNMADDSD, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+11108, 202},
    {I_VFNMADDSD, 3, {XMM_L16,RM_XMM_L16|BITS64,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+11116, 202},
    {I_VFNMADDSD, 4, {XMM_L16,XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0}, NO_DECORATOR, nasm_bytecodes+11124, 202},
    {I_VFNMADDSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+11132, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMADDSS[] = {
    {I_VFNMADDSS, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+11140, 202},
    {I_VFNMADDSS, 3, {XMM_L16,RM_XMM_L16|BITS32,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+11148, 202},
    {I_VFNMADDSS, 4, {XMM_L16,XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0}, NO_DECORATOR, nasm_bytecodes+11156, 202},
    {I_VFNMADDSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+11164, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMSUBPD[] = {
    {I_VFNMSUBPD, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+11172, 202},
    {I_VFNMSUBPD, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+11180, 202},
    {I_VFNMSUBPD, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0}, NO_DECORATOR, nasm_bytecodes+11188, 202},
    {I_VFNMSUBPD, 3, {YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+11196, 202},
    {I_VFNMSUBPD, 4, {XMM_L16,XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0}, NO_DECORATOR, nasm_bytecodes+11204, 202},
    {I_VFNMSUBPD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+11212, 202},
    {I_VFNMSUBPD, 4, {YMM_L16,YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0}, NO_DECORATOR, nasm_bytecodes+11220, 202},
    {I_VFNMSUBPD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+11228, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMSUBPS[] = {
    {I_VFNMSUBPS, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+11236, 202},
    {I_VFNMSUBPS, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+11244, 202},
    {I_VFNMSUBPS, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0}, NO_DECORATOR, nasm_bytecodes+11252, 202},
    {I_VFNMSUBPS, 3, {YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+11260, 202},
    {I_VFNMSUBPS, 4, {XMM_L16,XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0}, NO_DECORATOR, nasm_bytecodes+11268, 202},
    {I_VFNMSUBPS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+11276, 202},
    {I_VFNMSUBPS, 4, {YMM_L16,YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0}, NO_DECORATOR, nasm_bytecodes+11284, 202},
    {I_VFNMSUBPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+11292, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMSUBSD[] = {
    {I_VFNMSUBSD, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+11300, 202},
    {I_VFNMSUBSD, 3, {XMM_L16,RM_XMM_L16|BITS64,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+11308, 202},
    {I_VFNMSUBSD, 4, {XMM_L16,XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0}, NO_DECORATOR, nasm_bytecodes+11316, 202},
    {I_VFNMSUBSD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+11324, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFNMSUBSS[] = {
    {I_VFNMSUBSS, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+11332, 202},
    {I_VFNMSUBSS, 3, {XMM_L16,RM_XMM_L16|BITS32,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+11340, 202},
    {I_VFNMSUBSS, 4, {XMM_L16,XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0}, NO_DECORATOR, nasm_bytecodes+11348, 202},
    {I_VFNMSUBSS, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+11356, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFRCZPD[] = {
    {I_VFRCZPD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+31963, 202},
    {I_VFRCZPD, 1, {XMM_L16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+31970, 202},
    {I_VFRCZPD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+31977, 202},
    {I_VFRCZPD, 1, {YMM_L16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+31984, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFRCZPS[] = {
    {I_VFRCZPS, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+31991, 202},
    {I_VFRCZPS, 1, {XMM_L16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+31998, 202},
    {I_VFRCZPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+32005, 202},
    {I_VFRCZPS, 1, {YMM_L16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+32012, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFRCZSD[] = {
    {I_VFRCZSD, 2, {XMM_L16,RM_XMM_L16|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+32019, 202},
    {I_VFRCZSD, 1, {XMM_L16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+32026, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFRCZSS[] = {
    {I_VFRCZSS, 2, {XMM_L16,RM_XMM_L16|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+32033, 202},
    {I_VFRCZSS, 1, {XMM_L16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+32040, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCMOV[] = {
    {I_VPCMOV, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+11364, 202},
    {I_VPCMOV, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+11372, 202},
    {I_VPCMOV, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0}, NO_DECORATOR, nasm_bytecodes+11380, 202},
    {I_VPCMOV, 3, {YMM_L16,RM_YMM_L16|BITS256,YMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+11388, 202},
    {I_VPCMOV, 4, {XMM_L16,XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0}, NO_DECORATOR, nasm_bytecodes+11396, 202},
    {I_VPCMOV, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+11404, 202},
    {I_VPCMOV, 4, {YMM_L16,YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0}, NO_DECORATOR, nasm_bytecodes+11412, 202},
    {I_VPCMOV, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+11420, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCOMB[] = {
    {I_VPCOMB, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+11428, 202},
    {I_VPCOMB, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+11436, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCOMD[] = {
    {I_VPCOMD, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+11444, 202},
    {I_VPCOMD, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+11452, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCOMQ[] = {
    {I_VPCOMQ, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+11460, 202},
    {I_VPCOMQ, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+11468, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCOMUB[] = {
    {I_VPCOMUB, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+11476, 202},
    {I_VPCOMUB, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+11484, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCOMUD[] = {
    {I_VPCOMUD, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+11492, 202},
    {I_VPCOMUD, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+11500, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCOMUQ[] = {
    {I_VPCOMUQ, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+11508, 202},
    {I_VPCOMUQ, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+11516, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCOMUW[] = {
    {I_VPCOMUW, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+11524, 202},
    {I_VPCOMUW, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+11532, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCOMW[] = {
    {I_VPCOMW, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+11540, 202},
    {I_VPCOMW, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+11548, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPHADDBD[] = {
    {I_VPHADDBD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+32047, 202},
    {I_VPHADDBD, 1, {XMM_L16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+32054, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPHADDBQ[] = {
    {I_VPHADDBQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+32061, 202},
    {I_VPHADDBQ, 1, {XMM_L16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+32068, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPHADDBW[] = {
    {I_VPHADDBW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+32075, 202},
    {I_VPHADDBW, 1, {XMM_L16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+32082, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPHADDDQ[] = {
    {I_VPHADDDQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+32089, 202},
    {I_VPHADDDQ, 1, {XMM_L16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+32096, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPHADDUBD[] = {
    {I_VPHADDUBD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+32103, 202},
    {I_VPHADDUBD, 1, {XMM_L16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+32110, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPHADDUBQ[] = {
    {I_VPHADDUBQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+32117, 202},
    {I_VPHADDUBQ, 1, {XMM_L16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+32124, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPHADDUBW[] = {
    {I_VPHADDUBW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+32131, 202},
    {I_VPHADDUBW, 1, {XMM_L16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+32138, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPHADDUDQ[] = {
    {I_VPHADDUDQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+32145, 202},
    {I_VPHADDUDQ, 1, {XMM_L16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+32152, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPHADDUWD[] = {
    {I_VPHADDUWD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+32159, 202},
    {I_VPHADDUWD, 1, {XMM_L16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+32166, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPHADDUWQ[] = {
    {I_VPHADDUWQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+32173, 202},
    {I_VPHADDUWQ, 1, {XMM_L16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+32180, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPHADDWD[] = {
    {I_VPHADDWD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+32187, 202},
    {I_VPHADDWD, 1, {XMM_L16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+32194, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPHADDWQ[] = {
    {I_VPHADDWQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+32201, 202},
    {I_VPHADDWQ, 1, {XMM_L16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+32208, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPHSUBBW[] = {
    {I_VPHSUBBW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+32215, 202},
    {I_VPHSUBBW, 1, {XMM_L16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+32222, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPHSUBDQ[] = {
    {I_VPHSUBDQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+32229, 202},
    {I_VPHSUBDQ, 1, {XMM_L16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+32236, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPHSUBWD[] = {
    {I_VPHSUBWD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+32243, 202},
    {I_VPHSUBWD, 1, {XMM_L16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+32250, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMACSDD[] = {
    {I_VPMACSDD, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+11556, 202},
    {I_VPMACSDD, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+11564, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMACSDQH[] = {
    {I_VPMACSDQH, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+11572, 202},
    {I_VPMACSDQH, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+11580, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMACSDQL[] = {
    {I_VPMACSDQL, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+11588, 202},
    {I_VPMACSDQL, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+11596, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMACSSDD[] = {
    {I_VPMACSSDD, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+11604, 202},
    {I_VPMACSSDD, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+11612, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMACSSDQH[] = {
    {I_VPMACSSDQH, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+11620, 202},
    {I_VPMACSSDQH, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+11628, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMACSSDQL[] = {
    {I_VPMACSSDQL, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+11636, 202},
    {I_VPMACSSDQL, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+11644, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMACSSWD[] = {
    {I_VPMACSSWD, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+11652, 202},
    {I_VPMACSSWD, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+11660, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMACSSWW[] = {
    {I_VPMACSSWW, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+11668, 202},
    {I_VPMACSSWW, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+11676, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMACSWD[] = {
    {I_VPMACSWD, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+11684, 202},
    {I_VPMACSWD, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+11692, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMACSWW[] = {
    {I_VPMACSWW, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+11700, 202},
    {I_VPMACSWW, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+11708, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMADCSSWD[] = {
    {I_VPMADCSSWD, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+11716, 202},
    {I_VPMADCSSWD, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+11724, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMADCSWD[] = {
    {I_VPMADCSWD, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+11732, 202},
    {I_VPMADCSWD, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+11740, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPPERM[] = {
    {I_VPPERM, 4, {XMM_L16,XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0}, NO_DECORATOR, nasm_bytecodes+11748, 202},
    {I_VPPERM, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+11756, 202},
    {I_VPPERM, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0}, NO_DECORATOR, nasm_bytecodes+11764, 202},
    {I_VPPERM, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+11772, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPROTB[] = {
    {I_VPROTB, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+32257, 202},
    {I_VPROTB, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+32264, 202},
    {I_VPROTB, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+32271, 202},
    {I_VPROTB, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+32278, 202},
    {I_VPROTB, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+11780, 202},
    {I_VPROTB, 2, {XMM_L16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+11788, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPROTD[] = {
    {I_VPROTD, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+32285, 202},
    {I_VPROTD, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+32292, 202},
    {I_VPROTD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+32299, 202},
    {I_VPROTD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+32306, 202},
    {I_VPROTD, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+11796, 202},
    {I_VPROTD, 2, {XMM_L16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+11804, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPROTQ[] = {
    {I_VPROTQ, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+32313, 202},
    {I_VPROTQ, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+32320, 202},
    {I_VPROTQ, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+32327, 202},
    {I_VPROTQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+32334, 202},
    {I_VPROTQ, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+11812, 202},
    {I_VPROTQ, 2, {XMM_L16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+11820, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPROTW[] = {
    {I_VPROTW, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+32341, 202},
    {I_VPROTW, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+32348, 202},
    {I_VPROTW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+32355, 202},
    {I_VPROTW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+32362, 202},
    {I_VPROTW, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+11828, 202},
    {I_VPROTW, 2, {XMM_L16,IMMEDIATE|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+11836, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSHAB[] = {
    {I_VPSHAB, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+32369, 202},
    {I_VPSHAB, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+32376, 202},
    {I_VPSHAB, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+32383, 202},
    {I_VPSHAB, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+32390, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSHAD[] = {
    {I_VPSHAD, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+32397, 202},
    {I_VPSHAD, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+32404, 202},
    {I_VPSHAD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+32411, 202},
    {I_VPSHAD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+32418, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSHAQ[] = {
    {I_VPSHAQ, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+32425, 202},
    {I_VPSHAQ, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+32432, 202},
    {I_VPSHAQ, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+32439, 202},
    {I_VPSHAQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+32446, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSHAW[] = {
    {I_VPSHAW, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+32453, 202},
    {I_VPSHAW, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+32460, 202},
    {I_VPSHAW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+32467, 202},
    {I_VPSHAW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+32474, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSHLB[] = {
    {I_VPSHLB, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+32481, 202},
    {I_VPSHLB, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+32488, 202},
    {I_VPSHLB, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+32495, 202},
    {I_VPSHLB, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+32502, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSHLD[] = {
    {I_VPSHLD, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+32509, 202},
    {I_VPSHLD, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+32516, 202},
    {I_VPSHLD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+32523, 202},
    {I_VPSHLD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+32530, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSHLQ[] = {
    {I_VPSHLQ, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+32537, 202},
    {I_VPSHLQ, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+32544, 202},
    {I_VPSHLQ, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+32551, 202},
    {I_VPSHLQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+32558, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSHLW[] = {
    {I_VPSHLW, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+32565, 202},
    {I_VPSHLW, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+32572, 202},
    {I_VPSHLW, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+32579, 202},
    {I_VPSHLW, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+32586, 202},
    ITEMPLATE_END
};

static const struct itemplate instrux_VBROADCASTI128[] = {
    {I_VBROADCASTI128, 2, {YMM_L16,MEMORY|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+33860, 203},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPBLENDD[] = {
    {I_VPBLENDD, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+12092, 203},
    {I_VPBLENDD, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+12100, 203},
    {I_VPBLENDD, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+12108, 203},
    {I_VPBLENDD, 3, {YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+12116, 203},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPBROADCASTB[] = {
    {I_VPBROADCASTB, 2, {XMM_L16,MEMORY|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+33867, 203},
    {I_VPBROADCASTB, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+33867, 203},
    {I_VPBROADCASTB, 2, {YMM_L16,MEMORY|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+33874, 203},
    {I_VPBROADCASTB, 2, {YMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+33874, 203},
    {I_VPBROADCASTB, 2, {XMMREG,RM_XMM|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17820, 229},
    {I_VPBROADCASTB, 2, {YMMREG,RM_XMM|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17828, 229},
    {I_VPBROADCASTB, 2, {ZMMREG,RM_XMM|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17836, 230},
    {I_VPBROADCASTB, 2, {XMMREG,REG_GPR|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17844, 229},
    {I_VPBROADCASTB, 2, {XMMREG,REG_GPR|BITS16,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17844, 229},
    {I_VPBROADCASTB, 2, {XMMREG,REG_GPR|BITS32,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17844, 229},
    {I_VPBROADCASTB, 2, {XMMREG,REG_GPR|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17844, 229},
    {I_VPBROADCASTB, 2, {YMMREG,REG_GPR|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17852, 229},
    {I_VPBROADCASTB, 2, {YMMREG,REG_GPR|BITS16,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17852, 229},
    {I_VPBROADCASTB, 2, {YMMREG,REG_GPR|BITS32,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17852, 229},
    {I_VPBROADCASTB, 2, {YMMREG,REG_GPR|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17852, 229},
    {I_VPBROADCASTB, 2, {ZMMREG,REG_GPR|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17860, 230},
    {I_VPBROADCASTB, 2, {ZMMREG,REG_GPR|BITS16,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17860, 230},
    {I_VPBROADCASTB, 2, {ZMMREG,REG_GPR|BITS32,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17860, 230},
    {I_VPBROADCASTB, 2, {ZMMREG,REG_GPR|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17860, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPBROADCASTW[] = {
    {I_VPBROADCASTW, 2, {XMM_L16,MEMORY|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+33881, 203},
    {I_VPBROADCASTW, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+33881, 203},
    {I_VPBROADCASTW, 2, {YMM_L16,MEMORY|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+33888, 203},
    {I_VPBROADCASTW, 2, {YMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+33888, 203},
    {I_VPBROADCASTW, 2, {XMMREG,RM_XMM|BITS16,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18060, 229},
    {I_VPBROADCASTW, 2, {YMMREG,RM_XMM|BITS16,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18068, 229},
    {I_VPBROADCASTW, 2, {ZMMREG,RM_XMM|BITS16,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18076, 230},
    {I_VPBROADCASTW, 2, {XMMREG,REG_GPR|BITS16,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18084, 229},
    {I_VPBROADCASTW, 2, {XMMREG,REG_GPR|BITS32,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18084, 229},
    {I_VPBROADCASTW, 2, {XMMREG,REG_GPR|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18084, 229},
    {I_VPBROADCASTW, 2, {YMMREG,REG_GPR|BITS16,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18092, 229},
    {I_VPBROADCASTW, 2, {YMMREG,REG_GPR|BITS32,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18092, 229},
    {I_VPBROADCASTW, 2, {YMMREG,REG_GPR|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18092, 229},
    {I_VPBROADCASTW, 2, {ZMMREG,REG_GPR|BITS16,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18100, 230},
    {I_VPBROADCASTW, 2, {ZMMREG,REG_GPR|BITS32,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18100, 230},
    {I_VPBROADCASTW, 2, {ZMMREG,REG_GPR|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18100, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPBROADCASTD[] = {
    {I_VPBROADCASTD, 2, {XMM_L16,MEMORY|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+33895, 203},
    {I_VPBROADCASTD, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+33895, 203},
    {I_VPBROADCASTD, 2, {YMM_L16,MEMORY|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+33902, 203},
    {I_VPBROADCASTD, 2, {YMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+33902, 203},
    {I_VPBROADCASTD, 2, {XMMREG,MEMORY|BITS32,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17868, 225},
    {I_VPBROADCASTD, 2, {YMMREG,MEMORY|BITS32,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17876, 225},
    {I_VPBROADCASTD, 2, {ZMMREG,MEMORY|BITS32,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17884, 226},
    {I_VPBROADCASTD, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17892, 225},
    {I_VPBROADCASTD, 2, {YMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17900, 225},
    {I_VPBROADCASTD, 2, {ZMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17908, 226},
    {I_VPBROADCASTD, 2, {XMMREG,REG_GPR|BITS32,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17916, 225},
    {I_VPBROADCASTD, 2, {YMMREG,REG_GPR|BITS32,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17924, 225},
    {I_VPBROADCASTD, 2, {ZMMREG,REG_GPR|BITS32,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17932, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPBROADCASTQ[] = {
    {I_VPBROADCASTQ, 2, {XMM_L16,MEMORY|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+33909, 203},
    {I_VPBROADCASTQ, 2, {XMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+33909, 203},
    {I_VPBROADCASTQ, 2, {YMM_L16,MEMORY|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+33916, 203},
    {I_VPBROADCASTQ, 2, {YMM_L16,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+33916, 203},
    {I_VPBROADCASTQ, 2, {XMMREG,MEMORY|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17988, 225},
    {I_VPBROADCASTQ, 2, {YMMREG,MEMORY|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17996, 225},
    {I_VPBROADCASTQ, 2, {ZMMREG,MEMORY|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18004, 226},
    {I_VPBROADCASTQ, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18012, 225},
    {I_VPBROADCASTQ, 2, {YMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18020, 225},
    {I_VPBROADCASTQ, 2, {ZMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18028, 226},
    {I_VPBROADCASTQ, 2, {XMMREG,REG_GPR|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18036, 225},
    {I_VPBROADCASTQ, 2, {YMMREG,REG_GPR|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18044, 225},
    {I_VPBROADCASTQ, 2, {ZMMREG,REG_GPR|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18052, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPERMD[] = {
    {I_VPERMD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33923, 203},
    {I_VPERMD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33930, 203},
    {I_VPERMD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+18492, 225},
    {I_VPERMD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+18500, 225},
    {I_VPERMD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+18508, 226},
    {I_VPERMD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+18516, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPERMPD[] = {
    {I_VPERMPD, 3, {YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+12124, 203},
    {I_VPERMPD, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+5514, 225},
    {I_VPERMPD, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+5523, 226},
    {I_VPERMPD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+18764, 225},
    {I_VPERMPD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+18772, 225},
    {I_VPERMPD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+18780, 226},
    {I_VPERMPD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+18788, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPERMPS[] = {
    {I_VPERMPS, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33937, 203},
    {I_VPERMPS, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33944, 203},
    {I_VPERMPS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+18796, 225},
    {I_VPERMPS, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+18804, 225},
    {I_VPERMPS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+18812, 226},
    {I_VPERMPS, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+18820, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPERMQ[] = {
    {I_VPERMQ, 3, {YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+12132, 203},
    {I_VPERMQ, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+5532, 225},
    {I_VPERMQ, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+5541, 226},
    {I_VPERMQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+18828, 225},
    {I_VPERMQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+18836, 225},
    {I_VPERMQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+18844, 226},
    {I_VPERMQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+18852, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPERM2I128[] = {
    {I_VPERM2I128, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+12140, 203},
    {I_VPERM2I128, 3, {YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+12148, 203},
    ITEMPLATE_END
};

static const struct itemplate instrux_VEXTRACTI128[] = {
    {I_VEXTRACTI128, 3, {RM_XMM_L16|BITS128,YMM_L16,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+12156, 203},
    ITEMPLATE_END
};

static const struct itemplate instrux_VINSERTI128[] = {
    {I_VINSERTI128, 4, {YMM_L16,YMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+12164, 203},
    {I_VINSERTI128, 3, {YMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+12172, 203},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMASKMOVD[] = {
    {I_VPMASKMOVD, 3, {XMM_L16,XMM_L16,MEMORY|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+33951, 203},
    {I_VPMASKMOVD, 2, {XMM_L16,MEMORY|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+33958, 203},
    {I_VPMASKMOVD, 3, {YMM_L16,YMM_L16,MEMORY|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33965, 203},
    {I_VPMASKMOVD, 2, {YMM_L16,MEMORY|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+33972, 203},
    {I_VPMASKMOVD, 3, {MEMORY|BITS128,XMM_L16,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+34007, 203},
    {I_VPMASKMOVD, 2, {MEMORY|BITS128,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+34014, 203},
    {I_VPMASKMOVD, 3, {MEMORY|BITS256,YMM_L16,YMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+34021, 203},
    {I_VPMASKMOVD, 2, {MEMORY|BITS256,YMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+34028, 203},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMASKMOVQ[] = {
    {I_VPMASKMOVQ, 3, {XMM_L16,XMM_L16,MEMORY|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+33979, 203},
    {I_VPMASKMOVQ, 2, {XMM_L16,MEMORY|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+33986, 203},
    {I_VPMASKMOVQ, 3, {YMM_L16,YMM_L16,MEMORY|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+33993, 203},
    {I_VPMASKMOVQ, 2, {YMM_L16,MEMORY|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+34000, 203},
    {I_VPMASKMOVQ, 3, {MEMORY|BITS128,XMM_L16,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+34035, 203},
    {I_VPMASKMOVQ, 2, {MEMORY|BITS128,XMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+34042, 203},
    {I_VPMASKMOVQ, 3, {MEMORY|BITS256,YMM_L16,YMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+34049, 203},
    {I_VPMASKMOVQ, 2, {MEMORY|BITS256,YMM_L16,0,0,0}, NO_DECORATOR, nasm_bytecodes+34056, 203},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSLLVD[] = {
    {I_VPSLLVD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+34063, 203},
    {I_VPSLLVD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+34070, 203},
    {I_VPSLLVD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+34091, 203},
    {I_VPSLLVD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+34098, 203},
    {I_VPSLLVD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+22316, 225},
    {I_VPSLLVD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+22324, 225},
    {I_VPSLLVD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+22332, 225},
    {I_VPSLLVD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+22340, 225},
    {I_VPSLLVD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+22348, 226},
    {I_VPSLLVD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+22356, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSLLVQ[] = {
    {I_VPSLLVQ, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+34077, 203},
    {I_VPSLLVQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+34084, 203},
    {I_VPSLLVQ, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+34105, 203},
    {I_VPSLLVQ, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+34112, 203},
    {I_VPSLLVQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+22364, 225},
    {I_VPSLLVQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+22372, 225},
    {I_VPSLLVQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+22380, 225},
    {I_VPSLLVQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+22388, 225},
    {I_VPSLLVQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+22396, 226},
    {I_VPSLLVQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+22404, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSRAVD[] = {
    {I_VPSRAVD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+34119, 203},
    {I_VPSRAVD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+34126, 203},
    {I_VPSRAVD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+34133, 203},
    {I_VPSRAVD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+34140, 203},
    {I_VPSRAVD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+22604, 225},
    {I_VPSRAVD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+22612, 225},
    {I_VPSRAVD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+22620, 225},
    {I_VPSRAVD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+22628, 225},
    {I_VPSRAVD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+22636, 226},
    {I_VPSRAVD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+22644, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSRLVD[] = {
    {I_VPSRLVD, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+34147, 203},
    {I_VPSRLVD, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+34154, 203},
    {I_VPSRLVD, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+34175, 203},
    {I_VPSRLVD, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+34182, 203},
    {I_VPSRLVD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+22892, 225},
    {I_VPSRLVD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+22900, 225},
    {I_VPSRLVD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+22908, 225},
    {I_VPSRLVD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+22916, 225},
    {I_VPSRLVD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+22924, 226},
    {I_VPSRLVD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+22932, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSRLVQ[] = {
    {I_VPSRLVQ, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+34161, 203},
    {I_VPSRLVQ, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+34168, 203},
    {I_VPSRLVQ, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+34189, 203},
    {I_VPSRLVQ, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+34196, 203},
    {I_VPSRLVQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+22940, 225},
    {I_VPSRLVQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+22948, 225},
    {I_VPSRLVQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+22956, 225},
    {I_VPSRLVQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+22964, 225},
    {I_VPSRLVQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+22972, 226},
    {I_VPSRLVQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+22980, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VGATHERDPD[] = {
    {I_VGATHERDPD, 3, {XMM_L16,XMEM|BITS64,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+12180, 203},
    {I_VGATHERDPD, 3, {YMM_L16,XMEM|BITS64,YMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+12196, 203},
    {I_VGATHERDPD, 2, {XMMREG,XMEM|BITS64,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4704, 225},
    {I_VGATHERDPD, 2, {YMMREG,XMEM|BITS64,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4713, 225},
    {I_VGATHERDPD, 2, {ZMMREG,YMEM|BITS64,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4722, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VGATHERQPD[] = {
    {I_VGATHERQPD, 3, {XMM_L16,XMEM|BITS64,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+12188, 203},
    {I_VGATHERQPD, 3, {YMM_L16,YMEM|BITS64,YMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+12204, 203},
    {I_VGATHERQPD, 2, {XMMREG,XMEM|BITS64,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4830, 225},
    {I_VGATHERQPD, 2, {YMMREG,YMEM|BITS64,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4839, 225},
    {I_VGATHERQPD, 2, {ZMMREG,ZMEM|BITS64,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4848, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VGATHERDPS[] = {
    {I_VGATHERDPS, 3, {XMM_L16,XMEM|BITS32,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+12212, 203},
    {I_VGATHERDPS, 3, {YMM_L16,YMEM|BITS32,YMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+12228, 203},
    {I_VGATHERDPS, 2, {XMMREG,XMEM|BITS32,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4731, 225},
    {I_VGATHERDPS, 2, {YMMREG,YMEM|BITS32,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4740, 225},
    {I_VGATHERDPS, 2, {ZMMREG,ZMEM|BITS32,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4749, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VGATHERQPS[] = {
    {I_VGATHERQPS, 3, {XMM_L16,XMEM|BITS32,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+12220, 203},
    {I_VGATHERQPS, 3, {XMM_L16,YMEM|BITS32,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+12236, 203},
    {I_VGATHERQPS, 2, {XMMREG,XMEM|BITS32,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4857, 225},
    {I_VGATHERQPS, 2, {XMMREG,YMEM|BITS32,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4866, 225},
    {I_VGATHERQPS, 2, {YMMREG,ZMEM|BITS32,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4875, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPGATHERDD[] = {
    {I_VPGATHERDD, 3, {XMM_L16,XMEM|BITS32,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+12244, 203},
    {I_VPGATHERDD, 3, {YMM_L16,YMEM|BITS32,YMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+12260, 203},
    {I_VPGATHERDD, 2, {XMMREG,XMEM|BITS32,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+5595, 225},
    {I_VPGATHERDD, 2, {YMMREG,YMEM|BITS32,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+5604, 225},
    {I_VPGATHERDD, 2, {ZMMREG,ZMEM|BITS32,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+5613, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPGATHERQD[] = {
    {I_VPGATHERQD, 3, {XMM_L16,XMEM|BITS32,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+12252, 203},
    {I_VPGATHERQD, 3, {XMM_L16,YMEM|BITS32,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+12268, 203},
    {I_VPGATHERQD, 2, {XMMREG,XMEM|BITS32,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+5649, 225},
    {I_VPGATHERQD, 2, {XMMREG,YMEM|BITS32,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+5658, 225},
    {I_VPGATHERQD, 2, {YMMREG,ZMEM|BITS32,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+5667, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPGATHERDQ[] = {
    {I_VPGATHERDQ, 3, {XMM_L16,XMEM|BITS64,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+12276, 203},
    {I_VPGATHERDQ, 3, {YMM_L16,XMEM|BITS64,YMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+12292, 203},
    {I_VPGATHERDQ, 2, {XMMREG,XMEM|BITS64,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+5622, 225},
    {I_VPGATHERDQ, 2, {YMMREG,XMEM|BITS64,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+5631, 225},
    {I_VPGATHERDQ, 2, {ZMMREG,YMEM|BITS64,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+5640, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPGATHERQQ[] = {
    {I_VPGATHERQQ, 3, {XMM_L16,XMEM|BITS64,XMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+12284, 203},
    {I_VPGATHERQQ, 3, {YMM_L16,YMEM|BITS64,YMM_L16,0,0}, NO_DECORATOR, nasm_bytecodes+12300, 203},
    {I_VPGATHERQQ, 2, {XMMREG,XMEM|BITS64,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+5676, 225},
    {I_VPGATHERQQ, 2, {YMMREG,YMEM|BITS64,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+5685, 225},
    {I_VPGATHERQQ, 2, {ZMMREG,ZMEM|BITS64,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+5694, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_XABORT[] = {
    {I_XABORT, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40486, 204},
    {I_XABORT, 1, {IMMEDIATE|BITS8,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40486, 204},
    ITEMPLATE_END
};

static const struct itemplate instrux_XBEGIN[] = {
    {I_XBEGIN, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37432, 204},
    {I_XBEGIN, 1, {IMMEDIATE|NEAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37432, 204},
    {I_XBEGIN, 1, {IMMEDIATE|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37438, 205},
    {I_XBEGIN, 1, {IMMEDIATE|BITS16|NEAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37438, 205},
    {I_XBEGIN, 1, {IMMEDIATE|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37444, 205},
    {I_XBEGIN, 1, {IMMEDIATE|BITS32|NEAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37444, 205},
    {I_XBEGIN, 1, {IMMEDIATE|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37450, 206},
    {I_XBEGIN, 1, {IMMEDIATE|BITS64|NEAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37450, 206},
    ITEMPLATE_END
};

static const struct itemplate instrux_XEND[] = {
    {I_XEND, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40491, 204},
    ITEMPLATE_END
};

static const struct itemplate instrux_XTEST[] = {
    {I_XTEST, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40496, 207},
    ITEMPLATE_END
};

static const struct itemplate instrux_ANDN[] = {
    {I_ANDN, 3, {REG_GPR|BITS32,REG_GPR|BITS32,RM_GPR|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+34203, 208},
    {I_ANDN, 3, {REG_GPR|BITS64,REG_GPR|BITS64,RM_GPR|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+34210, 209},
    ITEMPLATE_END
};

static const struct itemplate instrux_BEXTR[] = {
    {I_BEXTR, 3, {REG_GPR|BITS32,RM_GPR|BITS32,REG_GPR|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+34217, 208},
    {I_BEXTR, 3, {REG_GPR|BITS64,RM_GPR|BITS64,REG_GPR|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+34224, 209},
    {I_BEXTR, 3, {REG_GPR|BITS32,RM_GPR|BITS32,IMMEDIATE|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+12308, 210},
    {I_BEXTR, 3, {REG_GPR|BITS64,RM_GPR|BITS64,IMMEDIATE|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+12316, 211},
    ITEMPLATE_END
};

static const struct itemplate instrux_BLCI[] = {
    {I_BLCI, 2, {REG_GPR|BITS32,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+34231, 210},
    {I_BLCI, 2, {REG_GPR|BITS64,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+34238, 211},
    ITEMPLATE_END
};

static const struct itemplate instrux_BLCIC[] = {
    {I_BLCIC, 2, {REG_GPR|BITS32,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+34245, 210},
    {I_BLCIC, 2, {REG_GPR|BITS64,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+34252, 211},
    ITEMPLATE_END
};

static const struct itemplate instrux_BLSI[] = {
    {I_BLSI, 2, {REG_GPR|BITS32,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+34259, 208},
    {I_BLSI, 2, {REG_GPR|BITS64,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+34266, 209},
    ITEMPLATE_END
};

static const struct itemplate instrux_BLSIC[] = {
    {I_BLSIC, 2, {REG_GPR|BITS32,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+34273, 210},
    {I_BLSIC, 2, {REG_GPR|BITS64,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+34280, 211},
    ITEMPLATE_END
};

static const struct itemplate instrux_BLCFILL[] = {
    {I_BLCFILL, 2, {REG_GPR|BITS32,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+34287, 210},
    {I_BLCFILL, 2, {REG_GPR|BITS64,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+34294, 211},
    ITEMPLATE_END
};

static const struct itemplate instrux_BLSFILL[] = {
    {I_BLSFILL, 2, {REG_GPR|BITS32,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+34301, 210},
    {I_BLSFILL, 2, {REG_GPR|BITS64,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+34308, 211},
    ITEMPLATE_END
};

static const struct itemplate instrux_BLCMSK[] = {
    {I_BLCMSK, 2, {REG_GPR|BITS32,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+34315, 210},
    {I_BLCMSK, 2, {REG_GPR|BITS64,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+34322, 211},
    ITEMPLATE_END
};

static const struct itemplate instrux_BLSMSK[] = {
    {I_BLSMSK, 2, {REG_GPR|BITS32,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+34329, 208},
    {I_BLSMSK, 2, {REG_GPR|BITS64,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+34336, 209},
    ITEMPLATE_END
};

static const struct itemplate instrux_BLSR[] = {
    {I_BLSR, 2, {REG_GPR|BITS32,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+34343, 208},
    {I_BLSR, 2, {REG_GPR|BITS64,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+34350, 209},
    ITEMPLATE_END
};

static const struct itemplate instrux_BLCS[] = {
    {I_BLCS, 2, {REG_GPR|BITS32,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+34357, 210},
    {I_BLCS, 2, {REG_GPR|BITS64,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+34364, 211},
    ITEMPLATE_END
};

static const struct itemplate instrux_BZHI[] = {
    {I_BZHI, 3, {REG_GPR|BITS32,RM_GPR|BITS32,REG_GPR|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+34371, 212},
    {I_BZHI, 3, {REG_GPR|BITS64,RM_GPR|BITS64,REG_GPR|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+34378, 213},
    ITEMPLATE_END
};

static const struct itemplate instrux_MULX[] = {
    {I_MULX, 3, {REG_GPR|BITS32,REG_GPR|BITS32,RM_GPR|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+34385, 212},
    {I_MULX, 3, {REG_GPR|BITS64,REG_GPR|BITS64,RM_GPR|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+34392, 213},
    ITEMPLATE_END
};

static const struct itemplate instrux_PDEP[] = {
    {I_PDEP, 3, {REG_GPR|BITS32,REG_GPR|BITS32,RM_GPR|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+34399, 212},
    {I_PDEP, 3, {REG_GPR|BITS64,REG_GPR|BITS64,RM_GPR|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+34406, 213},
    ITEMPLATE_END
};

static const struct itemplate instrux_PEXT[] = {
    {I_PEXT, 3, {REG_GPR|BITS32,REG_GPR|BITS32,RM_GPR|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+34413, 212},
    {I_PEXT, 3, {REG_GPR|BITS64,REG_GPR|BITS64,RM_GPR|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+34420, 213},
    ITEMPLATE_END
};

static const struct itemplate instrux_RORX[] = {
    {I_RORX, 3, {REG_GPR|BITS32,RM_GPR|BITS32,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+12324, 212},
    {I_RORX, 3, {REG_GPR|BITS64,RM_GPR|BITS64,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+12332, 213},
    ITEMPLATE_END
};

static const struct itemplate instrux_SARX[] = {
    {I_SARX, 3, {REG_GPR|BITS32,RM_GPR|BITS32,REG_GPR|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+34427, 212},
    {I_SARX, 3, {REG_GPR|BITS64,RM_GPR|BITS64,REG_GPR|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+34434, 213},
    ITEMPLATE_END
};

static const struct itemplate instrux_SHLX[] = {
    {I_SHLX, 3, {REG_GPR|BITS32,RM_GPR|BITS32,REG_GPR|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+34441, 212},
    {I_SHLX, 3, {REG_GPR|BITS64,RM_GPR|BITS64,REG_GPR|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+34448, 213},
    ITEMPLATE_END
};

static const struct itemplate instrux_SHRX[] = {
    {I_SHRX, 3, {REG_GPR|BITS32,RM_GPR|BITS32,REG_GPR|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+34455, 212},
    {I_SHRX, 3, {REG_GPR|BITS64,RM_GPR|BITS64,REG_GPR|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+34462, 213},
    ITEMPLATE_END
};

static const struct itemplate instrux_TZCNT[] = {
    {I_TZCNT, 2, {REG_GPR|BITS16,RM_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+34469, 214},
    {I_TZCNT, 2, {REG_GPR|BITS32,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+34476, 214},
    {I_TZCNT, 2, {REG_GPR|BITS64,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+34483, 215},
    ITEMPLATE_END
};

static const struct itemplate instrux_TZMSK[] = {
    {I_TZMSK, 2, {REG_GPR|BITS32,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+34490, 210},
    {I_TZMSK, 2, {REG_GPR|BITS64,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+34497, 211},
    ITEMPLATE_END
};

static const struct itemplate instrux_T1MSKC[] = {
    {I_T1MSKC, 2, {REG_GPR|BITS32,RM_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+34504, 210},
    {I_T1MSKC, 2, {REG_GPR|BITS64,RM_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+34511, 211},
    ITEMPLATE_END
};

static const struct itemplate instrux_PREFETCHWT1[] = {
    {I_PREFETCHWT1, 1, {MEMORY|BITS8,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40501, 216},
    ITEMPLATE_END
};

static const struct itemplate instrux_BNDMK[] = {
    {I_BNDMK, 2, {BNDREG,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+37456, 217},
    ITEMPLATE_END
};

static const struct itemplate instrux_BNDCL[] = {
    {I_BNDCL, 2, {BNDREG,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+34519, 218},
    {I_BNDCL, 2, {BNDREG,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+34519, 219},
    {I_BNDCL, 2, {BNDREG,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+34518, 220},
    ITEMPLATE_END
};

static const struct itemplate instrux_BNDCU[] = {
    {I_BNDCU, 2, {BNDREG,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+34526, 218},
    {I_BNDCU, 2, {BNDREG,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+34526, 219},
    {I_BNDCU, 2, {BNDREG,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+34525, 220},
    ITEMPLATE_END
};

static const struct itemplate instrux_BNDCN[] = {
    {I_BNDCN, 2, {BNDREG,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+34533, 218},
    {I_BNDCN, 2, {BNDREG,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+34533, 219},
    {I_BNDCN, 2, {BNDREG,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+34532, 220},
    ITEMPLATE_END
};

static const struct itemplate instrux_BNDMOV[] = {
    {I_BNDMOV, 2, {BNDREG,BNDREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+37462, 218},
    {I_BNDMOV, 2, {BNDREG,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+37462, 218},
    {I_BNDMOV, 2, {BNDREG,BNDREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+37468, 218},
    {I_BNDMOV, 2, {MEMORY,BNDREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+37468, 218},
    ITEMPLATE_END
};

static const struct itemplate instrux_BNDLDX[] = {
    {I_BNDLDX, 2, {BNDREG,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+37463, 217},
    {I_BNDLDX, 3, {BNDREG,MEMORY,REG_GPR|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+37474, 221},
    {I_BNDLDX, 3, {BNDREG,MEMORY,REG_GPR|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+37474, 222},
    ITEMPLATE_END
};

static const struct itemplate instrux_BNDSTX[] = {
    {I_BNDSTX, 2, {MEMORY,BNDREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+37469, 217},
    {I_BNDSTX, 3, {MEMORY,REG_GPR|BITS32,BNDREG,0,0}, NO_DECORATOR, nasm_bytecodes+37480, 221},
    {I_BNDSTX, 3, {MEMORY,REG_GPR|BITS64,BNDREG,0,0}, NO_DECORATOR, nasm_bytecodes+37480, 222},
    {I_BNDSTX, 3, {MEMORY,BNDREG,REG_GPR|BITS32,0,0}, NO_DECORATOR, nasm_bytecodes+37486, 221},
    {I_BNDSTX, 3, {MEMORY,BNDREG,REG_GPR|BITS64,0,0}, NO_DECORATOR, nasm_bytecodes+37486, 222},
    ITEMPLATE_END
};

static const struct itemplate instrux_SHA1MSG1[] = {
    {I_SHA1MSG1, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+37492, 223},
    ITEMPLATE_END
};

static const struct itemplate instrux_SHA1MSG2[] = {
    {I_SHA1MSG2, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+37498, 223},
    ITEMPLATE_END
};

static const struct itemplate instrux_SHA1NEXTE[] = {
    {I_SHA1NEXTE, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+37504, 223},
    ITEMPLATE_END
};

static const struct itemplate instrux_SHA1RNDS4[] = {
    {I_SHA1RNDS4, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+34539, 223},
    ITEMPLATE_END
};

static const struct itemplate instrux_SHA256MSG1[] = {
    {I_SHA256MSG1, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+37510, 223},
    ITEMPLATE_END
};

static const struct itemplate instrux_SHA256MSG2[] = {
    {I_SHA256MSG2, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+37516, 223},
    ITEMPLATE_END
};

static const struct itemplate instrux_SHA256RNDS2[] = {
    {I_SHA256RNDS2, 3, {XMM_L16,RM_XMM_L16|BITS128,XMM0,0,0}, NO_DECORATOR, nasm_bytecodes+37522, 223},
    {I_SHA256RNDS2, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+37522, 223},
    ITEMPLATE_END
};

static const struct itemplate instrux_KADDB[] = {
    {I_KADDB, 3, {KREG,KREG,KREG,0,0}, NO_DECORATOR, nasm_bytecodes+34546, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KADDD[] = {
    {I_KADDD, 3, {KREG,KREG,KREG,0,0}, NO_DECORATOR, nasm_bytecodes+34553, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KADDQ[] = {
    {I_KADDQ, 3, {KREG,KREG,KREG,0,0}, NO_DECORATOR, nasm_bytecodes+34560, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KADDW[] = {
    {I_KADDW, 3, {KREG,KREG,KREG,0,0}, NO_DECORATOR, nasm_bytecodes+34567, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KANDB[] = {
    {I_KANDB, 3, {KREG,KREG,KREG,0,0}, NO_DECORATOR, nasm_bytecodes+34574, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KANDD[] = {
    {I_KANDD, 3, {KREG,KREG,KREG,0,0}, NO_DECORATOR, nasm_bytecodes+34581, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KANDNB[] = {
    {I_KANDNB, 3, {KREG,KREG,KREG,0,0}, NO_DECORATOR, nasm_bytecodes+34588, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KANDND[] = {
    {I_KANDND, 3, {KREG,KREG,KREG,0,0}, NO_DECORATOR, nasm_bytecodes+34595, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KANDNQ[] = {
    {I_KANDNQ, 3, {KREG,KREG,KREG,0,0}, NO_DECORATOR, nasm_bytecodes+34602, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KANDNW[] = {
    {I_KANDNW, 3, {KREG,KREG,KREG,0,0}, NO_DECORATOR, nasm_bytecodes+34609, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KANDQ[] = {
    {I_KANDQ, 3, {KREG,KREG,KREG,0,0}, NO_DECORATOR, nasm_bytecodes+34616, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KANDW[] = {
    {I_KANDW, 3, {KREG,KREG,KREG,0,0}, NO_DECORATOR, nasm_bytecodes+34623, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KMOVB[] = {
    {I_KMOVB, 2, {KREG,RM_K|BITS8,0,0,0}, NO_DECORATOR, nasm_bytecodes+34630, 224},
    {I_KMOVB, 2, {MEMORY|BITS8,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+34637, 224},
    {I_KMOVB, 2, {KREG,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+34644, 224},
    {I_KMOVB, 2, {REG_GPR|BITS32,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+34651, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KMOVD[] = {
    {I_KMOVD, 2, {KREG,RM_K|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+34658, 224},
    {I_KMOVD, 2, {MEMORY|BITS32,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+34665, 224},
    {I_KMOVD, 2, {KREG,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+34672, 224},
    {I_KMOVD, 2, {REG_GPR|BITS32,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+34679, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KMOVQ[] = {
    {I_KMOVQ, 2, {KREG,RM_K|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+34686, 224},
    {I_KMOVQ, 2, {MEMORY|BITS64,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+34693, 224},
    {I_KMOVQ, 2, {KREG,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+34700, 224},
    {I_KMOVQ, 2, {REG_GPR|BITS64,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+34707, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KMOVW[] = {
    {I_KMOVW, 2, {KREG,RM_K|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+34714, 224},
    {I_KMOVW, 2, {MEMORY|BITS16,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+34721, 224},
    {I_KMOVW, 2, {KREG,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+34728, 224},
    {I_KMOVW, 2, {REG_GPR|BITS32,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+34735, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KNOTB[] = {
    {I_KNOTB, 2, {KREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+34742, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KNOTD[] = {
    {I_KNOTD, 2, {KREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+34749, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KNOTQ[] = {
    {I_KNOTQ, 2, {KREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+34756, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KNOTW[] = {
    {I_KNOTW, 2, {KREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+34763, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KORB[] = {
    {I_KORB, 3, {KREG,KREG,KREG,0,0}, NO_DECORATOR, nasm_bytecodes+34770, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KORD[] = {
    {I_KORD, 3, {KREG,KREG,KREG,0,0}, NO_DECORATOR, nasm_bytecodes+34777, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KORQ[] = {
    {I_KORQ, 3, {KREG,KREG,KREG,0,0}, NO_DECORATOR, nasm_bytecodes+34784, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KORTESTB[] = {
    {I_KORTESTB, 2, {KREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+34791, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KORTESTD[] = {
    {I_KORTESTD, 2, {KREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+34798, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KORTESTQ[] = {
    {I_KORTESTQ, 2, {KREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+34805, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KORTESTW[] = {
    {I_KORTESTW, 2, {KREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+34812, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KORW[] = {
    {I_KORW, 3, {KREG,KREG,KREG,0,0}, NO_DECORATOR, nasm_bytecodes+34819, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KSHIFTLB[] = {
    {I_KSHIFTLB, 3, {KREG,KREG,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+12340, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KSHIFTLD[] = {
    {I_KSHIFTLD, 3, {KREG,KREG,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+12348, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KSHIFTLQ[] = {
    {I_KSHIFTLQ, 3, {KREG,KREG,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+12356, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KSHIFTLW[] = {
    {I_KSHIFTLW, 3, {KREG,KREG,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+12364, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KSHIFTRB[] = {
    {I_KSHIFTRB, 3, {KREG,KREG,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+12372, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KSHIFTRD[] = {
    {I_KSHIFTRD, 3, {KREG,KREG,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+12380, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KSHIFTRQ[] = {
    {I_KSHIFTRQ, 3, {KREG,KREG,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+12388, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KSHIFTRW[] = {
    {I_KSHIFTRW, 3, {KREG,KREG,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+12396, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KTESTB[] = {
    {I_KTESTB, 2, {KREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+34826, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KTESTD[] = {
    {I_KTESTD, 2, {KREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+34833, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KTESTQ[] = {
    {I_KTESTQ, 2, {KREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+34840, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KTESTW[] = {
    {I_KTESTW, 2, {KREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+34847, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KUNPCKBW[] = {
    {I_KUNPCKBW, 3, {KREG,KREG,KREG,0,0}, NO_DECORATOR, nasm_bytecodes+34854, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KUNPCKDQ[] = {
    {I_KUNPCKDQ, 3, {KREG,KREG,KREG,0,0}, NO_DECORATOR, nasm_bytecodes+34861, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KUNPCKWD[] = {
    {I_KUNPCKWD, 3, {KREG,KREG,KREG,0,0}, NO_DECORATOR, nasm_bytecodes+34868, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KXNORB[] = {
    {I_KXNORB, 3, {KREG,KREG,KREG,0,0}, NO_DECORATOR, nasm_bytecodes+34875, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KXNORD[] = {
    {I_KXNORD, 3, {KREG,KREG,KREG,0,0}, NO_DECORATOR, nasm_bytecodes+34882, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KXNORQ[] = {
    {I_KXNORQ, 3, {KREG,KREG,KREG,0,0}, NO_DECORATOR, nasm_bytecodes+34889, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KXNORW[] = {
    {I_KXNORW, 3, {KREG,KREG,KREG,0,0}, NO_DECORATOR, nasm_bytecodes+34896, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KXORB[] = {
    {I_KXORB, 3, {KREG,KREG,KREG,0,0}, NO_DECORATOR, nasm_bytecodes+34903, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KXORD[] = {
    {I_KXORD, 3, {KREG,KREG,KREG,0,0}, NO_DECORATOR, nasm_bytecodes+34910, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KXORQ[] = {
    {I_KXORQ, 3, {KREG,KREG,KREG,0,0}, NO_DECORATOR, nasm_bytecodes+34917, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_KXORW[] = {
    {I_KXORW, 3, {KREG,KREG,KREG,0,0}, NO_DECORATOR, nasm_bytecodes+34924, 224},
    ITEMPLATE_END
};

static const struct itemplate instrux_VALIGND[] = {
    {I_VALIGND, 4, {XMMREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+4002, 225},
    {I_VALIGND, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+4011, 225},
    {I_VALIGND, 4, {YMMREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+4020, 225},
    {I_VALIGND, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+4029, 225},
    {I_VALIGND, 4, {ZMMREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+4038, 226},
    {I_VALIGND, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+4047, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VALIGNQ[] = {
    {I_VALIGNQ, 4, {XMMREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+4056, 225},
    {I_VALIGNQ, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+4065, 225},
    {I_VALIGNQ, 4, {YMMREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+4074, 225},
    {I_VALIGNQ, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+4083, 225},
    {I_VALIGNQ, 4, {ZMMREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+4092, 226},
    {I_VALIGNQ, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+4101, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VBLENDMPD[] = {
    {I_VBLENDMPD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+12724, 225},
    {I_VBLENDMPD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+12732, 225},
    {I_VBLENDMPD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+12740, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VBLENDMPS[] = {
    {I_VBLENDMPS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+12748, 225},
    {I_VBLENDMPS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+12756, 225},
    {I_VBLENDMPS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+12764, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VBROADCASTF32X2[] = {
    {I_VBROADCASTF32X2, 2, {YMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+12772, 227},
    {I_VBROADCASTF32X2, 2, {ZMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+12780, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VBROADCASTF32X4[] = {
    {I_VBROADCASTF32X4, 2, {YMMREG,MEMORY|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+12788, 225},
    {I_VBROADCASTF32X4, 2, {ZMMREG,MEMORY|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+12796, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VBROADCASTF32X8[] = {
    {I_VBROADCASTF32X8, 2, {ZMMREG,MEMORY|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+12804, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VBROADCASTF64X2[] = {
    {I_VBROADCASTF64X2, 2, {YMMREG,MEMORY|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+12812, 227},
    {I_VBROADCASTF64X2, 2, {ZMMREG,MEMORY|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+12820, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VBROADCASTF64X4[] = {
    {I_VBROADCASTF64X4, 2, {ZMMREG,MEMORY|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+12828, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VBROADCASTI32X2[] = {
    {I_VBROADCASTI32X2, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+12836, 227},
    {I_VBROADCASTI32X2, 2, {YMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+12844, 227},
    {I_VBROADCASTI32X2, 2, {ZMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+12852, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VBROADCASTI32X4[] = {
    {I_VBROADCASTI32X4, 2, {YMMREG,MEMORY|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+12860, 225},
    {I_VBROADCASTI32X4, 2, {ZMMREG,MEMORY|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+12868, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VBROADCASTI32X8[] = {
    {I_VBROADCASTI32X8, 2, {ZMMREG,MEMORY|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+12876, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VBROADCASTI64X2[] = {
    {I_VBROADCASTI64X2, 2, {YMMREG,MEMORY|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+12884, 227},
    {I_VBROADCASTI64X2, 2, {ZMMREG,MEMORY|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+12892, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VBROADCASTI64X4[] = {
    {I_VBROADCASTI64X4, 2, {ZMMREG,MEMORY|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+12900, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCOMPRESSPD[] = {
    {I_VCOMPRESSPD, 2, {MEMORY|BITS128,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+13004, 225},
    {I_VCOMPRESSPD, 2, {MEMORY|BITS256,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+13012, 225},
    {I_VCOMPRESSPD, 2, {MEMORY|BITS512,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+13020, 226},
    {I_VCOMPRESSPD, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+13028, 225},
    {I_VCOMPRESSPD, 2, {YMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+13036, 225},
    {I_VCOMPRESSPD, 2, {ZMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+13044, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCOMPRESSPS[] = {
    {I_VCOMPRESSPS, 2, {MEMORY|BITS128,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+13052, 225},
    {I_VCOMPRESSPS, 2, {MEMORY|BITS256,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+13060, 225},
    {I_VCOMPRESSPS, 2, {MEMORY|BITS512,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+13068, 226},
    {I_VCOMPRESSPS, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+13076, 225},
    {I_VCOMPRESSPS, 2, {YMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+13084, 225},
    {I_VCOMPRESSPS, 2, {ZMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+13092, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTPD2QQ[] = {
    {I_VCVTPD2QQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+13196, 227},
    {I_VCVTPD2QQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+13204, 227},
    {I_VCVTPD2QQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64|ER,0,0,0}, nasm_bytecodes+13212, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTPD2UDQ[] = {
    {I_VCVTPD2UDQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+13220, 225},
    {I_VCVTPD2UDQ, 2, {XMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+13228, 225},
    {I_VCVTPD2UDQ, 2, {YMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64|ER,0,0,0}, nasm_bytecodes+13236, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTPD2UQQ[] = {
    {I_VCVTPD2UQQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+13244, 227},
    {I_VCVTPD2UQQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+13252, 227},
    {I_VCVTPD2UQQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64|ER,0,0,0}, nasm_bytecodes+13260, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTPS2QQ[] = {
    {I_VCVTPS2QQ, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+13340, 227},
    {I_VCVTPS2QQ, 2, {YMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+13348, 227},
    {I_VCVTPS2QQ, 2, {ZMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32|ER,0,0,0}, nasm_bytecodes+13356, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTPS2UDQ[] = {
    {I_VCVTPS2UDQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+13364, 225},
    {I_VCVTPS2UDQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+13372, 225},
    {I_VCVTPS2UDQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32|ER,0,0,0}, nasm_bytecodes+13380, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTPS2UQQ[] = {
    {I_VCVTPS2UQQ, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+13388, 227},
    {I_VCVTPS2UQQ, 2, {YMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+13396, 227},
    {I_VCVTPS2UQQ, 2, {ZMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32|ER,0,0,0}, nasm_bytecodes+13404, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTQQ2PD[] = {
    {I_VCVTQQ2PD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+13412, 227},
    {I_VCVTQQ2PD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+13420, 227},
    {I_VCVTQQ2PD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64|ER,0,0,0}, nasm_bytecodes+13428, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTQQ2PS[] = {
    {I_VCVTQQ2PS, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+13436, 227},
    {I_VCVTQQ2PS, 2, {XMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+13444, 227},
    {I_VCVTQQ2PS, 2, {YMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64|ER,0,0,0}, nasm_bytecodes+13452, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTSD2USI[] = {
    {I_VCVTSD2USI, 2, {REG_GPR|BITS32,RM_XMM|BITS64,0,0,0}, {0,ER,0,0,0}, nasm_bytecodes+13484, 226},
    {I_VCVTSD2USI, 2, {REG_GPR|BITS64,RM_XMM|BITS64,0,0,0}, {0,ER,0,0,0}, nasm_bytecodes+13492, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTSS2USI[] = {
    {I_VCVTSS2USI, 2, {REG_GPR|BITS32,RM_XMM|BITS32,0,0,0}, {0,ER,0,0,0}, nasm_bytecodes+13556, 226},
    {I_VCVTSS2USI, 2, {REG_GPR|BITS64,RM_XMM|BITS32,0,0,0}, {0,ER,0,0,0}, nasm_bytecodes+13564, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTTPD2QQ[] = {
    {I_VCVTTPD2QQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+13596, 227},
    {I_VCVTTPD2QQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+13604, 227},
    {I_VCVTTPD2QQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64|SAE,0,0,0}, nasm_bytecodes+13612, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTTPD2UDQ[] = {
    {I_VCVTTPD2UDQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+13620, 225},
    {I_VCVTTPD2UDQ, 2, {XMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+13628, 225},
    {I_VCVTTPD2UDQ, 2, {YMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64|SAE,0,0,0}, nasm_bytecodes+13636, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTTPD2UQQ[] = {
    {I_VCVTTPD2UQQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+13644, 227},
    {I_VCVTTPD2UQQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+13652, 227},
    {I_VCVTTPD2UQQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64|SAE,0,0,0}, nasm_bytecodes+13660, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTTPS2QQ[] = {
    {I_VCVTTPS2QQ, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+13692, 227},
    {I_VCVTTPS2QQ, 2, {YMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+13700, 227},
    {I_VCVTTPS2QQ, 2, {ZMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32|SAE,0,0,0}, nasm_bytecodes+13708, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTTPS2UDQ[] = {
    {I_VCVTTPS2UDQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+13716, 225},
    {I_VCVTTPS2UDQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+13724, 225},
    {I_VCVTTPS2UDQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32|SAE,0,0,0}, nasm_bytecodes+13732, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTTPS2UQQ[] = {
    {I_VCVTTPS2UQQ, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+13740, 227},
    {I_VCVTTPS2UQQ, 2, {YMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+13748, 227},
    {I_VCVTTPS2UQQ, 2, {ZMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32|SAE,0,0,0}, nasm_bytecodes+13756, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTTSD2USI[] = {
    {I_VCVTTSD2USI, 2, {REG_GPR|BITS32,RM_XMM|BITS64,0,0,0}, {0,SAE,0,0,0}, nasm_bytecodes+13780, 226},
    {I_VCVTTSD2USI, 2, {REG_GPR|BITS64,RM_XMM|BITS64,0,0,0}, {0,SAE,0,0,0}, nasm_bytecodes+13788, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTTSS2USI[] = {
    {I_VCVTTSS2USI, 2, {REG_GPR|BITS32,RM_XMM|BITS32,0,0,0}, {0,SAE,0,0,0}, nasm_bytecodes+13812, 226},
    {I_VCVTTSS2USI, 2, {REG_GPR|BITS64,RM_XMM|BITS32,0,0,0}, {0,SAE,0,0,0}, nasm_bytecodes+13820, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTUDQ2PD[] = {
    {I_VCVTUDQ2PD, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+13828, 225},
    {I_VCVTUDQ2PD, 2, {YMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+13836, 225},
    {I_VCVTUDQ2PD, 2, {ZMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32|ER,0,0,0}, nasm_bytecodes+13844, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTUDQ2PS[] = {
    {I_VCVTUDQ2PS, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+13852, 225},
    {I_VCVTUDQ2PS, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+13860, 225},
    {I_VCVTUDQ2PS, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32|ER,0,0,0}, nasm_bytecodes+13868, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTUQQ2PD[] = {
    {I_VCVTUQQ2PD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+13876, 227},
    {I_VCVTUQQ2PD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+13884, 227},
    {I_VCVTUQQ2PD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64|ER,0,0,0}, nasm_bytecodes+13892, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTUQQ2PS[] = {
    {I_VCVTUQQ2PS, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+13900, 227},
    {I_VCVTUQQ2PS, 2, {XMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+13908, 227},
    {I_VCVTUQQ2PS, 2, {YMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64|ER,0,0,0}, nasm_bytecodes+13916, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTUSI2SD[] = {
    {I_VCVTUSI2SD, 3, {XMMREG,XMMREG,RM_GPR|BITS32,0,0}, {0,ER,0,0,0}, nasm_bytecodes+13924, 226},
    {I_VCVTUSI2SD, 3, {XMMREG,XMMREG,RM_GPR|BITS64,0,0}, {0,ER,0,0,0}, nasm_bytecodes+13932, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VCVTUSI2SS[] = {
    {I_VCVTUSI2SS, 3, {XMMREG,XMMREG,RM_GPR|BITS32,0,0}, {0,ER,0,0,0}, nasm_bytecodes+13940, 226},
    {I_VCVTUSI2SS, 3, {XMMREG,XMMREG,RM_GPR|BITS64,0,0}, {0,ER,0,0,0}, nasm_bytecodes+13948, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VDBPSADBW[] = {
    {I_VDBPSADBW, 4, {XMMREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+4209, 229},
    {I_VDBPSADBW, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+4218, 229},
    {I_VDBPSADBW, 4, {YMMREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+4227, 229},
    {I_VDBPSADBW, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+4236, 229},
    {I_VDBPSADBW, 4, {ZMMREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+4245, 230},
    {I_VDBPSADBW, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+4254, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VEXP2PD[] = {
    {I_VEXP2PD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64|SAE,0,0,0}, nasm_bytecodes+14084, 231},
    ITEMPLATE_END
};

static const struct itemplate instrux_VEXP2PS[] = {
    {I_VEXP2PS, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32|SAE,0,0,0}, nasm_bytecodes+14092, 231},
    ITEMPLATE_END
};

static const struct itemplate instrux_VEXPANDPD[] = {
    {I_VEXPANDPD, 2, {XMMREG,MEMORY|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+14100, 225},
    {I_VEXPANDPD, 2, {YMMREG,MEMORY|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+14108, 225},
    {I_VEXPANDPD, 2, {ZMMREG,MEMORY|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+14116, 226},
    {I_VEXPANDPD, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+14100, 225},
    {I_VEXPANDPD, 2, {YMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+14108, 225},
    {I_VEXPANDPD, 2, {ZMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+14116, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VEXPANDPS[] = {
    {I_VEXPANDPS, 2, {XMMREG,MEMORY|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+14124, 225},
    {I_VEXPANDPS, 2, {YMMREG,MEMORY|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+14132, 225},
    {I_VEXPANDPS, 2, {ZMMREG,MEMORY|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+14140, 226},
    {I_VEXPANDPS, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+14124, 225},
    {I_VEXPANDPS, 2, {YMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+14132, 225},
    {I_VEXPANDPS, 2, {ZMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+14140, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VEXTRACTF32X4[] = {
    {I_VEXTRACTF32X4, 3, {XMMREG,YMMREG,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+4263, 225},
    {I_VEXTRACTF32X4, 3, {XMMREG,ZMMREG,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+4272, 226},
    {I_VEXTRACTF32X4, 3, {MEMORY|BITS128,YMMREG,IMMEDIATE|BITS8,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4281, 225},
    {I_VEXTRACTF32X4, 3, {MEMORY|BITS128,ZMMREG,IMMEDIATE|BITS8,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4290, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VEXTRACTF32X8[] = {
    {I_VEXTRACTF32X8, 3, {YMMREG,ZMMREG,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+4299, 228},
    {I_VEXTRACTF32X8, 3, {MEMORY|BITS256,ZMMREG,IMMEDIATE|BITS8,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4308, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VEXTRACTF64X2[] = {
    {I_VEXTRACTF64X2, 3, {XMMREG,YMMREG,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+4317, 227},
    {I_VEXTRACTF64X2, 3, {XMMREG,ZMMREG,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+4326, 228},
    {I_VEXTRACTF64X2, 3, {MEMORY|BITS128,YMMREG,IMMEDIATE|BITS8,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4335, 227},
    {I_VEXTRACTF64X2, 3, {MEMORY|BITS128,ZMMREG,IMMEDIATE|BITS8,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4344, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VEXTRACTF64X4[] = {
    {I_VEXTRACTF64X4, 3, {YMMREG,ZMMREG,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+4353, 226},
    {I_VEXTRACTF64X4, 3, {MEMORY|BITS256,ZMMREG,IMMEDIATE|BITS8,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4362, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VEXTRACTI32X4[] = {
    {I_VEXTRACTI32X4, 3, {XMMREG,YMMREG,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+4371, 225},
    {I_VEXTRACTI32X4, 3, {XMMREG,ZMMREG,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+4380, 226},
    {I_VEXTRACTI32X4, 3, {MEMORY|BITS128,YMMREG,IMMEDIATE|BITS8,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4389, 225},
    {I_VEXTRACTI32X4, 3, {MEMORY|BITS128,ZMMREG,IMMEDIATE|BITS8,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4398, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VEXTRACTI32X8[] = {
    {I_VEXTRACTI32X8, 3, {YMMREG,ZMMREG,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+4407, 228},
    {I_VEXTRACTI32X8, 3, {MEMORY|BITS256,ZMMREG,IMMEDIATE|BITS8,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4416, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VEXTRACTI64X2[] = {
    {I_VEXTRACTI64X2, 3, {XMMREG,YMMREG,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+4425, 227},
    {I_VEXTRACTI64X2, 3, {XMMREG,ZMMREG,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+4434, 228},
    {I_VEXTRACTI64X2, 3, {MEMORY|BITS128,YMMREG,IMMEDIATE|BITS8,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4443, 227},
    {I_VEXTRACTI64X2, 3, {MEMORY|BITS128,ZMMREG,IMMEDIATE|BITS8,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4452, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VEXTRACTI64X4[] = {
    {I_VEXTRACTI64X4, 3, {YMMREG,ZMMREG,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+4461, 226},
    {I_VEXTRACTI64X4, 3, {MEMORY|BITS256,ZMMREG,IMMEDIATE|BITS8,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4470, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFIXUPIMMPD[] = {
    {I_VFIXUPIMMPD, 4, {XMMREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+4488, 225},
    {I_VFIXUPIMMPD, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+4497, 225},
    {I_VFIXUPIMMPD, 4, {YMMREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+4506, 225},
    {I_VFIXUPIMMPD, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+4515, 225},
    {I_VFIXUPIMMPD, 4, {ZMMREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64|SAE,0,0}, nasm_bytecodes+4524, 226},
    {I_VFIXUPIMMPD, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64|SAE,0,0,0}, nasm_bytecodes+4533, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFIXUPIMMPS[] = {
    {I_VFIXUPIMMPS, 4, {XMMREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+4542, 225},
    {I_VFIXUPIMMPS, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+4551, 225},
    {I_VFIXUPIMMPS, 4, {YMMREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+4560, 225},
    {I_VFIXUPIMMPS, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+4569, 225},
    {I_VFIXUPIMMPS, 4, {ZMMREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK|Z,0,B32|SAE,0,0}, nasm_bytecodes+4578, 226},
    {I_VFIXUPIMMPS, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32|SAE,0,0,0}, nasm_bytecodes+4587, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFIXUPIMMSD[] = {
    {I_VFIXUPIMMSD, 4, {XMMREG,XMMREG,RM_XMM|BITS64,IMMEDIATE|BITS8,0}, {MASK|Z,0,SAE,0,0}, nasm_bytecodes+4596, 226},
    {I_VFIXUPIMMSD, 3, {XMMREG,RM_XMM|BITS64,IMMEDIATE|BITS8,0,0}, {MASK|Z,SAE,0,0,0}, nasm_bytecodes+4605, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFIXUPIMMSS[] = {
    {I_VFIXUPIMMSS, 4, {XMMREG,XMMREG,RM_XMM|BITS32,IMMEDIATE|BITS8,0}, {MASK|Z,0,SAE,0,0}, nasm_bytecodes+4614, 226},
    {I_VFIXUPIMMSS, 3, {XMMREG,RM_XMM|BITS32,IMMEDIATE|BITS8,0,0}, {MASK|Z,SAE,0,0,0}, nasm_bytecodes+4623, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFPCLASSPD[] = {
    {I_VFPCLASSPD, 3, {KREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK,B64,0,0,0}, nasm_bytecodes+4632, 227},
    {I_VFPCLASSPD, 3, {KREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK,B64,0,0,0}, nasm_bytecodes+4641, 227},
    {I_VFPCLASSPD, 3, {KREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK,B64,0,0,0}, nasm_bytecodes+4650, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFPCLASSPS[] = {
    {I_VFPCLASSPS, 3, {KREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK,B32,0,0,0}, nasm_bytecodes+4659, 227},
    {I_VFPCLASSPS, 3, {KREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK,B32,0,0,0}, nasm_bytecodes+4668, 227},
    {I_VFPCLASSPS, 3, {KREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK,B32,0,0,0}, nasm_bytecodes+4677, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFPCLASSSD[] = {
    {I_VFPCLASSSD, 3, {KREG,RM_XMM|BITS64,IMMEDIATE|BITS8,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4686, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VFPCLASSSS[] = {
    {I_VFPCLASSSS, 3, {KREG,RM_XMM|BITS32,IMMEDIATE|BITS8,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4695, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VGATHERPF0DPD[] = {
    {I_VGATHERPF0DPD, 1, {YMEM|BITS64,0,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4758, 232},
    ITEMPLATE_END
};

static const struct itemplate instrux_VGATHERPF0DPS[] = {
    {I_VGATHERPF0DPS, 1, {ZMEM|BITS32,0,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4767, 232},
    ITEMPLATE_END
};

static const struct itemplate instrux_VGATHERPF0QPD[] = {
    {I_VGATHERPF0QPD, 1, {ZMEM|BITS64,0,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4776, 232},
    ITEMPLATE_END
};

static const struct itemplate instrux_VGATHERPF0QPS[] = {
    {I_VGATHERPF0QPS, 1, {ZMEM|BITS32,0,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4785, 232},
    ITEMPLATE_END
};

static const struct itemplate instrux_VGATHERPF1DPD[] = {
    {I_VGATHERPF1DPD, 1, {YMEM|BITS64,0,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4794, 232},
    ITEMPLATE_END
};

static const struct itemplate instrux_VGATHERPF1DPS[] = {
    {I_VGATHERPF1DPS, 1, {ZMEM|BITS32,0,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4803, 232},
    ITEMPLATE_END
};

static const struct itemplate instrux_VGATHERPF1QPD[] = {
    {I_VGATHERPF1QPD, 1, {ZMEM|BITS64,0,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4812, 232},
    ITEMPLATE_END
};

static const struct itemplate instrux_VGATHERPF1QPS[] = {
    {I_VGATHERPF1QPS, 1, {ZMEM|BITS32,0,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+4821, 232},
    ITEMPLATE_END
};

static const struct itemplate instrux_VGETEXPPD[] = {
    {I_VGETEXPPD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+15204, 225},
    {I_VGETEXPPD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+15212, 225},
    {I_VGETEXPPD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64|SAE,0,0,0}, nasm_bytecodes+15220, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VGETEXPPS[] = {
    {I_VGETEXPPS, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+15228, 225},
    {I_VGETEXPPS, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+15236, 225},
    {I_VGETEXPPS, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32|SAE,0,0,0}, nasm_bytecodes+15244, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VGETEXPSD[] = {
    {I_VGETEXPSD, 3, {XMMREG,XMMREG,RM_XMM|BITS64,0,0}, {MASK|Z,0,SAE,0,0}, nasm_bytecodes+15252, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VGETEXPSS[] = {
    {I_VGETEXPSS, 3, {XMMREG,XMMREG,RM_XMM|BITS32,0,0}, {MASK|Z,0,SAE,0,0}, nasm_bytecodes+15260, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VGETMANTPD[] = {
    {I_VGETMANTPD, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+4884, 225},
    {I_VGETMANTPD, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+4893, 225},
    {I_VGETMANTPD, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64|SAE,0,0,0}, nasm_bytecodes+4902, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VGETMANTPS[] = {
    {I_VGETMANTPS, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+4911, 225},
    {I_VGETMANTPS, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+4920, 225},
    {I_VGETMANTPS, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32|SAE,0,0,0}, nasm_bytecodes+4929, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VGETMANTSD[] = {
    {I_VGETMANTSD, 4, {XMMREG,XMMREG,RM_XMM|BITS64,IMMEDIATE|BITS8,0}, {MASK|Z,0,SAE,0,0}, nasm_bytecodes+4938, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VGETMANTSS[] = {
    {I_VGETMANTSS, 4, {XMMREG,XMMREG,RM_XMM|BITS32,IMMEDIATE|BITS8,0}, {MASK|Z,0,SAE,0,0}, nasm_bytecodes+4947, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VINSERTF32X4[] = {
    {I_VINSERTF32X4, 4, {YMMREG,YMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+4956, 225},
    {I_VINSERTF32X4, 3, {YMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+4965, 225},
    {I_VINSERTF32X4, 4, {ZMMREG,ZMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+4974, 226},
    {I_VINSERTF32X4, 3, {ZMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+4983, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VINSERTF32X8[] = {
    {I_VINSERTF32X8, 4, {ZMMREG,ZMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+4992, 228},
    {I_VINSERTF32X8, 3, {ZMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5001, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VINSERTF64X2[] = {
    {I_VINSERTF64X2, 4, {YMMREG,YMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5010, 227},
    {I_VINSERTF64X2, 3, {YMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5019, 227},
    {I_VINSERTF64X2, 4, {ZMMREG,ZMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5028, 228},
    {I_VINSERTF64X2, 3, {ZMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5037, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VINSERTF64X4[] = {
    {I_VINSERTF64X4, 4, {ZMMREG,ZMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5046, 226},
    {I_VINSERTF64X4, 3, {ZMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5055, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VINSERTI32X4[] = {
    {I_VINSERTI32X4, 4, {YMMREG,YMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5064, 225},
    {I_VINSERTI32X4, 3, {YMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5073, 225},
    {I_VINSERTI32X4, 4, {ZMMREG,ZMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5082, 226},
    {I_VINSERTI32X4, 3, {ZMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5091, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VINSERTI32X8[] = {
    {I_VINSERTI32X8, 4, {ZMMREG,ZMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5100, 228},
    {I_VINSERTI32X8, 3, {ZMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5109, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VINSERTI64X2[] = {
    {I_VINSERTI64X2, 4, {YMMREG,YMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5118, 227},
    {I_VINSERTI64X2, 3, {YMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5127, 227},
    {I_VINSERTI64X2, 4, {ZMMREG,ZMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5136, 228},
    {I_VINSERTI64X2, 3, {ZMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5145, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VINSERTI64X4[] = {
    {I_VINSERTI64X4, 4, {ZMMREG,ZMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5154, 226},
    {I_VINSERTI64X4, 3, {ZMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5163, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVDQA32[] = {
    {I_VMOVDQA32, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15708, 225},
    {I_VMOVDQA32, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15716, 225},
    {I_VMOVDQA32, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15724, 226},
    {I_VMOVDQA32, 2, {RM_XMM|BITS128,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15732, 225},
    {I_VMOVDQA32, 2, {RM_YMM|BITS256,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15740, 225},
    {I_VMOVDQA32, 2, {RM_ZMM|BITS512,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15748, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVDQA64[] = {
    {I_VMOVDQA64, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15756, 225},
    {I_VMOVDQA64, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15764, 225},
    {I_VMOVDQA64, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15772, 226},
    {I_VMOVDQA64, 2, {RM_XMM|BITS128,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15780, 225},
    {I_VMOVDQA64, 2, {RM_YMM|BITS256,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15788, 225},
    {I_VMOVDQA64, 2, {RM_ZMM|BITS512,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15796, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVDQU16[] = {
    {I_VMOVDQU16, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15804, 229},
    {I_VMOVDQU16, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15812, 229},
    {I_VMOVDQU16, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15820, 230},
    {I_VMOVDQU16, 2, {RM_XMM|BITS128,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15828, 229},
    {I_VMOVDQU16, 2, {RM_YMM|BITS256,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15836, 229},
    {I_VMOVDQU16, 2, {RM_ZMM|BITS512,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15844, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVDQU32[] = {
    {I_VMOVDQU32, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15852, 225},
    {I_VMOVDQU32, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15860, 225},
    {I_VMOVDQU32, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15868, 226},
    {I_VMOVDQU32, 2, {RM_XMM|BITS128,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15876, 225},
    {I_VMOVDQU32, 2, {RM_YMM|BITS256,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15884, 225},
    {I_VMOVDQU32, 2, {RM_ZMM|BITS512,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15892, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVDQU64[] = {
    {I_VMOVDQU64, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15900, 225},
    {I_VMOVDQU64, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15908, 225},
    {I_VMOVDQU64, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15916, 226},
    {I_VMOVDQU64, 2, {RM_XMM|BITS128,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15924, 225},
    {I_VMOVDQU64, 2, {RM_YMM|BITS256,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15932, 225},
    {I_VMOVDQU64, 2, {RM_ZMM|BITS512,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15940, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VMOVDQU8[] = {
    {I_VMOVDQU8, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15948, 229},
    {I_VMOVDQU8, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15956, 229},
    {I_VMOVDQU8, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15964, 230},
    {I_VMOVDQU8, 2, {RM_XMM|BITS128,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15972, 229},
    {I_VMOVDQU8, 2, {RM_YMM|BITS256,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15980, 229},
    {I_VMOVDQU8, 2, {RM_ZMM|BITS512,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+15988, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPABSQ[] = {
    {I_VPABSQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+16812, 225},
    {I_VPABSQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+16820, 225},
    {I_VPABSQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+16828, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPANDD[] = {
    {I_VPANDD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+17436, 225},
    {I_VPANDD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+17444, 225},
    {I_VPANDD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+17452, 225},
    {I_VPANDD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+17460, 225},
    {I_VPANDD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+17468, 226},
    {I_VPANDD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+17476, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPANDND[] = {
    {I_VPANDND, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+17484, 225},
    {I_VPANDND, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+17492, 225},
    {I_VPANDND, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+17500, 225},
    {I_VPANDND, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+17508, 225},
    {I_VPANDND, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+17516, 226},
    {I_VPANDND, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+17524, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPANDNQ[] = {
    {I_VPANDNQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+17532, 225},
    {I_VPANDNQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+17540, 225},
    {I_VPANDNQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+17548, 225},
    {I_VPANDNQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+17556, 225},
    {I_VPANDNQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+17564, 226},
    {I_VPANDNQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+17572, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPANDQ[] = {
    {I_VPANDQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+17580, 225},
    {I_VPANDQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+17588, 225},
    {I_VPANDQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+17596, 225},
    {I_VPANDQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+17604, 225},
    {I_VPANDQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+17612, 226},
    {I_VPANDQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+17620, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPBLENDMB[] = {
    {I_VPBLENDMB, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17724, 229},
    {I_VPBLENDMB, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17732, 229},
    {I_VPBLENDMB, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17740, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPBLENDMD[] = {
    {I_VPBLENDMD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+17748, 225},
    {I_VPBLENDMD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+17756, 225},
    {I_VPBLENDMD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+17764, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPBLENDMQ[] = {
    {I_VPBLENDMQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+17772, 225},
    {I_VPBLENDMQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+17780, 225},
    {I_VPBLENDMQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+17788, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPBLENDMW[] = {
    {I_VPBLENDMW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17796, 229},
    {I_VPBLENDMW, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17804, 229},
    {I_VPBLENDMW, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+17812, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPBROADCASTMB2Q[] = {
    {I_VPBROADCASTMB2Q, 2, {XMMREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+17940, 233},
    {I_VPBROADCASTMB2Q, 2, {YMMREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+17948, 233},
    {I_VPBROADCASTMB2Q, 2, {ZMMREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+17956, 234},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPBROADCASTMW2D[] = {
    {I_VPBROADCASTMW2D, 2, {XMMREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+17964, 233},
    {I_VPBROADCASTMW2D, 2, {YMMREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+17972, 233},
    {I_VPBROADCASTMW2D, 2, {ZMMREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+17980, 234},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCMPB[] = {
    {I_VPCMPB, 4, {KREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK,0,0,0,0}, nasm_bytecodes+5244, 229},
    {I_VPCMPB, 4, {KREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK,0,0,0,0}, nasm_bytecodes+5253, 229},
    {I_VPCMPB, 4, {KREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK,0,0,0,0}, nasm_bytecodes+5262, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCMPD[] = {
    {I_VPCMPD, 4, {KREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK,0,B32,0,0}, nasm_bytecodes+5271, 225},
    {I_VPCMPD, 4, {KREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK,0,B32,0,0}, nasm_bytecodes+5280, 225},
    {I_VPCMPD, 4, {KREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK,0,B32,0,0}, nasm_bytecodes+5289, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCMPQ[] = {
    {I_VPCMPQ, 4, {KREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK,0,B64,0,0}, nasm_bytecodes+5298, 225},
    {I_VPCMPQ, 4, {KREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK,0,B64,0,0}, nasm_bytecodes+5307, 225},
    {I_VPCMPQ, 4, {KREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK,0,B64,0,0}, nasm_bytecodes+5316, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCMPUB[] = {
    {I_VPCMPUB, 4, {KREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK,0,0,0,0}, nasm_bytecodes+5325, 229},
    {I_VPCMPUB, 4, {KREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK,0,0,0,0}, nasm_bytecodes+5334, 229},
    {I_VPCMPUB, 4, {KREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK,0,0,0,0}, nasm_bytecodes+5343, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCMPUD[] = {
    {I_VPCMPUD, 4, {KREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK,0,B32,0,0}, nasm_bytecodes+5352, 225},
    {I_VPCMPUD, 4, {KREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK,0,B32,0,0}, nasm_bytecodes+5361, 225},
    {I_VPCMPUD, 4, {KREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK,0,B32,0,0}, nasm_bytecodes+5370, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCMPUQ[] = {
    {I_VPCMPUQ, 4, {KREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK,0,B64,0,0}, nasm_bytecodes+5379, 225},
    {I_VPCMPUQ, 4, {KREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK,0,B64,0,0}, nasm_bytecodes+5388, 225},
    {I_VPCMPUQ, 4, {KREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK,0,B64,0,0}, nasm_bytecodes+5397, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCMPUW[] = {
    {I_VPCMPUW, 4, {KREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK,0,0,0,0}, nasm_bytecodes+5406, 229},
    {I_VPCMPUW, 4, {KREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK,0,0,0,0}, nasm_bytecodes+5415, 229},
    {I_VPCMPUW, 4, {KREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK,0,0,0,0}, nasm_bytecodes+5424, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCMPW[] = {
    {I_VPCMPW, 4, {KREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK,0,0,0,0}, nasm_bytecodes+5433, 229},
    {I_VPCMPW, 4, {KREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK,0,0,0,0}, nasm_bytecodes+5442, 229},
    {I_VPCMPW, 4, {KREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK,0,0,0,0}, nasm_bytecodes+5451, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCOMPRESSD[] = {
    {I_VPCOMPRESSD, 2, {MEMORY|BITS128,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+18300, 225},
    {I_VPCOMPRESSD, 2, {MEMORY|BITS256,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+18308, 225},
    {I_VPCOMPRESSD, 2, {MEMORY|BITS512,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+18316, 226},
    {I_VPCOMPRESSD, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18324, 225},
    {I_VPCOMPRESSD, 2, {YMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18332, 225},
    {I_VPCOMPRESSD, 2, {ZMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18340, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCOMPRESSQ[] = {
    {I_VPCOMPRESSQ, 2, {MEMORY|BITS128,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+18348, 225},
    {I_VPCOMPRESSQ, 2, {MEMORY|BITS256,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+18356, 225},
    {I_VPCOMPRESSQ, 2, {MEMORY|BITS512,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+18364, 226},
    {I_VPCOMPRESSQ, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18372, 225},
    {I_VPCOMPRESSQ, 2, {YMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18380, 225},
    {I_VPCOMPRESSQ, 2, {ZMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18388, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCONFLICTD[] = {
    {I_VPCONFLICTD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+18396, 233},
    {I_VPCONFLICTD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+18404, 233},
    {I_VPCONFLICTD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+18412, 234},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCONFLICTQ[] = {
    {I_VPCONFLICTQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+18420, 233},
    {I_VPCONFLICTQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+18428, 233},
    {I_VPCONFLICTQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+18436, 234},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPERMB[] = {
    {I_VPERMB, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18444, 235},
    {I_VPERMB, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18452, 235},
    {I_VPERMB, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18460, 235},
    {I_VPERMB, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18468, 235},
    {I_VPERMB, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18476, 236},
    {I_VPERMB, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18484, 236},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPERMI2B[] = {
    {I_VPERMI2B, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18524, 235},
    {I_VPERMI2B, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18532, 235},
    {I_VPERMI2B, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18540, 236},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPERMI2D[] = {
    {I_VPERMI2D, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+18548, 225},
    {I_VPERMI2D, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+18556, 225},
    {I_VPERMI2D, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+18564, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPERMI2PD[] = {
    {I_VPERMI2PD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+18572, 225},
    {I_VPERMI2PD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+18580, 225},
    {I_VPERMI2PD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+18588, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPERMI2PS[] = {
    {I_VPERMI2PS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+18596, 225},
    {I_VPERMI2PS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+18604, 225},
    {I_VPERMI2PS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+18612, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPERMI2Q[] = {
    {I_VPERMI2Q, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+18620, 225},
    {I_VPERMI2Q, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+18628, 225},
    {I_VPERMI2Q, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+18636, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPERMI2W[] = {
    {I_VPERMI2W, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18644, 229},
    {I_VPERMI2W, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18652, 229},
    {I_VPERMI2W, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18660, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPERMT2B[] = {
    {I_VPERMT2B, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18860, 235},
    {I_VPERMT2B, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18868, 235},
    {I_VPERMT2B, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18876, 236},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPERMT2D[] = {
    {I_VPERMT2D, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+18884, 225},
    {I_VPERMT2D, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+18892, 225},
    {I_VPERMT2D, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+18900, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPERMT2PD[] = {
    {I_VPERMT2PD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+18908, 225},
    {I_VPERMT2PD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+18916, 225},
    {I_VPERMT2PD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+18924, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPERMT2PS[] = {
    {I_VPERMT2PS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+18932, 225},
    {I_VPERMT2PS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+18940, 225},
    {I_VPERMT2PS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+18948, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPERMT2Q[] = {
    {I_VPERMT2Q, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+18956, 225},
    {I_VPERMT2Q, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+18964, 225},
    {I_VPERMT2Q, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+18972, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPERMT2W[] = {
    {I_VPERMT2W, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18980, 229},
    {I_VPERMT2W, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18988, 229},
    {I_VPERMT2W, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+18996, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPERMW[] = {
    {I_VPERMW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19004, 229},
    {I_VPERMW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19012, 229},
    {I_VPERMW, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19020, 229},
    {I_VPERMW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19028, 229},
    {I_VPERMW, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19036, 230},
    {I_VPERMW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19044, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPEXPANDD[] = {
    {I_VPEXPANDD, 2, {XMMREG,MEMORY|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19052, 225},
    {I_VPEXPANDD, 2, {YMMREG,MEMORY|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19060, 225},
    {I_VPEXPANDD, 2, {ZMMREG,MEMORY|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19068, 226},
    {I_VPEXPANDD, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19052, 225},
    {I_VPEXPANDD, 2, {YMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19060, 225},
    {I_VPEXPANDD, 2, {ZMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19068, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPEXPANDQ[] = {
    {I_VPEXPANDQ, 2, {XMMREG,MEMORY|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19076, 225},
    {I_VPEXPANDQ, 2, {YMMREG,MEMORY|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19084, 225},
    {I_VPEXPANDQ, 2, {ZMMREG,MEMORY|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19092, 226},
    {I_VPEXPANDQ, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19076, 225},
    {I_VPEXPANDQ, 2, {YMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19084, 225},
    {I_VPEXPANDQ, 2, {ZMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+19092, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPLZCNTD[] = {
    {I_VPLZCNTD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+19100, 233},
    {I_VPLZCNTD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+19108, 233},
    {I_VPLZCNTD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+19116, 234},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPLZCNTQ[] = {
    {I_VPLZCNTQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+19124, 233},
    {I_VPLZCNTQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+19132, 233},
    {I_VPLZCNTQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+19140, 234},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMADD52HUQ[] = {
    {I_VPMADD52HUQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+19148, 237},
    {I_VPMADD52HUQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+19156, 237},
    {I_VPMADD52HUQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+19164, 238},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMADD52LUQ[] = {
    {I_VPMADD52LUQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+19172, 237},
    {I_VPMADD52LUQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+19180, 237},
    {I_VPMADD52LUQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+19188, 238},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMAXSQ[] = {
    {I_VPMAXSQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+19388, 225},
    {I_VPMAXSQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+19396, 225},
    {I_VPMAXSQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+19404, 225},
    {I_VPMAXSQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+19412, 225},
    {I_VPMAXSQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+19420, 226},
    {I_VPMAXSQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+19428, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMAXUQ[] = {
    {I_VPMAXUQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+19580, 225},
    {I_VPMAXUQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+19588, 225},
    {I_VPMAXUQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+19596, 225},
    {I_VPMAXUQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+19604, 225},
    {I_VPMAXUQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+19612, 226},
    {I_VPMAXUQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+19620, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMINSQ[] = {
    {I_VPMINSQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+19772, 225},
    {I_VPMINSQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+19780, 225},
    {I_VPMINSQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+19788, 225},
    {I_VPMINSQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+19796, 225},
    {I_VPMINSQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+19804, 226},
    {I_VPMINSQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+19812, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMINUQ[] = {
    {I_VPMINUQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+19964, 225},
    {I_VPMINUQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+19972, 225},
    {I_VPMINUQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+19980, 225},
    {I_VPMINUQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+19988, 225},
    {I_VPMINUQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+19996, 226},
    {I_VPMINUQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+20004, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVB2M[] = {
    {I_VPMOVB2M, 2, {KREG,XMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+20060, 229},
    {I_VPMOVB2M, 2, {KREG,YMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+20068, 229},
    {I_VPMOVB2M, 2, {KREG,ZMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+20076, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVD2M[] = {
    {I_VPMOVD2M, 2, {KREG,XMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+20084, 227},
    {I_VPMOVD2M, 2, {KREG,YMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+20092, 227},
    {I_VPMOVD2M, 2, {KREG,ZMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+20100, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVDB[] = {
    {I_VPMOVDB, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20108, 225},
    {I_VPMOVDB, 2, {XMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20116, 225},
    {I_VPMOVDB, 2, {XMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20124, 226},
    {I_VPMOVDB, 2, {MEMORY|BITS32,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20132, 225},
    {I_VPMOVDB, 2, {MEMORY|BITS64,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20140, 225},
    {I_VPMOVDB, 2, {MEMORY|BITS128,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20148, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVDW[] = {
    {I_VPMOVDW, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20156, 225},
    {I_VPMOVDW, 2, {XMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20164, 225},
    {I_VPMOVDW, 2, {YMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20172, 226},
    {I_VPMOVDW, 2, {MEMORY|BITS64,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20180, 225},
    {I_VPMOVDW, 2, {MEMORY|BITS128,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20188, 225},
    {I_VPMOVDW, 2, {MEMORY|BITS256,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20196, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVM2B[] = {
    {I_VPMOVM2B, 2, {XMMREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+20204, 229},
    {I_VPMOVM2B, 2, {YMMREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+20212, 229},
    {I_VPMOVM2B, 2, {ZMMREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+20220, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVM2D[] = {
    {I_VPMOVM2D, 2, {XMMREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+20228, 227},
    {I_VPMOVM2D, 2, {YMMREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+20236, 227},
    {I_VPMOVM2D, 2, {ZMMREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+20244, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVM2Q[] = {
    {I_VPMOVM2Q, 2, {XMMREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+20252, 227},
    {I_VPMOVM2Q, 2, {YMMREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+20260, 227},
    {I_VPMOVM2Q, 2, {ZMMREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+20268, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVM2W[] = {
    {I_VPMOVM2W, 2, {XMMREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+20276, 229},
    {I_VPMOVM2W, 2, {YMMREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+20284, 229},
    {I_VPMOVM2W, 2, {ZMMREG,KREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+20292, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVQ2M[] = {
    {I_VPMOVQ2M, 2, {KREG,XMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+20300, 227},
    {I_VPMOVQ2M, 2, {KREG,YMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+20308, 227},
    {I_VPMOVQ2M, 2, {KREG,ZMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+20316, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVQB[] = {
    {I_VPMOVQB, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20324, 225},
    {I_VPMOVQB, 2, {XMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20332, 225},
    {I_VPMOVQB, 2, {XMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20340, 226},
    {I_VPMOVQB, 2, {MEMORY|BITS16,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20348, 225},
    {I_VPMOVQB, 2, {MEMORY|BITS32,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20356, 225},
    {I_VPMOVQB, 2, {MEMORY|BITS64,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20364, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVQD[] = {
    {I_VPMOVQD, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20372, 225},
    {I_VPMOVQD, 2, {XMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20380, 225},
    {I_VPMOVQD, 2, {YMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20388, 226},
    {I_VPMOVQD, 2, {MEMORY|BITS64,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20396, 225},
    {I_VPMOVQD, 2, {MEMORY|BITS128,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20404, 225},
    {I_VPMOVQD, 2, {MEMORY|BITS256,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20412, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVQW[] = {
    {I_VPMOVQW, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20420, 225},
    {I_VPMOVQW, 2, {XMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20428, 225},
    {I_VPMOVQW, 2, {XMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20436, 226},
    {I_VPMOVQW, 2, {MEMORY|BITS32,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20444, 225},
    {I_VPMOVQW, 2, {MEMORY|BITS64,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20452, 225},
    {I_VPMOVQW, 2, {MEMORY|BITS128,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20460, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVSDB[] = {
    {I_VPMOVSDB, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20468, 225},
    {I_VPMOVSDB, 2, {XMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20476, 225},
    {I_VPMOVSDB, 2, {XMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20484, 226},
    {I_VPMOVSDB, 2, {MEMORY|BITS32,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20492, 225},
    {I_VPMOVSDB, 2, {MEMORY|BITS64,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20500, 225},
    {I_VPMOVSDB, 2, {MEMORY|BITS128,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20508, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVSDW[] = {
    {I_VPMOVSDW, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20516, 225},
    {I_VPMOVSDW, 2, {XMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20524, 225},
    {I_VPMOVSDW, 2, {YMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20532, 226},
    {I_VPMOVSDW, 2, {MEMORY|BITS64,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20540, 225},
    {I_VPMOVSDW, 2, {MEMORY|BITS128,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20548, 225},
    {I_VPMOVSDW, 2, {MEMORY|BITS256,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20556, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVSQB[] = {
    {I_VPMOVSQB, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20564, 225},
    {I_VPMOVSQB, 2, {XMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20572, 225},
    {I_VPMOVSQB, 2, {XMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20580, 226},
    {I_VPMOVSQB, 2, {MEMORY|BITS16,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20588, 225},
    {I_VPMOVSQB, 2, {MEMORY|BITS32,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20596, 225},
    {I_VPMOVSQB, 2, {MEMORY|BITS64,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20604, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVSQD[] = {
    {I_VPMOVSQD, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20612, 225},
    {I_VPMOVSQD, 2, {XMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20620, 225},
    {I_VPMOVSQD, 2, {YMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20628, 226},
    {I_VPMOVSQD, 2, {MEMORY|BITS64,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20636, 225},
    {I_VPMOVSQD, 2, {MEMORY|BITS128,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20644, 225},
    {I_VPMOVSQD, 2, {MEMORY|BITS256,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20652, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVSQW[] = {
    {I_VPMOVSQW, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20660, 225},
    {I_VPMOVSQW, 2, {XMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20668, 225},
    {I_VPMOVSQW, 2, {XMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20676, 226},
    {I_VPMOVSQW, 2, {MEMORY|BITS32,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20684, 225},
    {I_VPMOVSQW, 2, {MEMORY|BITS64,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20692, 225},
    {I_VPMOVSQW, 2, {MEMORY|BITS128,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20700, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVSWB[] = {
    {I_VPMOVSWB, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20708, 229},
    {I_VPMOVSWB, 2, {XMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20716, 229},
    {I_VPMOVSWB, 2, {YMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20724, 230},
    {I_VPMOVSWB, 2, {MEMORY|BITS64,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20732, 229},
    {I_VPMOVSWB, 2, {MEMORY|BITS128,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20740, 229},
    {I_VPMOVSWB, 2, {MEMORY|BITS256,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20748, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVUSDB[] = {
    {I_VPMOVUSDB, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20900, 225},
    {I_VPMOVUSDB, 2, {XMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20908, 225},
    {I_VPMOVUSDB, 2, {XMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20916, 226},
    {I_VPMOVUSDB, 2, {MEMORY|BITS32,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20924, 225},
    {I_VPMOVUSDB, 2, {MEMORY|BITS64,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20932, 225},
    {I_VPMOVUSDB, 2, {MEMORY|BITS128,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20940, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVUSDW[] = {
    {I_VPMOVUSDW, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20948, 225},
    {I_VPMOVUSDW, 2, {XMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20956, 225},
    {I_VPMOVUSDW, 2, {YMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20964, 226},
    {I_VPMOVUSDW, 2, {MEMORY|BITS64,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20972, 225},
    {I_VPMOVUSDW, 2, {MEMORY|BITS128,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20980, 225},
    {I_VPMOVUSDW, 2, {MEMORY|BITS256,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+20988, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVUSQB[] = {
    {I_VPMOVUSQB, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+20996, 225},
    {I_VPMOVUSQB, 2, {XMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21004, 225},
    {I_VPMOVUSQB, 2, {XMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21012, 226},
    {I_VPMOVUSQB, 2, {MEMORY|BITS16,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+21020, 225},
    {I_VPMOVUSQB, 2, {MEMORY|BITS32,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+21028, 225},
    {I_VPMOVUSQB, 2, {MEMORY|BITS64,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+21036, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVUSQD[] = {
    {I_VPMOVUSQD, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21044, 225},
    {I_VPMOVUSQD, 2, {XMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21052, 225},
    {I_VPMOVUSQD, 2, {YMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21060, 226},
    {I_VPMOVUSQD, 2, {MEMORY|BITS64,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+21068, 225},
    {I_VPMOVUSQD, 2, {MEMORY|BITS128,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+21076, 225},
    {I_VPMOVUSQD, 2, {MEMORY|BITS256,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+21084, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVUSQW[] = {
    {I_VPMOVUSQW, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21092, 225},
    {I_VPMOVUSQW, 2, {XMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21100, 225},
    {I_VPMOVUSQW, 2, {XMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21108, 226},
    {I_VPMOVUSQW, 2, {MEMORY|BITS32,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+21116, 225},
    {I_VPMOVUSQW, 2, {MEMORY|BITS64,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+21124, 225},
    {I_VPMOVUSQW, 2, {MEMORY|BITS128,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+21132, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVUSWB[] = {
    {I_VPMOVUSWB, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21140, 229},
    {I_VPMOVUSWB, 2, {XMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21148, 229},
    {I_VPMOVUSWB, 2, {YMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21156, 230},
    {I_VPMOVUSWB, 2, {MEMORY|BITS64,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+21164, 229},
    {I_VPMOVUSWB, 2, {MEMORY|BITS128,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+21172, 229},
    {I_VPMOVUSWB, 2, {MEMORY|BITS256,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+21180, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVW2M[] = {
    {I_VPMOVW2M, 2, {KREG,XMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+21188, 229},
    {I_VPMOVW2M, 2, {KREG,YMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+21196, 229},
    {I_VPMOVW2M, 2, {KREG,ZMMREG,0,0,0}, NO_DECORATOR, nasm_bytecodes+21204, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMOVWB[] = {
    {I_VPMOVWB, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21212, 229},
    {I_VPMOVWB, 2, {XMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21220, 229},
    {I_VPMOVWB, 2, {YMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+21228, 230},
    {I_VPMOVWB, 2, {MEMORY|BITS64,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+21236, 229},
    {I_VPMOVWB, 2, {MEMORY|BITS128,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+21244, 229},
    {I_VPMOVWB, 2, {MEMORY|BITS256,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+21252, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMULLQ[] = {
    {I_VPMULLQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+21644, 227},
    {I_VPMULLQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+21652, 227},
    {I_VPMULLQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+21660, 227},
    {I_VPMULLQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+21668, 227},
    {I_VPMULLQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+21676, 228},
    {I_VPMULLQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+21684, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPMULTISHIFTQB[] = {
    {I_VPMULTISHIFTQB, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+21740, 235},
    {I_VPMULTISHIFTQB, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+21748, 235},
    {I_VPMULTISHIFTQB, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+21756, 235},
    {I_VPMULTISHIFTQB, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+21764, 235},
    {I_VPMULTISHIFTQB, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+21772, 236},
    {I_VPMULTISHIFTQB, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+21780, 236},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPORD[] = {
    {I_VPORD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+21836, 225},
    {I_VPORD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+21844, 225},
    {I_VPORD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+21852, 225},
    {I_VPORD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+21860, 225},
    {I_VPORD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+21868, 226},
    {I_VPORD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+21876, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPORQ[] = {
    {I_VPORQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+21884, 225},
    {I_VPORQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+21892, 225},
    {I_VPORQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+21900, 225},
    {I_VPORQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+21908, 225},
    {I_VPORQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+21916, 226},
    {I_VPORQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+21924, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPROLD[] = {
    {I_VPROLD, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+5775, 225},
    {I_VPROLD, 2, {XMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5784, 225},
    {I_VPROLD, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+5793, 225},
    {I_VPROLD, 2, {YMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5802, 225},
    {I_VPROLD, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+5811, 226},
    {I_VPROLD, 2, {ZMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5820, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPROLQ[] = {
    {I_VPROLQ, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+5829, 225},
    {I_VPROLQ, 2, {XMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5838, 225},
    {I_VPROLQ, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+5847, 225},
    {I_VPROLQ, 2, {YMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5856, 225},
    {I_VPROLQ, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+5865, 226},
    {I_VPROLQ, 2, {ZMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5874, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPROLVD[] = {
    {I_VPROLVD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+21932, 225},
    {I_VPROLVD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+21940, 225},
    {I_VPROLVD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+21948, 225},
    {I_VPROLVD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+21956, 225},
    {I_VPROLVD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+21964, 226},
    {I_VPROLVD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+21972, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPROLVQ[] = {
    {I_VPROLVQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+21980, 225},
    {I_VPROLVQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+21988, 225},
    {I_VPROLVQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+21996, 225},
    {I_VPROLVQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+22004, 225},
    {I_VPROLVQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+22012, 226},
    {I_VPROLVQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+22020, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPRORD[] = {
    {I_VPRORD, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+5883, 225},
    {I_VPRORD, 2, {XMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5892, 225},
    {I_VPRORD, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+5901, 225},
    {I_VPRORD, 2, {YMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5910, 225},
    {I_VPRORD, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+5919, 226},
    {I_VPRORD, 2, {ZMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5928, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPRORQ[] = {
    {I_VPRORQ, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+5937, 225},
    {I_VPRORQ, 2, {XMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5946, 225},
    {I_VPRORQ, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+5955, 225},
    {I_VPRORQ, 2, {YMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5964, 225},
    {I_VPRORQ, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+5973, 226},
    {I_VPRORQ, 2, {ZMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+5982, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPRORVD[] = {
    {I_VPRORVD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+22028, 225},
    {I_VPRORVD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+22036, 225},
    {I_VPRORVD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+22044, 225},
    {I_VPRORVD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+22052, 225},
    {I_VPRORVD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+22060, 226},
    {I_VPRORVD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+22068, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPRORVQ[] = {
    {I_VPRORVQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+22076, 225},
    {I_VPRORVQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+22084, 225},
    {I_VPRORVQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+22092, 225},
    {I_VPRORVQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+22100, 225},
    {I_VPRORVQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+22108, 226},
    {I_VPRORVQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+22116, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSCATTERDD[] = {
    {I_VPSCATTERDD, 2, {XMEM|BITS32,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+5991, 225},
    {I_VPSCATTERDD, 2, {YMEM|BITS32,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+6000, 225},
    {I_VPSCATTERDD, 2, {ZMEM|BITS32,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+6009, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSCATTERDQ[] = {
    {I_VPSCATTERDQ, 2, {XMEM|BITS64,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+6018, 225},
    {I_VPSCATTERDQ, 2, {XMEM|BITS64,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+6027, 225},
    {I_VPSCATTERDQ, 2, {YMEM|BITS64,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+6036, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSCATTERQD[] = {
    {I_VPSCATTERQD, 2, {XMEM|BITS32,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+6045, 225},
    {I_VPSCATTERQD, 2, {YMEM|BITS32,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+6054, 225},
    {I_VPSCATTERQD, 2, {ZMEM|BITS32,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+6063, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSCATTERQQ[] = {
    {I_VPSCATTERQQ, 2, {XMEM|BITS64,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+6072, 225},
    {I_VPSCATTERQQ, 2, {YMEM|BITS64,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+6081, 225},
    {I_VPSCATTERQQ, 2, {ZMEM|BITS64,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+6090, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSLLVW[] = {
    {I_VPSLLVW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22412, 229},
    {I_VPSLLVW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22420, 229},
    {I_VPSLLVW, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22428, 229},
    {I_VPSLLVW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22436, 229},
    {I_VPSLLVW, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22444, 230},
    {I_VPSLLVW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22452, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSRAQ[] = {
    {I_VPSRAQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22556, 225},
    {I_VPSRAQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22564, 225},
    {I_VPSRAQ, 3, {YMMREG,YMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22572, 225},
    {I_VPSRAQ, 2, {YMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22580, 225},
    {I_VPSRAQ, 3, {ZMMREG,ZMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22588, 226},
    {I_VPSRAQ, 2, {ZMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22596, 226},
    {I_VPSRAQ, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+6450, 225},
    {I_VPSRAQ, 2, {XMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6459, 225},
    {I_VPSRAQ, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+6468, 225},
    {I_VPSRAQ, 2, {YMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6477, 225},
    {I_VPSRAQ, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+6486, 226},
    {I_VPSRAQ, 2, {ZMMREG,IMMEDIATE|BITS8,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+6495, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSRAVQ[] = {
    {I_VPSRAVQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+22652, 225},
    {I_VPSRAVQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+22660, 225},
    {I_VPSRAVQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+22668, 225},
    {I_VPSRAVQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+22676, 225},
    {I_VPSRAVQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+22684, 226},
    {I_VPSRAVQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+22692, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSRAVW[] = {
    {I_VPSRAVW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22700, 229},
    {I_VPSRAVW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22708, 229},
    {I_VPSRAVW, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22716, 229},
    {I_VPSRAVW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22724, 229},
    {I_VPSRAVW, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22732, 230},
    {I_VPSRAVW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22740, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSRLVW[] = {
    {I_VPSRLVW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22988, 229},
    {I_VPSRLVW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+22996, 229},
    {I_VPSRLVW, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23004, 229},
    {I_VPSRLVW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23012, 229},
    {I_VPSRLVW, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23020, 230},
    {I_VPSRLVW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+23028, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPTERNLOGD[] = {
    {I_VPTERNLOGD, 4, {XMMREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+6774, 225},
    {I_VPTERNLOGD, 4, {YMMREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+6783, 225},
    {I_VPTERNLOGD, 4, {ZMMREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+6792, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPTERNLOGQ[] = {
    {I_VPTERNLOGQ, 4, {XMMREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+6801, 225},
    {I_VPTERNLOGQ, 4, {YMMREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+6810, 225},
    {I_VPTERNLOGQ, 4, {ZMMREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+6819, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPTESTMB[] = {
    {I_VPTESTMB, 3, {KREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+23468, 229},
    {I_VPTESTMB, 3, {KREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+23476, 229},
    {I_VPTESTMB, 3, {KREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+23484, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPTESTMD[] = {
    {I_VPTESTMD, 3, {KREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK,0,B32,0,0}, nasm_bytecodes+23492, 225},
    {I_VPTESTMD, 3, {KREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK,0,B32,0,0}, nasm_bytecodes+23500, 225},
    {I_VPTESTMD, 3, {KREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK,0,B32,0,0}, nasm_bytecodes+23508, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPTESTMQ[] = {
    {I_VPTESTMQ, 3, {KREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK,0,B64,0,0}, nasm_bytecodes+23516, 225},
    {I_VPTESTMQ, 3, {KREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK,0,B64,0,0}, nasm_bytecodes+23524, 225},
    {I_VPTESTMQ, 3, {KREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK,0,B64,0,0}, nasm_bytecodes+23532, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPTESTMW[] = {
    {I_VPTESTMW, 3, {KREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+23540, 229},
    {I_VPTESTMW, 3, {KREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+23548, 229},
    {I_VPTESTMW, 3, {KREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+23556, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPTESTNMB[] = {
    {I_VPTESTNMB, 3, {KREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+23564, 229},
    {I_VPTESTNMB, 3, {KREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+23572, 229},
    {I_VPTESTNMB, 3, {KREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+23580, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPTESTNMD[] = {
    {I_VPTESTNMD, 3, {KREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK,0,B32,0,0}, nasm_bytecodes+23588, 225},
    {I_VPTESTNMD, 3, {KREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK,0,B32,0,0}, nasm_bytecodes+23596, 225},
    {I_VPTESTNMD, 3, {KREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK,0,B32,0,0}, nasm_bytecodes+23604, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPTESTNMQ[] = {
    {I_VPTESTNMQ, 3, {KREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK,0,B64,0,0}, nasm_bytecodes+23612, 225},
    {I_VPTESTNMQ, 3, {KREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK,0,B64,0,0}, nasm_bytecodes+23620, 225},
    {I_VPTESTNMQ, 3, {KREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK,0,B64,0,0}, nasm_bytecodes+23628, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPTESTNMW[] = {
    {I_VPTESTNMW, 3, {KREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+23636, 229},
    {I_VPTESTNMW, 3, {KREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+23644, 229},
    {I_VPTESTNMW, 3, {KREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+23652, 230},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPXORD[] = {
    {I_VPXORD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+24044, 225},
    {I_VPXORD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+24052, 225},
    {I_VPXORD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+24060, 225},
    {I_VPXORD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+24068, 225},
    {I_VPXORD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+24076, 226},
    {I_VPXORD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+24084, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPXORQ[] = {
    {I_VPXORQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+24092, 225},
    {I_VPXORQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+24100, 225},
    {I_VPXORQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+24108, 225},
    {I_VPXORQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+24116, 225},
    {I_VPXORQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+24124, 226},
    {I_VPXORQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+24132, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VRANGEPD[] = {
    {I_VRANGEPD, 4, {XMMREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+6828, 227},
    {I_VRANGEPD, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+6837, 227},
    {I_VRANGEPD, 4, {YMMREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+6846, 227},
    {I_VRANGEPD, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+6855, 227},
    {I_VRANGEPD, 4, {ZMMREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64|SAE,0,0}, nasm_bytecodes+6864, 228},
    {I_VRANGEPD, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64|SAE,0,0,0}, nasm_bytecodes+6873, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VRANGEPS[] = {
    {I_VRANGEPS, 4, {XMMREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+6882, 227},
    {I_VRANGEPS, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+6891, 227},
    {I_VRANGEPS, 4, {YMMREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+6900, 227},
    {I_VRANGEPS, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+6909, 227},
    {I_VRANGEPS, 4, {ZMMREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK|Z,0,B32|SAE,0,0}, nasm_bytecodes+6918, 228},
    {I_VRANGEPS, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32|SAE,0,0,0}, nasm_bytecodes+6927, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VRANGESD[] = {
    {I_VRANGESD, 4, {XMMREG,XMMREG,RM_XMM|BITS64,IMMEDIATE|BITS8,0}, {MASK|Z,0,SAE,0,0}, nasm_bytecodes+6936, 228},
    {I_VRANGESD, 3, {XMMREG,RM_XMM|BITS64,IMMEDIATE|BITS8,0,0}, {MASK|Z,SAE,0,0,0}, nasm_bytecodes+6945, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VRANGESS[] = {
    {I_VRANGESS, 4, {XMMREG,XMMREG,RM_XMM|BITS32,IMMEDIATE|BITS8,0}, {MASK|Z,0,SAE,0,0}, nasm_bytecodes+6954, 228},
    {I_VRANGESS, 3, {XMMREG,RM_XMM|BITS32,IMMEDIATE|BITS8,0,0}, {MASK|Z,SAE,0,0,0}, nasm_bytecodes+6963, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VRCP14PD[] = {
    {I_VRCP14PD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+24140, 225},
    {I_VRCP14PD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+24148, 225},
    {I_VRCP14PD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+24156, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VRCP14PS[] = {
    {I_VRCP14PS, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+24164, 225},
    {I_VRCP14PS, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+24172, 225},
    {I_VRCP14PS, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+24180, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VRCP14SD[] = {
    {I_VRCP14SD, 3, {XMMREG,XMMREG,RM_XMM|BITS64,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+24188, 226},
    {I_VRCP14SD, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+24196, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VRCP14SS[] = {
    {I_VRCP14SS, 3, {XMMREG,XMMREG,RM_XMM|BITS32,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+24204, 226},
    {I_VRCP14SS, 2, {XMMREG,RM_XMM|BITS32,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+24212, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VRCP28PD[] = {
    {I_VRCP28PD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64|SAE,0,0,0}, nasm_bytecodes+24220, 231},
    ITEMPLATE_END
};

static const struct itemplate instrux_VRCP28PS[] = {
    {I_VRCP28PS, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32|SAE,0,0,0}, nasm_bytecodes+24228, 231},
    ITEMPLATE_END
};

static const struct itemplate instrux_VRCP28SD[] = {
    {I_VRCP28SD, 3, {XMMREG,XMMREG,RM_XMM|BITS64,0,0}, {MASK|Z,0,SAE,0,0}, nasm_bytecodes+24236, 231},
    {I_VRCP28SD, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,SAE,0,0,0}, nasm_bytecodes+24244, 231},
    ITEMPLATE_END
};

static const struct itemplate instrux_VRCP28SS[] = {
    {I_VRCP28SS, 3, {XMMREG,XMMREG,RM_XMM|BITS32,0,0}, {MASK|Z,0,SAE,0,0}, nasm_bytecodes+24252, 231},
    {I_VRCP28SS, 2, {XMMREG,RM_XMM|BITS32,0,0,0}, {MASK|Z,SAE,0,0,0}, nasm_bytecodes+24260, 231},
    ITEMPLATE_END
};

static const struct itemplate instrux_VREDUCEPD[] = {
    {I_VREDUCEPD, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+6972, 227},
    {I_VREDUCEPD, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+6981, 227},
    {I_VREDUCEPD, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64|SAE,0,0,0}, nasm_bytecodes+6990, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VREDUCEPS[] = {
    {I_VREDUCEPS, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+6999, 227},
    {I_VREDUCEPS, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+7008, 227},
    {I_VREDUCEPS, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32|SAE,0,0,0}, nasm_bytecodes+7017, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VREDUCESD[] = {
    {I_VREDUCESD, 4, {XMMREG,XMMREG,RM_XMM|BITS64,IMMEDIATE|BITS8,0}, {MASK|Z,0,SAE,0,0}, nasm_bytecodes+7026, 228},
    {I_VREDUCESD, 3, {XMMREG,RM_XMM|BITS64,IMMEDIATE|BITS8,0,0}, {MASK|Z,SAE,0,0,0}, nasm_bytecodes+7035, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VREDUCESS[] = {
    {I_VREDUCESS, 4, {XMMREG,XMMREG,RM_XMM|BITS32,IMMEDIATE|BITS8,0}, {MASK|Z,0,SAE,0,0}, nasm_bytecodes+7044, 228},
    {I_VREDUCESS, 3, {XMMREG,RM_XMM|BITS32,IMMEDIATE|BITS8,0,0}, {MASK|Z,SAE,0,0,0}, nasm_bytecodes+7053, 228},
    ITEMPLATE_END
};

static const struct itemplate instrux_VRNDSCALEPD[] = {
    {I_VRNDSCALEPD, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+7062, 225},
    {I_VRNDSCALEPD, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+7071, 225},
    {I_VRNDSCALEPD, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64|SAE,0,0,0}, nasm_bytecodes+7080, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VRNDSCALEPS[] = {
    {I_VRNDSCALEPS, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+7089, 225},
    {I_VRNDSCALEPS, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+7098, 225},
    {I_VRNDSCALEPS, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32|SAE,0,0,0}, nasm_bytecodes+7107, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VRNDSCALESD[] = {
    {I_VRNDSCALESD, 4, {XMMREG,XMMREG,RM_XMM|BITS64,IMMEDIATE|BITS8,0}, {MASK|Z,0,SAE,0,0}, nasm_bytecodes+7116, 226},
    {I_VRNDSCALESD, 3, {XMMREG,RM_XMM|BITS64,IMMEDIATE|BITS8,0,0}, {MASK|Z,SAE,0,0,0}, nasm_bytecodes+7125, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VRNDSCALESS[] = {
    {I_VRNDSCALESS, 4, {XMMREG,XMMREG,RM_XMM|BITS32,IMMEDIATE|BITS8,0}, {MASK|Z,0,SAE,0,0}, nasm_bytecodes+7134, 226},
    {I_VRNDSCALESS, 3, {XMMREG,RM_XMM|BITS32,IMMEDIATE|BITS8,0,0}, {MASK|Z,SAE,0,0,0}, nasm_bytecodes+7143, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VRSQRT14PD[] = {
    {I_VRSQRT14PD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+24268, 225},
    {I_VRSQRT14PD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+24276, 225},
    {I_VRSQRT14PD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+24284, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VRSQRT14PS[] = {
    {I_VRSQRT14PS, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+24292, 225},
    {I_VRSQRT14PS, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+24300, 225},
    {I_VRSQRT14PS, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+24308, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VRSQRT14SD[] = {
    {I_VRSQRT14SD, 3, {XMMREG,XMMREG,RM_XMM|BITS64,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+24316, 226},
    {I_VRSQRT14SD, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+24324, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VRSQRT14SS[] = {
    {I_VRSQRT14SS, 3, {XMMREG,XMMREG,RM_XMM|BITS32,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+24332, 226},
    {I_VRSQRT14SS, 2, {XMMREG,RM_XMM|BITS32,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+24340, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VRSQRT28PD[] = {
    {I_VRSQRT28PD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64|SAE,0,0,0}, nasm_bytecodes+24348, 231},
    ITEMPLATE_END
};

static const struct itemplate instrux_VRSQRT28PS[] = {
    {I_VRSQRT28PS, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32|SAE,0,0,0}, nasm_bytecodes+24356, 231},
    ITEMPLATE_END
};

static const struct itemplate instrux_VRSQRT28SD[] = {
    {I_VRSQRT28SD, 3, {XMMREG,XMMREG,RM_XMM|BITS64,0,0}, {MASK|Z,0,SAE,0,0}, nasm_bytecodes+24364, 231},
    {I_VRSQRT28SD, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,SAE,0,0,0}, nasm_bytecodes+24372, 231},
    ITEMPLATE_END
};

static const struct itemplate instrux_VRSQRT28SS[] = {
    {I_VRSQRT28SS, 3, {XMMREG,XMMREG,RM_XMM|BITS32,0,0}, {MASK|Z,0,SAE,0,0}, nasm_bytecodes+24380, 231},
    {I_VRSQRT28SS, 2, {XMMREG,RM_XMM|BITS32,0,0,0}, {MASK|Z,SAE,0,0,0}, nasm_bytecodes+24388, 231},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSCALEFPD[] = {
    {I_VSCALEFPD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+24396, 225},
    {I_VSCALEFPD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+24404, 225},
    {I_VSCALEFPD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+24412, 225},
    {I_VSCALEFPD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+24420, 225},
    {I_VSCALEFPD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64|ER,0,0}, nasm_bytecodes+24428, 226},
    {I_VSCALEFPD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64|ER,0,0,0}, nasm_bytecodes+24436, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSCALEFPS[] = {
    {I_VSCALEFPS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+24444, 225},
    {I_VSCALEFPS, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+24452, 225},
    {I_VSCALEFPS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+24460, 225},
    {I_VSCALEFPS, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+24468, 225},
    {I_VSCALEFPS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32|ER,0,0}, nasm_bytecodes+24476, 226},
    {I_VSCALEFPS, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32|ER,0,0,0}, nasm_bytecodes+24484, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSCALEFSD[] = {
    {I_VSCALEFSD, 3, {XMMREG,XMMREG,RM_XMM|BITS64,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+24492, 226},
    {I_VSCALEFSD, 2, {XMMREG,RM_XMM|BITS64,0,0,0}, {MASK|Z,ER,0,0,0}, nasm_bytecodes+24500, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSCALEFSS[] = {
    {I_VSCALEFSS, 3, {XMMREG,XMMREG,RM_XMM|BITS32,0,0}, {MASK|Z,0,ER,0,0}, nasm_bytecodes+24508, 226},
    {I_VSCALEFSS, 2, {XMMREG,RM_XMM|BITS32,0,0,0}, {MASK|Z,ER,0,0,0}, nasm_bytecodes+24516, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSCATTERDPD[] = {
    {I_VSCATTERDPD, 2, {XMEM|BITS64,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+7152, 225},
    {I_VSCATTERDPD, 2, {XMEM|BITS64,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+7161, 225},
    {I_VSCATTERDPD, 2, {YMEM|BITS64,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+7170, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSCATTERDPS[] = {
    {I_VSCATTERDPS, 2, {XMEM|BITS32,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+7179, 225},
    {I_VSCATTERDPS, 2, {YMEM|BITS32,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+7188, 225},
    {I_VSCATTERDPS, 2, {ZMEM|BITS32,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+7197, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSCATTERPF0DPD[] = {
    {I_VSCATTERPF0DPD, 1, {YMEM|BITS64,0,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+7206, 232},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSCATTERPF0DPS[] = {
    {I_VSCATTERPF0DPS, 1, {ZMEM|BITS32,0,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+7215, 232},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSCATTERPF0QPD[] = {
    {I_VSCATTERPF0QPD, 1, {ZMEM|BITS64,0,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+7224, 232},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSCATTERPF0QPS[] = {
    {I_VSCATTERPF0QPS, 1, {ZMEM|BITS32,0,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+7233, 232},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSCATTERPF1DPD[] = {
    {I_VSCATTERPF1DPD, 1, {YMEM|BITS64,0,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+7242, 232},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSCATTERPF1DPS[] = {
    {I_VSCATTERPF1DPS, 1, {ZMEM|BITS32,0,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+7251, 232},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSCATTERPF1QPD[] = {
    {I_VSCATTERPF1QPD, 1, {ZMEM|BITS64,0,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+7260, 232},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSCATTERPF1QPS[] = {
    {I_VSCATTERPF1QPS, 1, {ZMEM|BITS32,0,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+7269, 232},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSCATTERQPD[] = {
    {I_VSCATTERQPD, 2, {XMEM|BITS64,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+7278, 225},
    {I_VSCATTERQPD, 2, {YMEM|BITS64,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+7287, 225},
    {I_VSCATTERQPD, 2, {ZMEM|BITS64,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+7296, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSCATTERQPS[] = {
    {I_VSCATTERQPS, 2, {XMEM|BITS32,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+7305, 225},
    {I_VSCATTERQPS, 2, {YMEM|BITS32,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+7314, 225},
    {I_VSCATTERQPS, 2, {ZMEM|BITS32,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+7323, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSHUFF32X4[] = {
    {I_VSHUFF32X4, 4, {YMMREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+7332, 225},
    {I_VSHUFF32X4, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+7341, 225},
    {I_VSHUFF32X4, 4, {ZMMREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+7350, 226},
    {I_VSHUFF32X4, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+7359, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSHUFF64X2[] = {
    {I_VSHUFF64X2, 4, {YMMREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+7368, 225},
    {I_VSHUFF64X2, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+7377, 225},
    {I_VSHUFF64X2, 4, {ZMMREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+7386, 226},
    {I_VSHUFF64X2, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+7395, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSHUFI32X4[] = {
    {I_VSHUFI32X4, 4, {YMMREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+7404, 225},
    {I_VSHUFI32X4, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+7413, 225},
    {I_VSHUFI32X4, 4, {ZMMREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+7422, 226},
    {I_VSHUFI32X4, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+7431, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_VSHUFI64X2[] = {
    {I_VSHUFI64X2, 4, {YMMREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+7440, 225},
    {I_VSHUFI64X2, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+7449, 225},
    {I_VSHUFI64X2, 4, {ZMMREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+7458, 226},
    {I_VSHUFI64X2, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+7467, 226},
    ITEMPLATE_END
};

static const struct itemplate instrux_RDPKRU[] = {
    {I_RDPKRU, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40506, 239},
    ITEMPLATE_END
};

static const struct itemplate instrux_WRPKRU[] = {
    {I_WRPKRU, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40511, 239},
    ITEMPLATE_END
};

static const struct itemplate instrux_RDPID[] = {
    {I_RDPID, 1, {REG_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+34932, 240},
    {I_RDPID, 1, {REG_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+34931, 239},
    {I_RDPID, 1, {REG_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+34932, 241},
    ITEMPLATE_END
};

static const struct itemplate instrux_CLFLUSHOPT[] = {
    {I_CLFLUSHOPT, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37528, 133},
    ITEMPLATE_END
};

static const struct itemplate instrux_CLWB[] = {
    {I_CLWB, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37534, 133},
    ITEMPLATE_END
};

static const struct itemplate instrux_PCOMMIT[] = {
    {I_PCOMMIT, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37540, 242},
    ITEMPLATE_END
};

static const struct itemplate instrux_CLZERO[] = {
    {I_CLZERO, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40516, 243},
    ITEMPLATE_END
};

static const struct itemplate instrux_PTWRITE[] = {
    {I_PTWRITE, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+26840, 133},
    {I_PTWRITE, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+26839, 239},
    ITEMPLATE_END
};

static const struct itemplate instrux_CLDEMOTE[] = {
    {I_CLDEMOTE, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37546, 133},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVDIRI[] = {
    {I_MOVDIRI, 2, {MEMORY|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+34938, 244},
    {I_MOVDIRI, 2, {MEMORY|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+34945, 245},
    ITEMPLATE_END
};

static const struct itemplate instrux_MOVDIR64B[] = {
    {I_MOVDIR64B, 2, {REG_GPR|BITS16,MEMORY|BITS512,0,0,0}, NO_DECORATOR, nasm_bytecodes+25036, 240},
    {I_MOVDIR64B, 2, {REG_GPR|BITS32,MEMORY|BITS512,0,0,0}, NO_DECORATOR, nasm_bytecodes+25044, 133},
    {I_MOVDIR64B, 2, {REG_GPR|BITS64,MEMORY|BITS512,0,0,0}, NO_DECORATOR, nasm_bytecodes+25052, 239},
    ITEMPLATE_END
};

static const struct itemplate instrux_PCONFIG[] = {
    {I_PCONFIG, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37552, 133},
    ITEMPLATE_END
};

static const struct itemplate instrux_TPAUSE[] = {
    {I_TPAUSE, 1, {REG_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37534, 133},
    {I_TPAUSE, 3, {REG_GPR|BITS32,REG_EDX,REG_EAX,0,0}, NO_DECORATOR, nasm_bytecodes+37534, 133},
    ITEMPLATE_END
};

static const struct itemplate instrux_UMONITOR[] = {
    {I_UMONITOR, 1, {REG_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+34952, 240},
    {I_UMONITOR, 1, {REG_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+34959, 133},
    {I_UMONITOR, 1, {REG_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+34966, 239},
    ITEMPLATE_END
};

static const struct itemplate instrux_UMWAIT[] = {
    {I_UMWAIT, 1, {REG_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37558, 133},
    {I_UMWAIT, 3, {REG_GPR|BITS32,REG_EDX,REG_EAX,0,0}, NO_DECORATOR, nasm_bytecodes+37558, 133},
    ITEMPLATE_END
};

static const struct itemplate instrux_WBNOINVD[] = {
    {I_WBNOINVD, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40521, 133},
    ITEMPLATE_END
};

static const struct itemplate instrux_GF2P8AFFINEINVQB[] = {
    {I_GF2P8AFFINEINVQB, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+25060, 246},
    ITEMPLATE_END
};

static const struct itemplate instrux_VGF2P8AFFINEINVQB[] = {
    {I_VGF2P8AFFINEINVQB, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+25068, 247},
    {I_VGF2P8AFFINEINVQB, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+25076, 247},
    {I_VGF2P8AFFINEINVQB, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+25084, 247},
    {I_VGF2P8AFFINEINVQB, 3, {YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+25092, 247},
    {I_VGF2P8AFFINEINVQB, 4, {XMMREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+7584, 248},
    {I_VGF2P8AFFINEINVQB, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+7593, 248},
    {I_VGF2P8AFFINEINVQB, 4, {YMMREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+7602, 248},
    {I_VGF2P8AFFINEINVQB, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+7611, 248},
    {I_VGF2P8AFFINEINVQB, 4, {ZMMREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+7620, 249},
    {I_VGF2P8AFFINEINVQB, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+7629, 249},
    ITEMPLATE_END
};

static const struct itemplate instrux_GF2P8AFFINEQB[] = {
    {I_GF2P8AFFINEQB, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+25100, 246},
    ITEMPLATE_END
};

static const struct itemplate instrux_VGF2P8AFFINEQB[] = {
    {I_VGF2P8AFFINEQB, 4, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+25108, 247},
    {I_VGF2P8AFFINEQB, 3, {XMM_L16,RM_XMM_L16|BITS128,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+25116, 247},
    {I_VGF2P8AFFINEQB, 4, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0}, NO_DECORATOR, nasm_bytecodes+25124, 247},
    {I_VGF2P8AFFINEQB, 3, {YMM_L16,RM_YMM_L16|BITS256,IMMEDIATE|BITS8,0,0}, NO_DECORATOR, nasm_bytecodes+25132, 247},
    {I_VGF2P8AFFINEQB, 4, {XMMREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+7638, 248},
    {I_VGF2P8AFFINEQB, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+7647, 248},
    {I_VGF2P8AFFINEQB, 4, {YMMREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+7656, 248},
    {I_VGF2P8AFFINEQB, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+7665, 248},
    {I_VGF2P8AFFINEQB, 4, {ZMMREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+7674, 249},
    {I_VGF2P8AFFINEQB, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+7683, 249},
    ITEMPLATE_END
};

static const struct itemplate instrux_GF2P8MULB[] = {
    {I_GF2P8MULB, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+34973, 246},
    ITEMPLATE_END
};

static const struct itemplate instrux_VGF2P8MULB[] = {
    {I_VGF2P8MULB, 3, {XMM_L16,XMM_L16,RM_XMM_L16|BITS128,0,0}, NO_DECORATOR, nasm_bytecodes+34980, 247},
    {I_VGF2P8MULB, 2, {XMM_L16,RM_XMM_L16|BITS128,0,0,0}, NO_DECORATOR, nasm_bytecodes+34987, 247},
    {I_VGF2P8MULB, 3, {YMM_L16,YMM_L16,RM_YMM_L16|BITS256,0,0}, NO_DECORATOR, nasm_bytecodes+34994, 247},
    {I_VGF2P8MULB, 2, {YMM_L16,RM_YMM_L16|BITS256,0,0,0}, NO_DECORATOR, nasm_bytecodes+35001, 247},
    {I_VGF2P8MULB, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25140, 248},
    {I_VGF2P8MULB, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25148, 248},
    {I_VGF2P8MULB, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25156, 248},
    {I_VGF2P8MULB, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25164, 248},
    {I_VGF2P8MULB, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25172, 249},
    {I_VGF2P8MULB, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25180, 249},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCOMPRESSB[] = {
    {I_VPCOMPRESSB, 2, {MEMORY|BITS128,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+25188, 250},
    {I_VPCOMPRESSB, 2, {MEMORY|BITS256,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+25196, 250},
    {I_VPCOMPRESSB, 2, {MEMORY|BITS512,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+25204, 251},
    {I_VPCOMPRESSB, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25212, 250},
    {I_VPCOMPRESSB, 2, {YMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25220, 250},
    {I_VPCOMPRESSB, 2, {ZMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25228, 251},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPCOMPRESSW[] = {
    {I_VPCOMPRESSW, 2, {MEMORY|BITS128,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+25236, 250},
    {I_VPCOMPRESSW, 2, {MEMORY|BITS256,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+25244, 250},
    {I_VPCOMPRESSW, 2, {MEMORY|BITS512,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+25252, 251},
    {I_VPCOMPRESSW, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25260, 250},
    {I_VPCOMPRESSW, 2, {YMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25268, 250},
    {I_VPCOMPRESSW, 2, {ZMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25276, 251},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPEXPANDB[] = {
    {I_VPEXPANDB, 2, {MEMORY|BITS128,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+25284, 250},
    {I_VPEXPANDB, 2, {MEMORY|BITS256,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+25292, 250},
    {I_VPEXPANDB, 2, {MEMORY|BITS512,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+25300, 251},
    {I_VPEXPANDB, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25308, 250},
    {I_VPEXPANDB, 2, {YMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25316, 250},
    {I_VPEXPANDB, 2, {ZMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25324, 251},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPEXPANDW[] = {
    {I_VPEXPANDW, 2, {MEMORY|BITS128,XMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+25332, 250},
    {I_VPEXPANDW, 2, {MEMORY|BITS256,YMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+25340, 250},
    {I_VPEXPANDW, 2, {MEMORY|BITS512,ZMMREG,0,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+25348, 251},
    {I_VPEXPANDW, 2, {XMMREG,XMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25356, 250},
    {I_VPEXPANDW, 2, {YMMREG,YMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25364, 250},
    {I_VPEXPANDW, 2, {ZMMREG,ZMMREG,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25372, 251},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSHLDW[] = {
    {I_VPSHLDW, 4, {XMMREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+7692, 250},
    {I_VPSHLDW, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+7701, 250},
    {I_VPSHLDW, 4, {YMMREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+7710, 250},
    {I_VPSHLDW, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+7719, 250},
    {I_VPSHLDW, 4, {ZMMREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+7728, 251},
    {I_VPSHLDW, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+7737, 251},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSHLDD[] = {
    {I_VPSHLDD, 4, {XMMREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+7746, 250},
    {I_VPSHLDD, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+7755, 250},
    {I_VPSHLDD, 4, {YMMREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+7764, 250},
    {I_VPSHLDD, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+7773, 250},
    {I_VPSHLDD, 4, {ZMMREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+7782, 251},
    {I_VPSHLDD, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+7791, 251},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSHLDQ[] = {
    {I_VPSHLDQ, 4, {XMMREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+7800, 250},
    {I_VPSHLDQ, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+7809, 250},
    {I_VPSHLDQ, 4, {YMMREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+7818, 250},
    {I_VPSHLDQ, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+7827, 250},
    {I_VPSHLDQ, 4, {ZMMREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+7836, 251},
    {I_VPSHLDQ, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+7845, 251},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSHLDVW[] = {
    {I_VPSHLDVW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+7854, 250},
    {I_VPSHLDVW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+7863, 250},
    {I_VPSHLDVW, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+7872, 250},
    {I_VPSHLDVW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+7881, 250},
    {I_VPSHLDVW, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+7890, 251},
    {I_VPSHLDVW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+7899, 251},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSHLDVD[] = {
    {I_VPSHLDVD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+7908, 250},
    {I_VPSHLDVD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+7917, 250},
    {I_VPSHLDVD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+7926, 250},
    {I_VPSHLDVD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+7935, 250},
    {I_VPSHLDVD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+7944, 251},
    {I_VPSHLDVD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+7953, 251},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSHLDVQ[] = {
    {I_VPSHLDVQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+7962, 250},
    {I_VPSHLDVQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+7971, 250},
    {I_VPSHLDVQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+7980, 250},
    {I_VPSHLDVQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+7989, 250},
    {I_VPSHLDVQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+7998, 251},
    {I_VPSHLDVQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+8007, 251},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSHRDW[] = {
    {I_VPSHRDW, 4, {XMMREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+8016, 250},
    {I_VPSHRDW, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+8025, 250},
    {I_VPSHRDW, 4, {YMMREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+8034, 250},
    {I_VPSHRDW, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+8043, 250},
    {I_VPSHRDW, 4, {ZMMREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+8052, 251},
    {I_VPSHRDW, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+8061, 251},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSHRDD[] = {
    {I_VPSHRDD, 4, {XMMREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+8070, 250},
    {I_VPSHRDD, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+8079, 250},
    {I_VPSHRDD, 4, {YMMREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+8088, 250},
    {I_VPSHRDD, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+8097, 250},
    {I_VPSHRDD, 4, {ZMMREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+8106, 251},
    {I_VPSHRDD, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+8115, 251},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSHRDQ[] = {
    {I_VPSHRDQ, 4, {XMMREG,XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+8124, 250},
    {I_VPSHRDQ, 3, {XMMREG,RM_XMM|BITS128,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+8133, 250},
    {I_VPSHRDQ, 4, {YMMREG,YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+8142, 250},
    {I_VPSHRDQ, 3, {YMMREG,RM_YMM|BITS256,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+8151, 250},
    {I_VPSHRDQ, 4, {ZMMREG,ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+8160, 251},
    {I_VPSHRDQ, 3, {ZMMREG,RM_ZMM|BITS512,IMMEDIATE|BITS8,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+8169, 251},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSHRDVW[] = {
    {I_VPSHRDVW, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+8178, 250},
    {I_VPSHRDVW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+8187, 250},
    {I_VPSHRDVW, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+8196, 250},
    {I_VPSHRDVW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+8205, 250},
    {I_VPSHRDVW, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+8214, 251},
    {I_VPSHRDVW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+8223, 251},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSHRDVD[] = {
    {I_VPSHRDVD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+8232, 250},
    {I_VPSHRDVD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+8241, 250},
    {I_VPSHRDVD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+8250, 250},
    {I_VPSHRDVD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+8259, 250},
    {I_VPSHRDVD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+8268, 251},
    {I_VPSHRDVD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+8277, 251},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSHRDVQ[] = {
    {I_VPSHRDVQ, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+8286, 250},
    {I_VPSHRDVQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+8295, 250},
    {I_VPSHRDVQ, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+8304, 250},
    {I_VPSHRDVQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+8313, 250},
    {I_VPSHRDVQ, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B64,0,0}, nasm_bytecodes+8322, 251},
    {I_VPSHRDVQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B64,0,0,0}, nasm_bytecodes+8331, 251},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPDPBUSD[] = {
    {I_VPDPBUSD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+25380, 252},
    {I_VPDPBUSD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+25388, 252},
    {I_VPDPBUSD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+25396, 252},
    {I_VPDPBUSD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+25404, 252},
    {I_VPDPBUSD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+25412, 253},
    {I_VPDPBUSD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+25420, 253},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPDPBUSDS[] = {
    {I_VPDPBUSDS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+25428, 252},
    {I_VPDPBUSDS, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+25436, 252},
    {I_VPDPBUSDS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+25444, 252},
    {I_VPDPBUSDS, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+25452, 252},
    {I_VPDPBUSDS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+25460, 253},
    {I_VPDPBUSDS, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+25468, 253},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPDPWSSD[] = {
    {I_VPDPWSSD, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+25476, 252},
    {I_VPDPWSSD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+25484, 252},
    {I_VPDPWSSD, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+25492, 252},
    {I_VPDPWSSD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+25500, 252},
    {I_VPDPWSSD, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+25508, 253},
    {I_VPDPWSSD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+25516, 253},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPDPWSSDS[] = {
    {I_VPDPWSSDS, 3, {XMMREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+25524, 252},
    {I_VPDPWSSDS, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+25532, 252},
    {I_VPDPWSSDS, 3, {YMMREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+25540, 252},
    {I_VPDPWSSDS, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+25548, 252},
    {I_VPDPWSSDS, 3, {ZMMREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK|Z,0,B32,0,0}, nasm_bytecodes+25556, 253},
    {I_VPDPWSSDS, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,B32,0,0,0}, nasm_bytecodes+25564, 253},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPOPCNTB[] = {
    {I_VPOPCNTB, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25572, 254},
    {I_VPOPCNTB, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25580, 254},
    {I_VPOPCNTB, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25588, 255},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPOPCNTW[] = {
    {I_VPOPCNTW, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25596, 254},
    {I_VPOPCNTW, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25604, 254},
    {I_VPOPCNTW, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25612, 255},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPOPCNTD[] = {
    {I_VPOPCNTD, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25620, 256},
    {I_VPOPCNTD, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25628, 256},
    {I_VPOPCNTD, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25636, 257},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPOPCNTQ[] = {
    {I_VPOPCNTQ, 2, {XMMREG,RM_XMM|BITS128,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25644, 256},
    {I_VPOPCNTQ, 2, {YMMREG,RM_YMM|BITS256,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25652, 256},
    {I_VPOPCNTQ, 2, {ZMMREG,RM_ZMM|BITS512,0,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25660, 257},
    ITEMPLATE_END
};

static const struct itemplate instrux_VPSHUFBITQMB[] = {
    {I_VPSHUFBITQMB, 3, {KREG,XMMREG,RM_XMM|BITS128,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+25668, 254},
    {I_VPSHUFBITQMB, 3, {KREG,YMMREG,RM_YMM|BITS256,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+25676, 254},
    {I_VPSHUFBITQMB, 3, {KREG,ZMMREG,RM_ZMM|BITS512,0,0}, {MASK,0,0,0,0}, nasm_bytecodes+25684, 255},
    ITEMPLATE_END
};

static const struct itemplate instrux_V4FMADDPS[] = {
    {I_V4FMADDPS, 3, {ZMM_L16,ZMM_L16|RS4,MEMORY,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25692, 258},
    ITEMPLATE_END
};

static const struct itemplate instrux_V4FNMADDPS[] = {
    {I_V4FNMADDPS, 3, {ZMM_L16,ZMM_L16|RS4,MEMORY,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25700, 258},
    ITEMPLATE_END
};

static const struct itemplate instrux_V4FMADDSS[] = {
    {I_V4FMADDSS, 3, {ZMM_L16,ZMM_L16|RS4,MEMORY,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25708, 258},
    ITEMPLATE_END
};

static const struct itemplate instrux_V4FNMADDSS[] = {
    {I_V4FNMADDSS, 3, {ZMM_L16,ZMM_L16|RS4,MEMORY,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25716, 258},
    ITEMPLATE_END
};

static const struct itemplate instrux_V4DPWSSDS[] = {
    {I_V4DPWSSDS, 3, {ZMM_L16,ZMM_L16|RS4,MEMORY,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25724, 259},
    ITEMPLATE_END
};

static const struct itemplate instrux_V4DPWSSD[] = {
    {I_V4DPWSSD, 3, {ZMM_L16,ZMM_L16|RS4,MEMORY,0,0}, {MASK|Z,0,0,0,0}, nasm_bytecodes+25732, 259},
    ITEMPLATE_END
};

static const struct itemplate instrux_ENCLS[] = {
    {I_ENCLS, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37564, 260},
    ITEMPLATE_END
};

static const struct itemplate instrux_ENCLU[] = {
    {I_ENCLU, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37570, 260},
    ITEMPLATE_END
};

static const struct itemplate instrux_ENCLV[] = {
    {I_ENCLV, 0, {0,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37576, 260},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP0[] = {
    {I_HINT_NOP0, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37582, 261},
    {I_HINT_NOP0, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37588, 261},
    {I_HINT_NOP0, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37594, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP1[] = {
    {I_HINT_NOP1, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37600, 261},
    {I_HINT_NOP1, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37606, 261},
    {I_HINT_NOP1, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37612, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP2[] = {
    {I_HINT_NOP2, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37618, 261},
    {I_HINT_NOP2, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37624, 261},
    {I_HINT_NOP2, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37630, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP3[] = {
    {I_HINT_NOP3, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37636, 261},
    {I_HINT_NOP3, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37642, 261},
    {I_HINT_NOP3, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37648, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP4[] = {
    {I_HINT_NOP4, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37654, 261},
    {I_HINT_NOP4, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37660, 261},
    {I_HINT_NOP4, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37666, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP5[] = {
    {I_HINT_NOP5, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37672, 261},
    {I_HINT_NOP5, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37678, 261},
    {I_HINT_NOP5, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37684, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP6[] = {
    {I_HINT_NOP6, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37690, 261},
    {I_HINT_NOP6, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37696, 261},
    {I_HINT_NOP6, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37702, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP7[] = {
    {I_HINT_NOP7, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37708, 261},
    {I_HINT_NOP7, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37714, 261},
    {I_HINT_NOP7, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37720, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP8[] = {
    {I_HINT_NOP8, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37726, 261},
    {I_HINT_NOP8, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37732, 261},
    {I_HINT_NOP8, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37738, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP9[] = {
    {I_HINT_NOP9, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37744, 261},
    {I_HINT_NOP9, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37750, 261},
    {I_HINT_NOP9, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37756, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP10[] = {
    {I_HINT_NOP10, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37762, 261},
    {I_HINT_NOP10, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37768, 261},
    {I_HINT_NOP10, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37774, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP11[] = {
    {I_HINT_NOP11, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37780, 261},
    {I_HINT_NOP11, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37786, 261},
    {I_HINT_NOP11, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37792, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP12[] = {
    {I_HINT_NOP12, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37798, 261},
    {I_HINT_NOP12, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37804, 261},
    {I_HINT_NOP12, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37810, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP13[] = {
    {I_HINT_NOP13, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37816, 261},
    {I_HINT_NOP13, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37822, 261},
    {I_HINT_NOP13, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37828, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP14[] = {
    {I_HINT_NOP14, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37834, 261},
    {I_HINT_NOP14, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37840, 261},
    {I_HINT_NOP14, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37846, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP15[] = {
    {I_HINT_NOP15, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37852, 261},
    {I_HINT_NOP15, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37858, 261},
    {I_HINT_NOP15, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37864, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP16[] = {
    {I_HINT_NOP16, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37870, 261},
    {I_HINT_NOP16, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37876, 261},
    {I_HINT_NOP16, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37882, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP17[] = {
    {I_HINT_NOP17, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37888, 261},
    {I_HINT_NOP17, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37894, 261},
    {I_HINT_NOP17, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37900, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP18[] = {
    {I_HINT_NOP18, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37906, 261},
    {I_HINT_NOP18, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37912, 261},
    {I_HINT_NOP18, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37918, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP19[] = {
    {I_HINT_NOP19, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37924, 261},
    {I_HINT_NOP19, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37930, 261},
    {I_HINT_NOP19, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37936, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP20[] = {
    {I_HINT_NOP20, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37942, 261},
    {I_HINT_NOP20, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37948, 261},
    {I_HINT_NOP20, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37954, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP21[] = {
    {I_HINT_NOP21, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37960, 261},
    {I_HINT_NOP21, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37966, 261},
    {I_HINT_NOP21, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37972, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP22[] = {
    {I_HINT_NOP22, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37978, 261},
    {I_HINT_NOP22, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37984, 261},
    {I_HINT_NOP22, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37990, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP23[] = {
    {I_HINT_NOP23, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+37996, 261},
    {I_HINT_NOP23, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38002, 261},
    {I_HINT_NOP23, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38008, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP24[] = {
    {I_HINT_NOP24, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38014, 261},
    {I_HINT_NOP24, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38020, 261},
    {I_HINT_NOP24, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38026, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP25[] = {
    {I_HINT_NOP25, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38032, 261},
    {I_HINT_NOP25, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38038, 261},
    {I_HINT_NOP25, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38044, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP26[] = {
    {I_HINT_NOP26, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38050, 261},
    {I_HINT_NOP26, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38056, 261},
    {I_HINT_NOP26, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38062, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP27[] = {
    {I_HINT_NOP27, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38068, 261},
    {I_HINT_NOP27, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38074, 261},
    {I_HINT_NOP27, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38080, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP28[] = {
    {I_HINT_NOP28, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38086, 261},
    {I_HINT_NOP28, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38092, 261},
    {I_HINT_NOP28, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38098, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP29[] = {
    {I_HINT_NOP29, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38104, 261},
    {I_HINT_NOP29, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38110, 261},
    {I_HINT_NOP29, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38116, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP30[] = {
    {I_HINT_NOP30, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38122, 261},
    {I_HINT_NOP30, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38128, 261},
    {I_HINT_NOP30, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38134, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP31[] = {
    {I_HINT_NOP31, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38140, 261},
    {I_HINT_NOP31, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38146, 261},
    {I_HINT_NOP31, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38152, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP32[] = {
    {I_HINT_NOP32, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38158, 261},
    {I_HINT_NOP32, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38164, 261},
    {I_HINT_NOP32, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38170, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP33[] = {
    {I_HINT_NOP33, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38176, 261},
    {I_HINT_NOP33, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38182, 261},
    {I_HINT_NOP33, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38188, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP34[] = {
    {I_HINT_NOP34, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38194, 261},
    {I_HINT_NOP34, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38200, 261},
    {I_HINT_NOP34, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38206, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP35[] = {
    {I_HINT_NOP35, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38212, 261},
    {I_HINT_NOP35, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38218, 261},
    {I_HINT_NOP35, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38224, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP36[] = {
    {I_HINT_NOP36, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38230, 261},
    {I_HINT_NOP36, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38236, 261},
    {I_HINT_NOP36, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38242, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP37[] = {
    {I_HINT_NOP37, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38248, 261},
    {I_HINT_NOP37, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38254, 261},
    {I_HINT_NOP37, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38260, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP38[] = {
    {I_HINT_NOP38, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38266, 261},
    {I_HINT_NOP38, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38272, 261},
    {I_HINT_NOP38, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38278, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP39[] = {
    {I_HINT_NOP39, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38284, 261},
    {I_HINT_NOP39, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38290, 261},
    {I_HINT_NOP39, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38296, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP40[] = {
    {I_HINT_NOP40, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38302, 261},
    {I_HINT_NOP40, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38308, 261},
    {I_HINT_NOP40, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38314, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP41[] = {
    {I_HINT_NOP41, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38320, 261},
    {I_HINT_NOP41, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38326, 261},
    {I_HINT_NOP41, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38332, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP42[] = {
    {I_HINT_NOP42, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38338, 261},
    {I_HINT_NOP42, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38344, 261},
    {I_HINT_NOP42, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38350, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP43[] = {
    {I_HINT_NOP43, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38356, 261},
    {I_HINT_NOP43, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38362, 261},
    {I_HINT_NOP43, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38368, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP44[] = {
    {I_HINT_NOP44, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38374, 261},
    {I_HINT_NOP44, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38380, 261},
    {I_HINT_NOP44, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38386, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP45[] = {
    {I_HINT_NOP45, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38392, 261},
    {I_HINT_NOP45, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38398, 261},
    {I_HINT_NOP45, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38404, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP46[] = {
    {I_HINT_NOP46, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38410, 261},
    {I_HINT_NOP46, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38416, 261},
    {I_HINT_NOP46, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38422, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP47[] = {
    {I_HINT_NOP47, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38428, 261},
    {I_HINT_NOP47, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38434, 261},
    {I_HINT_NOP47, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38440, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP48[] = {
    {I_HINT_NOP48, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38446, 261},
    {I_HINT_NOP48, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38452, 261},
    {I_HINT_NOP48, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38458, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP49[] = {
    {I_HINT_NOP49, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38464, 261},
    {I_HINT_NOP49, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38470, 261},
    {I_HINT_NOP49, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38476, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP50[] = {
    {I_HINT_NOP50, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38482, 261},
    {I_HINT_NOP50, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38488, 261},
    {I_HINT_NOP50, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38494, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP51[] = {
    {I_HINT_NOP51, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38500, 261},
    {I_HINT_NOP51, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38506, 261},
    {I_HINT_NOP51, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38512, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP52[] = {
    {I_HINT_NOP52, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38518, 261},
    {I_HINT_NOP52, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38524, 261},
    {I_HINT_NOP52, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38530, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP53[] = {
    {I_HINT_NOP53, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38536, 261},
    {I_HINT_NOP53, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38542, 261},
    {I_HINT_NOP53, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38548, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP54[] = {
    {I_HINT_NOP54, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38554, 261},
    {I_HINT_NOP54, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38560, 261},
    {I_HINT_NOP54, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38566, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP55[] = {
    {I_HINT_NOP55, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38572, 261},
    {I_HINT_NOP55, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38578, 261},
    {I_HINT_NOP55, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38584, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP56[] = {
    {I_HINT_NOP56, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35698, 261},
    {I_HINT_NOP56, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35704, 261},
    {I_HINT_NOP56, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+35710, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP57[] = {
    {I_HINT_NOP57, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38590, 261},
    {I_HINT_NOP57, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38596, 261},
    {I_HINT_NOP57, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38602, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP58[] = {
    {I_HINT_NOP58, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38608, 261},
    {I_HINT_NOP58, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38614, 261},
    {I_HINT_NOP58, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38620, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP59[] = {
    {I_HINT_NOP59, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38626, 261},
    {I_HINT_NOP59, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38632, 261},
    {I_HINT_NOP59, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38638, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP60[] = {
    {I_HINT_NOP60, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38644, 261},
    {I_HINT_NOP60, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38650, 261},
    {I_HINT_NOP60, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38656, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP61[] = {
    {I_HINT_NOP61, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38662, 261},
    {I_HINT_NOP61, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38668, 261},
    {I_HINT_NOP61, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38674, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP62[] = {
    {I_HINT_NOP62, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38680, 261},
    {I_HINT_NOP62, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38686, 261},
    {I_HINT_NOP62, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38692, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_HINT_NOP63[] = {
    {I_HINT_NOP63, 1, {RM_GPR|BITS16,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38698, 261},
    {I_HINT_NOP63, 1, {RM_GPR|BITS32,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38704, 261},
    {I_HINT_NOP63, 1, {RM_GPR|BITS64,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+38710, 262},
    ITEMPLATE_END
};

static const struct itemplate instrux_CMOVcc[] = {
    {I_CMOVcc, 2, {REG_GPR|BITS16,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+26720, 118},
    {I_CMOVcc, 2, {REG_GPR|BITS16,REG_GPR|BITS16,0,0,0}, NO_DECORATOR, nasm_bytecodes+26720, 86},
    {I_CMOVcc, 2, {REG_GPR|BITS32,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+26727, 118},
    {I_CMOVcc, 2, {REG_GPR|BITS32,REG_GPR|BITS32,0,0,0}, NO_DECORATOR, nasm_bytecodes+26727, 86},
    {I_CMOVcc, 2, {REG_GPR|BITS64,MEMORY,0,0,0}, NO_DECORATOR, nasm_bytecodes+26734, 10},
    {I_CMOVcc, 2, {REG_GPR|BITS64,REG_GPR|BITS64,0,0,0}, NO_DECORATOR, nasm_bytecodes+26734, 7},
    ITEMPLATE_END
};

static const struct itemplate instrux_Jcc[] = {
    {I_Jcc, 1, {IMMEDIATE|NEAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+26741, 119},
    {I_Jcc, 1, {IMMEDIATE|BITS16|NEAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+26748, 27},
    {I_Jcc, 1, {IMMEDIATE|BITS32|NEAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+26755, 27},
    {I_Jcc, 1, {IMMEDIATE|BITS64|NEAR,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+26762, 28},
    {I_Jcc, 1, {IMMEDIATE|SHORT,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40402, 25},
    {I_Jcc, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40401, 25},
    {I_Jcc, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+26763, 119},
    {I_Jcc, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+26769, 25},
    {I_Jcc, 1, {IMMEDIATE,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+40402, 25},
    ITEMPLATE_END
};

static const struct itemplate instrux_SETcc[] = {
    {I_SETcc, 1, {MEMORY,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+36250, 21},
    {I_SETcc, 1, {REG_GPR|BITS8,0,0,0,0}, NO_DECORATOR, nasm_bytecodes+36250, 5},
    ITEMPLATE_END
};

const struct itemplate * const nasm_instructions[] = {
    instrux_DB,
    instrux_DW,
    instrux_DD,
    instrux_DQ,
    instrux_DT,
    instrux_DO,
    instrux_DY,
    instrux_DZ,
    instrux_RESB,
    instrux_RESW,
    instrux_RESD,
    instrux_RESQ,
    instrux_REST,
    instrux_RESO,
    instrux_RESY,
    instrux_RESZ,
    instrux_INCBIN,
    instrux_AAA,
    instrux_AAD,
    instrux_AAM,
    instrux_AAS,
    instrux_ADC,
    instrux_ADD,
    instrux_AND,
    instrux_ARPL,
    instrux_BB0_RESET,
    instrux_BB1_RESET,
    instrux_BOUND,
    instrux_BSF,
    instrux_BSR,
    instrux_BSWAP,
    instrux_BT,
    instrux_BTC,
    instrux_BTR,
    instrux_BTS,
    instrux_CALL,
    instrux_CBW,
    instrux_CDQ,
    instrux_CDQE,
    instrux_CLC,
    instrux_CLD,
    instrux_CLI,
    instrux_CLTS,
    instrux_CMC,
    instrux_CMP,
    instrux_CMPSB,
    instrux_CMPSD,
    instrux_CMPSQ,
    instrux_CMPSW,
    instrux_CMPXCHG,
    instrux_CMPXCHG486,
    instrux_CMPXCHG8B,
    instrux_CMPXCHG16B,
    instrux_CPUID,
    instrux_CPU_READ,
    instrux_CPU_WRITE,
    instrux_CQO,
    instrux_CWD,
    instrux_CWDE,
    instrux_DAA,
    instrux_DAS,
    instrux_DEC,
    instrux_DIV,
    instrux_DMINT,
    instrux_EMMS,
    instrux_ENTER,
    instrux_EQU,
    instrux_F2XM1,
    instrux_FABS,
    instrux_FADD,
    instrux_FADDP,
    instrux_FBLD,
    instrux_FBSTP,
    instrux_FCHS,
    instrux_FCLEX,
    instrux_FCMOVB,
    instrux_FCMOVBE,
    instrux_FCMOVE,
    instrux_FCMOVNB,
    instrux_FCMOVNBE,
    instrux_FCMOVNE,
    instrux_FCMOVNU,
    instrux_FCMOVU,
    instrux_FCOM,
    instrux_FCOMI,
    instrux_FCOMIP,
    instrux_FCOMP,
    instrux_FCOMPP,
    instrux_FCOS,
    instrux_FDECSTP,
    instrux_FDISI,
    instrux_FDIV,
    instrux_FDIVP,
    instrux_FDIVR,
    instrux_FDIVRP,
    instrux_FEMMS,
    instrux_FENI,
    instrux_FFREE,
    instrux_FFREEP,
    instrux_FIADD,
    instrux_FICOM,
    instrux_FICOMP,
    instrux_FIDIV,
    instrux_FIDIVR,
    instrux_FILD,
    instrux_FIMUL,
    instrux_FINCSTP,
    instrux_FINIT,
    instrux_FIST,
    instrux_FISTP,
    instrux_FISTTP,
    instrux_FISUB,
    instrux_FISUBR,
    instrux_FLD,
    instrux_FLD1,
    instrux_FLDCW,
    instrux_FLDENV,
    instrux_FLDL2E,
    instrux_FLDL2T,
    instrux_FLDLG2,
    instrux_FLDLN2,
    instrux_FLDPI,
    instrux_FLDZ,
    instrux_FMUL,
    instrux_FMULP,
    instrux_FNCLEX,
    instrux_FNDISI,
    instrux_FNENI,
    instrux_FNINIT,
    instrux_FNOP,
    instrux_FNSAVE,
    instrux_FNSTCW,
    instrux_FNSTENV,
    instrux_FNSTSW,
    instrux_FPATAN,
    instrux_FPREM,
    instrux_FPREM1,
    instrux_FPTAN,
    instrux_FRNDINT,
    instrux_FRSTOR,
    instrux_FSAVE,
    instrux_FSCALE,
    instrux_FSETPM,
    instrux_FSIN,
    instrux_FSINCOS,
    instrux_FSQRT,
    instrux_FST,
    instrux_FSTCW,
    instrux_FSTENV,
    instrux_FSTP,
    instrux_FSTSW,
    instrux_FSUB,
    instrux_FSUBP,
    instrux_FSUBR,
    instrux_FSUBRP,
    instrux_FTST,
    instrux_FUCOM,
    instrux_FUCOMI,
    instrux_FUCOMIP,
    instrux_FUCOMP,
    instrux_FUCOMPP,
    instrux_FXAM,
    instrux_FXCH,
    instrux_FXTRACT,
    instrux_FYL2X,
    instrux_FYL2XP1,
    instrux_HLT,
    instrux_IBTS,
    instrux_ICEBP,
    instrux_IDIV,
    instrux_IMUL,
    instrux_IN,
    instrux_INC,
    instrux_INSB,
    instrux_INSD,
    instrux_INSW,
    instrux_INT,
    instrux_INT01,
    instrux_INT1,
    instrux_INT03,
    instrux_INT3,
    instrux_INTO,
    instrux_INVD,
    instrux_INVPCID,
    instrux_INVLPG,
    instrux_INVLPGA,
    instrux_IRET,
    instrux_IRETD,
    instrux_IRETQ,
    instrux_IRETW,
    instrux_JCXZ,
    instrux_JECXZ,
    instrux_JRCXZ,
    instrux_JMP,
    instrux_JMPE,
    instrux_LAHF,
    instrux_LAR,
    instrux_LDS,
    instrux_LEA,
    instrux_LEAVE,
    instrux_LES,
    instrux_LFENCE,
    instrux_LFS,
    instrux_LGDT,
    instrux_LGS,
    instrux_LIDT,
    instrux_LLDT,
    instrux_LMSW,
    instrux_LOADALL,
    instrux_LOADALL286,
    instrux_LODSB,
    instrux_LODSD,
    instrux_LODSQ,
    instrux_LODSW,
    instrux_LOOP,
    instrux_LOOPE,
    instrux_LOOPNE,
    instrux_LOOPNZ,
    instrux_LOOPZ,
    instrux_LSL,
    instrux_LSS,
    instrux_LTR,
    instrux_MFENCE,
    instrux_MONITOR,
    instrux_MONITORX,
    instrux_MOV,
    instrux_MOVD,
    instrux_MOVQ,
    instrux_MOVSB,
    instrux_MOVSD,
    instrux_MOVSQ,
    instrux_MOVSW,
    instrux_MOVSX,
    instrux_MOVSXD,
    instrux_MOVZX,
    instrux_MUL,
    instrux_MWAIT,
    instrux_MWAITX,
    instrux_NEG,
    instrux_NOP,
    instrux_NOT,
    instrux_OR,
    instrux_OUT,
    instrux_OUTSB,
    instrux_OUTSD,
    instrux_OUTSW,
    instrux_PACKSSDW,
    instrux_PACKSSWB,
    instrux_PACKUSWB,
    instrux_PADDB,
    instrux_PADDD,
    instrux_PADDSB,
    instrux_PADDSIW,
    instrux_PADDSW,
    instrux_PADDUSB,
    instrux_PADDUSW,
    instrux_PADDW,
    instrux_PAND,
    instrux_PANDN,
    instrux_PAUSE,
    instrux_PAVEB,
    instrux_PAVGUSB,
    instrux_PCMPEQB,
    instrux_PCMPEQD,
    instrux_PCMPEQW,
    instrux_PCMPGTB,
    instrux_PCMPGTD,
    instrux_PCMPGTW,
    instrux_PDISTIB,
    instrux_PF2ID,
    instrux_PFACC,
    instrux_PFADD,
    instrux_PFCMPEQ,
    instrux_PFCMPGE,
    instrux_PFCMPGT,
    instrux_PFMAX,
    instrux_PFMIN,
    instrux_PFMUL,
    instrux_PFRCP,
    instrux_PFRCPIT1,
    instrux_PFRCPIT2,
    instrux_PFRSQIT1,
    instrux_PFRSQRT,
    instrux_PFSUB,
    instrux_PFSUBR,
    instrux_PI2FD,
    instrux_PMACHRIW,
    instrux_PMADDWD,
    instrux_PMAGW,
    instrux_PMULHRIW,
    instrux_PMULHRWA,
    instrux_PMULHRWC,
    instrux_PMULHW,
    instrux_PMULLW,
    instrux_PMVGEZB,
    instrux_PMVLZB,
    instrux_PMVNZB,
    instrux_PMVZB,
    instrux_POP,
    instrux_POPA,
    instrux_POPAD,
    instrux_POPAW,
    instrux_POPF,
    instrux_POPFD,
    instrux_POPFQ,
    instrux_POPFW,
    instrux_POR,
    instrux_PREFETCH,
    instrux_PREFETCHW,
    instrux_PSLLD,
    instrux_PSLLQ,
    instrux_PSLLW,
    instrux_PSRAD,
    instrux_PSRAW,
    instrux_PSRLD,
    instrux_PSRLQ,
    instrux_PSRLW,
    instrux_PSUBB,
    instrux_PSUBD,
    instrux_PSUBSB,
    instrux_PSUBSIW,
    instrux_PSUBSW,
    instrux_PSUBUSB,
    instrux_PSUBUSW,
    instrux_PSUBW,
    instrux_PUNPCKHBW,
    instrux_PUNPCKHDQ,
    instrux_PUNPCKHWD,
    instrux_PUNPCKLBW,
    instrux_PUNPCKLDQ,
    instrux_PUNPCKLWD,
    instrux_PUSH,
    instrux_PUSHA,
    instrux_PUSHAD,
    instrux_PUSHAW,
    instrux_PUSHF,
    instrux_PUSHFD,
    instrux_PUSHFQ,
    instrux_PUSHFW,
    instrux_PXOR,
    instrux_RCL,
    instrux_RCR,
    instrux_RDSHR,
    instrux_RDMSR,
    instrux_RDPMC,
    instrux_RDTSC,
    instrux_RDTSCP,
    instrux_RET,
    instrux_RETF,
    instrux_RETN,
    instrux_RETW,
    instrux_RETFW,
    instrux_RETNW,
    instrux_RETD,
    instrux_RETFD,
    instrux_RETND,
    instrux_RETQ,
    instrux_RETFQ,
    instrux_RETNQ,
    instrux_ROL,
    instrux_ROR,
    instrux_RDM,
    instrux_RSDC,
    instrux_RSLDT,
    instrux_RSM,
    instrux_RSTS,
    instrux_SAHF,
    instrux_SAL,
    instrux_SALC,
    instrux_SAR,
    instrux_SBB,
    instrux_SCASB,
    instrux_SCASD,
    instrux_SCASQ,
    instrux_SCASW,
    instrux_SFENCE,
    instrux_SGDT,
    instrux_SHL,
    instrux_SHLD,
    instrux_SHR,
    instrux_SHRD,
    instrux_SIDT,
    instrux_SLDT,
    instrux_SKINIT,
    instrux_SMI,
    instrux_SMINT,
    instrux_SMINTOLD,
    instrux_SMSW,
    instrux_STC,
    instrux_STD,
    instrux_STI,
    instrux_STOSB,
    instrux_STOSD,
    instrux_STOSQ,
    instrux_STOSW,
    instrux_STR,
    instrux_SUB,
    instrux_SVDC,
    instrux_SVLDT,
    instrux_SVTS,
    instrux_SWAPGS,
    instrux_SYSCALL,
    instrux_SYSENTER,
    instrux_SYSEXIT,
    instrux_SYSRET,
    instrux_TEST,
    instrux_UD0,
    instrux_UD1,
    instrux_UD2B,
    instrux_UD2,
    instrux_UD2A,
    instrux_UMOV,
    instrux_VERR,
    instrux_VERW,
    instrux_FWAIT,
    instrux_WBINVD,
    instrux_WRSHR,
    instrux_WRMSR,
    instrux_XADD,
    instrux_XBTS,
    instrux_XCHG,
    instrux_XLATB,
    instrux_XLAT,
    instrux_XOR,
    instrux_ADDPS,
    instrux_ADDSS,
    instrux_ANDNPS,
    instrux_ANDPS,
    instrux_CMPEQPS,
    instrux_CMPEQSS,
    instrux_CMPLEPS,
    instrux_CMPLESS,
    instrux_CMPLTPS,
    instrux_CMPLTSS,
    instrux_CMPNEQPS,
    instrux_CMPNEQSS,
    instrux_CMPNLEPS,
    instrux_CMPNLESS,
    instrux_CMPNLTPS,
    instrux_CMPNLTSS,
    instrux_CMPORDPS,
    instrux_CMPORDSS,
    instrux_CMPUNORDPS,
    instrux_CMPUNORDSS,
    instrux_CMPPS,
    instrux_CMPSS,
    instrux_COMISS,
    instrux_CVTPI2PS,
    instrux_CVTPS2PI,
    instrux_CVTSI2SS,
    instrux_CVTSS2SI,
    instrux_CVTTPS2PI,
    instrux_CVTTSS2SI,
    instrux_DIVPS,
    instrux_DIVSS,
    instrux_LDMXCSR,
    instrux_MAXPS,
    instrux_MAXSS,
    instrux_MINPS,
    instrux_MINSS,
    instrux_MOVAPS,
    instrux_MOVHPS,
    instrux_MOVLHPS,
    instrux_MOVLPS,
    instrux_MOVHLPS,
    instrux_MOVMSKPS,
    instrux_MOVNTPS,
    instrux_MOVSS,
    instrux_MOVUPS,
    instrux_MULPS,
    instrux_MULSS,
    instrux_ORPS,
    instrux_RCPPS,
    instrux_RCPSS,
    instrux_RSQRTPS,
    instrux_RSQRTSS,
    instrux_SHUFPS,
    instrux_SQRTPS,
    instrux_SQRTSS,
    instrux_STMXCSR,
    instrux_SUBPS,
    instrux_SUBSS,
    instrux_UCOMISS,
    instrux_UNPCKHPS,
    instrux_UNPCKLPS,
    instrux_XORPS,
    instrux_FXRSTOR,
    instrux_FXRSTOR64,
    instrux_FXSAVE,
    instrux_FXSAVE64,
    instrux_XGETBV,
    instrux_XSETBV,
    instrux_XSAVE,
    instrux_XSAVE64,
    instrux_XSAVEC,
    instrux_XSAVEC64,
    instrux_XSAVEOPT,
    instrux_XSAVEOPT64,
    instrux_XSAVES,
    instrux_XSAVES64,
    instrux_XRSTOR,
    instrux_XRSTOR64,
    instrux_XRSTORS,
    instrux_XRSTORS64,
    instrux_PREFETCHNTA,
    instrux_PREFETCHT0,
    instrux_PREFETCHT1,
    instrux_PREFETCHT2,
    instrux_MASKMOVQ,
    instrux_MOVNTQ,
    instrux_PAVGB,
    instrux_PAVGW,
    instrux_PEXTRW,
    instrux_PINSRW,
    instrux_PMAXSW,
    instrux_PMAXUB,
    instrux_PMINSW,
    instrux_PMINUB,
    instrux_PMOVMSKB,
    instrux_PMULHUW,
    instrux_PSADBW,
    instrux_PSHUFW,
    instrux_PF2IW,
    instrux_PFNACC,
    instrux_PFPNACC,
    instrux_PI2FW,
    instrux_PSWAPD,
    instrux_MASKMOVDQU,
    instrux_CLFLUSH,
    instrux_MOVNTDQ,
    instrux_MOVNTI,
    instrux_MOVNTPD,
    instrux_MOVDQA,
    instrux_MOVDQU,
    instrux_MOVDQ2Q,
    instrux_MOVQ2DQ,
    instrux_PADDQ,
    instrux_PMULUDQ,
    instrux_PSHUFD,
    instrux_PSHUFHW,
    instrux_PSHUFLW,
    instrux_PSLLDQ,
    instrux_PSRLDQ,
    instrux_PSUBQ,
    instrux_PUNPCKHQDQ,
    instrux_PUNPCKLQDQ,
    instrux_ADDPD,
    instrux_ADDSD,
    instrux_ANDNPD,
    instrux_ANDPD,
    instrux_CMPEQPD,
    instrux_CMPEQSD,
    instrux_CMPLEPD,
    instrux_CMPLESD,
    instrux_CMPLTPD,
    instrux_CMPLTSD,
    instrux_CMPNEQPD,
    instrux_CMPNEQSD,
    instrux_CMPNLEPD,
    instrux_CMPNLESD,
    instrux_CMPNLTPD,
    instrux_CMPNLTSD,
    instrux_CMPORDPD,
    instrux_CMPORDSD,
    instrux_CMPUNORDPD,
    instrux_CMPUNORDSD,
    instrux_CMPPD,
    instrux_COMISD,
    instrux_CVTDQ2PD,
    instrux_CVTDQ2PS,
    instrux_CVTPD2DQ,
    instrux_CVTPD2PI,
    instrux_CVTPD2PS,
    instrux_CVTPI2PD,
    instrux_CVTPS2DQ,
    instrux_CVTPS2PD,
    instrux_CVTSD2SI,
    instrux_CVTSD2SS,
    instrux_CVTSI2SD,
    instrux_CVTSS2SD,
    instrux_CVTTPD2PI,
    instrux_CVTTPD2DQ,
    instrux_CVTTPS2DQ,
    instrux_CVTTSD2SI,
    instrux_DIVPD,
    instrux_DIVSD,
    instrux_MAXPD,
    instrux_MAXSD,
    instrux_MINPD,
    instrux_MINSD,
    instrux_MOVAPD,
    instrux_MOVHPD,
    instrux_MOVLPD,
    instrux_MOVMSKPD,
    instrux_MOVUPD,
    instrux_MULPD,
    instrux_MULSD,
    instrux_ORPD,
    instrux_SHUFPD,
    instrux_SQRTPD,
    instrux_SQRTSD,
    instrux_SUBPD,
    instrux_SUBSD,
    instrux_UCOMISD,
    instrux_UNPCKHPD,
    instrux_UNPCKLPD,
    instrux_XORPD,
    instrux_ADDSUBPD,
    instrux_ADDSUBPS,
    instrux_HADDPD,
    instrux_HADDPS,
    instrux_HSUBPD,
    instrux_HSUBPS,
    instrux_LDDQU,
    instrux_MOVDDUP,
    instrux_MOVSHDUP,
    instrux_MOVSLDUP,
    instrux_CLGI,
    instrux_STGI,
    instrux_VMCALL,
    instrux_VMCLEAR,
    instrux_VMFUNC,
    instrux_VMLAUNCH,
    instrux_VMLOAD,
    instrux_VMMCALL,
    instrux_VMPTRLD,
    instrux_VMPTRST,
    instrux_VMREAD,
    instrux_VMRESUME,
    instrux_VMRUN,
    instrux_VMSAVE,
    instrux_VMWRITE,
    instrux_VMXOFF,
    instrux_VMXON,
    instrux_INVEPT,
    instrux_INVVPID,
    instrux_PABSB,
    instrux_PABSW,
    instrux_PABSD,
    instrux_PALIGNR,
    instrux_PHADDW,
    instrux_PHADDD,
    instrux_PHADDSW,
    instrux_PHSUBW,
    instrux_PHSUBD,
    instrux_PHSUBSW,
    instrux_PMADDUBSW,
    instrux_PMULHRSW,
    instrux_PSHUFB,
    instrux_PSIGNB,
    instrux_PSIGNW,
    instrux_PSIGND,
    instrux_EXTRQ,
    instrux_INSERTQ,
    instrux_MOVNTSD,
    instrux_MOVNTSS,
    instrux_LZCNT,
    instrux_BLENDPD,
    instrux_BLENDPS,
    instrux_BLENDVPD,
    instrux_BLENDVPS,
    instrux_DPPD,
    instrux_DPPS,
    instrux_EXTRACTPS,
    instrux_INSERTPS,
    instrux_MOVNTDQA,
    instrux_MPSADBW,
    instrux_PACKUSDW,
    instrux_PBLENDVB,
    instrux_PBLENDW,
    instrux_PCMPEQQ,
    instrux_PEXTRB,
    instrux_PEXTRD,
    instrux_PEXTRQ,
    instrux_PHMINPOSUW,
    instrux_PINSRB,
    instrux_PINSRD,
    instrux_PINSRQ,
    instrux_PMAXSB,
    instrux_PMAXSD,
    instrux_PMAXUD,
    instrux_PMAXUW,
    instrux_PMINSB,
    instrux_PMINSD,
    instrux_PMINUD,
    instrux_PMINUW,
    instrux_PMOVSXBW,
    instrux_PMOVSXBD,
    instrux_PMOVSXBQ,
    instrux_PMOVSXWD,
    instrux_PMOVSXWQ,
    instrux_PMOVSXDQ,
    instrux_PMOVZXBW,
    instrux_PMOVZXBD,
    instrux_PMOVZXBQ,
    instrux_PMOVZXWD,
    instrux_PMOVZXWQ,
    instrux_PMOVZXDQ,
    instrux_PMULDQ,
    instrux_PMULLD,
    instrux_PTEST,
    instrux_ROUNDPD,
    instrux_ROUNDPS,
    instrux_ROUNDSD,
    instrux_ROUNDSS,
    instrux_CRC32,
    instrux_PCMPESTRI,
    instrux_PCMPESTRM,
    instrux_PCMPISTRI,
    instrux_PCMPISTRM,
    instrux_PCMPGTQ,
    instrux_POPCNT,
    instrux_GETSEC,
    instrux_PFRCPV,
    instrux_PFRSQRTV,
    instrux_MOVBE,
    instrux_AESENC,
    instrux_AESENCLAST,
    instrux_AESDEC,
    instrux_AESDECLAST,
    instrux_AESIMC,
    instrux_AESKEYGENASSIST,
    instrux_VAESENC,
    instrux_VAESENCLAST,
    instrux_VAESDEC,
    instrux_VAESDECLAST,
    instrux_VAESIMC,
    instrux_VAESKEYGENASSIST,
    instrux_VADDPD,
    instrux_VADDPS,
    instrux_VADDSD,
    instrux_VADDSS,
    instrux_VADDSUBPD,
    instrux_VADDSUBPS,
    instrux_VANDPD,
    instrux_VANDPS,
    instrux_VANDNPD,
    instrux_VANDNPS,
    instrux_VBLENDPD,
    instrux_VBLENDPS,
    instrux_VBLENDVPD,
    instrux_VBLENDVPS,
    instrux_VBROADCASTSS,
    instrux_VBROADCASTSD,
    instrux_VBROADCASTF128,
    instrux_VCMPEQ_OSPD,
    instrux_VCMPEQPD,
    instrux_VCMPLT_OSPD,
    instrux_VCMPLTPD,
    instrux_VCMPLE_OSPD,
    instrux_VCMPLEPD,
    instrux_VCMPUNORD_QPD,
    instrux_VCMPUNORDPD,
    instrux_VCMPNEQ_UQPD,
    instrux_VCMPNEQPD,
    instrux_VCMPNLT_USPD,
    instrux_VCMPNLTPD,
    instrux_VCMPNLE_USPD,
    instrux_VCMPNLEPD,
    instrux_VCMPORD_QPD,
    instrux_VCMPORDPD,
    instrux_VCMPEQ_UQPD,
    instrux_VCMPNGE_USPD,
    instrux_VCMPNGEPD,
    instrux_VCMPNGT_USPD,
    instrux_VCMPNGTPD,
    instrux_VCMPFALSE_OQPD,
    instrux_VCMPFALSEPD,
    instrux_VCMPNEQ_OQPD,
    instrux_VCMPGE_OSPD,
    instrux_VCMPGEPD,
    instrux_VCMPGT_OSPD,
    instrux_VCMPGTPD,
    instrux_VCMPTRUE_UQPD,
    instrux_VCMPTRUEPD,
    instrux_VCMPLT_OQPD,
    instrux_VCMPLE_OQPD,
    instrux_VCMPUNORD_SPD,
    instrux_VCMPNEQ_USPD,
    instrux_VCMPNLT_UQPD,
    instrux_VCMPNLE_UQPD,
    instrux_VCMPORD_SPD,
    instrux_VCMPEQ_USPD,
    instrux_VCMPNGE_UQPD,
    instrux_VCMPNGT_UQPD,
    instrux_VCMPFALSE_OSPD,
    instrux_VCMPNEQ_OSPD,
    instrux_VCMPGE_OQPD,
    instrux_VCMPGT_OQPD,
    instrux_VCMPTRUE_USPD,
    instrux_VCMPPD,
    instrux_VCMPEQ_OSPS,
    instrux_VCMPEQPS,
    instrux_VCMPLT_OSPS,
    instrux_VCMPLTPS,
    instrux_VCMPLE_OSPS,
    instrux_VCMPLEPS,
    instrux_VCMPUNORD_QPS,
    instrux_VCMPUNORDPS,
    instrux_VCMPNEQ_UQPS,
    instrux_VCMPNEQPS,
    instrux_VCMPNLT_USPS,
    instrux_VCMPNLTPS,
    instrux_VCMPNLE_USPS,
    instrux_VCMPNLEPS,
    instrux_VCMPORD_QPS,
    instrux_VCMPORDPS,
    instrux_VCMPEQ_UQPS,
    instrux_VCMPNGE_USPS,
    instrux_VCMPNGEPS,
    instrux_VCMPNGT_USPS,
    instrux_VCMPNGTPS,
    instrux_VCMPFALSE_OQPS,
    instrux_VCMPFALSEPS,
    instrux_VCMPNEQ_OQPS,
    instrux_VCMPGE_OSPS,
    instrux_VCMPGEPS,
    instrux_VCMPGT_OSPS,
    instrux_VCMPGTPS,
    instrux_VCMPTRUE_UQPS,
    instrux_VCMPTRUEPS,
    instrux_VCMPLT_OQPS,
    instrux_VCMPLE_OQPS,
    instrux_VCMPUNORD_SPS,
    instrux_VCMPNEQ_USPS,
    instrux_VCMPNLT_UQPS,
    instrux_VCMPNLE_UQPS,
    instrux_VCMPORD_SPS,
    instrux_VCMPEQ_USPS,
    instrux_VCMPNGE_UQPS,
    instrux_VCMPNGT_UQPS,
    instrux_VCMPFALSE_OSPS,
    instrux_VCMPNEQ_OSPS,
    instrux_VCMPGE_OQPS,
    instrux_VCMPGT_OQPS,
    instrux_VCMPTRUE_USPS,
    instrux_VCMPPS,
    instrux_VCMPEQ_OSSD,
    instrux_VCMPEQSD,
    instrux_VCMPLT_OSSD,
    instrux_VCMPLTSD,
    instrux_VCMPLE_OSSD,
    instrux_VCMPLESD,
    instrux_VCMPUNORD_QSD,
    instrux_VCMPUNORDSD,
    instrux_VCMPNEQ_UQSD,
    instrux_VCMPNEQSD,
    instrux_VCMPNLT_USSD,
    instrux_VCMPNLTSD,
    instrux_VCMPNLE_USSD,
    instrux_VCMPNLESD,
    instrux_VCMPORD_QSD,
    instrux_VCMPORDSD,
    instrux_VCMPEQ_UQSD,
    instrux_VCMPNGE_USSD,
    instrux_VCMPNGESD,
    instrux_VCMPNGT_USSD,
    instrux_VCMPNGTSD,
    instrux_VCMPFALSE_OQSD,
    instrux_VCMPFALSESD,
    instrux_VCMPNEQ_OQSD,
    instrux_VCMPGE_OSSD,
    instrux_VCMPGESD,
    instrux_VCMPGT_OSSD,
    instrux_VCMPGTSD,
    instrux_VCMPTRUE_UQSD,
    instrux_VCMPTRUESD,
    instrux_VCMPLT_OQSD,
    instrux_VCMPLE_OQSD,
    instrux_VCMPUNORD_SSD,
    instrux_VCMPNEQ_USSD,
    instrux_VCMPNLT_UQSD,
    instrux_VCMPNLE_UQSD,
    instrux_VCMPORD_SSD,
    instrux_VCMPEQ_USSD,
    instrux_VCMPNGE_UQSD,
    instrux_VCMPNGT_UQSD,
    instrux_VCMPFALSE_OSSD,
    instrux_VCMPNEQ_OSSD,
    instrux_VCMPGE_OQSD,
    instrux_VCMPGT_OQSD,
    instrux_VCMPTRUE_USSD,
    instrux_VCMPSD,
    instrux_VCMPEQ_OSSS,
    instrux_VCMPEQSS,
    instrux_VCMPLT_OSSS,
    instrux_VCMPLTSS,
    instrux_VCMPLE_OSSS,
    instrux_VCMPLESS,
    instrux_VCMPUNORD_QSS,
    instrux_VCMPUNORDSS,
    instrux_VCMPNEQ_UQSS,
    instrux_VCMPNEQSS,
    instrux_VCMPNLT_USSS,
    instrux_VCMPNLTSS,
    instrux_VCMPNLE_USSS,
    instrux_VCMPNLESS,
    instrux_VCMPORD_QSS,
    instrux_VCMPORDSS,
    instrux_VCMPEQ_UQSS,
    instrux_VCMPNGE_USSS,
    instrux_VCMPNGESS,
    instrux_VCMPNGT_USSS,
    instrux_VCMPNGTSS,
    instrux_VCMPFALSE_OQSS,
    instrux_VCMPFALSESS,
    instrux_VCMPNEQ_OQSS,
    instrux_VCMPGE_OSSS,
    instrux_VCMPGESS,
    instrux_VCMPGT_OSSS,
    instrux_VCMPGTSS,
    instrux_VCMPTRUE_UQSS,
    instrux_VCMPTRUESS,
    instrux_VCMPLT_OQSS,
    instrux_VCMPLE_OQSS,
    instrux_VCMPUNORD_SSS,
    instrux_VCMPNEQ_USSS,
    instrux_VCMPNLT_UQSS,
    instrux_VCMPNLE_UQSS,
    instrux_VCMPORD_SSS,
    instrux_VCMPEQ_USSS,
    instrux_VCMPNGE_UQSS,
    instrux_VCMPNGT_UQSS,
    instrux_VCMPFALSE_OSSS,
    instrux_VCMPNEQ_OSSS,
    instrux_VCMPGE_OQSS,
    instrux_VCMPGT_OQSS,
    instrux_VCMPTRUE_USSS,
    instrux_VCMPSS,
    instrux_VCOMISD,
    instrux_VCOMISS,
    instrux_VCVTDQ2PD,
    instrux_VCVTDQ2PS,
    instrux_VCVTPD2DQ,
    instrux_VCVTPD2PS,
    instrux_VCVTPS2DQ,
    instrux_VCVTPS2PD,
    instrux_VCVTSD2SI,
    instrux_VCVTSD2SS,
    instrux_VCVTSI2SD,
    instrux_VCVTSI2SS,
    instrux_VCVTSS2SD,
    instrux_VCVTSS2SI,
    instrux_VCVTTPD2DQ,
    instrux_VCVTTPS2DQ,
    instrux_VCVTTSD2SI,
    instrux_VCVTTSS2SI,
    instrux_VDIVPD,
    instrux_VDIVPS,
    instrux_VDIVSD,
    instrux_VDIVSS,
    instrux_VDPPD,
    instrux_VDPPS,
    instrux_VEXTRACTF128,
    instrux_VEXTRACTPS,
    instrux_VHADDPD,
    instrux_VHADDPS,
    instrux_VHSUBPD,
    instrux_VHSUBPS,
    instrux_VINSERTF128,
    instrux_VINSERTPS,
    instrux_VLDDQU,
    instrux_VLDQQU,
    instrux_VLDMXCSR,
    instrux_VMASKMOVDQU,
    instrux_VMASKMOVPS,
    instrux_VMASKMOVPD,
    instrux_VMAXPD,
    instrux_VMAXPS,
    instrux_VMAXSD,
    instrux_VMAXSS,
    instrux_VMINPD,
    instrux_VMINPS,
    instrux_VMINSD,
    instrux_VMINSS,
    instrux_VMOVAPD,
    instrux_VMOVAPS,
    instrux_VMOVD,
    instrux_VMOVQ,
    instrux_VMOVDDUP,
    instrux_VMOVDQA,
    instrux_VMOVQQA,
    instrux_VMOVDQU,
    instrux_VMOVQQU,
    instrux_VMOVHLPS,
    instrux_VMOVHPD,
    instrux_VMOVHPS,
    instrux_VMOVLHPS,
    instrux_VMOVLPD,
    instrux_VMOVLPS,
    instrux_VMOVMSKPD,
    instrux_VMOVMSKPS,
    instrux_VMOVNTDQ,
    instrux_VMOVNTQQ,
    instrux_VMOVNTDQA,
    instrux_VMOVNTPD,
    instrux_VMOVNTPS,
    instrux_VMOVSD,
    instrux_VMOVSHDUP,
    instrux_VMOVSLDUP,
    instrux_VMOVSS,
    instrux_VMOVUPD,
    instrux_VMOVUPS,
    instrux_VMPSADBW,
    instrux_VMULPD,
    instrux_VMULPS,
    instrux_VMULSD,
    instrux_VMULSS,
    instrux_VORPD,
    instrux_VORPS,
    instrux_VPABSB,
    instrux_VPABSW,
    instrux_VPABSD,
    instrux_VPACKSSWB,
    instrux_VPACKSSDW,
    instrux_VPACKUSWB,
    instrux_VPACKUSDW,
    instrux_VPADDB,
    instrux_VPADDW,
    instrux_VPADDD,
    instrux_VPADDQ,
    instrux_VPADDSB,
    instrux_VPADDSW,
    instrux_VPADDUSB,
    instrux_VPADDUSW,
    instrux_VPALIGNR,
    instrux_VPAND,
    instrux_VPANDN,
    instrux_VPAVGB,
    instrux_VPAVGW,
    instrux_VPBLENDVB,
    instrux_VPBLENDW,
    instrux_VPCMPESTRI,
    instrux_VPCMPESTRM,
    instrux_VPCMPISTRI,
    instrux_VPCMPISTRM,
    instrux_VPCMPEQB,
    instrux_VPCMPEQW,
    instrux_VPCMPEQD,
    instrux_VPCMPEQQ,
    instrux_VPCMPGTB,
    instrux_VPCMPGTW,
    instrux_VPCMPGTD,
    instrux_VPCMPGTQ,
    instrux_VPERMILPD,
    instrux_VPERMILPS,
    instrux_VPERM2F128,
    instrux_VPEXTRB,
    instrux_VPEXTRW,
    instrux_VPEXTRD,
    instrux_VPEXTRQ,
    instrux_VPHADDW,
    instrux_VPHADDD,
    instrux_VPHADDSW,
    instrux_VPHMINPOSUW,
    instrux_VPHSUBW,
    instrux_VPHSUBD,
    instrux_VPHSUBSW,
    instrux_VPINSRB,
    instrux_VPINSRW,
    instrux_VPINSRD,
    instrux_VPINSRQ,
    instrux_VPMADDWD,
    instrux_VPMADDUBSW,
    instrux_VPMAXSB,
    instrux_VPMAXSW,
    instrux_VPMAXSD,
    instrux_VPMAXUB,
    instrux_VPMAXUW,
    instrux_VPMAXUD,
    instrux_VPMINSB,
    instrux_VPMINSW,
    instrux_VPMINSD,
    instrux_VPMINUB,
    instrux_VPMINUW,
    instrux_VPMINUD,
    instrux_VPMOVMSKB,
    instrux_VPMOVSXBW,
    instrux_VPMOVSXBD,
    instrux_VPMOVSXBQ,
    instrux_VPMOVSXWD,
    instrux_VPMOVSXWQ,
    instrux_VPMOVSXDQ,
    instrux_VPMOVZXBW,
    instrux_VPMOVZXBD,
    instrux_VPMOVZXBQ,
    instrux_VPMOVZXWD,
    instrux_VPMOVZXWQ,
    instrux_VPMOVZXDQ,
    instrux_VPMULHUW,
    instrux_VPMULHRSW,
    instrux_VPMULHW,
    instrux_VPMULLW,
    instrux_VPMULLD,
    instrux_VPMULUDQ,
    instrux_VPMULDQ,
    instrux_VPOR,
    instrux_VPSADBW,
    instrux_VPSHUFB,
    instrux_VPSHUFD,
    instrux_VPSHUFHW,
    instrux_VPSHUFLW,
    instrux_VPSIGNB,
    instrux_VPSIGNW,
    instrux_VPSIGND,
    instrux_VPSLLDQ,
    instrux_VPSRLDQ,
    instrux_VPSLLW,
    instrux_VPSLLD,
    instrux_VPSLLQ,
    instrux_VPSRAW,
    instrux_VPSRAD,
    instrux_VPSRLW,
    instrux_VPSRLD,
    instrux_VPSRLQ,
    instrux_VPTEST,
    instrux_VPSUBB,
    instrux_VPSUBW,
    instrux_VPSUBD,
    instrux_VPSUBQ,
    instrux_VPSUBSB,
    instrux_VPSUBSW,
    instrux_VPSUBUSB,
    instrux_VPSUBUSW,
    instrux_VPUNPCKHBW,
    instrux_VPUNPCKHWD,
    instrux_VPUNPCKHDQ,
    instrux_VPUNPCKHQDQ,
    instrux_VPUNPCKLBW,
    instrux_VPUNPCKLWD,
    instrux_VPUNPCKLDQ,
    instrux_VPUNPCKLQDQ,
    instrux_VPXOR,
    instrux_VRCPPS,
    instrux_VRCPSS,
    instrux_VRSQRTPS,
    instrux_VRSQRTSS,
    instrux_VROUNDPD,
    instrux_VROUNDPS,
    instrux_VROUNDSD,
    instrux_VROUNDSS,
    instrux_VSHUFPD,
    instrux_VSHUFPS,
    instrux_VSQRTPD,
    instrux_VSQRTPS,
    instrux_VSQRTSD,
    instrux_VSQRTSS,
    instrux_VSTMXCSR,
    instrux_VSUBPD,
    instrux_VSUBPS,
    instrux_VSUBSD,
    instrux_VSUBSS,
    instrux_VTESTPS,
    instrux_VTESTPD,
    instrux_VUCOMISD,
    instrux_VUCOMISS,
    instrux_VUNPCKHPD,
    instrux_VUNPCKHPS,
    instrux_VUNPCKLPD,
    instrux_VUNPCKLPS,
    instrux_VXORPD,
    instrux_VXORPS,
    instrux_VZEROALL,
    instrux_VZEROUPPER,
    instrux_PCLMULLQLQDQ,
    instrux_PCLMULHQLQDQ,
    instrux_PCLMULLQHQDQ,
    instrux_PCLMULHQHQDQ,
    instrux_PCLMULQDQ,
    instrux_VPCLMULLQLQDQ,
    instrux_VPCLMULHQLQDQ,
    instrux_VPCLMULLQHQDQ,
    instrux_VPCLMULHQHQDQ,
    instrux_VPCLMULQDQ,
    instrux_VFMADD132PS,
    instrux_VFMADD132PD,
    instrux_VFMADD312PS,
    instrux_VFMADD312PD,
    instrux_VFMADD213PS,
    instrux_VFMADD213PD,
    instrux_VFMADD123PS,
    instrux_VFMADD123PD,
    instrux_VFMADD231PS,
    instrux_VFMADD231PD,
    instrux_VFMADD321PS,
    instrux_VFMADD321PD,
    instrux_VFMADDSUB132PS,
    instrux_VFMADDSUB132PD,
    instrux_VFMADDSUB312PS,
    instrux_VFMADDSUB312PD,
    instrux_VFMADDSUB213PS,
    instrux_VFMADDSUB213PD,
    instrux_VFMADDSUB123PS,
    instrux_VFMADDSUB123PD,
    instrux_VFMADDSUB231PS,
    instrux_VFMADDSUB231PD,
    instrux_VFMADDSUB321PS,
    instrux_VFMADDSUB321PD,
    instrux_VFMSUB132PS,
    instrux_VFMSUB132PD,
    instrux_VFMSUB312PS,
    instrux_VFMSUB312PD,
    instrux_VFMSUB213PS,
    instrux_VFMSUB213PD,
    instrux_VFMSUB123PS,
    instrux_VFMSUB123PD,
    instrux_VFMSUB231PS,
    instrux_VFMSUB231PD,
    instrux_VFMSUB321PS,
    instrux_VFMSUB321PD,
    instrux_VFMSUBADD132PS,
    instrux_VFMSUBADD132PD,
    instrux_VFMSUBADD312PS,
    instrux_VFMSUBADD312PD,
    instrux_VFMSUBADD213PS,
    instrux_VFMSUBADD213PD,
    instrux_VFMSUBADD123PS,
    instrux_VFMSUBADD123PD,
    instrux_VFMSUBADD231PS,
    instrux_VFMSUBADD231PD,
    instrux_VFMSUBADD321PS,
    instrux_VFMSUBADD321PD,
    instrux_VFNMADD132PS,
    instrux_VFNMADD132PD,
    instrux_VFNMADD312PS,
    instrux_VFNMADD312PD,
    instrux_VFNMADD213PS,
    instrux_VFNMADD213PD,
    instrux_VFNMADD123PS,
    instrux_VFNMADD123PD,
    instrux_VFNMADD231PS,
    instrux_VFNMADD231PD,
    instrux_VFNMADD321PS,
    instrux_VFNMADD321PD,
    instrux_VFNMSUB132PS,
    instrux_VFNMSUB132PD,
    instrux_VFNMSUB312PS,
    instrux_VFNMSUB312PD,
    instrux_VFNMSUB213PS,
    instrux_VFNMSUB213PD,
    instrux_VFNMSUB123PS,
    instrux_VFNMSUB123PD,
    instrux_VFNMSUB231PS,
    instrux_VFNMSUB231PD,
    instrux_VFNMSUB321PS,
    instrux_VFNMSUB321PD,
    instrux_VFMADD132SS,
    instrux_VFMADD132SD,
    instrux_VFMADD312SS,
    instrux_VFMADD312SD,
    instrux_VFMADD213SS,
    instrux_VFMADD213SD,
    instrux_VFMADD123SS,
    instrux_VFMADD123SD,
    instrux_VFMADD231SS,
    instrux_VFMADD231SD,
    instrux_VFMADD321SS,
    instrux_VFMADD321SD,
    instrux_VFMSUB132SS,
    instrux_VFMSUB132SD,
    instrux_VFMSUB312SS,
    instrux_VFMSUB312SD,
    instrux_VFMSUB213SS,
    instrux_VFMSUB213SD,
    instrux_VFMSUB123SS,
    instrux_VFMSUB123SD,
    instrux_VFMSUB231SS,
    instrux_VFMSUB231SD,
    instrux_VFMSUB321SS,
    instrux_VFMSUB321SD,
    instrux_VFNMADD132SS,
    instrux_VFNMADD132SD,
    instrux_VFNMADD312SS,
    instrux_VFNMADD312SD,
    instrux_VFNMADD213SS,
    instrux_VFNMADD213SD,
    instrux_VFNMADD123SS,
    instrux_VFNMADD123SD,
    instrux_VFNMADD231SS,
    instrux_VFNMADD231SD,
    instrux_VFNMADD321SS,
    instrux_VFNMADD321SD,
    instrux_VFNMSUB132SS,
    instrux_VFNMSUB132SD,
    instrux_VFNMSUB312SS,
    instrux_VFNMSUB312SD,
    instrux_VFNMSUB213SS,
    instrux_VFNMSUB213SD,
    instrux_VFNMSUB123SS,
    instrux_VFNMSUB123SD,
    instrux_VFNMSUB231SS,
    instrux_VFNMSUB231SD,
    instrux_VFNMSUB321SS,
    instrux_VFNMSUB321SD,
    instrux_RDFSBASE,
    instrux_RDGSBASE,
    instrux_RDRAND,
    instrux_WRFSBASE,
    instrux_WRGSBASE,
    instrux_VCVTPH2PS,
    instrux_VCVTPS2PH,
    instrux_ADCX,
    instrux_ADOX,
    instrux_RDSEED,
    instrux_CLAC,
    instrux_STAC,
    instrux_XSTORE,
    instrux_XCRYPTECB,
    instrux_XCRYPTCBC,
    instrux_XCRYPTCTR,
    instrux_XCRYPTCFB,
    instrux_XCRYPTOFB,
    instrux_MONTMUL,
    instrux_XSHA1,
    instrux_XSHA256,
    instrux_LLWPCB,
    instrux_SLWPCB,
    instrux_LWPVAL,
    instrux_LWPINS,
    instrux_VFMADDPD,
    instrux_VFMADDPS,
    instrux_VFMADDSD,
    instrux_VFMADDSS,
    instrux_VFMADDSUBPD,
    instrux_VFMADDSUBPS,
    instrux_VFMSUBADDPD,
    instrux_VFMSUBADDPS,
    instrux_VFMSUBPD,
    instrux_VFMSUBPS,
    instrux_VFMSUBSD,
    instrux_VFMSUBSS,
    instrux_VFNMADDPD,
    instrux_VFNMADDPS,
    instrux_VFNMADDSD,
    instrux_VFNMADDSS,
    instrux_VFNMSUBPD,
    instrux_VFNMSUBPS,
    instrux_VFNMSUBSD,
    instrux_VFNMSUBSS,
    instrux_VFRCZPD,
    instrux_VFRCZPS,
    instrux_VFRCZSD,
    instrux_VFRCZSS,
    instrux_VPCMOV,
    instrux_VPCOMB,
    instrux_VPCOMD,
    instrux_VPCOMQ,
    instrux_VPCOMUB,
    instrux_VPCOMUD,
    instrux_VPCOMUQ,
    instrux_VPCOMUW,
    instrux_VPCOMW,
    instrux_VPHADDBD,
    instrux_VPHADDBQ,
    instrux_VPHADDBW,
    instrux_VPHADDDQ,
    instrux_VPHADDUBD,
    instrux_VPHADDUBQ,
    instrux_VPHADDUBW,
    instrux_VPHADDUDQ,
    instrux_VPHADDUWD,
    instrux_VPHADDUWQ,
    instrux_VPHADDWD,
    instrux_VPHADDWQ,
    instrux_VPHSUBBW,
    instrux_VPHSUBDQ,
    instrux_VPHSUBWD,
    instrux_VPMACSDD,
    instrux_VPMACSDQH,
    instrux_VPMACSDQL,
    instrux_VPMACSSDD,
    instrux_VPMACSSDQH,
    instrux_VPMACSSDQL,
    instrux_VPMACSSWD,
    instrux_VPMACSSWW,
    instrux_VPMACSWD,
    instrux_VPMACSWW,
    instrux_VPMADCSSWD,
    instrux_VPMADCSWD,
    instrux_VPPERM,
    instrux_VPROTB,
    instrux_VPROTD,
    instrux_VPROTQ,
    instrux_VPROTW,
    instrux_VPSHAB,
    instrux_VPSHAD,
    instrux_VPSHAQ,
    instrux_VPSHAW,
    instrux_VPSHLB,
    instrux_VPSHLD,
    instrux_VPSHLQ,
    instrux_VPSHLW,
    instrux_VBROADCASTI128,
    instrux_VPBLENDD,
    instrux_VPBROADCASTB,
    instrux_VPBROADCASTW,
    instrux_VPBROADCASTD,
    instrux_VPBROADCASTQ,
    instrux_VPERMD,
    instrux_VPERMPD,
    instrux_VPERMPS,
    instrux_VPERMQ,
    instrux_VPERM2I128,
    instrux_VEXTRACTI128,
    instrux_VINSERTI128,
    instrux_VPMASKMOVD,
    instrux_VPMASKMOVQ,
    instrux_VPSLLVD,
    instrux_VPSLLVQ,
    instrux_VPSRAVD,
    instrux_VPSRLVD,
    instrux_VPSRLVQ,
    instrux_VGATHERDPD,
    instrux_VGATHERQPD,
    instrux_VGATHERDPS,
    instrux_VGATHERQPS,
    instrux_VPGATHERDD,
    instrux_VPGATHERQD,
    instrux_VPGATHERDQ,
    instrux_VPGATHERQQ,
    instrux_XABORT,
    instrux_XBEGIN,
    instrux_XEND,
    instrux_XTEST,
    instrux_ANDN,
    instrux_BEXTR,
    instrux_BLCI,
    instrux_BLCIC,
    instrux_BLSI,
    instrux_BLSIC,
    instrux_BLCFILL,
    instrux_BLSFILL,
    instrux_BLCMSK,
    instrux_BLSMSK,
    instrux_BLSR,
    instrux_BLCS,
    instrux_BZHI,
    instrux_MULX,
    instrux_PDEP,
    instrux_PEXT,
    instrux_RORX,
    instrux_SARX,
    instrux_SHLX,
    instrux_SHRX,
    instrux_TZCNT,
    instrux_TZMSK,
    instrux_T1MSKC,
    instrux_PREFETCHWT1,
    instrux_BNDMK,
    instrux_BNDCL,
    instrux_BNDCU,
    instrux_BNDCN,
    instrux_BNDMOV,
    instrux_BNDLDX,
    instrux_BNDSTX,
    instrux_SHA1MSG1,
    instrux_SHA1MSG2,
    instrux_SHA1NEXTE,
    instrux_SHA1RNDS4,
    instrux_SHA256MSG1,
    instrux_SHA256MSG2,
    instrux_SHA256RNDS2,
    instrux_KADDB,
    instrux_KADDD,
    instrux_KADDQ,
    instrux_KADDW,
    instrux_KANDB,
    instrux_KANDD,
    instrux_KANDNB,
    instrux_KANDND,
    instrux_KANDNQ,
    instrux_KANDNW,
    instrux_KANDQ,
    instrux_KANDW,
    instrux_KMOVB,
    instrux_KMOVD,
    instrux_KMOVQ,
    instrux_KMOVW,
    instrux_KNOTB,
    instrux_KNOTD,
    instrux_KNOTQ,
    instrux_KNOTW,
    instrux_KORB,
    instrux_KORD,
    instrux_KORQ,
    instrux_KORTESTB,
    instrux_KORTESTD,
    instrux_KORTESTQ,
    instrux_KORTESTW,
    instrux_KORW,
    instrux_KSHIFTLB,
    instrux_KSHIFTLD,
    instrux_KSHIFTLQ,
    instrux_KSHIFTLW,
    instrux_KSHIFTRB,
    instrux_KSHIFTRD,
    instrux_KSHIFTRQ,
    instrux_KSHIFTRW,
    instrux_KTESTB,
    instrux_KTESTD,
    instrux_KTESTQ,
    instrux_KTESTW,
    instrux_KUNPCKBW,
    instrux_KUNPCKDQ,
    instrux_KUNPCKWD,
    instrux_KXNORB,
    instrux_KXNORD,
    instrux_KXNORQ,
    instrux_KXNORW,
    instrux_KXORB,
    instrux_KXORD,
    instrux_KXORQ,
    instrux_KXORW,
    instrux_VALIGND,
    instrux_VALIGNQ,
    instrux_VBLENDMPD,
    instrux_VBLENDMPS,
    instrux_VBROADCASTF32X2,
    instrux_VBROADCASTF32X4,
    instrux_VBROADCASTF32X8,
    instrux_VBROADCASTF64X2,
    instrux_VBROADCASTF64X4,
    instrux_VBROADCASTI32X2,
    instrux_VBROADCASTI32X4,
    instrux_VBROADCASTI32X8,
    instrux_VBROADCASTI64X2,
    instrux_VBROADCASTI64X4,
    instrux_VCOMPRESSPD,
    instrux_VCOMPRESSPS,
    instrux_VCVTPD2QQ,
    instrux_VCVTPD2UDQ,
    instrux_VCVTPD2UQQ,
    instrux_VCVTPS2QQ,
    instrux_VCVTPS2UDQ,
    instrux_VCVTPS2UQQ,
    instrux_VCVTQQ2PD,
    instrux_VCVTQQ2PS,
    instrux_VCVTSD2USI,
    instrux_VCVTSS2USI,
    instrux_VCVTTPD2QQ,
    instrux_VCVTTPD2UDQ,
    instrux_VCVTTPD2UQQ,
    instrux_VCVTTPS2QQ,
    instrux_VCVTTPS2UDQ,
    instrux_VCVTTPS2UQQ,
    instrux_VCVTTSD2USI,
    instrux_VCVTTSS2USI,
    instrux_VCVTUDQ2PD,
    instrux_VCVTUDQ2PS,
    instrux_VCVTUQQ2PD,
    instrux_VCVTUQQ2PS,
    instrux_VCVTUSI2SD,
    instrux_VCVTUSI2SS,
    instrux_VDBPSADBW,
    instrux_VEXP2PD,
    instrux_VEXP2PS,
    instrux_VEXPANDPD,
    instrux_VEXPANDPS,
    instrux_VEXTRACTF32X4,
    instrux_VEXTRACTF32X8,
    instrux_VEXTRACTF64X2,
    instrux_VEXTRACTF64X4,
    instrux_VEXTRACTI32X4,
    instrux_VEXTRACTI32X8,
    instrux_VEXTRACTI64X2,
    instrux_VEXTRACTI64X4,
    instrux_VFIXUPIMMPD,
    instrux_VFIXUPIMMPS,
    instrux_VFIXUPIMMSD,
    instrux_VFIXUPIMMSS,
    instrux_VFPCLASSPD,
    instrux_VFPCLASSPS,
    instrux_VFPCLASSSD,
    instrux_VFPCLASSSS,
    instrux_VGATHERPF0DPD,
    instrux_VGATHERPF0DPS,
    instrux_VGATHERPF0QPD,
    instrux_VGATHERPF0QPS,
    instrux_VGATHERPF1DPD,
    instrux_VGATHERPF1DPS,
    instrux_VGATHERPF1QPD,
    instrux_VGATHERPF1QPS,
    instrux_VGETEXPPD,
    instrux_VGETEXPPS,
    instrux_VGETEXPSD,
    instrux_VGETEXPSS,
    instrux_VGETMANTPD,
    instrux_VGETMANTPS,
    instrux_VGETMANTSD,
    instrux_VGETMANTSS,
    instrux_VINSERTF32X4,
    instrux_VINSERTF32X8,
    instrux_VINSERTF64X2,
    instrux_VINSERTF64X4,
    instrux_VINSERTI32X4,
    instrux_VINSERTI32X8,
    instrux_VINSERTI64X2,
    instrux_VINSERTI64X4,
    instrux_VMOVDQA32,
    instrux_VMOVDQA64,
    instrux_VMOVDQU16,
    instrux_VMOVDQU32,
    instrux_VMOVDQU64,
    instrux_VMOVDQU8,
    instrux_VPABSQ,
    instrux_VPANDD,
    instrux_VPANDND,
    instrux_VPANDNQ,
    instrux_VPANDQ,
    instrux_VPBLENDMB,
    instrux_VPBLENDMD,
    instrux_VPBLENDMQ,
    instrux_VPBLENDMW,
    instrux_VPBROADCASTMB2Q,
    instrux_VPBROADCASTMW2D,
    instrux_VPCMPB,
    instrux_VPCMPD,
    instrux_VPCMPQ,
    instrux_VPCMPUB,
    instrux_VPCMPUD,
    instrux_VPCMPUQ,
    instrux_VPCMPUW,
    instrux_VPCMPW,
    instrux_VPCOMPRESSD,
    instrux_VPCOMPRESSQ,
    instrux_VPCONFLICTD,
    instrux_VPCONFLICTQ,
    instrux_VPERMB,
    instrux_VPERMI2B,
    instrux_VPERMI2D,
    instrux_VPERMI2PD,
    instrux_VPERMI2PS,
    instrux_VPERMI2Q,
    instrux_VPERMI2W,
    instrux_VPERMT2B,
    instrux_VPERMT2D,
    instrux_VPERMT2PD,
    instrux_VPERMT2PS,
    instrux_VPERMT2Q,
    instrux_VPERMT2W,
    instrux_VPERMW,
    instrux_VPEXPANDD,
    instrux_VPEXPANDQ,
    instrux_VPLZCNTD,
    instrux_VPLZCNTQ,
    instrux_VPMADD52HUQ,
    instrux_VPMADD52LUQ,
    instrux_VPMAXSQ,
    instrux_VPMAXUQ,
    instrux_VPMINSQ,
    instrux_VPMINUQ,
    instrux_VPMOVB2M,
    instrux_VPMOVD2M,
    instrux_VPMOVDB,
    instrux_VPMOVDW,
    instrux_VPMOVM2B,
    instrux_VPMOVM2D,
    instrux_VPMOVM2Q,
    instrux_VPMOVM2W,
    instrux_VPMOVQ2M,
    instrux_VPMOVQB,
    instrux_VPMOVQD,
    instrux_VPMOVQW,
    instrux_VPMOVSDB,
    instrux_VPMOVSDW,
    instrux_VPMOVSQB,
    instrux_VPMOVSQD,
    instrux_VPMOVSQW,
    instrux_VPMOVSWB,
    instrux_VPMOVUSDB,
    instrux_VPMOVUSDW,
    instrux_VPMOVUSQB,
    instrux_VPMOVUSQD,
    instrux_VPMOVUSQW,
    instrux_VPMOVUSWB,
    instrux_VPMOVW2M,
    instrux_VPMOVWB,
    instrux_VPMULLQ,
    instrux_VPMULTISHIFTQB,
    instrux_VPORD,
    instrux_VPORQ,
    instrux_VPROLD,
    instrux_VPROLQ,
    instrux_VPROLVD,
    instrux_VPROLVQ,
    instrux_VPRORD,
    instrux_VPRORQ,
    instrux_VPRORVD,
    instrux_VPRORVQ,
    instrux_VPSCATTERDD,
    instrux_VPSCATTERDQ,
    instrux_VPSCATTERQD,
    instrux_VPSCATTERQQ,
    instrux_VPSLLVW,
    instrux_VPSRAQ,
    instrux_VPSRAVQ,
    instrux_VPSRAVW,
    instrux_VPSRLVW,
    instrux_VPTERNLOGD,
    instrux_VPTERNLOGQ,
    instrux_VPTESTMB,
    instrux_VPTESTMD,
    instrux_VPTESTMQ,
    instrux_VPTESTMW,
    instrux_VPTESTNMB,
    instrux_VPTESTNMD,
    instrux_VPTESTNMQ,
    instrux_VPTESTNMW,
    instrux_VPXORD,
    instrux_VPXORQ,
    instrux_VRANGEPD,
    instrux_VRANGEPS,
    instrux_VRANGESD,
    instrux_VRANGESS,
    instrux_VRCP14PD,
    instrux_VRCP14PS,
    instrux_VRCP14SD,
    instrux_VRCP14SS,
    instrux_VRCP28PD,
    instrux_VRCP28PS,
    instrux_VRCP28SD,
    instrux_VRCP28SS,
    instrux_VREDUCEPD,
    instrux_VREDUCEPS,
    instrux_VREDUCESD,
    instrux_VREDUCESS,
    instrux_VRNDSCALEPD,
    instrux_VRNDSCALEPS,
    instrux_VRNDSCALESD,
    instrux_VRNDSCALESS,
    instrux_VRSQRT14PD,
    instrux_VRSQRT14PS,
    instrux_VRSQRT14SD,
    instrux_VRSQRT14SS,
    instrux_VRSQRT28PD,
    instrux_VRSQRT28PS,
    instrux_VRSQRT28SD,
    instrux_VRSQRT28SS,
    instrux_VSCALEFPD,
    instrux_VSCALEFPS,
    instrux_VSCALEFSD,
    instrux_VSCALEFSS,
    instrux_VSCATTERDPD,
    instrux_VSCATTERDPS,
    instrux_VSCATTERPF0DPD,
    instrux_VSCATTERPF0DPS,
    instrux_VSCATTERPF0QPD,
    instrux_VSCATTERPF0QPS,
    instrux_VSCATTERPF1DPD,
    instrux_VSCATTERPF1DPS,
    instrux_VSCATTERPF1QPD,
    instrux_VSCATTERPF1QPS,
    instrux_VSCATTERQPD,
    instrux_VSCATTERQPS,
    instrux_VSHUFF32X4,
    instrux_VSHUFF64X2,
    instrux_VSHUFI32X4,
    instrux_VSHUFI64X2,
    instrux_RDPKRU,
    instrux_WRPKRU,
    instrux_RDPID,
    instrux_CLFLUSHOPT,
    instrux_CLWB,
    instrux_PCOMMIT,
    instrux_CLZERO,
    instrux_PTWRITE,
    instrux_CLDEMOTE,
    instrux_MOVDIRI,
    instrux_MOVDIR64B,
    instrux_PCONFIG,
    instrux_TPAUSE,
    instrux_UMONITOR,
    instrux_UMWAIT,
    instrux_WBNOINVD,
    instrux_GF2P8AFFINEINVQB,
    instrux_VGF2P8AFFINEINVQB,
    instrux_GF2P8AFFINEQB,
    instrux_VGF2P8AFFINEQB,
    instrux_GF2P8MULB,
    instrux_VGF2P8MULB,
    instrux_VPCOMPRESSB,
    instrux_VPCOMPRESSW,
    instrux_VPEXPANDB,
    instrux_VPEXPANDW,
    instrux_VPSHLDW,
    instrux_VPSHLDD,
    instrux_VPSHLDQ,
    instrux_VPSHLDVW,
    instrux_VPSHLDVD,
    instrux_VPSHLDVQ,
    instrux_VPSHRDW,
    instrux_VPSHRDD,
    instrux_VPSHRDQ,
    instrux_VPSHRDVW,
    instrux_VPSHRDVD,
    instrux_VPSHRDVQ,
    instrux_VPDPBUSD,
    instrux_VPDPBUSDS,
    instrux_VPDPWSSD,
    instrux_VPDPWSSDS,
    instrux_VPOPCNTB,
    instrux_VPOPCNTW,
    instrux_VPOPCNTD,
    instrux_VPOPCNTQ,
    instrux_VPSHUFBITQMB,
    instrux_V4FMADDPS,
    instrux_V4FNMADDPS,
    instrux_V4FMADDSS,
    instrux_V4FNMADDSS,
    instrux_V4DPWSSDS,
    instrux_V4DPWSSD,
    instrux_ENCLS,
    instrux_ENCLU,
    instrux_ENCLV,
    instrux_HINT_NOP0,
    instrux_HINT_NOP1,
    instrux_HINT_NOP2,
    instrux_HINT_NOP3,
    instrux_HINT_NOP4,
    instrux_HINT_NOP5,
    instrux_HINT_NOP6,
    instrux_HINT_NOP7,
    instrux_HINT_NOP8,
    instrux_HINT_NOP9,
    instrux_HINT_NOP10,
    instrux_HINT_NOP11,
    instrux_HINT_NOP12,
    instrux_HINT_NOP13,
    instrux_HINT_NOP14,
    instrux_HINT_NOP15,
    instrux_HINT_NOP16,
    instrux_HINT_NOP17,
    instrux_HINT_NOP18,
    instrux_HINT_NOP19,
    instrux_HINT_NOP20,
    instrux_HINT_NOP21,
    instrux_HINT_NOP22,
    instrux_HINT_NOP23,
    instrux_HINT_NOP24,
    instrux_HINT_NOP25,
    instrux_HINT_NOP26,
    instrux_HINT_NOP27,
    instrux_HINT_NOP28,
    instrux_HINT_NOP29,
    instrux_HINT_NOP30,
    instrux_HINT_NOP31,
    instrux_HINT_NOP32,
    instrux_HINT_NOP33,
    instrux_HINT_NOP34,
    instrux_HINT_NOP35,
    instrux_HINT_NOP36,
    instrux_HINT_NOP37,
    instrux_HINT_NOP38,
    instrux_HINT_NOP39,
    instrux_HINT_NOP40,
    instrux_HINT_NOP41,
    instrux_HINT_NOP42,
    instrux_HINT_NOP43,
    instrux_HINT_NOP44,
    instrux_HINT_NOP45,
    instrux_HINT_NOP46,
    instrux_HINT_NOP47,
    instrux_HINT_NOP48,
    instrux_HINT_NOP49,
    instrux_HINT_NOP50,
    instrux_HINT_NOP51,
    instrux_HINT_NOP52,
    instrux_HINT_NOP53,
    instrux_HINT_NOP54,
    instrux_HINT_NOP55,
    instrux_HINT_NOP56,
    instrux_HINT_NOP57,
    instrux_HINT_NOP58,
    instrux_HINT_NOP59,
    instrux_HINT_NOP60,
    instrux_HINT_NOP61,
    instrux_HINT_NOP62,
    instrux_HINT_NOP63,
    instrux_CMOVcc,
    instrux_Jcc,
    instrux_SETcc,
};
