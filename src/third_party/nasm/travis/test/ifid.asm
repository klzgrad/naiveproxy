; BR 3392715: Test proper operation of %ifid with $ and $$
; This produces a human-readable file when compiled with -f bin

%define LF 10

%macro ifid 2
  %ifid %1
    %define %%is 'true'
  %else
    %define %%is 'false'
  %endif
  %defstr %%what   %1
  %defstr %%should %2
	db '%ifid ', %%what, ' = ', %%is, ' (expect ', %%should, ')', LF
%endmacro

	ifid hello, true
	ifid $, false
	ifid $$, false
