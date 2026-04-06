%define tonum(=x) x

	dd tonum(1+3)
	dd tonum(5*7)

%define mixed(a,=b,c)		(a + b)
%define mixed2(a,=b,)		(a + b)
%define mixed3(=a/u,=b/x,=c/ux)	(a + b + c)
%define ALPHA (1 + 2)
%define BETA  (3 + 4)
%define GAMMA (5 + 6)

	dd mixed(ALPHA, BETA, GAMMA)
	dd mixed2(ALPHA, BETA, GAMMA)
	dd mixed3(-ALPHA, -BETA, -GAMMA)
