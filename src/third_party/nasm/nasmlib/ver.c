/* ----------------------------------------------------------------------- *
 *   
 *   Copyright 1996-2016 The NASM Authors - All Rights Reserved
 *   See the file AUTHORS included with the NASM distribution for
 *   the specific copyright holders.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following
 *   conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *     
 *     THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 *     CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *     INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *     MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *     DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 *     CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *     SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *     NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *     LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *     HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 *     OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 *     EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ----------------------------------------------------------------------- */

#include "ver.h"
#include "version.h"

/* This is printed when entering nasm -v */
const char nasm_version[] = NASM_VER;
const char nasm_compile_options[] = ""
#ifdef DEBUG
    " with -DDEBUG"
#endif
    ;

/* These are used by some backends. */
static const char __nasm_comment[] =
    "The Netwide Assembler " NASM_VER;

static const char __nasm_signature[] =
    "NASM " NASM_VER;

/* These are constant so we could pass regression tests  */
static const char __nasm_comment_const[] ="The Netwide Assembler CONST";
static const char __nasm_signature_const[] = "NASM CONST";

int nasm_test_run(void)
{
	return getenv("NASM_TEST_RUN") ? 1 : 0;
}

const char *nasm_comment(void)
{
	if (!nasm_test_run())
		return __nasm_comment;
	return __nasm_comment_const;
}

size_t nasm_comment_len(void)
{
	if (!nasm_test_run())
		return strlen(__nasm_comment);
	return strlen(__nasm_comment_const);
}

const char *nasm_signature(void)
{
	if (!nasm_test_run())
		return __nasm_signature;
	return __nasm_signature_const;
}

size_t nasm_signature_len(void)
{
	if (!nasm_test_run())
		return strlen(__nasm_signature);
	return strlen(__nasm_signature_const);
}
