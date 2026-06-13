;Testname=avx10.2; Arguments=-fbin -oavx10.2.bin -O0 -DSRC; Files=stdout stderr avx10.2.bin

%macro testcase 2
 %ifdef BIN
  db %1
 %endif
 %ifdef SRC
  %2
 %endif
%endmacro


bits 64

testcase        {  0x62, 0xf5, 0x6d, 0x08, 0x58, 0xcb                                        }, {  VADDBF16 xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xb5, 0x6d, 0x08, 0x58, 0x4c, 0xf0, 0x01                            }, {  VADDBF16 xmm1, xmm2, [rax+r14*8+0x10]  }
testcase        {  0x62, 0xf5, 0x6d, 0x28, 0x58, 0xcb                                        }, {  VADDBF16 ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf5, 0x6d, 0x48, 0x58, 0xcb                                        }, {  VADDBF16 zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf3, 0x77, 0x08, 0xc2, 0xca, 0x10                                  }, {  VCMPBF16 k1, xmm1, xmm2, 0x10  }
testcase        {  0x62, 0xf3, 0x77, 0x0a, 0xc2, 0xca, 0x10                                  }, {  VCMPBF16 k1{k2}, xmm1, xmm2, 0x10  }
testcase        {  0x62, 0xb3, 0x77, 0x08, 0xc2, 0x54, 0xf0, 0x01, 0x10                      }, {  VCMPBF16 k2, xmm1, [rax+r14*8+0x10], 0x10  }
testcase        {  0x62, 0xf3, 0x77, 0x28, 0xc2, 0xca, 0x10                                  }, {  VCMPBF16 k1, ymm1, ymm2, 0x10  }
testcase        {  0x62, 0xf3, 0x77, 0x48, 0xc2, 0xca, 0x10                                  }, {  VCMPBF16 k1, zmm1, zmm2, 0x10  }
testcase        {  0x62, 0xf5, 0x7d, 0x08, 0x2f, 0xca                                        }, {  VCOMISBF16 xmm1, xmm2  }
testcase        {  0x62, 0xf1, 0xff, 0x08, 0x2f, 0xca                                        }, {  VCOMXSD xmm1, xmm2  }
testcase        {  0x62, 0xf5, 0x7e, 0x08, 0x2f, 0xca                                        }, {  VCOMXSH xmm1, xmm2  }
testcase        {  0x62, 0xf1, 0x7e, 0x08, 0x2f, 0xca                                        }, {  VCOMXSS xmm1, xmm2  }
testcase        {  0x62, 0xf2, 0x6f, 0x08, 0x74, 0xcb                                        }, {  VCVT2PH2BF8 xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xb2, 0x6f, 0x08, 0x74, 0x4c, 0xf0, 0x01                            }, {  VCVT2PH2BF8 xmm1, xmm2, [rax+r14*8+0x10]  }
testcase        {  0x62, 0xf2, 0x6f, 0x28, 0x74, 0xcb                                        }, {  VCVT2PH2BF8 ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf2, 0x6f, 0x48, 0x74, 0xcb                                        }, {  VCVT2PH2BF8 zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf5, 0x6f, 0x08, 0x74, 0xcb                                        }, {  VCVT2PH2BF8S xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xb5, 0x6f, 0x08, 0x74, 0x4c, 0xf0, 0x01                            }, {  VCVT2PH2BF8S xmm1, xmm2, [rax+r14*8+0x10]  }
testcase        {  0x62, 0xf5, 0x6f, 0x28, 0x74, 0xcb                                        }, {  VCVT2PH2BF8S ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf5, 0x6f, 0x48, 0x74, 0xcb                                        }, {  VCVT2PH2BF8S zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf5, 0x6f, 0x08, 0x18, 0xcb                                        }, {  VCVT2PH2HF8 xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xb5, 0x6f, 0x08, 0x18, 0x4c, 0xf0, 0x01                            }, {  VCVT2PH2HF8 xmm1, xmm2, [rax+r14*8+0x10]  }
testcase        {  0x62, 0xf5, 0x6f, 0x28, 0x18, 0xcb                                        }, {  VCVT2PH2HF8 ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf5, 0x6f, 0x48, 0x18, 0xcb                                        }, {  VCVT2PH2HF8 zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf5, 0x6f, 0x08, 0x1b, 0xcb                                        }, {  VCVT2PH2HF8S xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xb5, 0x6f, 0x08, 0x1b, 0x4c, 0xf0, 0x01                            }, {  VCVT2PH2HF8S xmm1, xmm2, [rax+r14*8+0x10]  }
testcase        {  0x62, 0xf5, 0x6f, 0x28, 0x1b, 0xcb                                        }, {  VCVT2PH2HF8S ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf5, 0x6f, 0x48, 0x1b, 0xcb                                        }, {  VCVT2PH2HF8S zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf5, 0x7f, 0x08, 0x69, 0xca                                        }, {  VCVTBF162IBS xmm1, xmm2  }
testcase        {  0x62, 0xf5, 0x7f, 0x28, 0x69, 0xca                                        }, {  VCVTBF162IBS ymm1, ymm2  }
testcase        {  0x62, 0xf5, 0x7f, 0x48, 0x69, 0xca                                        }, {  VCVTBF162IBS zmm1, zmm2  }
testcase        {  0x62, 0xf5, 0x7f, 0x08, 0x6b, 0xca                                        }, {  VCVTBF162IUBS xmm1, xmm2  }
testcase        {  0x62, 0xf5, 0x7f, 0x28, 0x6b, 0xca                                        }, {  VCVTBF162IUBS ymm1, ymm2  }
testcase        {  0x62, 0xf5, 0x7f, 0x48, 0x6b, 0xca                                        }, {  VCVTBF162IUBS zmm1, zmm2  }
testcase        {  0x62, 0xf2, 0x7e, 0x08, 0x74, 0xca                                        }, {  VCVTPH2BF8 xmm1, xmm2  }
testcase        {  0x62, 0xf2, 0x7e, 0x28, 0x74, 0xca                                        }, {  VCVTPH2BF8 ymm1, ymm2  }
testcase        {  0x62, 0xf2, 0x7e, 0x48, 0x74, 0xca                                        }, {  VCVTPH2BF8 zmm1, zmm2  }
testcase        {  0x62, 0xf5, 0x7e, 0x08, 0x74, 0xca                                        }, {  VCVTPH2BF8S xmm1, xmm2  }
testcase        {  0x62, 0xf5, 0x7e, 0x28, 0x74, 0xca                                        }, {  VCVTPH2BF8S ymm1, ymm2  }
testcase        {  0x62, 0xf5, 0x7e, 0x48, 0x74, 0xca                                        }, {  VCVTPH2BF8S zmm1, zmm2  }
testcase        {  0x62, 0xf5, 0x7e, 0x08, 0x18, 0xca                                        }, {  VCVTPH2HF8 xmm1, xmm2  }
testcase        {  0x62, 0xf5, 0x7e, 0x28, 0x18, 0xca                                        }, {  VCVTPH2HF8 ymm1, ymm2  }
testcase        {  0x62, 0xf5, 0x7e, 0x48, 0x18, 0xca                                        }, {  VCVTPH2HF8 zmm1, zmm2  }
testcase        {  0x62, 0xf5, 0x7e, 0x08, 0x1b, 0xca                                        }, {  VCVTPH2HF8S xmm1, xmm2  }
testcase        {  0x62, 0xf5, 0x7e, 0x28, 0x1b, 0xca                                        }, {  VCVTPH2HF8S ymm1, ymm2  }
testcase        {  0x62, 0xf5, 0x7e, 0x48, 0x1b, 0xca                                        }, {  VCVTPH2HF8S zmm1, zmm2  }
testcase    { 0x62, 0xf2, 0x75, 0x08, 0x67, 0xc7                                           }, { {evex} VCVT2PS2PHX  xmm0, xmm1, xmm7                                         }
testcase    { 0x62, 0xf2, 0x75, 0x08, 0x67, 0x00                                           }, { {evex} VCVT2PS2PHX  xmm0, xmm1, oword [rax]                                  }
testcase    { 0x62, 0xf2, 0x75, 0x18, 0x67, 0x00                                           }, { {evex} VCVT2PS2PHX  xmm0, xmm1, dword [rax]{1to4}                            }
testcase    { 0x62, 0xf2, 0x75, 0x0f, 0x67, 0xc7                                           }, { {evex} VCVT2PS2PHX  xmm0{k7}, xmm1, xmm7                                     }
testcase    { 0x62, 0xf2, 0x75, 0x0f, 0x67, 0x00                                           }, { {evex} VCVT2PS2PHX  xmm0{k7}, xmm1, oword [rax]                              }
testcase    { 0x62, 0xf2, 0x75, 0x1f, 0x67, 0x00                                           }, { {evex} VCVT2PS2PHX  xmm0{k7}, xmm1, dword [rax]{1to4}                        }
testcase    { 0x62, 0xf2, 0x75, 0x8f, 0x67, 0xc7                                           }, { {evex} VCVT2PS2PHX  xmm0{k7}{z}, xmm1, xmm7                                  }
testcase    { 0x62, 0xf2, 0x75, 0x8f, 0x67, 0x00                                           }, { {evex} VCVT2PS2PHX  xmm0{k7}{z}, xmm1, oword [rax]                           }
testcase    { 0x62, 0xf2, 0x75, 0x9f, 0x67, 0x00                                           }, { {evex} VCVT2PS2PHX  xmm0{k7}{z}, xmm1, dword [rax]{1to4}                     }
testcase    { 0x62, 0xd2, 0x75, 0x28, 0x67, 0xc7                                           }, { {evex} VCVT2PS2PHX  ymm0, ymm1, ymm15                                        }
testcase    { 0x62, 0xf2, 0x75, 0x28, 0x67, 0x00                                           }, { {evex} VCVT2PS2PHX  ymm0, ymm1, yword [rax]                                  }
testcase    { 0x62, 0xf2, 0x75, 0x38, 0x67, 0x00                                           }, { {evex} VCVT2PS2PHX  ymm0, ymm1, dword [rax]{1to8}                            }
testcase    { 0x62, 0xd2, 0x75, 0x2f, 0x67, 0xc7                                           }, { {evex} VCVT2PS2PHX  ymm0{k7}, ymm1, ymm15                                    }
testcase    { 0x62, 0xf2, 0x75, 0x2f, 0x67, 0x00                                           }, { {evex} VCVT2PS2PHX  ymm0{k7}, ymm1, yword [rax]                              }
testcase    { 0x62, 0xf2, 0x75, 0x3f, 0x67, 0x00                                           }, { {evex} VCVT2PS2PHX  ymm0{k7}, ymm1, dword [rax]{1to8}                        }
testcase    { 0x62, 0xd2, 0x75, 0xaf, 0x67, 0xc7                                           }, { {evex} VCVT2PS2PHX  ymm0{k7}{z}, ymm1, ymm15                                 }
testcase    { 0x62, 0xf2, 0x75, 0xaf, 0x67, 0x00                                           }, { {evex} VCVT2PS2PHX  ymm0{k7}{z}, ymm1, yword [rax]                           }
testcase    { 0x62, 0xf2, 0x75, 0xbf, 0x67, 0x00                                           }, { {evex} VCVT2PS2PHX  ymm0{k7}{z}, ymm1, dword [rax]{1to8}                     }
testcase    { 0x62, 0xd2, 0x65, 0x48, 0x67, 0xc7                                           }, { {evex} VCVT2PS2PHX  zmm0, zmm3, zmm15                                        }
testcase    { 0x62, 0xf2, 0x65, 0x48, 0x67, 0x00                                           }, { {evex} VCVT2PS2PHX  zmm0, zmm3, zword [rax]                                  }
testcase    { 0x62, 0xf2, 0x65, 0x58, 0x67, 0x00                                           }, { {evex} VCVT2PS2PHX  zmm0, zmm3, dword [rax]{1to16}                           }
testcase    { 0x62, 0xd2, 0x65, 0x4f, 0x67, 0xc7                                           }, { {evex} VCVT2PS2PHX  zmm0{k7}, zmm3, zmm15                                    }
testcase    { 0x62, 0xf2, 0x65, 0x4f, 0x67, 0x00                                           }, { {evex} VCVT2PS2PHX  zmm0{k7}, zmm3, zword [rax]                              }
testcase    { 0x62, 0xf2, 0x65, 0x5f, 0x67, 0x00                                           }, { {evex} VCVT2PS2PHX  zmm0{k7}, zmm3, dword [rax]{1to16}                       }
testcase    { 0x62, 0xd2, 0x65, 0xcf, 0x67, 0xc7                                           }, { {evex} VCVT2PS2PHX  zmm0{k7}{z}, zmm3, zmm15                                 }
testcase    { 0x62, 0xf2, 0x65, 0xcf, 0x67, 0x00                                           }, { {evex} VCVT2PS2PHX  zmm0{k7}{z}, zmm3, zword [rax]                           }
testcase    { 0x62, 0xf2, 0x65, 0xdf, 0x67, 0x00                                           }, { {evex} VCVT2PS2PHX  zmm0{k7}{z}, zmm3, dword [rax]{1to16}                    }
testcase    { 0x62, 0xf2, 0x74, 0x08, 0x74, 0xc7                                           }, { {evex} VCVTBIASPH2BF8  xmm0, xmm1, xmm7                                      }
testcase    { 0x62, 0xf2, 0x74, 0x08, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8  xmm0, xmm1, oword [rax]                               }
testcase    { 0x62, 0xf2, 0x74, 0x18, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8  xmm0, xmm1, word [rax]{1to8}                          }
testcase    { 0x62, 0xf2, 0x74, 0x0f, 0x74, 0xc7                                           }, { {evex} VCVTBIASPH2BF8  xmm0{k7}, xmm1, xmm7                                  }
testcase    { 0x62, 0xf2, 0x74, 0x0f, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8  xmm0{k7}, xmm1, oword [rax]                           }
testcase    { 0x62, 0xf2, 0x74, 0x1f, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8  xmm0{k7}, xmm1, word [rax]{1to8}                      }
testcase    { 0x62, 0xf2, 0x74, 0x8f, 0x74, 0xc7                                           }, { {evex} VCVTBIASPH2BF8  xmm0{k7}{z}, xmm1, xmm7                               }
testcase    { 0x62, 0xf2, 0x74, 0x8f, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8  xmm0{k7}{z}, xmm1, oword [rax]                        }
testcase    { 0x62, 0xf2, 0x74, 0x9f, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8  xmm0{k7}{z}, xmm1, word [rax]{1to8}                   }
testcase    { 0x62, 0xd2, 0x74, 0x28, 0x74, 0xc7                                           }, { {evex} VCVTBIASPH2BF8  xmm0, ymm1, ymm15                                     }
testcase    { 0x62, 0xf2, 0x74, 0x28, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8  xmm0, ymm1, yword [rax]                               }
testcase    { 0x62, 0xf2, 0x74, 0x38, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8  xmm0, ymm1, word [rax]{1to16}                         }
testcase    { 0x62, 0xd2, 0x74, 0x2f, 0x74, 0xc7                                           }, { {evex} VCVTBIASPH2BF8  xmm0{k7}, ymm1, ymm15                                 }
testcase    { 0x62, 0xf2, 0x74, 0x2f, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8  xmm0{k7}, ymm1, yword [rax]                           }
testcase    { 0x62, 0xf2, 0x74, 0x3f, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8  xmm0{k7}, ymm1, word [rax]{1to16}                     }
testcase    { 0x62, 0xd2, 0x74, 0xaf, 0x74, 0xc7                                           }, { {evex} VCVTBIASPH2BF8  xmm0{k7}{z}, ymm1, ymm15                              }
testcase    { 0x62, 0xf2, 0x74, 0xaf, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8  xmm0{k7}{z}, ymm1, yword [rax]                        }
testcase    { 0x62, 0xf2, 0x74, 0xbf, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8  xmm0{k7}{z}, ymm1, word [rax]{1to16}                  }
testcase    { 0x62, 0xd2, 0x64, 0x48, 0x74, 0xc7                                           }, { {evex} VCVTBIASPH2BF8  ymm0, zmm3, zmm15                                     }
testcase    { 0x62, 0xf2, 0x64, 0x48, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8  ymm0, zmm3, zword [rax]                               }
testcase    { 0x62, 0xf2, 0x64, 0x58, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8  ymm0, zmm3, word [rax]{1to32}                         }
testcase    { 0x62, 0xd2, 0x64, 0x4f, 0x74, 0xc7                                           }, { {evex} VCVTBIASPH2BF8  ymm0{k7}, zmm3, zmm15                                 }
testcase    { 0x62, 0xf2, 0x64, 0x4f, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8  ymm0{k7}, zmm3, zword [rax]                           }
testcase    { 0x62, 0xf2, 0x64, 0x5f, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8  ymm0{k7}, zmm3, word [rax]{1to32}                     }
testcase    { 0x62, 0xd2, 0x64, 0xcf, 0x74, 0xc7                                           }, { {evex} VCVTBIASPH2BF8  ymm0{k7}{z}, zmm3, zmm15                              }
testcase    { 0x62, 0xf2, 0x64, 0xcf, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8  ymm0{k7}{z}, zmm3, zword [rax]                        }
testcase    { 0x62, 0xf2, 0x64, 0xdf, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8  ymm0{k7}{z}, zmm3, word [rax]{1to32}                  }
testcase    { 0x62, 0xf5, 0x74, 0x08, 0x74, 0xc7                                           }, { {evex} VCVTBIASPH2BF8S  xmm0, xmm1, xmm7                                     }
testcase    { 0x62, 0xf5, 0x74, 0x08, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8S  xmm0, xmm1, oword [rax]                              }
testcase    { 0x62, 0xf5, 0x74, 0x18, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8S  xmm0, xmm1, word [rax]{1to8}                         }
testcase    { 0x62, 0xf5, 0x74, 0x0f, 0x74, 0xc7                                           }, { {evex} VCVTBIASPH2BF8S  xmm0{k7}, xmm1, xmm7                                 }
testcase    { 0x62, 0xf5, 0x74, 0x0f, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8S  xmm0{k7}, xmm1, oword [rax]                          }
testcase    { 0x62, 0xf5, 0x74, 0x1f, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8S  xmm0{k7}, xmm1, word [rax]{1to8}                     }
testcase    { 0x62, 0xf5, 0x74, 0x8f, 0x74, 0xc7                                           }, { {evex} VCVTBIASPH2BF8S  xmm0{k7}{z}, xmm1, xmm7                              }
testcase    { 0x62, 0xf5, 0x74, 0x8f, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8S  xmm0{k7}{z}, xmm1, oword [rax]                       }
testcase    { 0x62, 0xf5, 0x74, 0x9f, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8S  xmm0{k7}{z}, xmm1, word [rax]{1to8}                  }
testcase    { 0x62, 0xd5, 0x74, 0x28, 0x74, 0xc7                                           }, { {evex} VCVTBIASPH2BF8S  xmm0, ymm1, ymm15                                    }
testcase    { 0x62, 0xf5, 0x74, 0x28, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8S  xmm0, ymm1, yword [rax]                              }
testcase    { 0x62, 0xf5, 0x74, 0x38, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8S  xmm0, ymm1, word [rax]{1to16}                        }
testcase    { 0x62, 0xd5, 0x74, 0x2f, 0x74, 0xc7                                           }, { {evex} VCVTBIASPH2BF8S  xmm0{k7}, ymm1, ymm15                                }
testcase    { 0x62, 0xf5, 0x74, 0x2f, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8S  xmm0{k7}, ymm1, yword [rax]                          }
testcase    { 0x62, 0xf5, 0x74, 0x3f, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8S  xmm0{k7}, ymm1, word [rax]{1to16}                    }
testcase    { 0x62, 0xd5, 0x74, 0xaf, 0x74, 0xc7                                           }, { {evex} VCVTBIASPH2BF8S  xmm0{k7}{z}, ymm1, ymm15                             }
testcase    { 0x62, 0xf5, 0x74, 0xaf, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8S  xmm0{k7}{z}, ymm1, yword [rax]                       }
testcase    { 0x62, 0xf5, 0x74, 0xbf, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8S  xmm0{k7}{z}, ymm1, word [rax]{1to16}                 }
testcase    { 0x62, 0xd5, 0x64, 0x48, 0x74, 0xc7                                           }, { {evex} VCVTBIASPH2BF8S  ymm0, zmm3, zmm15                                    }
testcase    { 0x62, 0xf5, 0x64, 0x48, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8S  ymm0, zmm3, zword [rax]                              }
testcase    { 0x62, 0xf5, 0x64, 0x58, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8S  ymm0, zmm3, word [rax]{1to32}                        }
testcase    { 0x62, 0xd5, 0x64, 0x4f, 0x74, 0xc7                                           }, { {evex} VCVTBIASPH2BF8S  ymm0{k7}, zmm3, zmm15                                }
testcase    { 0x62, 0xf5, 0x64, 0x4f, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8S  ymm0{k7}, zmm3, zword [rax]                          }
testcase    { 0x62, 0xf5, 0x64, 0x5f, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8S  ymm0{k7}, zmm3, word [rax]{1to32}                    }
testcase    { 0x62, 0xd5, 0x64, 0xcf, 0x74, 0xc7                                           }, { {evex} VCVTBIASPH2BF8S  ymm0{k7}{z}, zmm3, zmm15                             }
testcase    { 0x62, 0xf5, 0x64, 0xcf, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8S  ymm0{k7}{z}, zmm3, zword [rax]                       }
testcase    { 0x62, 0xf5, 0x64, 0xdf, 0x74, 0x00                                           }, { {evex} VCVTBIASPH2BF8S  ymm0{k7}{z}, zmm3, word [rax]{1to32}                 }
testcase    { 0x62, 0xf5, 0x74, 0x08, 0x18, 0xc7                                           }, { {evex} VCVTBIASPH2HF8  xmm0, xmm1, xmm7                                      }
testcase    { 0x62, 0xf5, 0x74, 0x08, 0x18, 0x00                                           }, { {evex} VCVTBIASPH2HF8  xmm0, xmm1, oword [rax]                               }
testcase    { 0x62, 0xf5, 0x74, 0x18, 0x18, 0x00                                           }, { {evex} VCVTBIASPH2HF8  xmm0, xmm1, word [rax]{1to8}                          }
testcase    { 0x62, 0xf5, 0x74, 0x0f, 0x18, 0xc7                                           }, { {evex} VCVTBIASPH2HF8  xmm0{k7}, xmm1, xmm7                                  }
testcase    { 0x62, 0xf5, 0x74, 0x0f, 0x18, 0x00                                           }, { {evex} VCVTBIASPH2HF8  xmm0{k7}, xmm1, oword [rax]                           }
testcase    { 0x62, 0xf5, 0x74, 0x1f, 0x18, 0x00                                           }, { {evex} VCVTBIASPH2HF8  xmm0{k7}, xmm1, word [rax]{1to8}                      }
testcase    { 0x62, 0xf5, 0x74, 0x8f, 0x18, 0xc7                                           }, { {evex} VCVTBIASPH2HF8  xmm0{k7}{z}, xmm1, xmm7                               }
testcase    { 0x62, 0xf5, 0x74, 0x8f, 0x18, 0x00                                           }, { {evex} VCVTBIASPH2HF8  xmm0{k7}{z}, xmm1, oword [rax]                        }
testcase    { 0x62, 0xf5, 0x74, 0x9f, 0x18, 0x00                                           }, { {evex} VCVTBIASPH2HF8  xmm0{k7}{z}, xmm1, word [rax]{1to8}                   }
testcase    { 0x62, 0xd5, 0x74, 0x28, 0x18, 0xc7                                           }, { {evex} VCVTBIASPH2HF8  xmm0, ymm1, ymm15                                     }
testcase    { 0x62, 0xf5, 0x74, 0x28, 0x18, 0x00                                           }, { {evex} VCVTBIASPH2HF8  xmm0, ymm1, yword [rax]                               }
testcase    { 0x62, 0xf5, 0x74, 0x38, 0x18, 0x00                                           }, { {evex} VCVTBIASPH2HF8  xmm0, ymm1, word [rax]{1to16}                         }
testcase    { 0x62, 0xd5, 0x74, 0x2f, 0x18, 0xc7                                           }, { {evex} VCVTBIASPH2HF8  xmm0{k7}, ymm1, ymm15                                 }
testcase    { 0x62, 0xf5, 0x74, 0x2f, 0x18, 0x00                                           }, { {evex} VCVTBIASPH2HF8  xmm0{k7}, ymm1, yword [rax]                           }
testcase    { 0x62, 0xf5, 0x74, 0x3f, 0x18, 0x00                                           }, { {evex} VCVTBIASPH2HF8  xmm0{k7}, ymm1, word [rax]{1to16}                     }
testcase    { 0x62, 0xd5, 0x74, 0xaf, 0x18, 0xc7                                           }, { {evex} VCVTBIASPH2HF8  xmm0{k7}{z}, ymm1, ymm15                              }
testcase    { 0x62, 0xf5, 0x74, 0xaf, 0x18, 0x00                                           }, { {evex} VCVTBIASPH2HF8  xmm0{k7}{z}, ymm1, yword [rax]                        }
testcase    { 0x62, 0xf5, 0x74, 0xbf, 0x18, 0x00                                           }, { {evex} VCVTBIASPH2HF8  xmm0{k7}{z}, ymm1, word [rax]{1to16}                  }
testcase    { 0x62, 0xd5, 0x64, 0x48, 0x18, 0xc7                                           }, { {evex} VCVTBIASPH2HF8  ymm0, zmm3, zmm15                                     }
testcase    { 0x62, 0xf5, 0x64, 0x48, 0x18, 0x00                                           }, { {evex} VCVTBIASPH2HF8  ymm0, zmm3, zword [rax]                               }
testcase    { 0x62, 0xf5, 0x64, 0x58, 0x18, 0x00                                           }, { {evex} VCVTBIASPH2HF8  ymm0, zmm3, word [rax]{1to32}                         }
testcase    { 0x62, 0xd5, 0x64, 0x4f, 0x18, 0xc7                                           }, { {evex} VCVTBIASPH2HF8  ymm0{k7}, zmm3, zmm15                                 }
testcase    { 0x62, 0xf5, 0x64, 0x4f, 0x18, 0x00                                           }, { {evex} VCVTBIASPH2HF8  ymm0{k7}, zmm3, zword [rax]                           }
testcase    { 0x62, 0xf5, 0x64, 0x5f, 0x18, 0x00                                           }, { {evex} VCVTBIASPH2HF8  ymm0{k7}, zmm3, word [rax]{1to32}                     }
testcase    { 0x62, 0xd5, 0x64, 0xcf, 0x18, 0xc7                                           }, { {evex} VCVTBIASPH2HF8  ymm0{k7}{z}, zmm3, zmm15                              }
testcase    { 0x62, 0xf5, 0x64, 0xcf, 0x18, 0x00                                           }, { {evex} VCVTBIASPH2HF8  ymm0{k7}{z}, zmm3, zword [rax]                        }
testcase    { 0x62, 0xf5, 0x64, 0xdf, 0x18, 0x00                                           }, { {evex} VCVTBIASPH2HF8  ymm0{k7}{z}, zmm3, word [rax]{1to32}                  }
testcase    { 0x62, 0xf5, 0x74, 0x08, 0x1b, 0xc7                                           }, { {evex} VCVTBIASPH2HF8S  xmm0, xmm1, xmm7                                     }
testcase    { 0x62, 0xf5, 0x74, 0x08, 0x1b, 0x00                                           }, { {evex} VCVTBIASPH2HF8S  xmm0, xmm1, oword [rax]                              }
testcase    { 0x62, 0xf5, 0x74, 0x18, 0x1b, 0x00                                           }, { {evex} VCVTBIASPH2HF8S  xmm0, xmm1, word [rax]{1to8}                         }
testcase    { 0x62, 0xf5, 0x74, 0x0f, 0x1b, 0xc7                                           }, { {evex} VCVTBIASPH2HF8S  xmm0{k7}, xmm1, xmm7                                 }
testcase    { 0x62, 0xf5, 0x74, 0x0f, 0x1b, 0x00                                           }, { {evex} VCVTBIASPH2HF8S  xmm0{k7}, xmm1, oword [rax]                          }
testcase    { 0x62, 0xf5, 0x74, 0x1f, 0x1b, 0x00                                           }, { {evex} VCVTBIASPH2HF8S  xmm0{k7}, xmm1, word [rax]{1to8}                     }
testcase    { 0x62, 0xf5, 0x74, 0x8f, 0x1b, 0xc7                                           }, { {evex} VCVTBIASPH2HF8S  xmm0{k7}{z}, xmm1, xmm7                              }
testcase    { 0x62, 0xf5, 0x74, 0x8f, 0x1b, 0x00                                           }, { {evex} VCVTBIASPH2HF8S  xmm0{k7}{z}, xmm1, oword [rax]                       }
testcase    { 0x62, 0xf5, 0x74, 0x9f, 0x1b, 0x00                                           }, { {evex} VCVTBIASPH2HF8S  xmm0{k7}{z}, xmm1, word [rax]{1to8}                  }
testcase    { 0x62, 0xd5, 0x74, 0x28, 0x1b, 0xc7                                           }, { {evex} VCVTBIASPH2HF8S  xmm0, ymm1, ymm15                                    }
testcase    { 0x62, 0xf5, 0x74, 0x28, 0x1b, 0x00                                           }, { {evex} VCVTBIASPH2HF8S  xmm0, ymm1, yword [rax]                              }
testcase    { 0x62, 0xf5, 0x74, 0x38, 0x1b, 0x00                                           }, { {evex} VCVTBIASPH2HF8S  xmm0, ymm1, word [rax]{1to16}                        }
testcase    { 0x62, 0xd5, 0x74, 0x2f, 0x1b, 0xc7                                           }, { {evex} VCVTBIASPH2HF8S  xmm0{k7}, ymm1, ymm15                                }
testcase    { 0x62, 0xf5, 0x74, 0x2f, 0x1b, 0x00                                           }, { {evex} VCVTBIASPH2HF8S  xmm0{k7}, ymm1, yword [rax]                          }
testcase    { 0x62, 0xf5, 0x74, 0x3f, 0x1b, 0x00                                           }, { {evex} VCVTBIASPH2HF8S  xmm0{k7}, ymm1, word [rax]{1to16}                    }
testcase    { 0x62, 0xd5, 0x74, 0xaf, 0x1b, 0xc7                                           }, { {evex} VCVTBIASPH2HF8S  xmm0{k7}{z}, ymm1, ymm15                             }
testcase    { 0x62, 0xf5, 0x74, 0xaf, 0x1b, 0x00                                           }, { {evex} VCVTBIASPH2HF8S  xmm0{k7}{z}, ymm1, yword [rax]                       }
testcase    { 0x62, 0xf5, 0x74, 0xbf, 0x1b, 0x00                                           }, { {evex} VCVTBIASPH2HF8S  xmm0{k7}{z}, ymm1, word [rax]{1to16}                 }
testcase    { 0x62, 0xd5, 0x64, 0x48, 0x1b, 0xc7                                           }, { {evex} VCVTBIASPH2HF8S  ymm0, zmm3, zmm15                                    }
testcase    { 0x62, 0xf5, 0x64, 0x48, 0x1b, 0x00                                           }, { {evex} VCVTBIASPH2HF8S  ymm0, zmm3, zword [rax]                              }
testcase    { 0x62, 0xf5, 0x64, 0x58, 0x1b, 0x00                                           }, { {evex} VCVTBIASPH2HF8S  ymm0, zmm3, word [rax]{1to32}                        }
testcase    { 0x62, 0xd5, 0x64, 0x4f, 0x1b, 0xc7                                           }, { {evex} VCVTBIASPH2HF8S  ymm0{k7}, zmm3, zmm15                                }
testcase    { 0x62, 0xf5, 0x64, 0x4f, 0x1b, 0x00                                           }, { {evex} VCVTBIASPH2HF8S  ymm0{k7}, zmm3, zword [rax]                          }
testcase    { 0x62, 0xf5, 0x64, 0x5f, 0x1b, 0x00                                           }, { {evex} VCVTBIASPH2HF8S  ymm0{k7}, zmm3, word [rax]{1to32}                    }
testcase    { 0x62, 0xd5, 0x64, 0xcf, 0x1b, 0xc7                                           }, { {evex} VCVTBIASPH2HF8S  ymm0{k7}{z}, zmm3, zmm15                             }
testcase    { 0x62, 0xf5, 0x64, 0xcf, 0x1b, 0x00                                           }, { {evex} VCVTBIASPH2HF8S  ymm0{k7}{z}, zmm3, zword [rax]                       }
testcase    { 0x62, 0xf5, 0x64, 0xdf, 0x1b, 0x00                                           }, { {evex} VCVTBIASPH2HF8S  ymm0{k7}{z}, zmm3, word [rax]{1to32}                 }
testcase    { 0x62, 0xf5, 0x7f, 0x08, 0x1e, 0xc4                                           }, { {evex} VCVTHF82PH  xmm0, xmm4                                                }
testcase    { 0x62, 0xf5, 0x7f, 0x08, 0x1e, 0x00                                           }, { {evex} VCVTHF82PH  xmm0, qword [rax]                                         }
testcase    { 0x62, 0xf5, 0x7f, 0x0f, 0x1e, 0xc4                                           }, { {evex} VCVTHF82PH  xmm0{k7}, xmm4                                            }
testcase    { 0x62, 0xf5, 0x7f, 0x0f, 0x1e, 0x00                                           }, { {evex} VCVTHF82PH  xmm0{k7}, qword [rax]                                     }
testcase    { 0x62, 0xf5, 0x7f, 0x8f, 0x1e, 0xc4                                           }, { {evex} VCVTHF82PH  xmm0{k7}{z}, xmm4                                         }
testcase    { 0x62, 0xf5, 0x7f, 0x8f, 0x1e, 0x00                                           }, { {evex} VCVTHF82PH  xmm0{k7}{z}, qword [rax]                                  }
testcase    { 0x62, 0xf5, 0x7f, 0x28, 0x1e, 0xc5                                           }, { {evex} VCVTHF82PH  ymm0, xmm5                                                }
testcase    { 0x62, 0xb5, 0x7f, 0x28, 0x1e, 0x04, 0xf0                                     }, { {evex} VCVTHF82PH  ymm0, oword [rax+r14*8]                                   }
testcase    { 0x62, 0xf5, 0x7f, 0x2f, 0x1e, 0xc5                                           }, { {evex} VCVTHF82PH  ymm0{k7}, xmm5                                            }
testcase    { 0x62, 0xb5, 0x7f, 0x2f, 0x1e, 0x04, 0xf0                                     }, { {evex} VCVTHF82PH  ymm0{k7}, oword [rax+r14*8]                               }
testcase    { 0x62, 0xf5, 0x7f, 0xaf, 0x1e, 0xc5                                           }, { {evex} VCVTHF82PH  ymm0{k7}{z}, xmm5                                         }
testcase    { 0x62, 0xb5, 0x7f, 0xaf, 0x1e, 0x04, 0xf0                                     }, { {evex} VCVTHF82PH  ymm0{k7}{z}, oword [rax+r14*8]                            }
testcase    { 0x62, 0xd5, 0x7f, 0x48, 0x1e, 0xc7                                           }, { {evex} VCVTHF82PH  zmm0, ymm15                                               }
testcase    { 0x62, 0xd5, 0x7f, 0x4f, 0x1e, 0xc7                                           }, { {evex} VCVTHF82PH  zmm0{k7}, ymm15                                           }
testcase    { 0x62, 0xd5, 0x7f, 0xcf, 0x1e, 0xc7                                           }, { {evex} VCVTHF82PH  zmm0{k7}{z}, ymm15                                        }
testcase        {  0x62, 0xf5, 0x7c, 0x08, 0x69, 0xca                                        }, {  VCVTPH2IBS xmm1, xmm2  }
testcase        {  0x62, 0xf5, 0x7c, 0x28, 0x69, 0xca                                        }, {  VCVTPH2IBS ymm1, ymm2  }
testcase        {  0x62, 0xf5, 0x7c, 0x48, 0x69, 0xca                                        }, {  VCVTPH2IBS zmm1, zmm2  }
testcase        {  0x62, 0xf5, 0x7c, 0x08, 0x6b, 0xca                                        }, {  VCVTPH2IUBS xmm1, xmm2  }
testcase        {  0x62, 0xf5, 0x7c, 0x28, 0x6b, 0xca                                        }, {  VCVTPH2IUBS ymm1, ymm2  }
testcase        {  0x62, 0xf5, 0x7c, 0x48, 0x6b, 0xca                                        }, {  VCVTPH2IUBS zmm1, zmm2  }
testcase        {  0x62, 0xf5, 0x7d, 0x08, 0x69, 0xca                                        }, {  VCVTPS2IBS xmm1, xmm2  }
testcase        {  0x62, 0xf5, 0x7d, 0x28, 0x69, 0xca                                        }, {  VCVTPS2IBS ymm1, ymm2  }
testcase        {  0x62, 0xf5, 0x7d, 0x48, 0x69, 0xca                                        }, {  VCVTPS2IBS zmm1, zmm2  }
testcase        {  0x62, 0xf5, 0x7d, 0x08, 0x6b, 0xca                                        }, {  VCVTPS2IUBS xmm1, xmm2  }
testcase        {  0x62, 0xf5, 0x7d, 0x28, 0x6b, 0xca                                        }, {  VCVTPS2IUBS ymm1, ymm2  }
testcase        {  0x62, 0xf5, 0x7d, 0x48, 0x6b, 0xca                                        }, {  VCVTPS2IUBS zmm1, zmm2  }
testcase        {  0x62, 0xf5, 0x7f, 0x08, 0x68, 0xca                                        }, {  VCVTTBF162IBS xmm1, xmm2  }
testcase        {  0x62, 0xf5, 0x7f, 0x28, 0x68, 0xca                                        }, {  VCVTTBF162IBS ymm1, ymm2  }
testcase        {  0x62, 0xf5, 0x7f, 0x48, 0x68, 0xca                                        }, {  VCVTTBF162IBS zmm1, zmm2  }
testcase        {  0x62, 0xf5, 0x7f, 0x08, 0x6a, 0xca                                        }, {  VCVTTBF162IUBS xmm1, xmm2  }
testcase        {  0x62, 0xf5, 0x7f, 0x28, 0x6a, 0xca                                        }, {  VCVTTBF162IUBS ymm1, ymm2  }
testcase        {  0x62, 0xf5, 0x7f, 0x48, 0x6a, 0xca                                        }, {  VCVTTBF162IUBS zmm1, zmm2  }
testcase        {  0x62, 0xf5, 0xfc, 0x08, 0x6d, 0xca                                        }, {  VCVTTPD2DQS xmm1, xmm2  }
testcase        {  0x62, 0xf5, 0xfc, 0x28, 0x6d, 0xca                                        }, {  VCVTTPD2DQS xmm1, ymm2  }
testcase        {  0x62, 0xf5, 0xfc, 0x48, 0x6d, 0xca                                        }, {  VCVTTPD2DQS ymm1, zmm2  }
testcase        {  0x62, 0xf5, 0xfd, 0x08, 0x6d, 0xca                                        }, {  VCVTTPD2QQS xmm1, xmm2  }
testcase        {  0x62, 0xf5, 0xfd, 0x28, 0x6d, 0xca                                        }, {  VCVTTPD2QQS ymm1, ymm2  }
testcase        {  0x62, 0xf5, 0xfd, 0x48, 0x6d, 0xca                                        }, {  VCVTTPD2QQS zmm1, zmm2  }
testcase        {  0x62, 0xf5, 0xfc, 0x08, 0x6c, 0xca                                        }, {  VCVTTPD2UDQS xmm1, xmm2  }
testcase        {  0x62, 0xf5, 0xfc, 0x28, 0x6c, 0xca                                        }, {  VCVTTPD2UDQS xmm1, ymm2  }
testcase        {  0x62, 0xf5, 0xfc, 0x48, 0x6c, 0xca                                        }, {  VCVTTPD2UDQS ymm1, zmm2  }
testcase        {  0x62, 0xf5, 0xfd, 0x08, 0x6c, 0xca                                        }, {  VCVTTPD2UQQS xmm1, xmm2  }
testcase        {  0x62, 0xf5, 0xfd, 0x28, 0x6c, 0xca                                        }, {  VCVTTPD2UQQS ymm1, ymm2  }
testcase        {  0x62, 0xf5, 0xfd, 0x48, 0x6c, 0xca                                        }, {  VCVTTPD2UQQS zmm1, zmm2  }
testcase        {  0x62, 0xf5, 0x7c, 0x08, 0x68, 0xca                                        }, {  VCVTTPH2IBS xmm1, xmm2  }
testcase        {  0x62, 0xf5, 0x7c, 0x28, 0x68, 0xca                                        }, {  VCVTTPH2IBS ymm1, ymm2  }
testcase        {  0x62, 0xf5, 0x7c, 0x48, 0x68, 0xca                                        }, {  VCVTTPH2IBS zmm1, zmm2  }
testcase        {  0x62, 0xf5, 0x7c, 0x08, 0x6a, 0xca                                        }, {  VCVTTPH2IUBS xmm1, xmm2  }
testcase        {  0x62, 0xf5, 0x7c, 0x28, 0x6a, 0xca                                        }, {  VCVTTPH2IUBS ymm1, ymm2  }
testcase        {  0x62, 0xf5, 0x7c, 0x48, 0x6a, 0xca                                        }, {  VCVTTPH2IUBS zmm1, zmm2  }
testcase        {  0x62, 0xf5, 0x7c, 0x08, 0x6d, 0xca                                        }, {  VCVTTPS2DQS xmm1, xmm2  }
testcase        {  0x62, 0xf5, 0x7c, 0x28, 0x6d, 0xca                                        }, {  VCVTTPS2DQS ymm1, ymm2  }
testcase        {  0x62, 0xf5, 0x7c, 0x48, 0x6d, 0xca                                        }, {  VCVTTPS2DQS zmm1, zmm2  }
testcase        {  0x62, 0xf5, 0x7d, 0x08, 0x68, 0xca                                        }, {  VCVTTPS2IBS xmm1, xmm2  }
testcase        {  0x62, 0xf5, 0x7d, 0x28, 0x68, 0xca                                        }, {  VCVTTPS2IBS ymm1, ymm2  }
testcase        {  0x62, 0xf5, 0x7d, 0x48, 0x68, 0xca                                        }, {  VCVTTPS2IBS zmm1, zmm2  }
testcase        {  0x62, 0xf5, 0x7d, 0x08, 0x6a, 0xca                                        }, {  VCVTTPS2IUBS xmm1, xmm2  }
testcase        {  0x62, 0xf5, 0x7d, 0x28, 0x6a, 0xca                                        }, {  VCVTTPS2IUBS ymm1, ymm2  }
testcase        {  0x62, 0xf5, 0x7d, 0x48, 0x6a, 0xca                                        }, {  VCVTTPS2IUBS zmm1, zmm2  }
testcase        {  0x62, 0xf5, 0x7d, 0x08, 0x6d, 0xca                                        }, {  VCVTTPS2QQS xmm1, xmm2  }
testcase        {  0x62, 0xf5, 0x7d, 0x28, 0x6d, 0xca                                        }, {  VCVTTPS2QQS ymm1, xmm2  }
testcase        {  0x62, 0xf5, 0x7d, 0x48, 0x6d, 0xca                                        }, {  VCVTTPS2QQS zmm1, ymm2  }
testcase        {  0x62, 0xf5, 0x7c, 0x08, 0x6c, 0xca                                        }, {  VCVTTPS2UDQS xmm1, xmm2  }
testcase        {  0x62, 0xf5, 0x7c, 0x28, 0x6c, 0xca                                        }, {  VCVTTPS2UDQS ymm1, ymm2  }
testcase        {  0x62, 0xf5, 0x7c, 0x48, 0x6c, 0xca                                        }, {  VCVTTPS2UDQS zmm1, zmm2  }
testcase        {  0x62, 0xf5, 0x7d, 0x08, 0x6c, 0xca                                        }, {  VCVTTPS2UQQS xmm1, xmm2  }
testcase        {  0x62, 0xf5, 0x7d, 0x28, 0x6c, 0xca                                        }, {  VCVTTPS2UQQS ymm1, xmm2  }
testcase        {  0x62, 0xf5, 0x7d, 0x48, 0x6c, 0xca                                        }, {  VCVTTPS2UQQS zmm1, ymm2  }
testcase        {  0x62, 0xf5, 0x6d, 0x08, 0x5e, 0xcb                                        }, {  VDIVBF16 xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xb5, 0x6d, 0x08, 0x5e, 0x4c, 0xf0, 0x01                            }, {  VDIVBF16 xmm1, xmm2, [rax+r14*8+0x10]  }
testcase        {  0x62, 0xf5, 0x6d, 0x28, 0x5e, 0xcb                                        }, {  VDIVBF16 ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf5, 0x6d, 0x48, 0x5e, 0xcb                                        }, {  VDIVBF16 zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf2, 0x6c, 0x08, 0x52, 0xcb                                        }, {  VDPPHPS xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xf2, 0x6c, 0x28, 0x52, 0xcb                                        }, {  VDPPHPS ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf2, 0x6c, 0x48, 0x52, 0xcb                                        }, {  VDPPHPS zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf6, 0x6c, 0x08, 0x98, 0xcb                                        }, {  VFMADD132BF16 xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xb6, 0x6c, 0x08, 0x98, 0x4c, 0xf0, 0x01                            }, {  VFMADD132BF16 xmm1, xmm2, [rax+r14*8+0x10]  }
testcase        {  0x62, 0xf6, 0x6c, 0x28, 0x98, 0xcb                                        }, {  VFMADD132BF16 ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf6, 0x6c, 0x48, 0x98, 0xcb                                        }, {  VFMADD132BF16 zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf6, 0x6c, 0x08, 0xa8, 0xcb                                        }, {  VFMADD213BF16 xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xb6, 0x6c, 0x08, 0xa8, 0x4c, 0xf0, 0x01                            }, {  VFMADD213BF16 xmm1, xmm2, [rax+r14*8+0x10]  }
testcase        {  0x62, 0xf6, 0x6c, 0x28, 0xa8, 0xcb                                        }, {  VFMADD213BF16 ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf6, 0x6c, 0x48, 0xa8, 0xcb                                        }, {  VFMADD213BF16 zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf6, 0x6c, 0x08, 0xb8, 0xcb                                        }, {  VFMADD231BF16 xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xb6, 0x6c, 0x08, 0xb8, 0x4c, 0xf0, 0x01                            }, {  VFMADD231BF16 xmm1, xmm2, [rax+r14*8+0x10]  }
testcase        {  0x62, 0xf6, 0x6c, 0x28, 0xb8, 0xcb                                        }, {  VFMADD231BF16 ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf6, 0x6c, 0x48, 0xb8, 0xcb                                        }, {  VFMADD231BF16 zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf6, 0x6c, 0x08, 0x9a, 0xcb                                        }, {  VFMSUB132BF16 xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xb6, 0x6c, 0x08, 0x9a, 0x4c, 0xf0, 0x01                            }, {  VFMSUB132BF16 xmm1, xmm2, [rax+r14*8+0x10]  }
testcase        {  0x62, 0xf6, 0x6c, 0x28, 0x9a, 0xcb                                        }, {  VFMSUB132BF16 ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf6, 0x6c, 0x48, 0x9a, 0xcb                                        }, {  VFMSUB132BF16 zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf6, 0x6c, 0x08, 0xaa, 0xcb                                        }, {  VFMSUB213BF16 xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xb6, 0x6c, 0x08, 0xaa, 0x4c, 0xf0, 0x01                            }, {  VFMSUB213BF16 xmm1, xmm2, [rax+r14*8+0x10]  }
testcase        {  0x62, 0xf6, 0x6c, 0x28, 0xaa, 0xcb                                        }, {  VFMSUB213BF16 ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf6, 0x6c, 0x48, 0xaa, 0xcb                                        }, {  VFMSUB213BF16 zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf6, 0x6c, 0x08, 0xba, 0xcb                                        }, {  VFMSUB231BF16 xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xb6, 0x6c, 0x08, 0xba, 0x4c, 0xf0, 0x01                            }, {  VFMSUB231BF16 xmm1, xmm2, [rax+r14*8+0x10]  }
testcase        {  0x62, 0xf6, 0x6c, 0x28, 0xba, 0xcb                                        }, {  VFMSUB231BF16 ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf6, 0x6c, 0x48, 0xba, 0xcb                                        }, {  VFMSUB231BF16 zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf6, 0x6c, 0x08, 0x9c, 0xcb                                        }, {  VFNMADD132BF16 xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xb6, 0x6c, 0x08, 0x9c, 0x4c, 0xf0, 0x01                            }, {  VFNMADD132BF16 xmm1, xmm2, [rax+r14*8+0x10]  }
testcase        {  0x62, 0xf6, 0x6c, 0x28, 0x9c, 0xcb                                        }, {  VFNMADD132BF16 ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf6, 0x6c, 0x48, 0x9c, 0xcb                                        }, {  VFNMADD132BF16 zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf6, 0x6c, 0x08, 0xac, 0xcb                                        }, {  VFNMADD213BF16 xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xb6, 0x6c, 0x08, 0xac, 0x4c, 0xf0, 0x01                            }, {  VFNMADD213BF16 xmm1, xmm2, [rax+r14*8+0x10]  }
testcase        {  0x62, 0xf6, 0x6c, 0x28, 0xac, 0xcb                                        }, {  VFNMADD213BF16 ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf6, 0x6c, 0x48, 0xac, 0xcb                                        }, {  VFNMADD213BF16 zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf6, 0x6c, 0x08, 0xbc, 0xcb                                        }, {  VFNMADD231BF16 xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xb6, 0x6c, 0x08, 0xbc, 0x4c, 0xf0, 0x01                            }, {  VFNMADD231BF16 xmm1, xmm2, [rax+r14*8+0x10]  }
testcase        {  0x62, 0xf6, 0x6c, 0x28, 0xbc, 0xcb                                        }, {  VFNMADD231BF16 ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf6, 0x6c, 0x48, 0xbc, 0xcb                                        }, {  VFNMADD231BF16 zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf6, 0x6c, 0x08, 0x9e, 0xcb                                        }, {  VFNMSUB132BF16 xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xb6, 0x6c, 0x08, 0x9e, 0x4c, 0xf0, 0x01                            }, {  VFNMSUB132BF16 xmm1, xmm2, [rax+r14*8+0x10]  }
testcase        {  0x62, 0xf6, 0x6c, 0x28, 0x9e, 0xcb                                        }, {  VFNMSUB132BF16 ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf6, 0x6c, 0x48, 0x9e, 0xcb                                        }, {  VFNMSUB132BF16 zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf6, 0x6c, 0x08, 0xae, 0xcb                                        }, {  VFNMSUB213BF16 xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xb6, 0x6c, 0x08, 0xae, 0x4c, 0xf0, 0x01                            }, {  VFNMSUB213BF16 xmm1, xmm2, [rax+r14*8+0x10]  }
testcase        {  0x62, 0xf6, 0x6c, 0x28, 0xae, 0xcb                                        }, {  VFNMSUB213BF16 ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf6, 0x6c, 0x48, 0xae, 0xcb                                        }, {  VFNMSUB213BF16 zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf6, 0x6c, 0x08, 0xbe, 0xcb                                        }, {  VFNMSUB231BF16 xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xb6, 0x6c, 0x08, 0xbe, 0x4c, 0xf0, 0x01                            }, {  VFNMSUB231BF16 xmm1, xmm2, [rax+r14*8+0x10]  }
testcase        {  0x62, 0xf6, 0x6c, 0x28, 0xbe, 0xcb                                        }, {  VFNMSUB231BF16 ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf6, 0x6c, 0x48, 0xbe, 0xcb                                        }, {  VFNMSUB231BF16 zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf3, 0x7f, 0x08, 0x66, 0xc9, 0x10                                  }, {  VFPCLASSBF16 k1, xmm1, 0x10  }
testcase        {  0x62, 0xf3, 0x7f, 0x28, 0x66, 0xc9, 0x10                                  }, {  VFPCLASSBF16 k1, ymm1, 0x10  }
testcase        {  0x62, 0xf3, 0x7f, 0x48, 0x66, 0xc9, 0x10                                  }, {  VFPCLASSBF16 k1, zmm1, 0x10  }
testcase        {  0x62, 0xf6, 0x7c, 0x08, 0x42, 0xca                                        }, {  VGETEXPBF16 xmm1, xmm2  }
testcase        {  0x62, 0xf6, 0x7c, 0x28, 0x42, 0xca                                        }, {  VGETEXPBF16 ymm1, ymm2  }
testcase        {  0x62, 0xf6, 0x7c, 0x48, 0x42, 0xca                                        }, {  VGETEXPBF16 zmm1, zmm2  }
testcase        {  0x62, 0xf3, 0x7f, 0x08, 0x26, 0xca, 0x10                                  }, {  VGETMANTBF16 xmm1, xmm2, 0x10  }
testcase        {  0x62, 0xf3, 0x7f, 0x28, 0x26, 0xca, 0x10                                  }, {  VGETMANTBF16 ymm1, ymm2, 0x10  }
testcase        {  0x62, 0xf3, 0x7f, 0x48, 0x26, 0xca, 0x10                                  }, {  VGETMANTBF16 zmm1, zmm2, 0x10  }
testcase        {  0x62, 0xf5, 0x6d, 0x08, 0x5f, 0xcb                                        }, {  VMAXBF16 xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xb5, 0x6d, 0x08, 0x5f, 0x4c, 0xf0, 0x01                            }, {  VMAXBF16 xmm1, xmm2, [rax+r14*8+0x10]  }
testcase        {  0x62, 0xf5, 0x6d, 0x28, 0x5f, 0xcb                                        }, {  VMAXBF16 ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf5, 0x6d, 0x48, 0x5f, 0xcb                                        }, {  VMAXBF16 zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf5, 0x6d, 0x08, 0x5d, 0xcb                                        }, {  VMINBF16 xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xb5, 0x6d, 0x08, 0x5d, 0x4c, 0xf0, 0x01                            }, {  VMINBF16 xmm1, xmm2, [rax+r14*8+0x10]  }
testcase        {  0x62, 0xf5, 0x6d, 0x28, 0x5d, 0xcb                                        }, {  VMINBF16 ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf5, 0x6d, 0x48, 0x5d, 0xcb                                        }, {  VMINBF16 zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf3, 0x6f, 0x08, 0x52, 0xcb, 0x10                                  }, {  VMINMAXBF16 xmm1, xmm2, xmm3, 0x10  }
testcase        {  0x62, 0xf3, 0x6f, 0x28, 0x52, 0xcb, 0x10                                  }, {  VMINMAXBF16 ymm1, ymm2, ymm3, 0x10  }
testcase        {  0x62, 0xf3, 0x6f, 0x48, 0x52, 0xcb, 0x10                                  }, {  VMINMAXBF16 zmm1, zmm2, zmm3, 0x10  }
testcase        {  0x62, 0xf3, 0xed, 0x08, 0x52, 0xcb, 0x10                                  }, {  VMINMAXPD xmm1, xmm2, xmm3, 0x10  }
testcase        {  0x62, 0xf3, 0xed, 0x28, 0x52, 0xcb, 0x10                                  }, {  VMINMAXPD ymm1, ymm2, ymm3, 0x10  }
testcase        {  0x62, 0xf3, 0xed, 0x48, 0x52, 0xcb, 0x10                                  }, {  VMINMAXPD zmm1, zmm2, zmm3, 0x10  }
testcase        {  0x62, 0xf3, 0x6c, 0x08, 0x52, 0xcb, 0x10                                  }, {  VMINMAXPH xmm1, xmm2, xmm3, 0x10  }
testcase        {  0x62, 0xf3, 0x6c, 0x28, 0x52, 0xcb, 0x10                                  }, {  VMINMAXPH ymm1, ymm2, ymm3, 0x10  }
testcase        {  0x62, 0xf3, 0x6c, 0x48, 0x52, 0xcb, 0x10                                  }, {  VMINMAXPH zmm1, zmm2, zmm3, 0x10  }
testcase        {  0x62, 0xf3, 0x6d, 0x08, 0x52, 0xcb, 0x10                                  }, {  VMINMAXPS xmm1, xmm2, xmm3, 0x10  }
testcase        {  0x62, 0xf3, 0x6d, 0x28, 0x52, 0xcb, 0x10                                  }, {  VMINMAXPS ymm1, ymm2, ymm3, 0x10  }
testcase        {  0x62, 0xf3, 0x6d, 0x48, 0x52, 0xcb, 0x10                                  }, {  VMINMAXPS zmm1, zmm2, zmm3, 0x10  }
testcase        {  0x62, 0xf3, 0xed, 0x08, 0x53, 0xcb, 0x10                                  }, {  VMINMAXSD xmm1, xmm2, xmm3, 0x10  }
testcase        {  0x62, 0xf3, 0x6c, 0x08, 0x53, 0xcb, 0x10                                  }, {  VMINMAXSH xmm1, xmm2, xmm3, 0x10  }
testcase        {  0x62, 0xf3, 0x6d, 0x08, 0x53, 0xcb, 0x10                                  }, {  VMINMAXSS xmm1, xmm2, xmm3, 0x10  }

