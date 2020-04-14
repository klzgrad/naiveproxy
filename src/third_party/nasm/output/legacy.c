/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2016-2017 The NASM Authors - All Rights Reserved
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

/*
 * output/legacy.c
 *
 * Mangle a struct out_data to match the rather bizarre legacy
 * backend interface.
 *
 * The "data" parameter for the output function points to a "int64_t",
 * containing the address of the target in question, unless the type is
 * OUT_RAWDATA, in which case it points to an "uint8_t"
 * array.
 *
 * Exceptions are OUT_RELxADR, which denote an x-byte relocation
 * which will be a relative jump. For this we need to know the
 * distance in bytes from the start of the relocated record until
 * the end of the containing instruction. _This_ is what is stored
 * in the size part of the parameter, in this case.
 *
 * Also OUT_RESERVE denotes reservation of N bytes of BSS space,
 * and the contents of the "data" parameter is irrelevant.
 */

#include "nasm.h"
#include "outlib.h"

void nasm_do_legacy_output(const struct out_data *data)
{
    const void *dptr = data->data;
    enum out_type type = data->type;
    int32_t tsegment = data->tsegment;
    int32_t twrt = data->twrt;
    uint64_t size = data->size;

    switch (data->type) {
    case OUT_RELADDR:
        switch (data->size) {
        case 1:
            type = OUT_REL1ADR;
            break;
        case 2:
            type = OUT_REL2ADR;
            break;
        case 4:
            type = OUT_REL4ADR;
            break;
        case 8:
            type = OUT_REL8ADR;
            break;
        default:
            panic();
            break;
        }

        dptr = &data->toffset;
        size = data->relbase - data->offset;
        break;

    case OUT_SEGMENT:
        type = OUT_ADDRESS;
        dptr = zero_buffer;
        size = (data->sign == OUT_SIGNED) ? -data->size : data->size;
        tsegment |= 1;
        break;

    case OUT_ADDRESS:
        dptr = &data->toffset;
        size = (data->sign == OUT_SIGNED) ? -data->size : data->size;
        break;

    case OUT_RAWDATA:
    case OUT_RESERVE:
        tsegment = twrt = NO_SEG;
        break;

    case OUT_ZERODATA:
        tsegment = twrt = NO_SEG;
        type = OUT_RAWDATA;
        dptr = zero_buffer;
        while (size > ZERO_BUF_SIZE) {
            ofmt->legacy_output(data->segment, dptr, type,
                                ZERO_BUF_SIZE, tsegment, twrt);
            size -= ZERO_BUF_SIZE;
        }
        break;

    default:
        panic();
        break;
    }

    ofmt->legacy_output(data->segment, dptr, type, size, tsegment, twrt);
}
