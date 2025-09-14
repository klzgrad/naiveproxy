;
; This is a macro to generate packed BCD constants.
; It is obsoleted by "dt" with a packed BCD value with a "p"
; suffix, but it is included here as a rest of nested %rep.
;
%macro dtbcd 1-*.nolist
  %push dtbcd
  %rep %0
    %defstr %$abc %1
    %substr %$sign %$abc 1
    %if %$sign == '-'
      %substr %$abc %$abc 2,-1
      %xdefine %$sign 0x80
    %elif %$sign == '+'
      %substr %$abc %$abc 2,-1
      %xdefine %$sign 0x00
    %else
      %xdefine %$sign 0x00
    %endif
    %strlen %$abclen %$abc
    %defstr %$abclen_str %$abclen
    %assign %$pos %$abclen
    %assign %$bc 0
    %assign %$ld -1
    %rep %$abclen
      %substr %$chr %$abc %$pos
      %assign %$pos %$pos-1
      %if %$chr >= '0' && %$chr <= '9'
        %if %$ld < 0
          %assign %$ld %$chr-'0'
          %assign %$bc %$bc+1
          %if %$bc > 9
            %warning "too many digits in BCD constant"
	    %exitrep
          %endif
        %else
          db %$ld+((%$chr-'0') << 4)
	  %assign %$ld -1
        %endif
      %elif %$chr == '_'
        ; Do nothing...
      %else
        %error "invalid character in BCD constant"
	%exitrep
      %endif
    %endrep
    %if %$ld >= 0
      db %$ld
    %endif
    %rep 9-%$bc
      db 0
    %endrep
    db %$sign
    %rotate 1
  %endrep
  %pop
%endmacro

	dtbcd 123, -456, +789
	dt 123p, -456p, +789p
	dtbcd 765432109876543210
	dt 765432109876543210p
	dtbcd -765432109876543210
	dt -765432109876543210p
	dtbcd +765_432_109_876_543_210
	dt +765_432_109_876_543_210p
	dtbcd -765_432_109_876_543_210
	dt -765_432_109_876_543_210p

	;; Both of these should warn...
	dtbcd 8_765_432_109_876_543_210
	dt 8_765_432_109_876_543_210p
