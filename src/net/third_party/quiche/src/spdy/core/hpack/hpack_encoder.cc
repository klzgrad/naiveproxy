// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/spdy/core/hpack/hpack_encoder.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "net/third_party/quiche/src/spdy/core/hpack/hpack_constants.h"
#include "net/third_party/quiche/src/spdy/core/hpack/hpack_header_table.h"
#include "net/third_party/quiche/src/spdy/core/hpack/hpack_huffman_table.h"
#include "net/third_party/quiche/src/spdy/core/hpack/hpack_output_stream.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_estimate_memory_usage.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_logging.h"

namespace spdy {

class HpackEncoder::RepresentationIterator {
 public:
  // |pseudo_headers| and |regular_headers| must outlive the iterator.
  RepresentationIterator(const Representations& pseudo_headers,
                         const Representations& regular_headers)
      : pseudo_begin_(pseudo_headers.begin()),
        pseudo_end_(pseudo_headers.end()),
        regular_begin_(regular_headers.begin()),
        regular_end_(regular_headers.end()) {}

  // |headers| must outlive the iterator.
  explicit RepresentationIterator(const Representations& headers)
      : pseudo_begin_(headers.begin()),
        pseudo_end_(headers.end()),
        regular_begin_(headers.end()),
        regular_end_(headers.end()) {}

  bool HasNext() {
    return pseudo_begin_ != pseudo_end_ || regular_begin_ != regular_end_;
  }

  const Representation Next() {
    if (pseudo_begin_ != pseudo_end_) {
      return *pseudo_begin_++;
    } else {
      return *regular_begin_++;
    }
  }

