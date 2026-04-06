;Testname=warning;    Arguments=-fbin -omacdef.bin -w+macro-defaults; Files=stdout stderr macdef.bin
;Testname=nonwarning; Arguments=-fbin -omacdef.bin -w-macro-defaults; Files=stdout stderr macdef.bin

%MACRO mmac_fix 1 a
 ; While defined to take one parameter, any invocation will
 ; see two, due to the default parameter.
 %warning %0 %1 %2 %3 %4 %5
%ENDMACRO
mmac_fix one

%MACRO mmac_var 1-2 a,b
 ; While defined to take one or two parameters, invocations
 ; will see three, due to the default parameters.
 %warning %0 %1 %2 %3 %4 %5
%ENDMACRO
mmac_var one
mmac_var one,two

%MACRO mmac_plus 1-2+ a,b
 ; This does not warn. Although this looks like two default
 ; parameters, it ends up being only one: the "+" limits it
 ; to two parameters; if invoked without a second parameter
 ; the second parameter will be "a,b".
 %warning %0 %1 %2 %3 %4 %5
 ;Check rotating behaviour
%ENDMACRO
mmac_plus one
mmac_plus one,two
mmac_plus one,two,three

%MACRO mmac_star 1-* a,b
 ; This does not warn. Because the "*" extends the range of
 ; parameters to infinity, the "a,b" default parameters can
 ; not exceed that range.
 %warning %0 %1 %2 %3 %4 %5
%ENDMACRO
mmac_star one
mmac_star one,two
mmac_star one,two,three

%MACRO mmac_rotate 0-* a,b
 %warning %0 %1 %2 %3 %4 %5
 ;%rotate should rotate all parameters
 %rotate 1
 %warning %0 %1 %2 %3 %4 %5
%ENDMACRO
mmac_rotate
mmac_rotate one
mmac_rotate one,two
mmac_rotate one,two,three

;Scope / evaluation time test
%define  I 0
%assign  J 0
%xdefine K 0

%MACRO mmac_scope 0 I J K
 %warning %1 %2 %3
%ENDMACRO

%define  I 1
%assign  J 1
%xdefine K 1
mmac_scope
