%pragma list options -befms
%ifndef ERR
 %define ERR 0
%endif
%macro now 1+.nolist
 %if ERR || !(__BITS__ & 0x10)
	%1
 %endif
%endmacro
%macro nod 1+.nolist
 %if ERR || !(__BITS__ & 0x20)
	%1
 %endif
%endmacro
%macro nowd 1+.nolist
 %if ERR || !(__BITS__ & 0x30)
	%1
 %endif
%endmacro
%macro noq 1+.nolist
 %if ERR || !(__BITS__ & 0x40)
	%1
 %endif
%endmacro
%macro nowq 1+.nolist
 %if ERR || !(__BITS__ & 0x50)
	%1
 %endif
%endmacro
%macro nodq 1+.nolist
 %if ERR || !(__BITS__ & 0x60)
	%1
 %endif
%endmacro
%macro bogus 1+.nolist
 %if ERR
	%1
 %endif
%endmacro

	section text1
top:
	times 128 nop

here_jmp:
        jmp $
        jmp short $
        jmp near $
  noq   o16 jmp $
  noq   o16 jmp short $
  noq   o16 jmp near $
  noq   o32 jmp $
  noq   o32 jmp short $
  noq   o32 jmp near $
  nowd  o64 jmp $
  nowd  o64 jmp short $
  nowd  o64 jmp near $
  bogus jmp byte $
  bogus jmp byte short $
  bogus jmp byte near $
  bogus o16 jmp byte $
  bogus o16 jmp byte short $
  bogus o16 jmp byte near $
  bogus o32 jmp byte $
  bogus o32 jmp byte short $
  bogus o32 jmp byte near $
  bogus o64 jmp byte $
  bogus o64 jmp byte short $
  bogus o64 jmp byte near $
  noq   jmp word $
  noq   jmp word short $
  noq   jmp word near $
  noq   o16 jmp word $
  noq   o16 jmp word short $
  noq   o16 jmp word near $
  bogus o32 jmp word $
  bogus o32 jmp word short $
  bogus o32 jmp word near $
  bogus o64 jmp word $
  bogus o64 jmp word short $
  bogus o64 jmp word near $
  noq   jmp dword $
  noq   jmp dword short $
  noq   jmp dword near $
  bogus o16 jmp dword $
  bogus o16 jmp dword short $
  bogus o16 jmp dword near $
  noq   o32 jmp dword $
  noq   o32 jmp dword short $
  noq   o32 jmp dword near $
  bogus o64 jmp dword $
  bogus o64 jmp dword short $
  bogus o64 jmp dword near $
  nowd  jmp qword $
  nowd  jmp qword short $
  nowd  jmp qword near $
  bogus o16 jmp qword $
  bogus o16 jmp qword short $
  bogus o16 jmp qword near $
  bogus o32 jmp qword $
  bogus o32 jmp qword short $
  bogus o32 jmp qword near $
  nowd  o64 jmp qword $
  nowd  o64 jmp qword short $
  nowd  o64 jmp qword near $
        jmp strict $
        jmp strict short $
        jmp strict near $
  noq   o16 jmp strict $
  noq   o16 jmp strict short $
  noq   o16 jmp strict near $
  noq   o32 jmp strict $
  noq   o32 jmp strict short $
  noq   o32 jmp strict near $
  nowd  o64 jmp strict $
  nowd  o64 jmp strict short $
  nowd  o64 jmp strict near $
  bogus jmp strict byte $
  bogus jmp strict byte short $
  bogus jmp strict byte near $
  bogus o16 jmp strict byte $
  bogus o16 jmp strict byte short $
  bogus o16 jmp strict byte near $
  bogus o32 jmp strict byte $
  bogus o32 jmp strict byte short $
  bogus o32 jmp strict byte near $
  bogus o64 jmp strict byte $
  bogus o64 jmp strict byte short $
  bogus o64 jmp strict byte near $
  noq   jmp strict word $
  noq   jmp strict word short $
  noq   jmp strict word near $
  noq   o16 jmp strict word $
  noq   o16 jmp strict word short $
  noq   o16 jmp strict word near $
  bogus o32 jmp strict word $
  bogus o32 jmp strict word short $
  bogus o32 jmp strict word near $
  bogus o64 jmp strict word $
  bogus o64 jmp strict word short $
  bogus o64 jmp strict word near $
  noq   jmp strict dword $
  noq   jmp strict dword short $
  noq   jmp strict dword near $
  bogus o16 jmp strict dword $
  bogus o16 jmp strict dword short $
  bogus o16 jmp strict dword near $
  noq   o32 jmp strict dword $
  noq   o32 jmp strict dword short $
  noq   o32 jmp strict dword near $
  bogus o64 jmp strict dword $
  bogus o64 jmp strict dword short $
  bogus o64 jmp strict dword near $
  nowd  jmp strict qword $
  nowd  jmp strict qword short $
  nowd  jmp strict qword near $
  bogus o16 jmp strict qword $
  bogus o16 jmp strict qword short $
  bogus o16 jmp strict qword near $
  bogus o32 jmp strict qword $
  bogus o32 jmp strict qword short $
  bogus o32 jmp strict qword near $
  nowd  o64 jmp strict qword $
  nowd  o64 jmp strict qword short $
  nowd  o64 jmp strict qword near $
        jmp top
  bogus jmp short top
        jmp near top
  noq   o16 jmp top
  bogus o16 jmp short top
  noq   o16 jmp near top
  noq   o32 jmp top
  bogus o32 jmp short top
  noq   o32 jmp near top
  nowd  o64 jmp top
  bogus o64 jmp short top
  nowd  o64 jmp near top
  bogus jmp byte top
  bogus jmp byte short top
  bogus jmp byte near top
  bogus o16 jmp byte top
  bogus o16 jmp byte short top
  bogus o16 jmp byte near top
  bogus o32 jmp byte top
  bogus o32 jmp byte short top
  bogus o32 jmp byte near top
  bogus o64 jmp byte top
  bogus o64 jmp byte short top
  bogus o64 jmp byte near top
  noq   jmp word top
  bogus jmp word short top
  noq   jmp word near top
  noq   o16 jmp word top
  bogus o16 jmp word short top
  noq   o16 jmp word near top
  bogus o32 jmp word top
  bogus o32 jmp word short top
  bogus o32 jmp word near top
  bogus o64 jmp word top
  bogus o64 jmp word short top
  bogus o64 jmp word near top
  noq   jmp dword top
  bogus jmp dword short top
  noq   jmp dword near top
  bogus o16 jmp dword top
  bogus o16 jmp dword short top
  bogus o16 jmp dword near top
  noq   o32 jmp dword top
  bogus o32 jmp dword short top
  noq   o32 jmp dword near top
  bogus o64 jmp dword top
  bogus o64 jmp dword short top
  bogus o64 jmp dword near top
  nowd  jmp qword top
  bogus jmp qword short top
  nowd  jmp qword near top
  bogus o16 jmp qword top
  bogus o16 jmp qword short top
  bogus o16 jmp qword near top
  bogus o32 jmp qword top
  bogus o32 jmp qword short top
  bogus o32 jmp qword near top
  nowd  o64 jmp qword top
  bogus o64 jmp qword short top
  nowd  o64 jmp qword near top
        jmp strict top
  bogus jmp strict short top
        jmp strict near top
  noq   o16 jmp strict top
  bogus o16 jmp strict short top
  noq   o16 jmp strict near top
  noq   o32 jmp strict top
  bogus o32 jmp strict short top
  noq   o32 jmp strict near top
  nowd  o64 jmp strict top
  bogus o64 jmp strict short top
  nowd  o64 jmp strict near top
  bogus jmp strict byte top
  bogus jmp strict byte short top
  bogus jmp strict byte near top
  bogus o16 jmp strict byte top
  bogus o16 jmp strict byte short top
  bogus o16 jmp strict byte near top
  bogus o32 jmp strict byte top
  bogus o32 jmp strict byte short top
  bogus o32 jmp strict byte near top
  bogus o64 jmp strict byte top
  bogus o64 jmp strict byte short top
  bogus o64 jmp strict byte near top
  noq   jmp strict word top
  bogus jmp strict word short top
  noq   jmp strict word near top
  noq   o16 jmp strict word top
  bogus o16 jmp strict word short top
  noq   o16 jmp strict word near top
  bogus o32 jmp strict word top
  bogus o32 jmp strict word short top
  bogus o32 jmp strict word near top
  bogus o64 jmp strict word top
  bogus o64 jmp strict word short top
  bogus o64 jmp strict word near top
  noq   jmp strict dword top
  bogus jmp strict dword short top
  noq   jmp strict dword near top
  bogus o16 jmp strict dword top
  bogus o16 jmp strict dword short top
  bogus o16 jmp strict dword near top
  noq   o32 jmp strict dword top
  bogus o32 jmp strict dword short top
  noq   o32 jmp strict dword near top
  bogus o64 jmp strict dword top
  bogus o64 jmp strict dword short top
  bogus o64 jmp strict dword near top
  nowd  jmp strict qword top
  bogus jmp strict qword short top
  nowd  jmp strict qword near top
  bogus o16 jmp strict qword top
  bogus o16 jmp strict qword short top
  bogus o16 jmp strict qword near top
  bogus o32 jmp strict qword top
  bogus o32 jmp strict qword short top
  bogus o32 jmp strict qword near top
  nowd  o64 jmp strict qword top
  bogus o64 jmp strict qword short top
  nowd  o64 jmp strict qword near top
        jmp there
  bogus jmp short there
        jmp near there
  noq   o16 jmp there
  bogus o16 jmp short there
  noq   o16 jmp near there
  noq   o32 jmp there
  bogus o32 jmp short there
  noq   o32 jmp near there
  nowd  o64 jmp there
  bogus o64 jmp short there
  nowd  o64 jmp near there
  bogus jmp byte there
  bogus jmp byte short there
  bogus jmp byte near there
  bogus o16 jmp byte there
  bogus o16 jmp byte short there
  bogus o16 jmp byte near there
  bogus o32 jmp byte there
  bogus o32 jmp byte short there
  bogus o32 jmp byte near there
  bogus o64 jmp byte there
  bogus o64 jmp byte short there
  bogus o64 jmp byte near there
  noq   jmp word there
  bogus jmp word short there
  noq   jmp word near there
  noq   o16 jmp word there
  bogus o16 jmp word short there
  noq   o16 jmp word near there
  bogus o32 jmp word there
  bogus o32 jmp word short there
  bogus o32 jmp word near there
  bogus o64 jmp word there
  bogus o64 jmp word short there
  bogus o64 jmp word near there
  noq   jmp dword there
  bogus jmp dword short there
  noq   jmp dword near there
  bogus o16 jmp dword there
  bogus o16 jmp dword short there
  bogus o16 jmp dword near there
  noq   o32 jmp dword there
  bogus o32 jmp dword short there
  noq   o32 jmp dword near there
  bogus o64 jmp dword there
  bogus o64 jmp dword short there
  bogus o64 jmp dword near there
  nowd  jmp qword there
  bogus jmp qword short there
  nowd  jmp qword near there
  bogus o16 jmp qword there
  bogus o16 jmp qword short there
  bogus o16 jmp qword near there
  bogus o32 jmp qword there
  bogus o32 jmp qword short there
  bogus o32 jmp qword near there
  nowd  o64 jmp qword there
  bogus o64 jmp qword short there
  nowd  o64 jmp qword near there
        jmp strict there
  bogus jmp strict short there
        jmp strict near there
  noq   o16 jmp strict there
  bogus o16 jmp strict short there
  noq   o16 jmp strict near there
  noq   o32 jmp strict there
  bogus o32 jmp strict short there
  noq   o32 jmp strict near there
  nowd  o64 jmp strict there
  bogus o64 jmp strict short there
  nowd  o64 jmp strict near there
  bogus jmp strict byte there
  bogus jmp strict byte short there
  bogus jmp strict byte near there
  bogus o16 jmp strict byte there
  bogus o16 jmp strict byte short there
  bogus o16 jmp strict byte near there
  bogus o32 jmp strict byte there
  bogus o32 jmp strict byte short there
  bogus o32 jmp strict byte near there
  bogus o64 jmp strict byte there
  bogus o64 jmp strict byte short there
  bogus o64 jmp strict byte near there
  noq   jmp strict word there
  bogus jmp strict word short there
  noq   jmp strict word near there
  noq   o16 jmp strict word there
  bogus o16 jmp strict word short there
  noq   o16 jmp strict word near there
  bogus o32 jmp strict word there
  bogus o32 jmp strict word short there
  bogus o32 jmp strict word near there
  bogus o64 jmp strict word there
  bogus o64 jmp strict word short there
  bogus o64 jmp strict word near there
  noq   jmp strict dword there
  bogus jmp strict dword short there
  noq   jmp strict dword near there
  bogus o16 jmp strict dword there
  bogus o16 jmp strict dword short there
  bogus o16 jmp strict dword near there
  noq   o32 jmp strict dword there
  bogus o32 jmp strict dword short there
  noq   o32 jmp strict dword near there
  bogus o64 jmp strict dword there
  bogus o64 jmp strict dword short there
  bogus o64 jmp strict dword near there
  nowd  jmp strict qword there
  bogus jmp strict qword short there
  nowd  jmp strict qword near there
  bogus o16 jmp strict qword there
  bogus o16 jmp strict qword short there
  bogus o16 jmp strict qword near there
  bogus o32 jmp strict qword there
  bogus o32 jmp strict qword short there
  bogus o32 jmp strict qword near there
  nowd  o64 jmp strict qword there
  bogus o64 jmp strict qword short there
  nowd  o64 jmp strict qword near there
