/* hash.h     Routines to calculate a CRC32 hash value
 *
 *   These routines donated to the NASM effort by Graeme Defty.
 *
 * The Netwide Assembler is copyright (C) 1996 Simon Tatham and
 * Julian Hall. All rights reserved. The software is
 * redistributable under the license given in the file "LICENSE"
 * distributed in the NASM archive.
 */

#ifndef RDOFF_HASH_H
#define RDOFF_HASH_H 1


uint32_t hash(const char *name);

#endif
