	bits 64
	pushp rax
	pushp rcx
	pushp rdx
	pushp rbx
	pushp rsp
	pushp rbp
	pushp rsi
	pushp rdi

%assign rn 8
%rep 24
	pushp r %+ rn
  %assign rn rn+1
%endrep
