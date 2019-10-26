/* compression_utils_portable.cc
 *
 * Copyright 2019 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the Chromium source repository LICENSE file.
 */

#include "third_party/zlib/google/compression_utils_portable.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace zlib_internal {

// The difference in bytes between a zlib header and a gzip header.
const size_t kGzipZlibHeaderDifferenceBytes = 16;

// Pass an integer greater than the following get a gzip header instead of a
// zlib header when calling deflateInit2() and inflateInit2().
const int kWindowBitsToGetGzipHeader = 16;

// This describes the amount of memory zlib uses to compress data. It can go
// from 1 to 9, with 8 being the default. For details, see:
// http://www.zlib.net/manual.html (search for memLevel).
const int kZlibMemoryLevel = 8;

// The expected compressed size is based on the input size factored by
// internal Zlib constants (e.g. window size, etc) plus the wrapper
// header size.
uLongf GZipExpectedCompressedSize(uLongf input_size) {
  return kGzipZlibHeaderDifferenceBytes + compressBound(input_size);
}

// This code is taken almost verbatim from third_party/zlib/compress.c. The only
// difference is deflateInit2() is called which sets the window bits to be > 16.
// That causes a gzip header to be emitted rather than a zlib header.
int GzipCompressHelper(Bytef* dest,
                       uLongf* dest_length,
                       const Bytef* source,
                       uLong source_length,
                       void* (*malloc_fn)(size_t),
                       void (*free_fn)(void*)) {
  z_stream stream;

  // FIXME(cavalcantii): z_const is not defined as 'const'.
  stream.next_in = static_cast<z_const Bytef*>(const_cast<Bytef*>(source));
  stream.avail_in = static_cast<uInt>(source_length);
  stream.next_out = dest;
  stream.avail_out = static_cast<uInt>(*dest_length);
  if (static_cast<uLong>(stream.avail_out) != *dest_length)
    return Z_BUF_ERROR;

  // Cannot convert capturing lambdas to function pointers directly, hence the
  // structure.
  struct MallocFreeFunctions {
    void* (*malloc_fn)(size_t);
    void (*free_fn)(void*);
  } malloc_free = {malloc_fn, free_fn};

  if (malloc_fn) {
    if (!free_fn)
      return Z_BUF_ERROR;

    auto zalloc = [](void* opaque, uInt items, uInt size) {
      return reinterpret_cast<MallocFreeFunctions*>(opaque)->malloc_fn(items *
                                                                       size);
    };
    auto zfree = [](void* opaque, void* address) {
      return reinterpret_cast<MallocFreeFunctions*>(opaque)->free_fn(address);
    };

    stream.zalloc = static_cast<alloc_func>(zalloc);
    stream.zfree = static_cast<free_func>(zfree);
    stream.opaque = static_cast<voidpf>(&malloc_free);
  } else {
    stream.zalloc = static_cast<alloc_func>(0);
    stream.zfree = static_cast<free_func>(0);
    stream.opaque = static_cast<voidpf>(0);
  }

  gz_header gzip_header;
  memset(&gzip_header, 0, sizeof(gzip_header));
  int err = deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                         MAX_WBITS + kWindowBitsToGetGzipHeader,
                         kZlibMemoryLevel, Z_DEFAULT_STRATEGY);
  if (err != Z_OK)
    return err;

  err = deflateSetHeader(&stream, &gzip_header);
  if (err != Z_OK)
    return err;

  err = deflate(&stream, Z_FINISH);
  if (err != Z_STREAM_END) {
    deflateEnd(&stream);
    return err == Z_OK ? Z_BUF_ERROR : err;
  }
  *dest_length = stream.total_out;

  err = deflateEnd(&stream);
  return err;
}

// This code is taken almost verbatim from third_party/zlib/uncompr.c. The only
// difference is inflateInit2() is called which sets the window bits to be > 16.
// That causes a gzip header to be parsed rather than a zlib header.
int GzipUncompressHelper(Bytef* dest,
                         uLongf* dest_length,
                         const Bytef* source,
                         uLong source_length) {
  z_stream stream;

  // FIXME(cavalcantii): z_const is not defined as 'const'.
  stream.next_in = static_cast<z_const Bytef*>(const_cast<Bytef*>(source));
  stream.avail_in = static_cast<uInt>(source_length);
  if (static_cast<uLong>(stream.avail_in) != source_length)
    return Z_BUF_ERROR;

  stream.next_out = dest;
  stream.avail_out = static_cast<uInt>(*dest_length);
  if (static_cast<uLong>(stream.avail_out) != *dest_length)
    return Z_BUF_ERROR;

  stream.zalloc = static_cast<alloc_func>(0);
  stream.zfree = static_cast<free_func>(0);

  int err = inflateInit2(&stream, MAX_WBITS + kWindowBitsToGetGzipHeader);
  if (err != Z_OK)
    return err;

  err = inflate(&stream, Z_FINISH);
  if (err != Z_STREAM_END) {
    inflateEnd(&stream);
    if (err == Z_NEED_DICT || (err == Z_BUF_ERROR && stream.avail_in == 0))
      return Z_DATA_ERROR;
    return err;
  }
  *dest_length = stream.total_out;

  err = inflateEnd(&stream);
  return err;
}

}  // namespace zlib_internal
