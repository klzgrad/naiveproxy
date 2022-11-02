;Testname=unoptimized; Arguments=-O0 -fbin -ofloatize.bin; Files=stdout stderr floatize.bin
;Testname=optimized;   Arguments=-Ox -fbin -ofloatize.bin; Files=stdout stderr floatize.bin

%assign	x13	13+26
%assign f16   __float16__(1.6e-7)
%assign f32   __float32__(1.6e-7)
%assign f64   __float64__(1.6e-7)
%assign f80m  __float80m__(1.6e-7)
%assign f80e  __float80e__(1.6e-7)
%assign f128l __float128l__(1.6e-7)
%assign f128h __float128h__(1.6e-7)

	dw f16
	dd f32
	dq f64
	dq f80m
	dw f80e
	dq f128l
	dq f128h
