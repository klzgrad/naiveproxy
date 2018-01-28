/* chunkcopy_arm.h -- fast copies and sets
 * Copyright (C) 2017 ARM, Inc.
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

#ifndef CHUNKCOPY_ARM_H
#define CHUNKCOPY_ARM_H

#include <arm_neon.h>
#include "zutil.h"

#if __STDC_VERSION__ >= 199901L
#define Z_RESTRICT restrict
#else
#define Z_RESTRICT
#endif

/* A port to a new arch only requires to implement 2 functions
  (vld_dup and chunkset_core) and the chunk type.
*/

typedef uint8x16_t chunkcopy_chunk_t;
#define CHUNKCOPY_CHUNK_SIZE sizeof(chunkcopy_chunk_t)

/* Forward declarations. */
static inline unsigned char FAR* chunkunroll_relaxed(unsigned char FAR* out,
                                                     unsigned FAR* dist,
                                                     unsigned FAR* len);

static inline unsigned char FAR* chunkcopy_core(unsigned char FAR* out,
                                                const unsigned char FAR* from,
                                                unsigned len);

/* Architecture specific code starts here. */
static inline uint8x16_t chunkset_vld1q_dup_u8x8(
    const unsigned char FAR* Z_RESTRICT from) {
#if defined(__clang__) || defined(__aarch64__)
  return vreinterpretq_u8_u64(vld1q_dup_u64((void*)from));
#else
  /* 32-bit GCC uses an alignment hint for vld1q_dup_u64, even when given a
   * void pointer, so here's an alternate implementation.
   */
  uint8x8_t h = vld1_u8(from);
  return vcombine_u8(h, h);
#endif
}

/*
   Perform an overlapping copy which behaves as a memset() operation, but
   supporting periods other than one, and assume that length is non-zero and
   that it's OK to overwrite at least CHUNKCOPY_CHUNK_SIZE*3 bytes of output
   even if the length is shorter than this.
   TODO(cavalcantii): maybe rename vreinterpretq and chunkset_vld to make it
                      generic and move this code to chunkcopy.h (plus we
                      won't need the forward declarations).
 */
static inline unsigned char FAR* chunkset_core(unsigned char FAR* out,
                                               unsigned period,
                                               unsigned len) {
  uint8x16_t f;
  int bump = ((len - 1) % sizeof(f)) + 1;

  switch (period) {
    case 1:
      f = vld1q_dup_u8(out - 1);
      vst1q_u8(out, f);
      out += bump;
      len -= bump;
      while (len > 0) {
        vst1q_u8(out, f);
        out += sizeof(f);
        len -= sizeof(f);
      }
      return out;
    case 2:
      f = vreinterpretq_u8_u16(vld1q_dup_u16((void*)(out - 2)));
      vst1q_u8(out, f);
      out += bump;
      len -= bump;
      if (len > 0) {
        f = vreinterpretq_u8_u16(vld1q_dup_u16((void*)(out - 2)));
        do {
          vst1q_u8(out, f);
          out += sizeof(f);
          len -= sizeof(f);
        } while (len > 0);
      }
      return out;
    case 4:
      f = vreinterpretq_u8_u32(vld1q_dup_u32((void*)(out - 4)));
      vst1q_u8(out, f);
      out += bump;
      len -= bump;
      if (len > 0) {
        f = vreinterpretq_u8_u32(vld1q_dup_u32((void*)(out - 4)));
        do {
          vst1q_u8(out, f);
          out += sizeof(f);
          len -= sizeof(f);
        } while (len > 0);
      }
      return out;
    case 8:
      f = chunkset_vld1q_dup_u8x8(out - 8);
      vst1q_u8(out, f);
      out += bump;
      len -= bump;
      if (len > 0) {
        f = chunkset_vld1q_dup_u8x8(out - 8);
        do {
          vst1q_u8(out, f);
          out += sizeof(f);
          len -= sizeof(f);
        } while (len > 0);
      }
      return out;
  }
  out = chunkunroll_relaxed(out, &period, &len);
  return chunkcopy_core(out, out - period, len);
}

#endif /* CHUNKCOPY_ARM_H */
