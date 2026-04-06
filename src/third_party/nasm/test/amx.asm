	bits 64

%macro amx 1
  %define treg tmm %+ %1

	ldtilecfg [rsi]
	sttilecfg [rdi]

	tilezero treg

	tileloadd treg, [rax]
	tileloadd treg, [rax,rdx]
	tileloadd treg, [rax,rdx*2]

	tileloaddt1 treg, [rax]
	tileloaddt1 treg, [rax,rdx]
	tileloaddt1 treg, [rax,rdx*2]

	tdpbf16ps treg, treg, treg
	tdpbssd treg, treg, treg
	tdpbusd treg, treg, treg
	tdpbsud treg, treg, treg
	tdpbuud treg, treg, treg

	tilestored [rax], treg
	tilestored [rax,rdx], treg
	tilestored [rax,rdx*2], treg

	tilerelease
%endmacro

%assign n 0
  %rep 8
	amx n
    %assign n n+1
  %endrep
