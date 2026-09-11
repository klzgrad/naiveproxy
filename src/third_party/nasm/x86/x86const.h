#ifndef NASM_X86CONST_H
#define NASM_X86CONST_H

/*
 * Values used for the DFV bits in the CCMPcc and CTESTcc instructions.
 */
enum dfv_mask {
    DFV_CF   = 1,
    DFV_ZF   = 2,
    DFV_SF   = 4,
    DFV_OF   = 8
};

#endif /* NASM_X86CONST_H */