here_call:
        call $
  bogus call short $
        call near $
  noq   o16 call $
  bogus o16 call short $
  noq   o16 call near $
  noq   o32 call $
  bogus o32 call short $
  noq   o32 call near $
  nowd  o64 call $
  bogus o64 call short $
  nowd  o64 call near $
  bogus call byte $
  bogus call byte short $
  bogus call byte near $
  bogus o16 call byte $
  bogus o16 call byte short $
  bogus o16 call byte near $
  bogus o32 call byte $
  bogus o32 call byte short $
  bogus o32 call byte near $
  bogus o64 call byte $
  bogus o64 call byte short $
  bogus o64 call byte near $
  noq   call word $
  bogus call word short $
  noq   call word near $
  noq   o16 call word $
  bogus o16 call word short $
  noq   o16 call word near $
  bogus o32 call word $
  bogus o32 call word short $
  bogus o32 call word near $
  bogus o64 call word $
  bogus o64 call word short $
  bogus o64 call word near $
  noq   call dword $
  bogus call dword short $
  noq   call dword near $
  bogus o16 call dword $
  bogus o16 call dword short $
  bogus o16 call dword near $
  noq   o32 call dword $
  bogus o32 call dword short $
  noq   o32 call dword near $
  bogus o64 call dword $
  bogus o64 call dword short $
  bogus o64 call dword near $
  nowd  call qword $
  bogus call qword short $
  nowd  call qword near $
  bogus o16 call qword $
  bogus o16 call qword short $
  bogus o16 call qword near $
  bogus o32 call qword $
  bogus o32 call qword short $
  bogus o32 call qword near $
  nowd  o64 call qword $
  bogus o64 call qword short $
  nowd  o64 call qword near $
        call strict $
  bogus call strict short $
        call strict near $
  noq   o16 call strict $
  bogus o16 call strict short $
  noq   o16 call strict near $
  noq   o32 call strict $
  bogus o32 call strict short $
  noq   o32 call strict near $
  nowd  o64 call strict $
  bogus o64 call strict short $
  nowd  o64 call strict near $
  bogus call strict byte $
  bogus call strict byte short $
  bogus call strict byte near $
  bogus o16 call strict byte $
  bogus o16 call strict byte short $
  bogus o16 call strict byte near $
  bogus o32 call strict byte $
  bogus o32 call strict byte short $
  bogus o32 call strict byte near $
  bogus o64 call strict byte $
  bogus o64 call strict byte short $
  bogus o64 call strict byte near $
  noq   call strict word $
  bogus call strict word short $
  noq   call strict word near $
  noq   o16 call strict word $
  bogus o16 call strict word short $
  noq   o16 call strict word near $
  bogus o32 call strict word $
  bogus o32 call strict word short $
  bogus o32 call strict word near $
  bogus o64 call strict word $
  bogus o64 call strict word short $
  bogus o64 call strict word near $
  noq   call strict dword $
  bogus call strict dword short $
  noq   call strict dword near $
  bogus o16 call strict dword $
  bogus o16 call strict dword short $
  bogus o16 call strict dword near $
  noq   o32 call strict dword $
  bogus o32 call strict dword short $
  noq   o32 call strict dword near $
  bogus o64 call strict dword $
  bogus o64 call strict dword short $
  bogus o64 call strict dword near $
  nowd  call strict qword $
  bogus call strict qword short $
  nowd  call strict qword near $
  bogus o16 call strict qword $
  bogus o16 call strict qword short $
  bogus o16 call strict qword near $
  bogus o32 call strict qword $
  bogus o32 call strict qword short $
  bogus o32 call strict qword near $
  nowd  o64 call strict qword $
  bogus o64 call strict qword short $
  nowd  o64 call strict qword near $
        call top
  bogus call short top
        call near top
  noq   o16 call top
  bogus o16 call short top
  noq   o16 call near top
  noq   o32 call top
  bogus o32 call short top
  noq   o32 call near top
  nowd  o64 call top
  bogus o64 call short top
  nowd  o64 call near top
  bogus call byte top
  bogus call byte short top
  bogus call byte near top
  bogus o16 call byte top
  bogus o16 call byte short top
  bogus o16 call byte near top
  bogus o32 call byte top
  bogus o32 call byte short top
  bogus o32 call byte near top
  bogus o64 call byte top
  bogus o64 call byte short top
  bogus o64 call byte near top
  noq   call word top
  bogus call word short top
  noq   call word near top
  noq   o16 call word top
  bogus o16 call word short top
  noq   o16 call word near top
  bogus o32 call word top
  bogus o32 call word short top
  bogus o32 call word near top
  bogus o64 call word top
  bogus o64 call word short top
  bogus o64 call word near top
  noq   call dword top
  bogus call dword short top
  noq   call dword near top
  bogus o16 call dword top
  bogus o16 call dword short top
  bogus o16 call dword near top
  noq   o32 call dword top
  bogus o32 call dword short top
  noq   o32 call dword near top
  bogus o64 call dword top
  bogus o64 call dword short top
  bogus o64 call dword near top
  nowd  call qword top
  bogus call qword short top
  nowd  call qword near top
  bogus o16 call qword top
  bogus o16 call qword short top
  bogus o16 call qword near top
  bogus o32 call qword top
  bogus o32 call qword short top
  bogus o32 call qword near top
  nowd  o64 call qword top
  bogus o64 call qword short top
  nowd  o64 call qword near top
        call strict top
  bogus call strict short top
        call strict near top
  noq   o16 call strict top
  bogus o16 call strict short top
  noq   o16 call strict near top
  noq   o32 call strict top
  bogus o32 call strict short top
  noq   o32 call strict near top
  nowd  o64 call strict top
  bogus o64 call strict short top
  nowd  o64 call strict near top
  bogus call strict byte top
  bogus call strict byte short top
  bogus call strict byte near top
  bogus o16 call strict byte top
  bogus o16 call strict byte short top
  bogus o16 call strict byte near top
  bogus o32 call strict byte top
  bogus o32 call strict byte short top
  bogus o32 call strict byte near top
  bogus o64 call strict byte top
  bogus o64 call strict byte short top
  bogus o64 call strict byte near top
  noq   call strict word top
  bogus call strict word short top
  noq   call strict word near top
  noq   o16 call strict word top
  bogus o16 call strict word short top
  noq   o16 call strict word near top
  bogus o32 call strict word top
  bogus o32 call strict word short top
  bogus o32 call strict word near top
  bogus o64 call strict word top
  bogus o64 call strict word short top
  bogus o64 call strict word near top
  noq   call strict dword top
  bogus call strict dword short top
  noq   call strict dword near top
  bogus o16 call strict dword top
  bogus o16 call strict dword short top
  bogus o16 call strict dword near top
  noq   o32 call strict dword top
  bogus o32 call strict dword short top
  noq   o32 call strict dword near top
  bogus o64 call strict dword top
  bogus o64 call strict dword short top
  bogus o64 call strict dword near top
  nowd  call strict qword top
  bogus call strict qword short top
  nowd  call strict qword near top
  bogus o16 call strict qword top
  bogus o16 call strict qword short top
  bogus o16 call strict qword near top
  bogus o32 call strict qword top
  bogus o32 call strict qword short top
  bogus o32 call strict qword near top
  nowd  o64 call strict qword top
  bogus o64 call strict qword short top
  nowd  o64 call strict qword near top
        call there
  bogus call short there
        call near there
  noq   o16 call there
  bogus o16 call short there
  noq   o16 call near there
  noq   o32 call there
  bogus o32 call short there
  noq   o32 call near there
  nowd  o64 call there
  bogus o64 call short there
  nowd  o64 call near there
  bogus call byte there
  bogus call byte short there
  bogus call byte near there
  bogus o16 call byte there
  bogus o16 call byte short there
  bogus o16 call byte near there
  bogus o32 call byte there
  bogus o32 call byte short there
  bogus o32 call byte near there
  bogus o64 call byte there
  bogus o64 call byte short there
  bogus o64 call byte near there
  noq   call word there
  bogus call word short there
  noq   call word near there
  noq   o16 call word there
  bogus o16 call word short there
  noq   o16 call word near there
  bogus o32 call word there
  bogus o32 call word short there
  bogus o32 call word near there
  bogus o64 call word there
  bogus o64 call word short there
  bogus o64 call word near there
  noq   call dword there
  bogus call dword short there
  noq   call dword near there
  bogus o16 call dword there
  bogus o16 call dword short there
  bogus o16 call dword near there
  noq   o32 call dword there
  bogus o32 call dword short there
  noq   o32 call dword near there
  bogus o64 call dword there
  bogus o64 call dword short there
  bogus o64 call dword near there
  nowd  call qword there
  bogus call qword short there
  nowd  call qword near there
  bogus o16 call qword there
  bogus o16 call qword short there
  bogus o16 call qword near there
  bogus o32 call qword there
  bogus o32 call qword short there
  bogus o32 call qword near there
  nowd  o64 call qword there
  bogus o64 call qword short there
  nowd  o64 call qword near there
        call strict there
  bogus call strict short there
        call strict near there
  noq   o16 call strict there
  bogus o16 call strict short there
  noq   o16 call strict near there
  noq   o32 call strict there
  bogus o32 call strict short there
  noq   o32 call strict near there
  nowd  o64 call strict there
  bogus o64 call strict short there
  nowd  o64 call strict near there
  bogus call strict byte there
  bogus call strict byte short there
  bogus call strict byte near there
  bogus o16 call strict byte there
  bogus o16 call strict byte short there
  bogus o16 call strict byte near there
  bogus o32 call strict byte there
  bogus o32 call strict byte short there
  bogus o32 call strict byte near there
  bogus o64 call strict byte there
  bogus o64 call strict byte short there
  bogus o64 call strict byte near there
  noq   call strict word there
  bogus call strict word short there
  noq   call strict word near there
  noq   o16 call strict word there
  bogus o16 call strict word short there
  noq   o16 call strict word near there
  bogus o32 call strict word there
  bogus o32 call strict word short there
  bogus o32 call strict word near there
  bogus o64 call strict word there
  bogus o64 call strict word short there
  bogus o64 call strict word near there
  noq   call strict dword there
  bogus call strict dword short there
  noq   call strict dword near there
  bogus o16 call strict dword there
  bogus o16 call strict dword short there
  bogus o16 call strict dword near there
  noq   o32 call strict dword there
  bogus o32 call strict dword short there
  noq   o32 call strict dword near there
  bogus o64 call strict dword there
  bogus o64 call strict dword short there
  bogus o64 call strict dword near there
  nowd  call strict qword there
  bogus call strict qword short there
  nowd  call strict qword near there
  bogus o16 call strict qword there
  bogus o16 call strict qword short there
  bogus o16 call strict qword near there
  bogus o32 call strict qword there
  bogus o32 call strict qword short there
  bogus o32 call strict qword near there
  nowd  o64 call strict qword there
  bogus o64 call strict qword short there
  nowd  o64 call strict qword near there
