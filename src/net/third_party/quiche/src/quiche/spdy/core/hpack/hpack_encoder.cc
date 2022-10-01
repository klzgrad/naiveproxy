// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/spdy/core/hpack/hpack_encoder.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "quiche/http2/hpack/huffman/hpack_huffman_encoder.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/spdy/core/hpack/hpack_constants.h"
#include "quiche/spdy/core/hpack/hpack_header_table.h"
#include "quiche/spdy/core/hpack/hpack_output_stream.h"

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
void NoOpListener(absl::string_view /*name*/, absl::string_view /*value*/) {}

// The default HPACK indexing policy.
bool DefaultPolicy(absl::string_view name, absl::string_view /* value */) {
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

HpackEncoder::HpackEncoder()
    : output_stream_(),
      min_table_size_setting_received_(std::numeric_limits<size_t>::max()),
      listener_(NoOpListener),
      should_index_(DefaultPolicy),
      enable_compression_(true),
      should_emit_table_size_(false) {}

HpackEncoder::~HpackEncoder() = default;

std::string HpackEncoder::EncodeHeaderBlock(
    const Http2HeaderBlock& header_set) {
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

  RepresentationIterator iter(pseudo_headers, regular_headers);
  return EncodeRepresentations(&iter);
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

std::string HpackEncoder::EncodeRepresentations(RepresentationIterator* iter) {
  MaybeEmitTableSize();
  while (iter->HasNext()) {
    const auto header = iter->Next();
    listener_(header.first, header.second);
    if (enable_compression_) {
      size_t index =
          header_table_.GetByNameAndValue(header.first, header.second);
      if (index != kHpackEntryNotFound) {
        EmitIndex(index);
      } else if (should_index_(header.first, header.second)) {
        EmitIndexedLiteral(header);
      } else {
        EmitNonIndexedLiteral(header, enable_compression_);
      }
    } else {
      EmitNonIndexedLiteral(header, enable_compression_);
    }
  }

  return output_stream_.TakeString();
}

void HpackEncoder::EmitIndex(size_t index) {
  QUICHE_DVLOG(2) << "Emitting index " << index;
  output_stream_.AppendPrefix(kIndexedOpcode);
  output_stream_.AppendUint32(index);
}

void HpackEncoder::EmitIndexedLiteral(const Representation& representation) {
  QUICHE_DVLOG(2) << "Emitting indexed literal: (" << representation.first
                  << ", " << representation.second << ")";
  output_stream_.AppendPrefix(kLiteralIncrementalIndexOpcode);
  EmitLiteral(representation);
  header_table_.TryAddEntry(representation.first, representation.second);
}

void HpackEncoder::EmitNonIndexedLiteral(const Representation& representation,
                                         bool enable_compression) {
  QUICHE_DVLOG(2) << "Emitting nonindexed literal: (" << representation.first
                  << ", " << representation.second << ")";
  output_stream_.AppendPrefix(kLiteralNoIndexOpcode);
  size_t name_index = header_table_.GetByName(representation.first);
  if (enable_compression && name_index != kHpackEntryNotFound) {
    output_stream_.AppendUint32(name_index);
  } else {
    output_stream_.AppendUint32(0);
    EmitString(representation.first);
  }
  EmitString(representation.second);
}

void HpackEncoder::EmitLiteral(const Representation& representation) {
  size_t name_index = header_table_.GetByName(representation.first);
  if (name_index != kHpackEntryNotFound) {
    output_stream_.AppendUint32(name_index);
  } else {
    output_stream_.AppendUint32(0);
    EmitString(representation.first);
  }
  EmitString(representation.second);
}

void HpackEncoder::EmitString(absl::string_view str) {
  size_t encoded_size =
      enable_compression_ ? http2::HuffmanSize(str) : str.size();
  if (encoded_size < str.size()) {
    QUICHE_DVLOG(2) << "Emitted Huffman-encoded string of length "
                    << encoded_size;
    output_stream_.AppendPrefix(kStringLiteralHuffmanEncoded);
    output_stream_.AppendUint32(encoded_size);
    http2::HuffmanEncodeFast(str, encoded_size, output_stream_.MutableString());
  } else {
    QUICHE_DVLOG(2) << "Emitted literal string of length " << str.size();
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
  QUICHE_DVLOG(1) << "MaybeEmitTableSize current_size=" << current_size;
  QUICHE_DVLOG(1) << "MaybeEmitTableSize min_table_size_setting_received_="
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
  absl::string_view cookie_value = cookie.second;
  // Consume leading and trailing whitespace if present.
  absl::string_view::size_type first = cookie_value.find_first_not_of(" \t");
  absl::string_view::size_type last = cookie_value.find_last_not_of(" \t");
  if (first == absl::string_view::npos) {
    cookie_value = absl::string_view();
  } else {
    cookie_value = cookie_value.substr(first, (last - first) + 1);
  }
  for (size_t pos = 0;;) {
    size_t end = cookie_value.find(';', pos);

    if (end == absl::string_view::npos) {
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
  while (end != absl::string_view::npos) {
    end = header_field.second.find('\0', pos);
    out->push_back(std::make_pair(
        header_field.first,
        header_field.second.substr(
            pos, end == absl::string_view::npos ? end : end - pos)));
    pos = end + 1;
  }
}

// Iteratively encodes a Http2HeaderBlock.
class HpackEncoder::Encoderator : public ProgressiveEncoder {
 public:
  Encoderator(const Http2HeaderBlock& header_set, HpackEncoder* encoder);
  Encoderator(const Representations& representations, HpackEncoder* encoder);

  // Encoderator is neither copyable nor movable.
  Encoderator(const Encoderator&) = delete;
  Encoderator& operator=(const Encoderator&) = delete;

  // Returns true iff more remains to encode.
  bool HasNext() const override { return has_next_; }

  // Encodes and returns up to max_encoded_bytes of the current header block.
  std::string Next(size_t max_encoded_bytes) override;

 private:
  HpackEncoder* encoder_;
  std::unique_ptr<RepresentationIterator> header_it_;
  Representations pseudo_headers_;
  Representations regular_headers_;
  bool has_next_;
};

HpackEncoder::Encoderator::Encoderator(const Http2HeaderBlock& header_set,
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

std::string HpackEncoder::Encoderator::Next(size_t max_encoded_bytes) {
  QUICHE_BUG_IF(spdy_bug_61_1, !has_next_)
      << "Encoderator::Next called with nothing left to encode.";
  const bool enable_compression = encoder_->enable_compression_;

  // Encode up to max_encoded_bytes of headers.
  while (header_it_->HasNext() &&
         encoder_->output_stream_.size() <= max_encoded_bytes) {
    const Representation header = header_it_->Next();
    encoder_->listener_(header.first, header.second);
    if (enable_compression) {
      size_t index = encoder_->header_table_.GetByNameAndValue(header.first,
                                                               header.second);
      if (index != kHpackEntryNotFound) {
        encoder_->EmitIndex(index);
      } else if (encoder_->should_index_(header.first, header.second)) {
        encoder_->EmitIndexedLiteral(header);
      } else {
        encoder_->EmitNonIndexedLiteral(header, enable_compression);
      }
    } else {
      encoder_->EmitNonIndexedLiteral(header, enable_compression);
    }
  }

  has_next_ = encoder_->output_stream_.size() > max_encoded_bytes;
  return encoder_->output_stream_.BoundedTakeString(max_encoded_bytes);
}

std::unique_ptr<HpackEncoder::ProgressiveEncoder> HpackEncoder::EncodeHeaderSet(
    const Http2HeaderBlock& header_set) {
  return std::make_unique<Encoderator>(header_set, this);
}

std::unique_ptr<HpackEncoder::ProgressiveEncoder>
HpackEncoder::EncodeRepresentations(const Representations& representations) {
  return std::make_unique<Encoderator>(representations, this);
}

}  // namespace spdy
