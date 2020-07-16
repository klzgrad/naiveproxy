Testing NASM
============
We use [Travis CI](https://travis-ci.org/) service to execute NASM tests,
which basically prepares the environment and runs our `nasm-t.py` script.

The script scans a testing directory for `*.json` test descriptor files
and runs test by descriptor content.

Test engine
-----------
`nasm-t.py` script is a simple test engine written by Python3 language
which allows either execute a single test or run them all in a sequence.

A typical test case processed by the following steps:

 - a test descriptor get parsed to figure out which arguments
   are to be provided into the NASM command line;
 - invoke the NASM with arguments;
 - compare generated files with precompiled templates.

`nasm-t.py` supports the following commands:

 - `list`: to list all test cases
 - `run`: to run test cases
 - `update`: to update precompiled templates

Use `nasm-t.py -h` command to get the detailed description of every option.

Test descriptor file
--------------------
A descriptor file should provide enough information how to run the NASM
itself and which output files or streams to compare with predefined ones.
We use `JSON` format with the following fields:

 - `description`: a short description of a test which is shown to
   a user when tests are listed;
 - `id`: descriptor internal name to use with `ref` field;
 - `ref`: a reference to `id` from where settings should be
   copied, it is convenient when say only `option` is different
   while the rest of the fields are the same;
 - `format`: NASM output format to use (`bin`,`elf` and etc);
 - `source`: is a source file name to compile, this file must
   be shipped together with descriptor file itself;
 - `option`: an additional option passed to the command line;
 - `update`: a trigger to skip updating targets when running
   an update procedure;
 - `target`: an array of targets which the test engine should
   check once compilation finished:
    - `stderr`: a file containing *stderr* stream output to check;
    - `stdout`: a file containing *stdout* stream output to check;
    - `output`: a file containing compiled result to check, in other
      words it is a name passed as `-o` option to the compiler.

Examples
--------

A simple test where no additional options are used, simply compile
`absolute.asm` file with `bin` format for output, then compare
produced `absolute.bin` file with precompiled `absolute.bin.dest`.

```json
{
	"description": "Check absolute addressing",
	"format": "bin",
	"source": "absolute.asm",
	"target": [
		{ "output": "absolute.bin" }
	]
}
```

A slightly complex example: compile one source file with different optimization
options and all results must be the same. To not write three descriptors
we assign `id` to the first one and use `ref` term to copy settings.
Also, it is expected that `stderr` stream will not be empty but carry some
warnings to compare.

```json
[
	{
		"description": "Check 64-bit addressing (-Ox)",
		"id": "addr64x",
		"format": "bin",
		"source": "addr64x.asm",
		"option": "-Ox",
		"target": [
			{ "output": "addr64x.bin" },
			{ "stderr": "addr64x.stderr" }
		]
	},
	{
		"description": "Check 64-bit addressing (-O1)",
		"ref": "addr64x",
		"option": "-O1",
		"update": "false"
	},
	{
		"description": "Check 64-bit addressing (-O0)",
		"ref": "addr64x",
		"option": "-O0",
		"update": "false"
	}
]
```
