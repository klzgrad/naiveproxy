%define what(&x,i,n)	%str(i+1),"/",%str(n),"=", x, `\n`
	db %map(what:(%mapi,%mapn),foo,bar,baz)
