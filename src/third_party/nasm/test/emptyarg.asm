%define EMPTY

%macro bar 1
	db "bar", __LINE__, %0, %1
%endmacro

%macro baz 2
	db "baz", __LINE__, %0, %1, %2
%endmacro

%macro nothing 0
	db "nothing", __LINE__, %0
%endmacro

%macro xyzzy 1-2
	db "xyzzy", __LINE__, %0, %1, %2, %3
%endmacro

%macro vararg 0-*
	db "vararg", __LINE__, %0
	%assign %%i 1
	%rep %0
	  db "vararg arg ", %%i, %1
	  %rotate 1
	  %assign %%i %%i + 1
	%endrep
%endmacro

%macro defargs 1-5 def2, def3, def4, def5
	db "defargs", __LINE__, %0, %1, %2, %3, %4, %5
%endmacro

%macro ivar 1
	vararg %1
%endmacro

%macro foo 1-2
	db "foo", __LINE__, %0, %1, %2
	bar %2
	bar {%2}
	bar %2,
	bar {%2},
	baz %1,%2
	baz {%1},{%2}
	nothing %1
	nothing %2
	xyzzy "meep",%1,%2,
	xyzzy "meep","meep",%1,%2
	xyzzy "alpha","bravo",
	xyzzy "with","empty",EMPTY
%endmacro

%macro orange 1
	db %{1:1}
%endmacro

%macro prange1 2-3
	db %{1:2}, 0%3
%endmacro

%macro prange2 1-3 'two', 'three'
	db %{1:3}
%endmacro

	db 4,
	nothing
	nothing 1
	nothing			; foo
	nothing EMPTY

flup:	foo 1,2
	foo 3
	bar
	bar EMPTY
	foo 6,
	foo 6,			; With space/comment
	foo 6,EMPTY
	baz 8,EMPTY
	foo 6,{}
	foo ,5

	xyzzy 13,14,15,
	xyzzy 13,14,15,EMPTY
	xyzzy 20,21
	xyzzy 22,23,
	xyzzy 24,25,EMPTY
	xyzzy 26,27,,
	xyzzy 28,29,EMPTY,EMPTY

	vararg
	vararg EMPTY
	vararg ,
	vararg 10
	vararg 11,
	vararg 12,EMPTY
	vararg 13,14,15,
	vararg 13,14,15,EMPTY
	vararg 20,21
	vararg 22,23,
	vararg 24,25,EMPTY
	vararg 26,27,,
	vararg 28,29,EMPTY,EMPTY

	ivar {}
	ivar {EMPTY}
	ivar EMPTY
	ivar ,
	ivar {,}
	ivar {60}
	ivar {61,}
	ivar {62,EMPTY}
	ivar {63,64,65,}
	ivar {63,64,65,EMPTY}
	ivar {70,71}
	ivar {72,73,}
	ivar {74,75,EMPTY}
	ivar {76,77,,}
	ivar {78,79,EMPTY,EMPTY}

	defargs EMPTY
	defargs 91
	defargs 91,92
	defargs 91,92,93
	defargs 91,92,93,94
	defargs 91,92,93,94,95
	defargs ,
	defargs 91,
	defargs 91,92,
	defargs 91,92,93,
	defargs 91,92,93,94,
	defargs 91,92,93,94,95,

	prange1 101
	prange1 101, 102
	prange1 101, 102, 103
	prange2 121
	prange2 121, 122
	prange2 121, 122, 123
	prange2 {121}
	prange2 {121,121}
	prange2 {121},{122}
	prange2 {121},122,{123}
	prange2 121,{122,122},123

	orange 130
	orange 130, 131
	orange {130, 131}
