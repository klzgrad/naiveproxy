// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_QUIC_HEADER_LIST_H_
#define QUICHE_QUIC_CORE_HTTP_QUIC_HEADER_LIST_H_

#include <algorithm>
#include <functional>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "quiche/http2/core/spdy_headers_handler_interface.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/common/quiche_circular_deque.h"

namespace quic {

// A simple class that accumulates header pairs
class QUICHE_EXPORT QuicHeaderList {
 public:
  using ListType =
      quiche::QuicheCircularDeque<std::pair<std::string, std::string>>;
  using value_type = ListType::value_type;
  using const_iterator = ListType::const_iterator;

  QuicHeaderList() = default;
  QuicHeaderList(QuicHeaderList&& other) = default;
  QuicHeaderList(const QuicHeaderList& other) = default;
  QuicHeaderList& operator=(QuicHeaderList&& other) = default;
  QuicHeaderList& operator=(const QuicHeaderList& other) = default;

  void OnHeader(absl::string_view name, absl::string_view value);
  void OnHeaderBlockEnd(size_t uncompressed_header_bytes,
                        size_t compressed_header_bytes);

  void Clear();

  const_iterator begin() const { return header_list_.begin(); }
  const_iterator end() const { return header_list_.end(); }

  bool empty() const { return header_list_.empty(); }
  size_t uncompressed_header_bytes() const {
    return uncompressed_header_bytes_;
  }
  size_t compressed_header_bytes() const { return compressed_header_bytes_; }

  std::string DebugString() const;

 private:
  quiche::QuicheCircularDeque<std::pair<std::string, std::string>> header_list_;

  size_t uncompressed_header_bytes_ = 0;
  size_t compressed_header_bytes_ = 0;
};

inline bool operator==(const QuicHeaderList& l1, const QuicHeaderList& l2) {
  auto pred = [](const std::pair<std::string, std::string>& p1,
                 const std::pair<std::string, std::string>& p2) {
    return p1.first == p2.first && p1.second == p2.second;
  };
  return std::equal(l1.begin(), l1.end(), l2.begin(), pred);
}

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_QUIC_HEADER_LIST_H_
