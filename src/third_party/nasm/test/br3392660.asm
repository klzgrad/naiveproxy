%macro coreloop 1
    .count_%+1:
    .no_run_before_%+1:
    .broken_run_before_%-1:
%endmacro

label:
    coreloop z
    coreloop nz
