;
; To address https://bugzilla.nasm.us/show_bug.cgi?id=3392527
;
%ifndef __OUTPUT_FORMAT__
	%fatal '__OUTPUT_FORMAT__ defined as ', __OUTPUT_FORMAT__
%endif
