%define join(sep)		''
%define _join(sep,str) sep,str
%define join(sep,s1,sn+)	%strcat(s1, %map(_join:(sep) %, sn))

	db join(':')
	db join(':','a')
	db join(':','a','b')
	db join(':','a','b','c')
	db join(':','a','b','c','d')
