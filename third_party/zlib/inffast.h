/* inffast.h -- header to use inffast.c
 * Copyright (C) 1995-2003, 2010 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

/* WARNING: this file should *not* be used by applications. It is
   part of the implementation of the compression library and is
   subject to change. Applications should only use zlib.h.
 */

/* INFLATE_FAST_MIN_LEFT is the minimum number of output bytes that are left,
   so that we can call inflate_fast safely with only one up front bounds check.
   One length-distance code pair can copy up to 258 bytes.
 */
#define INFLATE_FAST_MIN_LEFT 258

/* INFLATE_FAST_MIN_HAVE is the minimum number of input bytes that we have, so
   that we can call inflate_fast safely with only one up front bounds check.
   One length-distance code pair (as two Huffman encoded values of up to 15
   bits each) plus any additional bits (up to 5 for length and 13 for distance)
   can require reading up to 48 bits, or 6 bytes.
 */
#define INFLATE_FAST_MIN_HAVE 6

void ZLIB_INTERNAL inflate_fast OF((z_streamp strm, unsigned start));
