;Testname=test; Arguments=-fbin -obr2148476.bin; Files=stdout stderr br2148476.bin

	bits 64

	cvtdq2pd xmm0, xmm1
	cvtdq2pd xmm0, [rdi]
	cvtdq2pd xmm0, qword [rdi]

	cvtdq2ps xmm0, xmm1
	cvtdq2ps xmm0, [rdi]
	cvtdq2ps xmm0, oword [rdi]

	cvtpd2dq xmm0, xmm1
	cvtpd2dq xmm0, [rdi]
	cvtpd2dq xmm0, oword [rdi]

	cvtpd2pi mm0, xmm1
	cvtpd2pi mm0, [rdi]
	cvtpd2pi mm0, oword [rdi]

	cvtpd2ps xmm0, xmm1
	cvtpd2ps xmm0, [rdi]
	cvtpd2ps xmm0, oword [rdi]

	cvtpi2pd xmm0, mm1
	cvtpi2pd xmm0, [rdi]
	cvtpi2pd xmm0, qword [rdi]

	cvtpi2ps xmm0, mm1
	cvtpi2ps xmm0, [rdi]
	cvtpi2ps xmm0, qword [rdi]

	cvtps2dq xmm0, xmm1
	cvtps2dq xmm0, [rdi]
	cvtps2dq xmm0, oword [rdi]

	cvtps2pd xmm0, xmm1
	cvtps2pd xmm0, [rdi]
	cvtps2pd xmm0, qword [rdi]

	cvtps2pi mm0, xmm1
	cvtps2pi mm0, [rdi]
	cvtps2pi mm0, qword [rdi]

	cvtsd2si eax, xmm1
	cvtsd2si eax, [rdi]
	cvtsd2si eax, qword [rdi]
	cvtsd2si rax, xmm1
	cvtsd2si rax, [rdi]
	cvtsd2si rax, qword [rdi]

	cvtsd2ss xmm0, xmm1
	cvtsd2ss xmm0, [rdi]
	cvtsd2ss xmm0, qword [rdi]

	cvtsi2sd xmm0, eax
	cvtsi2sd xmm0, [rdi]	; Compatibility
	cvtsi2sd xmm0, dword [rdi]
	cvtsi2sd xmm0, rax
	cvtsi2sd xmm0, qword [rdi]

	cvtsi2ss xmm0, eax
	cvtsi2ss xmm0, [rdi]	; Compatibility
	cvtsi2ss xmm0, dword [rdi]
	cvtsi2ss xmm0, rax
	cvtsi2ss xmm0, qword [rdi]

	cvtss2sd xmm0, xmm1
	cvtss2sd xmm0, [rdi]
	cvtss2sd xmm0, dword [rdi]

	cvtss2si eax, xmm1
	cvtss2si eax, [rdi]
	cvtss2si eax, dword [rdi]
	cvtss2si rax, xmm1
	cvtss2si rax, [rdi]
	cvtss2si rax, dword [rdi]

	cvttpd2dq xmm0, xmm1
	cvttpd2dq xmm0, [rdi]
	cvttpd2dq xmm0, oword [rdi]

	cvttpd2pi mm0, xmm1
	cvttpd2pi mm0, [rdi]
	cvttpd2pi mm0, oword [rdi]

	cvttps2dq xmm0, xmm1
	cvttps2dq xmm0, [rdi]
	cvttps2dq xmm0, oword [rdi]

	cvttps2pi mm0, xmm1
	cvttps2pi mm0, [rdi]
	cvttps2pi mm0, qword [rdi]

	cvttsd2si eax, xmm1
	cvttsd2si eax, [rdi]	; Compatibility
	cvttsd2si eax, qword [rdi]
	cvttsd2si rax, xmm1
	cvttsd2si rax, [rdi]
	cvttsd2si rax, qword [rdi]

	cvttss2si eax, xmm1
	cvttss2si eax, [rdi] 	; Compatibility
	cvttss2si eax, dword [rdi]
	cvttss2si rax, xmm1
	cvttss2si rax, [rdi]
	cvttss2si rax, dword [rdi]

	vcvtdq2pd xmm0, xmm1
	vcvtdq2pd xmm0, [rdi]
	vcvtdq2pd xmm0, qword [rdi]
	vcvtdq2pd ymm0, xmm1
	vcvtdq2pd ymm0, [rdi]
	vcvtdq2pd ymm0, oword [rdi]

	vcvtdq2ps xmm0, xmm1
	vcvtdq2ps xmm0, [rdi]
	vcvtdq2ps xmm0, oword [rdi]
	vcvtdq2ps ymm0, ymm1
	vcvtdq2ps ymm0, [rdi]
	vcvtdq2ps ymm0, yword [rdi]

	vcvtpd2dq xmm0, xmm1
	vcvtpd2dq xmm0, oword [rdi]
	vcvtpd2dq xmm0, ymm1
	vcvtpd2dq xmm0, yword [rdi]

	vcvtpd2ps xmm0, xmm1
	vcvtpd2ps xmm0, oword [rdi]
	vcvtpd2ps xmm0, ymm1
	vcvtpd2ps xmm0, yword [rdi]

	vcvtps2dq xmm0, xmm1
	vcvtps2dq xmm0, [rdi]
	vcvtps2dq xmm0, oword [rdi]
	vcvtps2dq ymm0, ymm1
	vcvtps2dq ymm0, [rdi]
	vcvtps2dq ymm0, yword [rdi]

	vcvtps2pd xmm0, xmm1
	vcvtps2pd xmm0, [rdi]
	vcvtps2pd xmm0, qword [rdi]
	vcvtps2pd ymm0, xmm1
	vcvtps2pd ymm0, [rdi]
	vcvtps2pd ymm0, oword [rdi]

	vcvtsd2si eax, xmm1
	vcvtsd2si eax, [rdi]
	vcvtsd2si eax, qword [rdi]
	vcvtsd2si rax, xmm1
	vcvtsd2si rax, [rdi]
	vcvtsd2si rax, qword [rdi]

	vcvtsd2ss xmm0, xmm1
	vcvtsd2ss xmm0, [rdi]
	vcvtsd2ss xmm0, qword [rdi]
	vcvtsd2ss xmm0, xmm1, xmm2
	vcvtsd2ss xmm0, xmm1, [rdi]
	vcvtsd2ss xmm0, xmm1, qword [rdi]

	vcvtsi2sd xmm0, eax
	vcvtsi2sd xmm0, [rdi]	; Compatibility
	vcvtsi2sd xmm0, dword [rdi]
	vcvtsi2sd xmm0, rax
	vcvtsi2sd xmm0, qword [rdi]
	vcvtsi2sd xmm0, xmm1, eax
	vcvtsi2sd xmm0, xmm1, [rdi]	; Compatibility
	vcvtsi2sd xmm0, xmm1, dword [rdi]
	vcvtsi2sd xmm0, xmm1, rax
	vcvtsi2sd xmm0, xmm1, qword [rdi]

	vcvtsi2ss xmm0, eax
	vcvtsi2ss xmm0, [rdi]	; Compatibility
	vcvtsi2ss xmm0, dword [rdi]
	vcvtsi2ss xmm0, rax
	vcvtsi2ss xmm0, qword [rdi]
	vcvtsi2ss xmm0, xmm1, eax
	vcvtsi2ss xmm0, xmm1, [rdi]	; Compatibility
	vcvtsi2ss xmm0, xmm1, dword [rdi]
	vcvtsi2ss xmm0, xmm1, rax
	vcvtsi2ss xmm0, xmm1, qword [rdi]

	vcvtss2sd xmm0, xmm1
	vcvtss2sd xmm0, [rdi]
	vcvtss2sd xmm0, dword [rdi]
	vcvtss2sd xmm0, xmm1, xmm2
	vcvtss2sd xmm0, xmm1, [rdi]
	vcvtss2sd xmm0, xmm1, dword [rdi]

	vcvtss2si eax, xmm1
	vcvtss2si eax, [rdi]
	vcvtss2si eax, dword [rdi]
	vcvtss2si rax, xmm1
	vcvtss2si rax, [rdi]
	vcvtss2si rax, dword [rdi]

	vcvttpd2dq xmm0, xmm1
	vcvttpd2dq xmm0, oword [rdi]
	vcvttpd2dq xmm0, ymm1
	vcvttpd2dq xmm0, yword [rdi]

	vcvttps2dq xmm0, xmm1
	vcvttps2dq xmm0, [rdi]
	vcvttps2dq xmm0, oword [rdi]
	vcvttps2dq ymm0, ymm1
	vcvttps2dq ymm0, [rdi]
	vcvttps2dq ymm0, yword [rdi]

	vcvttsd2si eax, xmm1
	vcvttsd2si eax, [rdi]	; Compatibility
	vcvttsd2si eax, qword [rdi]
	vcvttsd2si rax, xmm1
	vcvttsd2si rax, [rdi]
	vcvttsd2si rax, qword [rdi]

	vcvttss2si eax, xmm1
	vcvttss2si eax, [rdi] 	; Compatibility
	vcvttss2si eax, dword [rdi]
	vcvttss2si rax, xmm1
	vcvttss2si rax, [rdi]
	vcvttss2si rax, dword [rdi]
