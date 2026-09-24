;Testname=test; Arguments=-fbin -obr890790.bin; Files=stdout stderr br890790.bin
%rep 5
  db 0
  %include "br890790_i.asm"
%endrep

db 1
