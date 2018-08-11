default	rel
%define XMMWORD
%define YMMWORD
%define ZMMWORD
global	dummy_chacha20_poly1305_asm

dummy_chacha20_poly1305_asm:
	DB	0F3h,0C3h		;repret
