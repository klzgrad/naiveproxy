	bits 64
	vpcmpeqb k2{k2},zmm0,zmm1
	vpcmpgtb k2{k2},zmm0,zmm1
	vpcmpeqw k2{k2},zmm0,zmm1
	vpcmpgtw k2{k2},zmm0,zmm1
	vpcmpeqd k2{k2},zmm0,zmm1
	vpcmpgtd k2{k2},zmm0,zmm1
	vpcmpeqq k2{k2},zmm0,zmm1
	vpcmpgtq k2{k2},zmm0,zmm1

	vpcmpb k2{k2},zmm0,zmm1,0
	vpcmpb k2{k2},zmm0,zmm1,6
	vpcmpw k2{k2},zmm0,zmm1,0
	vpcmpw k2{k2},zmm0,zmm1,6
	vpcmpd k2{k2},zmm0,zmm1,0
	vpcmpd k2{k2},zmm0,zmm1,6
	vpcmpq k2{k2},zmm0,zmm1,0
	vpcmpq k2{k2},zmm0,zmm1,6

	vpcmpneqb k2{k2},zmm0,zmm1
	vpcmpleb k2{k2},zmm0,zmm1
	vpcmpneqw k2{k2},zmm0,zmm1
	vpcmplew k2{k2},zmm0,zmm1
	vpcmpneqd k2{k2},zmm0,zmm1
	vpcmpled k2{k2},zmm0,zmm1
	vpcmpneqq k2{k2},zmm0,zmm1
	vpcmpleq k2{k2},zmm0,zmm1
