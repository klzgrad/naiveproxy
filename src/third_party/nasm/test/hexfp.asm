;; BR 3392399

; All of these should be the same value...
%macro fp 1
	%1 0.5
	%1 5e-1
	%1 0x1.0p-1
	%1 0x0.8p0
	%1 0x0.8
	%1 0x8p-4
	%1 0x.8
	%1 0x1p-1
	%1 0x0.1p3
	%1 0x0.01p7
	%1 0x0.01p7
	%1 0x0.001p11

%endmacro

	fp do
	fp dt
	fp dq
	fp dd
	fp dw
	fp dd
