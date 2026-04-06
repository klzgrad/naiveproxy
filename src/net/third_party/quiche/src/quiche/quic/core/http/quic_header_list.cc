// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/http/quic_header_list.h"

#include <limits>
#include <string>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/qpack/qpack_header_table.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/platform/api/quic_flags.h"

namespace quic {

void QuicHeaderList::OnHeader(absl::string_view name, absl::string_view value) {
  header_list_.emplace_back(std::string(name), std::string(value));
}

void QuicHeaderList::OnHeaderBlockEnd(size_t uncompressed_header_bytes,
                                      size_t compressed_header_bytes) {
  uncompressed_header_bytes_ = uncompressed_header_bytes;
  compressed_header_bytes_ = compressed_header_bytes;
}

void QuicHeaderList::Clear() {
  header_list_.clear();
  uncompressed_header_bytes_ = 0;
  compressed_header_bytes_ = 0;
}

std::string QuicHeaderList::DebugString() const {
  std::string s = "{ ";
  for (const auto& p : *this) {
    s.append(p.first + "=" + p.second + ", ");
  }
  s.append("}");
  return s;
}

}  // namespace quic
