	bits 16
foo:	a32 loop foo
bar:	loop bar, ecx

	bits 32
baz:	a16 loop baz
qux:	loop qux, cx
