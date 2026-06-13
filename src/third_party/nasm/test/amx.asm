	bits 64

%macro amx 3
  %define treg tmm %+ %1
  %define treg2 tmm %+ %2
  %define treg3 tmm %+ %3
  %define zreg zmm %+ %1

	ldtilecfg [rsi]									;AMX_TILE
	sttilecfg [rdi]									;AMX_TILE

	tilezero			treg						;AMX_TILE

	tileloadd			treg, [rax]					;AMX_TILE
	tileloadd			treg, [rax+rdx]				;AMX_TILE
	tileloadd			treg, [rax+rdx*2]			;AMX_TILE

	tileloaddt1			treg, [rax]					;AMX_TILE
	tileloaddt1			treg, [rax+rdx]				;AMX_TILE
	tileloaddt1			treg, [rax+rdx*2]			;AMX_TILE

	tileloaddrs			treg, [rax]					;AMX-MOVRS
	tileloaddrs			treg, [rax+rdx]				;AMX-MOVRS
	tileloaddrs			treg, [rax+rdx*2]			;AMX-MOVRS

	tileloaddrst1		treg, [rax]					;AMX-MOVRS
	tileloaddrst1		treg, [rax+rdx]				;AMX-MOVRS
	tileloaddrst1		treg, [rax+rdx*2]			;AMX-MOVRS

	tdpbf16ps			treg, treg2, treg3			;AMX-BF16
	tdpbssd				treg, treg2, treg3			;AMX_INT8
	tdpbusd				treg, treg2, treg3			;AMX_INT8
	tdpbsud				treg, treg2, treg3			;AMX_INT8
	tdpbuud				treg, treg2, treg3			;AMX_INT8
	tdpfp16ps			treg, treg2, treg3			;AMX-FP16
	tcmmimfp16ps		treg, treg2, treg3			;AMX-COMPLEX
	tcmmrlfp16ps		treg, treg2, treg3			;AMX-COMPLEX

	tmmultf32ps			treg, treg2, treg3			;AMX_TF32

	tdpbf8ps			treg, treg2, treg3			;AMX-FP8
	tdpbhf8ps			treg, treg2, treg3			;AMX-FP8
	tdphbf8ps			treg, treg2, treg3			;AMX-FP8
	tdphf8ps			treg, treg2, treg3			;AMX-FP8

	tcvtrowd2ps			zreg, treg, eax				;AMX-AVX512
	tcvtrowd2ps			zreg, treg, %1				;AMX-AVX512
	tcvtrowps2bf16h		zreg, treg, eax				;AMX-AVX512
	tcvtrowps2bf16h		zreg, treg, %1				;AMX-AVX512
	tcvtrowps2bf16l		zreg, treg, eax				;AMX-AVX512
	tcvtrowps2bf16l		zreg, treg, %1				;AMX-AVX512
	tcvtrowps2phh		zreg, treg, eax				;AMX-AVX512
	tcvtrowps2phh		zreg, treg, %1				;AMX-AVX512
	tcvtrowps2phl		zreg, treg, eax				;AMX-AVX512
	tcvtrowps2phl		zreg, treg, %1				;AMX-AVX512
	tilemovrow			zreg, treg, eax				;AMX-AVX512
	tilemovrow			zreg, treg, %1				;AMX-AVX512

	; All the 16 AMX-TRANSPOSE instructions were removed from the 59th edition of
	; "Intel Architecture Instruction Set Extensions and Future Features Programming Reference"
	; September 2025, 319433-059
	; Similar to PCOMMIT, they are tagged as 'NEVER'

	[warning -obsolete-removed]
	t2rpntlvwz0			treg, [rax]					;AMX-TRANSPOSE
	t2rpntlvwz0			treg, [rax+rdx]				;AMX-TRANSPOSE
	t2rpntlvwz0			treg, [rax+rdx*2]			;AMX-TRANSPOSE

	t2rpntlvwz0t1			treg, [rax]					;AMX-TRANSPOSE
	t2rpntlvwz0t1			treg, [rax+rdx]				;AMX-TRANSPOSE
	t2rpntlvwz0t1			treg, [rax+rdx*2]			;AMX-TRANSPOSE

	t2rpntlvwz1			treg, [rax]					;AMX-TRANSPOSE
	t2rpntlvwz1			treg, [rax+rdx]				;AMX-TRANSPOSE
	t2rpntlvwz1			treg, [rax+rdx*2]			;AMX-TRANSPOSE

	t2rpntlvwz1t1			treg, [rax]					;AMX-TRANSPOSE
	t2rpntlvwz1t1			treg, [rax+rdx]				;AMX-TRANSPOSE
	t2rpntlvwz1t1			treg, [rax+rdx*2]			;AMX-TRANSPOSE

	ttransposed			treg, treg					;AMX-TRANSPOSE

	t2rpntlvwz0rs			treg, [rax]					;AMX-TRANSPOSE + AMX-MOVRS
	t2rpntlvwz0rs			treg, [rax+rdx]				;AMX-TRANSPOSE + AMX-MOVRS
	t2rpntlvwz0rs			treg, [rax+rdx*2]			;AMX-TRANSPOSE + AMX-MOVRS

	t2rpntlvwz0rst1		treg, [rax]					;AMX-TRANSPOSE + AMX-MOVRS
	t2rpntlvwz0rst1		treg, [rax+rdx]				;AMX-TRANSPOSE + AMX-MOVRS
	t2rpntlvwz0rst1		treg, [rax+rdx*2]			;AMX-TRANSPOSE + AMX-MOVRS

	t2rpntlvwz1rs			treg, [rax]					;AMX-TRANSPOSE + AMX-MOVRS
	t2rpntlvwz1rs			treg, [rax+rdx]				;AMX-TRANSPOSE + AMX-MOVRS
	t2rpntlvwz1rs			treg, [rax+rdx*2]			;AMX-TRANSPOSE + AMX-MOVRS

	t2rpntlvwz1rst1		treg, [rax]					;AMX-TRANSPOSE + AMX-MOVRS
	t2rpntlvwz1rst1		treg, [rax+rdx]				;AMX-TRANSPOSE + AMX-MOVRS
	t2rpntlvwz1rst1		treg, [rax+rdx*2]			;AMX-TRANSPOSE + AMX-MOVRS

	ttdpbf16ps				treg, treg2, treg3			;AMX-TRANSPOSE + AMX-BF16
	ttdpfp16ps				treg, treg2, treg3			;AMX-TRANSPOSE + AMX-FP16
	ttcmmimfp16ps treg, 	treg2, treg3				;AMX-TRANSPOSE + AMX-COMPLEX
	ttcmmrlfp16ps treg, 	treg2, treg3				;AMX-TRANSPOSE + AMX-COMPLEX
	tconjtcmmimfp16ps		treg, treg2, treg3			;AMX-TRANSPOSE + AMX-COMPLEX
	tconjtfp16				treg, treg					;AMX-TRANSPOSE + AMX-COMPLEX

	ttmmultf32ps			treg, treg2, treg3			;AMX-TRANSPOSE + AMX_TF32

	[warning *obsolete-removed]

	tilestored			[rax], treg					;AMX_TILE
	tilestored			[rax,rdx], treg				;AMX_TILE
	tilestored			[rax,rdx*2], treg			;AMX_TILE

	tilerelease										;AMX_TILE
%endmacro

%assign n 0
%assign m 1
%assign l 2
  %rep 8
	amx n, m, l
    %assign n ((n+1) % 8)
    %assign m ((m+1) % 8)
    %assign l ((l+1) % 8)
  %endrep
