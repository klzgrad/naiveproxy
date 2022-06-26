;Testname=unoptimized; Arguments=-O0 -frdf -obr560873.rdf; Files=stdout stderr br560873.rdf
;Testname=optimized;   Arguments=-Ox -frdf -obr560873.rdf; Files=stdout stderr br560873.rdf

label:
	bits 16
	call far dword label
	mov [label],ax
	mov [label],eax
	mov [word label],ax
	mov [word label],eax
	mov [dword label],ax
	mov [dword label],eax
	push 3700
	push word 3700
	push dword 3700
	
	bits 32
	call far word label
	mov [label],ax
	mov [label],eax
	mov [word label],ax
	mov [word label],eax
	mov [dword label],ax
	mov [dword label],eax
	push 3700
	push word 3700
	push dword 3700
