	bits 64
	add eax,edx
	{rex} add eax,edx
	add al,dl
	{rex} add al,dl
	add ah,dl
	comisd xmm0,xmm1
	{rex} comisd xmm0,xmm1
	vcomisd xmm0,xmm31
	vcomisd xmm0,xmm1
	{vex} vcomisd xmm0,xmm1
	{vex2} vcomisd xmm0,xmm1
	{vex3} vcomisd xmm0,xmm1
	{evex} vcomisd xmm0,xmm1
	{vex2} vcomisd xmm0,xmm1
	{vex3} vcomisd xmm0,xmm1
	{evex} vcomisd xmm0,xmm1
	{vex} vcomisd xmm0,[r8+rax*1]
	{vex3} vcomisd xmm0,[r8+rax*1]
	{evex} vcomisd xmm0,[r8+rax*1]
	{vex} vcomisd xmm0,[rax+r8*2]
	{vex3} vcomisd xmm0,[rax+r8*2]
	{evex} vcomisd xmm0,[rax+r8*2]

	;; These errors may be caught in different passes, so
	;; some shadows the others...
%ifdef ERROR
  %if ERROR <= 1
	{vex2} vcomisd xmm0,[rax+r8*2]
	{rex} add ah,dl
	bits 32
	mov eax,[r8d]
  %endif
  %if ERROR <= 2
	{rex} vcomisd xmm0,xmm1
	{vex} add eax,edx
	{vex3} add eax,edx
  %endif
%endif
