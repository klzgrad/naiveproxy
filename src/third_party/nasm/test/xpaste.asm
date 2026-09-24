%iassign OWORD_size 16 ; octo-word
%idefine sizeof(_x_) _x_%+_size

%define ptr eax+sizeof(oword)

movdqa [ptr], xmm1
