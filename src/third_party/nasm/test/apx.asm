	bits 64
	ccmpnz {dfv=} rax,rbx
	ccmpnz {dfv=cf} rax,rbx
	ccmpnz {dfv=zf} rax,rbx
	ccmpnz {dfv=sf} rax,rbx
	ccmpnz {dfv=of} rax,rbx
	ccmpnz {dfv=of,cf} rax,rbx
	ccmpnz {dfv=of,cf} [rax],rbx
	ccmpnz {dfv=of,cf} dword [rax],3
	ccmpnz {dfv=of,cf} dword [r31],3
	ccmpnz 15, dword [r31], 3

	ccmpnz {dfv=of,cf} dword [r31], byte 3
	ccmpnz {dfv=of,cf} dword [r31], 3
	ccmpnz {dfv=of,cf} dword [r31], dword 3
	ccmpnz {dfv=of,cf} dword [r31], strict dword 3
	ccmpnz {dfv=of,cf} dword [r31], 0xaabbccdd
	ccmpnz {dfv=of,cf} dword [r31], dword 0xaabbccdd
	ccmpnz {dfv=of,cf} qword [r31], 0xaabbccdd

	push rax
	pushp rax
	push rax, rbx
	push rax:rbx

	pop rax:rbx
	pop rbx, rax

	add      al,[rdx],cl
	add {nf} al,[rdx],cl
	add      [rdx],cl
	add {evex} [rdx],cl
	add {nf} [rdx],cl

	add      al,[rdx],r25b
	add {nf} al,[rdx],r25b
	add      [rdx],r25b
	add {evex} [rdx],r25b
	add {nf} [rdx],r25b

	add      al,[r27],cl
	add {nf} al,[r27],cl
	add      [r27],cl
	add {evex} [r27],cl
	add {nf} [r27],cl

	add      al,[r27],r25b
	add {nf} al,[r27],r25b
	add      [r27],r25b
	add {evex} [r27],r25b
	add {nf} [r27],r25b

	add      eax,[rdx],ecx
	add {nf} eax,[rdx],ecx
	add      [rdx],ecx
	add {evex} [rdx],ecx
	add {nf} [rdx],ecx

	add      eax,[rdx],r25d
	add {nf} eax,[rdx],r25d
	add      [rdx],r25d
	add {evex} [rdx],r25d
	add {nf} [rdx],r25d

	add      eax,[r27],ecx
	add {nf} eax,[r27],ecx
	add      [r27],ecx
	add {evex} [r27],ecx
	add {nf} [r27],ecx

	add      eax,[r27],r25d
	add {nf} eax,[r27],r25d
	add      [r27],r25d
	add {evex} [r27],r25d
	add {nf} [r27],r25d

	add{zu}     al,[rdx],cl
	add{zu}{nf} al,[rdx],cl
	add	    al,al,cl
	add{zu}	    al,al,cl
	add{zu}	    al,cl
	add{zu}{nf} al,al,cl
	add{zu}{nf} al,cl
	add{nf}{zu} al,al,cl
	add{nf}{zu} al,cl
	add{evex}   al,cl
	add{nf}     al,al,cl
	add{nf}     al,cl
	add	    al,cl

	add{zu}     eax,[rdx],ecx
	add{zu}{nf} eax,[rdx],ecx
	add	    eax,eax,ecx
	add{zu}	    eax,eax,ecx
	add{zu}	    eax,ecx
	add{zu}{nf} eax,eax,ecx
	add{zu}{nf} eax,ecx
	add{nf}{zu} eax,eax,ecx
	add{nf}{zu} eax,ecx
	add{evex}   eax,ecx
	add{nf}     eax,eax,ecx
	add{nf}     eax,ecx
	add	    eax,ecx

%if 0
	add{evex}   [rdx],16
	add{evex}   [rdx],byte 16
	add{evex}   [rdx],dword 16
	add{evex}   [rdx],strict dword 16
%endif

	add{evex}   dword [rdx],16
	add{evex}   dword [rdx],256
	add{evex}   dword [rdx],byte 16
	add{evex}   dword [rdx],dword 16
	add{evex}   dword [rdx],strict dword 16

%if 0
	add{nf}	    [rdx],16
	add{nf}	    [rdx],byte 16
	add{nf}	    [rdx],dword 16
	add{nf}	    [rdx],strict dword 16
%endif

	add{nf}	    dword [rdx],16
	add{nf}     dword [rdx],256
	add{nf}	    dword [rdx],byte 16
	add{nf}	    dword [rdx],dword 16
	add{nf}	    dword [rdx],strict dword 16

	add{nf}	    eax,[rdx],16
	add{nf}	    eax,[rdx],256
	add{nf}	    eax,[rdx],byte 16
	add{nf}	    eax,[rdx],dword 16
	add{nf}	    eax,[rdx],strict dword 16

	add{nf}	    eax,dword [rdx],16
	add{nf}	    eax,dword [rdx],256
	add{nf}	    eax,dword [rdx],byte 16
	add{nf}	    eax,dword [rdx],dword 16
	add{nf}	    eax,dword [rdx],strict dword 16

	setc	    al
	setc{evex}  al
	setc	    eax
	setc        rax
	setc{zu}    al
	setc{zu}    eax
	setc        [rdi]
	setc byte   [rdi]
%ifdef ERR
	setc dword  [rdi]
	setc{zu}    [rdi]
%endif
