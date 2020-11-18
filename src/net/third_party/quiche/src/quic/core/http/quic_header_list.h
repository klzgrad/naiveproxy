// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_QUIC_HEADER_LIST_H_
#define QUICHE_QUIC_CORE_HTTP_QUIC_HEADER_LIST_H_

#include <algorithm>
#include <functional>
#include <string>
#include <utility>

#include "net/third_party/quiche/src/quic/core/quic_circular_deque.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/spdy/core/spdy_header_block.h"
#include "net/third_party/quiche/src/spdy/core/spdy_headers_handler_interface.h"

namespace quic {

// A simple class that accumulates header pairs
class QUIC_EXPORT_PRIVATE QuicHeaderList
    : public spdy::SpdyHeadersHandlerInterface {
 public:
  using ListType = QuicCircularDeque<std::pair<std::string, std::string>>;
  using value_type = ListType::value_type;
  using const_iterator = ListType::const_iterator;

  QuicHeaderList();
  QuicHeaderList(QuicHeaderList&& other);
  QuicHeaderList(const QuicHeaderList& other);
  QuicHeaderList& operator=(QuicHeaderList&& other);
  QuicHeaderList& operator=(const QuicHeaderList& other);
  ~QuicHeaderList() override;

  // From SpdyHeadersHandlerInteface.
  void OnHeaderBlockStart() override;
  void OnHeader(quiche::QuicheStringPiece name,
                quiche::QuicheStringPiece value) override;
  void OnHeaderBlockEnd(size_t uncompressed_header_bytes,
                        size_t compressed_header_bytes) override;

  void Clear();

  const_iterator begin() const { return header_list_.begin(); }
  const_iterator end() const { return header_list_.end(); }

  bool empty() const { return header_list_.empty(); }
  size_t uncompressed_header_bytes() const {
    return uncompressed_header_bytes_;
  }
  size_t compressed_header_bytes() const { return compressed_header_bytes_; }

  // Deprecated.  TODO(b/145909215): remove.
  void set_max_header_list_size(size_t max_header_list_size) {
    max_header_list_size_ = max_header_list_size;
  }

  std::string DebugString() const;

 private:
  QuicCircularDeque<std::pair<std::string, std::string>> header_list_;

  // The limit on the size of the header list (defined by spec as name + value +
  // overhead for each header field). Headers over this limit will not be
  // buffered, and the list will be cleared upon OnHeaderBlockEnd.
  size_t max_header_list_size_;

  // Defined per the spec as the size of all header fields with an additional
  // overhead for each field.
  size_t current_header_list_size_;

  // TODO(dahollings) Are these fields necessary?
  size_t uncompressed_header_bytes_;
  size_t compressed_header_bytes_;
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
