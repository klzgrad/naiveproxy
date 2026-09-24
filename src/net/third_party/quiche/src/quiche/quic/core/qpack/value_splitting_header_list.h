// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_VALUE_SPLITTING_HEADER_LIST_H_
#define QUICHE_QUIC_CORE_QPACK_VALUE_SPLITTING_HEADER_LIST_H_

#include "absl/strings/string_view.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/common/http/http_header_block.h"

namespace quic {

// Enumeration that specifies whether cookie crumbling should be used when
// sending QPACK headers.
enum class CookieCrumbling { kEnabled, kDisabled };

// A wrapper class around Http2HeaderBlock that splits header values along ';'
// separators (while also removing optional space following separator) for
// cookies and along '\0' separators for other header fields.
class QUICHE_EXPORT ValueSplittingHeaderList {
 public:
  using value_type = quiche::HttpHeaderBlock::value_type;

  class QUICHE_EXPORT const_iterator {
   public:
    // |header_list| must outlive this object.
    const_iterator(const quiche::HttpHeaderBlock* header_list,
                   quiche::HttpHeaderBlock::const_iterator header_list_iterator,
                   CookieCrumbling cookie_crumbling);
    const_iterator(const const_iterator&) = default;
    const_iterator& operator=(const const_iterator&) = delete;

    bool operator==(const const_iterator& other) const;
    bool operator!=(const const_iterator& other) const;

    const const_iterator& operator++();

    const value_type& operator*() const;
    const value_type* operator->() const;

   private:
    // Find next separator; update |value_end_| and |header_field_|.
    void UpdateHeaderField();

    const quiche::HttpHeaderBlock* const header_list_;
    quiche::HttpHeaderBlock::const_iterator header_list_iterator_;
    const CookieCrumbling cookie_crumbling_;
    absl::string_view::size_type value_start_;
    absl::string_view::size_type value_end_;
    value_type header_field_;
  };

  // |header_list| must outlive this object.
  explicit ValueSplittingHeaderList(const quiche::HttpHeaderBlock* header_list,
                                    CookieCrumbling cookie_crumbling);
  ValueSplittingHeaderList(const ValueSplittingHeaderList&) = delete;
  ValueSplittingHeaderList& operator=(const ValueSplittingHeaderList&) = delete;

  const_iterator begin() const;
  const_iterator end() const;

 private:
  const quiche::HttpHeaderBlock* const header_list_;
  const CookieCrumbling cookie_crumbling_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_VALUE_SPLITTING_HEADER_LIST_H_
