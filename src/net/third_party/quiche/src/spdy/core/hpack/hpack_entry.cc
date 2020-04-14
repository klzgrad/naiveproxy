// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/spdy/core/hpack/hpack_entry.h"

#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_estimate_memory_usage.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_logging.h"

namespace spdy {

const size_t HpackEntry::kSizeOverhead = 32;

HpackEntry::HpackEntry(quiche::QuicheStringPiece name,
                       quiche::QuicheStringPiece value,
                       bool is_static,
                       size_t insertion_index)
    : name_(name.data(), name.size()),
      value_(value.data(), value.size()),
      name_ref_(name_),
      value_ref_(value_),
      insertion_index_(insertion_index),
      type_(is_static ? STATIC : DYNAMIC),
      time_added_(0) {}

HpackEntry::HpackEntry(quiche::QuicheStringPiece name,
                       quiche::QuicheStringPiece value)
    : name_ref_(name),
      value_ref_(value),
      insertion_index_(0),
      type_(LOOKUP),
      time_added_(0) {}

HpackEntry::HpackEntry() : insertion_index_(0), type_(LOOKUP), time_added_(0) {}

HpackEntry::HpackEntry(const HpackEntry& other)
    : insertion_index_(other.insertion_index_),
      type_(other.type_),
      time_added_(0) {
  if (type_ == LOOKUP) {
    name_ref_ = other.name_ref_;
    value_ref_ = other.value_ref_;
  } else {
    name_ = other.name_;
    value_ = other.value_;
    name_ref_ = quiche::QuicheStringPiece(name_.data(), name_.size());
    value_ref_ = quiche::QuicheStringPiece(value_.data(), value_.size());
  }
}

HpackEntry& HpackEntry::operator=(const HpackEntry& other) {
  insertion_index_ = other.insertion_index_;
  type_ = other.type_;
  if (type_ == LOOKUP) {
    name_.clear();
    value_.clear();
    name_ref_ = other.name_ref_;
    value_ref_ = other.value_ref_;
    return *this;
  }
  name_ = other.name_;
  value_ = other.value_;
  name_ref_ = quiche::QuicheStringPiece(name_.data(), name_.size());
  value_ref_ = quiche::QuicheStringPiece(value_.data(), value_.size());
  return *this;
}

HpackEntry::~HpackEntry() = default;

// static
size_t HpackEntry::Size(quiche::QuicheStringPiece name,
                        quiche::QuicheStringPiece value) {
  return name.size() + value.size() + kSizeOverhead;
}
size_t HpackEntry::Size() const {
  return Size(name(), value());
}

std::string HpackEntry::GetDebugString() const {
  return quiche::QuicheStrCat(
      "{ name: \"", name_ref_, "\", value: \"", value_ref_,
      "\", index: ", insertion_index_, " ",
      (IsStatic() ? " static" : (IsLookup() ? " lookup" : " dynamic")), " }");
}

size_t HpackEntry::EstimateMemoryUsage() const {
  return SpdyEstimateMemoryUsage(name_) + SpdyEstimateMemoryUsage(value_);
}

}  // namespace spdy
