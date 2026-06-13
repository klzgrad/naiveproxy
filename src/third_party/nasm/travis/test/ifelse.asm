;Testname=ifelse; Arguments=-fbin -oifelse.bin; Files=stdout stderr ifelse.bin

;No problems -> db 3
%if 0
 db 0
%elif 0 > 0
 db 1
%elif 1 < 1
 db 2
%else
 db 3
%endif

;Garbage after else, elif after else -> db 5
%if 0
  db 4
%else trailing garbage
  db 5
%elif 1
  db 6
%endif

;Garbage after endif ->
%if 0
  db 7
%endif trailing garbage

;else after else -> db 9
%if 0
  db 8
%else
  db 9
%else
  db 10
%endif

;Problem preprocessed out, no warning ->
%if 0
  %if 1
    db 11
  %else
    db 12
  %else
    db 13
  %endif
%endif
