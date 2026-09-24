%define a 64
%define b -30

	dq a*b
	db %num(a*b,,16), `\n`
	db %num(a*b,16,16), `\n`

	db %num(a*b), `\n`
	db %num(a*b,10), `\n`
	db %num(a*b,3), `\n`
	db %num(a*b,-3), `\n`
	db %num(a*b,10,2), `\n`
	db %num(a*b,-10,2), `\n`
	db %num(a*b,,2), `\n`

	dq -a*b
	db %num(-a*b,,16), `\n`
	db %num(-a*b,16,16), `\n`

	db %num(-a*b), `\n`
	db %num(-a*b,10), `\n`
	db %num(-a*b,3), `\n`
	db %num(-a*b,-3), `\n`
	db %num(-a*b,10,2), `\n`
	db %num(-a*b,-10,2), `\n`

	dq %abs(a*b)
	db %num(%abs(a*b),,16), `\n`
	db %num(%abs(a*b),16,16), `\n`

	db %num(%abs(a*b)), `\n`
	db %num(%abs(a*b),10), `\n`
	db %num(%abs(a*b),3), `\n`
	db %num(%abs(a*b),-3), `\n`
	db %num(%abs(a*b),10,2), `\n`
	db %num(%abs(a*b),-10,2), `\n`

	dq %abs(-a*b)
	db %num(%abs(-a*b),,16), `\n`
	db %num(%abs(-a*b),16,16), `\n`

	db %num(%abs(-a*b)), `\n`
	db %num(%abs(-a*b),10), `\n`
	db %num(%abs(-a*b),3), `\n`
	db %num(%abs(-a*b),-3), `\n`
	db %num(%abs(-a*b),10,2), `\n`
	db %num(%abs(-a*b),-10,2), `\n`
