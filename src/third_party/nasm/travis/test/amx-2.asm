;Testname=amx; Arguments=-fbin -oamx.bin -O0 -DSRC; Files=stdout stderr amx.bin

%macro testcase 2
 %ifdef BIN
  db %1
 %endif
 %ifdef SRC
  %2
 %endif
%endmacro


bits 64

testcase    { 0xc4, 0xe2, 0x7b, 0x5c, 0xd1                                                 }, { {vex} TDPFP16PS  tmm2, tmm1, tmm0                                            }
