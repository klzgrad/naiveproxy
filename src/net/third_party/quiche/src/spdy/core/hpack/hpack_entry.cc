// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spdy/core/hpack/hpack_entry.h"

#include "absl/strings/str_cat.h"
#include "spdy/platform/api/spdy_estimate_memory_usage.h"
#include "spdy/platform/api/spdy_logging.h"

namespace spdy {

const size_t HpackEntry::kSizeOverhead = 32;

HpackEntry::HpackEntry(absl::string_view name,
                       absl::string_view value,
                       bool is_static,
                       size_t insertion_index)
    : name_(name.data(), name.size()),
      value_(value.data(), value.size()),
      name_ref_(name_),
      value_ref_(value_),
      insertion_index_(insertion_index),
      type_(is_static ? STATIC : DYNAMIC),
      time_added_(0) {}

HpackEntry::HpackEntry(absl::string_view name, absl::string_view value)
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
    name_ref_ = absl::string_view(name_.data(), name_.size());
    value_ref_ = absl::string_view(value_.data(), value_.size());
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
  name_ref_ = absl::string_view(name_.data(), name_.size());
  value_ref_ = absl::string_view(value_.data(), value_.size());
  return *this;
}

HpackEntry::~HpackEntry() = default;

// static
size_t HpackEntry::Size(absl::string_view name, absl::string_view value) {
  return name.size() + value.size() + kSizeOverhead;
}
size_t HpackEntry::Size() const {
  return Size(name(), value());
}

std::string HpackEntry::GetDebugString() const {
  return absl::StrCat(
      "{ name: \"", name_ref_, "\", value: \"", value_ref_,
      "\", index: ", insertion_index_, " ",
      (IsStatic() ? " static" : (IsLookup() ? " lookup" : " dynamic")), " }");
}

size_t HpackEntry::EstimateMemoryUsage() const {
  return SpdyEstimateMemoryUsage(name_) + SpdyEstimateMemoryUsage(value_);
}

}  // namespace spdy