here_jz:
        jz $
        jz short $
        jz near $
  noq   o16 jz $
  noq   o16 jz short $
  noq   o16 jz near $
  noq   o32 jz $
  noq   o32 jz short $
  noq   o32 jz near $
  nowd  o64 jz $
  nowd  o64 jz short $
  nowd  o64 jz near $
  bogus jz byte $
  bogus jz byte short $
  bogus jz byte near $
  bogus o16 jz byte $
  bogus o16 jz byte short $
  bogus o16 jz byte near $
  bogus o32 jz byte $
  bogus o32 jz byte short $
  bogus o32 jz byte near $
  bogus o64 jz byte $
  bogus o64 jz byte short $
  bogus o64 jz byte near $
  noq   jz word $
  noq   jz word short $
  noq   jz word near $
  noq   o16 jz word $
  noq   o16 jz word short $
  noq   o16 jz word near $
  bogus o32 jz word $
  bogus o32 jz word short $
  bogus o32 jz word near $
  bogus o64 jz word $
  bogus o64 jz word short $
  bogus o64 jz word near $
  noq   jz dword $
  noq   jz dword short $
  noq   jz dword near $
  bogus o16 jz dword $
  bogus o16 jz dword short $
  bogus o16 jz dword near $
  noq   o32 jz dword $
  noq   o32 jz dword short $
  noq   o32 jz dword near $
  bogus o64 jz dword $
  bogus o64 jz dword short $
  bogus o64 jz dword near $
  nowd  jz qword $
  nowd  jz qword short $
  nowd  jz qword near $
  bogus o16 jz qword $
  bogus o16 jz qword short $
  bogus o16 jz qword near $
  bogus o32 jz qword $
  bogus o32 jz qword short $
  bogus o32 jz qword near $
  nowd  o64 jz qword $
  nowd  o64 jz qword short $
  nowd  o64 jz qword near $
        jz strict $
        jz strict short $
        jz strict near $
  noq   o16 jz strict $
  noq   o16 jz strict short $
  noq   o16 jz strict near $
  noq   o32 jz strict $
  noq   o32 jz strict short $
  noq   o32 jz strict near $
  nowd  o64 jz strict $
  nowd  o64 jz strict short $
  nowd  o64 jz strict near $
  bogus jz strict byte $
  bogus jz strict byte short $
  bogus jz strict byte near $
  bogus o16 jz strict byte $
  bogus o16 jz strict byte short $
  bogus o16 jz strict byte near $
  bogus o32 jz strict byte $
  bogus o32 jz strict byte short $
  bogus o32 jz strict byte near $
  bogus o64 jz strict byte $
  bogus o64 jz strict byte short $
  bogus o64 jz strict byte near $
  noq   jz strict word $
  noq   jz strict word short $
  noq   jz strict word near $
  noq   o16 jz strict word $
  noq   o16 jz strict word short $
  noq   o16 jz strict word near $
  bogus o32 jz strict word $
  bogus o32 jz strict word short $
  bogus o32 jz strict word near $
  bogus o64 jz strict word $
  bogus o64 jz strict word short $
  bogus o64 jz strict word near $
  noq   jz strict dword $
  noq   jz strict dword short $
  noq   jz strict dword near $
  bogus o16 jz strict dword $
  bogus o16 jz strict dword short $
  bogus o16 jz strict dword near $
  noq   o32 jz strict dword $
  noq   o32 jz strict dword short $
  noq   o32 jz strict dword near $
  bogus o64 jz strict dword $
  bogus o64 jz strict dword short $
  bogus o64 jz strict dword near $
  nowd  jz strict qword $
  nowd  jz strict qword short $
  nowd  jz strict qword near $
  bogus o16 jz strict qword $
  bogus o16 jz strict qword short $
  bogus o16 jz strict qword near $
  bogus o32 jz strict qword $
  bogus o32 jz strict qword short $
  bogus o32 jz strict qword near $
  nowd  o64 jz strict qword $
  nowd  o64 jz strict qword short $
  nowd  o64 jz strict qword near $
        jz top
  bogus jz short top
        jz near top
  noq   o16 jz top
  bogus o16 jz short top
  noq   o16 jz near top
  noq   o32 jz top
  bogus o32 jz short top
  noq   o32 jz near top
  nowd  o64 jz top
  bogus o64 jz short top
  nowd  o64 jz near top
  bogus jz byte top
  bogus jz byte short top
  bogus jz byte near top
  bogus o16 jz byte top
  bogus o16 jz byte short top
  bogus o16 jz byte near top
  bogus o32 jz byte top
  bogus o32 jz byte short top
  bogus o32 jz byte near top
  bogus o64 jz byte top
  bogus o64 jz byte short top
  bogus o64 jz byte near top
  noq   jz word top
  bogus jz word short top
  noq   jz word near top
  noq   o16 jz word top
  bogus o16 jz word short top
  noq   o16 jz word near top
  bogus o32 jz word top
  bogus o32 jz word short top
  bogus o32 jz word near top
  bogus o64 jz word top
  bogus o64 jz word short top
  bogus o64 jz word near top
  noq   jz dword top
  bogus jz dword short top
  noq   jz dword near top
  bogus o16 jz dword top
  bogus o16 jz dword short top
  bogus o16 jz dword near top
  noq   o32 jz dword top
  bogus o32 jz dword short top
  noq   o32 jz dword near top
  bogus o64 jz dword top
  bogus o64 jz dword short top
  bogus o64 jz dword near top
  nowd  jz qword top
  bogus jz qword short top
  nowd  jz qword near top
  bogus o16 jz qword top
  bogus o16 jz qword short top
  bogus o16 jz qword near top
  bogus o32 jz qword top
  bogus o32 jz qword short top
  bogus o32 jz qword near top
  nowd  o64 jz qword top
  bogus o64 jz qword short top
  nowd  o64 jz qword near top
        jz strict top
  bogus jz strict short top
        jz strict near top
  noq   o16 jz strict top
  bogus o16 jz strict short top
  noq   o16 jz strict near top
  noq   o32 jz strict top
  bogus o32 jz strict short top
  noq   o32 jz strict near top
  nowd  o64 jz strict top
  bogus o64 jz strict short top
  nowd  o64 jz strict near top
  bogus jz strict byte top
  bogus jz strict byte short top
  bogus jz strict byte near top
  bogus o16 jz strict byte top
  bogus o16 jz strict byte short top
  bogus o16 jz strict byte near top
  bogus o32 jz strict byte top
  bogus o32 jz strict byte short top
  bogus o32 jz strict byte near top
  bogus o64 jz strict byte top
  bogus o64 jz strict byte short top
  bogus o64 jz strict byte near top
  noq   jz strict word top
  bogus jz strict word short top
  noq   jz strict word near top
  noq   o16 jz strict word top
  bogus o16 jz strict word short top
  noq   o16 jz strict word near top
  bogus o32 jz strict word top
  bogus o32 jz strict word short top
  bogus o32 jz strict word near top
  bogus o64 jz strict word top
  bogus o64 jz strict word short top
  bogus o64 jz strict word near top
  noq   jz strict dword top
  bogus jz strict dword short top
  noq   jz strict dword near top
  bogus o16 jz strict dword top
  bogus o16 jz strict dword short top
  bogus o16 jz strict dword near top
  noq   o32 jz strict dword top
  bogus o32 jz strict dword short top
  noq   o32 jz strict dword near top
  bogus o64 jz strict dword top
  bogus o64 jz strict dword short top
  bogus o64 jz strict dword near top
  nowd  jz strict qword top
  bogus jz strict qword short top
  nowd  jz strict qword near top
  bogus o16 jz strict qword top
  bogus o16 jz strict qword short top
  bogus o16 jz strict qword near top
  bogus o32 jz strict qword top
  bogus o32 jz strict qword short top
  bogus o32 jz strict qword near top
  nowd  o64 jz strict qword top
  bogus o64 jz strict qword short top
  nowd  o64 jz strict qword near top
        jz there
  bogus jz short there
        jz near there
  noq   o16 jz there
  bogus o16 jz short there
  noq   o16 jz near there
  noq   o32 jz there
  bogus o32 jz short there
  noq   o32 jz near there
  nowd  o64 jz there
  bogus o64 jz short there
  nowd  o64 jz near there
  bogus jz byte there
  bogus jz byte short there
  bogus jz byte near there
  bogus o16 jz byte there
  bogus o16 jz byte short there
  bogus o16 jz byte near there
  bogus o32 jz byte there
  bogus o32 jz byte short there
  bogus o32 jz byte near there
  bogus o64 jz byte there
  bogus o64 jz byte short there
  bogus o64 jz byte near there
  noq   jz word there
  bogus jz word short there
  noq   jz word near there
  noq   o16 jz word there
  bogus o16 jz word short there
  noq   o16 jz word near there
  bogus o32 jz word there
  bogus o32 jz word short there
  bogus o32 jz word near there
  bogus o64 jz word there
  bogus o64 jz word short there
  bogus o64 jz word near there
  noq   jz dword there
  bogus jz dword short there
  noq   jz dword near there
  bogus o16 jz dword there
  bogus o16 jz dword short there
  bogus o16 jz dword near there
  noq   o32 jz dword there
  bogus o32 jz dword short there
  noq   o32 jz dword near there
  bogus o64 jz dword there
  bogus o64 jz dword short there
  bogus o64 jz dword near there
  nowd  jz qword there
  bogus jz qword short there
  nowd  jz qword near there
  bogus o16 jz qword there
  bogus o16 jz qword short there
  bogus o16 jz qword near there
  bogus o32 jz qword there
  bogus o32 jz qword short there
  bogus o32 jz qword near there
  nowd  o64 jz qword there
  bogus o64 jz qword short there
  nowd  o64 jz qword near there
        jz strict there
  bogus jz strict short there
        jz strict near there
  noq   o16 jz strict there
  bogus o16 jz strict short there
  noq   o16 jz strict near there
  noq   o32 jz strict there
  bogus o32 jz strict short there
  noq   o32 jz strict near there
  nowd  o64 jz strict there
  bogus o64 jz strict short there
  nowd  o64 jz strict near there
  bogus jz strict byte there
  bogus jz strict byte short there
  bogus jz strict byte near there
  bogus o16 jz strict byte there
  bogus o16 jz strict byte short there
  bogus o16 jz strict byte near there
  bogus o32 jz strict byte there
  bogus o32 jz strict byte short there
  bogus o32 jz strict byte near there
  bogus o64 jz strict byte there
  bogus o64 jz strict byte short there
  bogus o64 jz strict byte near there
  noq   jz strict word there
  bogus jz strict word short there
  noq   jz strict word near there
  noq   o16 jz strict word there
  bogus o16 jz strict word short there
  noq   o16 jz strict word near there
  bogus o32 jz strict word there
  bogus o32 jz strict word short there
  bogus o32 jz strict word near there
  bogus o64 jz strict word there
  bogus o64 jz strict word short there
  bogus o64 jz strict word near there
  noq   jz strict dword there
  bogus jz strict dword short there
  noq   jz strict dword near there
  bogus o16 jz strict dword there
  bogus o16 jz strict dword short there
  bogus o16 jz strict dword near there
  noq   o32 jz strict dword there
  bogus o32 jz strict dword short there
  noq   o32 jz strict dword near there
  bogus o64 jz strict dword there
  bogus o64 jz strict dword short there
  bogus o64 jz strict dword near there
  nowd  jz strict qword there
  bogus jz strict qword short there
  nowd  jz strict qword near there
  bogus o16 jz strict qword there
  bogus o16 jz strict qword short there
  bogus o16 jz strict qword near there
  bogus o32 jz strict qword there
  bogus o32 jz strict qword short there
  bogus o32 jz strict qword near there
  nowd  o64 jz strict qword there
  bogus o64 jz strict qword short there
  nowd  o64 jz strict qword near there