 private:
  Representations::const_iterator pseudo_begin_;
  Representations::const_iterator pseudo_end_;
  Representations::const_iterator regular_begin_;
  Representations::const_iterator regular_end_;
};

namespace {

// The default header listener.
void NoOpListener(quiche::QuicheStringPiece /*name*/,
                  quiche::QuicheStringPiece /*value*/) {}

// The default HPACK indexing policy.
bool DefaultPolicy(quiche::QuicheStringPiece name,
                   quiche::QuicheStringPiece /* value */) {
  if (name.empty()) {
    return false;
  }
  // :authority is always present and rarely changes, and has moderate
  // length, therefore it makes a lot of sense to index (insert in the
  // dynamic table).
  if (name[0] == kPseudoHeaderPrefix) {
    return name == ":authority";
  }
  return true;
}

}  // namespace

HpackEncoder::HpackEncoder(const HpackHuffmanTable& table)
    : output_stream_(),
      huffman_table_(table),
      min_table_size_setting_received_(std::numeric_limits<size_t>::max()),
      listener_(NoOpListener),
      should_index_(DefaultPolicy),
      enable_compression_(true),
      should_emit_table_size_(false) {}

HpackEncoder::~HpackEncoder() = default;

bool HpackEncoder::EncodeHeaderSet(const SpdyHeaderBlock& header_set,
                                   std::string* output) {
  // Separate header set into pseudo-headers and regular headers.
  Representations pseudo_headers;
  Representations regular_headers;
  bool found_cookie = false;
  for (const auto& header : header_set) {
    if (!found_cookie && header.first == "cookie") {
      // Note that there can only be one "cookie" header, because header_set is
      // a map.
      found_cookie = true;
      CookieToCrumbs(header, &regular_headers);
    } else if (!header.first.empty() &&
               header.first[0] == kPseudoHeaderPrefix) {
      DecomposeRepresentation(header, &pseudo_headers);
    } else {
      DecomposeRepresentation(header, &regular_headers);
    }
  }

  {
    RepresentationIterator iter(pseudo_headers, regular_headers);
    EncodeRepresentations(&iter, output);
  }
  return true;
}

void HpackEncoder::ApplyHeaderTableSizeSetting(size_t size_setting) {
  if (size_setting == header_table_.settings_size_bound()) {
    return;
  }
  if (size_setting < header_table_.settings_size_bound()) {
    min_table_size_setting_received_ =
        std::min(size_setting, min_table_size_setting_received_);
  }
  header_table_.SetSettingsHeaderTableSize(size_setting);
  should_emit_table_size_ = true;
}

size_t HpackEncoder::EstimateMemoryUsage() const {
  // |huffman_table_| is a singleton. It's accounted for in spdy_session_pool.cc
  return SpdyEstimateMemoryUsage(header_table_) +
         SpdyEstimateMemoryUsage(output_stream_);
}

void HpackEncoder::EncodeRepresentations(RepresentationIterator* iter,
                                         std::string* output) {
  MaybeEmitTableSize();
  while (iter->HasNext()) {
    const auto header = iter->Next();
    listener_(header.first, header.second);
    if (enable_compression_) {
      const HpackEntry* entry =
          header_table_.GetByNameAndValue(header.first, header.second);
      if (entry != nullptr) {
        EmitIndex(entry);
      } else if (should_index_(header.first, header.second)) {
        EmitIndexedLiteral(header);
      } else {
        EmitNonIndexedLiteral(header);
      }
    } else {
      EmitNonIndexedLiteral(header);
    }
  }

  output_stream_.TakeString(output);
}

void HpackEncoder::EmitIndex(const HpackEntry* entry) {
  SPDY_DVLOG(2) << "Emitting index " << header_table_.IndexOf(entry);
  output_stream_.AppendPrefix(kIndexedOpcode);
  output_stream_.AppendUint32(header_table_.IndexOf(entry));
}

void HpackEncoder::EmitIndexedLiteral(const Representation& representation) {
  SPDY_DVLOG(2) << "Emitting indexed literal: (" << representation.first << ", "
                << representation.second << ")";
  output_stream_.AppendPrefix(kLiteralIncrementalIndexOpcode);
  EmitLiteral(representation);
  header_table_.TryAddEntry(representation.first, representation.second);
}

void HpackEncoder::EmitNonIndexedLiteral(const Representation& representation) {
  SPDY_DVLOG(2) << "Emitting nonindexed literal: (" << representation.first
                << ", " << representation.second << ")";
  output_stream_.AppendPrefix(kLiteralNoIndexOpcode);
  output_stream_.AppendUint32(0);
  EmitString(representation.first);
  EmitString(representation.second);
}

void HpackEncoder::EmitLiteral(const Representation& representation) {
  const HpackEntry* name_entry = header_table_.GetByName(representation.first);
  if (name_entry != nullptr) {
    output_stream_.AppendUint32(header_table_.IndexOf(name_entry));
  } else {
    output_stream_.AppendUint32(0);
    EmitString(representation.first);
  }
  EmitString(representation.second);
}

void HpackEncoder::EmitString(quiche::QuicheStringPiece str) {
  size_t encoded_size =
      enable_compression_ ? huffman_table_.EncodedSize(str) : str.size();
  if (encoded_size < str.size()) {
    SPDY_DVLOG(2) << "Emitted Huffman-encoded string of length "
                  << encoded_size;
    output_stream_.AppendPrefix(kStringLiteralHuffmanEncoded);
    output_stream_.AppendUint32(encoded_size);
    huffman_table_.EncodeString(str, &output_stream_);
  } else {
    SPDY_DVLOG(2) << "Emitted literal string of length " << str.size();
    output_stream_.AppendPrefix(kStringLiteralIdentityEncoded);
    output_stream_.AppendUint32(str.size());
    output_stream_.AppendBytes(str);
  }
}

void HpackEncoder::MaybeEmitTableSize() {
  if (!should_emit_table_size_) {
    return;
  }
  const size_t current_size = CurrentHeaderTableSizeSetting();
  SPDY_DVLOG(1) << "MaybeEmitTableSize current_size=" << current_size;
  SPDY_DVLOG(1) << "MaybeEmitTableSize min_table_size_setting_received_="
                << min_table_size_setting_received_;
  if (min_table_size_setting_received_ < current_size) {
    output_stream_.AppendPrefix(kHeaderTableSizeUpdateOpcode);
    output_stream_.AppendUint32(min_table_size_setting_received_);
  }
  output_stream_.AppendPrefix(kHeaderTableSizeUpdateOpcode);
  output_stream_.AppendUint32(current_size);
  min_table_size_setting_received_ = std::numeric_limits<size_t>::max();
  should_emit_table_size_ = false;
}

// static
void HpackEncoder::CookieToCrumbs(const Representation& cookie,
                                  Representations* out) {
  // See Section 8.1.2.5. "Compressing the Cookie Header Field" in the HTTP/2
  // specification at https://tools.ietf.org/html/draft-ietf-httpbis-http2-14.
  // Cookie values are split into individually-encoded HPACK representations.
  quiche::QuicheStringPiece cookie_value = cookie.second;
  // Consume leading and trailing whitespace if present.
  quiche::QuicheStringPiece::size_type first =
      cookie_value.find_first_not_of(" \t");
  quiche::QuicheStringPiece::size_type last =
      cookie_value.find_last_not_of(" \t");
  if (first == quiche::QuicheStringPiece::npos) {
    cookie_value = quiche::QuicheStringPiece();
  } else {
    cookie_value = cookie_value.substr(first, (last - first) + 1);
  }
  for (size_t pos = 0;;) {
    size_t end = cookie_value.find(";", pos);

    if (end == quiche::QuicheStringPiece::npos) {
      out->push_back(std::make_pair(cookie.first, cookie_value.substr(pos)));
      break;
    }
    out->push_back(
        std::make_pair(cookie.first, cookie_value.substr(pos, end - pos)));

    // Consume next space if present.
    pos = end + 1;
    if (pos != cookie_value.size() && cookie_value[pos] == ' ') {
      pos++;
    }
  }
}

// static
void HpackEncoder::DecomposeRepresentation(const Representation& header_field,
                                           Representations* out) {
  size_t pos = 0;
  size_t end = 0;
  while (end != quiche::QuicheStringPiece::npos) {
    end = header_field.second.find('\0', pos);
    out->push_back(std::make_pair(
        header_field.first,
        header_field.second.substr(
            pos, end == quiche::QuicheStringPiece::npos ? end : end - pos)));
    pos = end + 1;
  }
}

// Iteratively encodes a SpdyHeaderBlock.
class HpackEncoder::Encoderator : public ProgressiveEncoder {
 public:
  Encoderator(const SpdyHeaderBlock& header_set, HpackEncoder* encoder);
  Encoderator(const Representations& representations, HpackEncoder* encoder);

