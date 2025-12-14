; Not automatically testable because it is not constant
;
; FIXME: Need to adjust code and all this macros for
; --reproducible NASM option.
;
	db __DATE__, 13, 10
	db __TIME__, 13, 10
	db __UTC_DATE__, 13, 10
	db __UTC_TIME__, 13, 10

	align 4
	dd __DATE_NUM__
	dd __TIME_NUM__
	dd __UTC_DATE_NUM__
	dd __UTC_TIME_NUM__
	dd __POSIX_TIME__
