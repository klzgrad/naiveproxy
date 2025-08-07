/*
 * Copyright (C) 2022 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/trace_processor/util/zip_reader.h"

#include <cstdint>
#include <cstring>
#include <ctime>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/util/gzip_utils.h"
#include "src/trace_processor/util/streaming_line_reader.h"

#if PERFETTO_BUILDFLAG(PERFETTO_ZLIB)
#include <zconf.h>
#include <zlib.h>
#endif

namespace perfetto::trace_processor::util {

namespace {

// Entry signatures.
constexpr uint32_t kFileHeaderSig = 0x04034b50;
constexpr uint32_t kCentralDirectorySig = 0x02014b50;
constexpr uint32_t kDataDescriptorSig = 0x08074b50;

// 4 bytes each of: 1) signature, 2) crc, 3) compressed size 4) uncompressed
// size.
constexpr uint32_t kDataDescriptorSize = 4 * 4;

enum GeneralPurposeBitFlag : uint32_t {
  kEncrypted = 1 << 0,
  k8kSlidingDictionary = 1u << 1,
  kShannonFaro = 1u << 2,
  kDataDescriptor = 1u << 3,
  kLangageEncoding = 1u << 11,
  kUnknown = ~(kEncrypted | k8kSlidingDictionary | kShannonFaro |
               kDataDescriptor | kLangageEncoding),
};

// Compression flags.
const uint16_t kNoCompression = 0;
const uint16_t kDeflate = 8;

template <typename T>
T ReadAndAdvance(const uint8_t** ptr) {
  T res{};
  memcpy(base::AssumeLittleEndian(&res), *ptr, sizeof(T));
  *ptr += sizeof(T);
  return res;
}

}  // namespace

ZipReader::ZipReader() = default;
ZipReader::~ZipReader() = default;

base::Status ZipReader::Parse(TraceBlobView tbv) {
  reader_.PushBack(std::move(tbv));

  // .zip file sequence:
  // [ File 1 header (30 bytes) ]
  // [ File 1 name ]
  // [ File 1 extra fields (optional) ]
  // [ File 1 compressed payload ]
  // [ File 1 data descriptor (optional) ]
  //
  // [ File 2 header (30 bytes) ]
  // [ File 2 name ]
  // [ File 2 extra fields (optional) ]
  // [ File 2 compressed payload ]
  // [ File 2 data descriptor (optional) ]
  //
  // [ Central directory (ignored) ]

  for (;;) {
    auto state = cur_.parse_state;
    switch (state) {
      case FileParseState::kHeader:
        RETURN_IF_ERROR(TryParseHeader());
        break;
      case FileParseState::kFilename:
        RETURN_IF_ERROR(TryParseFilename());
        break;
      case FileParseState::kSkipBytes:
        RETURN_IF_ERROR(TrySkipBytes());
        break;
      case FileParseState::kCompressedData:
        RETURN_IF_ERROR(TryParseCompressedData());
        break;
    }
    if (state == cur_.parse_state) {
      return base::OkStatus();
    }
  }
}

base::Status ZipReader::TryParseHeader() {
  PERFETTO_CHECK(cur_.hdr.signature == 0);

  std::optional<TraceBlobView> hdr =
      reader_.SliceOff(reader_.start_offset(), kZipFileHdrSize);
  if (!hdr) {
    return base::OkStatus();
  }
  PERFETTO_CHECK(reader_.PopFrontBytes(kZipFileHdrSize));

  const uint8_t* hdr_it = hdr->data();
  cur_.hdr.signature = ReadAndAdvance<uint32_t>(&hdr_it);
  if (cur_.hdr.signature == kCentralDirectorySig) {
    // We reached the central directory at the end of file.
    // We don't make any use here of the central directory, so we just
    // ignore everything else after this point.
    // Here we abuse the ZipFile class a bit. The Central Directory header
    // has a different layout. The first 4 bytes (signature) match, the
    // rest don't but the sizeof(central dir) is >> sizeof(file header) so
    // we are fine.
    // We do this rather than retuning because we could have further
    // Parse() calls (imagine parsing bytes one by one), and we need a way
    // to keep track of the "keep eating input without doing anything".
    cur_.ignore_bytes_after_fname = std::numeric_limits<size_t>::max();
    cur_.parse_state = FileParseState::kSkipBytes;
    return base::OkStatus();
  }
  if (cur_.hdr.signature != kFileHeaderSig) {
    return base::ErrStatus(
        "Invalid signature found at offset 0x%zx. Actual=0x%x, "
        "expected=0x%x",
        reader_.start_offset(), cur_.hdr.signature, kFileHeaderSig);
  }

  cur_.hdr.version = ReadAndAdvance<uint16_t>(&hdr_it);
  cur_.hdr.flags = ReadAndAdvance<uint16_t>(&hdr_it);
  cur_.hdr.compression = ReadAndAdvance<uint16_t>(&hdr_it);
  cur_.hdr.mtime = ReadAndAdvance<uint16_t>(&hdr_it);
  cur_.hdr.mdate = ReadAndAdvance<uint16_t>(&hdr_it);
  cur_.hdr.checksum = ReadAndAdvance<uint32_t>(&hdr_it);
  cur_.hdr.compressed_size = ReadAndAdvance<uint32_t>(&hdr_it);
  cur_.hdr.uncompressed_size = ReadAndAdvance<uint32_t>(&hdr_it);
  cur_.hdr.fname_len = ReadAndAdvance<uint16_t>(&hdr_it);
  cur_.hdr.extra_field_len = ReadAndAdvance<uint16_t>(&hdr_it);
  PERFETTO_DCHECK(static_cast<size_t>(hdr_it - hdr->data()) == kZipFileHdrSize);

  // We support only up to version 2.0 (20). Higher versions define
  // more advanced features that we don't support (zip64 extensions,
  // encryption).
  // Disallow encryption or any flags we don't know how to handle.
  if ((cur_.hdr.version > 20) || (cur_.hdr.flags & kEncrypted) ||
      (cur_.hdr.flags & kUnknown)) {
    return base::ErrStatus(
        "Unsupported zip features at offset 0x%zx. version=%x, flags=%x",
        reader_.start_offset(), cur_.hdr.version, cur_.hdr.flags);
  }
  if (cur_.hdr.compression != kNoCompression &&
      cur_.hdr.compression != kDeflate) {
    return base::ErrStatus(
        "Unsupported compression type at offset 0x%zx. type=%x. Only "
        "deflate and no compression are supported.",
        reader_.start_offset(), cur_.hdr.compression);
  }
  if (cur_.hdr.flags & kDataDescriptor && cur_.hdr.compression != kDeflate) {
    return base::ErrStatus(
        "Unsupported compression type at offset 0x%zx. type=%x. Only "
        "deflate supported for ZIPs compressed in a streaming fashion.",
        reader_.start_offset(), cur_.hdr.compression);
  }
  cur_.ignore_bytes_after_fname = cur_.hdr.extra_field_len;
  cur_.parse_state = FileParseState::kFilename;
  return base::OkStatus();
}

base::Status ZipReader::TryParseFilename() {
  if (cur_.hdr.fname_len == 0) {
    cur_.parse_state = FileParseState::kSkipBytes;
    return base::OkStatus();
  }
  PERFETTO_CHECK(cur_.hdr.fname.empty());

  std::optional<TraceBlobView> fname_tbv =
      reader_.SliceOff(reader_.start_offset(), cur_.hdr.fname_len);
  if (!fname_tbv) {
    return base::OkStatus();
  }
  PERFETTO_CHECK(reader_.PopFrontBytes(cur_.hdr.fname_len));
  cur_.hdr.fname = std::string(reinterpret_cast<const char*>(fname_tbv->data()),
                               cur_.hdr.fname_len);
  cur_.parse_state = FileParseState::kSkipBytes;
  return base::OkStatus();
}

base::Status ZipReader::TrySkipBytes() {
  if (cur_.ignore_bytes_after_fname == 0) {
    cur_.parse_state = FileParseState::kCompressedData;
    return base::OkStatus();
  }

  size_t avail = reader_.avail();
  if (avail < cur_.ignore_bytes_after_fname) {
    PERFETTO_CHECK(reader_.PopFrontBytes(avail));
    cur_.ignore_bytes_after_fname -= avail;
    return base::OkStatus();
  }
  PERFETTO_CHECK(reader_.PopFrontBytes(cur_.ignore_bytes_after_fname));
  cur_.ignore_bytes_after_fname = 0;
  cur_.parse_state = FileParseState::kCompressedData;
  return base::OkStatus();
}

base::Status ZipReader::TryParseCompressedData() {
  // Build up the compressed payload
  if (cur_.hdr.flags & kDataDescriptor) {
    if (!cur_.compressed) {
      ASSIGN_OR_RETURN(auto compressed, TryParseUnsizedCompressedData());
      if (!compressed) {
        return base::OkStatus();
      }
      cur_.compressed = std::move(compressed);
    }

    std::optional<TraceBlobView> data_descriptor =
        reader_.SliceOff(reader_.start_offset(), kDataDescriptorSize);
    if (!data_descriptor) {
      return base::OkStatus();
    }
    PERFETTO_CHECK(reader_.PopFrontBytes(kDataDescriptorSize));

    const auto* desc_it = data_descriptor->data();
    auto desc_sig = ReadAndAdvance<uint32_t>(&desc_it);
    if (desc_sig != kDataDescriptorSig) {
      return base::ErrStatus(
          "Invalid signature found at offset 0x%zx. Actual=0x%x, "
          "expected=0x%x",
          reader_.start_offset(), desc_sig, kDataDescriptorSig);
    }
    cur_.hdr.checksum = ReadAndAdvance<uint32_t>(&desc_it);
    cur_.hdr.compressed_size = ReadAndAdvance<uint32_t>(&desc_it);
    cur_.hdr.uncompressed_size = ReadAndAdvance<uint32_t>(&desc_it);
  } else {
    PERFETTO_CHECK(!cur_.compressed);
    std::optional<TraceBlobView> raw_compressed =
        reader_.SliceOff(reader_.start_offset(), cur_.hdr.compressed_size);
    if (!raw_compressed) {
      return base::OkStatus();
    }
    cur_.compressed = *std::move(raw_compressed);
    PERFETTO_CHECK(reader_.PopFrontBytes(cur_.hdr.compressed_size));
  }

  // We have accumulated the whole header, file name and compressed payload.
  PERFETTO_CHECK(cur_.compressed);
  PERFETTO_CHECK(cur_.hdr.fname.size() == cur_.hdr.fname_len);
  PERFETTO_CHECK(cur_.compressed->size() == cur_.hdr.compressed_size);
  PERFETTO_CHECK(cur_.ignore_bytes_after_fname == 0);

  files_.emplace_back();
  files_.back().hdr_ = std::move(cur_.hdr);
  files_.back().compressed_data_ = *std::move(cur_.compressed);
  cur_ = FileParseState();  // Reset the parsing state for the next file.
  return base::OkStatus();
}  // namespace perfetto::trace_processor::util

base::StatusOr<std::optional<TraceBlobView>>
ZipReader::TryParseUnsizedCompressedData() {
  PERFETTO_CHECK(cur_.hdr.compression == kDeflate);

  auto start = reader_.start_offset() + cur_.decompressor_bytes_fed;
  auto end = reader_.end_offset();
  auto slice = reader_.SliceOff(start, end - start);
  PERFETTO_CHECK(slice);
  auto res_code = cur_.decompressor.FeedAndExtract(slice->data(), slice->size(),
                                                   [](const uint8_t*, size_t) {
                                                     // Intentionally do
                                                     // nothing: we are only
                                                     // looking for the bounds
                                                     // of the deflate stream,
                                                     // we are not actually
                                                     // interested in the
                                                     // output.
                                                   });
  switch (res_code) {
    case GzipDecompressor::ResultCode::kNeedsMoreInput:
      cur_.decompressor_bytes_fed += slice->size();
      return {std::nullopt};
    case GzipDecompressor::ResultCode::kError:
      return base::ErrStatus(
          "Failed decompressing stream in ZIP file at offset 0x%zx",
          reader_.start_offset());
    case GzipDecompressor::ResultCode::kOk:
      PERFETTO_FATAL("Unexpected result code");
    case GzipDecompressor::ResultCode::kEof:
      break;
  }
  cur_.decompressor_bytes_fed += slice->size() - cur_.decompressor.AvailIn();
  auto raw_compressed =
      reader_.SliceOff(reader_.start_offset(), cur_.decompressor_bytes_fed);
  PERFETTO_CHECK(raw_compressed);
  PERFETTO_CHECK(reader_.PopFrontBytes(cur_.decompressor_bytes_fed));
  return {std::move(raw_compressed)};
}

ZipFile* ZipReader::Find(const std::string& path) {
  for (ZipFile& zf : files_) {
    if (zf.name() == path)
      return &zf;
  }
  return nullptr;
}

ZipFile::ZipFile() = default;
ZipFile::~ZipFile() = default;
ZipFile::ZipFile(ZipFile&& other) noexcept = default;
ZipFile& ZipFile::operator=(ZipFile&& other) noexcept = default;

base::Status ZipFile::Decompress(std::vector<uint8_t>* out_data) const {
  out_data->clear();
  RETURN_IF_ERROR(DoDecompressionChecks());

  if (hdr_.compression == kNoCompression) {
    const uint8_t* data = compressed_data_.data();
    out_data->insert(out_data->end(), data, data + hdr_.compressed_size);
    return base::OkStatus();
  }

  if (hdr_.uncompressed_size == 0) {
    return base::OkStatus();
  }

  PERFETTO_DCHECK(hdr_.compression == kDeflate);
  GzipDecompressor dec(GzipDecompressor::InputMode::kRawDeflate);
  dec.Feed(compressed_data_.data(), hdr_.compressed_size);

  out_data->resize(hdr_.uncompressed_size);
  auto dec_res = dec.ExtractOutput(out_data->data(), out_data->size());
  if (dec_res.ret != GzipDecompressor::ResultCode::kEof) {
    return base::ErrStatus("Zip decompression error (%d) on %s (c=%u, u=%u)",
                           static_cast<int>(dec_res.ret), hdr_.fname.c_str(),
                           hdr_.compressed_size, hdr_.uncompressed_size);
  }
  out_data->resize(dec_res.bytes_written);

#if PERFETTO_BUILDFLAG(PERFETTO_ZLIB)
  const auto* crc_data = reinterpret_cast<const ::Bytef*>(out_data->data());
  auto crc_len = static_cast<::uInt>(out_data->size());
  auto actual_crc32 = static_cast<uint32_t>(::crc32(0u, crc_data, crc_len));
  if (actual_crc32 != hdr_.checksum) {
    return base::ErrStatus("Zip CRC32 failure on %s (actual: %x, expected: %x)",
                           hdr_.fname.c_str(), actual_crc32, hdr_.checksum);
  }
#endif

  return base::OkStatus();
}

base::Status ZipFile::DecompressLines(LinesCallback callback) const {
  using ResultCode = GzipDecompressor::ResultCode;
  RETURN_IF_ERROR(DoDecompressionChecks());

  StreamingLineReader line_reader(std::move(callback));

  if (hdr_.compression == kNoCompression) {
    line_reader.Tokenize(
        base::StringView(reinterpret_cast<const char*>(compressed_data_.data()),
                         hdr_.compressed_size));
    return base::OkStatus();
  }

  PERFETTO_DCHECK(hdr_.compression == kDeflate);
  GzipDecompressor dec(GzipDecompressor::InputMode::kRawDeflate);
  dec.Feed(compressed_data_.data(), hdr_.compressed_size);

  static constexpr size_t kChunkSize = 32768;
  GzipDecompressor::Result dec_res;
  do {
    auto* wptr = reinterpret_cast<uint8_t*>(line_reader.BeginWrite(kChunkSize));
    dec_res = dec.ExtractOutput(wptr, kChunkSize);
    if (dec_res.ret == ResultCode::kError ||
        dec_res.ret == ResultCode::kNeedsMoreInput) {
      return base::ErrStatus("zlib decompression error on %s (%d)",
                             name().c_str(), static_cast<int>(dec_res.ret));
    }
    PERFETTO_DCHECK(dec_res.bytes_written <= kChunkSize);
    line_reader.EndWrite(dec_res.bytes_written);
  } while (dec_res.ret == ResultCode::kOk);
  return base::OkStatus();
}

// Common logic for both Decompress() and DecompressLines().
base::Status ZipFile::DoDecompressionChecks() const {
  if (hdr_.compression == kNoCompression) {
    PERFETTO_CHECK(hdr_.compressed_size == hdr_.uncompressed_size);
    return base::OkStatus();
  }
  if (hdr_.compression != kDeflate) {
    return base::ErrStatus("Zip compression mode not supported (%u)",
                           hdr_.compression);
  }
  if (!IsGzipSupported()) {
    return base::ErrStatus(
        "Cannot open zip file. Gzip is not enabled in the current build. "
        "Rebuild with enable_perfetto_zlib=true");
  }
  return base::OkStatus();
}

// Returns a 64-bit version of time_t, that is, the num seconds since the
// Epoch.
int64_t ZipFile::GetDatetime() const {
  // Date: 7 bits year, 4 bits month, 5 bits day.
  // Time: 5 bits hour, 6 bits minute, 5 bits second.
  struct tm mdt{};
  // As per man 3 mktime, `tm_year` is relative to 1900 not Epoch. Go figure.
  mdt.tm_year = 1980 + (hdr_.mdate >> (16 - 7)) - 1900;

  // As per the man page, the month ranges 0 to 11 (Jan = 0).
  mdt.tm_mon = ((hdr_.mdate >> (16 - 7 - 4)) & 0x0f) - 1;

  // However, still according to the same man page, the day starts from 1.
  mdt.tm_mday = hdr_.mdate & 0x1f;

  mdt.tm_hour = hdr_.mtime >> (16 - 5);
  mdt.tm_min = (hdr_.mtime >> (16 - 5 - 6)) & 0x3f;

  // Seconds in the DOS format have only 5 bits, so they lose the last bit of
  // resolution, hence the * 2.
  mdt.tm_sec = (hdr_.mtime & 0x1f) * 2;
  return base::TimeGm(&mdt);
}

std::string ZipFile::GetDatetimeStr() const {
  char buf[32]{};
  time_t secs = static_cast<time_t>(GetDatetime());
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", gmtime(&secs));
  buf[sizeof(buf) - 1] = '\0';
  return buf;
}

}  // namespace perfetto::trace_processor::util
