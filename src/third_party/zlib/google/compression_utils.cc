// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/zlib/google/compression_utils.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "base/bit_cast.h"
#include "base/logging.h"
#include "base/process/memory.h"
#include "base/strings/string_piece.h"
#include "base/sys_byteorder.h"

#if defined(USE_SYSTEM_ZLIB)
#include <zlib.h>
#else
#include "third_party/zlib/zlib.h"
#endif

namespace {

// The difference in bytes between a zlib header and a gzip header.
const size_t kGzipZlibHeaderDifferenceBytes = 16;

// Pass an integer greater than the following get a gzip header instead of a
// zlib header when calling deflateInit2() and inflateInit2().
const int kWindowBitsToGetGzipHeader = 16;

// This describes the amount of memory zlib uses to compress data. It can go
// from 1 to 9, with 8 being the default. For details, see:
// http://www.zlib.net/manual.html (search for memLevel).
const int kZlibMemoryLevel = 8;

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

  stream.next_in = bit_cast<Bytef*>(source);
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
    DCHECK(free_fn);
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
  int err = deflateInit2(&stream,
                         Z_DEFAULT_COMPRESSION,
                         Z_DEFLATED,
                         MAX_WBITS + kWindowBitsToGetGzipHeader,
                         kZlibMemoryLevel,
                         Z_DEFAULT_STRATEGY);
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

  stream.next_in = bit_cast<Bytef*>(source);
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

}  // namespace

namespace compression {

bool GzipCompress(base::StringPiece input,
                  char* output_buffer,
                  size_t output_buffer_size,
                  size_t* compressed_size,
                  void* (*malloc_fn)(size_t),
                  void (*free_fn)(void*)) {
  static_assert(sizeof(Bytef) == 1, "");

  // uLongf can be larger than size_t.
  uLongf compressed_size_long = static_cast<uLongf>(output_buffer_size);
  if (GzipCompressHelper(bit_cast<Bytef*>(output_buffer), &compressed_size_long,
                         bit_cast<const Bytef*>(input.data()),
                         static_cast<uLongf>(input.size()), malloc_fn,
                         free_fn) != Z_OK) {
    return false;
  }
  // No overflow, as compressed_size_long <= output.size() which is a size_t.
  *compressed_size = static_cast<size_t>(compressed_size_long);
  return true;
}

bool GzipCompress(base::StringPiece input, std::string* output) {
  // Not using std::vector<> because allocation failures are recoverable,
  // which is hidden by std::vector<>.
  static_assert(sizeof(Bytef) == 1, "");
  const uLongf input_size = static_cast<uLongf>(input.size());

  uLongf compressed_data_size =
      kGzipZlibHeaderDifferenceBytes + compressBound(input_size);
  Bytef* compressed_data;
  if (!base::UncheckedMalloc(compressed_data_size,
                             reinterpret_cast<void**>(&compressed_data))) {
    return false;
  }

  if (GzipCompressHelper(compressed_data, &compressed_data_size,
                         bit_cast<const Bytef*>(input.data()), input_size,
                         nullptr, nullptr) != Z_OK) {
    free(compressed_data);
    return false;
  }

  Bytef* resized_data =
      reinterpret_cast<Bytef*>(realloc(compressed_data, compressed_data_size));
  if (!resized_data) {
    free(compressed_data);
    return false;
  }
  output->assign(resized_data, resized_data + compressed_data_size);
  DCHECK_EQ(input_size, GetUncompressedSize(*output));

  free(resized_data);
  return true;
}

bool GzipUncompress(const std::string& input, std::string* output) {
  std::string uncompressed_output;
  uLongf uncompressed_size = static_cast<uLongf>(GetUncompressedSize(input));
  if (uncompressed_size > uncompressed_output.max_size())
    return false;

  uncompressed_output.resize(uncompressed_size);
  if (GzipUncompressHelper(bit_cast<Bytef*>(uncompressed_output.data()),
                           &uncompressed_size,
                           bit_cast<const Bytef*>(input.data()),
                           static_cast<uLongf>(input.length())) == Z_OK) {
    output->swap(uncompressed_output);
    return true;
  }
  return false;
}

bool GzipUncompress(base::StringPiece input, base::StringPiece output) {
  uLongf uncompressed_size = GetUncompressedSize(input);
  if (uncompressed_size > output.size())
    return false;
  return GzipUncompressHelper(bit_cast<Bytef*>(output.data()),
                              &uncompressed_size,
                              bit_cast<const Bytef*>(input.data()),
                              static_cast<uLongf>(input.length())) == Z_OK;
}

bool GzipUncompress(base::StringPiece input, std::string* output) {
  // Disallow in-place usage, i.e., |input| using |*output| as underlying data.
  DCHECK_NE(input.data(), output->data());
  uLongf uncompressed_size = GetUncompressedSize(input);
  output->resize(uncompressed_size);
  return GzipUncompressHelper(bit_cast<Bytef*>(output->data()),
                              &uncompressed_size,
                              bit_cast<const Bytef*>(input.data()),
                              static_cast<uLongf>(input.length())) == Z_OK;
}

uint32_t GetUncompressedSize(base::StringPiece compressed_data) {
  // The uncompressed size is stored in the last 4 bytes of |input| in LE.
  uint32_t size;
  if (compressed_data.length() < sizeof(size))
    return 0;
  memcpy(&size,
         &compressed_data.data()[compressed_data.length() - sizeof(size)],
         sizeof(size));
  return base::ByteSwapToLE32(size);
}

}  // namespace compression
