// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_QUIC_HEADER_LIST_H_
#define NET_QUIC_CORE_QUIC_HEADER_LIST_H_

#include <algorithm>
#include <functional>
#include <string>
#include <utility>

#include "net/quic/platform/api/quic_bug_tracker.h"
#include "net/quic/platform/api/quic_containers.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/platform/api/quic_string_piece.h"
#include "net/spdy/core/spdy_header_block.h"
#include "net/spdy/core/spdy_headers_handler_interface.h"

namespace net {

// A simple class that accumulates header pairs
class QUIC_EXPORT_PRIVATE QuicHeaderList : public SpdyHeadersHandlerInterface {
 public:
  typedef QuicDeque<std::pair<std::string, std::string>> ListType;
  typedef ListType::const_iterator const_iterator;

  QuicHeaderList();
  QuicHeaderList(QuicHeaderList&& other);
  QuicHeaderList(const QuicHeaderList& other);
  QuicHeaderList& operator=(QuicHeaderList&& other);
  QuicHeaderList& operator=(const QuicHeaderList& other);
  ~QuicHeaderList() override;

  // From SpdyHeadersHandlerInteface.
  void OnHeaderBlockStart() override;
  void OnHeader(QuicStringPiece name, QuicStringPiece value) override;
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

  void set_max_header_list_size(size_t max_header_list_size) {
    max_header_list_size_ = max_header_list_size;
  }

  size_t max_header_list_size() const { return max_header_list_size_; }

  std::string DebugString() const;

 private:
  QuicDeque<std::pair<std::string, std::string>> header_list_;

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

}  // namespace net

#endif  // NET_QUIC_CORE_QUIC_HEADER_LIST_H_