here_jcxz:
  noq   jcxz $
  noq   jcxz short $
  bogus jcxz near $
  noq   o16 jcxz $
  noq   o16 jcxz short $
  bogus o16 jcxz near $
  noq   o32 jcxz $
  noq   o32 jcxz short $
  bogus o32 jcxz near $
  bogus o64 jcxz $
  bogus o64 jcxz short $
  bogus o64 jcxz near $
  bogus jcxz byte $
  bogus jcxz byte short $
  bogus jcxz byte near $
  bogus o16 jcxz byte $
  bogus o16 jcxz byte short $
  bogus o16 jcxz byte near $
  bogus o32 jcxz byte $
  bogus o32 jcxz byte short $
  bogus o32 jcxz byte near $
  bogus o64 jcxz byte $
  bogus o64 jcxz byte short $
  bogus o64 jcxz byte near $
  noq   jcxz word $
  noq   jcxz word short $
  bogus jcxz word near $
  noq   o16 jcxz word $
  noq   o16 jcxz word short $
  bogus o16 jcxz word near $
  bogus o32 jcxz word $
  bogus o32 jcxz word short $
  bogus o32 jcxz word near $
  bogus o64 jcxz word $
  bogus o64 jcxz word short $
  bogus o64 jcxz word near $
  noq   jcxz dword $
  noq   jcxz dword short $
  bogus jcxz dword near $
  bogus o16 jcxz dword $
  bogus o16 jcxz dword short $
  bogus o16 jcxz dword near $
  noq   o32 jcxz dword $
  noq   o32 jcxz dword short $
  bogus o32 jcxz dword near $
  bogus o64 jcxz dword $
  bogus o64 jcxz dword short $
  bogus o64 jcxz dword near $
  bogus jcxz qword $
  bogus jcxz qword short $
  bogus jcxz qword near $
  bogus o16 jcxz qword $
  bogus o16 jcxz qword short $
  bogus o16 jcxz qword near $
  bogus o32 jcxz qword $
  bogus o32 jcxz qword short $
  bogus o32 jcxz qword near $
  bogus o64 jcxz qword $
  bogus o64 jcxz qword short $
  bogus o64 jcxz qword near $
  noq   jcxz strict $
  noq   jcxz strict short $
  bogus jcxz strict near $
  noq   o16 jcxz strict $
  noq   o16 jcxz strict short $
  bogus o16 jcxz strict near $
  noq   o32 jcxz strict $
  noq   o32 jcxz strict short $
  bogus o32 jcxz strict near $
  bogus o64 jcxz strict $
  bogus o64 jcxz strict short $
  bogus o64 jcxz strict near $
  bogus jcxz strict byte $
  bogus jcxz strict byte short $
  bogus jcxz strict byte near $
  bogus o16 jcxz strict byte $
  bogus o16 jcxz strict byte short $
  bogus o16 jcxz strict byte near $
  bogus o32 jcxz strict byte $
  bogus o32 jcxz strict byte short $
  bogus o32 jcxz strict byte near $
  bogus o64 jcxz strict byte $
  bogus o64 jcxz strict byte short $
  bogus o64 jcxz strict byte near $
  noq   jcxz strict word $
  noq   jcxz strict word short $
  bogus jcxz strict word near $
  noq   o16 jcxz strict word $
  noq   o16 jcxz strict word short $
  bogus o16 jcxz strict word near $
  bogus o32 jcxz strict word $
  bogus o32 jcxz strict word short $
  bogus o32 jcxz strict word near $
  bogus o64 jcxz strict word $
  bogus o64 jcxz strict word short $
  bogus o64 jcxz strict word near $
  noq   jcxz strict dword $
  noq   jcxz strict dword short $
  bogus jcxz strict dword near $
  bogus o16 jcxz strict dword $
  bogus o16 jcxz strict dword short $
  bogus o16 jcxz strict dword near $
  noq   o32 jcxz strict dword $
  noq   o32 jcxz strict dword short $
  bogus o32 jcxz strict dword near $
  bogus o64 jcxz strict dword $
  bogus o64 jcxz strict dword short $
  bogus o64 jcxz strict dword near $
  bogus jcxz strict qword $
  bogus jcxz strict qword short $
  bogus jcxz strict qword near $
  bogus o16 jcxz strict qword $
  bogus o16 jcxz strict qword short $
  bogus o16 jcxz strict qword near $
  bogus o32 jcxz strict qword $
  bogus o32 jcxz strict qword short $
  bogus o32 jcxz strict qword near $
  bogus o64 jcxz strict qword $
  bogus o64 jcxz strict qword short $
  bogus o64 jcxz strict qword near $
  bogus jcxz top
  bogus jcxz short top
  bogus jcxz near top
  bogus o16 jcxz top
  bogus o16 jcxz short top
  bogus o16 jcxz near top
  bogus o32 jcxz top
  bogus o32 jcxz short top
  bogus o32 jcxz near top
  bogus o64 jcxz top
  bogus o64 jcxz short top
  bogus o64 jcxz near top
  bogus jcxz byte top
  bogus jcxz byte short top
  bogus jcxz byte near top
  bogus o16 jcxz byte top
  bogus o16 jcxz byte short top
  bogus o16 jcxz byte near top
  bogus o32 jcxz byte top
  bogus o32 jcxz byte short top
  bogus o32 jcxz byte near top
  bogus o64 jcxz byte top
  bogus o64 jcxz byte short top
  bogus o64 jcxz byte near top
  bogus jcxz word top
  bogus jcxz word short top
  bogus jcxz word near top
  bogus o16 jcxz word top
  bogus o16 jcxz word short top
  bogus o16 jcxz word near top
  bogus o32 jcxz word top
  bogus o32 jcxz word short top
  bogus o32 jcxz word near top
  bogus o64 jcxz word top
  bogus o64 jcxz word short top
  bogus o64 jcxz word near top
  bogus jcxz dword top
  bogus jcxz dword short top
  bogus jcxz dword near top
  bogus o16 jcxz dword top
  bogus o16 jcxz dword short top
  bogus o16 jcxz dword near top
  bogus o32 jcxz dword top
  bogus o32 jcxz dword short top
  bogus o32 jcxz dword near top
  bogus o64 jcxz dword top
  bogus o64 jcxz dword short top
  bogus o64 jcxz dword near top
  bogus jcxz qword top
  bogus jcxz qword short top
  bogus jcxz qword near top
  bogus o16 jcxz qword top
  bogus o16 jcxz qword short top
  bogus o16 jcxz qword near top
  bogus o32 jcxz qword top
  bogus o32 jcxz qword short top
  bogus o32 jcxz qword near top
  bogus o64 jcxz qword top
  bogus o64 jcxz qword short top
  bogus o64 jcxz qword near top
  bogus jcxz strict top
  bogus jcxz strict short top
  bogus jcxz strict near top
  bogus o16 jcxz strict top
  bogus o16 jcxz strict short top
  bogus o16 jcxz strict near top
  bogus o32 jcxz strict top
  bogus o32 jcxz strict short top
  bogus o32 jcxz strict near top
  bogus o64 jcxz strict top
  bogus o64 jcxz strict short top
  bogus o64 jcxz strict near top
  bogus jcxz strict byte top
  bogus jcxz strict byte short top
  bogus jcxz strict byte near top
  bogus o16 jcxz strict byte top
  bogus o16 jcxz strict byte short top
  bogus o16 jcxz strict byte near top
  bogus o32 jcxz strict byte top
  bogus o32 jcxz strict byte short top
  bogus o32 jcxz strict byte near top
  bogus o64 jcxz strict byte top
  bogus o64 jcxz strict byte short top
  bogus o64 jcxz strict byte near top
  bogus jcxz strict word top
  bogus jcxz strict word short top
  bogus jcxz strict word near top
  bogus o16 jcxz strict word top
  bogus o16 jcxz strict word short top
  bogus o16 jcxz strict word near top
  bogus o32 jcxz strict word top
  bogus o32 jcxz strict word short top
  bogus o32 jcxz strict word near top
  bogus o64 jcxz strict word top
  bogus o64 jcxz strict word short top
  bogus o64 jcxz strict word near top
  bogus jcxz strict dword top
  bogus jcxz strict dword short top
  bogus jcxz strict dword near top
  bogus o16 jcxz strict dword top
  bogus o16 jcxz strict dword short top
  bogus o16 jcxz strict dword near top
  bogus o32 jcxz strict dword top
  bogus o32 jcxz strict dword short top
  bogus o32 jcxz strict dword near top
  bogus o64 jcxz strict dword top
  bogus o64 jcxz strict dword short top
  bogus o64 jcxz strict dword near top
  bogus jcxz strict qword top
  bogus jcxz strict qword short top
  bogus jcxz strict qword near top
  bogus o16 jcxz strict qword top
  bogus o16 jcxz strict qword short top
  bogus o16 jcxz strict qword near top
  bogus o32 jcxz strict qword top
  bogus o32 jcxz strict qword short top
  bogus o32 jcxz strict qword near top
  bogus o64 jcxz strict qword top
  bogus o64 jcxz strict qword short top
  bogus o64 jcxz strict qword near top
  bogus jcxz there
  bogus jcxz short there
  bogus jcxz near there
  bogus o16 jcxz there
  bogus o16 jcxz short there
  bogus o16 jcxz near there
  bogus o32 jcxz there
  bogus o32 jcxz short there
  bogus o32 jcxz near there
  bogus o64 jcxz there
  bogus o64 jcxz short there
  bogus o64 jcxz near there
  bogus jcxz byte there
  bogus jcxz byte short there
  bogus jcxz byte near there
  bogus o16 jcxz byte there
  bogus o16 jcxz byte short there
  bogus o16 jcxz byte near there
  bogus o32 jcxz byte there
  bogus o32 jcxz byte short there
  bogus o32 jcxz byte near there
  bogus o64 jcxz byte there
  bogus o64 jcxz byte short there
  bogus o64 jcxz byte near there
  bogus jcxz word there
  bogus jcxz word short there
  bogus jcxz word near there
  bogus o16 jcxz word there
  bogus o16 jcxz word short there
  bogus o16 jcxz word near there
  bogus o32 jcxz word there
  bogus o32 jcxz word short there
  bogus o32 jcxz word near there
  bogus o64 jcxz word there
  bogus o64 jcxz word short there
  bogus o64 jcxz word near there
  bogus jcxz dword there
  bogus jcxz dword short there
  bogus jcxz dword near there
  bogus o16 jcxz dword there
  bogus o16 jcxz dword short there
  bogus o16 jcxz dword near there
  bogus o32 jcxz dword there
  bogus o32 jcxz dword short there
  bogus o32 jcxz dword near there
  bogus o64 jcxz dword there
  bogus o64 jcxz dword short there
  bogus o64 jcxz dword near there
  bogus jcxz qword there
  bogus jcxz qword short there
  bogus jcxz qword near there
  bogus o16 jcxz qword there
  bogus o16 jcxz qword short there
  bogus o16 jcxz qword near there
  bogus o32 jcxz qword there
  bogus o32 jcxz qword short there
  bogus o32 jcxz qword near there
  bogus o64 jcxz qword there
  bogus o64 jcxz qword short there
  bogus o64 jcxz qword near there
  bogus jcxz strict there
  bogus jcxz strict short there
  bogus jcxz strict near there
  bogus o16 jcxz strict there
  bogus o16 jcxz strict short there
  bogus o16 jcxz strict near there
  bogus o32 jcxz strict there
  bogus o32 jcxz strict short there
  bogus o32 jcxz strict near there
  bogus o64 jcxz strict there
  bogus o64 jcxz strict short there
  bogus o64 jcxz strict near there
  bogus jcxz strict byte there
  bogus jcxz strict byte short there
  bogus jcxz strict byte near there
  bogus o16 jcxz strict byte there
  bogus o16 jcxz strict byte short there
  bogus o16 jcxz strict byte near there
  bogus o32 jcxz strict byte there
  bogus o32 jcxz strict byte short there
  bogus o32 jcxz strict byte near there
  bogus o64 jcxz strict byte there
  bogus o64 jcxz strict byte short there
  bogus o64 jcxz strict byte near there
  bogus jcxz strict word there
  bogus jcxz strict word short there
  bogus jcxz strict word near there
  bogus o16 jcxz strict word there
  bogus o16 jcxz strict word short there
  bogus o16 jcxz strict word near there
  bogus o32 jcxz strict word there
  bogus o32 jcxz strict word short there
  bogus o32 jcxz strict word near there
  bogus o64 jcxz strict word there
  bogus o64 jcxz strict word short there
  bogus o64 jcxz strict word near there
  bogus jcxz strict dword there
  bogus jcxz strict dword short there
  bogus jcxz strict dword near there
  bogus o16 jcxz strict dword there
  bogus o16 jcxz strict dword short there
  bogus o16 jcxz strict dword near there
  bogus o32 jcxz strict dword there
  bogus o32 jcxz strict dword short there
  bogus o32 jcxz strict dword near there
  bogus o64 jcxz strict dword there
  bogus o64 jcxz strict dword short there
  bogus o64 jcxz strict dword near there
  bogus jcxz strict qword there
  bogus jcxz strict qword short there
  bogus jcxz strict qword near there
  bogus o16 jcxz strict qword there
  bogus o16 jcxz strict qword short there
  bogus o16 jcxz strict qword near there
  bogus o32 jcxz strict qword there
  bogus o32 jcxz strict qword short there
  bogus o32 jcxz strict qword near there
  bogus o64 jcxz strict qword there
  bogus o64 jcxz strict qword short there
  bogus o64 jcxz strict qword near there
here_jecxz:
        jecxz $
        jecxz short $
  bogus jecxz near $
  noq   o16 jecxz $
  noq   o16 jecxz short $
  bogus o16 jecxz near $
  noq   o32 jecxz $
  noq   o32 jecxz short $
  bogus o32 jecxz near $
  nowd  o64 jecxz $
  nowd  o64 jecxz short $
  bogus o64 jecxz near $
  bogus jecxz byte $
  bogus jecxz byte short $
  bogus jecxz byte near $
  bogus o16 jecxz byte $
  bogus o16 jecxz byte short $
  bogus o16 jecxz byte near $
  bogus o32 jecxz byte $
  bogus o32 jecxz byte short $
  bogus o32 jecxz byte near $
  bogus o64 jecxz byte $
  bogus o64 jecxz byte short $
  bogus o64 jecxz byte near $
  noq   jecxz word $
  noq   jecxz word short $
  bogus jecxz word near $
  noq   o16 jecxz word $
  noq   o16 jecxz word short $
  bogus o16 jecxz word near $
  bogus o32 jecxz word $
  bogus o32 jecxz word short $
  bogus o32 jecxz word near $
  bogus o64 jecxz word $
  bogus o64 jecxz word short $
  bogus o64 jecxz word near $
  noq   jecxz dword $
  noq   jecxz dword short $
  bogus jecxz dword near $
  bogus o16 jecxz dword $
  bogus o16 jecxz dword short $
  bogus o16 jecxz dword near $
  noq   o32 jecxz dword $
  noq   o32 jecxz dword short $
  bogus o32 jecxz dword near $
  bogus o64 jecxz dword $
  bogus o64 jecxz dword short $
  bogus o64 jecxz dword near $
  nowd  jecxz qword $
  nowd  jecxz qword short $
  bogus jecxz qword near $
  bogus o16 jecxz qword $
  bogus o16 jecxz qword short $
  bogus o16 jecxz qword near $
  bogus o32 jecxz qword $
  bogus o32 jecxz qword short $
  bogus o32 jecxz qword near $
  nowd  o64 jecxz qword $
  nowd  o64 jecxz qword short $
  bogus o64 jecxz qword near $
        jecxz strict $
        jecxz strict short $
  bogus jecxz strict near $
  noq   o16 jecxz strict $
  noq   o16 jecxz strict short $
  bogus o16 jecxz strict near $
  noq   o32 jecxz strict $
  noq   o32 jecxz strict short $
  bogus o32 jecxz strict near $
  nowd  o64 jecxz strict $
  nowd  o64 jecxz strict short $
  bogus o64 jecxz strict near $
  bogus jecxz strict byte $
  bogus jecxz strict byte short $
  bogus jecxz strict byte near $
  bogus o16 jecxz strict byte $
  bogus o16 jecxz strict byte short $
  bogus o16 jecxz strict byte near $
  bogus o32 jecxz strict byte $
  bogus o32 jecxz strict byte short $
  bogus o32 jecxz strict byte near $
  bogus o64 jecxz strict byte $
  bogus o64 jecxz strict byte short $
  bogus o64 jecxz strict byte near $
  noq   jecxz strict word $
  noq   jecxz strict word short $
  bogus jecxz strict word near $
  noq   o16 jecxz strict word $
  noq   o16 jecxz strict word short $
  bogus o16 jecxz strict word near $
  bogus o32 jecxz strict word $
  bogus o32 jecxz strict word short $
  bogus o32 jecxz strict word near $
  bogus o64 jecxz strict word $
  bogus o64 jecxz strict word short $
  bogus o64 jecxz strict word near $
  noq   jecxz strict dword $
  noq   jecxz strict dword short $
  bogus jecxz strict dword near $
  bogus o16 jecxz strict dword $
  bogus o16 jecxz strict dword short $
  bogus o16 jecxz strict dword near $
  noq   o32 jecxz strict dword $
  noq   o32 jecxz strict dword short $
  bogus o32 jecxz strict dword near $
  bogus o64 jecxz strict dword $
  bogus o64 jecxz strict dword short $
  bogus o64 jecxz strict dword near $
  nowd  jecxz strict qword $
  nowd  jecxz strict qword short $
  bogus jecxz strict qword near $
  bogus o16 jecxz strict qword $
  bogus o16 jecxz strict qword short $
  bogus o16 jecxz strict qword near $
  bogus o32 jecxz strict qword $
  bogus o32 jecxz strict qword short $
  bogus o32 jecxz strict qword near $
  nowd  o64 jecxz strict qword $
  nowd  o64 jecxz strict qword short $
  bogus o64 jecxz strict qword near $
  bogus jecxz top
  bogus jecxz short top
  bogus jecxz near top
  bogus o16 jecxz top
  bogus o16 jecxz short top
  bogus o16 jecxz near top
  bogus o32 jecxz top
  bogus o32 jecxz short top
  bogus o32 jecxz near top
  bogus o64 jecxz top
  bogus o64 jecxz short top
  bogus o64 jecxz near top
  bogus jecxz byte top
  bogus jecxz byte short top
  bogus jecxz byte near top
  bogus o16 jecxz byte top
  bogus o16 jecxz byte short top
  bogus o16 jecxz byte near top
  bogus o32 jecxz byte top
  bogus o32 jecxz byte short top
  bogus o32 jecxz byte near top
  bogus o64 jecxz byte top
  bogus o64 jecxz byte short top
  bogus o64 jecxz byte near top
  bogus jecxz word top
  bogus jecxz word short top
  bogus jecxz word near top
  bogus o16 jecxz word top
  bogus o16 jecxz word short top
  bogus o16 jecxz word near top
  bogus o32 jecxz word top
  bogus o32 jecxz word short top
  bogus o32 jecxz word near top
  bogus o64 jecxz word top
  bogus o64 jecxz word short top
  bogus o64 jecxz word near top
  bogus jecxz dword top
  bogus jecxz dword short top
  bogus jecxz dword near top
  bogus o16 jecxz dword top
  bogus o16 jecxz dword short top
  bogus o16 jecxz dword near top
  bogus o32 jecxz dword top
  bogus o32 jecxz dword short top
  bogus o32 jecxz dword near top
  bogus o64 jecxz dword top
  bogus o64 jecxz dword short top
  bogus o64 jecxz dword near top
  bogus jecxz qword top
  bogus jecxz qword short top
  bogus jecxz qword near top
  bogus o16 jecxz qword top
  bogus o16 jecxz qword short top
  bogus o16 jecxz qword near top
  bogus o32 jecxz qword top
  bogus o32 jecxz qword short top
  bogus o32 jecxz qword near top
  bogus o64 jecxz qword top
  bogus o64 jecxz qword short top
  bogus o64 jecxz qword near top
  bogus jecxz strict top
  bogus jecxz strict short top
  bogus jecxz strict near top
  bogus o16 jecxz strict top
  bogus o16 jecxz strict short top
  bogus o16 jecxz strict near top
  bogus o32 jecxz strict top
  bogus o32 jecxz strict short top
  bogus o32 jecxz strict near top
  bogus o64 jecxz strict top
  bogus o64 jecxz strict short top
  bogus o64 jecxz strict near top
  bogus jecxz strict byte top
  bogus jecxz strict byte short top
  bogus jecxz strict byte near top
  bogus o16 jecxz strict byte top
  bogus o16 jecxz strict byte short top
  bogus o16 jecxz strict byte near top
  bogus o32 jecxz strict byte top
  bogus o32 jecxz strict byte short top
  bogus o32 jecxz strict byte near top
  bogus o64 jecxz strict byte top
  bogus o64 jecxz strict byte short top
  bogus o64 jecxz strict byte near top
  bogus jecxz strict word top
  bogus jecxz strict word short top
  bogus jecxz strict word near top
  bogus o16 jecxz strict word top
  bogus o16 jecxz strict word short top
  bogus o16 jecxz strict word near top
  bogus o32 jecxz strict word top
  bogus o32 jecxz strict word short top
  bogus o32 jecxz strict word near top
  bogus o64 jecxz strict word top
  bogus o64 jecxz strict word short top
  bogus o64 jecxz strict word near top
  bogus jecxz strict dword top
  bogus jecxz strict dword short top
  bogus jecxz strict dword near top
  bogus o16 jecxz strict dword top
  bogus o16 jecxz strict dword short top
  bogus o16 jecxz strict dword near top
  bogus o32 jecxz strict dword top
  bogus o32 jecxz strict dword short top
  bogus o32 jecxz strict dword near top
  bogus o64 jecxz strict dword top
  bogus o64 jecxz strict dword short top
  bogus o64 jecxz strict dword near top
  bogus jecxz strict qword top
  bogus jecxz strict qword short top
  bogus jecxz strict qword near top
  bogus o16 jecxz strict qword top
  bogus o16 jecxz strict qword short top
  bogus o16 jecxz strict qword near top
  bogus o32 jecxz strict qword top
  bogus o32 jecxz strict qword short top
  bogus o32 jecxz strict qword near top
  bogus o64 jecxz strict qword top
  bogus o64 jecxz strict qword short top
  bogus o64 jecxz strict qword near top
  bogus jecxz there
  bogus jecxz short there
  bogus jecxz near there
  bogus o16 jecxz there
  bogus o16 jecxz short there
  bogus o16 jecxz near there
  bogus o32 jecxz there
  bogus o32 jecxz short there
  bogus o32 jecxz near there
  bogus o64 jecxz there
  bogus o64 jecxz short there
  bogus o64 jecxz near there
  bogus jecxz byte there
  bogus jecxz byte short there
  bogus jecxz byte near there
  bogus o16 jecxz byte there
  bogus o16 jecxz byte short there
  bogus o16 jecxz byte near there
  bogus o32 jecxz byte there
  bogus o32 jecxz byte short there
  bogus o32 jecxz byte near there
  bogus o64 jecxz byte there
  bogus o64 jecxz byte short there
  bogus o64 jecxz byte near there
  bogus jecxz word there
  bogus jecxz word short there
  bogus jecxz word near there
  bogus o16 jecxz word there
  bogus o16 jecxz word short there
  bogus o16 jecxz word near there
  bogus o32 jecxz word there
  bogus o32 jecxz word short there
  bogus o32 jecxz word near there
  bogus o64 jecxz word there
  bogus o64 jecxz word short there
  bogus o64 jecxz word near there
  bogus jecxz dword there
  bogus jecxz dword short there
  bogus jecxz dword near there
  bogus o16 jecxz dword there
  bogus o16 jecxz dword short there
  bogus o16 jecxz dword near there
  bogus o32 jecxz dword there
  bogus o32 jecxz dword short there
  bogus o32 jecxz dword near there
  bogus o64 jecxz dword there
  bogus o64 jecxz dword short there
  bogus o64 jecxz dword near there
  bogus jecxz qword there
  bogus jecxz qword short there
  bogus jecxz qword near there
  bogus o16 jecxz qword there
  bogus o16 jecxz qword short there
  bogus o16 jecxz qword near there
  bogus o32 jecxz qword there
  bogus o32 jecxz qword short there
  bogus o32 jecxz qword near there
  bogus o64 jecxz qword there
  bogus o64 jecxz qword short there
  bogus o64 jecxz qword near there
  bogus jecxz strict there
  bogus jecxz strict short there
  bogus jecxz strict near there
  bogus o16 jecxz strict there
  bogus o16 jecxz strict short there
  bogus o16 jecxz strict near there
  bogus o32 jecxz strict there
  bogus o32 jecxz strict short there
  bogus o32 jecxz strict near there
  bogus o64 jecxz strict there
  bogus o64 jecxz strict short there
  bogus o64 jecxz strict near there
  bogus jecxz strict byte there
  bogus jecxz strict byte short there
  bogus jecxz strict byte near there
  bogus o16 jecxz strict byte there
  bogus o16 jecxz strict byte short there
  bogus o16 jecxz strict byte near there
  bogus o32 jecxz strict byte there
  bogus o32 jecxz strict byte short there
  bogus o32 jecxz strict byte near there
  bogus o64 jecxz strict byte there
  bogus o64 jecxz strict byte short there
  bogus o64 jecxz strict byte near there
  bogus jecxz strict word there
  bogus jecxz strict word short there
  bogus jecxz strict word near there
  bogus o16 jecxz strict word there
  bogus o16 jecxz strict word short there
  bogus o16 jecxz strict word near there
  bogus o32 jecxz strict word there
  bogus o32 jecxz strict word short there
  bogus o32 jecxz strict word near there
  bogus o64 jecxz strict word there
  bogus o64 jecxz strict word short there
  bogus o64 jecxz strict word near there
  bogus jecxz strict dword there
  bogus jecxz strict dword short there
  bogus jecxz strict dword near there
  bogus o16 jecxz strict dword there
  bogus o16 jecxz strict dword short there
  bogus o16 jecxz strict dword near there
  bogus o32 jecxz strict dword there
  bogus o32 jecxz strict dword short there
  bogus o32 jecxz strict dword near there
  bogus o64 jecxz strict dword there
  bogus o64 jecxz strict dword short there
  bogus o64 jecxz strict dword near there
  bogus jecxz strict qword there
  bogus jecxz strict qword short there
  bogus jecxz strict qword near there
  bogus o16 jecxz strict qword there
  bogus o16 jecxz strict qword short there
  bogus o16 jecxz strict qword near there
  bogus o32 jecxz strict qword there
  bogus o32 jecxz strict qword short there
  bogus o32 jecxz strict qword near there
  bogus o64 jecxz strict qword there
  bogus o64 jecxz strict qword short there
  bogus o64 jecxz strict qword near there
