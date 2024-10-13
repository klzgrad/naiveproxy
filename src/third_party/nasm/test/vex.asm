	bits 64
	vcomisd xmm0,xmm31
	vcomisd xmm0,xmm1
	{vex2} vcomisd xmm0,xmm1
	{vex3} vcomisd xmm0,xmm1
	{evex} vcomisd xmm0,xmm1
%ifdef ERROR
	{vex3} add eax,edx
%endif
