// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/qpack/value_splitting_header_list.h"

#include "absl/strings/string_view.h"
#include "quiche/quic/platform/api/quic_logging.h"

namespace quic {
namespace {

const char kCookieKey[] = "cookie";
const char kCookieSeparator = ';';
const char kOptionalSpaceAfterCookieSeparator = ' ';
const char kNonCookieSeparator = '\0';

}  // namespace

ValueSplittingHeaderList::const_iterator::const_iterator(
    const spdy::Http2HeaderBlock* header_list,
    spdy::Http2HeaderBlock::const_iterator header_list_iterator)
    : header_list_(header_list),
      header_list_iterator_(header_list_iterator),
      value_start_(0) {
  UpdateHeaderField();
}

bool ValueSplittingHeaderList::const_iterator::operator==(
    const const_iterator& other) const {
  return header_list_iterator_ == other.header_list_iterator_ &&
         value_start_ == other.value_start_;
}

bool ValueSplittingHeaderList::const_iterator::operator!=(
    const const_iterator& other) const {
  return !(*this == other);
}

const ValueSplittingHeaderList::const_iterator&
ValueSplittingHeaderList::const_iterator::operator++() {
  if (value_end_ == absl::string_view::npos) {
    // This was the last frament within |*header_list_iterator_|,
    // move on to the next header element of |header_list_|.
    ++header_list_iterator_;
    value_start_ = 0;
  } else {
    // Find the next fragment within |*header_list_iterator_|.
    value_start_ = value_end_ + 1;
  }
  UpdateHeaderField();

  return *this;
}

const ValueSplittingHeaderList::value_type&
ValueSplittingHeaderList::const_iterator::operator*() const {
  return header_field_;
}
const ValueSplittingHeaderList::value_type*
ValueSplittingHeaderList::const_iterator::operator->() const {
  return &header_field_;
}

void ValueSplittingHeaderList::const_iterator::UpdateHeaderField() {
  QUICHE_DCHECK(value_start_ != absl::string_view::npos);

  if (header_list_iterator_ == header_list_->end()) {
    return;
  }

  const absl::string_view name = header_list_iterator_->first;
  const absl::string_view original_value = header_list_iterator_->second;

  if (name == kCookieKey) {
    value_end_ = original_value.find(kCookieSeparator, value_start_);
  } else {
    value_end_ = original_value.find(kNonCookieSeparator, value_start_);
  }

  const absl::string_view value =
      original_value.substr(value_start_, value_end_ - value_start_);
  header_field_ = std::make_pair(name, value);

  // Skip character after ';' separator if it is a space.
  if (name == kCookieKey && value_end_ != absl::string_view::npos &&
      value_end_ + 1 < original_value.size() &&
      original_value[value_end_ + 1] == kOptionalSpaceAfterCookieSeparator) {
    ++value_end_;
  }
}

ValueSplittingHeaderList::ValueSplittingHeaderList(
    const spdy::Http2HeaderBlock* header_list)
    : header_list_(header_list) {
  QUICHE_DCHECK(header_list_);
}

ValueSplittingHeaderList::const_iterator ValueSplittingHeaderList::begin()
    const {
  return const_iterator(header_list_, header_list_->begin());
}

ValueSplittingHeaderList::const_iterator ValueSplittingHeaderList::end() const {
  return const_iterator(header_list_, header_list_->end());
}

}  // namespace quic