testcase        {  0x62, 0xf1, 0x7e, 0x08, 0x7e, 0xca                                        }, {  VMOVD xmm1, xmm2  }
testcase        {  0x62, 0xf5, 0x7e, 0x08, 0x6e, 0xca                                        }, {  VMOVW xmm1, xmm2  }
testcase        { 0xc5, 0xf9, 0x6e, 0xc8                                                     }, { {vex} VMOVD  xmm1, eax                                                       }
testcase        { 0x67, 0xc5, 0xf9, 0x6e, 0x08                                               }, { {vex} VMOVD  xmm1, dword [eax]                                               }
testcase        { 0xc5, 0xf9, 0x7e, 0xc8                                                     }, { {vex} VMOVD  eax, xmm1                                                       }
testcase        { 0x67, 0xc5, 0xf9, 0x7e, 0x08                                               }, { {vex} VMOVD  dword [eax], xmm1                                               }
testcase        { 0x62, 0xf1, 0x7d, 0x08, 0x6e, 0xc8                                         }, { {evex} VMOVD  xmm1, eax                                                      }
testcase        { 0x67, 0x62, 0xf1, 0x7d, 0x08, 0x6e, 0x08                                   }, { {evex} VMOVD  xmm1, dword [eax]                                              }
testcase        { 0x62, 0xf1, 0x7d, 0x08, 0x7e, 0xc8                                         }, { {evex} VMOVD  eax, xmm1                                                      }
testcase        { 0x67, 0x62, 0xf1, 0x7d, 0x08, 0x7e, 0x08                                   }, { {evex} VMOVD  dword [eax], xmm1                                              }
testcase        { 0x67, 0x62, 0xf1, 0x7d, 0x08, 0x6e, 0x08                                   }, { {evex} VMOVD  xmm1, dword [eax]                                              }
testcase        { 0x67, 0x62, 0xf1, 0x7d, 0x08, 0x7e, 0x08                                   }, { {evex} VMOVD  dword [eax], xmm1                                              }
testcase        { 0x67, 0x62, 0xf5, 0x7d, 0x08, 0x6e, 0x00                                   }, { {evex} VMOVW  xmm0, word [eax]                                               }
testcase        { 0x67, 0x62, 0xf5, 0x7d, 0x08, 0x7e, 0x08                                   }, { {evex} VMOVW  word [eax], xmm1                                               }
testcase        { 0x62, 0xf5, 0x7d, 0x08, 0x6e, 0x08                                         }, { {evex} VMOVW  xmm1, word [rax]                                               }
testcase        { 0x62, 0xf5, 0x7d, 0x08, 0x7e, 0x08                                         }, { {evex} VMOVW  word [rax], xmm1                                               }

