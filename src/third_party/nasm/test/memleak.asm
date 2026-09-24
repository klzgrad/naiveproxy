        %imacro call 1.nolist
%push
 %assign %$size 2
 %assign %$isreg 0
 %assign %$exit 0
 %rep 2
  %ifn %$exit
   %if %$size == 2
    %define %$regnames "ax  cx  dx  bx  sp  bp  si  di  "
   %elif %$size == 4
    %define %$regnames "eax ecx edx ebx esp ebp esi edi "
   %endif
   %assign %$index 0
   %rep 8
    %ifn %$exit
     %substr %$reg %$regnames %$index * 4 + 1, 4
     %deftok %$reg %$reg
     %ifnempty %$reg
      %ifidni %$reg, %1
       %assign %$isreg %$size
       %assign %$exit 1
       %exitrep
      %endif
     %endif
    %endif
    %assign %$index %$index + 1
   %endrep
   %if %$exit
    %exitrep
   %endif
   %assign %$size %$size * 2
  %endif
 %endrep

 %assign %$ismulti 0
 %assign %$ismem 0
 %defstr %$string %1
 %strlen %$length %$string
 %assign %$ii 0
 %rep %$length
  %substr %$point %$string %$ii + 1, 1
  %if %$point == 32 || %$point == 9
   %assign %$ismulti 1
  %endif
  %ifidn %$point, '['
   %assign %$ismem 1
  %endif
  %assign %$ii %$ii + 1
 %endrep
%pop
        %endmacro


%ifndef _OUTER
 %assign _OUTER 100
%endif
%ifndef _INNER
 %assign _INNER 50
%endif

%rep _OUTER
call cx
call near [0]
call .
 %rep _INNER
call label
call labelfoo
call labelbar
call labelbaz
 %endrep
%endrep
