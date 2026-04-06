; This file is part of the include test.
; See inctest.asm for build instructions.

message:  db 'hello, world',13,10,'$'

%include "inc2.asm"
