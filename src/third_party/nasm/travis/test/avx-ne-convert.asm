;Testname=avx-ne-convert; Arguments=-fbin -oavx-ne-convert.bin -O0 -DSRC; Files=stdout stderr avx-ne-convert.bin

%macro testcase 2
 %ifdef BIN
  db %1
 %endif
 %ifdef SRC
  %2
 %endif
%endmacro


bits 64

testcase    { 0xc4, 0xe2, 0x7a, 0xb1, 0x08                                                 }, { {vex} VBCSTNEBF162PS  xmm1, word [rax]                                       }
testcase    { 0xc4, 0xe2, 0x7a, 0xb1, 0x0c, 0xc8                                           }, { {vex} VBCSTNEBF162PS  xmm1, word [rax+rcx*8]                                 }
testcase    { 0xc4, 0xe2, 0x7e, 0xb1, 0x08                                                 }, { {vex} VBCSTNEBF162PS  ymm1, word [rax]                                       }
testcase    { 0xc4, 0xe2, 0x7e, 0xb1, 0x0c, 0xc8                                           }, { {vex} VBCSTNEBF162PS  ymm1, word [rax+rcx*8]                                 }
