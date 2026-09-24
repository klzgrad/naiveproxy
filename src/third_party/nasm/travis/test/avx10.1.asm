;Testname=avx10.1; Arguments=-fbin -oavx10.1.bin -O0 -DSRC; Files=stdout stderr avx10.1.bin

%macro testcase 2
 %ifdef BIN
  db %1
 %endif
 %ifdef SRC
  %2
 %endif
%endmacro


		bits 64
testcase	{ 0x62, 0xf5, 0x6c, 0x08, 0x5f, 0xcb                                     }, { vmaxph xmm1,xmm2,xmm3                                        }
testcase	{ 0x62, 0xf5, 0x6c, 0x28, 0x5f, 0xcb                                     }, { vmaxph ymm1,ymm2,ymm3                                        }
testcase	{ 0x62, 0xf5, 0x6c, 0x48, 0x5f, 0xcb                                     }, { vmaxph zmm1,zmm2,zmm3                                        }
testcase	{ 0x62, 0xb5, 0x6c, 0x08, 0x5f, 0x4c, 0xf0, 0x01                         }, { vmaxph xmm1,xmm2,[rax+r14*8+0x10]                            }
testcase	{ 0x62, 0xb5, 0x6c, 0x28, 0x5f, 0x8c, 0xf0, 0x10, 0x00, 0x00, 0x00       }, { vmaxph ymm1,ymm2,[rax+r14*8+0x10]                            }
testcase	{ 0x62, 0xb5, 0x6c, 0x48, 0x5f, 0x8c, 0xf0, 0x10, 0x00, 0x00, 0x00       }, { vmaxph zmm1,zmm2,[rax+r14*8+0x10]                            }

testcase	{ 0x62, 0xf5, 0x6c, 0x08, 0x5d, 0xcb                                     }, { vminph xmm1,xmm2,xmm3                                        }
testcase	{ 0x62, 0xf5, 0x6c, 0x28, 0x5d, 0xcb                                     }, { vminph ymm1,ymm2,ymm3                                        }
testcase	{ 0x62, 0xf5, 0x6c, 0x48, 0x5d, 0xcb                                     }, { vminph zmm1,zmm2,zmm3                                        }
testcase	{ 0x62, 0xb5, 0x6c, 0x08, 0x5d, 0x4c, 0xf0, 0x01                         }, { vminph xmm1,xmm2,[rax+r14*8+0x10]                            }
testcase	{ 0x62, 0xb5, 0x6c, 0x28, 0x5d, 0x8c, 0xf0, 0x10, 0x00, 0x00, 0x00       }, { vminph ymm1,ymm2,[rax+r14*8+0x10]                            }
testcase	{ 0x62, 0xb5, 0x6c, 0x48, 0x5d, 0x8c, 0xf0, 0x10, 0x00, 0x00, 0x00       }, { vminph zmm1,zmm2,[rax+r14*8+0x10]                            }

testcase	{ 0x62, 0xf3, 0x7c, 0x08, 0x08, 0xca, 0x0a                               }, { vrndscaleph xmm1,xmm2,10                                     }
testcase	{ 0x62, 0xf3, 0x7c, 0x28, 0x08, 0xca, 0x0a                               }, { vrndscaleph ymm1,ymm2,10                                     }
testcase	{ 0x62, 0xf3, 0x7c, 0x48, 0x08, 0xca, 0x0a                               }, { vrndscaleph zmm1,zmm2,10                                     }
testcase	{ 0x62, 0xf3, 0x7c, 0x89, 0x08, 0xca, 0x0a                               }, { vrndscaleph xmm1{k1}{z},xmm2,10                              }
testcase	{ 0x62, 0xf3, 0x7c, 0xa9, 0x08, 0xca, 0x0a                               }, { vrndscaleph ymm1{k1}{z},ymm2,10                              }
testcase	{ 0x62, 0xf3, 0x7c, 0xc9, 0x08, 0xca, 0x0a                               }, { vrndscaleph zmm1{k1}{z},zmm2,10                              }

testcase	{ 0x62, 0xf3, 0x6c, 0x08, 0x0a, 0xcb, 0x0a                               }, { vrndscalesh xmm1,xmm2,xmm3,10                                }
testcase	{ 0x62, 0xf3, 0x6c, 0x89, 0x0a, 0xcb, 0x0a                               }, { vrndscalesh xmm1{k1}{z},xmm2,xmm3,10                         }
testcase	{ 0x62, 0xf3, 0x6c, 0x99, 0x0a, 0xcb, 0x0a                               }, { vrndscalesh xmm1{k1}{z},xmm2,xmm3,{sae},10                   }

testcase	{ 0x62, 0xf5, 0x7d, 0x08, 0x1d, 0xca                                     }, { vcvtps2phx xmm1,xmm2                                         }
testcase	{ 0x62, 0xf5, 0x7d, 0x28, 0x1d, 0xca                                     }, { vcvtps2phx xmm1,ymm2                                         }
testcase	{ 0x62, 0xb5, 0x7d, 0x08, 0x1d, 0x4c, 0xf0, 0x01                         }, { vcvtps2phx xmm1,[rax+r14*8+0x10]                             }
testcase	{ 0x62, 0xf5, 0x7d, 0x48, 0x1d, 0xca                                     }, { vcvtps2phx ymm1,zmm2                                         }
testcase	{ 0x62, 0xb5, 0x7d, 0x48, 0x1d, 0x8c, 0xf0, 0x10, 0x00, 0x00, 0x00       }, { vcvtps2phx ymm1,[rax+r14*8+0x10]                             }
testcase	{ 0x62, 0xb5, 0x7d, 0xc9, 0x1d, 0x8c, 0xf0, 0x10, 0x00, 0x00, 0x00       }, { vcvtps2phx ymm1{k1}{z},[rax+r14*8+0x10]                      }

