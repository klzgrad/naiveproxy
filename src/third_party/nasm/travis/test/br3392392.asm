	bits 64
	vpaddd zmm0, zmm0, [rax]{1to16}
	vpaddd zmm2{k3}, zmm0, zmm1
	vpaddd zmm2 {k3}, zmm0, zmm1
	vpaddd zmm0{k1}, zmm0, [rax]{1to16}
	vmovdqa32 [rsi]{k1}, zmm1
	vmovdqa32 [rsi]{z}, zmm1
	vmovdqa32 [rsi]{k1}{z}, zmm1
	vmovdqa32 [rsi]{z}{k1}, zmm1
%ifdef ERROR
	vmovdqa32 [rsi]{z}{1to16}, zmm1
	vmovdqa32 [rsi]{z}{k1}{1to16}, zmm1
	vpaddd zmm0, zmm0, [rax]{k1}
	vpaddd zmm0, zmm1, zmm2{1to16}
%endif