here_jrcxz:
  nowd  jrcxz $
  nowd  jrcxz short $
  bogus jrcxz near $
  bogus o16 jrcxz $
  bogus o16 jrcxz short $
  bogus o16 jrcxz near $
  bogus o32 jrcxz $
  bogus o32 jrcxz short $
  bogus o32 jrcxz near $
  nowd  o64 jrcxz $
  nowd  o64 jrcxz short $
  bogus o64 jrcxz near $
  bogus jrcxz byte $
  bogus jrcxz byte short $
  bogus jrcxz byte near $
  bogus o16 jrcxz byte $
  bogus o16 jrcxz byte short $
  bogus o16 jrcxz byte near $
  bogus o32 jrcxz byte $
  bogus o32 jrcxz byte short $
  bogus o32 jrcxz byte near $
  bogus o64 jrcxz byte $
  bogus o64 jrcxz byte short $
  bogus o64 jrcxz byte near $
  bogus jrcxz word $
  bogus jrcxz word short $
  bogus jrcxz word near $
  bogus o16 jrcxz word $
  bogus o16 jrcxz word short $
  bogus o16 jrcxz word near $
  bogus o32 jrcxz word $
  bogus o32 jrcxz word short $
  bogus o32 jrcxz word near $
  bogus o64 jrcxz word $
  bogus o64 jrcxz word short $
  bogus o64 jrcxz word near $
  bogus jrcxz dword $
  bogus jrcxz dword short $
  bogus jrcxz dword near $
  bogus o16 jrcxz dword $
  bogus o16 jrcxz dword short $
  bogus o16 jrcxz dword near $
  bogus o32 jrcxz dword $
  bogus o32 jrcxz dword short $
  bogus o32 jrcxz dword near $
  bogus o64 jrcxz dword $
  bogus o64 jrcxz dword short $
  bogus o64 jrcxz dword near $
  nowd  jrcxz qword $
  nowd  jrcxz qword short $
  bogus jrcxz qword near $
  bogus o16 jrcxz qword $
  bogus o16 jrcxz qword short $
  bogus o16 jrcxz qword near $
  bogus o32 jrcxz qword $
  bogus o32 jrcxz qword short $
  bogus o32 jrcxz qword near $
  nowd  o64 jrcxz qword $
  nowd  o64 jrcxz qword short $
  bogus o64 jrcxz qword near $
  nowd  jrcxz strict $
  nowd  jrcxz strict short $
  bogus jrcxz strict near $
  bogus o16 jrcxz strict $
  bogus o16 jrcxz strict short $
  bogus o16 jrcxz strict near $
  bogus o32 jrcxz strict $
  bogus o32 jrcxz strict short $
  bogus o32 jrcxz strict near $
  nowd  o64 jrcxz strict $
  nowd  o64 jrcxz strict short $
  bogus o64 jrcxz strict near $
  bogus jrcxz strict byte $
  bogus jrcxz strict byte short $
  bogus jrcxz strict byte near $
  bogus o16 jrcxz strict byte $
  bogus o16 jrcxz strict byte short $
  bogus o16 jrcxz strict byte near $
  bogus o32 jrcxz strict byte $
  bogus o32 jrcxz strict byte short $
  bogus o32 jrcxz strict byte near $
  bogus o64 jrcxz strict byte $
  bogus o64 jrcxz strict byte short $
  bogus o64 jrcxz strict byte near $
  bogus jrcxz strict word $
  bogus jrcxz strict word short $
  bogus jrcxz strict word near $
  bogus o16 jrcxz strict word $
  bogus o16 jrcxz strict word short $
  bogus o16 jrcxz strict word near $
  bogus o32 jrcxz strict word $
  bogus o32 jrcxz strict word short $
  bogus o32 jrcxz strict word near $
  bogus o64 jrcxz strict word $
  bogus o64 jrcxz strict word short $
  bogus o64 jrcxz strict word near $
  bogus jrcxz strict dword $
  bogus jrcxz strict dword short $
  bogus jrcxz strict dword near $
  bogus o16 jrcxz strict dword $
  bogus o16 jrcxz strict dword short $
  bogus o16 jrcxz strict dword near $
  bogus o32 jrcxz strict dword $
  bogus o32 jrcxz strict dword short $
  bogus o32 jrcxz strict dword near $
  bogus o64 jrcxz strict dword $
  bogus o64 jrcxz strict dword short $
  bogus o64 jrcxz strict dword near $
  nowd  jrcxz strict qword $
  nowd  jrcxz strict qword short $
  bogus jrcxz strict qword near $
  bogus o16 jrcxz strict qword $
  bogus o16 jrcxz strict qword short $
  bogus o16 jrcxz strict qword near $
  bogus o32 jrcxz strict qword $
  bogus o32 jrcxz strict qword short $
  bogus o32 jrcxz strict qword near $
  nowd  o64 jrcxz strict qword $
  nowd  o64 jrcxz strict qword short $
  bogus o64 jrcxz strict qword near $
  bogus jrcxz top
  bogus jrcxz short top
  bogus jrcxz near top
  bogus o16 jrcxz top
  bogus o16 jrcxz short top
  bogus o16 jrcxz near top
  bogus o32 jrcxz top
  bogus o32 jrcxz short top
  bogus o32 jrcxz near top
  bogus o64 jrcxz top
  bogus o64 jrcxz short top
  bogus o64 jrcxz near top
  bogus jrcxz byte top
  bogus jrcxz byte short top
  bogus jrcxz byte near top
  bogus o16 jrcxz byte top
  bogus o16 jrcxz byte short top
  bogus o16 jrcxz byte near top
  bogus o32 jrcxz byte top
  bogus o32 jrcxz byte short top
  bogus o32 jrcxz byte near top
  bogus o64 jrcxz byte top
  bogus o64 jrcxz byte short top
  bogus o64 jrcxz byte near top
  bogus jrcxz word top
  bogus jrcxz word short top
  bogus jrcxz word near top
  bogus o16 jrcxz word top
  bogus o16 jrcxz word short top
  bogus o16 jrcxz word near top
  bogus o32 jrcxz word top
  bogus o32 jrcxz word short top
  bogus o32 jrcxz word near top
  bogus o64 jrcxz word top
  bogus o64 jrcxz word short top
  bogus o64 jrcxz word near top
  bogus jrcxz dword top
  bogus jrcxz dword short top
  bogus jrcxz dword near top
  bogus o16 jrcxz dword top
  bogus o16 jrcxz dword short top
  bogus o16 jrcxz dword near top
  bogus o32 jrcxz dword top
  bogus o32 jrcxz dword short top
  bogus o32 jrcxz dword near top
  bogus o64 jrcxz dword top
  bogus o64 jrcxz dword short top
  bogus o64 jrcxz dword near top
  bogus jrcxz qword top
  bogus jrcxz qword short top
  bogus jrcxz qword near top
  bogus o16 jrcxz qword top
  bogus o16 jrcxz qword short top
  bogus o16 jrcxz qword near top
  bogus o32 jrcxz qword top
  bogus o32 jrcxz qword short top
  bogus o32 jrcxz qword near top
  bogus o64 jrcxz qword top
  bogus o64 jrcxz qword short top
  bogus o64 jrcxz qword near top
  bogus jrcxz strict top
  bogus jrcxz strict short top
  bogus jrcxz strict near top
  bogus o16 jrcxz strict top
  bogus o16 jrcxz strict short top
  bogus o16 jrcxz strict near top
  bogus o32 jrcxz strict top
  bogus o32 jrcxz strict short top
  bogus o32 jrcxz strict near top
  bogus o64 jrcxz strict top
  bogus o64 jrcxz strict short top
  bogus o64 jrcxz strict near top
  bogus jrcxz strict byte top
  bogus jrcxz strict byte short top
  bogus jrcxz strict byte near top
  bogus o16 jrcxz strict byte top
  bogus o16 jrcxz strict byte short top
  bogus o16 jrcxz strict byte near top
  bogus o32 jrcxz strict byte top
  bogus o32 jrcxz strict byte short top
  bogus o32 jrcxz strict byte near top
  bogus o64 jrcxz strict byte top
  bogus o64 jrcxz strict byte short top
  bogus o64 jrcxz strict byte near top
  bogus jrcxz strict word top
  bogus jrcxz strict word short top
  bogus jrcxz strict word near top
  bogus o16 jrcxz strict word top
  bogus o16 jrcxz strict word short top
  bogus o16 jrcxz strict word near top
  bogus o32 jrcxz strict word top
  bogus o32 jrcxz strict word short top
  bogus o32 jrcxz strict word near top
  bogus o64 jrcxz strict word top
  bogus o64 jrcxz strict word short top
  bogus o64 jrcxz strict word near top
  bogus jrcxz strict dword top
  bogus jrcxz strict dword short top
  bogus jrcxz strict dword near top
  bogus o16 jrcxz strict dword top
  bogus o16 jrcxz strict dword short top
  bogus o16 jrcxz strict dword near top
  bogus o32 jrcxz strict dword top
  bogus o32 jrcxz strict dword short top
  bogus o32 jrcxz strict dword near top
  bogus o64 jrcxz strict dword top
  bogus o64 jrcxz strict dword short top
  bogus o64 jrcxz strict dword near top
  bogus jrcxz strict qword top
  bogus jrcxz strict qword short top
  bogus jrcxz strict qword near top
  bogus o16 jrcxz strict qword top
  bogus o16 jrcxz strict qword short top
  bogus o16 jrcxz strict qword near top
  bogus o32 jrcxz strict qword top
  bogus o32 jrcxz strict qword short top
  bogus o32 jrcxz strict qword near top
  bogus o64 jrcxz strict qword top
  bogus o64 jrcxz strict qword short top
  bogus o64 jrcxz strict qword near top
  bogus jrcxz there
  bogus jrcxz short there
  bogus jrcxz near there
  bogus o16 jrcxz there
  bogus o16 jrcxz short there
  bogus o16 jrcxz near there
  bogus o32 jrcxz there
  bogus o32 jrcxz short there
  bogus o32 jrcxz near there
  bogus o64 jrcxz there
  bogus o64 jrcxz short there
  bogus o64 jrcxz near there
  bogus jrcxz byte there
  bogus jrcxz byte short there
  bogus jrcxz byte near there
  bogus o16 jrcxz byte there
  bogus o16 jrcxz byte short there
  bogus o16 jrcxz byte near there
  bogus o32 jrcxz byte there
  bogus o32 jrcxz byte short there
  bogus o32 jrcxz byte near there
  bogus o64 jrcxz byte there
  bogus o64 jrcxz byte short there
  bogus o64 jrcxz byte near there
  bogus jrcxz word there
  bogus jrcxz word short there
  bogus jrcxz word near there
  bogus o16 jrcxz word there
  bogus o16 jrcxz word short there
  bogus o16 jrcxz word near there
  bogus o32 jrcxz word there
  bogus o32 jrcxz word short there
  bogus o32 jrcxz word near there
  bogus o64 jrcxz word there
  bogus o64 jrcxz word short there
  bogus o64 jrcxz word near there
  bogus jrcxz dword there
  bogus jrcxz dword short there
  bogus jrcxz dword near there
  bogus o16 jrcxz dword there
  bogus o16 jrcxz dword short there
  bogus o16 jrcxz dword near there
  bogus o32 jrcxz dword there
  bogus o32 jrcxz dword short there
  bogus o32 jrcxz dword near there
  bogus o64 jrcxz dword there
  bogus o64 jrcxz dword short there
  bogus o64 jrcxz dword near there
  bogus jrcxz qword there
  bogus jrcxz qword short there
  bogus jrcxz qword near there
  bogus o16 jrcxz qword there
  bogus o16 jrcxz qword short there
  bogus o16 jrcxz qword near there
  bogus o32 jrcxz qword there
  bogus o32 jrcxz qword short there
  bogus o32 jrcxz qword near there
  bogus o64 jrcxz qword there
  bogus o64 jrcxz qword short there
  bogus o64 jrcxz qword near there
  bogus jrcxz strict there
  bogus jrcxz strict short there
  bogus jrcxz strict near there
  bogus o16 jrcxz strict there
  bogus o16 jrcxz strict short there
  bogus o16 jrcxz strict near there
  bogus o32 jrcxz strict there
  bogus o32 jrcxz strict short there
  bogus o32 jrcxz strict near there
  bogus o64 jrcxz strict there
  bogus o64 jrcxz strict short there
  bogus o64 jrcxz strict near there
  bogus jrcxz strict byte there
  bogus jrcxz strict byte short there
  bogus jrcxz strict byte near there
  bogus o16 jrcxz strict byte there
  bogus o16 jrcxz strict byte short there
  bogus o16 jrcxz strict byte near there
  bogus o32 jrcxz strict byte there
  bogus o32 jrcxz strict byte short there
  bogus o32 jrcxz strict byte near there
  bogus o64 jrcxz strict byte there
  bogus o64 jrcxz strict byte short there
  bogus o64 jrcxz strict byte near there
  bogus jrcxz strict word there
  bogus jrcxz strict word short there
  bogus jrcxz strict word near there
  bogus o16 jrcxz strict word there
  bogus o16 jrcxz strict word short there
  bogus o16 jrcxz strict word near there
  bogus o32 jrcxz strict word there
  bogus o32 jrcxz strict word short there
  bogus o32 jrcxz strict word near there
  bogus o64 jrcxz strict word there
  bogus o64 jrcxz strict word short there
  bogus o64 jrcxz strict word near there
  bogus jrcxz strict dword there
  bogus jrcxz strict dword short there
  bogus jrcxz strict dword near there
  bogus o16 jrcxz strict dword there
  bogus o16 jrcxz strict dword short there
  bogus o16 jrcxz strict dword near there
  bogus o32 jrcxz strict dword there
  bogus o32 jrcxz strict dword short there
  bogus o32 jrcxz strict dword near there
  bogus o64 jrcxz strict dword there
  bogus o64 jrcxz strict dword short there
  bogus o64 jrcxz strict dword near there
  bogus jrcxz strict qword there
  bogus jrcxz strict qword short there
  bogus jrcxz strict qword near there
  bogus o16 jrcxz strict qword there
  bogus o16 jrcxz strict qword short there
  bogus o16 jrcxz strict qword near there
  bogus o32 jrcxz strict qword there
  bogus o32 jrcxz strict qword short there
  bogus o32 jrcxz strict qword near there
  bogus o64 jrcxz strict qword there
  bogus o64 jrcxz strict qword short there
  bogus o64 jrcxz strict qword near there
