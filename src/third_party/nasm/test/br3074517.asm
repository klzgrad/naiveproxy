;%define UNDEFINED
%macro macro 0
    %ifndef UNDEFINED
        %rep 1
            %fatal This should display "fatal: (m:3)"
        %endrep
    %endif
    %fatal This should display "fatal: (m:6)" if 'UNDEFINED' defined
%endmacro

macro

