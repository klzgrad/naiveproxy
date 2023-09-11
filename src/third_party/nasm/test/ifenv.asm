%macro import 1
	%defstr %%incfile %!PROJECTBASEDIR/%{1}.inc
	%defstr %%decfile %!'PROJECTBASEDIR'/%{1}.dec
	db %%incfile, `\n`
	db %%decfile, `\n`
%endmacro

%ifenv PROJECTBASEDIR
import foo
%else
%warning No PROJECTBASEDIR defined
%endif

%ifenv %!PROJECTBASEDIR
import foo
%else
%warning No PROJECTBASEDIR defined
%endif

%ifenv 'PROJECTBASEDIR'
import foo
%else
%warning No PROJECTBASEDIR defined
%endif

%ifenv %!'PROJECTBASEDIR'
import foo
%else
%warning No PROJECTBASEDIR defined
%endif

