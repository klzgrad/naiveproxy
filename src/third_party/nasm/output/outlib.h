/* ----------------------------------------------------------------------- *
 *   
 *   Copyright 1996-2018 The NASM Authors - All Rights Reserved
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

#ifndef NASM_OUTLIB_H
#define NASM_OUTLIB_H

#include "nasm.h"
#include "error.h"

uint64_t realsize(enum out_type type, uint64_t size);

/* Do-nothing versions of some output routines */
enum directive_result
null_directive(enum directive directive, char *value, int pass);
void null_sectalign(int32_t seg, unsigned int value);
void null_reset(void);
int32_t null_segbase(int32_t seg);

/* Do-nothing versions of all the debug routines */
void null_debug_init(void);
void null_debug_linenum(const char *filename, int32_t linenumber,
			int32_t segto);
void null_debug_deflabel(char *name, int32_t segment, int64_t offset,
                         int is_global, char *special);
void null_debug_directive(const char *directive, const char *params);
void null_debug_typevalue(int32_t type);
void null_debug_output(int type, void *param);
void null_debug_cleanup(void);
extern const struct dfmt * const null_debug_arr[2];

/* Wrapper for unported backends */
void nasm_do_legacy_output(const struct out_data *data);

#endif /* NASM_OUTLIB_H */

