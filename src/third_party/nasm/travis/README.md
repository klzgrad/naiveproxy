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

### Test unit structure
Each test consists at least of three files:

 - a test descriptor in with `*.json` extension;
 - a source file to compile;
 - a target file to compare result with, it is assumed to have
   the same name as output generated during the pass file but with `*.t`
   extension; thus if a test generates `*.bin` file the appropriate target
   should have `*.bin.t` name.

Running tests
-------------
To run all currently available tests simply type the following

```console
python3 travis/nasm-t.py run
```

By default the `nasm-t.py` scans `test` subdirectory for `*.json` files and
consider each as a test descriptor. Then every test is executed sequentially.
If the descriptor can not be parsed it silently ignored.

To run a particular test provide the test name, for example

```console
python3 travis/nasm-t.py list
...
./travis/test/utf                Test __utf__ helpers
./travis/test/utf                Test errors in __utf__ helpers
...
python3 travis/nasm-t.py run -t ./travis/test/utf
```

Test name duplicates in the listing above means that the descriptor
carries several tests with same name but different options.

Test descriptor file
--------------------
A descriptor file should provide enough information how to run the NASM
itself and which output files or streams to compare with predefined ones.
We use *JSON* format with the following fields:

 - `description`: a short description of a test which is shown to
   a user when tests are being listed;
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
      words it is a name passed as `-o` option to the compiler;
 - `error`: an error handler, can be either *over* to ignore any
   error happened, or *expected* to make sure the test is failing.

### Examples
A simple test where no additional options are used, simply compile
`absolute.asm` file with `bin` format for output, then compare
produced `absolute.bin` file with precompiled `absolute.bin.t`.

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

Note the `output` target is named as *absolute.bin* where *absolute.bin.t*
should be already precompiled (we will talk about it in `update` action)
and present on disk.

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

Updating tests
--------------
If during development some of the targets are expected to change
the tests will start to fail so the should be updated. Thus new
precompiled results will be treated as templates to compare with.

To update all tests in one pass run

```console
python3 travis/nasm-t.py update
...
=== Updating ./travis/test/xcrypt ===
	Processing ./travis/test/xcrypt
	Executing ./nasm -f bin -o ./travis/test/xcrypt.bin ./travis/test/xcrypt.asm
	Moving ./travis/test/xcrypt.bin to ./travis/test/xcrypt.bin.t
=== Test ./travis/test/xcrypt UPDATED ===
...
```

and commit the results. To update a particular test provide its name
with `-t` option.
