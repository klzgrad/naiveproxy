%macro dir 1
	%assign y %isdirective(%1)
	%xdefine c %cond(y,YES,NO)
	db "Directive ", %str(%1), %cond(y,""," not"), ` valid\r\n`
%endmacro

	dir db
	dir %iffile
	dir iffile
	dir [%iffile]
	dir [extern]
	dir extern
	dir %extern
	dir org
	dir uppercase
