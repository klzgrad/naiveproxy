/* chunkcopy.h -- fast copies and sets
 * Copyright (C) 2017 ARM, Inc.
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

#ifndef CHUNKCOPY_H
#define CHUNKCOPY_H

#include <arm_neon.h>
#include "zutil.h"

#if __STDC_VERSION__ >= 199901L
#define Z_RESTRICT restrict
#else
#define Z_RESTRICT
#endif

typedef uint8x16_t chunkcopy_chunk_t;
#define CHUNKCOPY_CHUNK_SIZE sizeof(chunkcopy_chunk_t)

/*
   Ask the compiler to perform a wide, unaligned load with an machine
   instruction appropriate for the chunkcopy_chunk_t type.
 */
static inline chunkcopy_chunk_t loadchunk(const unsigned char FAR* s) {
  chunkcopy_chunk_t c;
  __builtin_memcpy(&c, s, sizeof(c));
  return c;
}

/*
   Ask the compiler to perform a wide, unaligned store with an machine
   instruction appropriate for the chunkcopy_chunk_t type.
 */
static inline void storechunk(unsigned char FAR* d, chunkcopy_chunk_t c) {
  __builtin_memcpy(d, &c, sizeof(c));
}

/*
   Perform a memcpy-like operation, but assume that length is non-zero and that
   it's OK to overwrite at least CHUNKCOPY_CHUNK_SIZE bytes of output even if
   the length is shorter than this.

   It also guarantees that it will properly unroll the data if the distance
   between `out` and `from` is at least CHUNKCOPY_CHUNK_SIZE, which we rely on
   in chunkcopy_relaxed().

   Aside from better memory bus utilisation, this means that short copies
   (CHUNKCOPY_CHUNK_SIZE bytes or fewer) will fall straight through the loop
   without iteration, which will hopefully make the branch prediction more
   reliable.
 */
static inline unsigned char FAR* chunkcopy_core(unsigned char FAR* out,
                                                const unsigned char FAR* from,
                                                unsigned len) {
  int bump = (--len % CHUNKCOPY_CHUNK_SIZE) + 1;
  storechunk(out, loadchunk(from));
  out += bump;
  from += bump;
  len /= CHUNKCOPY_CHUNK_SIZE;
  while (len-- > 0) {
    storechunk(out, loadchunk(from));
    out += CHUNKCOPY_CHUNK_SIZE;
    from += CHUNKCOPY_CHUNK_SIZE;
  }
  return out;
}

/*
   Like chunkcopy_core, but avoid writing beyond of legal output.

   Accepts an additional pointer to the end of safe output.  A generic safe
   copy would use (out + len), but it's normally the case that the end of the
   output buffer is beyond the end of the current copy, and this can still be
   exploited.
 */
static inline unsigned char FAR* chunkcopy_core_safe(
    unsigned char FAR* out,
    const unsigned char FAR* from,
    unsigned len,
    unsigned char FAR* limit) {
  Assert(out + len <= limit, "chunk copy exceeds safety limit");
  if (limit - out < CHUNKCOPY_CHUNK_SIZE) {
    const unsigned char FAR* Z_RESTRICT rfrom = from;
    if (len & 8) {
      __builtin_memcpy(out, rfrom, 8);
      out += 8;
      rfrom += 8;
    }
    if (len & 4) {
      __builtin_memcpy(out, rfrom, 4);
      out += 4;
      rfrom += 4;
    }
    if (len & 2) {
      __builtin_memcpy(out, rfrom, 2);
      out += 2;
      rfrom += 2;
    }
    if (len & 1) {
      *out++ = *rfrom++;
    }
    return out;
  }
  return chunkcopy_core(out, from, len);
}

/*
   Perform short copies until distance can be rewritten as being at least
   CHUNKCOPY_CHUNK_SIZE.

   This assumes that it's OK to overwrite at least the first
   2*CHUNKCOPY_CHUNK_SIZE bytes of output even if the copy is shorter than
   this.  This assumption holds within inflate_fast() which starts every
   iteration with at least 258 bytes of output space available (258 being the
   maximum length output from a single token; see inffast.c).
 */
static inline unsigned char FAR* chunkunroll_relaxed(unsigned char FAR* out,
                                                     unsigned FAR* dist,
                                                     unsigned FAR* len) {
  const unsigned char FAR* from = out - *dist;
  while (*dist < *len && *dist < CHUNKCOPY_CHUNK_SIZE) {
    storechunk(out, loadchunk(from));
    out += *dist;
    *len -= *dist;
    *dist += *dist;
  }
  return out;
}

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

/*
   Perform a memcpy-like operation, but assume that length is non-zero and that
   it's OK to overwrite at least CHUNKCOPY_CHUNK_SIZE bytes of output even if
   the length is shorter than this.

   Unlike chunkcopy_core() above, no guarantee is made regarding the behaviour
   of overlapping buffers, regardless of the distance between the pointers.
   This is reflected in the `restrict`-qualified pointers, allowing the
   compiler to reorder loads and stores.
 */
static inline unsigned char FAR* chunkcopy_relaxed(
    unsigned char FAR* Z_RESTRICT out,
    const unsigned char FAR* Z_RESTRICT from,
    unsigned len) {
  return chunkcopy_core(out, from, len);
}

/*
   Like chunkcopy_relaxed, but avoid writing beyond of legal output.

   Unlike chunkcopy_core_safe() above, no guarantee is made regarding the
   behaviour of overlapping buffers, regardless of the distance between the
   pointers.  This is reflected in the `restrict`-qualified pointers, allowing
   the compiler to reorder loads and stores.

   Accepts an additional pointer to the end of safe output.  A generic safe
   copy would use (out + len), but it's normally the case that the end of the
   output buffer is beyond the end of the current copy, and this can still be
   exploited.
 */
static inline unsigned char FAR* chunkcopy_safe(
    unsigned char FAR* out,
    const unsigned char FAR* Z_RESTRICT from,
    unsigned len,
    unsigned char FAR* limit) {
  Assert(out + len <= limit, "chunk copy exceeds safety limit");
  return chunkcopy_core_safe(out, from, len, limit);
}

/*
   Perform chunky copy within the same buffer, where the source and destination
   may potentially overlap.

   Assumes that len > 0 on entry, and that it's safe to write at least
   CHUNKCOPY_CHUNK_SIZE*3 bytes to the output.
 */
static inline unsigned char FAR*
chunkcopy_lapped_relaxed(unsigned char FAR* out, unsigned dist, unsigned len) {
  if (dist < len && dist < CHUNKCOPY_CHUNK_SIZE) {
    return chunkset_core(out, dist, len);
  }
  return chunkcopy_core(out, out - dist, len);
}

/*
   Behave like chunkcopy_lapped_relaxed, but avoid writing beyond of legal
   output.

   Accepts an additional pointer to the end of safe output.  A generic safe
   copy would use (out + len), but it's normally the case that the end of the
   output buffer is beyond the end of the current copy, and this can still be
   exploited.
 */
static inline unsigned char FAR* chunkcopy_lapped_safe(
    unsigned char FAR* out,
    unsigned dist,
    unsigned len,
    unsigned char FAR* limit) {
  Assert(out + len <= limit, "chunk copy exceeds safety limit");
  if (limit - out < CHUNKCOPY_CHUNK_SIZE * 3) {
    /* TODO: try harder to optimise this */
    while (len-- > 0) {
      *out = *(out - dist);
      out++;
    }
    return out;
  }
  return chunkcopy_lapped_relaxed(out, dist, len);
}

#undef Z_RESTRICT

#endif /* CHUNKCOPY_H */