here_loop:
        loop $
        loop short $
  bogus loop near $
  noq   o16 loop $
  noq   o16 loop short $
  bogus o16 loop near $
  noq   o32 loop $
  noq   o32 loop short $
  bogus o32 loop near $
  nowd  o64 loop $
  nowd  o64 loop short $
  bogus o64 loop near $
  bogus loop byte $
  bogus loop byte short $
  bogus loop byte near $
  bogus o16 loop byte $
  bogus o16 loop byte short $
  bogus o16 loop byte near $
  bogus o32 loop byte $
  bogus o32 loop byte short $
  bogus o32 loop byte near $
  bogus o64 loop byte $
  bogus o64 loop byte short $
  bogus o64 loop byte near $
  noq   loop word $
  noq   loop word short $
  bogus loop word near $
  noq   o16 loop word $
  noq   o16 loop word short $
  bogus o16 loop word near $
  bogus o32 loop word $
  bogus o32 loop word short $
  bogus o32 loop word near $
  bogus o64 loop word $
  bogus o64 loop word short $
  bogus o64 loop word near $
  noq   loop dword $
  noq   loop dword short $
  bogus loop dword near $
  bogus o16 loop dword $
  bogus o16 loop dword short $
  bogus o16 loop dword near $
  noq   o32 loop dword $
  noq   o32 loop dword short $
  bogus o32 loop dword near $
  bogus o64 loop dword $
  bogus o64 loop dword short $
  bogus o64 loop dword near $
  nowd  loop qword $
  nowd  loop qword short $
  bogus loop qword near $
  bogus o16 loop qword $
  bogus o16 loop qword short $
  bogus o16 loop qword near $
  bogus o32 loop qword $
  bogus o32 loop qword short $
  bogus o32 loop qword near $
  nowd  o64 loop qword $
  nowd  o64 loop qword short $
  bogus o64 loop qword near $
        loop strict $
        loop strict short $
  bogus loop strict near $
  noq   o16 loop strict $
  noq   o16 loop strict short $
  bogus o16 loop strict near $
  noq   o32 loop strict $
  noq   o32 loop strict short $
  bogus o32 loop strict near $
  nowd  o64 loop strict $
  nowd  o64 loop strict short $
  bogus o64 loop strict near $
  bogus loop strict byte $
  bogus loop strict byte short $
  bogus loop strict byte near $
  bogus o16 loop strict byte $
  bogus o16 loop strict byte short $
  bogus o16 loop strict byte near $
  bogus o32 loop strict byte $
  bogus o32 loop strict byte short $
  bogus o32 loop strict byte near $
  bogus o64 loop strict byte $
  bogus o64 loop strict byte short $
  bogus o64 loop strict byte near $
  noq   loop strict word $
  noq   loop strict word short $
  bogus loop strict word near $
  noq   o16 loop strict word $
  noq   o16 loop strict word short $
  bogus o16 loop strict word near $
  bogus o32 loop strict word $
  bogus o32 loop strict word short $
  bogus o32 loop strict word near $
  bogus o64 loop strict word $
  bogus o64 loop strict word short $
  bogus o64 loop strict word near $
  noq   loop strict dword $
  noq   loop strict dword short $
  bogus loop strict dword near $
  bogus o16 loop strict dword $
  bogus o16 loop strict dword short $
  bogus o16 loop strict dword near $
  noq   o32 loop strict dword $
  noq   o32 loop strict dword short $
  bogus o32 loop strict dword near $
  bogus o64 loop strict dword $
  bogus o64 loop strict dword short $
  bogus o64 loop strict dword near $
  nowd  loop strict qword $
  nowd  loop strict qword short $
  bogus loop strict qword near $
  bogus o16 loop strict qword $
  bogus o16 loop strict qword short $
  bogus o16 loop strict qword near $
  bogus o32 loop strict qword $
  bogus o32 loop strict qword short $
  bogus o32 loop strict qword near $
  nowd  o64 loop strict qword $
  nowd  o64 loop strict qword short $
  bogus o64 loop strict qword near $
  bogus loop top
  bogus loop short top
  bogus loop near top
  bogus o16 loop top
  bogus o16 loop short top
  bogus o16 loop near top
  bogus o32 loop top
  bogus o32 loop short top
  bogus o32 loop near top
  bogus o64 loop top
  bogus o64 loop short top
  bogus o64 loop near top
  bogus loop byte top
  bogus loop byte short top
  bogus loop byte near top
  bogus o16 loop byte top
  bogus o16 loop byte short top
  bogus o16 loop byte near top
  bogus o32 loop byte top
  bogus o32 loop byte short top
  bogus o32 loop byte near top
  bogus o64 loop byte top
  bogus o64 loop byte short top
  bogus o64 loop byte near top
  bogus loop word top
  bogus loop word short top
  bogus loop word near top
  bogus o16 loop word top
  bogus o16 loop word short top
  bogus o16 loop word near top
  bogus o32 loop word top
  bogus o32 loop word short top
  bogus o32 loop word near top
  bogus o64 loop word top
  bogus o64 loop word short top
  bogus o64 loop word near top
  bogus loop dword top
  bogus loop dword short top
  bogus loop dword near top
  bogus o16 loop dword top
  bogus o16 loop dword short top
  bogus o16 loop dword near top
  bogus o32 loop dword top
  bogus o32 loop dword short top
  bogus o32 loop dword near top
  bogus o64 loop dword top
  bogus o64 loop dword short top
  bogus o64 loop dword near top
  bogus loop qword top
  bogus loop qword short top
  bogus loop qword near top
  bogus o16 loop qword top
  bogus o16 loop qword short top
  bogus o16 loop qword near top
  bogus o32 loop qword top
  bogus o32 loop qword short top
  bogus o32 loop qword near top
  bogus o64 loop qword top
  bogus o64 loop qword short top
  bogus o64 loop qword near top
  bogus loop strict top
  bogus loop strict short top
  bogus loop strict near top
  bogus o16 loop strict top
  bogus o16 loop strict short top
  bogus o16 loop strict near top
  bogus o32 loop strict top
  bogus o32 loop strict short top
  bogus o32 loop strict near top
  bogus o64 loop strict top
  bogus o64 loop strict short top
  bogus o64 loop strict near top
  bogus loop strict byte top
  bogus loop strict byte short top
  bogus loop strict byte near top
  bogus o16 loop strict byte top
  bogus o16 loop strict byte short top
  bogus o16 loop strict byte near top
  bogus o32 loop strict byte top
  bogus o32 loop strict byte short top
  bogus o32 loop strict byte near top
  bogus o64 loop strict byte top
  bogus o64 loop strict byte short top
  bogus o64 loop strict byte near top
  bogus loop strict word top
  bogus loop strict word short top
  bogus loop strict word near top
  bogus o16 loop strict word top
  bogus o16 loop strict word short top
  bogus o16 loop strict word near top
  bogus o32 loop strict word top
  bogus o32 loop strict word short top
  bogus o32 loop strict word near top
  bogus o64 loop strict word top
  bogus o64 loop strict word short top
  bogus o64 loop strict word near top
  bogus loop strict dword top
  bogus loop strict dword short top
  bogus loop strict dword near top
  bogus o16 loop strict dword top
  bogus o16 loop strict dword short top
  bogus o16 loop strict dword near top
  bogus o32 loop strict dword top
  bogus o32 loop strict dword short top
  bogus o32 loop strict dword near top
  bogus o64 loop strict dword top
  bogus o64 loop strict dword short top
  bogus o64 loop strict dword near top
  bogus loop strict qword top
  bogus loop strict qword short top
  bogus loop strict qword near top
  bogus o16 loop strict qword top
  bogus o16 loop strict qword short top
  bogus o16 loop strict qword near top
  bogus o32 loop strict qword top
  bogus o32 loop strict qword short top
  bogus o32 loop strict qword near top
  bogus o64 loop strict qword top
  bogus o64 loop strict qword short top
  bogus o64 loop strict qword near top
  bogus loop there
  bogus loop short there
  bogus loop near there
  bogus o16 loop there
  bogus o16 loop short there
  bogus o16 loop near there
  bogus o32 loop there
  bogus o32 loop short there
  bogus o32 loop near there
  bogus o64 loop there
  bogus o64 loop short there
  bogus o64 loop near there
  bogus loop byte there
  bogus loop byte short there
  bogus loop byte near there
  bogus o16 loop byte there
  bogus o16 loop byte short there
  bogus o16 loop byte near there
  bogus o32 loop byte there
  bogus o32 loop byte short there
  bogus o32 loop byte near there
  bogus o64 loop byte there
  bogus o64 loop byte short there
  bogus o64 loop byte near there
  bogus loop word there
  bogus loop word short there
  bogus loop word near there
  bogus o16 loop word there
  bogus o16 loop word short there
  bogus o16 loop word near there
  bogus o32 loop word there
  bogus o32 loop word short there
  bogus o32 loop word near there
  bogus o64 loop word there
  bogus o64 loop word short there
  bogus o64 loop word near there
  bogus loop dword there
  bogus loop dword short there
  bogus loop dword near there
  bogus o16 loop dword there
  bogus o16 loop dword short there
  bogus o16 loop dword near there
  bogus o32 loop dword there
  bogus o32 loop dword short there
  bogus o32 loop dword near there
  bogus o64 loop dword there
  bogus o64 loop dword short there
  bogus o64 loop dword near there
  bogus loop qword there
  bogus loop qword short there
  bogus loop qword near there
  bogus o16 loop qword there
  bogus o16 loop qword short there
  bogus o16 loop qword near there
  bogus o32 loop qword there
  bogus o32 loop qword short there
  bogus o32 loop qword near there
  bogus o64 loop qword there
  bogus o64 loop qword short there
  bogus o64 loop qword near there
  bogus loop strict there
  bogus loop strict short there
  bogus loop strict near there
  bogus o16 loop strict there
  bogus o16 loop strict short there
  bogus o16 loop strict near there
  bogus o32 loop strict there
  bogus o32 loop strict short there
  bogus o32 loop strict near there
  bogus o64 loop strict there
  bogus o64 loop strict short there
  bogus o64 loop strict near there
  bogus loop strict byte there
  bogus loop strict byte short there
  bogus loop strict byte near there
  bogus o16 loop strict byte there
  bogus o16 loop strict byte short there
  bogus o16 loop strict byte near there
  bogus o32 loop strict byte there
  bogus o32 loop strict byte short there
  bogus o32 loop strict byte near there
  bogus o64 loop strict byte there
  bogus o64 loop strict byte short there
  bogus o64 loop strict byte near there
  bogus loop strict word there
  bogus loop strict word short there
  bogus loop strict word near there
  bogus o16 loop strict word there
  bogus o16 loop strict word short there
  bogus o16 loop strict word near there
  bogus o32 loop strict word there
  bogus o32 loop strict word short there
  bogus o32 loop strict word near there
  bogus o64 loop strict word there
  bogus o64 loop strict word short there
  bogus o64 loop strict word near there
  bogus loop strict dword there
  bogus loop strict dword short there
  bogus loop strict dword near there
  bogus o16 loop strict dword there
  bogus o16 loop strict dword short there
  bogus o16 loop strict dword near there
  bogus o32 loop strict dword there
  bogus o32 loop strict dword short there
  bogus o32 loop strict dword near there
  bogus o64 loop strict dword there
  bogus o64 loop strict dword short there
  bogus o64 loop strict dword near there
  bogus loop strict qword there
  bogus loop strict qword short there
  bogus loop strict qword near there
  bogus o16 loop strict qword there
  bogus o16 loop strict qword short there
  bogus o16 loop strict qword near there
  bogus o32 loop strict qword there
  bogus o32 loop strict qword short there
  bogus o32 loop strict qword near there
  bogus o64 loop strict qword there
  bogus o64 loop strict qword short there
  bogus o64 loop strict qword near there
