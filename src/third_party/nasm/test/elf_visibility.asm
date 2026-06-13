global foo:(foo_end - foo)
global foo_hidden:function hidden
global foo_protected:function protected
global foo_internal:function internal
global foo_weak:function weak
global foo_hidden_weak:function hidden weak

extern strong_ref, weak_ref:weak, unused_ref
extern weak_object_ref:weak object
required required_ref

SECTION .text  align=16

foo:
	nop
foo_hidden:
	nop
foo_protected:
	nop
foo_internal:
	nop
foo_weak:
	ret
foo_hidden_weak:
	mov eax,weak_ref
	mov eax,strong_ref
	mov eax,weak_object_ref
foo_label:
	ret
foo_end:
