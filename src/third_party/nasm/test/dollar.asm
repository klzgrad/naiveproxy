	[dollarhex off]

	section $$$bar
	global $foo, $$foo, $$$foo, $3
_start:
	mov eax,$$foo
$foo:
	nop
$$foo:
	nop
	nop
$$$foo:
	nop
	nop
	nop
$3:
	nop
	nop
	nop
	nop

%macro diff 1
  %assign bar %1
	mov eax, %1
	mov ecx, bar
%endmacro

	diff $3 - $
	diff $3 - $
	diff $3 - $$
	diff $3 - $$
	diff $foo - $
	diff $foo - $$
	diff $$foo - $foo
	diff $$$foo - $$foo
	diff $$$foo - $foo
	diff $$$foo - foo