here_loope:
        loope $
        loope short $
  bogus loope near $
  noq   o16 loope $
  noq   o16 loope short $
  bogus o16 loope near $
  noq   o32 loope $
  noq   o32 loope short $
  bogus o32 loope near $
  nowd  o64 loope $
  nowd  o64 loope short $
  bogus o64 loope near $
  bogus loope byte $
  bogus loope byte short $
  bogus loope byte near $
  bogus o16 loope byte $
  bogus o16 loope byte short $
  bogus o16 loope byte near $
  bogus o32 loope byte $
  bogus o32 loope byte short $
  bogus o32 loope byte near $
  bogus o64 loope byte $
  bogus o64 loope byte short $
  bogus o64 loope byte near $
  noq   loope word $
  noq   loope word short $
  bogus loope word near $
  noq   o16 loope word $
  noq   o16 loope word short $
  bogus o16 loope word near $
  bogus o32 loope word $
  bogus o32 loope word short $
  bogus o32 loope word near $
  bogus o64 loope word $
  bogus o64 loope word short $
  bogus o64 loope word near $
  noq   loope dword $
  noq   loope dword short $
  bogus loope dword near $
  bogus o16 loope dword $
  bogus o16 loope dword short $
  bogus o16 loope dword near $
  noq   o32 loope dword $
  noq   o32 loope dword short $
  bogus o32 loope dword near $
  bogus o64 loope dword $
  bogus o64 loope dword short $
  bogus o64 loope dword near $
  nowd  loope qword $
  nowd  loope qword short $
  bogus loope qword near $
  bogus o16 loope qword $
  bogus o16 loope qword short $
  bogus o16 loope qword near $
  bogus o32 loope qword $
  bogus o32 loope qword short $
  bogus o32 loope qword near $
  nowd  o64 loope qword $
  nowd  o64 loope qword short $
  bogus o64 loope qword near $
        loope strict $
        loope strict short $
  bogus loope strict near $
  noq   o16 loope strict $
  noq   o16 loope strict short $
  bogus o16 loope strict near $
  noq   o32 loope strict $
  noq   o32 loope strict short $
  bogus o32 loope strict near $
  nowd  o64 loope strict $
  nowd  o64 loope strict short $
  bogus o64 loope strict near $
  bogus loope strict byte $
  bogus loope strict byte short $
  bogus loope strict byte near $
  bogus o16 loope strict byte $
  bogus o16 loope strict byte short $
  bogus o16 loope strict byte near $
  bogus o32 loope strict byte $
  bogus o32 loope strict byte short $
  bogus o32 loope strict byte near $
  bogus o64 loope strict byte $
  bogus o64 loope strict byte short $
  bogus o64 loope strict byte near $
  noq   loope strict word $
  noq   loope strict word short $
  bogus loope strict word near $
  noq   o16 loope strict word $
  noq   o16 loope strict word short $
  bogus o16 loope strict word near $
  bogus o32 loope strict word $
  bogus o32 loope strict word short $
  bogus o32 loope strict word near $
  bogus o64 loope strict word $
  bogus o64 loope strict word short $
  bogus o64 loope strict word near $
  noq   loope strict dword $
  noq   loope strict dword short $
  bogus loope strict dword near $
  bogus o16 loope strict dword $
  bogus o16 loope strict dword short $
  bogus o16 loope strict dword near $
  noq   o32 loope strict dword $
  noq   o32 loope strict dword short $
  bogus o32 loope strict dword near $
  bogus o64 loope strict dword $
  bogus o64 loope strict dword short $
  bogus o64 loope strict dword near $
  nowd  loope strict qword $
  nowd  loope strict qword short $
  bogus loope strict qword near $
  bogus o16 loope strict qword $
  bogus o16 loope strict qword short $
  bogus o16 loope strict qword near $
  bogus o32 loope strict qword $
  bogus o32 loope strict qword short $
  bogus o32 loope strict qword near $
  nowd  o64 loope strict qword $
  nowd  o64 loope strict qword short $
  bogus o64 loope strict qword near $
  bogus loope top
  bogus loope short top
  bogus loope near top
  bogus o16 loope top
  bogus o16 loope short top
  bogus o16 loope near top
  bogus o32 loope top
  bogus o32 loope short top
  bogus o32 loope near top
  bogus o64 loope top
  bogus o64 loope short top
  bogus o64 loope near top
  bogus loope byte top
  bogus loope byte short top
  bogus loope byte near top
  bogus o16 loope byte top
  bogus o16 loope byte short top
  bogus o16 loope byte near top
  bogus o32 loope byte top
  bogus o32 loope byte short top
  bogus o32 loope byte near top
  bogus o64 loope byte top
  bogus o64 loope byte short top
  bogus o64 loope byte near top
  bogus loope word top
  bogus loope word short top
  bogus loope word near top
  bogus o16 loope word top
  bogus o16 loope word short top
  bogus o16 loope word near top
  bogus o32 loope word top
  bogus o32 loope word short top
  bogus o32 loope word near top
  bogus o64 loope word top
  bogus o64 loope word short top
  bogus o64 loope word near top
  bogus loope dword top
  bogus loope dword short top
  bogus loope dword near top
  bogus o16 loope dword top
  bogus o16 loope dword short top
  bogus o16 loope dword near top
  bogus o32 loope dword top
  bogus o32 loope dword short top
  bogus o32 loope dword near top
  bogus o64 loope dword top
  bogus o64 loope dword short top
  bogus o64 loope dword near top
  bogus loope qword top
  bogus loope qword short top
  bogus loope qword near top
  bogus o16 loope qword top
  bogus o16 loope qword short top
  bogus o16 loope qword near top
  bogus o32 loope qword top
  bogus o32 loope qword short top
  bogus o32 loope qword near top
  bogus o64 loope qword top
  bogus o64 loope qword short top
  bogus o64 loope qword near top
  bogus loope strict top
  bogus loope strict short top
  bogus loope strict near top
  bogus o16 loope strict top
  bogus o16 loope strict short top
  bogus o16 loope strict near top
  bogus o32 loope strict top
  bogus o32 loope strict short top
  bogus o32 loope strict near top
  bogus o64 loope strict top
  bogus o64 loope strict short top
  bogus o64 loope strict near top
  bogus loope strict byte top
  bogus loope strict byte short top
  bogus loope strict byte near top
  bogus o16 loope strict byte top
  bogus o16 loope strict byte short top
  bogus o16 loope strict byte near top
  bogus o32 loope strict byte top
  bogus o32 loope strict byte short top
  bogus o32 loope strict byte near top
  bogus o64 loope strict byte top
  bogus o64 loope strict byte short top
  bogus o64 loope strict byte near top
  bogus loope strict word top
  bogus loope strict word short top
  bogus loope strict word near top
  bogus o16 loope strict word top
  bogus o16 loope strict word short top
  bogus o16 loope strict word near top
  bogus o32 loope strict word top
  bogus o32 loope strict word short top
  bogus o32 loope strict word near top
  bogus o64 loope strict word top
  bogus o64 loope strict word short top
  bogus o64 loope strict word near top
  bogus loope strict dword top
  bogus loope strict dword short top
  bogus loope strict dword near top
  bogus o16 loope strict dword top
  bogus o16 loope strict dword short top
  bogus o16 loope strict dword near top
  bogus o32 loope strict dword top
  bogus o32 loope strict dword short top
  bogus o32 loope strict dword near top
  bogus o64 loope strict dword top
  bogus o64 loope strict dword short top
  bogus o64 loope strict dword near top
  bogus loope strict qword top
  bogus loope strict qword short top
  bogus loope strict qword near top
  bogus o16 loope strict qword top
  bogus o16 loope strict qword short top
  bogus o16 loope strict qword near top
  bogus o32 loope strict qword top
  bogus o32 loope strict qword short top
  bogus o32 loope strict qword near top
  bogus o64 loope strict qword top
  bogus o64 loope strict qword short top
  bogus o64 loope strict qword near top
  bogus loope there
  bogus loope short there
  bogus loope near there
  bogus o16 loope there
  bogus o16 loope short there
  bogus o16 loope near there
  bogus o32 loope there
  bogus o32 loope short there
  bogus o32 loope near there
  bogus o64 loope there
  bogus o64 loope short there
  bogus o64 loope near there
  bogus loope byte there
  bogus loope byte short there
  bogus loope byte near there
  bogus o16 loope byte there
  bogus o16 loope byte short there
  bogus o16 loope byte near there
  bogus o32 loope byte there
  bogus o32 loope byte short there
  bogus o32 loope byte near there
  bogus o64 loope byte there
  bogus o64 loope byte short there
  bogus o64 loope byte near there
  bogus loope word there
  bogus loope word short there
  bogus loope word near there
  bogus o16 loope word there
  bogus o16 loope word short there
  bogus o16 loope word near there
  bogus o32 loope word there
  bogus o32 loope word short there
  bogus o32 loope word near there
  bogus o64 loope word there
  bogus o64 loope word short there
  bogus o64 loope word near there
  bogus loope dword there
  bogus loope dword short there
  bogus loope dword near there
  bogus o16 loope dword there
  bogus o16 loope dword short there
  bogus o16 loope dword near there
  bogus o32 loope dword there
  bogus o32 loope dword short there
  bogus o32 loope dword near there
  bogus o64 loope dword there
  bogus o64 loope dword short there
  bogus o64 loope dword near there
  bogus loope qword there
  bogus loope qword short there
  bogus loope qword near there
  bogus o16 loope qword there
  bogus o16 loope qword short there
  bogus o16 loope qword near there
  bogus o32 loope qword there
  bogus o32 loope qword short there
  bogus o32 loope qword near there
  bogus o64 loope qword there
  bogus o64 loope qword short there
  bogus o64 loope qword near there
  bogus loope strict there
  bogus loope strict short there
  bogus loope strict near there
  bogus o16 loope strict there
  bogus o16 loope strict short there
  bogus o16 loope strict near there
  bogus o32 loope strict there
  bogus o32 loope strict short there
  bogus o32 loope strict near there
  bogus o64 loope strict there
  bogus o64 loope strict short there
  bogus o64 loope strict near there
  bogus loope strict byte there
  bogus loope strict byte short there
  bogus loope strict byte near there
  bogus o16 loope strict byte there
  bogus o16 loope strict byte short there
  bogus o16 loope strict byte near there
  bogus o32 loope strict byte there
  bogus o32 loope strict byte short there
  bogus o32 loope strict byte near there
  bogus o64 loope strict byte there
  bogus o64 loope strict byte short there
  bogus o64 loope strict byte near there
  bogus loope strict word there
  bogus loope strict word short there
  bogus loope strict word near there
  bogus o16 loope strict word there
  bogus o16 loope strict word short there
  bogus o16 loope strict word near there
  bogus o32 loope strict word there
  bogus o32 loope strict word short there
  bogus o32 loope strict word near there
  bogus o64 loope strict word there
  bogus o64 loope strict word short there
  bogus o64 loope strict word near there
  bogus loope strict dword there
  bogus loope strict dword short there
  bogus loope strict dword near there
  bogus o16 loope strict dword there
  bogus o16 loope strict dword short there
  bogus o16 loope strict dword near there
  bogus o32 loope strict dword there
  bogus o32 loope strict dword short there
  bogus o32 loope strict dword near there
  bogus o64 loope strict dword there
  bogus o64 loope strict dword short there
  bogus o64 loope strict dword near there
  bogus loope strict qword there
  bogus loope strict qword short there
  bogus loope strict qword near there
  bogus o16 loope strict qword there
  bogus o16 loope strict qword short there
  bogus o16 loope strict qword near there
  bogus o32 loope strict qword there
  bogus o32 loope strict qword short there
  bogus o32 loope strict qword near there
  bogus o64 loope strict qword there
  bogus o64 loope strict qword short there
  bogus o64 loope strict qword near there
