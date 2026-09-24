	bits 64

%macro cxx 1
	%1 dl,sil
	%1 dx,si
	%1 edx,esi
	%1 rdx,rsi
	%1 rdx,r14
	%1 rdx,r30
	%1 cl,[rbx]
	%1 cx,[rbx]
	%1 ecx,[rbx]
	%1 rcx,[rbx]
	%1 cl, 0x10
	%1 cx, 0x10
	%1 cx, 0x1000
	%1 ecx, 0x10
	%1 ecx, 0x1000
	%1 rcx, 0x10
	%1 rcx, 0x1000
%endmacro

%macro cxdfv 1-2+
	cxx {%1 {dfv=%2}}
%endmacro

%macro cx 1
	cxdfv %1
	cxdfv %1,cf
	cxdfv %1,zf
	cxdfv %1,cf,zf
	cxdfv %1,sf
	cxdfv %1,sf,cf
	cxdfv %1,sf,zf
	cxdfv %1,sf,cf,zf
	cxdfv %1,of
	cxdfv %1,of,cf
	cxdfv %1,of,zf
	cxdfv %1,of,cf,zf
	cxdfv %1,of,sf
	cxdfv %1,of,sf,cf
	cxdfv %1,of,sf,zf
	cxdfv %1,of,sf,cf,zf
%endmacro

%macro cxi 1
 %assign n 0
  %rep 16
	cxx {%1 n,}
  %assign n n+1
 %endrep
%endmacro

%macro mkequ 1-2+
	dfv%1	equ {dfv=%2}
%endmacro

%macro cxe 1
 %assign n 0
  %rep 16
	cxx {%1 dfv %+ n,}
  %assign n n+1
 %endrep
%endmacro


%macro c 2
	%1 %{2}o
	%1 %{2}no
	%1 %{2}c
	%1 %{2}nc
	%1 %{2}z
	%1 %{2}nz
	%1 %{2}na
	%1 %{2}a
	%1 %{2}s
	%1 %{2}ns
	%1 %{2}f
	%1 %{2}t
	%1 %{2}l
	%1 %{2}nl
	%1 %{2}ng
	%1 %{2}g
%endmacro

	mkequ  0
	mkequ  1,cf
	mkequ  2,zf
	mkequ  3,cf,zf
	mkequ  4,sf
	mkequ  5,sf,cf
	mkequ  6,sf,zf
	mkequ  7,sf,cf,zf
	mkequ  8,of
	mkequ  9,of,cf
	mkequ 10,of,zf
	mkequ 11,of,cf,zf
	mkequ 12,of,sf
	mkequ 13,of,sf,cf
	mkequ 14,of,sf,zf
	mkequ 15,of,sf,cf,zf

	c cxdfv, ccmp
	c cxi, ccmp
	c cxe, ccmp

	nop
	nop
	nop

	db 0xb0, {dfv=of,sf}&~{dfv=cf,sf}
	mov al, ({dfv=of,sf}&~{dfv=cf,sf})
