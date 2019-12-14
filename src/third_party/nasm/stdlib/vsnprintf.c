/*
 * vsnprintf()
 *
 * Poor substitute for a real vsnprintf() function for systems
 * that don't have them...
 */

#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "nasmlib.h"
#include "error.h"

#if !defined(HAVE_VSNPRINTF) && !defined(HAVE__VSNPRINTF)

#define BUFFER_SIZE     65536   /* Bigger than any string we might print... */

static char snprintf_buffer[BUFFER_SIZE];

int vsnprintf(char *str, size_t size, const char *format, va_list ap)
{
    int rv, bytes;

    if (size > BUFFER_SIZE) {
        nasm_panic("vsnprintf: size (%d) > BUFFER_SIZE (%d)",
                   size, BUFFER_SIZE);
        size = BUFFER_SIZE;
    }

    rv = vsprintf(snprintf_buffer, format, ap);
    if (rv >= BUFFER_SIZE)
        nasm_panic("vsnprintf buffer overflow");

    if (size > 0) {
        if ((size_t)rv < size-1)
            bytes = rv;
        else
            bytes = size-1;
        memcpy(str, snprintf_buffer, bytes);
        str[bytes] = '\0';
    }

    return rv;
}

#endif