  // Encoderator is neither copyable nor movable.
  Encoderator(const Encoderator&) = delete;
  Encoderator& operator=(const Encoderator&) = delete;

  // Returns true iff more remains to encode.
  bool HasNext() const override { return has_next_; }

  // Encodes up to max_encoded_bytes of the current header block into the
  // given output string.
  void Next(size_t max_encoded_bytes, std::string* output) override;

 private:
  HpackEncoder* encoder_;
  std::unique_ptr<RepresentationIterator> header_it_;
  Representations pseudo_headers_;
  Representations regular_headers_;
  bool has_next_;
};

HpackEncoder::Encoderator::Encoderator(const SpdyHeaderBlock& header_set,
                                       HpackEncoder* encoder)
    : encoder_(encoder), has_next_(true) {
  // Separate header set into pseudo-headers and regular headers.
  bool found_cookie = false;
  for (const auto& header : header_set) {
    if (!found_cookie && header.first == "cookie") {
      // Note that there can only be one "cookie" header, because header_set
      // is a map.
      found_cookie = true;
      CookieToCrumbs(header, &regular_headers_);
    } else if (!header.first.empty() &&
               header.first[0] == kPseudoHeaderPrefix) {
      DecomposeRepresentation(header, &pseudo_headers_);
    } else {
      DecomposeRepresentation(header, &regular_headers_);
    }
  }
  header_it_ = std::make_unique<RepresentationIterator>(pseudo_headers_,
                                                        regular_headers_);

  encoder_->MaybeEmitTableSize();
}

HpackEncoder::Encoderator::Encoderator(const Representations& representations,
                                       HpackEncoder* encoder)
    : encoder_(encoder), has_next_(true) {
  for (const auto& header : representations) {
    if (header.first == "cookie") {
      CookieToCrumbs(header, &regular_headers_);
    } else if (!header.first.empty() &&
               header.first[0] == kPseudoHeaderPrefix) {
      pseudo_headers_.push_back(header);
    } else {
      regular_headers_.push_back(header);
    }
  }
  header_it_ = std::make_unique<RepresentationIterator>(pseudo_headers_,
                                                        regular_headers_);

  encoder_->MaybeEmitTableSize();
}

void HpackEncoder::Encoderator::Next(size_t max_encoded_bytes,
                                     std::string* output) {
  SPDY_BUG_IF(!has_next_)
      << "Encoderator::Next called with nothing left to encode.";
  const bool use_compression = encoder_->enable_compression_;

  // Encode up to max_encoded_bytes of headers.
  while (header_it_->HasNext() &&
         encoder_->output_stream_.size() <= max_encoded_bytes) {
    const Representation header = header_it_->Next();
    encoder_->listener_(header.first, header.second);
    if (use_compression) {
      const HpackEntry* entry = encoder_->header_table_.GetByNameAndValue(
          header.first, header.second);
      if (entry != nullptr) {
        encoder_->EmitIndex(entry);
      } else if (encoder_->should_index_(header.first, header.second)) {
        encoder_->EmitIndexedLiteral(header);
      } else {
        encoder_->EmitNonIndexedLiteral(header);
      }
    } else {
      encoder_->EmitNonIndexedLiteral(header);
    }
  }

  has_next_ = encoder_->output_stream_.size() > max_encoded_bytes;
  encoder_->output_stream_.BoundedTakeString(max_encoded_bytes, output);
}

std::unique_ptr<HpackEncoder::ProgressiveEncoder> HpackEncoder::EncodeHeaderSet(
    const SpdyHeaderBlock& header_set) {
  return std::make_unique<Encoderator>(header_set, this);
}

std::unique_ptr<HpackEncoder::ProgressiveEncoder>
HpackEncoder::EncodeRepresentations(const Representations& representations) {
  return std::make_unique<Encoderator>(representations, this);
}

}  // namespace spdy