testcase        {  0xc4, 0xe3, 0x69, 0x42, 0xcb, 0x10                                        }, {  VMPSADBW xmm1, xmm2, xmm3, 0x10  }
testcase        {  0xc4, 0xe3, 0x6d, 0x42, 0xcb, 0x10                                        }, {  VMPSADBW ymm1, ymm2, ymm3, 0x10  }
testcase        {  0x62, 0xf3, 0x6e, 0x48, 0x42, 0xcb, 0x10                                  }, {  VMPSADBW zmm1, zmm2, zmm3, 0x10  }
testcase        {  0x62, 0xf5, 0x6d, 0x08, 0x59, 0xcb                                        }, {  VMULBF16 xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xb5, 0x6d, 0x08, 0x59, 0x4c, 0xf0, 0x01                            }, {  VMULBF16 xmm1, xmm2, [rax+r14*8+0x10]  }
testcase        {  0x62, 0xf5, 0x6d, 0x28, 0x59, 0xcb                                        }, {  VMULBF16 ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf5, 0x6d, 0x48, 0x59, 0xcb                                        }, {  VMULBF16 zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf2, 0x6f, 0x08, 0x50, 0xcb                                        }, {  VPDPBSSD xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xf2, 0x6f, 0x28, 0x50, 0xcb                                        }, {  VPDPBSSD ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf2, 0x6f, 0x48, 0x50, 0xcb                                        }, {  VPDPBSSD zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf2, 0x6f, 0x08, 0x51, 0xcb                                        }, {  VPDPBSSDS xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xf2, 0x6f, 0x28, 0x51, 0xcb                                        }, {  VPDPBSSDS ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf2, 0x6f, 0x48, 0x51, 0xcb                                        }, {  VPDPBSSDS zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf2, 0x6e, 0x08, 0x50, 0xcb                                        }, {  VPDPBSUD xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xf2, 0x6e, 0x28, 0x50, 0xcb                                        }, {  VPDPBSUD ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf2, 0x6e, 0x48, 0x50, 0xcb                                        }, {  VPDPBSUD zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf2, 0x6e, 0x08, 0x51, 0xcb                                        }, {  VPDPBSUDS xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xf2, 0x6e, 0x28, 0x51, 0xcb                                        }, {  VPDPBSUDS ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf2, 0x6e, 0x48, 0x51, 0xcb                                        }, {  VPDPBSUDS zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf2, 0x6c, 0x08, 0x50, 0xcb                                        }, {  VPDPBUUD xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xf2, 0x6c, 0x28, 0x50, 0xcb                                        }, {  VPDPBUUD ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf2, 0x6c, 0x48, 0x50, 0xcb                                        }, {  VPDPBUUD zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf2, 0x6c, 0x08, 0x51, 0xcb                                        }, {  VPDPBUUDS xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xf2, 0x6c, 0x28, 0x51, 0xcb                                        }, {  VPDPBUUDS ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf2, 0x6c, 0x48, 0x51, 0xcb                                        }, {  VPDPBUUDS zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf2, 0x76, 0x08, 0xd2, 0xc9                                        }, {  {evex} VPDPWSUD xmm1, xmm1, xmm1                                     }
testcase        {  0x62, 0xf2, 0x76, 0x8f, 0xd2, 0xc9                                        }, {  {evex} VPDPWSUD xmm1{k7}{z}, xmm1, xmm1                              }
testcase        {  0x62, 0xf2, 0x76, 0x8f, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD xmm1{k7}{z}, xmm1, oword [rax]                       }
testcase        {  0x62, 0xb2, 0x76, 0x8f, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUD xmm1{k7}{z}, xmm1, oword [rbp+r14*2+0x8]             }
testcase        {  0x62, 0xf2, 0x76, 0x9f, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD xmm1{k7}{z}, xmm1, dword [rax]{1to4}                 }
testcase        {  0x62, 0xb2, 0x76, 0x9f, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUD xmm1{k7}{z}, xmm1, dword [rbp+r14*2+0x8]{1to4}       }
testcase        {  0x62, 0xf2, 0x76, 0x28, 0xd2, 0xc9                                        }, {  {evex} VPDPWSUD ymm1, ymm1, ymm1                                     }
testcase        {  0x62, 0xf2, 0x76, 0x28, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD ymm1, ymm1, yword [rax]                              }
testcase        {  0x62, 0xb2, 0x76, 0x28, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUD ymm1, ymm1, yword [rbp+r14*2+0x8]                    }
testcase        {  0x62, 0xf2, 0x76, 0x38, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD ymm1, ymm1, dword [rax]{1to8}                        }
testcase        {  0x62, 0xb2, 0x76, 0x38, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUD ymm1, ymm1, dword [rbp+r14*2+0x8]{1to8}              }
testcase        {  0x62, 0xf2, 0x76, 0x08, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD xmm1, xmm1, oword [rax]                              }
testcase        {  0x62, 0xf2, 0x76, 0x2f, 0xd2, 0xc9                                        }, {  {evex} VPDPWSUD ymm1{k7}, ymm1, ymm1                                 }
testcase        {  0x62, 0xf2, 0x76, 0x2f, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD ymm1{k7}, ymm1, yword [rax]                          }
testcase        {  0x62, 0xb2, 0x76, 0x2f, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUD ymm1{k7}, ymm1, yword [rbp+r14*2+0x8]                }
testcase        {  0x62, 0xf2, 0x76, 0x3f, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD ymm1{k7}, ymm1, dword [rax]{1to8}                    }
testcase        {  0x62, 0xb2, 0x76, 0x3f, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUD ymm1{k7}, ymm1, dword [rbp+r14*2+0x8]{1to8}          }
testcase        {  0x62, 0xf2, 0x76, 0xaf, 0xd2, 0xc9                                        }, {  {evex} VPDPWSUD ymm1{k7}{z}, ymm1, ymm1                              }
testcase        {  0x62, 0xf2, 0x76, 0xaf, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD ymm1{k7}{z}, ymm1, yword [rax]                       }
testcase        {  0x62, 0xb2, 0x76, 0xaf, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUD ymm1{k7}{z}, ymm1, yword [rbp+r14*2+0x8]             }
testcase        {  0x62, 0xf2, 0x76, 0xbf, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD ymm1{k7}{z}, ymm1, dword [rax]{1to8}                 }
testcase        {  0x62, 0xb2, 0x76, 0xbf, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUD ymm1{k7}{z}, ymm1, dword [rbp+r14*2+0x8]{1to8}       }
testcase        {  0x62, 0xb2, 0x76, 0x08, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUD xmm1, xmm1, oword [rbp+r14*2+0x8]                    }
testcase        {  0x62, 0xf2, 0x7e, 0x48, 0xd2, 0xc1                                        }, {  {evex} VPDPWSUD zmm0, zmm0, zmm1                                     }
testcase        {  0x62, 0xf2, 0x7e, 0x48, 0xd2, 0x00                                        }, {  {evex} VPDPWSUD zmm0, zmm0, zword [rax]                              }
testcase        {  0x62, 0xb2, 0x7e, 0x48, 0xd2, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUD zmm0, zmm0, zword [rbp+r14*2+0x8]                    }
testcase        {  0x62, 0xf2, 0x7e, 0x58, 0xd2, 0x00                                        }, {  {evex} VPDPWSUD zmm0, zmm0, dword [rax]{1to16}                       }
testcase        {  0x62, 0xb2, 0x7e, 0x58, 0xd2, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWSUD zmm0, zmm0, dword [rbp+r14*2+0x8]{1to16}             }
testcase        {  0x62, 0xf2, 0x7e, 0x4f, 0xd2, 0xc1                                        }, {  {evex} VPDPWSUD zmm0{k7}, zmm0, zmm1                                 }
testcase        {  0x62, 0xf2, 0x7e, 0x4f, 0xd2, 0x00                                        }, {  {evex} VPDPWSUD zmm0{k7}, zmm0, zword [rax]                          }
testcase        {  0x62, 0xb2, 0x7e, 0x4f, 0xd2, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUD zmm0{k7}, zmm0, zword [rbp+r14*2+0x8]                }
testcase        {  0x62, 0xf2, 0x7e, 0x5f, 0xd2, 0x00                                        }, {  {evex} VPDPWSUD zmm0{k7}, zmm0, dword [rax]{1to16}                   }
testcase        {  0x62, 0xb2, 0x7e, 0x5f, 0xd2, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWSUD zmm0{k7}, zmm0, dword [rbp+r14*2+0x8]{1to16}         }
testcase        {  0x62, 0xf2, 0x76, 0x18, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD xmm1, xmm1, dword [rax]{1to4}                        }
testcase        {  0x62, 0xf2, 0x7e, 0xcf, 0xd2, 0xc1                                        }, {  {evex} VPDPWSUD zmm0{k7}{z}, zmm0, zmm1                              }
testcase        {  0x62, 0xf2, 0x7e, 0xcf, 0xd2, 0x00                                        }, {  {evex} VPDPWSUD zmm0{k7}{z}, zmm0, zword [rax]                       }
testcase        {  0x62, 0xb2, 0x7e, 0xcf, 0xd2, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUD zmm0{k7}{z}, zmm0, zword [rbp+r14*2+0x8]             }
testcase        {  0x62, 0xf2, 0x7e, 0xdf, 0xd2, 0x00                                        }, {  {evex} VPDPWSUD zmm0{k7}{z}, zmm0, dword [rax]{1to16}                }
testcase        {  0x62, 0xb2, 0x7e, 0xdf, 0xd2, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWSUD zmm0{k7}{z}, zmm0, dword [rbp+r14*2+0x8]{1to16}      }
testcase        {  0x62, 0xb2, 0x76, 0x18, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUD xmm1, xmm1, dword [rbp+r14*2+0x8]{1to4}              }
testcase        {  0x62, 0xf2, 0x76, 0x0f, 0xd2, 0xc9                                        }, {  {evex} VPDPWSUD xmm1{k7}, xmm1, xmm1                                 }
testcase        {  0x62, 0xf2, 0x76, 0x0f, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD xmm1{k7}, xmm1, oword [rax]                          }
testcase        {  0x62, 0xb2, 0x76, 0x0f, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUD xmm1{k7}, xmm1, oword [rbp+r14*2+0x8]                }
testcase        {  0x62, 0xf2, 0x76, 0x1f, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD xmm1{k7}, xmm1, dword [rax]{1to4}                    }
testcase        {  0x62, 0xb2, 0x76, 0x1f, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUD xmm1{k7}, xmm1, dword [rbp+r14*2+0x8]{1to4}          }
testcase        {  0x62, 0xf2, 0x76, 0x08, 0xd2, 0xc9                                        }, {  {evex} VPDPWSUD xmm1, xmm1, xmm1                                     }
testcase        {  0x62, 0xf2, 0x76, 0x8f, 0xd2, 0xc9                                        }, {  {evex} VPDPWSUD xmm1{k7}{z}, xmm1, xmm1                              }
testcase        {  0x62, 0xf2, 0x76, 0x8f, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD xmm1{k7}{z}, xmm1, oword [rax]                       }
testcase        {  0x62, 0xb2, 0x76, 0x8f, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUD xmm1{k7}{z}, xmm1, oword [rbp+r14*2+0x8]             }
testcase        {  0x62, 0xf2, 0x76, 0x9f, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD xmm1{k7}{z}, xmm1, dword [rax]{1to4}                 }
testcase        {  0x62, 0xb2, 0x76, 0x9f, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUD xmm1{k7}{z}, xmm1, dword [rbp+r14*2+0x8]{1to4}       }
testcase        {  0x62, 0xf2, 0x76, 0x28, 0xd2, 0xc9                                        }, {  {evex} VPDPWSUD ymm1, ymm1, ymm1                                     }
testcase        {  0x62, 0xf2, 0x76, 0x28, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD ymm1, ymm1, yword [rax]                              }
testcase        {  0x62, 0xb2, 0x76, 0x28, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUD ymm1, ymm1, yword [rbp+r14*2+0x8]                    }
testcase        {  0x62, 0xf2, 0x76, 0x38, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD ymm1, ymm1, dword [rax]{1to8}                        }
testcase        {  0x62, 0xb2, 0x76, 0x38, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUD ymm1, ymm1, dword [rbp+r14*2+0x8]{1to8}              }
testcase        {  0x62, 0xf2, 0x76, 0x08, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD xmm1, xmm1, oword [rax]                              }
testcase        {  0x62, 0xf2, 0x76, 0x2f, 0xd2, 0xc9                                        }, {  {evex} VPDPWSUD ymm1{k7}, ymm1, ymm1                                 }
testcase        {  0x62, 0xf2, 0x76, 0x2f, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD ymm1{k7}, ymm1, yword [rax]                          }
testcase        {  0x62, 0xb2, 0x76, 0x2f, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUD ymm1{k7}, ymm1, yword [rbp+r14*2+0x8]                }
testcase        {  0x62, 0xf2, 0x76, 0x3f, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD ymm1{k7}, ymm1, dword [rax]{1to8}                    }
testcase        {  0x62, 0xb2, 0x76, 0x3f, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUD ymm1{k7}, ymm1, dword [rbp+r14*2+0x8]{1to8}          }
testcase        {  0x62, 0xf2, 0x76, 0xaf, 0xd2, 0xc9                                        }, {  {evex} VPDPWSUD ymm1{k7}{z}, ymm1, ymm1                              }
testcase        {  0x62, 0xf2, 0x76, 0xaf, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD ymm1{k7}{z}, ymm1, yword [rax]                       }
testcase        {  0x62, 0xb2, 0x76, 0xaf, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUD ymm1{k7}{z}, ymm1, yword [rbp+r14*2+0x8]             }
testcase        {  0x62, 0xf2, 0x76, 0xbf, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD ymm1{k7}{z}, ymm1, dword [rax]{1to8}                 }
testcase        {  0x62, 0xb2, 0x76, 0xbf, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUD ymm1{k7}{z}, ymm1, dword [rbp+r14*2+0x8]{1to8}       }
testcase        {  0x62, 0xb2, 0x76, 0x08, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUD xmm1, xmm1, oword [rbp+r14*2+0x8]                    }
testcase        {  0x62, 0xf2, 0x7e, 0x48, 0xd2, 0xc1                                        }, {  {evex} VPDPWSUD zmm0, zmm0, zmm1                                     }
testcase        {  0x62, 0xf2, 0x7e, 0x48, 0xd2, 0x00                                        }, {  {evex} VPDPWSUD zmm0, zmm0, zword [rax]                              }
testcase        {  0x62, 0xb2, 0x7e, 0x48, 0xd2, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUD zmm0, zmm0, zword [rbp+r14*2+0x8]                    }
testcase        {  0x62, 0xf2, 0x7e, 0x58, 0xd2, 0x00                                        }, {  {evex} VPDPWSUD zmm0, zmm0, dword [rax]{1to16}                       }
testcase        {  0x62, 0xb2, 0x7e, 0x58, 0xd2, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWSUD zmm0, zmm0, dword [rbp+r14*2+0x8]{1to16}             }
testcase        {  0x62, 0xf2, 0x7e, 0x4f, 0xd2, 0xc1                                        }, {  {evex} VPDPWSUD zmm0{k7}, zmm0, zmm1                                 }
testcase        {  0x62, 0xf2, 0x7e, 0x4f, 0xd2, 0x00                                        }, {  {evex} VPDPWSUD zmm0{k7}, zmm0, zword [rax]                          }
testcase        {  0x62, 0xb2, 0x7e, 0x4f, 0xd2, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUD zmm0{k7}, zmm0, zword [rbp+r14*2+0x8]                }
testcase        {  0x62, 0xf2, 0x7e, 0x5f, 0xd2, 0x00                                        }, {  {evex} VPDPWSUD zmm0{k7}, zmm0, dword [rax]{1to16}                   }
testcase        {  0x62, 0xb2, 0x7e, 0x5f, 0xd2, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWSUD zmm0{k7}, zmm0, dword [rbp+r14*2+0x8]{1to16}         }
testcase        {  0x62, 0xf2, 0x76, 0x18, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD xmm1, xmm1, dword [rax]{1to4}                        }
testcase        {  0x62, 0xf2, 0x7e, 0xcf, 0xd2, 0xc1                                        }, {  {evex} VPDPWSUD zmm0{k7}{z}, zmm0, zmm1                              }
testcase        {  0x62, 0xf2, 0x7e, 0xcf, 0xd2, 0x00                                        }, {  {evex} VPDPWSUD zmm0{k7}{z}, zmm0, zword [rax]                       }
testcase        {  0x62, 0xb2, 0x7e, 0xcf, 0xd2, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUD zmm0{k7}{z}, zmm0, zword [rbp+r14*2+0x8]             }
testcase        {  0x62, 0xf2, 0x7e, 0xdf, 0xd2, 0x00                                        }, {  {evex} VPDPWSUD zmm0{k7}{z}, zmm0, dword [rax]{1to16}                }
testcase        {  0x62, 0xb2, 0x7e, 0xdf, 0xd2, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWSUD zmm0{k7}{z}, zmm0, dword [rbp+r14*2+0x8]{1to16}      }
testcase        {  0x62, 0xb2, 0x76, 0x18, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUD xmm1, xmm1, dword [rbp+r14*2+0x8]{1to4}              }
testcase        {  0x62, 0xf2, 0x76, 0x0f, 0xd2, 0xc9                                        }, {  {evex} VPDPWSUD xmm1{k7}, xmm1, xmm1                                 }
testcase        {  0x62, 0xf2, 0x76, 0x0f, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD xmm1{k7}, xmm1, oword [rax]                          }
testcase        {  0x62, 0xb2, 0x76, 0x0f, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUD xmm1{k7}, xmm1, oword [rbp+r14*2+0x8]                }
testcase        {  0x62, 0xf2, 0x76, 0x1f, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD xmm1{k7}, xmm1, dword [rax]{1to4}                    }
testcase        {  0x62, 0xb2, 0x76, 0x1f, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUD xmm1{k7}, xmm1, dword [rbp+r14*2+0x8]{1to4}          }
testcase        {  0x62, 0xf2, 0x76, 0x08, 0xd2, 0xc9                                        }, {  {evex} VPDPWSUD xmm1, xmm1, xmm1                                     }
testcase        {  0x62, 0xf2, 0x76, 0x8f, 0xd2, 0xc9                                        }, {  {evex} VPDPWSUD xmm1{k7}{z}, xmm1, xmm1                              }
testcase        {  0x62, 0xf2, 0x76, 0x8f, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD xmm1{k7}{z}, xmm1, oword [rax]                       }
testcase        {  0x62, 0xb2, 0x76, 0x8f, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUD xmm1{k7}{z}, xmm1, oword [rbp+r14*2+0x8]             }
testcase        {  0x62, 0xf2, 0x76, 0x9f, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD xmm1{k7}{z}, xmm1, dword [rax]{1to4}                 }
testcase        {  0x62, 0xb2, 0x76, 0x9f, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUD xmm1{k7}{z}, xmm1, dword [rbp+r14*2+0x8]{1to4}       }
testcase        {  0x62, 0xf2, 0x76, 0x28, 0xd2, 0xc9                                        }, {  {evex} VPDPWSUD ymm1, ymm1, ymm1                                     }
testcase        {  0x62, 0xf2, 0x76, 0x28, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD ymm1, ymm1, yword [rax]                              }
testcase        {  0x62, 0xb2, 0x76, 0x28, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUD ymm1, ymm1, yword [rbp+r14*2+0x8]                    }
testcase        {  0x62, 0xf2, 0x76, 0x38, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD ymm1, ymm1, dword [rax]{1to8}                        }
testcase        {  0x62, 0xb2, 0x76, 0x38, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUD ymm1, ymm1, dword [rbp+r14*2+0x8]{1to8}              }
testcase        {  0x62, 0xf2, 0x76, 0x08, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD xmm1, xmm1, oword [rax]                              }
testcase        {  0x62, 0xf2, 0x76, 0x2f, 0xd2, 0xc9                                        }, {  {evex} VPDPWSUD ymm1{k7}, ymm1, ymm1                                 }
testcase        {  0x62, 0xf2, 0x76, 0x2f, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD ymm1{k7}, ymm1, yword [rax]                          }
testcase        {  0x62, 0xb2, 0x76, 0x2f, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUD ymm1{k7}, ymm1, yword [rbp+r14*2+0x8]                }
testcase        {  0x62, 0xf2, 0x76, 0x3f, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD ymm1{k7}, ymm1, dword [rax]{1to8}                    }
testcase        {  0x62, 0xb2, 0x76, 0x3f, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUD ymm1{k7}, ymm1, dword [rbp+r14*2+0x8]{1to8}          }
testcase        {  0x62, 0xf2, 0x76, 0xaf, 0xd2, 0xc9                                        }, {  {evex} VPDPWSUD ymm1{k7}{z}, ymm1, ymm1                              }
testcase        {  0x62, 0xf2, 0x76, 0xaf, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD ymm1{k7}{z}, ymm1, yword [rax]                       }
testcase        {  0x62, 0xb2, 0x76, 0xaf, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUD ymm1{k7}{z}, ymm1, yword [rbp+r14*2+0x8]             }
testcase        {  0x62, 0xf2, 0x76, 0xbf, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD ymm1{k7}{z}, ymm1, dword [rax]{1to8}                 }
testcase        {  0x62, 0xb2, 0x76, 0xbf, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUD ymm1{k7}{z}, ymm1, dword [rbp+r14*2+0x8]{1to8}       }
testcase        {  0x62, 0xb2, 0x76, 0x08, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUD xmm1, xmm1, oword [rbp+r14*2+0x8]                    }
testcase        {  0x62, 0xf2, 0x7e, 0x48, 0xd2, 0xc1                                        }, {  {evex} VPDPWSUD zmm0, zmm0, zmm1                                     }
testcase        {  0x62, 0xf2, 0x7e, 0x48, 0xd2, 0x00                                        }, {  {evex} VPDPWSUD zmm0, zmm0, zword [rax]                              }
testcase        {  0x62, 0xb2, 0x7e, 0x48, 0xd2, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUD zmm0, zmm0, zword [rbp+r14*2+0x8]                    }
testcase        {  0x62, 0xf2, 0x7e, 0x58, 0xd2, 0x00                                        }, {  {evex} VPDPWSUD zmm0, zmm0, dword [rax]{1to16}                       }
testcase        {  0x62, 0xb2, 0x7e, 0x58, 0xd2, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWSUD zmm0, zmm0, dword [rbp+r14*2+0x8]{1to16}             }
testcase        {  0x62, 0xf2, 0x7e, 0x4f, 0xd2, 0xc1                                        }, {  {evex} VPDPWSUD zmm0{k7}, zmm0, zmm1                                 }
testcase        {  0x62, 0xf2, 0x7e, 0x4f, 0xd2, 0x00                                        }, {  {evex} VPDPWSUD zmm0{k7}, zmm0, zword [rax]                          }
testcase        {  0x62, 0xb2, 0x7e, 0x4f, 0xd2, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUD zmm0{k7}, zmm0, zword [rbp+r14*2+0x8]                }
testcase        {  0x62, 0xf2, 0x7e, 0x5f, 0xd2, 0x00                                        }, {  {evex} VPDPWSUD zmm0{k7}, zmm0, dword [rax]{1to16}                   }
testcase        {  0x62, 0xb2, 0x7e, 0x5f, 0xd2, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWSUD zmm0{k7}, zmm0, dword [rbp+r14*2+0x8]{1to16}         }
testcase        {  0x62, 0xf2, 0x76, 0x18, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD xmm1, xmm1, dword [rax]{1to4}                        }
testcase        {  0x62, 0xf2, 0x7e, 0xcf, 0xd2, 0xc1                                        }, {  {evex} VPDPWSUD zmm0{k7}{z}, zmm0, zmm1                              }
testcase        {  0x62, 0xf2, 0x7e, 0xcf, 0xd2, 0x00                                        }, {  {evex} VPDPWSUD zmm0{k7}{z}, zmm0, zword [rax]                       }
testcase        {  0x62, 0xb2, 0x7e, 0xcf, 0xd2, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUD zmm0{k7}{z}, zmm0, zword [rbp+r14*2+0x8]             }
testcase        {  0x62, 0xf2, 0x7e, 0xdf, 0xd2, 0x00                                        }, {  {evex} VPDPWSUD zmm0{k7}{z}, zmm0, dword [rax]{1to16}                }
testcase        {  0x62, 0xb2, 0x7e, 0xdf, 0xd2, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWSUD zmm0{k7}{z}, zmm0, dword [rbp+r14*2+0x8]{1to16}      }
testcase        {  0x62, 0xb2, 0x76, 0x18, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUD xmm1, xmm1, dword [rbp+r14*2+0x8]{1to4}              }
testcase        {  0x62, 0xf2, 0x76, 0x0f, 0xd2, 0xc9                                        }, {  {evex} VPDPWSUD xmm1{k7}, xmm1, xmm1                                 }
testcase        {  0x62, 0xf2, 0x76, 0x0f, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD xmm1{k7}, xmm1, oword [rax]                          }
testcase        {  0x62, 0xb2, 0x76, 0x0f, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUD xmm1{k7}, xmm1, oword [rbp+r14*2+0x8]                }
testcase        {  0x62, 0xf2, 0x76, 0x1f, 0xd2, 0x08                                        }, {  {evex} VPDPWSUD xmm1{k7}, xmm1, dword [rax]{1to4}                    }
testcase        {  0x62, 0xb2, 0x76, 0x1f, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUD xmm1{k7}, xmm1, dword [rbp+r14*2+0x8]{1to4}          }
testcase        {  0x62, 0xf2, 0x76, 0x08, 0xd3, 0xc9                                        }, {  {evex} VPDPWSUDS xmm1, xmm1, xmm1                                    }
testcase        {  0x62, 0xf2, 0x76, 0x8f, 0xd3, 0xc9                                        }, {  {evex} VPDPWSUDS xmm1{k7}{z}, xmm1, xmm1                             }
testcase        {  0x62, 0xf2, 0x76, 0x8f, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS xmm1{k7}{z}, xmm1, oword [rax]                      }
testcase        {  0x62, 0xb2, 0x76, 0x8f, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUDS xmm1{k7}{z}, xmm1, oword [rbp+r14*2+0x8]            }
testcase        {  0x62, 0xf2, 0x76, 0x9f, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS xmm1{k7}{z}, xmm1, dword [rax]{1to4}                }
testcase        {  0x62, 0xb2, 0x76, 0x9f, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUDS xmm1{k7}{z}, xmm1, dword [rbp+r14*2+0x8]{1to4}      }
testcase        {  0x62, 0xf2, 0x76, 0x28, 0xd3, 0xc9                                        }, {  {evex} VPDPWSUDS ymm1, ymm1, ymm1                                    }
testcase        {  0x62, 0xf2, 0x76, 0x28, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS ymm1, ymm1, yword [rax]                             }
testcase        {  0x62, 0xb2, 0x76, 0x28, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUDS ymm1, ymm1, yword [rbp+r14*2+0x8]                   }
testcase        {  0x62, 0xf2, 0x76, 0x38, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS ymm1, ymm1, dword [rax]{1to8}                       }
testcase        {  0x62, 0xb2, 0x76, 0x38, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUDS ymm1, ymm1, dword [rbp+r14*2+0x8]{1to8}             }
testcase        {  0x62, 0xf2, 0x76, 0x08, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS xmm1, xmm1, oword [rax]                             }
testcase        {  0x62, 0xf2, 0x76, 0x2f, 0xd3, 0xc9                                        }, {  {evex} VPDPWSUDS ymm1{k7}, ymm1, ymm1                                }
testcase        {  0x62, 0xf2, 0x76, 0x2f, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS ymm1{k7}, ymm1, yword [rax]                         }
testcase        {  0x62, 0xb2, 0x76, 0x2f, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUDS ymm1{k7}, ymm1, yword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf2, 0x76, 0x3f, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS ymm1{k7}, ymm1, dword [rax]{1to8}                   }
testcase        {  0x62, 0xb2, 0x76, 0x3f, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUDS ymm1{k7}, ymm1, dword [rbp+r14*2+0x8]{1to8}         }
testcase        {  0x62, 0xf2, 0x76, 0xaf, 0xd3, 0xc9                                        }, {  {evex} VPDPWSUDS ymm1{k7}{z}, ymm1, ymm1                             }
testcase        {  0x62, 0xf2, 0x76, 0xaf, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS ymm1{k7}{z}, ymm1, yword [rax]                      }
testcase        {  0x62, 0xb2, 0x76, 0xaf, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUDS ymm1{k7}{z}, ymm1, yword [rbp+r14*2+0x8]            }
testcase        {  0x62, 0xf2, 0x76, 0xbf, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS ymm1{k7}{z}, ymm1, dword [rax]{1to8}                }
testcase        {  0x62, 0xb2, 0x76, 0xbf, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUDS ymm1{k7}{z}, ymm1, dword [rbp+r14*2+0x8]{1to8}      }
testcase        {  0x62, 0xb2, 0x76, 0x08, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUDS xmm1, xmm1, oword [rbp+r14*2+0x8]                   }
testcase        {  0x62, 0xf2, 0x7e, 0x48, 0xd3, 0xc1                                        }, {  {evex} VPDPWSUDS zmm0, zmm0, zmm1                                    }
testcase        {  0x62, 0xf2, 0x7e, 0x48, 0xd3, 0x00                                        }, {  {evex} VPDPWSUDS zmm0, zmm0, zword [rax]                             }
testcase        {  0x62, 0xb2, 0x7e, 0x48, 0xd3, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUDS zmm0, zmm0, zword [rbp+r14*2+0x8]                   }
testcase        {  0x62, 0xf2, 0x7e, 0x58, 0xd3, 0x00                                        }, {  {evex} VPDPWSUDS zmm0, zmm0, dword [rax]{1to16}                      }
testcase        {  0x62, 0xb2, 0x7e, 0x58, 0xd3, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWSUDS zmm0, zmm0, dword [rbp+r14*2+0x8]{1to16}            }
testcase        {  0x62, 0xf2, 0x7e, 0x4f, 0xd3, 0xc1                                        }, {  {evex} VPDPWSUDS zmm0{k7}, zmm0, zmm1                                }
testcase        {  0x62, 0xf2, 0x7e, 0x4f, 0xd3, 0x00                                        }, {  {evex} VPDPWSUDS zmm0{k7}, zmm0, zword [rax]                         }
testcase        {  0x62, 0xb2, 0x7e, 0x4f, 0xd3, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUDS zmm0{k7}, zmm0, zword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf2, 0x7e, 0x5f, 0xd3, 0x00                                        }, {  {evex} VPDPWSUDS zmm0{k7}, zmm0, dword [rax]{1to16}                  }
testcase        {  0x62, 0xb2, 0x7e, 0x5f, 0xd3, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWSUDS zmm0{k7}, zmm0, dword [rbp+r14*2+0x8]{1to16}        }
testcase        {  0x62, 0xf2, 0x76, 0x18, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS xmm1, xmm1, dword [rax]{1to4}                       }
testcase        {  0x62, 0xf2, 0x7e, 0xcf, 0xd3, 0xc1                                        }, {  {evex} VPDPWSUDS zmm0{k7}{z}, zmm0, zmm1                             }
testcase        {  0x62, 0xf2, 0x7e, 0xcf, 0xd3, 0x00                                        }, {  {evex} VPDPWSUDS zmm0{k7}{z}, zmm0, zword [rax]                      }
testcase        {  0x62, 0xb2, 0x7e, 0xcf, 0xd3, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUDS zmm0{k7}{z}, zmm0, zword [rbp+r14*2+0x8]            }
testcase        {  0x62, 0xf2, 0x7e, 0xdf, 0xd3, 0x00                                        }, {  {evex} VPDPWSUDS zmm0{k7}{z}, zmm0, dword [rax]{1to16}               }
testcase        {  0x62, 0xb2, 0x7e, 0xdf, 0xd3, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWSUDS zmm0{k7}{z}, zmm0, dword [rbp+r14*2+0x8]{1to16}     }
testcase        {  0x62, 0xb2, 0x76, 0x18, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUDS xmm1, xmm1, dword [rbp+r14*2+0x8]{1to4}             }
testcase        {  0x62, 0xf2, 0x76, 0x0f, 0xd3, 0xc9                                        }, {  {evex} VPDPWSUDS xmm1{k7}, xmm1, xmm1                                }
testcase        {  0x62, 0xf2, 0x76, 0x0f, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS xmm1{k7}, xmm1, oword [rax]                         }
testcase        {  0x62, 0xb2, 0x76, 0x0f, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUDS xmm1{k7}, xmm1, oword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf2, 0x76, 0x1f, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS xmm1{k7}, xmm1, dword [rax]{1to4}                   }
testcase        {  0x62, 0xb2, 0x76, 0x1f, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUDS xmm1{k7}, xmm1, dword [rbp+r14*2+0x8]{1to4}         }
testcase        {  0x62, 0xf2, 0x76, 0x08, 0xd3, 0xc9                                        }, {  {evex} VPDPWSUDS xmm1, xmm1, xmm1                                    }
testcase        {  0x62, 0xf2, 0x76, 0x8f, 0xd3, 0xc9                                        }, {  {evex} VPDPWSUDS xmm1{k7}{z}, xmm1, xmm1                             }
testcase        {  0x62, 0xf2, 0x76, 0x8f, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS xmm1{k7}{z}, xmm1, oword [rax]                      }
testcase        {  0x62, 0xb2, 0x76, 0x8f, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUDS xmm1{k7}{z}, xmm1, oword [rbp+r14*2+0x8]            }
testcase        {  0x62, 0xf2, 0x76, 0x9f, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS xmm1{k7}{z}, xmm1, dword [rax]{1to4}                }
testcase        {  0x62, 0xb2, 0x76, 0x9f, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUDS xmm1{k7}{z}, xmm1, dword [rbp+r14*2+0x8]{1to4}      }
testcase        {  0x62, 0xf2, 0x76, 0x28, 0xd3, 0xc9                                        }, {  {evex} VPDPWSUDS ymm1, ymm1, ymm1                                    }
testcase        {  0x62, 0xf2, 0x76, 0x28, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS ymm1, ymm1, yword [rax]                             }
testcase        {  0x62, 0xb2, 0x76, 0x28, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUDS ymm1, ymm1, yword [rbp+r14*2+0x8]                   }
testcase        {  0x62, 0xf2, 0x76, 0x38, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS ymm1, ymm1, dword [rax]{1to8}                       }
testcase        {  0x62, 0xb2, 0x76, 0x38, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUDS ymm1, ymm1, dword [rbp+r14*2+0x8]{1to8}             }
testcase        {  0x62, 0xf2, 0x76, 0x08, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS xmm1, xmm1, oword [rax]                             }
testcase        {  0x62, 0xf2, 0x76, 0x2f, 0xd3, 0xc9                                        }, {  {evex} VPDPWSUDS ymm1{k7}, ymm1, ymm1                                }
testcase        {  0x62, 0xf2, 0x76, 0x2f, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS ymm1{k7}, ymm1, yword [rax]                         }
testcase        {  0x62, 0xb2, 0x76, 0x2f, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUDS ymm1{k7}, ymm1, yword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf2, 0x76, 0x3f, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS ymm1{k7}, ymm1, dword [rax]{1to8}                   }
testcase        {  0x62, 0xb2, 0x76, 0x3f, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUDS ymm1{k7}, ymm1, dword [rbp+r14*2+0x8]{1to8}         }
testcase        {  0x62, 0xf2, 0x76, 0xaf, 0xd3, 0xc9                                        }, {  {evex} VPDPWSUDS ymm1{k7}{z}, ymm1, ymm1                             }
testcase        {  0x62, 0xf2, 0x76, 0xaf, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS ymm1{k7}{z}, ymm1, yword [rax]                      }
testcase        {  0x62, 0xb2, 0x76, 0xaf, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUDS ymm1{k7}{z}, ymm1, yword [rbp+r14*2+0x8]            }
testcase        {  0x62, 0xf2, 0x76, 0xbf, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS ymm1{k7}{z}, ymm1, dword [rax]{1to8}                }
testcase        {  0x62, 0xb2, 0x76, 0xbf, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUDS ymm1{k7}{z}, ymm1, dword [rbp+r14*2+0x8]{1to8}      }
testcase        {  0x62, 0xb2, 0x76, 0x08, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUDS xmm1, xmm1, oword [rbp+r14*2+0x8]                   }
testcase        {  0x62, 0xf2, 0x7e, 0x48, 0xd3, 0xc1                                        }, {  {evex} VPDPWSUDS zmm0, zmm0, zmm1                                    }
testcase        {  0x62, 0xf2, 0x7e, 0x48, 0xd3, 0x00                                        }, {  {evex} VPDPWSUDS zmm0, zmm0, zword [rax]                             }
testcase        {  0x62, 0xb2, 0x7e, 0x48, 0xd3, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUDS zmm0, zmm0, zword [rbp+r14*2+0x8]                   }
testcase        {  0x62, 0xf2, 0x7e, 0x58, 0xd3, 0x00                                        }, {  {evex} VPDPWSUDS zmm0, zmm0, dword [rax]{1to16}                      }
testcase        {  0x62, 0xb2, 0x7e, 0x58, 0xd3, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWSUDS zmm0, zmm0, dword [rbp+r14*2+0x8]{1to16}            }
testcase        {  0x62, 0xf2, 0x7e, 0x4f, 0xd3, 0xc1                                        }, {  {evex} VPDPWSUDS zmm0{k7}, zmm0, zmm1                                }
testcase        {  0x62, 0xf2, 0x7e, 0x4f, 0xd3, 0x00                                        }, {  {evex} VPDPWSUDS zmm0{k7}, zmm0, zword [rax]                         }
testcase        {  0x62, 0xb2, 0x7e, 0x4f, 0xd3, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUDS zmm0{k7}, zmm0, zword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf2, 0x7e, 0x5f, 0xd3, 0x00                                        }, {  {evex} VPDPWSUDS zmm0{k7}, zmm0, dword [rax]{1to16}                  }
testcase        {  0x62, 0xb2, 0x7e, 0x5f, 0xd3, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWSUDS zmm0{k7}, zmm0, dword [rbp+r14*2+0x8]{1to16}        }
testcase        {  0x62, 0xf2, 0x76, 0x18, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS xmm1, xmm1, dword [rax]{1to4}                       }
testcase        {  0x62, 0xf2, 0x7e, 0xcf, 0xd3, 0xc1                                        }, {  {evex} VPDPWSUDS zmm0{k7}{z}, zmm0, zmm1                             }
testcase        {  0x62, 0xf2, 0x7e, 0xcf, 0xd3, 0x00                                        }, {  {evex} VPDPWSUDS zmm0{k7}{z}, zmm0, zword [rax]                      }
testcase        {  0x62, 0xb2, 0x7e, 0xcf, 0xd3, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUDS zmm0{k7}{z}, zmm0, zword [rbp+r14*2+0x8]            }
testcase        {  0x62, 0xf2, 0x7e, 0xdf, 0xd3, 0x00                                        }, {  {evex} VPDPWSUDS zmm0{k7}{z}, zmm0, dword [rax]{1to16}               }
testcase        {  0x62, 0xb2, 0x7e, 0xdf, 0xd3, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWSUDS zmm0{k7}{z}, zmm0, dword [rbp+r14*2+0x8]{1to16}     }
testcase        {  0x62, 0xb2, 0x76, 0x18, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUDS xmm1, xmm1, dword [rbp+r14*2+0x8]{1to4}             }
testcase        {  0x62, 0xf2, 0x76, 0x0f, 0xd3, 0xc9                                        }, {  {evex} VPDPWSUDS xmm1{k7}, xmm1, xmm1                                }
testcase        {  0x62, 0xf2, 0x76, 0x0f, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS xmm1{k7}, xmm1, oword [rax]                         }
testcase        {  0x62, 0xb2, 0x76, 0x0f, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUDS xmm1{k7}, xmm1, oword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf2, 0x76, 0x1f, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS xmm1{k7}, xmm1, dword [rax]{1to4}                   }
testcase        {  0x62, 0xb2, 0x76, 0x1f, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUDS xmm1{k7}, xmm1, dword [rbp+r14*2+0x8]{1to4}         }
testcase        {  0x62, 0xf2, 0x76, 0x08, 0xd3, 0xc9                                        }, {  {evex} VPDPWSUDS xmm1, xmm1, xmm1                                    }
testcase        {  0x62, 0xf2, 0x76, 0x8f, 0xd3, 0xc9                                        }, {  {evex} VPDPWSUDS xmm1{k7}{z}, xmm1, xmm1                             }
testcase        {  0x62, 0xf2, 0x76, 0x8f, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS xmm1{k7}{z}, xmm1, oword [rax]                      }
testcase        {  0x62, 0xb2, 0x76, 0x8f, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUDS xmm1{k7}{z}, xmm1, oword [rbp+r14*2+0x8]            }
testcase        {  0x62, 0xf2, 0x76, 0x9f, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS xmm1{k7}{z}, xmm1, dword [rax]{1to4}                }
testcase        {  0x62, 0xb2, 0x76, 0x9f, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUDS xmm1{k7}{z}, xmm1, dword [rbp+r14*2+0x8]{1to4}      }
testcase        {  0x62, 0xf2, 0x76, 0x28, 0xd3, 0xc9                                        }, {  {evex} VPDPWSUDS ymm1, ymm1, ymm1                                    }
testcase        {  0x62, 0xf2, 0x76, 0x28, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS ymm1, ymm1, yword [rax]                             }
testcase        {  0x62, 0xb2, 0x76, 0x28, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUDS ymm1, ymm1, yword [rbp+r14*2+0x8]                   }
testcase        {  0x62, 0xf2, 0x76, 0x38, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS ymm1, ymm1, dword [rax]{1to8}                       }
testcase        {  0x62, 0xb2, 0x76, 0x38, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUDS ymm1, ymm1, dword [rbp+r14*2+0x8]{1to8}             }
testcase        {  0x62, 0xf2, 0x76, 0x08, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS xmm1, xmm1, oword [rax]                             }
testcase        {  0x62, 0xf2, 0x76, 0x2f, 0xd3, 0xc9                                        }, {  {evex} VPDPWSUDS ymm1{k7}, ymm1, ymm1                                }
testcase        {  0x62, 0xf2, 0x76, 0x2f, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS ymm1{k7}, ymm1, yword [rax]                         }
testcase        {  0x62, 0xb2, 0x76, 0x2f, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUDS ymm1{k7}, ymm1, yword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf2, 0x76, 0x3f, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS ymm1{k7}, ymm1, dword [rax]{1to8}                   }
testcase        {  0x62, 0xb2, 0x76, 0x3f, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUDS ymm1{k7}, ymm1, dword [rbp+r14*2+0x8]{1to8}         }
testcase        {  0x62, 0xf2, 0x76, 0xaf, 0xd3, 0xc9                                        }, {  {evex} VPDPWSUDS ymm1{k7}{z}, ymm1, ymm1                             }
testcase        {  0x62, 0xf2, 0x76, 0xaf, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS ymm1{k7}{z}, ymm1, yword [rax]                      }
testcase        {  0x62, 0xb2, 0x76, 0xaf, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUDS ymm1{k7}{z}, ymm1, yword [rbp+r14*2+0x8]            }
testcase        {  0x62, 0xf2, 0x76, 0xbf, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS ymm1{k7}{z}, ymm1, dword [rax]{1to8}                }
testcase        {  0x62, 0xb2, 0x76, 0xbf, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUDS ymm1{k7}{z}, ymm1, dword [rbp+r14*2+0x8]{1to8}      }
testcase        {  0x62, 0xb2, 0x76, 0x08, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUDS xmm1, xmm1, oword [rbp+r14*2+0x8]                   }
testcase        {  0x62, 0xf2, 0x7e, 0x48, 0xd3, 0xc1                                        }, {  {evex} VPDPWSUDS zmm0, zmm0, zmm1                                    }
testcase        {  0x62, 0xf2, 0x7e, 0x48, 0xd3, 0x00                                        }, {  {evex} VPDPWSUDS zmm0, zmm0, zword [rax]                             }
testcase        {  0x62, 0xb2, 0x7e, 0x48, 0xd3, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUDS zmm0, zmm0, zword [rbp+r14*2+0x8]                   }
testcase        {  0x62, 0xf2, 0x7e, 0x58, 0xd3, 0x00                                        }, {  {evex} VPDPWSUDS zmm0, zmm0, dword [rax]{1to16}                      }
testcase        {  0x62, 0xb2, 0x7e, 0x58, 0xd3, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWSUDS zmm0, zmm0, dword [rbp+r14*2+0x8]{1to16}            }
testcase        {  0x62, 0xf2, 0x7e, 0x4f, 0xd3, 0xc1                                        }, {  {evex} VPDPWSUDS zmm0{k7}, zmm0, zmm1                                }
testcase        {  0x62, 0xf2, 0x7e, 0x4f, 0xd3, 0x00                                        }, {  {evex} VPDPWSUDS zmm0{k7}, zmm0, zword [rax]                         }
testcase        {  0x62, 0xb2, 0x7e, 0x4f, 0xd3, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUDS zmm0{k7}, zmm0, zword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf2, 0x7e, 0x5f, 0xd3, 0x00                                        }, {  {evex} VPDPWSUDS zmm0{k7}, zmm0, dword [rax]{1to16}                  }
testcase        {  0x62, 0xb2, 0x7e, 0x5f, 0xd3, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWSUDS zmm0{k7}, zmm0, dword [rbp+r14*2+0x8]{1to16}        }
testcase        {  0x62, 0xf2, 0x76, 0x18, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS xmm1, xmm1, dword [rax]{1to4}                       }
testcase        {  0x62, 0xf2, 0x7e, 0xcf, 0xd3, 0xc1                                        }, {  {evex} VPDPWSUDS zmm0{k7}{z}, zmm0, zmm1                             }
testcase        {  0x62, 0xf2, 0x7e, 0xcf, 0xd3, 0x00                                        }, {  {evex} VPDPWSUDS zmm0{k7}{z}, zmm0, zword [rax]                      }
testcase        {  0x62, 0xb2, 0x7e, 0xcf, 0xd3, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUDS zmm0{k7}{z}, zmm0, zword [rbp+r14*2+0x8]            }
testcase        {  0x62, 0xf2, 0x7e, 0xdf, 0xd3, 0x00                                        }, {  {evex} VPDPWSUDS zmm0{k7}{z}, zmm0, dword [rax]{1to16}               }
testcase        {  0x62, 0xb2, 0x7e, 0xdf, 0xd3, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWSUDS zmm0{k7}{z}, zmm0, dword [rbp+r14*2+0x8]{1to16}     }
testcase        {  0x62, 0xb2, 0x76, 0x18, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUDS xmm1, xmm1, dword [rbp+r14*2+0x8]{1to4}             }
testcase        {  0x62, 0xf2, 0x76, 0x0f, 0xd3, 0xc9                                        }, {  {evex} VPDPWSUDS xmm1{k7}, xmm1, xmm1                                }
testcase        {  0x62, 0xf2, 0x76, 0x0f, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS xmm1{k7}, xmm1, oword [rax]                         }
testcase        {  0x62, 0xb2, 0x76, 0x0f, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWSUDS xmm1{k7}, xmm1, oword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf2, 0x76, 0x1f, 0xd3, 0x08                                        }, {  {evex} VPDPWSUDS xmm1{k7}, xmm1, dword [rax]{1to4}                   }
testcase        {  0x62, 0xb2, 0x76, 0x1f, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWSUDS xmm1{k7}, xmm1, dword [rbp+r14*2+0x8]{1to4}         }
testcase        {  0x62, 0xf2, 0x75, 0x08, 0xd2, 0xc9                                        }, {  {evex} VPDPWUSD xmm1, xmm1, xmm1                                     }
testcase        {  0x62, 0xf2, 0x75, 0x8f, 0xd2, 0xc9                                        }, {  {evex} VPDPWUSD xmm1{k7}{z}, xmm1, xmm1                              }
testcase        {  0x62, 0xf2, 0x75, 0x8f, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD xmm1{k7}{z}, xmm1, oword [rax]                       }
testcase        {  0x62, 0xb2, 0x75, 0x8f, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSD xmm1{k7}{z}, xmm1, oword [rbp+r14*2+0x8]             }
testcase        {  0x62, 0xf2, 0x75, 0x9f, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD xmm1{k7}{z}, xmm1, dword [rax]{1to4}                 }
testcase        {  0x62, 0xb2, 0x75, 0x9f, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSD xmm1{k7}{z}, xmm1, dword [rbp+r14*2+0x8]{1to4}       }
testcase        {  0x62, 0xf2, 0x75, 0x28, 0xd2, 0xc9                                        }, {  {evex} VPDPWUSD ymm1, ymm1, ymm1                                     }
testcase        {  0x62, 0xf2, 0x75, 0x28, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD ymm1, ymm1, yword [rax]                              }
testcase        {  0x62, 0xb2, 0x75, 0x28, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSD ymm1, ymm1, yword [rbp+r14*2+0x8]                    }
testcase        {  0x62, 0xf2, 0x75, 0x38, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD ymm1, ymm1, dword [rax]{1to8}                        }
testcase        {  0x62, 0xb2, 0x75, 0x38, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSD ymm1, ymm1, dword [rbp+r14*2+0x8]{1to8}              }
testcase        {  0x62, 0xf2, 0x75, 0x08, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD xmm1, xmm1, oword [rax]                              }
testcase        {  0x62, 0xf2, 0x75, 0x2f, 0xd2, 0xc9                                        }, {  {evex} VPDPWUSD ymm1{k7}, ymm1, ymm1                                 }
testcase        {  0x62, 0xf2, 0x75, 0x2f, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD ymm1{k7}, ymm1, yword [rax]                          }
testcase        {  0x62, 0xb2, 0x75, 0x2f, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSD ymm1{k7}, ymm1, yword [rbp+r14*2+0x8]                }
testcase        {  0x62, 0xf2, 0x75, 0x3f, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD ymm1{k7}, ymm1, dword [rax]{1to8}                    }
testcase        {  0x62, 0xb2, 0x75, 0x3f, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSD ymm1{k7}, ymm1, dword [rbp+r14*2+0x8]{1to8}          }
testcase        {  0x62, 0xf2, 0x75, 0xaf, 0xd2, 0xc9                                        }, {  {evex} VPDPWUSD ymm1{k7}{z}, ymm1, ymm1                              }
testcase        {  0x62, 0xf2, 0x75, 0xaf, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD ymm1{k7}{z}, ymm1, yword [rax]                       }
testcase        {  0x62, 0xb2, 0x75, 0xaf, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSD ymm1{k7}{z}, ymm1, yword [rbp+r14*2+0x8]             }
testcase        {  0x62, 0xf2, 0x75, 0xbf, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD ymm1{k7}{z}, ymm1, dword [rax]{1to8}                 }
testcase        {  0x62, 0xb2, 0x75, 0xbf, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSD ymm1{k7}{z}, ymm1, dword [rbp+r14*2+0x8]{1to8}       }
testcase        {  0x62, 0xb2, 0x75, 0x08, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSD xmm1, xmm1, oword [rbp+r14*2+0x8]                    }
testcase        {  0x62, 0xf2, 0x7d, 0x48, 0xd2, 0xc1                                        }, {  {evex} VPDPWUSD zmm0, zmm0, zmm1                                     }
testcase        {  0x62, 0xf2, 0x7d, 0x48, 0xd2, 0x00                                        }, {  {evex} VPDPWUSD zmm0, zmm0, zword [rax]                              }
testcase        {  0x62, 0xb2, 0x7d, 0x48, 0xd2, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSD zmm0, zmm0, zword [rbp+r14*2+0x8]                    }
testcase        {  0x62, 0xf2, 0x7d, 0x58, 0xd2, 0x00                                        }, {  {evex} VPDPWUSD zmm0, zmm0, dword [rax]{1to16}                       }
testcase        {  0x62, 0xb2, 0x7d, 0x58, 0xd2, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUSD zmm0, zmm0, dword [rbp+r14*2+0x8]{1to16}             }
testcase        {  0x62, 0xf2, 0x7d, 0x4f, 0xd2, 0xc1                                        }, {  {evex} VPDPWUSD zmm0{k7}, zmm0, zmm1                                 }
testcase        {  0x62, 0xf2, 0x7d, 0x4f, 0xd2, 0x00                                        }, {  {evex} VPDPWUSD zmm0{k7}, zmm0, zword [rax]                          }
testcase        {  0x62, 0xb2, 0x7d, 0x4f, 0xd2, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSD zmm0{k7}, zmm0, zword [rbp+r14*2+0x8]                }
testcase        {  0x62, 0xf2, 0x7d, 0x5f, 0xd2, 0x00                                        }, {  {evex} VPDPWUSD zmm0{k7}, zmm0, dword [rax]{1to16}                   }
testcase        {  0x62, 0xb2, 0x7d, 0x5f, 0xd2, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUSD zmm0{k7}, zmm0, dword [rbp+r14*2+0x8]{1to16}         }
testcase        {  0x62, 0xf2, 0x75, 0x18, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD xmm1, xmm1, dword [rax]{1to4}                        }
testcase        {  0x62, 0xf2, 0x7d, 0xcf, 0xd2, 0xc1                                        }, {  {evex} VPDPWUSD zmm0{k7}{z}, zmm0, zmm1                              }
testcase        {  0x62, 0xf2, 0x7d, 0xcf, 0xd2, 0x00                                        }, {  {evex} VPDPWUSD zmm0{k7}{z}, zmm0, zword [rax]                       }
testcase        {  0x62, 0xb2, 0x7d, 0xcf, 0xd2, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSD zmm0{k7}{z}, zmm0, zword [rbp+r14*2+0x8]             }
testcase        {  0x62, 0xf2, 0x7d, 0xdf, 0xd2, 0x00                                        }, {  {evex} VPDPWUSD zmm0{k7}{z}, zmm0, dword [rax]{1to16}                }
testcase        {  0x62, 0xb2, 0x7d, 0xdf, 0xd2, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUSD zmm0{k7}{z}, zmm0, dword [rbp+r14*2+0x8]{1to16}      }
testcase        {  0x62, 0xb2, 0x75, 0x18, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSD xmm1, xmm1, dword [rbp+r14*2+0x8]{1to4}              }
testcase        {  0x62, 0xf2, 0x75, 0x0f, 0xd2, 0xc9                                        }, {  {evex} VPDPWUSD xmm1{k7}, xmm1, xmm1                                 }
testcase        {  0x62, 0xf2, 0x75, 0x0f, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD xmm1{k7}, xmm1, oword [rax]                          }
testcase        {  0x62, 0xb2, 0x75, 0x0f, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSD xmm1{k7}, xmm1, oword [rbp+r14*2+0x8]                }
testcase        {  0x62, 0xf2, 0x75, 0x1f, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD xmm1{k7}, xmm1, dword [rax]{1to4}                    }
testcase        {  0x62, 0xb2, 0x75, 0x1f, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSD xmm1{k7}, xmm1, dword [rbp+r14*2+0x8]{1to4}          }
testcase        {  0x62, 0xf2, 0x75, 0x08, 0xd2, 0xc9                                        }, {  {evex} VPDPWUSD xmm1, xmm1, xmm1                                     }
testcase        {  0x62, 0xf2, 0x75, 0x8f, 0xd2, 0xc9                                        }, {  {evex} VPDPWUSD xmm1{k7}{z}, xmm1, xmm1                              }
testcase        {  0x62, 0xf2, 0x75, 0x8f, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD xmm1{k7}{z}, xmm1, oword [rax]                       }
testcase        {  0x62, 0xb2, 0x75, 0x8f, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSD xmm1{k7}{z}, xmm1, oword [rbp+r14*2+0x8]             }
testcase        {  0x62, 0xf2, 0x75, 0x9f, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD xmm1{k7}{z}, xmm1, dword [rax]{1to4}                 }
testcase        {  0x62, 0xb2, 0x75, 0x9f, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSD xmm1{k7}{z}, xmm1, dword [rbp+r14*2+0x8]{1to4}       }
testcase        {  0x62, 0xf2, 0x75, 0x28, 0xd2, 0xc9                                        }, {  {evex} VPDPWUSD ymm1, ymm1, ymm1                                     }
testcase        {  0x62, 0xf2, 0x75, 0x28, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD ymm1, ymm1, yword [rax]                              }
testcase        {  0x62, 0xb2, 0x75, 0x28, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSD ymm1, ymm1, yword [rbp+r14*2+0x8]                    }
testcase        {  0x62, 0xf2, 0x75, 0x38, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD ymm1, ymm1, dword [rax]{1to8}                        }
testcase        {  0x62, 0xb2, 0x75, 0x38, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSD ymm1, ymm1, dword [rbp+r14*2+0x8]{1to8}              }
testcase        {  0x62, 0xf2, 0x75, 0x08, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD xmm1, xmm1, oword [rax]                              }
testcase        {  0x62, 0xf2, 0x75, 0x2f, 0xd2, 0xc9                                        }, {  {evex} VPDPWUSD ymm1{k7}, ymm1, ymm1                                 }
testcase        {  0x62, 0xf2, 0x75, 0x2f, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD ymm1{k7}, ymm1, yword [rax]                          }
testcase        {  0x62, 0xb2, 0x75, 0x2f, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSD ymm1{k7}, ymm1, yword [rbp+r14*2+0x8]                }
testcase        {  0x62, 0xf2, 0x75, 0x3f, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD ymm1{k7}, ymm1, dword [rax]{1to8}                    }
testcase        {  0x62, 0xb2, 0x75, 0x3f, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSD ymm1{k7}, ymm1, dword [rbp+r14*2+0x8]{1to8}          }
testcase        {  0x62, 0xf2, 0x75, 0xaf, 0xd2, 0xc9                                        }, {  {evex} VPDPWUSD ymm1{k7}{z}, ymm1, ymm1                              }
testcase        {  0x62, 0xf2, 0x75, 0xaf, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD ymm1{k7}{z}, ymm1, yword [rax]                       }
testcase        {  0x62, 0xb2, 0x75, 0xaf, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSD ymm1{k7}{z}, ymm1, yword [rbp+r14*2+0x8]             }
testcase        {  0x62, 0xf2, 0x75, 0xbf, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD ymm1{k7}{z}, ymm1, dword [rax]{1to8}                 }
testcase        {  0x62, 0xb2, 0x75, 0xbf, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSD ymm1{k7}{z}, ymm1, dword [rbp+r14*2+0x8]{1to8}       }
testcase        {  0x62, 0xb2, 0x75, 0x08, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSD xmm1, xmm1, oword [rbp+r14*2+0x8]                    }
testcase        {  0x62, 0xf2, 0x7d, 0x48, 0xd2, 0xc1                                        }, {  {evex} VPDPWUSD zmm0, zmm0, zmm1                                     }
testcase        {  0x62, 0xf2, 0x7d, 0x48, 0xd2, 0x00                                        }, {  {evex} VPDPWUSD zmm0, zmm0, zword [rax]                              }
testcase        {  0x62, 0xb2, 0x7d, 0x48, 0xd2, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSD zmm0, zmm0, zword [rbp+r14*2+0x8]                    }
testcase        {  0x62, 0xf2, 0x7d, 0x58, 0xd2, 0x00                                        }, {  {evex} VPDPWUSD zmm0, zmm0, dword [rax]{1to16}                       }
testcase        {  0x62, 0xb2, 0x7d, 0x58, 0xd2, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUSD zmm0, zmm0, dword [rbp+r14*2+0x8]{1to16}             }
testcase        {  0x62, 0xf2, 0x7d, 0x4f, 0xd2, 0xc1                                        }, {  {evex} VPDPWUSD zmm0{k7}, zmm0, zmm1                                 }
testcase        {  0x62, 0xf2, 0x7d, 0x4f, 0xd2, 0x00                                        }, {  {evex} VPDPWUSD zmm0{k7}, zmm0, zword [rax]                          }
testcase        {  0x62, 0xb2, 0x7d, 0x4f, 0xd2, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSD zmm0{k7}, zmm0, zword [rbp+r14*2+0x8]                }
testcase        {  0x62, 0xf2, 0x7d, 0x5f, 0xd2, 0x00                                        }, {  {evex} VPDPWUSD zmm0{k7}, zmm0, dword [rax]{1to16}                   }
testcase        {  0x62, 0xb2, 0x7d, 0x5f, 0xd2, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUSD zmm0{k7}, zmm0, dword [rbp+r14*2+0x8]{1to16}         }
testcase        {  0x62, 0xf2, 0x75, 0x18, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD xmm1, xmm1, dword [rax]{1to4}                        }
testcase        {  0x62, 0xf2, 0x7d, 0xcf, 0xd2, 0xc1                                        }, {  {evex} VPDPWUSD zmm0{k7}{z}, zmm0, zmm1                              }
testcase        {  0x62, 0xf2, 0x7d, 0xcf, 0xd2, 0x00                                        }, {  {evex} VPDPWUSD zmm0{k7}{z}, zmm0, zword [rax]                       }
testcase        {  0x62, 0xb2, 0x7d, 0xcf, 0xd2, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSD zmm0{k7}{z}, zmm0, zword [rbp+r14*2+0x8]             }
testcase        {  0x62, 0xf2, 0x7d, 0xdf, 0xd2, 0x00                                        }, {  {evex} VPDPWUSD zmm0{k7}{z}, zmm0, dword [rax]{1to16}                }
testcase        {  0x62, 0xb2, 0x7d, 0xdf, 0xd2, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUSD zmm0{k7}{z}, zmm0, dword [rbp+r14*2+0x8]{1to16}      }
testcase        {  0x62, 0xb2, 0x75, 0x18, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSD xmm1, xmm1, dword [rbp+r14*2+0x8]{1to4}              }
testcase        {  0x62, 0xf2, 0x75, 0x0f, 0xd2, 0xc9                                        }, {  {evex} VPDPWUSD xmm1{k7}, xmm1, xmm1                                 }
testcase        {  0x62, 0xf2, 0x75, 0x0f, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD xmm1{k7}, xmm1, oword [rax]                          }
testcase        {  0x62, 0xb2, 0x75, 0x0f, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSD xmm1{k7}, xmm1, oword [rbp+r14*2+0x8]                }
testcase        {  0x62, 0xf2, 0x75, 0x1f, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD xmm1{k7}, xmm1, dword [rax]{1to4}                    }
testcase        {  0x62, 0xb2, 0x75, 0x1f, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSD xmm1{k7}, xmm1, dword [rbp+r14*2+0x8]{1to4}          }
testcase        {  0x62, 0xf2, 0x75, 0x08, 0xd2, 0xc9                                        }, {  {evex} VPDPWUSD xmm1, xmm1, xmm1                                     }
testcase        {  0x62, 0xf2, 0x75, 0x8f, 0xd2, 0xc9                                        }, {  {evex} VPDPWUSD xmm1{k7}{z}, xmm1, xmm1                              }
testcase        {  0x62, 0xf2, 0x75, 0x8f, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD xmm1{k7}{z}, xmm1, oword [rax]                       }
testcase        {  0x62, 0xb2, 0x75, 0x8f, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSD xmm1{k7}{z}, xmm1, oword [rbp+r14*2+0x8]             }
testcase        {  0x62, 0xf2, 0x75, 0x9f, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD xmm1{k7}{z}, xmm1, dword [rax]{1to4}                 }
testcase        {  0x62, 0xb2, 0x75, 0x9f, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSD xmm1{k7}{z}, xmm1, dword [rbp+r14*2+0x8]{1to4}       }
testcase        {  0x62, 0xf2, 0x75, 0x28, 0xd2, 0xc9                                        }, {  {evex} VPDPWUSD ymm1, ymm1, ymm1                                     }
testcase        {  0x62, 0xf2, 0x75, 0x28, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD ymm1, ymm1, yword [rax]                              }
testcase        {  0x62, 0xb2, 0x75, 0x28, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSD ymm1, ymm1, yword [rbp+r14*2+0x8]                    }
testcase        {  0x62, 0xf2, 0x75, 0x38, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD ymm1, ymm1, dword [rax]{1to8}                        }
testcase        {  0x62, 0xb2, 0x75, 0x38, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSD ymm1, ymm1, dword [rbp+r14*2+0x8]{1to8}              }
testcase        {  0x62, 0xf2, 0x75, 0x08, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD xmm1, xmm1, oword [rax]                              }
testcase        {  0x62, 0xf2, 0x75, 0x2f, 0xd2, 0xc9                                        }, {  {evex} VPDPWUSD ymm1{k7}, ymm1, ymm1                                 }
testcase        {  0x62, 0xf2, 0x75, 0x2f, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD ymm1{k7}, ymm1, yword [rax]                          }
testcase        {  0x62, 0xb2, 0x75, 0x2f, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSD ymm1{k7}, ymm1, yword [rbp+r14*2+0x8]                }
testcase        {  0x62, 0xf2, 0x75, 0x3f, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD ymm1{k7}, ymm1, dword [rax]{1to8}                    }
testcase        {  0x62, 0xb2, 0x75, 0x3f, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSD ymm1{k7}, ymm1, dword [rbp+r14*2+0x8]{1to8}          }
testcase        {  0x62, 0xf2, 0x75, 0xaf, 0xd2, 0xc9                                        }, {  {evex} VPDPWUSD ymm1{k7}{z}, ymm1, ymm1                              }
testcase        {  0x62, 0xf2, 0x75, 0xaf, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD ymm1{k7}{z}, ymm1, yword [rax]                       }
testcase        {  0x62, 0xb2, 0x75, 0xaf, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSD ymm1{k7}{z}, ymm1, yword [rbp+r14*2+0x8]             }
testcase        {  0x62, 0xf2, 0x75, 0xbf, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD ymm1{k7}{z}, ymm1, dword [rax]{1to8}                 }
testcase        {  0x62, 0xb2, 0x75, 0xbf, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSD ymm1{k7}{z}, ymm1, dword [rbp+r14*2+0x8]{1to8}       }
testcase        {  0x62, 0xb2, 0x75, 0x08, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSD xmm1, xmm1, oword [rbp+r14*2+0x8]                    }
testcase        {  0x62, 0xf2, 0x7d, 0x48, 0xd2, 0xc1                                        }, {  {evex} VPDPWUSD zmm0, zmm0, zmm1                                     }
testcase        {  0x62, 0xf2, 0x7d, 0x48, 0xd2, 0x00                                        }, {  {evex} VPDPWUSD zmm0, zmm0, zword [rax]                              }
testcase        {  0x62, 0xb2, 0x7d, 0x48, 0xd2, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSD zmm0, zmm0, zword [rbp+r14*2+0x8]                    }
testcase        {  0x62, 0xf2, 0x7d, 0x58, 0xd2, 0x00                                        }, {  {evex} VPDPWUSD zmm0, zmm0, dword [rax]{1to16}                       }
testcase        {  0x62, 0xb2, 0x7d, 0x58, 0xd2, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUSD zmm0, zmm0, dword [rbp+r14*2+0x8]{1to16}             }
testcase        {  0x62, 0xf2, 0x7d, 0x4f, 0xd2, 0xc1                                        }, {  {evex} VPDPWUSD zmm0{k7}, zmm0, zmm1                                 }
testcase        {  0x62, 0xf2, 0x7d, 0x4f, 0xd2, 0x00                                        }, {  {evex} VPDPWUSD zmm0{k7}, zmm0, zword [rax]                          }
testcase        {  0x62, 0xb2, 0x7d, 0x4f, 0xd2, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSD zmm0{k7}, zmm0, zword [rbp+r14*2+0x8]                }
testcase        {  0x62, 0xf2, 0x7d, 0x5f, 0xd2, 0x00                                        }, {  {evex} VPDPWUSD zmm0{k7}, zmm0, dword [rax]{1to16}                   }
testcase        {  0x62, 0xb2, 0x7d, 0x5f, 0xd2, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUSD zmm0{k7}, zmm0, dword [rbp+r14*2+0x8]{1to16}         }
testcase        {  0x62, 0xf2, 0x75, 0x18, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD xmm1, xmm1, dword [rax]{1to4}                        }
testcase        {  0x62, 0xf2, 0x7d, 0xcf, 0xd2, 0xc1                                        }, {  {evex} VPDPWUSD zmm0{k7}{z}, zmm0, zmm1                              }
testcase        {  0x62, 0xf2, 0x7d, 0xcf, 0xd2, 0x00                                        }, {  {evex} VPDPWUSD zmm0{k7}{z}, zmm0, zword [rax]                       }
testcase        {  0x62, 0xb2, 0x7d, 0xcf, 0xd2, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSD zmm0{k7}{z}, zmm0, zword [rbp+r14*2+0x8]             }
testcase        {  0x62, 0xf2, 0x7d, 0xdf, 0xd2, 0x00                                        }, {  {evex} VPDPWUSD zmm0{k7}{z}, zmm0, dword [rax]{1to16}                }
testcase        {  0x62, 0xb2, 0x7d, 0xdf, 0xd2, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUSD zmm0{k7}{z}, zmm0, dword [rbp+r14*2+0x8]{1to16}      }
testcase        {  0x62, 0xb2, 0x75, 0x18, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSD xmm1, xmm1, dword [rbp+r14*2+0x8]{1to4}              }
testcase        {  0x62, 0xf2, 0x75, 0x0f, 0xd2, 0xc9                                        }, {  {evex} VPDPWUSD xmm1{k7}, xmm1, xmm1                                 }
testcase        {  0x62, 0xf2, 0x75, 0x0f, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD xmm1{k7}, xmm1, oword [rax]                          }
testcase        {  0x62, 0xb2, 0x75, 0x0f, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSD xmm1{k7}, xmm1, oword [rbp+r14*2+0x8]                }
testcase        {  0x62, 0xf2, 0x75, 0x1f, 0xd2, 0x08                                        }, {  {evex} VPDPWUSD xmm1{k7}, xmm1, dword [rax]{1to4}                    }
testcase        {  0x62, 0xb2, 0x75, 0x1f, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSD xmm1{k7}, xmm1, dword [rbp+r14*2+0x8]{1to4}          }
testcase        {  0x62, 0xf2, 0x75, 0x08, 0xd3, 0xc9                                        }, {  {evex} VPDPWUSDS xmm1, xmm1, xmm1                                    }
testcase        {  0x62, 0xf2, 0x75, 0x8f, 0xd3, 0xc9                                        }, {  {evex} VPDPWUSDS xmm1{k7}{z}, xmm1, xmm1                             }
testcase        {  0x62, 0xf2, 0x75, 0x8f, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS xmm1{k7}{z}, xmm1, oword [rax]                      }
testcase        {  0x62, 0xb2, 0x75, 0x8f, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSDS xmm1{k7}{z}, xmm1, oword [rbp+r14*2+0x8]            }
testcase        {  0x62, 0xf2, 0x75, 0x9f, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS xmm1{k7}{z}, xmm1, dword [rax]{1to4}                }
testcase        {  0x62, 0xb2, 0x75, 0x9f, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSDS xmm1{k7}{z}, xmm1, dword [rbp+r14*2+0x8]{1to4}      }
testcase        {  0x62, 0xf2, 0x75, 0x28, 0xd3, 0xc9                                        }, {  {evex} VPDPWUSDS ymm1, ymm1, ymm1                                    }
testcase        {  0x62, 0xf2, 0x75, 0x28, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS ymm1, ymm1, yword [rax]                             }
testcase        {  0x62, 0xb2, 0x75, 0x28, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSDS ymm1, ymm1, yword [rbp+r14*2+0x8]                   }
testcase        {  0x62, 0xf2, 0x75, 0x38, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS ymm1, ymm1, dword [rax]{1to8}                       }
testcase        {  0x62, 0xb2, 0x75, 0x38, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSDS ymm1, ymm1, dword [rbp+r14*2+0x8]{1to8}             }
testcase        {  0x62, 0xf2, 0x75, 0x08, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS xmm1, xmm1, oword [rax]                             }
testcase        {  0x62, 0xf2, 0x75, 0x2f, 0xd3, 0xc9                                        }, {  {evex} VPDPWUSDS ymm1{k7}, ymm1, ymm1                                }
testcase        {  0x62, 0xf2, 0x75, 0x2f, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS ymm1{k7}, ymm1, yword [rax]                         }
testcase        {  0x62, 0xb2, 0x75, 0x2f, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSDS ymm1{k7}, ymm1, yword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf2, 0x75, 0x3f, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS ymm1{k7}, ymm1, dword [rax]{1to8}                   }
testcase        {  0x62, 0xb2, 0x75, 0x3f, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSDS ymm1{k7}, ymm1, dword [rbp+r14*2+0x8]{1to8}         }
testcase        {  0x62, 0xf2, 0x75, 0xaf, 0xd3, 0xc9                                        }, {  {evex} VPDPWUSDS ymm1{k7}{z}, ymm1, ymm1                             }
testcase        {  0x62, 0xf2, 0x75, 0xaf, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS ymm1{k7}{z}, ymm1, yword [rax]                      }
testcase        {  0x62, 0xb2, 0x75, 0xaf, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSDS ymm1{k7}{z}, ymm1, yword [rbp+r14*2+0x8]            }
testcase        {  0x62, 0xf2, 0x75, 0xbf, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS ymm1{k7}{z}, ymm1, dword [rax]{1to8}                }
testcase        {  0x62, 0xb2, 0x75, 0xbf, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSDS ymm1{k7}{z}, ymm1, dword [rbp+r14*2+0x8]{1to8}      }
testcase        {  0x62, 0xb2, 0x75, 0x08, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSDS xmm1, xmm1, oword [rbp+r14*2+0x8]                   }
testcase        {  0x62, 0xf2, 0x7d, 0x48, 0xd3, 0xc1                                        }, {  {evex} VPDPWUSDS zmm0, zmm0, zmm1                                    }
testcase        {  0x62, 0xf2, 0x7d, 0x48, 0xd3, 0x00                                        }, {  {evex} VPDPWUSDS zmm0, zmm0, zword [rax]                             }
testcase        {  0x62, 0xb2, 0x7d, 0x48, 0xd3, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSDS zmm0, zmm0, zword [rbp+r14*2+0x8]                   }
testcase        {  0x62, 0xf2, 0x7d, 0x58, 0xd3, 0x00                                        }, {  {evex} VPDPWUSDS zmm0, zmm0, dword [rax]{1to16}                      }
testcase        {  0x62, 0xb2, 0x7d, 0x58, 0xd3, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUSDS zmm0, zmm0, dword [rbp+r14*2+0x8]{1to16}            }
testcase        {  0x62, 0xf2, 0x7d, 0x4f, 0xd3, 0xc1                                        }, {  {evex} VPDPWUSDS zmm0{k7}, zmm0, zmm1                                }
testcase        {  0x62, 0xf2, 0x7d, 0x4f, 0xd3, 0x00                                        }, {  {evex} VPDPWUSDS zmm0{k7}, zmm0, zword [rax]                         }
testcase        {  0x62, 0xb2, 0x7d, 0x4f, 0xd3, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSDS zmm0{k7}, zmm0, zword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf2, 0x7d, 0x5f, 0xd3, 0x00                                        }, {  {evex} VPDPWUSDS zmm0{k7}, zmm0, dword [rax]{1to16}                  }
testcase        {  0x62, 0xb2, 0x7d, 0x5f, 0xd3, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUSDS zmm0{k7}, zmm0, dword [rbp+r14*2+0x8]{1to16}        }
testcase        {  0x62, 0xf2, 0x75, 0x18, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS xmm1, xmm1, dword [rax]{1to4}                       }
testcase        {  0x62, 0xf2, 0x7d, 0xcf, 0xd3, 0xc1                                        }, {  {evex} VPDPWUSDS zmm0{k7}{z}, zmm0, zmm1                             }
testcase        {  0x62, 0xf2, 0x7d, 0xcf, 0xd3, 0x00                                        }, {  {evex} VPDPWUSDS zmm0{k7}{z}, zmm0, zword [rax]                      }
testcase        {  0x62, 0xb2, 0x7d, 0xcf, 0xd3, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSDS zmm0{k7}{z}, zmm0, zword [rbp+r14*2+0x8]            }
testcase        {  0x62, 0xf2, 0x7d, 0xdf, 0xd3, 0x00                                        }, {  {evex} VPDPWUSDS zmm0{k7}{z}, zmm0, dword [rax]{1to16}               }
testcase        {  0x62, 0xb2, 0x7d, 0xdf, 0xd3, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUSDS zmm0{k7}{z}, zmm0, dword [rbp+r14*2+0x8]{1to16}     }
testcase        {  0x62, 0xb2, 0x75, 0x18, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSDS xmm1, xmm1, dword [rbp+r14*2+0x8]{1to4}             }
testcase        {  0x62, 0xf2, 0x75, 0x0f, 0xd3, 0xc9                                        }, {  {evex} VPDPWUSDS xmm1{k7}, xmm1, xmm1                                }
testcase        {  0x62, 0xf2, 0x75, 0x0f, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS xmm1{k7}, xmm1, oword [rax]                         }
testcase        {  0x62, 0xb2, 0x75, 0x0f, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSDS xmm1{k7}, xmm1, oword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf2, 0x75, 0x1f, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS xmm1{k7}, xmm1, dword [rax]{1to4}                   }
testcase        {  0x62, 0xb2, 0x75, 0x1f, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSDS xmm1{k7}, xmm1, dword [rbp+r14*2+0x8]{1to4}         }
testcase        {  0x62, 0xf2, 0x75, 0x08, 0xd3, 0xc9                                        }, {  {evex} VPDPWUSDS xmm1, xmm1, xmm1                                    }
testcase        {  0x62, 0xf2, 0x75, 0x8f, 0xd3, 0xc9                                        }, {  {evex} VPDPWUSDS xmm1{k7}{z}, xmm1, xmm1                             }
testcase        {  0x62, 0xf2, 0x75, 0x8f, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS xmm1{k7}{z}, xmm1, oword [rax]                      }
testcase        {  0x62, 0xb2, 0x75, 0x8f, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSDS xmm1{k7}{z}, xmm1, oword [rbp+r14*2+0x8]            }
testcase        {  0x62, 0xf2, 0x75, 0x9f, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS xmm1{k7}{z}, xmm1, dword [rax]{1to4}                }
testcase        {  0x62, 0xb2, 0x75, 0x9f, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSDS xmm1{k7}{z}, xmm1, dword [rbp+r14*2+0x8]{1to4}      }
testcase        {  0x62, 0xf2, 0x75, 0x28, 0xd3, 0xc9                                        }, {  {evex} VPDPWUSDS ymm1, ymm1, ymm1                                    }
testcase        {  0x62, 0xf2, 0x75, 0x28, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS ymm1, ymm1, yword [rax]                             }
testcase        {  0x62, 0xb2, 0x75, 0x28, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSDS ymm1, ymm1, yword [rbp+r14*2+0x8]                   }
testcase        {  0x62, 0xf2, 0x75, 0x38, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS ymm1, ymm1, dword [rax]{1to8}                       }
testcase        {  0x62, 0xb2, 0x75, 0x38, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSDS ymm1, ymm1, dword [rbp+r14*2+0x8]{1to8}             }
testcase        {  0x62, 0xf2, 0x75, 0x08, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS xmm1, xmm1, oword [rax]                             }
testcase        {  0x62, 0xf2, 0x75, 0x2f, 0xd3, 0xc9                                        }, {  {evex} VPDPWUSDS ymm1{k7}, ymm1, ymm1                                }
testcase        {  0x62, 0xf2, 0x75, 0x2f, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS ymm1{k7}, ymm1, yword [rax]                         }
testcase        {  0x62, 0xb2, 0x75, 0x2f, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSDS ymm1{k7}, ymm1, yword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf2, 0x75, 0x3f, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS ymm1{k7}, ymm1, dword [rax]{1to8}                   }
testcase        {  0x62, 0xb2, 0x75, 0x3f, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSDS ymm1{k7}, ymm1, dword [rbp+r14*2+0x8]{1to8}         }
testcase        {  0x62, 0xf2, 0x75, 0xaf, 0xd3, 0xc9                                        }, {  {evex} VPDPWUSDS ymm1{k7}{z}, ymm1, ymm1                             }
testcase        {  0x62, 0xf2, 0x75, 0xaf, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS ymm1{k7}{z}, ymm1, yword [rax]                      }
testcase        {  0x62, 0xb2, 0x75, 0xaf, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSDS ymm1{k7}{z}, ymm1, yword [rbp+r14*2+0x8]            }
testcase        {  0x62, 0xf2, 0x75, 0xbf, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS ymm1{k7}{z}, ymm1, dword [rax]{1to8}                }
testcase        {  0x62, 0xb2, 0x75, 0xbf, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSDS ymm1{k7}{z}, ymm1, dword [rbp+r14*2+0x8]{1to8}      }
testcase        {  0x62, 0xb2, 0x75, 0x08, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSDS xmm1, xmm1, oword [rbp+r14*2+0x8]                   }
testcase        {  0x62, 0xf2, 0x7d, 0x48, 0xd3, 0xc1                                        }, {  {evex} VPDPWUSDS zmm0, zmm0, zmm1                                    }
testcase        {  0x62, 0xf2, 0x7d, 0x48, 0xd3, 0x00                                        }, {  {evex} VPDPWUSDS zmm0, zmm0, zword [rax]                             }
testcase        {  0x62, 0xb2, 0x7d, 0x48, 0xd3, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSDS zmm0, zmm0, zword [rbp+r14*2+0x8]                   }
testcase        {  0x62, 0xf2, 0x7d, 0x58, 0xd3, 0x00                                        }, {  {evex} VPDPWUSDS zmm0, zmm0, dword [rax]{1to16}                      }
testcase        {  0x62, 0xb2, 0x7d, 0x58, 0xd3, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUSDS zmm0, zmm0, dword [rbp+r14*2+0x8]{1to16}            }
testcase        {  0x62, 0xf2, 0x7d, 0x4f, 0xd3, 0xc1                                        }, {  {evex} VPDPWUSDS zmm0{k7}, zmm0, zmm1                                }
testcase        {  0x62, 0xf2, 0x7d, 0x4f, 0xd3, 0x00                                        }, {  {evex} VPDPWUSDS zmm0{k7}, zmm0, zword [rax]                         }
testcase        {  0x62, 0xb2, 0x7d, 0x4f, 0xd3, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSDS zmm0{k7}, zmm0, zword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf2, 0x7d, 0x5f, 0xd3, 0x00                                        }, {  {evex} VPDPWUSDS zmm0{k7}, zmm0, dword [rax]{1to16}                  }
testcase        {  0x62, 0xb2, 0x7d, 0x5f, 0xd3, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUSDS zmm0{k7}, zmm0, dword [rbp+r14*2+0x8]{1to16}        }
testcase        {  0x62, 0xf2, 0x75, 0x18, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS xmm1, xmm1, dword [rax]{1to4}                       }
testcase        {  0x62, 0xf2, 0x7d, 0xcf, 0xd3, 0xc1                                        }, {  {evex} VPDPWUSDS zmm0{k7}{z}, zmm0, zmm1                             }
testcase        {  0x62, 0xf2, 0x7d, 0xcf, 0xd3, 0x00                                        }, {  {evex} VPDPWUSDS zmm0{k7}{z}, zmm0, zword [rax]                      }
testcase        {  0x62, 0xb2, 0x7d, 0xcf, 0xd3, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSDS zmm0{k7}{z}, zmm0, zword [rbp+r14*2+0x8]            }
testcase        {  0x62, 0xf2, 0x7d, 0xdf, 0xd3, 0x00                                        }, {  {evex} VPDPWUSDS zmm0{k7}{z}, zmm0, dword [rax]{1to16}               }
testcase        {  0x62, 0xb2, 0x7d, 0xdf, 0xd3, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUSDS zmm0{k7}{z}, zmm0, dword [rbp+r14*2+0x8]{1to16}     }
testcase        {  0x62, 0xb2, 0x75, 0x18, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSDS xmm1, xmm1, dword [rbp+r14*2+0x8]{1to4}             }
testcase        {  0x62, 0xf2, 0x75, 0x0f, 0xd3, 0xc9                                        }, {  {evex} VPDPWUSDS xmm1{k7}, xmm1, xmm1                                }
testcase        {  0x62, 0xf2, 0x75, 0x0f, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS xmm1{k7}, xmm1, oword [rax]                         }
testcase        {  0x62, 0xb2, 0x75, 0x0f, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSDS xmm1{k7}, xmm1, oword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf2, 0x75, 0x1f, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS xmm1{k7}, xmm1, dword [rax]{1to4}                   }
testcase        {  0x62, 0xb2, 0x75, 0x1f, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSDS xmm1{k7}, xmm1, dword [rbp+r14*2+0x8]{1to4}         }
testcase        {  0x62, 0xf2, 0x75, 0x08, 0xd3, 0xc9                                        }, {  {evex} VPDPWUSDS xmm1, xmm1, xmm1                                    }
testcase        {  0x62, 0xf2, 0x75, 0x8f, 0xd3, 0xc9                                        }, {  {evex} VPDPWUSDS xmm1{k7}{z}, xmm1, xmm1                             }
testcase        {  0x62, 0xf2, 0x75, 0x8f, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS xmm1{k7}{z}, xmm1, oword [rax]                      }
testcase        {  0x62, 0xb2, 0x75, 0x8f, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSDS xmm1{k7}{z}, xmm1, oword [rbp+r14*2+0x8]            }
testcase        {  0x62, 0xf2, 0x75, 0x9f, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS xmm1{k7}{z}, xmm1, dword [rax]{1to4}                }
testcase        {  0x62, 0xb2, 0x75, 0x9f, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSDS xmm1{k7}{z}, xmm1, dword [rbp+r14*2+0x8]{1to4}      }
testcase        {  0x62, 0xf2, 0x75, 0x28, 0xd3, 0xc9                                        }, {  {evex} VPDPWUSDS ymm1, ymm1, ymm1                                    }
testcase        {  0x62, 0xf2, 0x75, 0x28, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS ymm1, ymm1, yword [rax]                             }
testcase        {  0x62, 0xb2, 0x75, 0x28, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSDS ymm1, ymm1, yword [rbp+r14*2+0x8]                   }
testcase        {  0x62, 0xf2, 0x75, 0x38, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS ymm1, ymm1, dword [rax]{1to8}                       }
testcase        {  0x62, 0xb2, 0x75, 0x38, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSDS ymm1, ymm1, dword [rbp+r14*2+0x8]{1to8}             }
testcase        {  0x62, 0xf2, 0x75, 0x08, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS xmm1, xmm1, oword [rax]                             }
testcase        {  0x62, 0xf2, 0x75, 0x2f, 0xd3, 0xc9                                        }, {  {evex} VPDPWUSDS ymm1{k7}, ymm1, ymm1                                }
testcase        {  0x62, 0xf2, 0x75, 0x2f, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS ymm1{k7}, ymm1, yword [rax]                         }
testcase        {  0x62, 0xb2, 0x75, 0x2f, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSDS ymm1{k7}, ymm1, yword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf2, 0x75, 0x3f, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS ymm1{k7}, ymm1, dword [rax]{1to8}                   }
testcase        {  0x62, 0xb2, 0x75, 0x3f, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSDS ymm1{k7}, ymm1, dword [rbp+r14*2+0x8]{1to8}         }
testcase        {  0x62, 0xf2, 0x75, 0xaf, 0xd3, 0xc9                                        }, {  {evex} VPDPWUSDS ymm1{k7}{z}, ymm1, ymm1                             }
testcase        {  0x62, 0xf2, 0x75, 0xaf, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS ymm1{k7}{z}, ymm1, yword [rax]                      }
testcase        {  0x62, 0xb2, 0x75, 0xaf, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSDS ymm1{k7}{z}, ymm1, yword [rbp+r14*2+0x8]            }
testcase        {  0x62, 0xf2, 0x75, 0xbf, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS ymm1{k7}{z}, ymm1, dword [rax]{1to8}                }
testcase        {  0x62, 0xb2, 0x75, 0xbf, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSDS ymm1{k7}{z}, ymm1, dword [rbp+r14*2+0x8]{1to8}      }
testcase        {  0x62, 0xb2, 0x75, 0x08, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSDS xmm1, xmm1, oword [rbp+r14*2+0x8]                   }
testcase        {  0x62, 0xf2, 0x7d, 0x48, 0xd3, 0xc1                                        }, {  {evex} VPDPWUSDS zmm0, zmm0, zmm1                                    }
testcase        {  0x62, 0xf2, 0x7d, 0x48, 0xd3, 0x00                                        }, {  {evex} VPDPWUSDS zmm0, zmm0, zword [rax]                             }
testcase        {  0x62, 0xb2, 0x7d, 0x48, 0xd3, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSDS zmm0, zmm0, zword [rbp+r14*2+0x8]                   }
testcase        {  0x62, 0xf2, 0x7d, 0x58, 0xd3, 0x00                                        }, {  {evex} VPDPWUSDS zmm0, zmm0, dword [rax]{1to16}                      }
testcase        {  0x62, 0xb2, 0x7d, 0x58, 0xd3, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUSDS zmm0, zmm0, dword [rbp+r14*2+0x8]{1to16}            }
testcase        {  0x62, 0xf2, 0x7d, 0x4f, 0xd3, 0xc1                                        }, {  {evex} VPDPWUSDS zmm0{k7}, zmm0, zmm1                                }
testcase        {  0x62, 0xf2, 0x7d, 0x4f, 0xd3, 0x00                                        }, {  {evex} VPDPWUSDS zmm0{k7}, zmm0, zword [rax]                         }
testcase        {  0x62, 0xb2, 0x7d, 0x4f, 0xd3, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSDS zmm0{k7}, zmm0, zword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf2, 0x7d, 0x5f, 0xd3, 0x00                                        }, {  {evex} VPDPWUSDS zmm0{k7}, zmm0, dword [rax]{1to16}                  }
testcase        {  0x62, 0xb2, 0x7d, 0x5f, 0xd3, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUSDS zmm0{k7}, zmm0, dword [rbp+r14*2+0x8]{1to16}        }
testcase        {  0x62, 0xf2, 0x75, 0x18, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS xmm1, xmm1, dword [rax]{1to4}                       }
testcase        {  0x62, 0xf2, 0x7d, 0xcf, 0xd3, 0xc1                                        }, {  {evex} VPDPWUSDS zmm0{k7}{z}, zmm0, zmm1                             }
testcase        {  0x62, 0xf2, 0x7d, 0xcf, 0xd3, 0x00                                        }, {  {evex} VPDPWUSDS zmm0{k7}{z}, zmm0, zword [rax]                      }
testcase        {  0x62, 0xb2, 0x7d, 0xcf, 0xd3, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSDS zmm0{k7}{z}, zmm0, zword [rbp+r14*2+0x8]            }
testcase        {  0x62, 0xf2, 0x7d, 0xdf, 0xd3, 0x00                                        }, {  {evex} VPDPWUSDS zmm0{k7}{z}, zmm0, dword [rax]{1to16}               }
testcase        {  0x62, 0xb2, 0x7d, 0xdf, 0xd3, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUSDS zmm0{k7}{z}, zmm0, dword [rbp+r14*2+0x8]{1to16}     }
testcase        {  0x62, 0xb2, 0x75, 0x18, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSDS xmm1, xmm1, dword [rbp+r14*2+0x8]{1to4}             }
testcase        {  0x62, 0xf2, 0x75, 0x0f, 0xd3, 0xc9                                        }, {  {evex} VPDPWUSDS xmm1{k7}, xmm1, xmm1                                }
testcase        {  0x62, 0xf2, 0x75, 0x0f, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS xmm1{k7}, xmm1, oword [rax]                         }
testcase        {  0x62, 0xb2, 0x75, 0x0f, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUSDS xmm1{k7}, xmm1, oword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf2, 0x75, 0x1f, 0xd3, 0x08                                        }, {  {evex} VPDPWUSDS xmm1{k7}, xmm1, dword [rax]{1to4}                   }
testcase        {  0x62, 0xb2, 0x75, 0x1f, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUSDS xmm1{k7}, xmm1, dword [rbp+r14*2+0x8]{1to4}         }
testcase        {  0x62, 0xf2, 0x74, 0x08, 0xd2, 0xc9                                        }, {  {evex} VPDPWUUD xmm1, xmm1, xmm1                                     }
testcase        {  0x62, 0xf2, 0x74, 0x8f, 0xd2, 0xc9                                        }, {  {evex} VPDPWUUD xmm1{k7}{z}, xmm1, xmm1                              }
testcase        {  0x62, 0xf2, 0x74, 0x8f, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD xmm1{k7}{z}, xmm1, oword [rax]                       }
testcase        {  0x62, 0xb2, 0x74, 0x8f, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUD xmm1{k7}{z}, xmm1, oword [rbp+r14*2+0x8]             }
testcase        {  0x62, 0xf2, 0x74, 0x9f, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD xmm1{k7}{z}, xmm1, dword [rax]{1to4}                 }
testcase        {  0x62, 0xb2, 0x74, 0x9f, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUD xmm1{k7}{z}, xmm1, dword [rbp+r14*2+0x8]{1to4}       }
testcase        {  0x62, 0xf2, 0x74, 0x28, 0xd2, 0xc9                                        }, {  {evex} VPDPWUUD ymm1, ymm1, ymm1                                     }
testcase        {  0x62, 0xf2, 0x74, 0x28, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD ymm1, ymm1, yword [rax]                              }
testcase        {  0x62, 0xb2, 0x74, 0x28, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUD ymm1, ymm1, yword [rbp+r14*2+0x8]                    }
testcase        {  0x62, 0xf2, 0x74, 0x38, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD ymm1, ymm1, dword [rax]{1to8}                        }
testcase        {  0x62, 0xb2, 0x74, 0x38, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUD ymm1, ymm1, dword [rbp+r14*2+0x8]{1to8}              }
testcase        {  0x62, 0xf2, 0x74, 0x08, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD xmm1, xmm1, oword [rax]                              }
testcase        {  0x62, 0xf2, 0x74, 0x2f, 0xd2, 0xc9                                        }, {  {evex} VPDPWUUD ymm1{k7}, ymm1, ymm1                                 }
testcase        {  0x62, 0xf2, 0x74, 0x2f, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD ymm1{k7}, ymm1, yword [rax]                          }
testcase        {  0x62, 0xb2, 0x74, 0x2f, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUD ymm1{k7}, ymm1, yword [rbp+r14*2+0x8]                }
testcase        {  0x62, 0xf2, 0x74, 0x3f, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD ymm1{k7}, ymm1, dword [rax]{1to8}                    }
testcase        {  0x62, 0xb2, 0x74, 0x3f, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUD ymm1{k7}, ymm1, dword [rbp+r14*2+0x8]{1to8}          }
testcase        {  0x62, 0xf2, 0x74, 0xaf, 0xd2, 0xc9                                        }, {  {evex} VPDPWUUD ymm1{k7}{z}, ymm1, ymm1                              }
testcase        {  0x62, 0xf2, 0x74, 0xaf, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD ymm1{k7}{z}, ymm1, yword [rax]                       }
testcase        {  0x62, 0xb2, 0x74, 0xaf, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUD ymm1{k7}{z}, ymm1, yword [rbp+r14*2+0x8]             }
testcase        {  0x62, 0xf2, 0x74, 0xbf, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD ymm1{k7}{z}, ymm1, dword [rax]{1to8}                 }
testcase        {  0x62, 0xb2, 0x74, 0xbf, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUD ymm1{k7}{z}, ymm1, dword [rbp+r14*2+0x8]{1to8}       }
testcase        {  0x62, 0xb2, 0x74, 0x08, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUD xmm1, xmm1, oword [rbp+r14*2+0x8]                    }
testcase        {  0x62, 0xf2, 0x7c, 0x48, 0xd2, 0xc1                                        }, {  {evex} VPDPWUUD zmm0, zmm0, zmm1                                     }
testcase        {  0x62, 0xf2, 0x7c, 0x48, 0xd2, 0x00                                        }, {  {evex} VPDPWUUD zmm0, zmm0, zword [rax]                              }
testcase        {  0x62, 0xb2, 0x7c, 0x48, 0xd2, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUD zmm0, zmm0, zword [rbp+r14*2+0x8]                    }
testcase        {  0x62, 0xf2, 0x7c, 0x58, 0xd2, 0x00                                        }, {  {evex} VPDPWUUD zmm0, zmm0, dword [rax]{1to16}                       }
testcase        {  0x62, 0xb2, 0x7c, 0x58, 0xd2, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUUD zmm0, zmm0, dword [rbp+r14*2+0x8]{1to16}             }
testcase        {  0x62, 0xf2, 0x7c, 0x4f, 0xd2, 0xc1                                        }, {  {evex} VPDPWUUD zmm0{k7}, zmm0, zmm1                                 }
testcase        {  0x62, 0xf2, 0x7c, 0x4f, 0xd2, 0x00                                        }, {  {evex} VPDPWUUD zmm0{k7}, zmm0, zword [rax]                          }
testcase        {  0x62, 0xb2, 0x7c, 0x4f, 0xd2, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUD zmm0{k7}, zmm0, zword [rbp+r14*2+0x8]                }
testcase        {  0x62, 0xf2, 0x7c, 0x5f, 0xd2, 0x00                                        }, {  {evex} VPDPWUUD zmm0{k7}, zmm0, dword [rax]{1to16}                   }
testcase        {  0x62, 0xb2, 0x7c, 0x5f, 0xd2, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUUD zmm0{k7}, zmm0, dword [rbp+r14*2+0x8]{1to16}         }
testcase        {  0x62, 0xf2, 0x74, 0x18, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD xmm1, xmm1, dword [rax]{1to4}                        }
testcase        {  0x62, 0xf2, 0x7c, 0xcf, 0xd2, 0xc1                                        }, {  {evex} VPDPWUUD zmm0{k7}{z}, zmm0, zmm1                              }
testcase        {  0x62, 0xf2, 0x7c, 0xcf, 0xd2, 0x00                                        }, {  {evex} VPDPWUUD zmm0{k7}{z}, zmm0, zword [rax]                       }
testcase        {  0x62, 0xb2, 0x7c, 0xcf, 0xd2, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUD zmm0{k7}{z}, zmm0, zword [rbp+r14*2+0x8]             }
testcase        {  0x62, 0xf2, 0x7c, 0xdf, 0xd2, 0x00                                        }, {  {evex} VPDPWUUD zmm0{k7}{z}, zmm0, dword [rax]{1to16}                }
testcase        {  0x62, 0xb2, 0x7c, 0xdf, 0xd2, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUUD zmm0{k7}{z}, zmm0, dword [rbp+r14*2+0x8]{1to16}      }
testcase        {  0x62, 0xb2, 0x74, 0x18, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUD xmm1, xmm1, dword [rbp+r14*2+0x8]{1to4}              }
testcase        {  0x62, 0xf2, 0x74, 0x0f, 0xd2, 0xc9                                        }, {  {evex} VPDPWUUD xmm1{k7}, xmm1, xmm1                                 }
testcase        {  0x62, 0xf2, 0x74, 0x0f, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD xmm1{k7}, xmm1, oword [rax]                          }
testcase        {  0x62, 0xb2, 0x74, 0x0f, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUD xmm1{k7}, xmm1, oword [rbp+r14*2+0x8]                }
testcase        {  0x62, 0xf2, 0x74, 0x1f, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD xmm1{k7}, xmm1, dword [rax]{1to4}                    }
testcase        {  0x62, 0xb2, 0x74, 0x1f, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUD xmm1{k7}, xmm1, dword [rbp+r14*2+0x8]{1to4}          }
testcase        {  0x62, 0xf2, 0x74, 0x08, 0xd2, 0xc9                                        }, {  {evex} VPDPWUUD xmm1, xmm1, xmm1                                     }
testcase        {  0x62, 0xf2, 0x74, 0x8f, 0xd2, 0xc9                                        }, {  {evex} VPDPWUUD xmm1{k7}{z}, xmm1, xmm1                              }
testcase        {  0x62, 0xf2, 0x74, 0x8f, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD xmm1{k7}{z}, xmm1, oword [rax]                       }
testcase        {  0x62, 0xb2, 0x74, 0x8f, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUD xmm1{k7}{z}, xmm1, oword [rbp+r14*2+0x8]             }
testcase        {  0x62, 0xf2, 0x74, 0x9f, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD xmm1{k7}{z}, xmm1, dword [rax]{1to4}                 }
testcase        {  0x62, 0xb2, 0x74, 0x9f, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUD xmm1{k7}{z}, xmm1, dword [rbp+r14*2+0x8]{1to4}       }
testcase        {  0x62, 0xf2, 0x74, 0x28, 0xd2, 0xc9                                        }, {  {evex} VPDPWUUD ymm1, ymm1, ymm1                                     }
testcase        {  0x62, 0xf2, 0x74, 0x28, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD ymm1, ymm1, yword [rax]                              }
testcase        {  0x62, 0xb2, 0x74, 0x28, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUD ymm1, ymm1, yword [rbp+r14*2+0x8]                    }
testcase        {  0x62, 0xf2, 0x74, 0x38, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD ymm1, ymm1, dword [rax]{1to8}                        }
testcase        {  0x62, 0xb2, 0x74, 0x38, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUD ymm1, ymm1, dword [rbp+r14*2+0x8]{1to8}              }
testcase        {  0x62, 0xf2, 0x74, 0x08, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD xmm1, xmm1, oword [rax]                              }
testcase        {  0x62, 0xf2, 0x74, 0x2f, 0xd2, 0xc9                                        }, {  {evex} VPDPWUUD ymm1{k7}, ymm1, ymm1                                 }
testcase        {  0x62, 0xf2, 0x74, 0x2f, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD ymm1{k7}, ymm1, yword [rax]                          }
testcase        {  0x62, 0xb2, 0x74, 0x2f, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUD ymm1{k7}, ymm1, yword [rbp+r14*2+0x8]                }
testcase        {  0x62, 0xf2, 0x74, 0x3f, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD ymm1{k7}, ymm1, dword [rax]{1to8}                    }
testcase        {  0x62, 0xb2, 0x74, 0x3f, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUD ymm1{k7}, ymm1, dword [rbp+r14*2+0x8]{1to8}          }
testcase        {  0x62, 0xf2, 0x74, 0xaf, 0xd2, 0xc9                                        }, {  {evex} VPDPWUUD ymm1{k7}{z}, ymm1, ymm1                              }
testcase        {  0x62, 0xf2, 0x74, 0xaf, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD ymm1{k7}{z}, ymm1, yword [rax]                       }
testcase        {  0x62, 0xb2, 0x74, 0xaf, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUD ymm1{k7}{z}, ymm1, yword [rbp+r14*2+0x8]             }
testcase        {  0x62, 0xf2, 0x74, 0xbf, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD ymm1{k7}{z}, ymm1, dword [rax]{1to8}                 }
testcase        {  0x62, 0xb2, 0x74, 0xbf, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUD ymm1{k7}{z}, ymm1, dword [rbp+r14*2+0x8]{1to8}       }
testcase        {  0x62, 0xb2, 0x74, 0x08, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUD xmm1, xmm1, oword [rbp+r14*2+0x8]                    }
testcase        {  0x62, 0xf2, 0x7c, 0x48, 0xd2, 0xc1                                        }, {  {evex} VPDPWUUD zmm0, zmm0, zmm1                                     }
testcase        {  0x62, 0xf2, 0x7c, 0x48, 0xd2, 0x00                                        }, {  {evex} VPDPWUUD zmm0, zmm0, zword [rax]                              }
testcase        {  0x62, 0xb2, 0x7c, 0x48, 0xd2, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUD zmm0, zmm0, zword [rbp+r14*2+0x8]                    }
testcase        {  0x62, 0xf2, 0x7c, 0x58, 0xd2, 0x00                                        }, {  {evex} VPDPWUUD zmm0, zmm0, dword [rax]{1to16}                       }
testcase        {  0x62, 0xb2, 0x7c, 0x58, 0xd2, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUUD zmm0, zmm0, dword [rbp+r14*2+0x8]{1to16}             }
testcase        {  0x62, 0xf2, 0x7c, 0x4f, 0xd2, 0xc1                                        }, {  {evex} VPDPWUUD zmm0{k7}, zmm0, zmm1                                 }
testcase        {  0x62, 0xf2, 0x7c, 0x4f, 0xd2, 0x00                                        }, {  {evex} VPDPWUUD zmm0{k7}, zmm0, zword [rax]                          }
testcase        {  0x62, 0xb2, 0x7c, 0x4f, 0xd2, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUD zmm0{k7}, zmm0, zword [rbp+r14*2+0x8]                }
testcase        {  0x62, 0xf2, 0x7c, 0x5f, 0xd2, 0x00                                        }, {  {evex} VPDPWUUD zmm0{k7}, zmm0, dword [rax]{1to16}                   }
testcase        {  0x62, 0xb2, 0x7c, 0x5f, 0xd2, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUUD zmm0{k7}, zmm0, dword [rbp+r14*2+0x8]{1to16}         }
testcase        {  0x62, 0xf2, 0x74, 0x18, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD xmm1, xmm1, dword [rax]{1to4}                        }
testcase        {  0x62, 0xf2, 0x7c, 0xcf, 0xd2, 0xc1                                        }, {  {evex} VPDPWUUD zmm0{k7}{z}, zmm0, zmm1                              }
testcase        {  0x62, 0xf2, 0x7c, 0xcf, 0xd2, 0x00                                        }, {  {evex} VPDPWUUD zmm0{k7}{z}, zmm0, zword [rax]                       }
testcase        {  0x62, 0xb2, 0x7c, 0xcf, 0xd2, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUD zmm0{k7}{z}, zmm0, zword [rbp+r14*2+0x8]             }
testcase        {  0x62, 0xf2, 0x7c, 0xdf, 0xd2, 0x00                                        }, {  {evex} VPDPWUUD zmm0{k7}{z}, zmm0, dword [rax]{1to16}                }
testcase        {  0x62, 0xb2, 0x7c, 0xdf, 0xd2, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUUD zmm0{k7}{z}, zmm0, dword [rbp+r14*2+0x8]{1to16}      }
testcase        {  0x62, 0xb2, 0x74, 0x18, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUD xmm1, xmm1, dword [rbp+r14*2+0x8]{1to4}              }
testcase        {  0x62, 0xf2, 0x74, 0x0f, 0xd2, 0xc9                                        }, {  {evex} VPDPWUUD xmm1{k7}, xmm1, xmm1                                 }
testcase        {  0x62, 0xf2, 0x74, 0x0f, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD xmm1{k7}, xmm1, oword [rax]                          }
testcase        {  0x62, 0xb2, 0x74, 0x0f, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUD xmm1{k7}, xmm1, oword [rbp+r14*2+0x8]                }
testcase        {  0x62, 0xf2, 0x74, 0x1f, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD xmm1{k7}, xmm1, dword [rax]{1to4}                    }
testcase        {  0x62, 0xb2, 0x74, 0x1f, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUD xmm1{k7}, xmm1, dword [rbp+r14*2+0x8]{1to4}          }
testcase        {  0x62, 0xf2, 0x74, 0x08, 0xd2, 0xc9                                        }, {  {evex} VPDPWUUD xmm1, xmm1, xmm1                                     }
testcase        {  0x62, 0xf2, 0x74, 0x8f, 0xd2, 0xc9                                        }, {  {evex} VPDPWUUD xmm1{k7}{z}, xmm1, xmm1                              }
testcase        {  0x62, 0xf2, 0x74, 0x8f, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD xmm1{k7}{z}, xmm1, oword [rax]                       }
testcase        {  0x62, 0xb2, 0x74, 0x8f, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUD xmm1{k7}{z}, xmm1, oword [rbp+r14*2+0x8]             }
testcase        {  0x62, 0xf2, 0x74, 0x9f, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD xmm1{k7}{z}, xmm1, dword [rax]{1to4}                 }
testcase        {  0x62, 0xb2, 0x74, 0x9f, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUD xmm1{k7}{z}, xmm1, dword [rbp+r14*2+0x8]{1to4}       }
testcase        {  0x62, 0xf2, 0x74, 0x28, 0xd2, 0xc9                                        }, {  {evex} VPDPWUUD ymm1, ymm1, ymm1                                     }
testcase        {  0x62, 0xf2, 0x74, 0x28, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD ymm1, ymm1, yword [rax]                              }
testcase        {  0x62, 0xb2, 0x74, 0x28, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUD ymm1, ymm1, yword [rbp+r14*2+0x8]                    }
testcase        {  0x62, 0xf2, 0x74, 0x38, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD ymm1, ymm1, dword [rax]{1to8}                        }
testcase        {  0x62, 0xb2, 0x74, 0x38, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUD ymm1, ymm1, dword [rbp+r14*2+0x8]{1to8}              }
testcase        {  0x62, 0xf2, 0x74, 0x08, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD xmm1, xmm1, oword [rax]                              }
testcase        {  0x62, 0xf2, 0x74, 0x2f, 0xd2, 0xc9                                        }, {  {evex} VPDPWUUD ymm1{k7}, ymm1, ymm1                                 }
testcase        {  0x62, 0xf2, 0x74, 0x2f, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD ymm1{k7}, ymm1, yword [rax]                          }
testcase        {  0x62, 0xb2, 0x74, 0x2f, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUD ymm1{k7}, ymm1, yword [rbp+r14*2+0x8]                }
testcase        {  0x62, 0xf2, 0x74, 0x3f, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD ymm1{k7}, ymm1, dword [rax]{1to8}                    }
testcase        {  0x62, 0xb2, 0x74, 0x3f, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUD ymm1{k7}, ymm1, dword [rbp+r14*2+0x8]{1to8}          }
testcase        {  0x62, 0xf2, 0x74, 0xaf, 0xd2, 0xc9                                        }, {  {evex} VPDPWUUD ymm1{k7}{z}, ymm1, ymm1                              }
testcase        {  0x62, 0xf2, 0x74, 0xaf, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD ymm1{k7}{z}, ymm1, yword [rax]                       }
testcase        {  0x62, 0xb2, 0x74, 0xaf, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUD ymm1{k7}{z}, ymm1, yword [rbp+r14*2+0x8]             }
testcase        {  0x62, 0xf2, 0x74, 0xbf, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD ymm1{k7}{z}, ymm1, dword [rax]{1to8}                 }
testcase        {  0x62, 0xb2, 0x74, 0xbf, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUD ymm1{k7}{z}, ymm1, dword [rbp+r14*2+0x8]{1to8}       }
testcase        {  0x62, 0xb2, 0x74, 0x08, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUD xmm1, xmm1, oword [rbp+r14*2+0x8]                    }
testcase        {  0x62, 0xf2, 0x7c, 0x48, 0xd2, 0xc1                                        }, {  {evex} VPDPWUUD zmm0, zmm0, zmm1                                     }
testcase        {  0x62, 0xf2, 0x7c, 0x48, 0xd2, 0x00                                        }, {  {evex} VPDPWUUD zmm0, zmm0, zword [rax]                              }
testcase        {  0x62, 0xb2, 0x7c, 0x48, 0xd2, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUD zmm0, zmm0, zword [rbp+r14*2+0x8]                    }
testcase        {  0x62, 0xf2, 0x7c, 0x58, 0xd2, 0x00                                        }, {  {evex} VPDPWUUD zmm0, zmm0, dword [rax]{1to16}                       }
testcase        {  0x62, 0xb2, 0x7c, 0x58, 0xd2, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUUD zmm0, zmm0, dword [rbp+r14*2+0x8]{1to16}             }
testcase        {  0x62, 0xf2, 0x7c, 0x4f, 0xd2, 0xc1                                        }, {  {evex} VPDPWUUD zmm0{k7}, zmm0, zmm1                                 }
testcase        {  0x62, 0xf2, 0x7c, 0x4f, 0xd2, 0x00                                        }, {  {evex} VPDPWUUD zmm0{k7}, zmm0, zword [rax]                          }
testcase        {  0x62, 0xb2, 0x7c, 0x4f, 0xd2, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUD zmm0{k7}, zmm0, zword [rbp+r14*2+0x8]                }
testcase        {  0x62, 0xf2, 0x7c, 0x5f, 0xd2, 0x00                                        }, {  {evex} VPDPWUUD zmm0{k7}, zmm0, dword [rax]{1to16}                   }
testcase        {  0x62, 0xb2, 0x7c, 0x5f, 0xd2, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUUD zmm0{k7}, zmm0, dword [rbp+r14*2+0x8]{1to16}         }
testcase        {  0x62, 0xf2, 0x74, 0x18, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD xmm1, xmm1, dword [rax]{1to4}                        }
testcase        {  0x62, 0xf2, 0x7c, 0xcf, 0xd2, 0xc1                                        }, {  {evex} VPDPWUUD zmm0{k7}{z}, zmm0, zmm1                              }
testcase        {  0x62, 0xf2, 0x7c, 0xcf, 0xd2, 0x00                                        }, {  {evex} VPDPWUUD zmm0{k7}{z}, zmm0, zword [rax]                       }
testcase        {  0x62, 0xb2, 0x7c, 0xcf, 0xd2, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUD zmm0{k7}{z}, zmm0, zword [rbp+r14*2+0x8]             }
testcase        {  0x62, 0xf2, 0x7c, 0xdf, 0xd2, 0x00                                        }, {  {evex} VPDPWUUD zmm0{k7}{z}, zmm0, dword [rax]{1to16}                }
testcase        {  0x62, 0xb2, 0x7c, 0xdf, 0xd2, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUUD zmm0{k7}{z}, zmm0, dword [rbp+r14*2+0x8]{1to16}      }
testcase        {  0x62, 0xb2, 0x74, 0x18, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUD xmm1, xmm1, dword [rbp+r14*2+0x8]{1to4}              }
testcase        {  0x62, 0xf2, 0x74, 0x0f, 0xd2, 0xc9                                        }, {  {evex} VPDPWUUD xmm1{k7}, xmm1, xmm1                                 }
testcase        {  0x62, 0xf2, 0x74, 0x0f, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD xmm1{k7}, xmm1, oword [rax]                          }
testcase        {  0x62, 0xb2, 0x74, 0x0f, 0xd2, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUD xmm1{k7}, xmm1, oword [rbp+r14*2+0x8]                }
testcase        {  0x62, 0xf2, 0x74, 0x1f, 0xd2, 0x08                                        }, {  {evex} VPDPWUUD xmm1{k7}, xmm1, dword [rax]{1to4}                    }
testcase        {  0x62, 0xb2, 0x74, 0x1f, 0xd2, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUD xmm1{k7}, xmm1, dword [rbp+r14*2+0x8]{1to4}          }
testcase        {  0x62, 0xf2, 0x74, 0x08, 0xd3, 0xc9                                        }, {  {evex} VPDPWUUDS xmm1, xmm1, xmm1                                    }
testcase        {  0x62, 0xf2, 0x74, 0x8f, 0xd3, 0xc9                                        }, {  {evex} VPDPWUUDS xmm1{k7}{z}, xmm1, xmm1                             }
testcase        {  0x62, 0xf2, 0x74, 0x8f, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS xmm1{k7}{z}, xmm1, oword [rax]                      }
testcase        {  0x62, 0xb2, 0x74, 0x8f, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUDS xmm1{k7}{z}, xmm1, oword [rbp+r14*2+0x8]            }
testcase        {  0x62, 0xf2, 0x74, 0x9f, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS xmm1{k7}{z}, xmm1, dword [rax]{1to4}                }
testcase        {  0x62, 0xb2, 0x74, 0x9f, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUDS xmm1{k7}{z}, xmm1, dword [rbp+r14*2+0x8]{1to4}      }
testcase        {  0x62, 0xf2, 0x74, 0x28, 0xd3, 0xc9                                        }, {  {evex} VPDPWUUDS ymm1, ymm1, ymm1                                    }
testcase        {  0x62, 0xf2, 0x74, 0x28, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS ymm1, ymm1, yword [rax]                             }
testcase        {  0x62, 0xb2, 0x74, 0x28, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUDS ymm1, ymm1, yword [rbp+r14*2+0x8]                   }
testcase        {  0x62, 0xf2, 0x74, 0x38, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS ymm1, ymm1, dword [rax]{1to8}                       }
testcase        {  0x62, 0xb2, 0x74, 0x38, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUDS ymm1, ymm1, dword [rbp+r14*2+0x8]{1to8}             }
testcase        {  0x62, 0xf2, 0x74, 0x08, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS xmm1, xmm1, oword [rax]                             }
testcase        {  0x62, 0xf2, 0x74, 0x2f, 0xd3, 0xc9                                        }, {  {evex} VPDPWUUDS ymm1{k7}, ymm1, ymm1                                }
testcase        {  0x62, 0xf2, 0x74, 0x2f, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS ymm1{k7}, ymm1, yword [rax]                         }
testcase        {  0x62, 0xb2, 0x74, 0x2f, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUDS ymm1{k7}, ymm1, yword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf2, 0x74, 0x3f, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS ymm1{k7}, ymm1, dword [rax]{1to8}                   }
testcase        {  0x62, 0xb2, 0x74, 0x3f, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUDS ymm1{k7}, ymm1, dword [rbp+r14*2+0x8]{1to8}         }
testcase        {  0x62, 0xf2, 0x74, 0xaf, 0xd3, 0xc9                                        }, {  {evex} VPDPWUUDS ymm1{k7}{z}, ymm1, ymm1                             }
testcase        {  0x62, 0xf2, 0x74, 0xaf, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS ymm1{k7}{z}, ymm1, yword [rax]                      }
testcase        {  0x62, 0xb2, 0x74, 0xaf, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUDS ymm1{k7}{z}, ymm1, yword [rbp+r14*2+0x8]            }
testcase        {  0x62, 0xf2, 0x74, 0xbf, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS ymm1{k7}{z}, ymm1, dword [rax]{1to8}                }
testcase        {  0x62, 0xb2, 0x74, 0xbf, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUDS ymm1{k7}{z}, ymm1, dword [rbp+r14*2+0x8]{1to8}      }
testcase        {  0x62, 0xb2, 0x74, 0x08, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUDS xmm1, xmm1, oword [rbp+r14*2+0x8]                   }
testcase        {  0x62, 0xf2, 0x7c, 0x48, 0xd3, 0xc1                                        }, {  {evex} VPDPWUUDS zmm0, zmm0, zmm1                                    }
testcase        {  0x62, 0xf2, 0x7c, 0x48, 0xd3, 0x00                                        }, {  {evex} VPDPWUUDS zmm0, zmm0, zword [rax]                             }
testcase        {  0x62, 0xb2, 0x7c, 0x48, 0xd3, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUDS zmm0, zmm0, zword [rbp+r14*2+0x8]                   }
testcase        {  0x62, 0xf2, 0x7c, 0x58, 0xd3, 0x00                                        }, {  {evex} VPDPWUUDS zmm0, zmm0, dword [rax]{1to16}                      }
testcase        {  0x62, 0xb2, 0x7c, 0x58, 0xd3, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUUDS zmm0, zmm0, dword [rbp+r14*2+0x8]{1to16}            }
testcase        {  0x62, 0xf2, 0x7c, 0x4f, 0xd3, 0xc1                                        }, {  {evex} VPDPWUUDS zmm0{k7}, zmm0, zmm1                                }
testcase        {  0x62, 0xf2, 0x7c, 0x4f, 0xd3, 0x00                                        }, {  {evex} VPDPWUUDS zmm0{k7}, zmm0, zword [rax]                         }
testcase        {  0x62, 0xb2, 0x7c, 0x4f, 0xd3, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUDS zmm0{k7}, zmm0, zword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf2, 0x7c, 0x5f, 0xd3, 0x00                                        }, {  {evex} VPDPWUUDS zmm0{k7}, zmm0, dword [rax]{1to16}                  }
testcase        {  0x62, 0xb2, 0x7c, 0x5f, 0xd3, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUUDS zmm0{k7}, zmm0, dword [rbp+r14*2+0x8]{1to16}        }
testcase        {  0x62, 0xf2, 0x74, 0x18, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS xmm1, xmm1, dword [rax]{1to4}                       }
testcase        {  0x62, 0xf2, 0x7c, 0xcf, 0xd3, 0xc1                                        }, {  {evex} VPDPWUUDS zmm0{k7}{z}, zmm0, zmm1                             }
testcase        {  0x62, 0xf2, 0x7c, 0xcf, 0xd3, 0x00                                        }, {  {evex} VPDPWUUDS zmm0{k7}{z}, zmm0, zword [rax]                      }
testcase        {  0x62, 0xb2, 0x7c, 0xcf, 0xd3, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUDS zmm0{k7}{z}, zmm0, zword [rbp+r14*2+0x8]            }
testcase        {  0x62, 0xf2, 0x7c, 0xdf, 0xd3, 0x00                                        }, {  {evex} VPDPWUUDS zmm0{k7}{z}, zmm0, dword [rax]{1to16}               }
testcase        {  0x62, 0xb2, 0x7c, 0xdf, 0xd3, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUUDS zmm0{k7}{z}, zmm0, dword [rbp+r14*2+0x8]{1to16}     }
testcase        {  0x62, 0xb2, 0x74, 0x18, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUDS xmm1, xmm1, dword [rbp+r14*2+0x8]{1to4}             }
testcase        {  0x62, 0xf2, 0x74, 0x0f, 0xd3, 0xc9                                        }, {  {evex} VPDPWUUDS xmm1{k7}, xmm1, xmm1                                }
testcase        {  0x62, 0xf2, 0x74, 0x0f, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS xmm1{k7}, xmm1, oword [rax]                         }
testcase        {  0x62, 0xb2, 0x74, 0x0f, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUDS xmm1{k7}, xmm1, oword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf2, 0x74, 0x1f, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS xmm1{k7}, xmm1, dword [rax]{1to4}                   }
testcase        {  0x62, 0xb2, 0x74, 0x1f, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUDS xmm1{k7}, xmm1, dword [rbp+r14*2+0x8]{1to4}         }
testcase        {  0x62, 0xf2, 0x74, 0x08, 0xd3, 0xc9                                        }, {  {evex} VPDPWUUDS xmm1, xmm1, xmm1                                    }
testcase        {  0x62, 0xf2, 0x74, 0x8f, 0xd3, 0xc9                                        }, {  {evex} VPDPWUUDS xmm1{k7}{z}, xmm1, xmm1                             }
testcase        {  0x62, 0xf2, 0x74, 0x8f, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS xmm1{k7}{z}, xmm1, oword [rax]                      }
testcase        {  0x62, 0xb2, 0x74, 0x8f, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUDS xmm1{k7}{z}, xmm1, oword [rbp+r14*2+0x8]            }
testcase        {  0x62, 0xf2, 0x74, 0x9f, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS xmm1{k7}{z}, xmm1, dword [rax]{1to4}                }
testcase        {  0x62, 0xb2, 0x74, 0x9f, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUDS xmm1{k7}{z}, xmm1, dword [rbp+r14*2+0x8]{1to4}      }
testcase        {  0x62, 0xf2, 0x74, 0x28, 0xd3, 0xc9                                        }, {  {evex} VPDPWUUDS ymm1, ymm1, ymm1                                    }
testcase        {  0x62, 0xf2, 0x74, 0x28, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS ymm1, ymm1, yword [rax]                             }
testcase        {  0x62, 0xb2, 0x74, 0x28, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUDS ymm1, ymm1, yword [rbp+r14*2+0x8]                   }
testcase        {  0x62, 0xf2, 0x74, 0x38, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS ymm1, ymm1, dword [rax]{1to8}                       }
testcase        {  0x62, 0xb2, 0x74, 0x38, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUDS ymm1, ymm1, dword [rbp+r14*2+0x8]{1to8}             }
testcase        {  0x62, 0xf2, 0x74, 0x08, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS xmm1, xmm1, oword [rax]                             }
testcase        {  0x62, 0xf2, 0x74, 0x2f, 0xd3, 0xc9                                        }, {  {evex} VPDPWUUDS ymm1{k7}, ymm1, ymm1                                }
testcase        {  0x62, 0xf2, 0x74, 0x2f, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS ymm1{k7}, ymm1, yword [rax]                         }
testcase        {  0x62, 0xb2, 0x74, 0x2f, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUDS ymm1{k7}, ymm1, yword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf2, 0x74, 0x3f, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS ymm1{k7}, ymm1, dword [rax]{1to8}                   }
testcase        {  0x62, 0xb2, 0x74, 0x3f, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUDS ymm1{k7}, ymm1, dword [rbp+r14*2+0x8]{1to8}         }
testcase        {  0x62, 0xf2, 0x74, 0xaf, 0xd3, 0xc9                                        }, {  {evex} VPDPWUUDS ymm1{k7}{z}, ymm1, ymm1                             }
testcase        {  0x62, 0xf2, 0x74, 0xaf, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS ymm1{k7}{z}, ymm1, yword [rax]                      }
testcase        {  0x62, 0xb2, 0x74, 0xaf, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUDS ymm1{k7}{z}, ymm1, yword [rbp+r14*2+0x8]            }
testcase        {  0x62, 0xf2, 0x74, 0xbf, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS ymm1{k7}{z}, ymm1, dword [rax]{1to8}                }
testcase        {  0x62, 0xb2, 0x74, 0xbf, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUDS ymm1{k7}{z}, ymm1, dword [rbp+r14*2+0x8]{1to8}      }
testcase        {  0x62, 0xb2, 0x74, 0x08, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUDS xmm1, xmm1, oword [rbp+r14*2+0x8]                   }
testcase        {  0x62, 0xf2, 0x7c, 0x48, 0xd3, 0xc1                                        }, {  {evex} VPDPWUUDS zmm0, zmm0, zmm1                                    }
testcase        {  0x62, 0xf2, 0x7c, 0x48, 0xd3, 0x00                                        }, {  {evex} VPDPWUUDS zmm0, zmm0, zword [rax]                             }
testcase        {  0x62, 0xb2, 0x7c, 0x48, 0xd3, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUDS zmm0, zmm0, zword [rbp+r14*2+0x8]                   }
testcase        {  0x62, 0xf2, 0x7c, 0x58, 0xd3, 0x00                                        }, {  {evex} VPDPWUUDS zmm0, zmm0, dword [rax]{1to16}                      }
testcase        {  0x62, 0xb2, 0x7c, 0x58, 0xd3, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUUDS zmm0, zmm0, dword [rbp+r14*2+0x8]{1to16}            }
testcase        {  0x62, 0xf2, 0x7c, 0x4f, 0xd3, 0xc1                                        }, {  {evex} VPDPWUUDS zmm0{k7}, zmm0, zmm1                                }
testcase        {  0x62, 0xf2, 0x7c, 0x4f, 0xd3, 0x00                                        }, {  {evex} VPDPWUUDS zmm0{k7}, zmm0, zword [rax]                         }
testcase        {  0x62, 0xb2, 0x7c, 0x4f, 0xd3, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUDS zmm0{k7}, zmm0, zword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf2, 0x7c, 0x5f, 0xd3, 0x00                                        }, {  {evex} VPDPWUUDS zmm0{k7}, zmm0, dword [rax]{1to16}                  }
testcase        {  0x62, 0xb2, 0x7c, 0x5f, 0xd3, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUUDS zmm0{k7}, zmm0, dword [rbp+r14*2+0x8]{1to16}        }
testcase        {  0x62, 0xf2, 0x74, 0x18, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS xmm1, xmm1, dword [rax]{1to4}                       }
testcase        {  0x62, 0xf2, 0x7c, 0xcf, 0xd3, 0xc1                                        }, {  {evex} VPDPWUUDS zmm0{k7}{z}, zmm0, zmm1                             }
testcase        {  0x62, 0xf2, 0x7c, 0xcf, 0xd3, 0x00                                        }, {  {evex} VPDPWUUDS zmm0{k7}{z}, zmm0, zword [rax]                      }
testcase        {  0x62, 0xb2, 0x7c, 0xcf, 0xd3, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUDS zmm0{k7}{z}, zmm0, zword [rbp+r14*2+0x8]            }
testcase        {  0x62, 0xf2, 0x7c, 0xdf, 0xd3, 0x00                                        }, {  {evex} VPDPWUUDS zmm0{k7}{z}, zmm0, dword [rax]{1to16}               }
testcase        {  0x62, 0xb2, 0x7c, 0xdf, 0xd3, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUUDS zmm0{k7}{z}, zmm0, dword [rbp+r14*2+0x8]{1to16}     }
testcase        {  0x62, 0xb2, 0x74, 0x18, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUDS xmm1, xmm1, dword [rbp+r14*2+0x8]{1to4}             }
testcase        {  0x62, 0xf2, 0x74, 0x0f, 0xd3, 0xc9                                        }, {  {evex} VPDPWUUDS xmm1{k7}, xmm1, xmm1                                }
testcase        {  0x62, 0xf2, 0x74, 0x0f, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS xmm1{k7}, xmm1, oword [rax]                         }
testcase        {  0x62, 0xb2, 0x74, 0x0f, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUDS xmm1{k7}, xmm1, oword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf2, 0x74, 0x1f, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS xmm1{k7}, xmm1, dword [rax]{1to4}                   }
testcase        {  0x62, 0xb2, 0x74, 0x1f, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUDS xmm1{k7}, xmm1, dword [rbp+r14*2+0x8]{1to4}         }
testcase        {  0x62, 0xf2, 0x74, 0x08, 0xd3, 0xc9                                        }, {  {evex} VPDPWUUDS xmm1, xmm1, xmm1                                    }
testcase        {  0x62, 0xf2, 0x74, 0x8f, 0xd3, 0xc9                                        }, {  {evex} VPDPWUUDS xmm1{k7}{z}, xmm1, xmm1                             }
testcase        {  0x62, 0xf2, 0x74, 0x8f, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS xmm1{k7}{z}, xmm1, oword [rax]                      }
testcase        {  0x62, 0xb2, 0x74, 0x8f, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUDS xmm1{k7}{z}, xmm1, oword [rbp+r14*2+0x8]            }
testcase        {  0x62, 0xf2, 0x74, 0x9f, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS xmm1{k7}{z}, xmm1, dword [rax]{1to4}                }
testcase        {  0x62, 0xb2, 0x74, 0x9f, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUDS xmm1{k7}{z}, xmm1, dword [rbp+r14*2+0x8]{1to4}      }
testcase        {  0x62, 0xf2, 0x74, 0x28, 0xd3, 0xc9                                        }, {  {evex} VPDPWUUDS ymm1, ymm1, ymm1                                    }
testcase        {  0x62, 0xf2, 0x74, 0x28, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS ymm1, ymm1, yword [rax]                             }
testcase        {  0x62, 0xb2, 0x74, 0x28, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUDS ymm1, ymm1, yword [rbp+r14*2+0x8]                   }
testcase        {  0x62, 0xf2, 0x74, 0x38, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS ymm1, ymm1, dword [rax]{1to8}                       }
testcase        {  0x62, 0xb2, 0x74, 0x38, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUDS ymm1, ymm1, dword [rbp+r14*2+0x8]{1to8}             }
testcase        {  0x62, 0xf2, 0x74, 0x08, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS xmm1, xmm1, oword [rax]                             }
testcase        {  0x62, 0xf2, 0x74, 0x2f, 0xd3, 0xc9                                        }, {  {evex} VPDPWUUDS ymm1{k7}, ymm1, ymm1                                }
testcase        {  0x62, 0xf2, 0x74, 0x2f, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS ymm1{k7}, ymm1, yword [rax]                         }
testcase        {  0x62, 0xb2, 0x74, 0x2f, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUDS ymm1{k7}, ymm1, yword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf2, 0x74, 0x3f, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS ymm1{k7}, ymm1, dword [rax]{1to8}                   }
testcase        {  0x62, 0xb2, 0x74, 0x3f, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUDS ymm1{k7}, ymm1, dword [rbp+r14*2+0x8]{1to8}         }
testcase        {  0x62, 0xf2, 0x74, 0xaf, 0xd3, 0xc9                                        }, {  {evex} VPDPWUUDS ymm1{k7}{z}, ymm1, ymm1                             }
testcase        {  0x62, 0xf2, 0x74, 0xaf, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS ymm1{k7}{z}, ymm1, yword [rax]                      }
testcase        {  0x62, 0xb2, 0x74, 0xaf, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUDS ymm1{k7}{z}, ymm1, yword [rbp+r14*2+0x8]            }
testcase        {  0x62, 0xf2, 0x74, 0xbf, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS ymm1{k7}{z}, ymm1, dword [rax]{1to8}                }
testcase        {  0x62, 0xb2, 0x74, 0xbf, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUDS ymm1{k7}{z}, ymm1, dword [rbp+r14*2+0x8]{1to8}      }
testcase        {  0x62, 0xb2, 0x74, 0x08, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUDS xmm1, xmm1, oword [rbp+r14*2+0x8]                   }
testcase        {  0x62, 0xf2, 0x7c, 0x48, 0xd3, 0xc1                                        }, {  {evex} VPDPWUUDS zmm0, zmm0, zmm1                                    }
testcase        {  0x62, 0xf2, 0x7c, 0x48, 0xd3, 0x00                                        }, {  {evex} VPDPWUUDS zmm0, zmm0, zword [rax]                             }
testcase        {  0x62, 0xb2, 0x7c, 0x48, 0xd3, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUDS zmm0, zmm0, zword [rbp+r14*2+0x8]                   }
testcase        {  0x62, 0xf2, 0x7c, 0x58, 0xd3, 0x00                                        }, {  {evex} VPDPWUUDS zmm0, zmm0, dword [rax]{1to16}                      }
testcase        {  0x62, 0xb2, 0x7c, 0x58, 0xd3, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUUDS zmm0, zmm0, dword [rbp+r14*2+0x8]{1to16}            }
testcase        {  0x62, 0xf2, 0x7c, 0x4f, 0xd3, 0xc1                                        }, {  {evex} VPDPWUUDS zmm0{k7}, zmm0, zmm1                                }
testcase        {  0x62, 0xf2, 0x7c, 0x4f, 0xd3, 0x00                                        }, {  {evex} VPDPWUUDS zmm0{k7}, zmm0, zword [rax]                         }
testcase        {  0x62, 0xb2, 0x7c, 0x4f, 0xd3, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUDS zmm0{k7}, zmm0, zword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf2, 0x7c, 0x5f, 0xd3, 0x00                                        }, {  {evex} VPDPWUUDS zmm0{k7}, zmm0, dword [rax]{1to16}                  }
testcase        {  0x62, 0xb2, 0x7c, 0x5f, 0xd3, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUUDS zmm0{k7}, zmm0, dword [rbp+r14*2+0x8]{1to16}        }
testcase        {  0x62, 0xf2, 0x74, 0x18, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS xmm1, xmm1, dword [rax]{1to4}                       }
testcase        {  0x62, 0xf2, 0x7c, 0xcf, 0xd3, 0xc1                                        }, {  {evex} VPDPWUUDS zmm0{k7}{z}, zmm0, zmm1                             }
testcase        {  0x62, 0xf2, 0x7c, 0xcf, 0xd3, 0x00                                        }, {  {evex} VPDPWUUDS zmm0{k7}{z}, zmm0, zword [rax]                      }
testcase        {  0x62, 0xb2, 0x7c, 0xcf, 0xd3, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUDS zmm0{k7}{z}, zmm0, zword [rbp+r14*2+0x8]            }
testcase        {  0x62, 0xf2, 0x7c, 0xdf, 0xd3, 0x00                                        }, {  {evex} VPDPWUUDS zmm0{k7}{z}, zmm0, dword [rax]{1to16}               }
testcase        {  0x62, 0xb2, 0x7c, 0xdf, 0xd3, 0x44, 0x75, 0x02                            }, {  {evex} VPDPWUUDS zmm0{k7}{z}, zmm0, dword [rbp+r14*2+0x8]{1to16}     }
testcase        {  0x62, 0xb2, 0x74, 0x18, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUDS xmm1, xmm1, dword [rbp+r14*2+0x8]{1to4}             }
testcase        {  0x62, 0xf2, 0x74, 0x0f, 0xd3, 0xc9                                        }, {  {evex} VPDPWUUDS xmm1{k7}, xmm1, xmm1                                }
testcase        {  0x62, 0xf2, 0x74, 0x0f, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS xmm1{k7}, xmm1, oword [rax]                         }
testcase        {  0x62, 0xb2, 0x74, 0x0f, 0xd3, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {  {evex} VPDPWUUDS xmm1{k7}, xmm1, oword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf2, 0x74, 0x1f, 0xd3, 0x08                                        }, {  {evex} VPDPWUUDS xmm1{k7}, xmm1, dword [rax]{1to4}                   }
testcase        {  0x62, 0xb2, 0x74, 0x1f, 0xd3, 0x4c, 0x75, 0x02                            }, {  {evex} VPDPWUUDS xmm1{k7}, xmm1, dword [rbp+r14*2+0x8]{1to4}         }
testcase        {  0x62, 0xf6, 0x7c, 0x08, 0x4c, 0xca                                        }, {  VRCPBF16 xmm1, xmm2  }
testcase        {  0x62, 0xf6, 0x7c, 0x28, 0x4c, 0xca                                        }, {  VRCPBF16 ymm1, ymm2  }
testcase        {  0x62, 0xf6, 0x7c, 0x48, 0x4c, 0xca                                        }, {  VRCPBF16 zmm1, zmm2  }
testcase        {  0x62, 0xf3, 0x7f, 0x08, 0x56, 0xca, 0x10                                  }, {  VREDUCEBF16 xmm1, xmm2, 0x10  }
testcase        {  0x62, 0xf3, 0x7f, 0x28, 0x56, 0xca, 0x10                                  }, {  VREDUCEBF16 ymm1, ymm2, 0x10  }
testcase        {  0x62, 0xf3, 0x7f, 0x48, 0x56, 0xca, 0x10                                  }, {  VREDUCEBF16 zmm1, zmm2, 0x10  }
testcase        {  0x62, 0xf3, 0x7f, 0x08, 0x08, 0xca, 0x10                                  }, {  VRNDSCALEBF16 xmm1, xmm2, 0x10  }
testcase        {  0x62, 0xf3, 0x7f, 0x28, 0x08, 0xca, 0x10                                  }, {  VRNDSCALEBF16 ymm1, ymm2, 0x10  }
testcase        {  0x62, 0xf3, 0x7f, 0x48, 0x08, 0xca, 0x10                                  }, {  VRNDSCALEBF16 zmm1, zmm2, 0x10  }
testcase        {  0x62, 0xf6, 0x7c, 0x08, 0x4e, 0xca                                        }, {  VRSQRTBF16 xmm1, xmm2  }
testcase        {  0x62, 0xf6, 0x7c, 0x28, 0x4e, 0xca                                        }, {  VRSQRTBF16 ymm1, ymm2  }
testcase        {  0x62, 0xf6, 0x7c, 0x48, 0x4e, 0xca                                        }, {  VRSQRTBF16 zmm1, zmm2  }
testcase        {  0x62, 0xf6, 0x6c, 0x08, 0x2c, 0xcb                                        }, {  VSCALEFBF16 xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xb6, 0x6c, 0x08, 0x2c, 0x4c, 0xf0, 0x01                            }, {  VSCALEFBF16 xmm1, xmm2, [rax+r14*8+0x10]  }
testcase        {  0x62, 0xf6, 0x6c, 0x28, 0x2c, 0xcb                                        }, {  VSCALEFBF16 ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf6, 0x6c, 0x48, 0x2c, 0xcb                                        }, {  VSCALEFBF16 zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf5, 0x7d, 0x08, 0x51, 0xca                                        }, {  VSQRTBF16 xmm1, xmm2  }
testcase        {  0x62, 0xf5, 0x7d, 0x28, 0x51, 0xca                                        }, {  VSQRTBF16 ymm1, ymm2  }
testcase        {  0x62, 0xf5, 0x7d, 0x48, 0x51, 0xca                                        }, {  VSQRTBF16 zmm1, zmm2  }
testcase        {  0x62, 0xf5, 0x6d, 0x08, 0x5c, 0xcb                                        }, {  VSUBBF16 xmm1, xmm2, xmm3  }
testcase        {  0x62, 0xb5, 0x6d, 0x08, 0x5c, 0x4c, 0xf0, 0x01                            }, {  VSUBBF16 xmm1, xmm2, [rax+r14*8+0x10]  }
testcase        {  0x62, 0xf5, 0x6d, 0x28, 0x5c, 0xcb                                        }, {  VSUBBF16 ymm1, ymm2, ymm3  }
testcase        {  0x62, 0xf5, 0x6d, 0x48, 0x5c, 0xcb                                        }, {  VSUBBF16 zmm1, zmm2, zmm3  }
testcase        {  0x62, 0xf1, 0xff, 0x08, 0x2e, 0xca                                        }, {  VUCOMXSD xmm1, xmm2  }
testcase        {  0x62, 0xf5, 0x7e, 0x08, 0x2e, 0xca                                        }, {  VUCOMXSH xmm1, xmm2  }
testcase        {  0x62, 0xf1, 0x7e, 0x08, 0x2e, 0xca                                        }, {  VUCOMXSS xmm1, xmm2  }
testcase        {  0x62, 0xf5, 0x7f, 0x08, 0x6d, 0xc1                                        }, {  VCVTTSD2SIS eax, xmm1  }
testcase        {  0x62, 0xf5, 0xff, 0x08, 0x6d, 0xc1                                        }, {  VCVTTSD2SIS rax, xmm1  }
testcase        {  0x62, 0xf5, 0x7f, 0x08, 0x6c, 0xc1                                        }, {  VCVTTSD2USIS eax, xmm1  }
testcase        {  0x62, 0xf5, 0xff, 0x08, 0x6c, 0xc1                                        }, {  VCVTTSD2USIS rax, xmm1  }
testcase        {  0x62, 0xf5, 0x7e, 0x08, 0x6d, 0xc1                                        }, {  VCVTTSS2SIS eax, xmm1  }
testcase        {  0x62, 0xf5, 0xfe, 0x08, 0x6d, 0xc1                                        }, {  VCVTTSS2SIS rax, xmm1  }
testcase        {  0x62, 0xf5, 0x7e, 0x08, 0x6c, 0xc1                                        }, {  VCVTTSS2USIS eax, xmm1  }
testcase        {  0x62, 0xf5, 0xfe, 0x08, 0x6c, 0xc1                                        }, {  VCVTTSS2USIS rax, xmm1  }
