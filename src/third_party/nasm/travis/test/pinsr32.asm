	bits 32

	pinsrw mm0,eax,0
	pinsrw mm1,si,0
	pinsrw mm2,[ecx],0
	pinsrw mm3,word [ecx],0

	pinsrb xmm0,eax,0
	pinsrb xmm1,sil,0
;	pinsrb xmm1,bh,0
	pinsrb xmm2,[ecx],0
	pinsrb xmm3,byte [ecx],0

	pinsrw xmm0,eax,0
	pinsrw xmm1,si,0
	pinsrw xmm2,[ecx],0
	pinsrw xmm3,word [ecx],0

	pinsrd xmm0,eax,0
	pinsrd xmm1,esi,0
	pinsrd xmm2,[ecx],0
	pinsrd xmm3,dword [ecx],0

	vpinsrb xmm0,eax,0
	vpinsrb xmm1,bl,0
	vpinsrb xmm2,[ecx],0
	vpinsrb xmm3,byte [ecx],0

	vpinsrw xmm0,eax,0
	vpinsrw xmm1,si,0
	vpinsrw xmm2,[ecx],0
	vpinsrw xmm3,word [ecx],0

	vpinsrd xmm0,eax,0
	vpinsrd xmm1,esi,0
	vpinsrd xmm2,[ecx],0
	vpinsrd xmm3,dword [ecx],0

	vpinsrb xmm4,xmm0,eax,0
	vpinsrb xmm5,xmm1,bl,0
	vpinsrb xmm6,xmm2,[ecx],0
	vpinsrb xmm7,xmm3,byte [ecx],0

	vpinsrw xmm4,xmm0,eax,0
	vpinsrw xmm5,xmm1,si,0
	vpinsrw xmm6,xmm2,[ecx],0
	vpinsrw xmm7,xmm3,word [ecx],0

	vpinsrd xmm4,xmm0,eax,0
	vpinsrd xmm5,xmm1,esi,0
	vpinsrd xmm6,xmm2,[ecx],0
	vpinsrd xmm7,xmm3,dword [ecx],0
