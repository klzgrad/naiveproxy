	bits 64

	pinsrw mm0,eax,0
	pinsrw mm1,si,0
	pinsrw mm2,[rcx],0
	pinsrw mm3,word [rcx],0

	pinsrb xmm0,eax,0
	pinsrb xmm1,sil,0
;	pinsrb xmm1,bh,0
	pinsrb xmm2,[rcx],0
	pinsrb xmm3,byte [rcx],0

	pinsrw xmm0,eax,0
	pinsrw xmm1,si,0
	pinsrw xmm2,[rcx],0
	pinsrw xmm3,word [rcx],0

	pinsrd xmm0,eax,0
	pinsrd xmm1,esi,0
	pinsrd xmm2,[rcx],0
	pinsrd xmm3,dword [rcx],0

	pinsrq xmm0,rax,0
	pinsrq xmm1,rsi,0
	pinsrq xmm2,[rcx],0
	pinsrq xmm3,qword [rcx],0

	vpinsrb xmm0,eax,0
	vpinsrb xmm1,sil,0
	vpinsrb xmm2,[rcx],0
	vpinsrb xmm3,byte [rcx],0

	vpinsrw xmm0,eax,0
	vpinsrw xmm1,si,0
	vpinsrw xmm2,[rcx],0
	vpinsrw xmm3,word [rcx],0

	vpinsrd xmm0,eax,0
	vpinsrd xmm1,esi,0
	vpinsrd xmm2,[rcx],0
	vpinsrd xmm3,dword [rcx],0

	vpinsrq xmm0,rax,0
	vpinsrq xmm1,rsi,0
	vpinsrq xmm2,[rcx],0
	vpinsrq xmm3,qword [rcx],0

	vpinsrb xmm4,xmm0,eax,0
	vpinsrb xmm5,xmm1,sil,0
	vpinsrb xmm6,xmm2,[rcx],0
	vpinsrb xmm7,xmm3,byte [rcx],0

	vpinsrw xmm4,xmm0,eax,0
	vpinsrw xmm5,xmm1,si,0
	vpinsrw xmm6,xmm2,[rcx],0
	vpinsrw xmm7,xmm3,word [rcx],0

	vpinsrd xmm4,xmm0,eax,0
	vpinsrd xmm5,xmm1,esi,0
	vpinsrd xmm6,xmm2,[rcx],0
	vpinsrd xmm7,xmm3,dword [rcx],0

	vpinsrq xmm4,xmm0,rax,0
	vpinsrq xmm5,xmm1,rsi,0
	vpinsrq xmm6,xmm2,[rcx],0
	vpinsrq xmm7,xmm3,qword [rdx],0