testcase        {  0x62, 0xf5, 0x74, 0x08, 0x58, 0xc9                                        }, {        {evex} VADDPH xmm1, xmm1, xmm1                                       }
testcase        {  0x62, 0xf5, 0x74, 0x0f, 0x58, 0xc9                                        }, {        {evex} VADDPH xmm1{k7}, xmm1, xmm1                                   }
testcase        {  0x62, 0xf5, 0x74, 0x0f, 0x58, 0x08                                        }, {        {evex} VADDPH xmm1{k7}, xmm1, oword [rax]                            }
testcase        {  0x62, 0xb5, 0x74, 0x0f, 0x58, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {        {evex} VADDPH xmm1{k7}, xmm1, oword [rbp+r14*2+0x8]                  }
testcase        {  0x62, 0xf5, 0x74, 0x1f, 0x58, 0x08                                        }, {        {evex} VADDPH xmm1{k7}, xmm1, word [rax]{1to8}                       }
testcase        {  0x62, 0xb5, 0x74, 0x1f, 0x58, 0x4c, 0x75, 0x02                            }, {        {evex} VADDPH xmm1{k7}, xmm1, word [rbp+r14*2+0x8]{1to8}             }
testcase        {  0x62, 0xf5, 0x74, 0x0f, 0x58, 0xc9                                        }, {        {evex} VADDPH xmm1{k7}, xmm1                                         }
testcase        {  0x62, 0xf5, 0x74, 0x0f, 0x58, 0x08                                        }, {        {evex} VADDPH xmm1{k7}, oword [rax]                                  }
testcase        {  0x62, 0xb5, 0x74, 0x0f, 0x58, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {        {evex} VADDPH xmm1{k7}, oword [rbp+r14*2+0x8]                        }
testcase        {  0x62, 0xf5, 0x74, 0x1f, 0x58, 0x08                                        }, {        {evex} VADDPH xmm1{k7}, word [rax]{1to8}                             }
testcase        {  0x62, 0xb5, 0x74, 0x1f, 0x58, 0x4c, 0x75, 0x02                            }, {        {evex} VADDPH xmm1{k7}, word [rbp+r14*2+0x8]{1to8}                   }
testcase        {  0x62, 0xf5, 0x74, 0x08, 0x58, 0x08                                        }, {        {evex} VADDPH xmm1, xmm1, oword [rax]                                }
testcase        {  0x62, 0xf5, 0x74, 0x8f, 0x58, 0xc9                                        }, {        {evex} VADDPH xmm1{k7}{z}, xmm1, xmm1                                }
testcase        {  0x62, 0xf5, 0x74, 0x8f, 0x58, 0x08                                        }, {        {evex} VADDPH xmm1{k7}{z}, xmm1, oword [rax]                         }
testcase        {  0x62, 0xb5, 0x74, 0x8f, 0x58, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {        {evex} VADDPH xmm1{k7}{z}, xmm1, oword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf5, 0x74, 0x9f, 0x58, 0x08                                        }, {        {evex} VADDPH xmm1{k7}{z}, xmm1, word [rax]{1to8}                    }
testcase        {  0x62, 0xb5, 0x74, 0x9f, 0x58, 0x4c, 0x75, 0x02                            }, {        {evex} VADDPH xmm1{k7}{z}, xmm1, word [rbp+r14*2+0x8]{1to8}          }
testcase        {  0x62, 0xf5, 0x74, 0x8f, 0x58, 0xc9                                        }, {        {evex} VADDPH xmm1{k7}{z}, xmm1                                      }
testcase        {  0x62, 0xf5, 0x74, 0x8f, 0x58, 0x08                                        }, {        {evex} VADDPH xmm1{k7}{z}, oword [rax]                               }
testcase        {  0x62, 0xb5, 0x74, 0x8f, 0x58, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {        {evex} VADDPH xmm1{k7}{z}, oword [rbp+r14*2+0x8]                     }
testcase        {  0x62, 0xf5, 0x74, 0x9f, 0x58, 0x08                                        }, {        {evex} VADDPH xmm1{k7}{z}, word [rax]{1to8}                          }
testcase        {  0x62, 0xb5, 0x74, 0x9f, 0x58, 0x4c, 0x75, 0x02                            }, {        {evex} VADDPH xmm1{k7}{z}, word [rbp+r14*2+0x8]{1to8}                }
testcase        {  0x62, 0xb5, 0x74, 0x08, 0x58, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {        {evex} VADDPH xmm1, xmm1, oword [rbp+r14*2+0x8]                      }
testcase        {  0x62, 0xf5, 0x74, 0x28, 0x58, 0xc9                                        }, {        {evex} VADDPH ymm1, ymm1, ymm1                                       }
testcase        {  0x62, 0xf5, 0x74, 0x28, 0x58, 0x08                                        }, {        {evex} VADDPH ymm1, ymm1, yword [rax]                                }
testcase        {  0x62, 0xb5, 0x74, 0x28, 0x58, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {        {evex} VADDPH ymm1, ymm1, yword [rbp+r14*2+0x8]                      }
testcase        {  0x62, 0xf5, 0x74, 0x38, 0x58, 0x08                                        }, {        {evex} VADDPH ymm1, ymm1, word [rax]{1to16}                          }
testcase        {  0x62, 0xb5, 0x74, 0x38, 0x58, 0x4c, 0x75, 0x02                            }, {        {evex} VADDPH ymm1, ymm1, word [rbp+r14*2+0x8]{1to16}                }
testcase        {  0x62, 0xf5, 0x74, 0x28, 0x58, 0xc9                                        }, {        {evex} VADDPH ymm1, ymm1                                             }
testcase        {  0x62, 0xf5, 0x74, 0x28, 0x58, 0x08                                        }, {        {evex} VADDPH ymm1, yword [rax]                                      }
testcase        {  0x62, 0xb5, 0x74, 0x28, 0x58, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {        {evex} VADDPH ymm1, yword [rbp+r14*2+0x8]                            }
testcase        {  0x62, 0xf5, 0x74, 0x38, 0x58, 0x08                                        }, {        {evex} VADDPH ymm1, word [rax]{1to16}                                }
testcase        {  0x62, 0xb5, 0x74, 0x38, 0x58, 0x4c, 0x75, 0x02                            }, {        {evex} VADDPH ymm1, word [rbp+r14*2+0x8]{1to16}                      }
testcase        {  0x62, 0xf5, 0x74, 0x18, 0x58, 0x08                                        }, {        {evex} VADDPH xmm1, xmm1, word [rax]{1to8}                           }
testcase        {  0x62, 0xf5, 0x74, 0x2f, 0x58, 0xc9                                        }, {        {evex} VADDPH ymm1{k7}, ymm1, ymm1                                   }
testcase        {  0x62, 0xf5, 0x74, 0x2f, 0x58, 0x08                                        }, {        {evex} VADDPH ymm1{k7}, ymm1, yword [rax]                            }
testcase        {  0x62, 0xb5, 0x74, 0x2f, 0x58, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {        {evex} VADDPH ymm1{k7}, ymm1, yword [rbp+r14*2+0x8]                  }
testcase        {  0x62, 0xf5, 0x74, 0x3f, 0x58, 0x08                                        }, {        {evex} VADDPH ymm1{k7}, ymm1, word [rax]{1to16}                      }
testcase        {  0x62, 0xb5, 0x74, 0x3f, 0x58, 0x4c, 0x75, 0x02                            }, {        {evex} VADDPH ymm1{k7}, ymm1, word [rbp+r14*2+0x8]{1to16}            }
testcase        {  0x62, 0xf5, 0x74, 0x2f, 0x58, 0xc9                                        }, {        {evex} VADDPH ymm1{k7}, ymm1                                         }
testcase        {  0x62, 0xf5, 0x74, 0x2f, 0x58, 0x08                                        }, {        {evex} VADDPH ymm1{k7}, yword [rax]                                  }
testcase        {  0x62, 0xb5, 0x74, 0x2f, 0x58, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {        {evex} VADDPH ymm1{k7}, yword [rbp+r14*2+0x8]                        }
testcase        {  0x62, 0xf5, 0x74, 0x3f, 0x58, 0x08                                        }, {        {evex} VADDPH ymm1{k7}, word [rax]{1to16}                            }
testcase        {  0x62, 0xb5, 0x74, 0x3f, 0x58, 0x4c, 0x75, 0x02                            }, {        {evex} VADDPH ymm1{k7}, word [rbp+r14*2+0x8]{1to16}                  }
testcase        {  0x62, 0xb5, 0x74, 0x18, 0x58, 0x4c, 0x75, 0x02                            }, {        {evex} VADDPH xmm1, xmm1, word [rbp+r14*2+0x8]{1to8}                 }
testcase        {  0x62, 0xf5, 0x74, 0xaf, 0x58, 0xc9                                        }, {        {evex} VADDPH ymm1{k7}{z}, ymm1, ymm1                                }
testcase        {  0x62, 0xf5, 0x74, 0xaf, 0x58, 0x08                                        }, {        {evex} VADDPH ymm1{k7}{z}, ymm1, yword [rax]                         }
testcase        {  0x62, 0xb5, 0x74, 0xaf, 0x58, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {        {evex} VADDPH ymm1{k7}{z}, ymm1, yword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf5, 0x74, 0xbf, 0x58, 0x08                                        }, {        {evex} VADDPH ymm1{k7}{z}, ymm1, word [rax]{1to16}                   }
testcase        {  0x62, 0xb5, 0x74, 0xbf, 0x58, 0x4c, 0x75, 0x02                            }, {        {evex} VADDPH ymm1{k7}{z}, ymm1, word [rbp+r14*2+0x8]{1to16}         }
testcase        {  0x62, 0xf5, 0x74, 0xaf, 0x58, 0xc9                                        }, {        {evex} VADDPH ymm1{k7}{z}, ymm1                                      }
testcase        {  0x62, 0xf5, 0x74, 0xaf, 0x58, 0x08                                        }, {        {evex} VADDPH ymm1{k7}{z}, yword [rax]                               }
testcase        {  0x62, 0xb5, 0x74, 0xaf, 0x58, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {        {evex} VADDPH ymm1{k7}{z}, yword [rbp+r14*2+0x8]                     }
testcase        {  0x62, 0xf5, 0x74, 0xbf, 0x58, 0x08                                        }, {        {evex} VADDPH ymm1{k7}{z}, word [rax]{1to16}                         }
testcase        {  0x62, 0xb5, 0x74, 0xbf, 0x58, 0x4c, 0x75, 0x02                            }, {        {evex} VADDPH ymm1{k7}{z}, word [rbp+r14*2+0x8]{1to16}               }
testcase        {  0x62, 0xf5, 0x74, 0x08, 0x58, 0xc9                                        }, {        {evex} VADDPH xmm1, xmm1                                             }
testcase        {  0x62, 0xf5, 0x7c, 0x48, 0x58, 0xc1                                        }, {        {evex} VADDPH zmm0, zmm0, zmm1                                       }
testcase        {  0x62, 0xf5, 0x7c, 0x48, 0x58, 0x00                                        }, {        {evex} VADDPH zmm0, zmm0, zword [rax]                                }
testcase        {  0x62, 0xb5, 0x7c, 0x48, 0x58, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {        {evex} VADDPH zmm0, zmm0, zword [rbp+r14*2+0x8]                      }
testcase        {  0x62, 0xf5, 0x7c, 0x38, 0x58, 0xc1                                        }, {        {evex} VADDPH zmm0, zmm0, zmm1,{rd-sae}                              }
testcase        {  0x62, 0xf5, 0x7c, 0x48, 0x58, 0xc1                                        }, {        {evex} VADDPH zmm0, zmm1                                             }
testcase        {  0x62, 0xf5, 0x7c, 0x48, 0x58, 0x00                                        }, {        {evex} VADDPH zmm0, zword [rax]                                      }
testcase        {  0x62, 0xb5, 0x7c, 0x48, 0x58, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {        {evex} VADDPH zmm0, zword [rbp+r14*2+0x8]                            }
testcase        {  0x62, 0xf5, 0x7c, 0x38, 0x58, 0xc1                                        }, {        {evex} VADDPH zmm0, zmm1,{rd-sae}                                    }
testcase        {  0x62, 0xf5, 0x7c, 0x4f, 0x58, 0xc1                                        }, {        {evex} VADDPH zmm0{k7}, zmm0, zmm1                                   }
testcase        {  0x62, 0xf5, 0x7c, 0x4f, 0x58, 0x00                                        }, {        {evex} VADDPH zmm0{k7}, zmm0, zword [rax]                            }
testcase        {  0x62, 0xf5, 0x74, 0x08, 0x58, 0x08                                        }, {        {evex} VADDPH xmm1, oword [rax]                                      }
testcase        {  0x62, 0xb5, 0x7c, 0x4f, 0x58, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {        {evex} VADDPH zmm0{k7}, zmm0, zword [rbp+r14*2+0x8]                  }
testcase        {  0x62, 0xf5, 0x7c, 0x3f, 0x58, 0xc1                                        }, {        {evex} VADDPH zmm0{k7}, zmm0, zmm1,{rd-sae}                          }
testcase        {  0x62, 0xf5, 0x7c, 0x4f, 0x58, 0xc1                                        }, {        {evex} VADDPH zmm0{k7}, zmm1                                         }
testcase        {  0x62, 0xf5, 0x7c, 0x4f, 0x58, 0x00                                        }, {        {evex} VADDPH zmm0{k7}, zword [rax]                                  }
testcase        {  0x62, 0xb5, 0x7c, 0x4f, 0x58, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {        {evex} VADDPH zmm0{k7}, zword [rbp+r14*2+0x8]                        }
testcase        {  0x62, 0xf5, 0x7c, 0x3f, 0x58, 0xc1                                        }, {        {evex} VADDPH zmm0{k7}, zmm1,{rd-sae}                                }
testcase        {  0x62, 0xf5, 0x7c, 0xcf, 0x58, 0xc1                                        }, {        {evex} VADDPH zmm0{k7}{z}, zmm0, zmm1                                }
testcase        {  0x62, 0xf5, 0x7c, 0xcf, 0x58, 0x00                                        }, {        {evex} VADDPH zmm0{k7}{z}, zmm0, zword [rax]                         }
testcase        {  0x62, 0xb5, 0x7c, 0xcf, 0x58, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {        {evex} VADDPH zmm0{k7}{z}, zmm0, zword [rbp+r14*2+0x8]               }
testcase        {  0x62, 0xf5, 0x7c, 0xbf, 0x58, 0xc1                                        }, {        {evex} VADDPH zmm0{k7}{z}, zmm0, zmm1,{rd-sae}                       }
testcase        {  0x62, 0xb5, 0x74, 0x08, 0x58, 0x8c, 0x75, 0x08, 0x00, 0x00, 0x00          }, {        {evex} VADDPH xmm1, oword [rbp+r14*2+0x8]                            }
testcase        {  0x62, 0xf5, 0x7c, 0xcf, 0x58, 0xc1                                        }, {        {evex} VADDPH zmm0{k7}{z}, zmm1                                      }
testcase        {  0x62, 0xf5, 0x7c, 0xcf, 0x58, 0x00                                        }, {        {evex} VADDPH zmm0{k7}{z}, zword [rax]                               }
testcase        {  0x62, 0xb5, 0x7c, 0xcf, 0x58, 0x84, 0x75, 0x08, 0x00, 0x00, 0x00          }, {        {evex} VADDPH zmm0{k7}{z}, zword [rbp+r14*2+0x8]                     }
testcase        {  0x62, 0xf5, 0x7c, 0xbf, 0x58, 0xc1                                        }, {        {evex} VADDPH zmm0{k7}{z}, zmm1,{rd-sae}                             }
testcase        {  0x62, 0xf5, 0x74, 0x18, 0x58, 0x08                                        }, {        {evex} VADDPH xmm1, word [rax]{1to8}                                 }
testcase        {  0x62, 0xb5, 0x74, 0x18, 0x58, 0x4c, 0x75, 0x02                            }, {        {evex} VADDPH xmm1, word [rbp+r14*2+0x8]{1to8}                       }

testcase        {  0x62, 0xf5, 0x74, 0x08, 0x58, 0xc9                                        }, {        {evex} VADDPH xmm1, xmm1, xmm1                                       }
testcase        {  0x62, 0xf5, 0x74, 0x0f, 0x58, 0xc9                                        }, {        {evex} VADDPH xmm1{k7}, xmm1, xmm1                                   }
testcase        {  0x62, 0xf5, 0x74, 0x0f, 0x58, 0x08                                        }, {        {evex} VADDPH xmm1{k7}, xmm1, oword [rax]                            }
testcase        {  0x62, 0xf5, 0x74, 0x1f, 0x58, 0x08                                        }, {        {evex} VADDPH xmm1{k7}, xmm1, word [rax]{1to8}                       }
testcase        {  0x62, 0xf5, 0x74, 0x08, 0x58, 0x08                                        }, {        {evex} VADDPH xmm1, xmm1, oword [rax]                                }
testcase        {  0x62, 0xf5, 0x74, 0x8f, 0x58, 0xc9                                        }, {        {evex} VADDPH xmm1{k7}{z}, xmm1, xmm1                                }
testcase        {  0x62, 0xf5, 0x74, 0x8f, 0x58, 0x08                                        }, {        {evex} VADDPH xmm1{k7}{z}, xmm1, oword [rax]                         }
testcase        {  0x62, 0xf5, 0x74, 0x9f, 0x58, 0x08                                        }, {        {evex} VADDPH xmm1{k7}{z}, xmm1, word [rax]{1to8}                    }
testcase        {  0x62, 0xf5, 0x74, 0x28, 0x58, 0x08                                        }, {        {evex} VADDPH ymm1, ymm1, yword [rax]                                }
testcase        {  0x62, 0xf5, 0x74, 0x38, 0x58, 0x08                                        }, {        {evex} VADDPH ymm1, ymm1, word [rax]{1to16}                          }
testcase        {  0x62, 0xf5, 0x74, 0x18, 0x58, 0x08                                        }, {        {evex} VADDPH xmm1, xmm1, word [rax]{1to8}                           }
testcase        {  0x62, 0xf5, 0x74, 0x2f, 0x58, 0x08                                        }, {        {evex} VADDPH ymm1{k7}, ymm1, yword [rax]                            }
testcase        {  0x62, 0xf5, 0x74, 0x3f, 0x58, 0x08                                        }, {        {evex} VADDPH ymm1{k7}, ymm1, word [rax]{1to16}                      }
testcase        {  0x62, 0xf5, 0x74, 0xaf, 0x58, 0x08                                        }, {        {evex} VADDPH ymm1{k7}{z}, ymm1, yword [rax]                         }
testcase        {  0x62, 0xf5, 0x74, 0xbf, 0x58, 0x08                                        }, {        {evex} VADDPH ymm1{k7}{z}, ymm1, word [rax]{1to16}                   }
testcase        {  0x62, 0xf5, 0x7c, 0x48, 0x58, 0xc1                                        }, {        {evex} VADDPH zmm0, zmm0, zmm1                                       }
testcase        {  0x62, 0xf5, 0x7c, 0x48, 0x58, 0x00                                        }, {        {evex} VADDPH zmm0, zmm0, zword [rax]                                }
testcase        {  0x62, 0xf5, 0x7c, 0x4f, 0x58, 0xc1                                        }, {        {evex} VADDPH zmm0{k7}, zmm0, zmm1                                   }
testcase        {  0x62, 0xf5, 0x7c, 0x4f, 0x58, 0x00                                        }, {        {evex} VADDPH zmm0{k7}, zmm0, zword [rax]                            }
testcase        {  0x62, 0xf5, 0x7c, 0xcf, 0x58, 0xc1                                        }, {        {evex} VADDPH zmm0{k7}{z}, zmm0, zmm1                                }
testcase        {  0x62, 0xf5, 0x7c, 0xcf, 0x58, 0x00                                        }, {        {evex} VADDPH zmm0{k7}{z}, zmm0, zword [rax]                         }

testcase    { 0xc4, 0xe3, 0x7d, 0x19, 0xcd, 0x55                                           }, { {vex} VEXTRACTF128  xmm5, ymm1, 0x55                                         }
testcase    { 0xc4, 0xa3, 0x7d, 0x19, 0x0c, 0xf0, 0x55                                     }, { {vex} VEXTRACTF128  oword [rax+r14*8], ymm1, 0x55                            }
testcase    { 0x62, 0xf3, 0x7d, 0x28, 0x19, 0xc8, 0x55                                     }, { {evex} VEXTRACTF32X4  xmm0, ymm1, 0x55                                       }
testcase    { 0x62, 0xf3, 0x7d, 0x2f, 0x19, 0xc8, 0x55                                     }, { {evex} VEXTRACTF32X4  xmm0{k7}, ymm1, 0x55                                   }
testcase    { 0x62, 0xf3, 0x7d, 0xaf, 0x19, 0xc8, 0x55                                     }, { {evex} VEXTRACTF32X4  xmm0{k7}{z}, ymm1, 0x55                                }
testcase    { 0x62, 0xf3, 0x7d, 0x28, 0x19, 0x09, 0x55                                     }, { {evex} VEXTRACTF32X4  [rcx], ymm1, 0x55                                      }
testcase    { 0x62, 0xf3, 0x7d, 0x2f, 0x19, 0x09, 0x55                                     }, { {evex} VEXTRACTF32X4  [rcx]{k7}, ymm1, 0x55                                  }
testcase    { 0x62, 0xf3, 0x7d, 0x48, 0x19, 0xd8, 0x55                                     }, { {evex} VEXTRACTF32X4  xmm0, zmm3, 0x55                                       }
testcase    { 0x62, 0xf3, 0x7d, 0x4f, 0x19, 0xd8, 0x55                                     }, { {evex} VEXTRACTF32X4  xmm0{k7}, zmm3, 0x55                                   }
testcase    { 0x62, 0xf3, 0x7d, 0xcf, 0x19, 0xd8, 0x55                                     }, { {evex} VEXTRACTF32X4  xmm0{k7}{z}, zmm3, 0x55                                }
testcase    { 0x62, 0xf3, 0x7d, 0x48, 0x19, 0x19, 0x55                                     }, { {evex} VEXTRACTF32X4  [rcx], zmm3, 0x55                                      }
testcase    { 0x62, 0xf3, 0x7d, 0x4f, 0x19, 0x19, 0x55                                     }, { {evex} VEXTRACTF32X4  [rcx]{k7}, zmm3, 0x55                                  }
testcase    { 0x62, 0xf3, 0x7d, 0x28, 0x19, 0xc8, 0x55                                     }, { {evex} VEXTRACTF32X4  xmm0, ymm1, 0x55                                       }
testcase    { 0x62, 0xf3, 0x7d, 0x2f, 0x19, 0xc8, 0x55                                     }, { {evex} VEXTRACTF32X4  xmm0{k7}, ymm1, 0x55                                   }
testcase    { 0x62, 0xf3, 0x7d, 0xaf, 0x19, 0xc8, 0x55                                     }, { {evex} VEXTRACTF32X4  xmm0{k7}{z}, ymm1, 0x55                                }
testcase    { 0x62, 0xf3, 0x7d, 0x48, 0x19, 0xd8, 0x55                                     }, { {evex} VEXTRACTF32X4  xmm0, zmm3, 0x55                                       }
testcase    { 0x62, 0xf3, 0x7d, 0x4f, 0x19, 0xd8, 0x55                                     }, { {evex} VEXTRACTF32X4  xmm0{k7}, zmm3, 0x55                                   }
testcase    { 0x62, 0xf3, 0x7d, 0xcf, 0x19, 0xd8, 0x55                                     }, { {evex} VEXTRACTF32X4  xmm0{k7}{z}, zmm3, 0x55                                }
testcase    { 0x62, 0xb3, 0x7d, 0x29, 0x19, 0x0c, 0xf0, 0x55                               }, { {evex} VEXTRACTF32X4  oword [rax+r14*8]{k1}, ymm1, 0x55                      }
testcase    { 0x62, 0xb3, 0x7d, 0x49, 0x19, 0x1c, 0xf0, 0x55                               }, { {evex} VEXTRACTF32X4  oword [rax+r14*8]{k1}, zmm3, 0x55                      }
testcase    { 0x62, 0xf3, 0x7d, 0x48, 0x1b, 0xd8, 0x55                                     }, { {evex} VEXTRACTF32X8  ymm0, zmm3, 0x55                                       }
testcase    { 0x62, 0xf3, 0x7d, 0x4f, 0x1b, 0xd8, 0x55                                     }, { {evex} VEXTRACTF32X8  ymm0{k7}, zmm3, 0x55                                   }
testcase    { 0x62, 0xf3, 0x7d, 0xcf, 0x1b, 0xd8, 0x55                                     }, { {evex} VEXTRACTF32X8  ymm0{k7}{z}, zmm3, 0x55                                }
testcase    { 0x62, 0xf3, 0x7d, 0x48, 0x1b, 0x19, 0x55                                     }, { {evex} VEXTRACTF32X8  [rcx], zmm3, 0x55                                      }
testcase    { 0x62, 0xf3, 0x7d, 0x4f, 0x1b, 0x19, 0x55                                     }, { {evex} VEXTRACTF32X8  [rcx]{k7}, zmm3, 0x55                                  }
testcase    { 0x62, 0xf3, 0x7d, 0x48, 0x1b, 0xd8, 0x55                                     }, { {evex} VEXTRACTF32X8  ymm0, zmm3, 0x55                                       }
testcase    { 0x62, 0xf3, 0x7d, 0x4f, 0x1b, 0xd8, 0x55                                     }, { {evex} VEXTRACTF32X8  ymm0{k7}, zmm3, 0x55                                   }
testcase    { 0x62, 0xf3, 0x7d, 0xcf, 0x1b, 0xd8, 0x55                                     }, { {evex} VEXTRACTF32X8  ymm0{k7}{z}, zmm3, 0x55                                }
testcase    { 0x62, 0xb3, 0x7d, 0x49, 0x1b, 0x1c, 0xf0, 0x55                               }, { {evex} VEXTRACTF32X8  yword [rax+r14*8]{k1}, zmm3, 0x55                      }
testcase    { 0x62, 0xf3, 0xfd, 0x28, 0x19, 0xc8, 0x55                                     }, { {evex} VEXTRACTF64X2  xmm0, ymm1, 0x55                                       }
testcase    { 0x62, 0xf3, 0xfd, 0x2f, 0x19, 0xc8, 0x55                                     }, { {evex} VEXTRACTF64X2  xmm0{k7}, ymm1, 0x55                                   }
testcase    { 0x62, 0xf3, 0xfd, 0xaf, 0x19, 0xc8, 0x55                                     }, { {evex} VEXTRACTF64X2  xmm0{k7}{z}, ymm1, 0x55                                }
testcase    { 0x62, 0xf3, 0xfd, 0x28, 0x19, 0x09, 0x55                                     }, { {evex} VEXTRACTF64X2  [rcx], ymm1, 0x55                                      }
testcase    { 0x62, 0xf3, 0xfd, 0x2f, 0x19, 0x09, 0x55                                     }, { {evex} VEXTRACTF64X2  [rcx]{k7}, ymm1, 0x55                                  }
testcase    { 0x62, 0xf3, 0xfd, 0x48, 0x19, 0xd8, 0x55                                     }, { {evex} VEXTRACTF64X2  xmm0, zmm3, 0x55                                       }
testcase    { 0x62, 0xf3, 0xfd, 0x4f, 0x19, 0xd8, 0x55                                     }, { {evex} VEXTRACTF64X2  xmm0{k7}, zmm3, 0x55                                   }
testcase    { 0x62, 0xf3, 0xfd, 0xcf, 0x19, 0xd8, 0x55                                     }, { {evex} VEXTRACTF64X2  xmm0{k7}{z}, zmm3, 0x55                                }
testcase    { 0x62, 0xf3, 0xfd, 0x48, 0x19, 0x19, 0x55                                     }, { {evex} VEXTRACTF64X2  [rcx], zmm3, 0x55                                      }
testcase    { 0x62, 0xf3, 0xfd, 0x4f, 0x19, 0x19, 0x55                                     }, { {evex} VEXTRACTF64X2  [rcx]{k7}, zmm3, 0x55                                  }
testcase    { 0x62, 0xf3, 0xfd, 0x28, 0x19, 0xc8, 0x55                                     }, { {evex} VEXTRACTF64X2  xmm0, ymm1, 0x55                                       }
testcase    { 0x62, 0xf3, 0xfd, 0x2f, 0x19, 0xc8, 0x55                                     }, { {evex} VEXTRACTF64X2  xmm0{k7}, ymm1, 0x55                                   }
testcase    { 0x62, 0xf3, 0xfd, 0xaf, 0x19, 0xc8, 0x55                                     }, { {evex} VEXTRACTF64X2  xmm0{k7}{z}, ymm1, 0x55                                }
testcase    { 0x62, 0xf3, 0xfd, 0x48, 0x19, 0xd8, 0x55                                     }, { {evex} VEXTRACTF64X2  xmm0, zmm3, 0x55                                       }
testcase    { 0x62, 0xf3, 0xfd, 0x4f, 0x19, 0xd8, 0x55                                     }, { {evex} VEXTRACTF64X2  xmm0{k7}, zmm3, 0x55                                   }
testcase    { 0x62, 0xf3, 0xfd, 0xcf, 0x19, 0xd8, 0x55                                     }, { {evex} VEXTRACTF64X2  xmm0{k7}{z}, zmm3, 0x55                                }
testcase    { 0x62, 0xb3, 0xfd, 0x29, 0x19, 0x0c, 0xf0, 0x55                               }, { {evex} VEXTRACTF64X2  oword [rax+r14*8]{k1}, ymm1, 0x55                      }
testcase    { 0x62, 0xb3, 0xfd, 0x49, 0x19, 0x1c, 0xf0, 0x55                               }, { {evex} VEXTRACTF64X2  oword [rax+r14*8]{k1}, zmm3, 0x55                      }
testcase    { 0x62, 0xf3, 0xfd, 0x48, 0x1b, 0xd8, 0x55                                     }, { {evex} VEXTRACTF64X4  ymm0, zmm3, 0x55                                       }
testcase    { 0x62, 0xf3, 0xfd, 0x4f, 0x1b, 0xd8, 0x55                                     }, { {evex} VEXTRACTF64X4  ymm0{k7}, zmm3, 0x55                                   }
testcase    { 0x62, 0xf3, 0xfd, 0xcf, 0x1b, 0xd8, 0x55                                     }, { {evex} VEXTRACTF64X4  ymm0{k7}{z}, zmm3, 0x55                                }
testcase    { 0x62, 0xf3, 0xfd, 0x48, 0x1b, 0x19, 0x55                                     }, { {evex} VEXTRACTF64X4  [rcx], zmm3, 0x55                                      }
testcase    { 0x62, 0xf3, 0xfd, 0x4f, 0x1b, 0x19, 0x55                                     }, { {evex} VEXTRACTF64X4  [rcx]{k7}, zmm3, 0x55                                  }
testcase    { 0x62, 0xf3, 0xfd, 0x48, 0x1b, 0xd8, 0x55                                     }, { {evex} VEXTRACTF64X4  ymm0, zmm3, 0x55                                       }
testcase    { 0x62, 0xf3, 0xfd, 0x4f, 0x1b, 0xd8, 0x55                                     }, { {evex} VEXTRACTF64X4  ymm0{k7}, zmm3, 0x55                                   }
testcase    { 0x62, 0xf3, 0xfd, 0xcf, 0x1b, 0xd8, 0x55                                     }, { {evex} VEXTRACTF64X4  ymm0{k7}{z}, zmm3, 0x55                                }
testcase    { 0x62, 0xb3, 0xfd, 0x49, 0x1b, 0x1c, 0xf0, 0x55                               }, { {evex} VEXTRACTF64X4  yword [rax+r14*8]{k1}, zmm3, 0x55                      }
testcase    { 0xc4, 0xe3, 0x7d, 0x39, 0xcd, 0x55                                           }, { {vex} VEXTRACTI128  xmm5, ymm1, 0x55                                         }
testcase    { 0xc4, 0xa3, 0x7d, 0x39, 0x0c, 0xf0, 0x55                                     }, { {vex} VEXTRACTI128  oword [rax+r14*8], ymm1, 0x55                            }
testcase    { 0x62, 0xf3, 0x7d, 0x28, 0x39, 0xc8, 0x55                                     }, { {evex} VEXTRACTI32X4  xmm0, ymm1, 0x55                                       }
testcase    { 0x62, 0xf3, 0x7d, 0x2f, 0x39, 0xc8, 0x55                                     }, { {evex} VEXTRACTI32X4  xmm0{k7}, ymm1, 0x55                                   }
testcase    { 0x62, 0xf3, 0x7d, 0xaf, 0x39, 0xc8, 0x55                                     }, { {evex} VEXTRACTI32X4  xmm0{k7}{z}, ymm1, 0x55                                }
testcase    { 0x62, 0xf3, 0x7d, 0x28, 0x39, 0x09, 0x55                                     }, { {evex} VEXTRACTI32X4  [rcx], ymm1, 0x55                                      }
testcase    { 0x62, 0xf3, 0x7d, 0x2f, 0x39, 0x09, 0x55                                     }, { {evex} VEXTRACTI32X4  [rcx]{k7}, ymm1, 0x55                                  }
testcase    { 0x62, 0xf3, 0x7d, 0x48, 0x39, 0xd8, 0x55                                     }, { {evex} VEXTRACTI32X4  xmm0, zmm3, 0x55                                       }
testcase    { 0x62, 0xf3, 0x7d, 0x4f, 0x39, 0xd8, 0x55                                     }, { {evex} VEXTRACTI32X4  xmm0{k7}, zmm3, 0x55                                   }
testcase    { 0x62, 0xf3, 0x7d, 0xcf, 0x39, 0xd8, 0x55                                     }, { {evex} VEXTRACTI32X4  xmm0{k7}{z}, zmm3, 0x55                                }
testcase    { 0x62, 0xf3, 0x7d, 0x48, 0x39, 0x19, 0x55                                     }, { {evex} VEXTRACTI32X4  [rcx], zmm3, 0x55                                      }
testcase    { 0x62, 0xf3, 0x7d, 0x4f, 0x39, 0x19, 0x55                                     }, { {evex} VEXTRACTI32X4  [rcx]{k7}, zmm3, 0x55                                  }
testcase    { 0x62, 0xf3, 0x7d, 0x28, 0x39, 0xc8, 0x55                                     }, { {evex} VEXTRACTI32X4  xmm0, ymm1, 0x55                                       }
testcase    { 0x62, 0xf3, 0x7d, 0x2f, 0x39, 0xc8, 0x55                                     }, { {evex} VEXTRACTI32X4  xmm0{k7}, ymm1, 0x55                                   }
testcase    { 0x62, 0xf3, 0x7d, 0xaf, 0x39, 0xc8, 0x55                                     }, { {evex} VEXTRACTI32X4  xmm0{k7}{z}, ymm1, 0x55                                }
testcase    { 0x62, 0xf3, 0x7d, 0x48, 0x39, 0xd8, 0x55                                     }, { {evex} VEXTRACTI32X4  xmm0, zmm3, 0x55                                       }
testcase    { 0x62, 0xf3, 0x7d, 0x4f, 0x39, 0xd8, 0x55                                     }, { {evex} VEXTRACTI32X4  xmm0{k7}, zmm3, 0x55                                   }
testcase    { 0x62, 0xf3, 0x7d, 0xcf, 0x39, 0xd8, 0x55                                     }, { {evex} VEXTRACTI32X4  xmm0{k7}{z}, zmm3, 0x55                                }
testcase    { 0x62, 0xb3, 0x7d, 0x29, 0x39, 0x0c, 0xf0, 0x55                               }, { {evex} VEXTRACTI32X4  oword [rax+r14*8]{k1}, ymm1, 0x55                      }
testcase    { 0x62, 0xb3, 0x7d, 0x49, 0x39, 0x1c, 0xf0, 0x55                               }, { {evex} VEXTRACTI32X4  oword [rax+r14*8]{k1}, zmm3, 0x55                      }
testcase    { 0x62, 0xf3, 0x7d, 0x48, 0x3b, 0xd8, 0x55                                     }, { {evex} VEXTRACTI32X8  ymm0, zmm3, 0x55                                       }
testcase    { 0x62, 0xf3, 0x7d, 0x4f, 0x3b, 0xd8, 0x55                                     }, { {evex} VEXTRACTI32X8  ymm0{k7}, zmm3, 0x55                                   }
testcase    { 0x62, 0xf3, 0x7d, 0xcf, 0x3b, 0xd8, 0x55                                     }, { {evex} VEXTRACTI32X8  ymm0{k7}{z}, zmm3, 0x55                                }
testcase    { 0x62, 0xf3, 0x7d, 0x48, 0x3b, 0x19, 0x55                                     }, { {evex} VEXTRACTI32X8  [rcx], zmm3, 0x55                                      }
testcase    { 0x62, 0xf3, 0x7d, 0x4f, 0x3b, 0x19, 0x55                                     }, { {evex} VEXTRACTI32X8  [rcx]{k7}, zmm3, 0x55                                  }
testcase    { 0x62, 0xf3, 0x7d, 0x48, 0x3b, 0xd8, 0x55                                     }, { {evex} VEXTRACTI32X8  ymm0, zmm3, 0x55                                       }
testcase    { 0x62, 0xf3, 0x7d, 0x4f, 0x3b, 0xd8, 0x55                                     }, { {evex} VEXTRACTI32X8  ymm0{k7}, zmm3, 0x55                                   }
testcase    { 0x62, 0xf3, 0x7d, 0xcf, 0x3b, 0xd8, 0x55                                     }, { {evex} VEXTRACTI32X8  ymm0{k7}{z}, zmm3, 0x55                                }
testcase    { 0x62, 0xb3, 0x7d, 0x49, 0x3b, 0x1c, 0xf0, 0x55                               }, { {evex} VEXTRACTI32X8  yword [rax+r14*8]{k1}, zmm3, 0x55                      }
testcase    { 0x62, 0xf3, 0xfd, 0x28, 0x39, 0xc8, 0x55                                     }, { {evex} VEXTRACTI64X2  xmm0, ymm1, 0x55                                       }
testcase    { 0x62, 0xf3, 0xfd, 0x2f, 0x39, 0xc8, 0x55                                     }, { {evex} VEXTRACTI64X2  xmm0{k7}, ymm1, 0x55                                   }
testcase    { 0x62, 0xf3, 0xfd, 0xaf, 0x39, 0xc8, 0x55                                     }, { {evex} VEXTRACTI64X2  xmm0{k7}{z}, ymm1, 0x55                                }
testcase    { 0x62, 0xf3, 0xfd, 0x28, 0x39, 0x09, 0x55                                     }, { {evex} VEXTRACTI64X2  [rcx], ymm1, 0x55                                      }
testcase    { 0x62, 0xf3, 0xfd, 0x2f, 0x39, 0x09, 0x55                                     }, { {evex} VEXTRACTI64X2  [rcx]{k7}, ymm1, 0x55                                  }
testcase    { 0x62, 0xf3, 0xfd, 0x48, 0x39, 0xd8, 0x55                                     }, { {evex} VEXTRACTI64X2  xmm0, zmm3, 0x55                                       }
testcase    { 0x62, 0xf3, 0xfd, 0x4f, 0x39, 0xd8, 0x55                                     }, { {evex} VEXTRACTI64X2  xmm0{k7}, zmm3, 0x55                                   }
testcase    { 0x62, 0xf3, 0xfd, 0xcf, 0x39, 0xd8, 0x55                                     }, { {evex} VEXTRACTI64X2  xmm0{k7}{z}, zmm3, 0x55                                }
testcase    { 0x62, 0xf3, 0xfd, 0x48, 0x39, 0x19, 0x55                                     }, { {evex} VEXTRACTI64X2  [rcx], zmm3, 0x55                                      }
testcase    { 0x62, 0xf3, 0xfd, 0x4f, 0x39, 0x19, 0x55                                     }, { {evex} VEXTRACTI64X2  [rcx]{k7}, zmm3, 0x55                                  }
testcase    { 0x62, 0xf3, 0xfd, 0x28, 0x39, 0xc8, 0x55                                     }, { {evex} VEXTRACTI64X2  xmm0, ymm1, 0x55                                       }
testcase    { 0x62, 0xf3, 0xfd, 0x2f, 0x39, 0xc8, 0x55                                     }, { {evex} VEXTRACTI64X2  xmm0{k7}, ymm1, 0x55                                   }
testcase    { 0x62, 0xf3, 0xfd, 0xaf, 0x39, 0xc8, 0x55                                     }, { {evex} VEXTRACTI64X2  xmm0{k7}{z}, ymm1, 0x55                                }
testcase    { 0x62, 0xf3, 0xfd, 0x48, 0x39, 0xd8, 0x55                                     }, { {evex} VEXTRACTI64X2  xmm0, zmm3, 0x55                                       }
testcase    { 0x62, 0xf3, 0xfd, 0x4f, 0x39, 0xd8, 0x55                                     }, { {evex} VEXTRACTI64X2  xmm0{k7}, zmm3, 0x55                                   }
testcase    { 0x62, 0xf3, 0xfd, 0xcf, 0x39, 0xd8, 0x55                                     }, { {evex} VEXTRACTI64X2  xmm0{k7}{z}, zmm3, 0x55                                }
testcase    { 0x62, 0xb3, 0xfd, 0x29, 0x39, 0x0c, 0xf0, 0x55                               }, { {evex} VEXTRACTI64X2  oword [rax+r14*8]{k1}, ymm1, 0x55                      }
testcase    { 0x62, 0xb3, 0xfd, 0x49, 0x39, 0x1c, 0xf0, 0x55                               }, { {evex} VEXTRACTI64X2  oword [rax+r14*8]{k1}, zmm3, 0x55                      }
testcase    { 0x62, 0xf3, 0xfd, 0x48, 0x3b, 0xd8, 0x55                                     }, { {evex} VEXTRACTI64X4  ymm0, zmm3, 0x55                                       }
testcase    { 0x62, 0xf3, 0xfd, 0x4f, 0x3b, 0xd8, 0x55                                     }, { {evex} VEXTRACTI64X4  ymm0{k7}, zmm3, 0x55                                   }
testcase    { 0x62, 0xf3, 0xfd, 0xcf, 0x3b, 0xd8, 0x55                                     }, { {evex} VEXTRACTI64X4  ymm0{k7}{z}, zmm3, 0x55                                }
testcase    { 0x62, 0xf3, 0xfd, 0x48, 0x3b, 0x19, 0x55                                     }, { {evex} VEXTRACTI64X4  [rcx], zmm3, 0x55                                      }
testcase    { 0x62, 0xf3, 0xfd, 0x4f, 0x3b, 0x19, 0x55                                     }, { {evex} VEXTRACTI64X4  [rcx]{k7}, zmm3, 0x55                                  }
testcase    { 0x62, 0xf3, 0xfd, 0x48, 0x3b, 0xd8, 0x55                                     }, { {evex} VEXTRACTI64X4  ymm0, zmm3, 0x55                                       }
testcase    { 0x62, 0xf3, 0xfd, 0x4f, 0x3b, 0xd8, 0x55                                     }, { {evex} VEXTRACTI64X4  ymm0{k7}, zmm3, 0x55                                   }
testcase    { 0x62, 0xf3, 0xfd, 0xcf, 0x3b, 0xd8, 0x55                                     }, { {evex} VEXTRACTI64X4  ymm0{k7}{z}, zmm3, 0x55                                }
testcase    { 0x62, 0xb3, 0xfd, 0x49, 0x3b, 0x1c, 0xf0, 0x55                               }, { {evex} VEXTRACTI64X4  yword [rax+r14*8]{k1}, zmm3, 0x55                      }

testcase        {  0x62, 0xf6, 0x77, 0x08, 0xd6, 0xc7                                        }, {        {evex} VFCMULCPH xmm0, xmm1, xmm7                                    }
testcase        {  0x62, 0xf6, 0x77, 0x8f, 0xd6, 0xc7                                        }, {        {evex} VFCMULCPH xmm0{k7}{z}, xmm1, xmm7                             }
testcase        {  0x62, 0xf6, 0x77, 0x8f, 0xd6, 0x00                                        }, {        {evex} VFCMULCPH xmm0{k7}{z}, xmm1, oword [rax]                      }
testcase        {  0x62, 0xf6, 0x77, 0x9f, 0xd6, 0x00                                        }, {        {evex} VFCMULCPH xmm0{k7}{z}, xmm1, dword [rax]{1to4}                }
testcase        {  0x62, 0xd6, 0x77, 0x28, 0xd6, 0xc7                                        }, {        {evex} VFCMULCPH ymm0, ymm1, ymm15                                   }
testcase        {  0x62, 0xf6, 0x77, 0x28, 0xd6, 0x00                                        }, {        {evex} VFCMULCPH ymm0, ymm1, yword [rax]                             }
testcase        {  0x62, 0xf6, 0x77, 0x08, 0xd6, 0x00                                        }, {        {evex} VFCMULCPH xmm0, xmm1, oword [rax]                             }
testcase        {  0x62, 0xd6, 0x77, 0x2f, 0xd6, 0xc7                                        }, {        {evex} VFCMULCPH ymm0{k7}, ymm1, ymm15                               }
testcase        {  0x62, 0xf6, 0x77, 0x2f, 0xd6, 0x00                                        }, {        {evex} VFCMULCPH ymm0{k7}, ymm1, yword [rax]                         }
testcase        {  0x62, 0xd6, 0x77, 0xaf, 0xd6, 0xc7                                        }, {        {evex} VFCMULCPH ymm0{k7}{z}, ymm1, ymm15                            }
testcase        {  0x62, 0xf6, 0x77, 0xaf, 0xd6, 0x00                                        }, {        {evex} VFCMULCPH ymm0{k7}{z}, ymm1, yword [rax]                      }
testcase        {  0x62, 0xd6, 0x67, 0x48, 0xd6, 0xc7                                        }, {        {evex} VFCMULCPH zmm0, zmm3, zmm15                                   }
testcase        {  0x62, 0xf6, 0x67, 0x48, 0xd6, 0x00                                        }, {        {evex} VFCMULCPH zmm0, zmm3, zword [rax]                             }
testcase        {  0x62, 0xd6, 0x67, 0x4f, 0xd6, 0xc7                                        }, {        {evex} VFCMULCPH zmm0{k7}, zmm3, zmm15                               }
testcase        {  0x62, 0xf6, 0x67, 0x4f, 0xd6, 0x00                                        }, {        {evex} VFCMULCPH zmm0{k7}, zmm3, zword [rax]                         }
testcase        {  0x62, 0xf6, 0x77, 0x18, 0xd6, 0x00                                        }, {        {evex} VFCMULCPH xmm0, xmm1, dword [rax]{1to4}                       }
testcase        {  0x62, 0xd6, 0x67, 0xcf, 0xd6, 0xc7                                        }, {        {evex} VFCMULCPH zmm0{k7}{z}, zmm3, zmm15                            }
testcase        {  0x62, 0xf6, 0x67, 0xcf, 0xd6, 0x00                                        }, {        {evex} VFCMULCPH zmm0{k7}{z}, zmm3, zword [rax]                      }
testcase        {  0x62, 0xf6, 0x77, 0x0f, 0xd6, 0xc7                                        }, {        {evex} VFCMULCPH xmm0{k7}, xmm1, xmm7                                }
testcase        {  0x62, 0xf6, 0x77, 0x0f, 0xd6, 0x00                                        }, {        {evex} VFCMULCPH xmm0{k7}, xmm1, oword [rax]                         }
testcase        {  0x62, 0xf6, 0x77, 0x1f, 0xd6, 0x00                                        }, {        {evex} VFCMULCPH xmm0{k7}, xmm1, dword [rax]{1to4}                   }
testcase        {  0x62, 0xf6, 0x75, 0x08, 0x99, 0xc7                                        }, {        {evex} VFMADD132SH xmm0, xmm1, xmm7                                  }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x08, 0x99, 0x00                                  }, {        {evex} VFMADD132SH xmm0, xmm1, word [eax]                            }
testcase        {  0x62, 0xf6, 0x75, 0x0f, 0x99, 0xc7                                        }, {        {evex} VFMADD132SH xmm0{k7}, xmm1, xmm7                              }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x0f, 0x99, 0x00                                  }, {        {evex} VFMADD132SH xmm0{k7}, xmm1, word [eax]                        }
testcase        {  0x62, 0xf6, 0x75, 0x8f, 0x99, 0xc7                                        }, {        {evex} VFMADD132SH xmm0{k7}{z}, xmm1, xmm7                           }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x8f, 0x99, 0x00                                  }, {        {evex} VFMADD132SH xmm0{k7}{z}, xmm1, word [eax]                     }
testcase        {  0x62, 0xf6, 0x75, 0x08, 0xa9, 0xc7                                        }, {        {evex} VFMADD213SH xmm0, xmm1, xmm7                                  }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x08, 0xa9, 0x00                                  }, {        {evex} VFMADD213SH xmm0, xmm1, word [eax]                            }
testcase        {  0x62, 0xf6, 0x75, 0x0f, 0xa9, 0xc7                                        }, {        {evex} VFMADD213SH xmm0{k7}, xmm1, xmm7                              }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x0f, 0xa9, 0x00                                  }, {        {evex} VFMADD213SH xmm0{k7}, xmm1, word [eax]                        }
testcase        {  0x62, 0xf6, 0x75, 0x8f, 0xa9, 0xc7                                        }, {        {evex} VFMADD213SH xmm0{k7}{z}, xmm1, xmm7                           }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x8f, 0xa9, 0x00                                  }, {        {evex} VFMADD213SH xmm0{k7}{z}, xmm1, word [eax]                     }
testcase        {  0x62, 0xf6, 0x75, 0x08, 0xb9, 0xc7                                        }, {        {evex} VFMADD231SH xmm0, xmm1, xmm7                                  }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x08, 0xb9, 0x00                                  }, {        {evex} VFMADD231SH xmm0, xmm1, word [eax]                            }
testcase        {  0x62, 0xf6, 0x75, 0x0f, 0xb9, 0xc7                                        }, {        {evex} VFMADD231SH xmm0{k7}, xmm1, xmm7                              }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x0f, 0xb9, 0x00                                  }, {        {evex} VFMADD231SH xmm0{k7}, xmm1, word [eax]                        }
testcase        {  0x62, 0xf6, 0x75, 0x8f, 0xb9, 0xc7                                        }, {        {evex} VFMADD231SH xmm0{k7}{z}, xmm1, xmm7                           }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x8f, 0xb9, 0x00                                  }, {        {evex} VFMADD231SH xmm0{k7}{z}, xmm1, word [eax]                     }
testcase        {  0x62, 0xf6, 0x75, 0x08, 0x9b, 0xc7                                        }, {        {evex} VFMSUB132SH xmm0, xmm1, xmm7                                  }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x08, 0x9b, 0x00                                  }, {        {evex} VFMSUB132SH xmm0, xmm1, word [eax]                            }
testcase        {  0x62, 0xf6, 0x75, 0x0f, 0x9b, 0xc7                                        }, {        {evex} VFMSUB132SH xmm0{k7}, xmm1, xmm7                              }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x0f, 0x9b, 0x00                                  }, {        {evex} VFMSUB132SH xmm0{k7}, xmm1, word [eax]                        }
testcase        {  0x62, 0xf6, 0x75, 0x8f, 0x9b, 0xc7                                        }, {        {evex} VFMSUB132SH xmm0{k7}{z}, xmm1, xmm7                           }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x8f, 0x9b, 0x00                                  }, {        {evex} VFMSUB132SH xmm0{k7}{z}, xmm1, word [eax]                     }
testcase        {  0x62, 0xf6, 0x75, 0x08, 0x9b, 0xc7                                        }, {        {evex} VFMSUB132SH xmm0, xmm1, xmm7                                  }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x08, 0x9b, 0x00                                  }, {        {evex} VFMSUB132SH xmm0, xmm1, word [eax]                            }
testcase        {  0x62, 0xf6, 0x75, 0x0f, 0x9b, 0xc7                                        }, {        {evex} VFMSUB132SH xmm0{k7}, xmm1, xmm7                              }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x0f, 0x9b, 0x00                                  }, {        {evex} VFMSUB132SH xmm0{k7}, xmm1, word [eax]                        }
testcase        {  0x62, 0xf6, 0x75, 0x8f, 0x9b, 0xc7                                        }, {        {evex} VFMSUB132SH xmm0{k7}{z}, xmm1, xmm7                           }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x8f, 0x9b, 0x00                                  }, {        {evex} VFMSUB132SH xmm0{k7}{z}, xmm1, word [eax]                     }
testcase        {  0x62, 0xf6, 0x75, 0x08, 0xab, 0xc7                                        }, {        {evex} VFMSUB213SH xmm0, xmm1, xmm7                                  }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x08, 0xab, 0x00                                  }, {        {evex} VFMSUB213SH xmm0, xmm1, word [eax]                            }
testcase        {  0x62, 0xf6, 0x75, 0x0f, 0xab, 0xc7                                        }, {        {evex} VFMSUB213SH xmm0{k7}, xmm1, xmm7                              }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x0f, 0xab, 0x00                                  }, {        {evex} VFMSUB213SH xmm0{k7}, xmm1, word [eax]                        }
testcase        {  0x62, 0xf6, 0x75, 0x8f, 0xab, 0xc7                                        }, {        {evex} VFMSUB213SH xmm0{k7}{z}, xmm1, xmm7                           }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x8f, 0xab, 0x00                                  }, {        {evex} VFMSUB213SH xmm0{k7}{z}, xmm1, word [eax]                     }
testcase        {  0x62, 0xf6, 0x75, 0x08, 0xab, 0xc7                                        }, {        {evex} VFMSUB213SH xmm0, xmm1, xmm7                                  }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x08, 0xab, 0x00                                  }, {        {evex} VFMSUB213SH xmm0, xmm1, word [eax]                            }
testcase        {  0x62, 0xf6, 0x75, 0x0f, 0xab, 0xc7                                        }, {        {evex} VFMSUB213SH xmm0{k7}, xmm1, xmm7                              }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x0f, 0xab, 0x00                                  }, {        {evex} VFMSUB213SH xmm0{k7}, xmm1, word [eax]                        }
testcase        {  0x62, 0xf6, 0x75, 0x8f, 0xab, 0xc7                                        }, {        {evex} VFMSUB213SH xmm0{k7}{z}, xmm1, xmm7                           }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x8f, 0xab, 0x00                                  }, {        {evex} VFMSUB213SH xmm0{k7}{z}, xmm1, word [eax]                     }
testcase        {  0x62, 0xf6, 0x75, 0x08, 0xbb, 0xc7                                        }, {        {evex} VFMSUB231SH xmm0, xmm1, xmm7                                  }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x08, 0xbb, 0x00                                  }, {        {evex} VFMSUB231SH xmm0, xmm1, word [eax]                            }
testcase        {  0x62, 0xf6, 0x75, 0x0f, 0xbb, 0xc7                                        }, {        {evex} VFMSUB231SH xmm0{k7}, xmm1, xmm7                              }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x0f, 0xbb, 0x00                                  }, {        {evex} VFMSUB231SH xmm0{k7}, xmm1, word [eax]                        }
testcase        {  0x62, 0xf6, 0x75, 0x8f, 0xbb, 0xc7                                        }, {        {evex} VFMSUB231SH xmm0{k7}{z}, xmm1, xmm7                           }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x8f, 0xbb, 0x00                                  }, {        {evex} VFMSUB231SH xmm0{k7}{z}, xmm1, word [eax]                     }
testcase        {  0x62, 0xf6, 0x75, 0x08, 0xbb, 0xc7                                        }, {        {evex} VFMSUB231SH xmm0, xmm1, xmm7                                  }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x08, 0xbb, 0x00                                  }, {        {evex} VFMSUB231SH xmm0, xmm1, word [eax]                            }
testcase        {  0x62, 0xf6, 0x75, 0x0f, 0xbb, 0xc7                                        }, {        {evex} VFMSUB231SH xmm0{k7}, xmm1, xmm7                              }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x0f, 0xbb, 0x00                                  }, {        {evex} VFMSUB231SH xmm0{k7}, xmm1, word [eax]                        }
testcase        {  0x62, 0xf6, 0x75, 0x8f, 0xbb, 0xc7                                        }, {        {evex} VFMSUB231SH xmm0{k7}{z}, xmm1, xmm7                           }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x8f, 0xbb, 0x00                                  }, {        {evex} VFMSUB231SH xmm0{k7}{z}, xmm1, word [eax]                     }
testcase        {  0x62, 0xf6, 0x76, 0x08, 0xd6, 0xc7                                        }, {        {evex} VFMULCPH xmm0, xmm1, xmm7                                     }
testcase        {  0x62, 0xf6, 0x76, 0x8f, 0xd6, 0xc7                                        }, {        {evex} VFMULCPH xmm0{k7}{z}, xmm1, xmm7                              }
testcase        {  0x62, 0xf6, 0x76, 0x8f, 0xd6, 0x00                                        }, {        {evex} VFMULCPH xmm0{k7}{z}, xmm1, oword [rax]                       }
testcase        {  0x62, 0xf6, 0x76, 0x9f, 0xd6, 0x00                                        }, {        {evex} VFMULCPH xmm0{k7}{z}, xmm1, dword [rax]{1to4}                 }
testcase        {  0x62, 0xd6, 0x76, 0x28, 0xd6, 0xc7                                        }, {        {evex} VFMULCPH ymm0, ymm1, ymm15                                    }
testcase        {  0x62, 0xf6, 0x76, 0x28, 0xd6, 0x00                                        }, {        {evex} VFMULCPH ymm0, ymm1, yword [rax]                              }
testcase        {  0x62, 0xf6, 0x76, 0x08, 0xd6, 0x00                                        }, {        {evex} VFMULCPH xmm0, xmm1, oword [rax]                              }
testcase        {  0x62, 0xd6, 0x76, 0x2f, 0xd6, 0xc7                                        }, {        {evex} VFMULCPH ymm0{k7}, ymm1, ymm15                                }
testcase        {  0x62, 0xf6, 0x76, 0x2f, 0xd6, 0x00                                        }, {        {evex} VFMULCPH ymm0{k7}, ymm1, yword [rax]                          }
testcase        {  0x62, 0xd6, 0x76, 0xaf, 0xd6, 0xc7                                        }, {        {evex} VFMULCPH ymm0{k7}{z}, ymm1, ymm15                             }
testcase        {  0x62, 0xf6, 0x76, 0xaf, 0xd6, 0x00                                        }, {        {evex} VFMULCPH ymm0{k7}{z}, ymm1, yword [rax]                       }
testcase        {  0x62, 0xd6, 0x66, 0x48, 0xd6, 0xc7                                        }, {        {evex} VFMULCPH zmm0, zmm3, zmm15                                    }
testcase        {  0x62, 0xf6, 0x66, 0x48, 0xd6, 0x00                                        }, {        {evex} VFMULCPH zmm0, zmm3, zword [rax]                              }
testcase        {  0x62, 0xd6, 0x66, 0x4f, 0xd6, 0xc7                                        }, {        {evex} VFMULCPH zmm0{k7}, zmm3, zmm15                                }
testcase        {  0x62, 0xf6, 0x66, 0x4f, 0xd6, 0x00                                        }, {        {evex} VFMULCPH zmm0{k7}, zmm3, zword [rax]                          }
testcase        {  0x62, 0xf6, 0x76, 0x18, 0xd6, 0x00                                        }, {        {evex} VFMULCPH xmm0, xmm1, dword [rax]{1to4}                        }
testcase        {  0x62, 0xd6, 0x66, 0xcf, 0xd6, 0xc7                                        }, {        {evex} VFMULCPH zmm0{k7}{z}, zmm3, zmm15                             }
testcase        {  0x62, 0xf6, 0x66, 0xcf, 0xd6, 0x00                                        }, {        {evex} VFMULCPH zmm0{k7}{z}, zmm3, zword [rax]                       }
testcase        {  0x62, 0xf6, 0x76, 0x0f, 0xd6, 0xc7                                        }, {        {evex} VFMULCPH xmm0{k7}, xmm1, xmm7                                 }
testcase        {  0x62, 0xf6, 0x76, 0x0f, 0xd6, 0x00                                        }, {        {evex} VFMULCPH xmm0{k7}, xmm1, oword [rax]                          }
testcase        {  0x62, 0xf6, 0x76, 0x1f, 0xd6, 0x00                                        }, {        {evex} VFMULCPH xmm0{k7}, xmm1, dword [rax]{1to4}                    }
testcase        {  0x62, 0xf6, 0x75, 0x08, 0x9d, 0xc7                                        }, {        {evex} VFNMADD132SH xmm0, xmm1, xmm7                                 }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x08, 0x9d, 0x00                                  }, {        {evex} VFNMADD132SH xmm0, xmm1, word [eax]                           }
testcase        {  0x62, 0xf6, 0x75, 0x0f, 0x9d, 0xc7                                        }, {        {evex} VFNMADD132SH xmm0{k7}, xmm1, xmm7                             }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x0f, 0x9d, 0x00                                  }, {        {evex} VFNMADD132SH xmm0{k7}, xmm1, word [eax]                       }
testcase        {  0x62, 0xf6, 0x75, 0x8f, 0x9d, 0xc7                                        }, {        {evex} VFNMADD132SH xmm0{k7}{z}, xmm1, xmm7                          }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x8f, 0x9d, 0x00                                  }, {        {evex} VFNMADD132SH xmm0{k7}{z}, xmm1, word [eax]                    }
testcase        {  0x62, 0xf6, 0x75, 0x08, 0xad, 0xc7                                        }, {        {evex} VFNMADD213SH xmm0, xmm1, xmm7                                 }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x08, 0xad, 0x00                                  }, {        {evex} VFNMADD213SH xmm0, xmm1, word [eax]                           }
testcase        {  0x62, 0xf6, 0x75, 0x0f, 0xad, 0xc7                                        }, {        {evex} VFNMADD213SH xmm0{k7}, xmm1, xmm7                             }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x0f, 0xad, 0x00                                  }, {        {evex} VFNMADD213SH xmm0{k7}, xmm1, word [eax]                       }
testcase        {  0x62, 0xf6, 0x75, 0x8f, 0xad, 0xc7                                        }, {        {evex} VFNMADD213SH xmm0{k7}{z}, xmm1, xmm7                          }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x8f, 0xad, 0x00                                  }, {        {evex} VFNMADD213SH xmm0{k7}{z}, xmm1, word [eax]                    }
testcase        {  0x62, 0xf6, 0x75, 0x08, 0xbd, 0xc7                                        }, {        {evex} VFNMADD231SH xmm0, xmm1, xmm7                                 }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x08, 0xbd, 0x00                                  }, {        {evex} VFNMADD231SH xmm0, xmm1, word [eax]                           }
testcase        {  0x62, 0xf6, 0x75, 0x0f, 0xbd, 0xc7                                        }, {        {evex} VFNMADD231SH xmm0{k7}, xmm1, xmm7                             }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x0f, 0xbd, 0x00                                  }, {        {evex} VFNMADD231SH xmm0{k7}, xmm1, word [eax]                       }
testcase        {  0x62, 0xf6, 0x75, 0x8f, 0xbd, 0xc7                                        }, {        {evex} VFNMADD231SH xmm0{k7}{z}, xmm1, xmm7                          }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x8f, 0xbd, 0x00                                  }, {        {evex} VFNMADD231SH xmm0{k7}{z}, xmm1, word [eax]                    }
testcase        {  0x62, 0xf6, 0x75, 0x08, 0x9f, 0xc7                                        }, {        {evex} VFNMSUB132SH xmm0, xmm1, xmm7                                 }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x08, 0x9f, 0x00                                  }, {        {evex} VFNMSUB132SH xmm0, xmm1, word [eax]                           }
testcase        {  0x62, 0xf6, 0x75, 0x0f, 0x9f, 0xc7                                        }, {        {evex} VFNMSUB132SH xmm0{k7}, xmm1, xmm7                             }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x0f, 0x9f, 0x00                                  }, {        {evex} VFNMSUB132SH xmm0{k7}, xmm1, word [eax]                       }
testcase        {  0x62, 0xf6, 0x75, 0x8f, 0x9f, 0xc7                                        }, {        {evex} VFNMSUB132SH xmm0{k7}{z}, xmm1, xmm7                          }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x8f, 0x9f, 0x00                                  }, {        {evex} VFNMSUB132SH xmm0{k7}{z}, xmm1, word [eax]                    }
testcase        {  0x62, 0xf6, 0x75, 0x08, 0x9f, 0xc7                                        }, {        {evex} VFNMSUB132SH xmm0, xmm1, xmm7                                 }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x08, 0x9f, 0x00                                  }, {        {evex} VFNMSUB132SH xmm0, xmm1, word [eax]                           }
testcase        {  0x62, 0xf6, 0x75, 0x0f, 0x9f, 0xc7                                        }, {        {evex} VFNMSUB132SH xmm0{k7}, xmm1, xmm7                             }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x0f, 0x9f, 0x00                                  }, {        {evex} VFNMSUB132SH xmm0{k7}, xmm1, word [eax]                       }
testcase        {  0x62, 0xf6, 0x75, 0x8f, 0x9f, 0xc7                                        }, {        {evex} VFNMSUB132SH xmm0{k7}{z}, xmm1, xmm7                          }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x8f, 0x9f, 0x00                                  }, {        {evex} VFNMSUB132SH xmm0{k7}{z}, xmm1, word [eax]                    }
testcase        {  0x62, 0xf6, 0x75, 0x08, 0xaf, 0xc7                                        }, {        {evex} VFNMSUB213SH xmm0, xmm1, xmm7                                 }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x08, 0xaf, 0x00                                  }, {        {evex} VFNMSUB213SH xmm0, xmm1, word [eax]                           }
testcase        {  0x62, 0xf6, 0x75, 0x0f, 0xaf, 0xc7                                        }, {        {evex} VFNMSUB213SH xmm0{k7}, xmm1, xmm7                             }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x0f, 0xaf, 0x00                                  }, {        {evex} VFNMSUB213SH xmm0{k7}, xmm1, word [eax]                       }
testcase        {  0x62, 0xf6, 0x75, 0x8f, 0xaf, 0xc7                                        }, {        {evex} VFNMSUB213SH xmm0{k7}{z}, xmm1, xmm7                          }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x8f, 0xaf, 0x00                                  }, {        {evex} VFNMSUB213SH xmm0{k7}{z}, xmm1, word [eax]                    }
testcase        {  0x62, 0xf6, 0x75, 0x08, 0xaf, 0xc7                                        }, {        {evex} VFNMSUB213SH xmm0, xmm1, xmm7                                 }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x08, 0xaf, 0x00                                  }, {        {evex} VFNMSUB213SH xmm0, xmm1, word [eax]                           }
testcase        {  0x62, 0xf6, 0x75, 0x0f, 0xaf, 0xc7                                        }, {        {evex} VFNMSUB213SH xmm0{k7}, xmm1, xmm7                             }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x0f, 0xaf, 0x00                                  }, {        {evex} VFNMSUB213SH xmm0{k7}, xmm1, word [eax]                       }
testcase        {  0x62, 0xf6, 0x75, 0x8f, 0xaf, 0xc7                                        }, {        {evex} VFNMSUB213SH xmm0{k7}{z}, xmm1, xmm7                          }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x8f, 0xaf, 0x00                                  }, {        {evex} VFNMSUB213SH xmm0{k7}{z}, xmm1, word [eax]                    }
testcase        {  0x62, 0xf6, 0x75, 0x08, 0xbf, 0xc7                                        }, {        {evex} VFNMSUB231SH xmm0, xmm1, xmm7                                 }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x08, 0xbf, 0x00                                  }, {        {evex} VFNMSUB231SH xmm0, xmm1, word [eax]                           }
testcase        {  0x62, 0xf6, 0x75, 0x0f, 0xbf, 0xc7                                        }, {        {evex} VFNMSUB231SH xmm0{k7}, xmm1, xmm7                             }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x0f, 0xbf, 0x00                                  }, {        {evex} VFNMSUB231SH xmm0{k7}, xmm1, word [eax]                       }
testcase        {  0x62, 0xf6, 0x75, 0x8f, 0xbf, 0xc7                                        }, {        {evex} VFNMSUB231SH xmm0{k7}{z}, xmm1, xmm7                          }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x8f, 0xbf, 0x00                                  }, {        {evex} VFNMSUB231SH xmm0{k7}{z}, xmm1, word [eax]                    }
testcase        {  0x62, 0xf6, 0x75, 0x08, 0xbf, 0xc7                                        }, {        {evex} VFNMSUB231SH xmm0, xmm1, xmm7                                 }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x08, 0xbf, 0x00                                  }, {        {evex} VFNMSUB231SH xmm0, xmm1, word [eax]                           }
testcase        {  0x62, 0xf6, 0x75, 0x0f, 0xbf, 0xc7                                        }, {        {evex} VFNMSUB231SH xmm0{k7}, xmm1, xmm7                             }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x0f, 0xbf, 0x00                                  }, {        {evex} VFNMSUB231SH xmm0{k7}, xmm1, word [eax]                       }
testcase        {  0x62, 0xf6, 0x75, 0x8f, 0xbf, 0xc7                                        }, {        {evex} VFNMSUB231SH xmm0{k7}{z}, xmm1, xmm7                          }
testcase        {  0x67, 0x62, 0xf6, 0x75, 0x8f, 0xbf, 0x00                                  }, {        {evex} VFNMSUB231SH xmm0{k7}{z}, xmm1, word [eax]                    }
testcase        {  0x62, 0xf3, 0x6d, 0x28, 0x18, 0xc5, 0x55                                  }, {        {evex} VINSERTF32X4 ymm0, ymm2, xmm5, 0x55                           }
testcase        {  0x62, 0xf3, 0x6d, 0x48, 0x18, 0xc5, 0x55                                  }, {        {evex} VINSERTF32X4 zmm0, zmm2, xmm5, 0x55                           }
testcase        {  0x62, 0xb3, 0x6d, 0x48, 0x18, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTF32X4 zmm0, zmm2, oword [rax+r14*8], 0x55              }
testcase        {  0x62, 0xf3, 0x6d, 0x4f, 0x18, 0xc5, 0x55                                  }, {        {evex} VINSERTF32X4 zmm0{k7}, zmm2, xmm5, 0x55                       }
testcase        {  0x62, 0xb3, 0x6d, 0x4f, 0x18, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTF32X4 zmm0{k7}, zmm2, oword [rax+r14*8], 0x55          }
testcase        {  0x62, 0xb3, 0x6d, 0x28, 0x18, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTF32X4 ymm0, ymm2, oword [rax+r14*8], 0x55              }
testcase        {  0x62, 0xf3, 0x6d, 0xcf, 0x18, 0xc5, 0x55                                  }, {        {evex} VINSERTF32X4 zmm0{k7}{z}, zmm2, xmm5, 0x55                    }
testcase        {  0x62, 0xb3, 0x6d, 0xcf, 0x18, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTF32X4 zmm0{k7}{z}, zmm2, oword [rax+r14*8], 0x55       }
testcase        {  0x62, 0xf3, 0x6d, 0x2f, 0x18, 0xc5, 0x55                                  }, {        {evex} VINSERTF32X4 ymm0{k7}, ymm2, xmm5, 0x55                       }
testcase        {  0x62, 0xb3, 0x6d, 0x2f, 0x18, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTF32X4 ymm0{k7}, ymm2, oword [rax+r14*8], 0x55          }
testcase        {  0x62, 0xf3, 0x6d, 0xaf, 0x18, 0xc5, 0x55                                  }, {        {evex} VINSERTF32X4 ymm0{k7}{z}, ymm2, xmm5, 0x55                    }
testcase        {  0x62, 0xb3, 0x6d, 0xaf, 0x18, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTF32X4 ymm0{k7}{z}, ymm2, oword [rax+r14*8], 0x55       }
testcase        {  0x62, 0xf3, 0x6d, 0x28, 0x18, 0xc5, 0x55                                  }, {        {evex} VINSERTF32X4 ymm0, ymm2, xmm5, 0x55                           }
testcase        {  0x62, 0xf3, 0x6d, 0x48, 0x18, 0xc5, 0x55                                  }, {        {evex} VINSERTF32X4 zmm0, zmm2, xmm5, 0x55                           }
testcase        {  0x62, 0xb3, 0x6d, 0x48, 0x18, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTF32X4 zmm0, zmm2, oword [rax+r14*8], 0x55              }
testcase        {  0x62, 0xf3, 0x6d, 0x4f, 0x18, 0xc5, 0x55                                  }, {        {evex} VINSERTF32X4 zmm0{k7}, zmm2, xmm5, 0x55                       }
testcase        {  0x62, 0xb3, 0x6d, 0x4f, 0x18, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTF32X4 zmm0{k7}, zmm2, oword [rax+r14*8], 0x55          }
testcase        {  0x62, 0xb3, 0x6d, 0x28, 0x18, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTF32X4 ymm0, ymm2, oword [rax+r14*8], 0x55              }
testcase        {  0x62, 0xf3, 0x6d, 0xcf, 0x18, 0xc5, 0x55                                  }, {        {evex} VINSERTF32X4 zmm0{k7}{z}, zmm2, xmm5, 0x55                    }
testcase        {  0x62, 0xb3, 0x6d, 0xcf, 0x18, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTF32X4 zmm0{k7}{z}, zmm2, oword [rax+r14*8], 0x55       }
testcase        {  0x62, 0xf3, 0x6d, 0x2f, 0x18, 0xc5, 0x55                                  }, {        {evex} VINSERTF32X4 ymm0{k7}, ymm2, xmm5, 0x55                       }
testcase        {  0x62, 0xb3, 0x6d, 0x2f, 0x18, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTF32X4 ymm0{k7}, ymm2, oword [rax+r14*8], 0x55          }
testcase        {  0x62, 0xf3, 0x6d, 0xaf, 0x18, 0xc5, 0x55                                  }, {        {evex} VINSERTF32X4 ymm0{k7}{z}, ymm2, xmm5, 0x55                    }
testcase        {  0x62, 0xb3, 0x6d, 0xaf, 0x18, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTF32X4 ymm0{k7}{z}, ymm2, oword [rax+r14*8], 0x55       }
testcase        {  0x62, 0xf3, 0xed, 0x28, 0x18, 0xc5, 0x55                                  }, {        {evex} VINSERTF64X2 ymm0, ymm2, xmm5, 0x55                           }
testcase        {  0x62, 0xf3, 0xed, 0x48, 0x18, 0xc5, 0x55                                  }, {        {evex} VINSERTF64X2 zmm0, zmm2, xmm5, 0x55                           }
testcase        {  0x62, 0xb3, 0xed, 0x48, 0x18, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTF64X2 zmm0, zmm2, oword [rax+r14*8], 0x55              }
testcase        {  0x62, 0xf3, 0xed, 0x4f, 0x18, 0xc5, 0x55                                  }, {        {evex} VINSERTF64X2 zmm0{k7}, zmm2, xmm5, 0x55                       }
testcase        {  0x62, 0xb3, 0xed, 0x4f, 0x18, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTF64X2 zmm0{k7}, zmm2, oword [rax+r14*8], 0x55          }
testcase        {  0x62, 0xb3, 0xed, 0x28, 0x18, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTF64X2 ymm0, ymm2, oword [rax+r14*8], 0x55              }
testcase        {  0x62, 0xf3, 0xed, 0xcf, 0x18, 0xc5, 0x55                                  }, {        {evex} VINSERTF64X2 zmm0{k7}{z}, zmm2, xmm5, 0x55                    }
testcase        {  0x62, 0xb3, 0xed, 0xcf, 0x18, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTF64X2 zmm0{k7}{z}, zmm2, oword [rax+r14*8], 0x55       }
testcase        {  0x62, 0xf3, 0xed, 0x2f, 0x18, 0xc5, 0x55                                  }, {        {evex} VINSERTF64X2 ymm0{k7}, ymm2, xmm5, 0x55                       }
testcase        {  0x62, 0xb3, 0xed, 0x2f, 0x18, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTF64X2 ymm0{k7}, ymm2, oword [rax+r14*8], 0x55          }
testcase        {  0x62, 0xf3, 0xed, 0xaf, 0x18, 0xc5, 0x55                                  }, {        {evex} VINSERTF64X2 ymm0{k7}{z}, ymm2, xmm5, 0x55                    }
testcase        {  0x62, 0xb3, 0xed, 0xaf, 0x18, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTF64X2 ymm0{k7}{z}, ymm2, oword [rax+r14*8], 0x55       }
testcase        {  0x62, 0xf3, 0xed, 0x28, 0x18, 0xc5, 0x55                                  }, {        {evex} VINSERTF64X2 ymm0, ymm2, xmm5, 0x55                           }
testcase        {  0x62, 0xf3, 0xed, 0x48, 0x18, 0xc5, 0x55                                  }, {        {evex} VINSERTF64X2 zmm0, zmm2, xmm5, 0x55                           }
testcase        {  0x62, 0xb3, 0xed, 0x48, 0x18, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTF64X2 zmm0, zmm2, oword [rax+r14*8], 0x55              }
testcase        {  0x62, 0xf3, 0xed, 0x4f, 0x18, 0xc5, 0x55                                  }, {        {evex} VINSERTF64X2 zmm0{k7}, zmm2, xmm5, 0x55                       }
testcase        {  0x62, 0xb3, 0xed, 0x4f, 0x18, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTF64X2 zmm0{k7}, zmm2, oword [rax+r14*8], 0x55          }
testcase        {  0x62, 0xb3, 0xed, 0x28, 0x18, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTF64X2 ymm0, ymm2, oword [rax+r14*8], 0x55              }
testcase        {  0x62, 0xf3, 0xed, 0xcf, 0x18, 0xc5, 0x55                                  }, {        {evex} VINSERTF64X2 zmm0{k7}{z}, zmm2, xmm5, 0x55                    }
testcase        {  0x62, 0xb3, 0xed, 0xcf, 0x18, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTF64X2 zmm0{k7}{z}, zmm2, oword [rax+r14*8], 0x55       }
testcase        {  0x62, 0xf3, 0xed, 0x2f, 0x18, 0xc5, 0x55                                  }, {        {evex} VINSERTF64X2 ymm0{k7}, ymm2, xmm5, 0x55                       }
testcase        {  0x62, 0xb3, 0xed, 0x2f, 0x18, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTF64X2 ymm0{k7}, ymm2, oword [rax+r14*8], 0x55          }
testcase        {  0x62, 0xf3, 0xed, 0xaf, 0x18, 0xc5, 0x55                                  }, {        {evex} VINSERTF64X2 ymm0{k7}{z}, ymm2, xmm5, 0x55                    }
testcase        {  0x62, 0xb3, 0xed, 0xaf, 0x18, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTF64X2 ymm0{k7}{z}, ymm2, oword [rax+r14*8], 0x55       }
testcase        {  0x62, 0xd3, 0x6d, 0x48, 0x1a, 0xc7, 0x55                                  }, {        {evex} VINSERTF32X8 zmm0, zmm2, ymm15, 0x55                          }
testcase        {  0x62, 0xd3, 0x6d, 0x4f, 0x1a, 0xc7, 0x55                                  }, {        {evex} VINSERTF32X8 zmm0{k7}, zmm2, ymm15, 0x55                      }
testcase        {  0x62, 0xd3, 0x6d, 0xcf, 0x1a, 0xc7, 0x55                                  }, {        {evex} VINSERTF32X8 zmm0{k7}{z}, zmm2, ymm15, 0x55                   }
testcase        {  0x62, 0xd3, 0xed, 0x48, 0x1a, 0xc7, 0x55                                  }, {        {evex} VINSERTF64X4 zmm0, zmm2, ymm15, 0x55                          }
testcase        {  0x62, 0xd3, 0xed, 0x4f, 0x1a, 0xc7, 0x55                                  }, {        {evex} VINSERTF64X4 zmm0{k7}, zmm2, ymm15, 0x55                      }
testcase        {  0x62, 0xd3, 0xed, 0xcf, 0x1a, 0xc7, 0x55                                  }, {        {evex} VINSERTF64X4 zmm0{k7}{z}, zmm2, ymm15, 0x55                   }
testcase        {  0x62, 0xf3, 0x6d, 0x28, 0x38, 0xc5, 0x55                                  }, {        {evex} VINSERTI32X4 ymm0, ymm2, xmm5, 0x55                           }
testcase        {  0x62, 0xf3, 0x6d, 0x48, 0x38, 0xc5, 0x55                                  }, {        {evex} VINSERTI32X4 zmm0, zmm2, xmm5, 0x55                           }
testcase        {  0x62, 0xb3, 0x6d, 0x48, 0x38, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTI32X4 zmm0, zmm2, oword [rax+r14*8], 0x55              }
testcase        {  0x62, 0xf3, 0x6d, 0x4f, 0x38, 0xc5, 0x55                                  }, {        {evex} VINSERTI32X4 zmm0{k7}, zmm2, xmm5, 0x55                       }
testcase        {  0x62, 0xb3, 0x6d, 0x4f, 0x38, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTI32X4 zmm0{k7}, zmm2, oword [rax+r14*8], 0x55          }
testcase        {  0x62, 0xb3, 0x6d, 0x28, 0x38, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTI32X4 ymm0, ymm2, oword [rax+r14*8], 0x55              }
testcase        {  0x62, 0xf3, 0x6d, 0xcf, 0x38, 0xc5, 0x55                                  }, {        {evex} VINSERTI32X4 zmm0{k7}{z}, zmm2, xmm5, 0x55                    }
testcase        {  0x62, 0xb3, 0x6d, 0xcf, 0x38, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTI32X4 zmm0{k7}{z}, zmm2, oword [rax+r14*8], 0x55       }
testcase        {  0x62, 0xf3, 0x6d, 0x2f, 0x38, 0xc5, 0x55                                  }, {        {evex} VINSERTI32X4 ymm0{k7}, ymm2, xmm5, 0x55                       }
testcase        {  0x62, 0xb3, 0x6d, 0x2f, 0x38, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTI32X4 ymm0{k7}, ymm2, oword [rax+r14*8], 0x55          }
testcase        {  0x62, 0xf3, 0x6d, 0xaf, 0x38, 0xc5, 0x55                                  }, {        {evex} VINSERTI32X4 ymm0{k7}{z}, ymm2, xmm5, 0x55                    }
testcase        {  0x62, 0xb3, 0x6d, 0xaf, 0x38, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTI32X4 ymm0{k7}{z}, ymm2, oword [rax+r14*8], 0x55       }
testcase        {  0x62, 0xf3, 0x6d, 0x28, 0x38, 0xc5, 0x55                                  }, {        {evex} VINSERTI32X4 ymm0, ymm2, xmm5, 0x55                           }
testcase        {  0x62, 0xf3, 0x6d, 0x48, 0x38, 0xc5, 0x55                                  }, {        {evex} VINSERTI32X4 zmm0, zmm2, xmm5, 0x55                           }
testcase        {  0x62, 0xb3, 0x6d, 0x48, 0x38, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTI32X4 zmm0, zmm2, oword [rax+r14*8], 0x55              }
testcase        {  0x62, 0xf3, 0x6d, 0x4f, 0x38, 0xc5, 0x55                                  }, {        {evex} VINSERTI32X4 zmm0{k7}, zmm2, xmm5, 0x55                       }
testcase        {  0x62, 0xb3, 0x6d, 0x4f, 0x38, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTI32X4 zmm0{k7}, zmm2, oword [rax+r14*8], 0x55          }
testcase        {  0x62, 0xb3, 0x6d, 0x28, 0x38, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTI32X4 ymm0, ymm2, oword [rax+r14*8], 0x55              }
testcase        {  0x62, 0xf3, 0x6d, 0xcf, 0x38, 0xc5, 0x55                                  }, {        {evex} VINSERTI32X4 zmm0{k7}{z}, zmm2, xmm5, 0x55                    }
testcase        {  0x62, 0xb3, 0x6d, 0xcf, 0x38, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTI32X4 zmm0{k7}{z}, zmm2, oword [rax+r14*8], 0x55       }
testcase        {  0x62, 0xf3, 0x6d, 0x2f, 0x38, 0xc5, 0x55                                  }, {        {evex} VINSERTI32X4 ymm0{k7}, ymm2, xmm5, 0x55                       }
testcase        {  0x62, 0xb3, 0x6d, 0x2f, 0x38, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTI32X4 ymm0{k7}, ymm2, oword [rax+r14*8], 0x55          }
testcase        {  0x62, 0xf3, 0x6d, 0xaf, 0x38, 0xc5, 0x55                                  }, {        {evex} VINSERTI32X4 ymm0{k7}{z}, ymm2, xmm5, 0x55                    }
testcase        {  0x62, 0xb3, 0x6d, 0xaf, 0x38, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTI32X4 ymm0{k7}{z}, ymm2, oword [rax+r14*8], 0x55       }
testcase        {  0x62, 0xf3, 0xed, 0x28, 0x38, 0xc5, 0x55                                  }, {        {evex} VINSERTI64X2 ymm0, ymm2, xmm5, 0x55                           }
testcase        {  0x62, 0xf3, 0xed, 0x48, 0x38, 0xc5, 0x55                                  }, {        {evex} VINSERTI64X2 zmm0, zmm2, xmm5, 0x55                           }
testcase        {  0x62, 0xb3, 0xed, 0x48, 0x38, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTI64X2 zmm0, zmm2, oword [rax+r14*8], 0x55              }
testcase        {  0x62, 0xf3, 0xed, 0x4f, 0x38, 0xc5, 0x55                                  }, {        {evex} VINSERTI64X2 zmm0{k7}, zmm2, xmm5, 0x55                       }
testcase        {  0x62, 0xb3, 0xed, 0x4f, 0x38, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTI64X2 zmm0{k7}, zmm2, oword [rax+r14*8], 0x55          }
testcase        {  0x62, 0xb3, 0xed, 0x28, 0x38, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTI64X2 ymm0, ymm2, oword [rax+r14*8], 0x55              }
testcase        {  0x62, 0xf3, 0xed, 0xcf, 0x38, 0xc5, 0x55                                  }, {        {evex} VINSERTI64X2 zmm0{k7}{z}, zmm2, xmm5, 0x55                    }
testcase        {  0x62, 0xb3, 0xed, 0xcf, 0x38, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTI64X2 zmm0{k7}{z}, zmm2, oword [rax+r14*8], 0x55       }
testcase        {  0x62, 0xf3, 0xed, 0x2f, 0x38, 0xc5, 0x55                                  }, {        {evex} VINSERTI64X2 ymm0{k7}, ymm2, xmm5, 0x55                       }
testcase        {  0x62, 0xb3, 0xed, 0x2f, 0x38, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTI64X2 ymm0{k7}, ymm2, oword [rax+r14*8], 0x55          }
testcase        {  0x62, 0xf3, 0xed, 0xaf, 0x38, 0xc5, 0x55                                  }, {        {evex} VINSERTI64X2 ymm0{k7}{z}, ymm2, xmm5, 0x55                    }
testcase        {  0x62, 0xb3, 0xed, 0xaf, 0x38, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTI64X2 ymm0{k7}{z}, ymm2, oword [rax+r14*8], 0x55       }
testcase        {  0x62, 0xf3, 0xed, 0x28, 0x38, 0xc5, 0x55                                  }, {        {evex} VINSERTI64X2 ymm0, ymm2, xmm5, 0x55                           }
testcase        {  0x62, 0xf3, 0xed, 0x48, 0x38, 0xc5, 0x55                                  }, {        {evex} VINSERTI64X2 zmm0, zmm2, xmm5, 0x55                           }
testcase        {  0x62, 0xb3, 0xed, 0x48, 0x38, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTI64X2 zmm0, zmm2, oword [rax+r14*8], 0x55              }
testcase        {  0x62, 0xf3, 0xed, 0x4f, 0x38, 0xc5, 0x55                                  }, {        {evex} VINSERTI64X2 zmm0{k7}, zmm2, xmm5, 0x55                       }
testcase        {  0x62, 0xb3, 0xed, 0x4f, 0x38, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTI64X2 zmm0{k7}, zmm2, oword [rax+r14*8], 0x55          }
testcase        {  0x62, 0xb3, 0xed, 0x28, 0x38, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTI64X2 ymm0, ymm2, oword [rax+r14*8], 0x55              }
testcase        {  0x62, 0xf3, 0xed, 0xcf, 0x38, 0xc5, 0x55                                  }, {        {evex} VINSERTI64X2 zmm0{k7}{z}, zmm2, xmm5, 0x55                    }
testcase        {  0x62, 0xb3, 0xed, 0xcf, 0x38, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTI64X2 zmm0{k7}{z}, zmm2, oword [rax+r14*8], 0x55       }
testcase        {  0x62, 0xf3, 0xed, 0x2f, 0x38, 0xc5, 0x55                                  }, {        {evex} VINSERTI64X2 ymm0{k7}, ymm2, xmm5, 0x55                       }
testcase        {  0x62, 0xb3, 0xed, 0x2f, 0x38, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTI64X2 ymm0{k7}, ymm2, oword [rax+r14*8], 0x55          }
testcase        {  0x62, 0xf3, 0xed, 0xaf, 0x38, 0xc5, 0x55                                  }, {        {evex} VINSERTI64X2 ymm0{k7}{z}, ymm2, xmm5, 0x55                    }
testcase        {  0x62, 0xb3, 0xed, 0xaf, 0x38, 0x04, 0xf0, 0x55                            }, {        {evex} VINSERTI64X2 ymm0{k7}{z}, ymm2, oword [rax+r14*8], 0x55       }
testcase        {  0x62, 0xd3, 0x6d, 0x48, 0x3a, 0xc7, 0x55                                  }, {        {evex} VINSERTI32X8 zmm0, zmm2, ymm15, 0x55                          }
testcase        {  0x62, 0xd3, 0x6d, 0x4f, 0x3a, 0xc7, 0x55                                  }, {        {evex} VINSERTI32X8 zmm0{k7}, zmm2, ymm15, 0x55                      }
testcase        {  0x62, 0xd3, 0x6d, 0xcf, 0x3a, 0xc7, 0x55                                  }, {        {evex} VINSERTI32X8 zmm0{k7}{z}, zmm2, ymm15, 0x55                   }
testcase        {  0x62, 0xd3, 0xed, 0x48, 0x3a, 0xc7, 0x55                                  }, {        {evex} VINSERTI64X4 zmm0, zmm2, ymm15, 0x55                          }
testcase        {  0x62, 0xd3, 0xed, 0x4f, 0x3a, 0xc7, 0x55                                  }, {        {evex} VINSERTI64X4 zmm0{k7}, zmm2, ymm15, 0x55                      }
testcase        {  0x62, 0xd3, 0xed, 0xcf, 0x3a, 0xc7, 0x55                                  }, {        {evex} VINSERTI64X4 zmm0{k7}{z}, zmm2, ymm15, 0x55                   }
testcase        {  0x62, 0xf5, 0x76, 0x08, 0x5f, 0xc7                                        }, {        {evex} VMAXSH xmm0, xmm1, xmm7                                       }
testcase        {  0x67, 0x62, 0xf5, 0x76, 0x08, 0x5f, 0x00                                  }, {        {evex} VMAXSH xmm0, xmm1, word [eax]                                 }
testcase        {  0x62, 0xf5, 0x76, 0x0f, 0x5f, 0xc7                                        }, {        {evex} VMAXSH xmm0{k7}, xmm1, xmm7                                   }
testcase        {  0x67, 0x62, 0xf5, 0x76, 0x0f, 0x5f, 0x00                                  }, {        {evex} VMAXSH xmm0{k7}, xmm1, word [eax]                             }
testcase        {  0x62, 0xf5, 0x76, 0x8f, 0x5f, 0xc7                                        }, {        {evex} VMAXSH xmm0{k7}{z}, xmm1, xmm7                                }
testcase        {  0x67, 0x62, 0xf5, 0x76, 0x8f, 0x5f, 0x00                                  }, {        {evex} VMAXSH xmm0{k7}{z}, xmm1, word [eax]                          }
testcase        {  0x62, 0xf5, 0x76, 0x08, 0x5d, 0xc7                                        }, {        {evex} VMINSH xmm0, xmm1, xmm7                                       }
testcase        {  0x67, 0x62, 0xf5, 0x76, 0x08, 0x5d, 0x00                                  }, {        {evex} VMINSH xmm0, xmm1, word [eax]                                 }
testcase        {  0x62, 0xf5, 0x76, 0x0f, 0x5d, 0xc7                                        }, {        {evex} VMINSH xmm0{k7}, xmm1, xmm7                                   }
testcase        {  0x67, 0x62, 0xf5, 0x76, 0x0f, 0x5d, 0x00                                  }, {        {evex} VMINSH xmm0{k7}, xmm1, word [eax]                             }
testcase        {  0x62, 0xf5, 0x76, 0x8f, 0x5d, 0xc7                                        }, {        {evex} VMINSH xmm0{k7}{z}, xmm1, xmm7                                }
testcase        {  0x67, 0x62, 0xf5, 0x76, 0x8f, 0x5d, 0x00                                  }, {        {evex} VMINSH xmm0{k7}{z}, xmm1, word [eax]                          }
testcase        {  0x62, 0xd3, 0x6d, 0x28, 0x23, 0xc7, 0x55                                  }, {        {evex} VSHUFF32X4 ymm0, ymm2, ymm15, 0x55                            }
testcase        {  0x62, 0xd3, 0x6d, 0x2f, 0x23, 0xc7, 0x55                                  }, {        {evex} VSHUFF32X4 ymm0{k7}, ymm2, ymm15, 0x55                        }
testcase        {  0x62, 0xf3, 0x6d, 0x2f, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF32X4 ymm0{k7}, ymm2, yword [rax], 0x55                  }
testcase        {  0x62, 0xf3, 0x6d, 0x28, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF32X4 ymm0, ymm2, yword [rax], 0x55                      }
testcase        {  0x62, 0xd3, 0x6d, 0xaf, 0x23, 0xc7, 0x55                                  }, {        {evex} VSHUFF32X4 ymm0{k7}{z}, ymm2, ymm15, 0x55                     }
testcase        {  0x62, 0xf3, 0x6d, 0xaf, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF32X4 ymm0{k7}{z}, ymm2, yword [rax], 0x55               }
testcase        {  0x62, 0xd3, 0x6d, 0x48, 0x23, 0xc7, 0x55                                  }, {        {evex} VSHUFF32X4 zmm0, zmm2, zmm15, 0x55                            }
testcase        {  0x62, 0xf3, 0x6d, 0x48, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF32X4 zmm0, zmm2, zword [rax], 0x55                      }
testcase        {  0x62, 0xd3, 0x6d, 0x4f, 0x23, 0xc7, 0x55                                  }, {        {evex} VSHUFF32X4 zmm0{k7}, zmm2, zmm15, 0x55                        }
testcase        {  0x62, 0xf3, 0x6d, 0x4f, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF32X4 zmm0{k7}, zmm2, zword [rax], 0x55                  }
testcase        {  0x62, 0xd3, 0x6d, 0xcf, 0x23, 0xc7, 0x55                                  }, {        {evex} VSHUFF32X4 zmm0{k7}{z}, zmm2, zmm15, 0x55                     }
testcase        {  0x62, 0xf3, 0x6d, 0xcf, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF32X4 zmm0{k7}{z}, zmm2, zword [rax], 0x55               }
testcase        {  0x62, 0xd3, 0x6d, 0x28, 0x23, 0xc7, 0x55                                  }, {        {evex} VSHUFF32X4 ymm0, ymm2, ymm15, 0x55                            }
testcase        {  0x62, 0xd3, 0x6d, 0x2f, 0x23, 0xc7, 0x55                                  }, {        {evex} VSHUFF32X4 ymm0{k7}, ymm2, ymm15, 0x55                        }
testcase        {  0x62, 0xf3, 0x6d, 0x2f, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF32X4 ymm0{k7}, ymm2, yword [rax], 0x55                  }
testcase        {  0x62, 0xf3, 0x6d, 0x28, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF32X4 ymm0, ymm2, yword [rax], 0x55                      }
testcase        {  0x62, 0xd3, 0x6d, 0xaf, 0x23, 0xc7, 0x55                                  }, {        {evex} VSHUFF32X4 ymm0{k7}{z}, ymm2, ymm15, 0x55                     }
testcase        {  0x62, 0xf3, 0x6d, 0xaf, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF32X4 ymm0{k7}{z}, ymm2, yword [rax], 0x55               }
testcase        {  0x62, 0xd3, 0x6d, 0x48, 0x23, 0xc7, 0x55                                  }, {        {evex} VSHUFF32X4 zmm0, zmm2, zmm15, 0x55                            }
testcase        {  0x62, 0xf3, 0x6d, 0x48, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF32X4 zmm0, zmm2, zword [rax], 0x55                      }
testcase        {  0x62, 0xd3, 0x6d, 0x4f, 0x23, 0xc7, 0x55                                  }, {        {evex} VSHUFF32X4 zmm0{k7}, zmm2, zmm15, 0x55                        }
testcase        {  0x62, 0xf3, 0x6d, 0x4f, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF32X4 zmm0{k7}, zmm2, zword [rax], 0x55                  }
testcase        {  0x62, 0xd3, 0x6d, 0xcf, 0x23, 0xc7, 0x55                                  }, {        {evex} VSHUFF32X4 zmm0{k7}{z}, zmm2, zmm15, 0x55                     }
testcase        {  0x62, 0xf3, 0x6d, 0xcf, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF32X4 zmm0{k7}{z}, zmm2, zword [rax], 0x55               }
testcase        {  0x62, 0xd3, 0xed, 0x28, 0x23, 0xc7, 0x55                                  }, {        {evex} VSHUFF64X2 ymm0, ymm2, ymm15, 0x55                            }
testcase        {  0x62, 0xd3, 0xed, 0x2f, 0x23, 0xc7, 0x55                                  }, {        {evex} VSHUFF64X2 ymm0{k7}, ymm2, ymm15, 0x55                        }
testcase        {  0x62, 0xf3, 0xed, 0x2f, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF64X2 ymm0{k7}, ymm2, yword [rax], 0x55                  }
testcase        {  0x62, 0xf3, 0xed, 0x3f, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF64X2 ymm0{k7}, ymm2, qword [rax]{1to4}, 0x55            }
testcase        {  0x62, 0xf3, 0xed, 0x28, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF64X2 ymm0, ymm2, yword [rax], 0x55                      }
testcase        {  0x62, 0xd3, 0xed, 0xaf, 0x23, 0xc7, 0x55                                  }, {        {evex} VSHUFF64X2 ymm0{k7}{z}, ymm2, ymm15, 0x55                     }
testcase        {  0x62, 0xf3, 0xed, 0xaf, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF64X2 ymm0{k7}{z}, ymm2, yword [rax], 0x55               }
testcase        {  0x62, 0xf3, 0xed, 0xbf, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF64X2 ymm0{k7}{z}, ymm2, qword [rax]{1to4}, 0x55         }
testcase        {  0x62, 0xd3, 0xed, 0x48, 0x23, 0xc7, 0x55                                  }, {        {evex} VSHUFF64X2 zmm0, zmm2, zmm15, 0x55                            }
testcase        {  0x62, 0xf3, 0xed, 0x48, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF64X2 zmm0, zmm2, zword [rax], 0x55                      }
testcase        {  0x62, 0xf3, 0xed, 0x58, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF64X2 zmm0, zmm2, qword [rax]{1to8}, 0x55                }
testcase        {  0x62, 0xf3, 0xed, 0x38, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF64X2 ymm0, ymm2, qword [rax]{1to4}, 0x55                }
testcase        {  0x62, 0xd3, 0xed, 0x4f, 0x23, 0xc7, 0x55                                  }, {        {evex} VSHUFF64X2 zmm0{k7}, zmm2, zmm15, 0x55                        }
testcase        {  0x62, 0xf3, 0xed, 0x4f, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF64X2 zmm0{k7}, zmm2, zword [rax], 0x55                  }
testcase        {  0x62, 0xf3, 0xed, 0x5f, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF64X2 zmm0{k7}, zmm2, qword [rax]{1to8}, 0x55            }
testcase        {  0x62, 0xd3, 0xed, 0xcf, 0x23, 0xc7, 0x55                                  }, {        {evex} VSHUFF64X2 zmm0{k7}{z}, zmm2, zmm15, 0x55                     }
testcase        {  0x62, 0xf3, 0xed, 0xcf, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF64X2 zmm0{k7}{z}, zmm2, zword [rax], 0x55               }
testcase        {  0x62, 0xf3, 0xed, 0xdf, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF64X2 zmm0{k7}{z}, zmm2, qword [rax]{1to8}, 0x55         }
testcase        {  0x62, 0xd3, 0xed, 0x28, 0x23, 0xc7, 0x55                                  }, {        {evex} VSHUFF64X2 ymm0, ymm2, ymm15, 0x55                            }
testcase        {  0x62, 0xd3, 0xed, 0x2f, 0x23, 0xc7, 0x55                                  }, {        {evex} VSHUFF64X2 ymm0{k7}, ymm2, ymm15, 0x55                        }
testcase        {  0x62, 0xf3, 0xed, 0x2f, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF64X2 ymm0{k7}, ymm2, yword [rax], 0x55                  }
testcase        {  0x62, 0xf3, 0xed, 0x3f, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF64X2 ymm0{k7}, ymm2, qword [rax]{1to4}, 0x55            }
testcase        {  0x62, 0xf3, 0xed, 0x28, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF64X2 ymm0, ymm2, yword [rax], 0x55                      }
testcase        {  0x62, 0xd3, 0xed, 0xaf, 0x23, 0xc7, 0x55                                  }, {        {evex} VSHUFF64X2 ymm0{k7}{z}, ymm2, ymm15, 0x55                     }
testcase        {  0x62, 0xf3, 0xed, 0xaf, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF64X2 ymm0{k7}{z}, ymm2, yword [rax], 0x55               }
testcase        {  0x62, 0xf3, 0xed, 0xbf, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF64X2 ymm0{k7}{z}, ymm2, qword [rax]{1to4}, 0x55         }
testcase        {  0x62, 0xd3, 0xed, 0x48, 0x23, 0xc7, 0x55                                  }, {        {evex} VSHUFF64X2 zmm0, zmm2, zmm15, 0x55                            }
testcase        {  0x62, 0xf3, 0xed, 0x48, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF64X2 zmm0, zmm2, zword [rax], 0x55                      }
testcase        {  0x62, 0xf3, 0xed, 0x58, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF64X2 zmm0, zmm2, qword [rax]{1to8}, 0x55                }
testcase        {  0x62, 0xf3, 0xed, 0x38, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF64X2 ymm0, ymm2, qword [rax]{1to4}, 0x55                }
testcase        {  0x62, 0xd3, 0xed, 0x4f, 0x23, 0xc7, 0x55                                  }, {        {evex} VSHUFF64X2 zmm0{k7}, zmm2, zmm15, 0x55                        }
testcase        {  0x62, 0xf3, 0xed, 0x4f, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF64X2 zmm0{k7}, zmm2, zword [rax], 0x55                  }
testcase        {  0x62, 0xf3, 0xed, 0x5f, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF64X2 zmm0{k7}, zmm2, qword [rax]{1to8}, 0x55            }
testcase        {  0x62, 0xd3, 0xed, 0xcf, 0x23, 0xc7, 0x55                                  }, {        {evex} VSHUFF64X2 zmm0{k7}{z}, zmm2, zmm15, 0x55                     }
testcase        {  0x62, 0xf3, 0xed, 0xcf, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF64X2 zmm0{k7}{z}, zmm2, zword [rax], 0x55               }
testcase        {  0x62, 0xf3, 0xed, 0xdf, 0x23, 0x00, 0x55                                  }, {        {evex} VSHUFF64X2 zmm0{k7}{z}, zmm2, qword [rax]{1to8}, 0x55         }
testcase        {  0x62, 0xd3, 0x6d, 0x28, 0x43, 0xc7, 0x55                                  }, {        {evex} VSHUFI32X4 ymm0, ymm2, ymm15, 0x55                            }
testcase        {  0x62, 0xd3, 0x6d, 0x2f, 0x43, 0xc7, 0x55                                  }, {        {evex} VSHUFI32X4 ymm0{k7}, ymm2, ymm15, 0x55                        }
testcase        {  0x62, 0xf3, 0x6d, 0x2f, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI32X4 ymm0{k7}, ymm2, yword [rax], 0x55                  }
testcase        {  0x62, 0xf3, 0x6d, 0x28, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI32X4 ymm0, ymm2, yword [rax], 0x55                      }
testcase        {  0x62, 0xd3, 0x6d, 0xaf, 0x43, 0xc7, 0x55                                  }, {        {evex} VSHUFI32X4 ymm0{k7}{z}, ymm2, ymm15, 0x55                     }
testcase        {  0x62, 0xf3, 0x6d, 0xaf, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI32X4 ymm0{k7}{z}, ymm2, yword [rax], 0x55               }
testcase        {  0x62, 0xd3, 0x6d, 0x48, 0x43, 0xc7, 0x55                                  }, {        {evex} VSHUFI32X4 zmm0, zmm2, zmm15, 0x55                            }
testcase        {  0x62, 0xf3, 0x6d, 0x48, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI32X4 zmm0, zmm2, zword [rax], 0x55                      }
testcase        {  0x62, 0xd3, 0x6d, 0x4f, 0x43, 0xc7, 0x55                                  }, {        {evex} VSHUFI32X4 zmm0{k7}, zmm2, zmm15, 0x55                        }
testcase        {  0x62, 0xf3, 0x6d, 0x4f, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI32X4 zmm0{k7}, zmm2, zword [rax], 0x55                  }
testcase        {  0x62, 0xd3, 0x6d, 0xcf, 0x43, 0xc7, 0x55                                  }, {        {evex} VSHUFI32X4 zmm0{k7}{z}, zmm2, zmm15, 0x55                     }
testcase        {  0x62, 0xf3, 0x6d, 0xcf, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI32X4 zmm0{k7}{z}, zmm2, zword [rax], 0x55               }
testcase        {  0x62, 0xd3, 0x6d, 0x28, 0x43, 0xc7, 0x55                                  }, {        {evex} VSHUFI32X4 ymm0, ymm2, ymm15, 0x55                            }
testcase        {  0x62, 0xd3, 0x6d, 0x2f, 0x43, 0xc7, 0x55                                  }, {        {evex} VSHUFI32X4 ymm0{k7}, ymm2, ymm15, 0x55                        }
testcase        {  0x62, 0xf3, 0x6d, 0x2f, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI32X4 ymm0{k7}, ymm2, yword [rax], 0x55                  }
testcase        {  0x62, 0xf3, 0x6d, 0x28, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI32X4 ymm0, ymm2, yword [rax], 0x55                      }
testcase        {  0x62, 0xd3, 0x6d, 0xaf, 0x43, 0xc7, 0x55                                  }, {        {evex} VSHUFI32X4 ymm0{k7}{z}, ymm2, ymm15, 0x55                     }
testcase        {  0x62, 0xf3, 0x6d, 0xaf, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI32X4 ymm0{k7}{z}, ymm2, yword [rax], 0x55               }
testcase        {  0x62, 0xd3, 0x6d, 0x48, 0x43, 0xc7, 0x55                                  }, {        {evex} VSHUFI32X4 zmm0, zmm2, zmm15, 0x55                            }
testcase        {  0x62, 0xf3, 0x6d, 0x48, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI32X4 zmm0, zmm2, zword [rax], 0x55                      }
testcase        {  0x62, 0xd3, 0x6d, 0x4f, 0x43, 0xc7, 0x55                                  }, {        {evex} VSHUFI32X4 zmm0{k7}, zmm2, zmm15, 0x55                        }
testcase        {  0x62, 0xf3, 0x6d, 0x4f, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI32X4 zmm0{k7}, zmm2, zword [rax], 0x55                  }
testcase        {  0x62, 0xd3, 0x6d, 0xcf, 0x43, 0xc7, 0x55                                  }, {        {evex} VSHUFI32X4 zmm0{k7}{z}, zmm2, zmm15, 0x55                     }
testcase        {  0x62, 0xf3, 0x6d, 0xcf, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI32X4 zmm0{k7}{z}, zmm2, zword [rax], 0x55               }
testcase        {  0x62, 0xd3, 0xed, 0x28, 0x43, 0xc7, 0x55                                  }, {        {evex} VSHUFI64X2 ymm0, ymm2, ymm15, 0x55                            }
testcase        {  0x62, 0xd3, 0xed, 0x2f, 0x43, 0xc7, 0x55                                  }, {        {evex} VSHUFI64X2 ymm0{k7}, ymm2, ymm15, 0x55                        }
testcase        {  0x62, 0xf3, 0xed, 0x2f, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI64X2 ymm0{k7}, ymm2, yword [rax], 0x55                  }
testcase        {  0x62, 0xf3, 0xed, 0x3f, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI64X2 ymm0{k7}, ymm2, qword [rax]{1to4}, 0x55            }
testcase        {  0x62, 0xf3, 0xed, 0x28, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI64X2 ymm0, ymm2, yword [rax], 0x55                      }
testcase        {  0x62, 0xd3, 0xed, 0xaf, 0x43, 0xc7, 0x55                                  }, {        {evex} VSHUFI64X2 ymm0{k7}{z}, ymm2, ymm15, 0x55                     }
testcase        {  0x62, 0xf3, 0xed, 0xaf, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI64X2 ymm0{k7}{z}, ymm2, yword [rax], 0x55               }
testcase        {  0x62, 0xf3, 0xed, 0xbf, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI64X2 ymm0{k7}{z}, ymm2, qword [rax]{1to4}, 0x55         }
testcase        {  0x62, 0xd3, 0xed, 0x48, 0x43, 0xc7, 0x55                                  }, {        {evex} VSHUFI64X2 zmm0, zmm2, zmm15, 0x55                            }
testcase        {  0x62, 0xf3, 0xed, 0x48, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI64X2 zmm0, zmm2, zword [rax], 0x55                      }
testcase        {  0x62, 0xf3, 0xed, 0x58, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI64X2 zmm0, zmm2, qword [rax]{1to8}, 0x55                }
testcase        {  0x62, 0xf3, 0xed, 0x38, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI64X2 ymm0, ymm2, qword [rax]{1to4}, 0x55                }
testcase        {  0x62, 0xd3, 0xed, 0x4f, 0x43, 0xc7, 0x55                                  }, {        {evex} VSHUFI64X2 zmm0{k7}, zmm2, zmm15, 0x55                        }
testcase        {  0x62, 0xf3, 0xed, 0x4f, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI64X2 zmm0{k7}, zmm2, zword [rax], 0x55                  }
testcase        {  0x62, 0xf3, 0xed, 0x5f, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI64X2 zmm0{k7}, zmm2, qword [rax]{1to8}, 0x55            }
testcase        {  0x62, 0xd3, 0xed, 0xcf, 0x43, 0xc7, 0x55                                  }, {        {evex} VSHUFI64X2 zmm0{k7}{z}, zmm2, zmm15, 0x55                     }
testcase        {  0x62, 0xf3, 0xed, 0xcf, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI64X2 zmm0{k7}{z}, zmm2, zword [rax], 0x55               }
testcase        {  0x62, 0xf3, 0xed, 0xdf, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI64X2 zmm0{k7}{z}, zmm2, qword [rax]{1to8}, 0x55         }
testcase        {  0x62, 0xd3, 0xed, 0x28, 0x43, 0xc7, 0x55                                  }, {        {evex} VSHUFI64X2 ymm0, ymm2, ymm15, 0x55                            }
testcase        {  0x62, 0xd3, 0xed, 0x2f, 0x43, 0xc7, 0x55                                  }, {        {evex} VSHUFI64X2 ymm0{k7}, ymm2, ymm15, 0x55                        }
testcase        {  0x62, 0xf3, 0xed, 0x2f, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI64X2 ymm0{k7}, ymm2, yword [rax], 0x55                  }
testcase        {  0x62, 0xf3, 0xed, 0x3f, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI64X2 ymm0{k7}, ymm2, qword [rax]{1to4}, 0x55            }
testcase        {  0x62, 0xf3, 0xed, 0x28, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI64X2 ymm0, ymm2, yword [rax], 0x55                      }
testcase        {  0x62, 0xd3, 0xed, 0xaf, 0x43, 0xc7, 0x55                                  }, {        {evex} VSHUFI64X2 ymm0{k7}{z}, ymm2, ymm15, 0x55                     }
testcase        {  0x62, 0xf3, 0xed, 0xaf, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI64X2 ymm0{k7}{z}, ymm2, yword [rax], 0x55               }
testcase        {  0x62, 0xf3, 0xed, 0xbf, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI64X2 ymm0{k7}{z}, ymm2, qword [rax]{1to4}, 0x55         }
testcase        {  0x62, 0xd3, 0xed, 0x48, 0x43, 0xc7, 0x55                                  }, {        {evex} VSHUFI64X2 zmm0, zmm2, zmm15, 0x55                            }
testcase        {  0x62, 0xf3, 0xed, 0x48, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI64X2 zmm0, zmm2, zword [rax], 0x55                      }
testcase        {  0x62, 0xf3, 0xed, 0x58, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI64X2 zmm0, zmm2, qword [rax]{1to8}, 0x55                }
testcase        {  0x62, 0xf3, 0xed, 0x38, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI64X2 ymm0, ymm2, qword [rax]{1to4}, 0x55                }
testcase        {  0x62, 0xd3, 0xed, 0x4f, 0x43, 0xc7, 0x55                                  }, {        {evex} VSHUFI64X2 zmm0{k7}, zmm2, zmm15, 0x55                        }
testcase        {  0x62, 0xf3, 0xed, 0x4f, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI64X2 zmm0{k7}, zmm2, zword [rax], 0x55                  }
testcase        {  0x62, 0xf3, 0xed, 0x5f, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI64X2 zmm0{k7}, zmm2, qword [rax]{1to8}, 0x55            }
testcase        {  0x62, 0xd3, 0xed, 0xcf, 0x43, 0xc7, 0x55                                  }, {        {evex} VSHUFI64X2 zmm0{k7}{z}, zmm2, zmm15, 0x55                     }
testcase        {  0x62, 0xf3, 0xed, 0xcf, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI64X2 zmm0{k7}{z}, zmm2, zword [rax], 0x55               }
testcase        {  0x62, 0xf3, 0xed, 0xdf, 0x43, 0x00, 0x55                                  }, {        {evex} VSHUFI64X2 zmm0{k7}{z}, zmm2, qword [rax]{1to8}, 0x55         }
