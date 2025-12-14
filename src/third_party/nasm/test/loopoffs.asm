;Testname=unoptimized; Arguments=-fbin -oloopoffs.bin -O0; Files=stdout stderr loopoffs.bin
;Testname=optimized;   Arguments=-fbin -oloopoffs.bin -Ox; Files=stdout stderr loopoffs.bin
	bits 16
delay:	loop delay
	loop $
delay2:	a32 loop delay2
	a32 loop $
delay3:	loop delay3,ecx
	loop $,ecx
delay4:	a32 loop delay4,ecx
	a32 loop $,ecx
	