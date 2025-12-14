/* ----------------------------------------------------------------------- *
 *   
 *   Copyright 1996-2020 The NASM Authors - All Rights Reserved
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

bool reproducible;              /* Reproducible output */

/* These are used by some backends. For a reproducible build,
 * these cannot contain version numbers.
 */
static const char * const _nasm_comment[2] =
{
    "The Netwide Assembler " NASM_VER,
    "The Netwide Assembler"
};

static const char * const _nasm_signature[2] = {
    "NASM " NASM_VER,
    "NASM"
};

const char *nasm_comment(void)
{
    return _nasm_comment[reproducible];
}

size_t nasm_comment_len(void)
{
    return strlen(nasm_comment());
}

const char *nasm_signature(void)
{
    return _nasm_signature[reproducible];
}

size_t nasm_signature_len(void)
{
    return strlen(nasm_signature());
}