here_loopne:
        loopne $
        loopne short $
  bogus loopne near $
  noq   o16 loopne $
  noq   o16 loopne short $
  bogus o16 loopne near $
  noq   o32 loopne $
  noq   o32 loopne short $
  bogus o32 loopne near $
  nowd  o64 loopne $
  nowd  o64 loopne short $
  bogus o64 loopne near $
  bogus loopne byte $
  bogus loopne byte short $
  bogus loopne byte near $
  bogus o16 loopne byte $
  bogus o16 loopne byte short $
  bogus o16 loopne byte near $
  bogus o32 loopne byte $
  bogus o32 loopne byte short $
  bogus o32 loopne byte near $
  bogus o64 loopne byte $
  bogus o64 loopne byte short $
  bogus o64 loopne byte near $
  noq   loopne word $
  noq   loopne word short $
  bogus loopne word near $
  noq   o16 loopne word $
  noq   o16 loopne word short $
  bogus o16 loopne word near $
  bogus o32 loopne word $
  bogus o32 loopne word short $
  bogus o32 loopne word near $
  bogus o64 loopne word $
  bogus o64 loopne word short $
  bogus o64 loopne word near $
  noq   loopne dword $
  noq   loopne dword short $
  bogus loopne dword near $
  bogus o16 loopne dword $
  bogus o16 loopne dword short $
  bogus o16 loopne dword near $
  noq   o32 loopne dword $
  noq   o32 loopne dword short $
  bogus o32 loopne dword near $
  bogus o64 loopne dword $
  bogus o64 loopne dword short $
  bogus o64 loopne dword near $
  nowd  loopne qword $
  nowd  loopne qword short $
  bogus loopne qword near $
  bogus o16 loopne qword $
  bogus o16 loopne qword short $
  bogus o16 loopne qword near $
  bogus o32 loopne qword $
  bogus o32 loopne qword short $
  bogus o32 loopne qword near $
  nowd  o64 loopne qword $
  nowd  o64 loopne qword short $
  bogus o64 loopne qword near $
        loopne strict $
        loopne strict short $
  bogus loopne strict near $
  noq   o16 loopne strict $
  noq   o16 loopne strict short $
  bogus o16 loopne strict near $
  noq   o32 loopne strict $
  noq   o32 loopne strict short $
  bogus o32 loopne strict near $
  nowd  o64 loopne strict $
  nowd  o64 loopne strict short $
  bogus o64 loopne strict near $
  bogus loopne strict byte $
  bogus loopne strict byte short $
  bogus loopne strict byte near $
  bogus o16 loopne strict byte $
  bogus o16 loopne strict byte short $
  bogus o16 loopne strict byte near $
  bogus o32 loopne strict byte $
  bogus o32 loopne strict byte short $
  bogus o32 loopne strict byte near $
  bogus o64 loopne strict byte $
  bogus o64 loopne strict byte short $
  bogus o64 loopne strict byte near $
  noq   loopne strict word $
  noq   loopne strict word short $
  bogus loopne strict word near $
  noq   o16 loopne strict word $
  noq   o16 loopne strict word short $
  bogus o16 loopne strict word near $
  bogus o32 loopne strict word $
  bogus o32 loopne strict word short $
  bogus o32 loopne strict word near $
  bogus o64 loopne strict word $
  bogus o64 loopne strict word short $
  bogus o64 loopne strict word near $
  noq   loopne strict dword $
  noq   loopne strict dword short $
  bogus loopne strict dword near $
  bogus o16 loopne strict dword $
  bogus o16 loopne strict dword short $
  bogus o16 loopne strict dword near $
  noq   o32 loopne strict dword $
  noq   o32 loopne strict dword short $
  bogus o32 loopne strict dword near $
  bogus o64 loopne strict dword $
  bogus o64 loopne strict dword short $
  bogus o64 loopne strict dword near $
  nowd  loopne strict qword $
  nowd  loopne strict qword short $
  bogus loopne strict qword near $
  bogus o16 loopne strict qword $
  bogus o16 loopne strict qword short $
  bogus o16 loopne strict qword near $
  bogus o32 loopne strict qword $
  bogus o32 loopne strict qword short $
  bogus o32 loopne strict qword near $
  nowd  o64 loopne strict qword $
  nowd  o64 loopne strict qword short $
  bogus o64 loopne strict qword near $
  bogus loopne top
  bogus loopne short top
  bogus loopne near top
  bogus o16 loopne top
  bogus o16 loopne short top
  bogus o16 loopne near top
  bogus o32 loopne top
  bogus o32 loopne short top
  bogus o32 loopne near top
  bogus o64 loopne top
  bogus o64 loopne short top
  bogus o64 loopne near top
  bogus loopne byte top
  bogus loopne byte short top
  bogus loopne byte near top
  bogus o16 loopne byte top
  bogus o16 loopne byte short top
  bogus o16 loopne byte near top
  bogus o32 loopne byte top
  bogus o32 loopne byte short top
  bogus o32 loopne byte near top
  bogus o64 loopne byte top
  bogus o64 loopne byte short top
  bogus o64 loopne byte near top
  bogus loopne word top
  bogus loopne word short top
  bogus loopne word near top
  bogus o16 loopne word top
  bogus o16 loopne word short top
  bogus o16 loopne word near top
  bogus o32 loopne word top
  bogus o32 loopne word short top
  bogus o32 loopne word near top
  bogus o64 loopne word top
  bogus o64 loopne word short top
  bogus o64 loopne word near top
  bogus loopne dword top
  bogus loopne dword short top
  bogus loopne dword near top
  bogus o16 loopne dword top
  bogus o16 loopne dword short top
  bogus o16 loopne dword near top
  bogus o32 loopne dword top
  bogus o32 loopne dword short top
  bogus o32 loopne dword near top
  bogus o64 loopne dword top
  bogus o64 loopne dword short top
  bogus o64 loopne dword near top
  bogus loopne qword top
  bogus loopne qword short top
  bogus loopne qword near top
  bogus o16 loopne qword top
  bogus o16 loopne qword short top
  bogus o16 loopne qword near top
  bogus o32 loopne qword top
  bogus o32 loopne qword short top
  bogus o32 loopne qword near top
  bogus o64 loopne qword top
  bogus o64 loopne qword short top
  bogus o64 loopne qword near top
  bogus loopne strict top
  bogus loopne strict short top
  bogus loopne strict near top
  bogus o16 loopne strict top
  bogus o16 loopne strict short top
  bogus o16 loopne strict near top
  bogus o32 loopne strict top
  bogus o32 loopne strict short top
  bogus o32 loopne strict near top
  bogus o64 loopne strict top
  bogus o64 loopne strict short top
  bogus o64 loopne strict near top
  bogus loopne strict byte top
  bogus loopne strict byte short top
  bogus loopne strict byte near top
  bogus o16 loopne strict byte top
  bogus o16 loopne strict byte short top
  bogus o16 loopne strict byte near top
  bogus o32 loopne strict byte top
  bogus o32 loopne strict byte short top
  bogus o32 loopne strict byte near top
  bogus o64 loopne strict byte top
  bogus o64 loopne strict byte short top
  bogus o64 loopne strict byte near top
  bogus loopne strict word top
  bogus loopne strict word short top
  bogus loopne strict word near top
  bogus o16 loopne strict word top
  bogus o16 loopne strict word short top
  bogus o16 loopne strict word near top
  bogus o32 loopne strict word top
  bogus o32 loopne strict word short top
  bogus o32 loopne strict word near top
  bogus o64 loopne strict word top
  bogus o64 loopne strict word short top
  bogus o64 loopne strict word near top
  bogus loopne strict dword top
  bogus loopne strict dword short top
  bogus loopne strict dword near top
  bogus o16 loopne strict dword top
  bogus o16 loopne strict dword short top
  bogus o16 loopne strict dword near top
  bogus o32 loopne strict dword top
  bogus o32 loopne strict dword short top
  bogus o32 loopne strict dword near top
  bogus o64 loopne strict dword top
  bogus o64 loopne strict dword short top
  bogus o64 loopne strict dword near top
  bogus loopne strict qword top
  bogus loopne strict qword short top
  bogus loopne strict qword near top
  bogus o16 loopne strict qword top
  bogus o16 loopne strict qword short top
  bogus o16 loopne strict qword near top
  bogus o32 loopne strict qword top
  bogus o32 loopne strict qword short top
  bogus o32 loopne strict qword near top
  bogus o64 loopne strict qword top
  bogus o64 loopne strict qword short top
  bogus o64 loopne strict qword near top
  bogus loopne there
  bogus loopne short there
  bogus loopne near there
  bogus o16 loopne there
  bogus o16 loopne short there
  bogus o16 loopne near there
  bogus o32 loopne there
  bogus o32 loopne short there
  bogus o32 loopne near there
  bogus o64 loopne there
  bogus o64 loopne short there
  bogus o64 loopne near there
  bogus loopne byte there
  bogus loopne byte short there
  bogus loopne byte near there
  bogus o16 loopne byte there
  bogus o16 loopne byte short there
  bogus o16 loopne byte near there
  bogus o32 loopne byte there
  bogus o32 loopne byte short there
  bogus o32 loopne byte near there
  bogus o64 loopne byte there
  bogus o64 loopne byte short there
  bogus o64 loopne byte near there
  bogus loopne word there
  bogus loopne word short there
  bogus loopne word near there
  bogus o16 loopne word there
  bogus o16 loopne word short there
  bogus o16 loopne word near there
  bogus o32 loopne word there
  bogus o32 loopne word short there
  bogus o32 loopne word near there
  bogus o64 loopne word there
  bogus o64 loopne word short there
  bogus o64 loopne word near there
  bogus loopne dword there
  bogus loopne dword short there
  bogus loopne dword near there
  bogus o16 loopne dword there
  bogus o16 loopne dword short there
  bogus o16 loopne dword near there
  bogus o32 loopne dword there
  bogus o32 loopne dword short there
  bogus o32 loopne dword near there
  bogus o64 loopne dword there
  bogus o64 loopne dword short there
  bogus o64 loopne dword near there
  bogus loopne qword there
  bogus loopne qword short there
  bogus loopne qword near there
  bogus o16 loopne qword there
  bogus o16 loopne qword short there
  bogus o16 loopne qword near there
  bogus o32 loopne qword there
  bogus o32 loopne qword short there
  bogus o32 loopne qword near there
  bogus o64 loopne qword there
  bogus o64 loopne qword short there
  bogus o64 loopne qword near there
  bogus loopne strict there
  bogus loopne strict short there
  bogus loopne strict near there
  bogus o16 loopne strict there
  bogus o16 loopne strict short there
  bogus o16 loopne strict near there
  bogus o32 loopne strict there
  bogus o32 loopne strict short there
  bogus o32 loopne strict near there
  bogus o64 loopne strict there
  bogus o64 loopne strict short there
  bogus o64 loopne strict near there
  bogus loopne strict byte there
  bogus loopne strict byte short there
  bogus loopne strict byte near there
  bogus o16 loopne strict byte there
  bogus o16 loopne strict byte short there
  bogus o16 loopne strict byte near there
  bogus o32 loopne strict byte there
  bogus o32 loopne strict byte short there
  bogus o32 loopne strict byte near there
  bogus o64 loopne strict byte there
  bogus o64 loopne strict byte short there
  bogus o64 loopne strict byte near there
  bogus loopne strict word there
  bogus loopne strict word short there
  bogus loopne strict word near there
  bogus o16 loopne strict word there
  bogus o16 loopne strict word short there
  bogus o16 loopne strict word near there
  bogus o32 loopne strict word there
  bogus o32 loopne strict word short there
  bogus o32 loopne strict word near there
  bogus o64 loopne strict word there
  bogus o64 loopne strict word short there
  bogus o64 loopne strict word near there
  bogus loopne strict dword there
  bogus loopne strict dword short there
  bogus loopne strict dword near there
  bogus o16 loopne strict dword there
  bogus o16 loopne strict dword short there
  bogus o16 loopne strict dword near there
  bogus o32 loopne strict dword there
  bogus o32 loopne strict dword short there
  bogus o32 loopne strict dword near there
  bogus o64 loopne strict dword there
  bogus o64 loopne strict dword short there
  bogus o64 loopne strict dword near there
  bogus loopne strict qword there
  bogus loopne strict qword short there
  bogus loopne strict qword near there
  bogus o16 loopne strict qword there
  bogus o16 loopne strict qword short there
  bogus o16 loopne strict qword near there
  bogus o32 loopne strict qword there
  bogus o32 loopne strict qword short there
  bogus o32 loopne strict qword near there
  bogus o64 loopne strict qword there
  bogus o64 loopne strict qword short there
  bogus o64 loopne strict qword near there

	section text2
there:
	ret
