;Testname=keylocker; Arguments=-fbin -okeylocker.bin -O0 -DSRC; Files=stdout stderr keylocker.bin

%macro testcase 2
 %ifdef BIN
  db %1
 %endif
 %ifdef SRC
  %2
 %endif
%endmacro


bits 64

testcase    {   0xf3, 0x0f, 0x38, 0xdd, 0x08                                               }, {         AESDEC128KL  xmm1, [rax]                                             }
testcase    {   0xf3, 0x0f, 0x38, 0xdf, 0x08                                               }, {         AESDEC256KL  xmm1, zword [rax]                                       }
testcase    {   0xf3, 0x42, 0x0f, 0x38, 0xdf, 0x0c, 0xf0                                   }, {         AESDEC256KL  xmm1, zword [rax+r14*8]                                 }
testcase    {   0xf3, 0x0f, 0x38, 0xdc, 0x08                                               }, {         AESENC128KL  xmm1, [rax]                                             }
testcase    {   0xf3, 0x0f, 0x38, 0xde, 0x08                                               }, {         AESENC256KL  xmm1, zword [rax]                                       }
testcase    {   0xf3, 0x42, 0x0f, 0x38, 0xde, 0x0c, 0xf0                                   }, {         AESENC256KL  xmm1, zword [rax+r14*8]                                 }
testcase    {   0xf3, 0x0f, 0x38, 0xfa, 0xc0                                               }, {         ENCODEKEY128  eax, eax                                               }
testcase    {   0xf3, 0x0f, 0x38, 0xfb, 0xc0                                               }, {         ENCODEKEY256  eax, eax                                               }
testcase    {   0xf3, 0x0f, 0x38, 0xdc, 0xc9                                               }, {         LOADIWKEY  xmm1, xmm1                                                }
