;Testname=avx512-vp2intersect; Arguments=-fbin -oavx512-vp2intersect.bin -O0 -DSRC; Files=stdout stderr avx512-vp2intersect.bin

%macro testcase 2
 %ifdef BIN
  db %1
 %endif
 %ifdef SRC
  %2
 %endif
%endmacro


bits 64

testcase    { 0x62, 0xf2, 0x77, 0x08, 0x68, 0xd7                                           }, { {evex} VP2INTERSECTD  k2, xmm1, xmm7                                         }
testcase    { 0x62, 0xd2, 0x67, 0x48, 0x68, 0xd7                                           }, { {evex} VP2INTERSECTD  k2, zmm3, zmm15                                        }
testcase    { 0x62, 0xf2, 0x67, 0x48, 0x68, 0x10                                           }, { {evex} VP2INTERSECTD  k2, zmm3, zword [rax]                                  }
testcase    { 0x62, 0xf2, 0x77, 0x08, 0x68, 0x10                                           }, { {evex} VP2INTERSECTD  k2, xmm1, oword [rax]                                  }
testcase    { 0x62, 0xf2, 0x77, 0x18, 0x68, 0x10                                           }, { {evex} VP2INTERSECTD  k2, xmm1, dword [rax]{1to4}                            }
testcase    { 0x62, 0xd2, 0x77, 0x28, 0x68, 0xd7                                           }, { {evex} VP2INTERSECTD  k2, ymm1, ymm15                                        }
testcase    { 0x62, 0xf2, 0x77, 0x28, 0x68, 0x10                                           }, { {evex} VP2INTERSECTD  k2, ymm1, yword [rax]                                  }
testcase    { 0x62, 0xf2, 0xf7, 0x08, 0x68, 0xd7                                           }, { {evex} VP2INTERSECTQ  k2, xmm1, xmm7                                         }
testcase    { 0x62, 0xd2, 0xe7, 0x48, 0x68, 0xd7                                           }, { {evex} VP2INTERSECTQ  k2, zmm3, zmm15                                        }
testcase    { 0x62, 0xf2, 0xe7, 0x48, 0x68, 0x10                                           }, { {evex} VP2INTERSECTQ  k2, zmm3, zword [rax]                                  }
testcase    { 0x62, 0xf2, 0xe7, 0x58, 0x68, 0x10                                           }, { {evex} VP2INTERSECTQ  k2, zmm3, qword [rax]{1to8}                            }
testcase    { 0x62, 0xf2, 0xf7, 0x08, 0x68, 0x10                                           }, { {evex} VP2INTERSECTQ  k2, xmm1, oword [rax]                                  }
testcase    { 0x62, 0xf2, 0xf7, 0x18, 0x68, 0x10                                           }, { {evex} VP2INTERSECTQ  k2, xmm1, qword [rax]{1to2}                            }
testcase    { 0x62, 0xd2, 0xf7, 0x28, 0x68, 0xd7                                           }, { {evex} VP2INTERSECTQ  k2, ymm1, ymm15                                        }
testcase    { 0x62, 0xf2, 0xf7, 0x28, 0x68, 0x10                                           }, { {evex} VP2INTERSECTQ  k2, ymm1, yword [rax]                                  }
testcase    { 0x62, 0xf2, 0xf7, 0x38, 0x68, 0x10                                           }, { {evex} VP2INTERSECTQ  k2, ymm1, qword [rax]{1to4}                            }
