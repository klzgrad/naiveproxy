	bits 64

	vgatherdpd xmm0,[rcx+xmm2],xmm3
	vgatherqpd xmm0,[rcx+xmm2],xmm3
	vgatherdpd ymm0,[rcx+xmm2],ymm3
	vgatherqpd ymm0,[rcx+ymm2],ymm3

	vgatherdpd xmm0,[rcx+xmm2*1],xmm3
	vgatherqpd xmm0,[rcx+xmm2*1],xmm3
	vgatherdpd ymm0,[rcx+xmm2*1],ymm3
	vgatherqpd ymm0,[rcx+ymm2*1],ymm3

	vgatherdpd xmm0,[rcx+xmm2*2],xmm3
	vgatherqpd xmm0,[rcx+xmm2*2],xmm3
	vgatherdpd ymm0,[rcx+xmm2*2],ymm3
	vgatherqpd ymm0,[rcx+ymm2*2],ymm3

	vgatherdpd xmm0,[rcx+xmm2*4],xmm3
	vgatherqpd xmm0,[rcx+xmm2*4],xmm3
	vgatherdpd ymm0,[rcx+xmm2*4],ymm3
	vgatherqpd ymm0,[rcx+ymm2*4],ymm3

	vgatherdpd xmm0,[rcx+xmm2*8],xmm3
	vgatherqpd xmm0,[rcx+xmm2*8],xmm3
	vgatherdpd ymm0,[rcx+xmm2*8],ymm3
	vgatherqpd ymm0,[rcx+ymm2*8],ymm3

	vgatherdpd xmm0,[xmm2],xmm3
	vgatherqpd xmm0,[xmm2],xmm3
	vgatherdpd ymm0,[xmm2],ymm3
	vgatherqpd ymm0,[ymm2],ymm3

	vgatherdpd xmm0,[xmm2*1],xmm3
	vgatherqpd xmm0,[xmm2*1],xmm3
	vgatherdpd ymm0,[xmm2*1],ymm3
	vgatherqpd ymm0,[ymm2*1],ymm3

	vgatherdpd xmm0,[xmm2*2],xmm3
	vgatherqpd xmm0,[xmm2*2],xmm3
	vgatherdpd ymm0,[xmm2*2],ymm3
	vgatherqpd ymm0,[ymm2*2],ymm3

	vgatherdpd xmm0,[xmm2*4],xmm3
	vgatherqpd xmm0,[xmm2*4],xmm3
	vgatherdpd ymm0,[xmm2*4],ymm3
	vgatherqpd ymm0,[ymm2*4],ymm3

	vgatherdpd xmm0,[xmm2*8],xmm3
	vgatherqpd xmm0,[xmm2*8],xmm3
	vgatherdpd ymm0,[xmm2*8],ymm3
	vgatherqpd ymm0,[ymm2*8],ymm3

	vgatherdpd xmm0,[xmm2+rcx],xmm3
	vgatherqpd xmm0,[xmm2+rcx],xmm3
	vgatherdpd ymm0,[xmm2+rcx],ymm3
	vgatherqpd ymm0,[ymm2+rcx],ymm3

	vgatherdpd xmm0,[xmm2*1+rcx],xmm3
	vgatherqpd xmm0,[xmm2*1+rcx],xmm3
	vgatherdpd ymm0,[xmm2*1+rcx],ymm3
	vgatherqpd ymm0,[ymm2*1+rcx],ymm3

	vgatherdpd xmm0,[xmm2*2+rcx],xmm3
	vgatherqpd xmm0,[xmm2*2+rcx],xmm3
	vgatherdpd ymm0,[xmm2*2+rcx],ymm3
	vgatherqpd ymm0,[ymm2*2+rcx],ymm3

	vgatherdpd xmm0,[xmm2*4+rcx],xmm3
	vgatherqpd xmm0,[xmm2*4+rcx],xmm3
	vgatherdpd ymm0,[xmm2*4+rcx],ymm3
	vgatherqpd ymm0,[ymm2*4+rcx],ymm3

	vgatherdpd xmm0,[xmm2*8+rcx],xmm3
	vgatherqpd xmm0,[xmm2*8+rcx],xmm3
	vgatherdpd ymm0,[xmm2*8+rcx],ymm3
	vgatherqpd ymm0,[ymm2*8+rcx],ymm3
