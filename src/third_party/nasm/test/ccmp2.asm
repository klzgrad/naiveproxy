	bits 64
	default rel

	ccmpl {dfv=of,cf} rdx, r30
	ccmpl {dfv=of,cf}, rdx, r30
	ccmpl 0x9, rdx, r30			; Comma required
	ccmpl ({dfv=of}|{dfv=cf}), rdx, r30	; Parens, comma required
ofcf1	equ {dfv=of,cf}				; Parens not required
	ccmpl ofcf1, rdx, r30			; Comma required
ofcf2	equ ({dfv=of,sf,cf} & ~{dfv=cf})	; Parens required
	ccmpl ofcf2, rdx, r30			; Comma required
