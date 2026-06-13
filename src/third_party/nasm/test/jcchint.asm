	bits 64
here:
	cs jz there
	ds jz there
	{pt} jz there
	{pn} jz there
	es jz there
	ss jz there

there:
	{pt} jmp there
	{pn} jmp there
