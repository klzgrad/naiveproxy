%define FOO(x) x
%ifndef FOO
  %define FOO(x) _ %+ x
%endif

FOO(this):
	jmp this
