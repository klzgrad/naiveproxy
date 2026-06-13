%define ZMACRO
%define NMACRO 1
%define TMACRO 1 2
	db 'N "":'
%iftoken
	db ' token'
%else
	db ' ntoken'
%endif
%ifempty
	db ' empty'
%else
	db ' nempty'
%endif
	db 10
	db 'C "":'
%iftoken  ; With a comment!
	db ' token'
%else
	db ' ntoken'
%endif
%ifempty  ; With a comment!
	db ' empty'
%else
	db ' nempty'
%endif
	db 10
	db 'N "ZMACRO":'
%iftoken ZMACRO
	db ' token'
%else
	db ' ntoken'
%endif
%ifempty ZMACRO
	db ' empty'
%else
	db ' nempty'
%endif
	db 10
	db 'C "ZMACRO":'
%iftoken ZMACRO ; With a comment!
	db ' token'
%else
	db ' ntoken'
%endif
%ifempty ZMACRO ; With a comment!
	db ' empty'
%else
	db ' nempty'
%endif
	db 10
	db 'N "NMACRO":'
%iftoken NMACRO
	db ' token'
%else
	db ' ntoken'
%endif
%ifempty NMACRO
	db ' empty'
%else
	db ' nempty'
%endif
	db 10
	db 'C "NMACRO":'
%iftoken NMACRO ; With a comment!
	db ' token'
%else
	db ' ntoken'
%endif
%ifempty NMACRO ; With a comment!
	db ' empty'
%else
	db ' nempty'
%endif
	db 10
	db 'N "TMACRO":'
%iftoken TMACRO
	db ' token'
%else
	db ' ntoken'
%endif
%ifempty TMACRO
	db ' empty'
%else
	db ' nempty'
%endif
	db 10
	db 'C "TMACRO":'
%iftoken TMACRO ; With a comment!
	db ' token'
%else
	db ' ntoken'
%endif
%ifempty TMACRO ; With a comment!
	db ' empty'
%else
	db ' nempty'
%endif
	db 10
	db 'N "1":'
%iftoken 1
	db ' token'
%else
	db ' ntoken'
%endif
%ifempty 1
	db ' empty'
%else
	db ' nempty'
%endif
	db 10
	db 'C "1":'
%iftoken 1 ; With a comment!
	db ' token'
%else
	db ' ntoken'
%endif
%ifempty 1 ; With a comment!
	db ' empty'
%else
	db ' nempty'
%endif
	db 10
	db 'N "+1":'
%iftoken +1
	db ' token'
%else
	db ' ntoken'
%endif
%ifempty +1
	db ' empty'
%else
	db ' nempty'
%endif
	db 10
	db 'C "+1":'
%iftoken +1 ; With a comment!
	db ' token'
%else
	db ' ntoken'
%endif
%ifempty +1 ; With a comment!
	db ' empty'
%else
	db ' nempty'
%endif
	db 10
	db 'N "1 2":'
%iftoken 1 2
	db ' token'
%else
	db ' ntoken'
%endif
%ifempty 1 2
	db ' empty'
%else
	db ' nempty'
%endif
	db 10
	db 'C "1 2":'
%iftoken 1 2 ; With a comment!
	db ' token'
%else
	db ' ntoken'
%endif
%ifempty 1 2 ; With a comment!
	db ' empty'
%else
	db ' nempty'
%endif
	db 10
	db 'N "1,2":'
%iftoken 1,2
	db ' token'
%else
	db ' ntoken'
%endif
%ifempty 1,2
	db ' empty'
%else
	db ' nempty'
%endif
	db 10
	db 'C "1,2":'
%iftoken 1,2 ; With a comment!
	db ' token'
%else
	db ' ntoken'
%endif
%ifempty 1,2 ; With a comment!
	db ' empty'
%else
	db ' nempty'
%endif
	db 10
	db 'N "foo":'
%iftoken foo
	db ' token'
%else
	db ' ntoken'
%endif
%ifempty foo
	db ' empty'
%else
	db ' nempty'
%endif
	db 10
	db 'C "foo":'
%iftoken foo ; With a comment!
	db ' token'
%else
	db ' ntoken'
%endif
%ifempty foo ; With a comment!
	db ' empty'
%else
	db ' nempty'
%endif
	db 10
	db 'N "foo bar":'
%iftoken foo bar
	db ' token'
%else
	db ' ntoken'
%endif
%ifempty foo bar
	db ' empty'
%else
	db ' nempty'
%endif
	db 10
	db 'C "foo bar":'
%iftoken foo bar ; With a comment!
	db ' token'
%else
	db ' ntoken'
%endif
%ifempty foo bar ; With a comment!
	db ' empty'
%else
	db ' nempty'
%endif
	db 10
	db 'N "%":'
%iftoken %
	db ' token'
%else
	db ' ntoken'
%endif
%ifempty %
	db ' empty'
%else
	db ' nempty'
%endif
	db 10
	db 'C "%":'
%iftoken % ; With a comment!
	db ' token'
%else
	db ' ntoken'
%endif
%ifempty % ; With a comment!
	db ' empty'
%else
	db ' nempty'
%endif
	db 10
	db 'N "+foo":'
%iftoken +foo
	db ' token'
%else
	db ' ntoken'
%endif
%ifempty +foo
	db ' empty'
%else
	db ' nempty'
%endif
	db 10
	db 'C "+foo":'
%iftoken +foo ; With a comment!
	db ' token'
%else
	db ' ntoken'
%endif
%ifempty +foo ; With a comment!
	db ' empty'
%else
	db ' nempty'
%endif
	db 10
	db 'N "<<":'
%iftoken <<
	db ' token'
%else
	db ' ntoken'
%endif
%ifempty <<
	db ' empty'
%else
	db ' nempty'
%endif
	db 10
	db 'C "<<":'
%iftoken << ; With a comment!
	db ' token'
%else
	db ' ntoken'
%endif
%ifempty << ; With a comment!
	db ' empty'
%else
	db ' nempty'
%endif
	db 10
