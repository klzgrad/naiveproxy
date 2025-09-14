%macro strlen_test 1
    %strlen len %2 ; not existing argument
%endmacro

strlen_test 'a'
