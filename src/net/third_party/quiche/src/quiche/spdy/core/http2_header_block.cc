// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/spdy/core/http2_header_block.h"

#include <string.h>

#include <algorithm>
#include <utility>

#include "absl/strings/str_cat.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace spdy {
namespace {

// By default, linked_hash_map's internal map allocates space for 100 map
// buckets on construction, which is larger than necessary.  Standard library
// unordered map implementations use a list of prime numbers to set the bucket
// count for a particular capacity.  |kInitialMapBuckets| is chosen to reduce
// memory usage for small header blocks, at the cost of having to rehash for
// large header blocks.
const size_t kInitialMapBuckets = 11;

const char kCookieKey[] = "cookie";
const char kNullSeparator = 0;

absl::string_view SeparatorForKey(absl::string_view key) {
  if (key == kCookieKey) {
    static absl::string_view cookie_separator = "; ";
    return cookie_separator;
  } else {
    return absl::string_view(&kNullSeparator, 1);
  }
}

}  // namespace

Http2HeaderBlock::HeaderValue::HeaderValue(Http2HeaderStorage* storage,
                                           absl::string_view key,
                                           absl::string_view initial_value)
    : storage_(storage),
      fragments_({initial_value}),
      pair_({key, {}}),
      size_(initial_value.size()),
      separator_size_(SeparatorForKey(key).size()) {}

Http2HeaderBlock::HeaderValue::HeaderValue(HeaderValue&& other)
    : storage_(other.storage_),
      fragments_(std::move(other.fragments_)),
      pair_(std::move(other.pair_)),
      size_(other.size_),
      separator_size_(other.separator_size_) {}

Http2HeaderBlock::HeaderValue& Http2HeaderBlock::HeaderValue::operator=(
    HeaderValue&& other) {
  storage_ = other.storage_;
  fragments_ = std::move(other.fragments_);
  pair_ = std::move(other.pair_);
  size_ = other.size_;
  separator_size_ = other.separator_size_;
  return *this;
}

void Http2HeaderBlock::HeaderValue::set_storage(Http2HeaderStorage* storage) {
  storage_ = storage;
}

Http2HeaderBlock::HeaderValue::~HeaderValue() = default;

absl::string_view Http2HeaderBlock::HeaderValue::ConsolidatedValue() const {
  if (fragments_.empty()) {
    return absl::string_view();
  }
  if (fragments_.size() > 1) {
    fragments_ = {
        storage_->WriteFragments(fragments_, SeparatorForKey(pair_.first))};
  }
  return fragments_[0];
}

void Http2HeaderBlock::HeaderValue::Append(absl::string_view fragment) {
  size_ += (fragment.size() + separator_size_);
  fragments_.push_back(fragment);
}

const std::pair<absl::string_view, absl::string_view>&
Http2HeaderBlock::HeaderValue::as_pair() const {
  pair_.second = ConsolidatedValue();
  return pair_;
}

Http2HeaderBlock::iterator::iterator(MapType::const_iterator it) : it_(it) {}

Http2HeaderBlock::iterator::iterator(const iterator& other) = default;

Http2HeaderBlock::iterator::~iterator() = default;

Http2HeaderBlock::ValueProxy::ValueProxy(
    Http2HeaderBlock* block, Http2HeaderBlock::MapType::iterator lookup_result,
    const absl::string_view key, size_t* spdy_header_block_value_size)
    : block_(block),
      lookup_result_(lookup_result),
      key_(key),
      spdy_header_block_value_size_(spdy_header_block_value_size),
      valid_(true) {}

Http2HeaderBlock::ValueProxy::ValueProxy(ValueProxy&& other)
    : block_(other.block_),
      lookup_result_(other.lookup_result_),
      key_(other.key_),
      spdy_header_block_value_size_(other.spdy_header_block_value_size_),
      valid_(true) {
  other.valid_ = false;
}

Http2HeaderBlock::ValueProxy& Http2HeaderBlock::ValueProxy::operator=(
    Http2HeaderBlock::ValueProxy&& other) {
  block_ = other.block_;
  lookup_result_ = other.lookup_result_;
  key_ = other.key_;
  valid_ = true;
  other.valid_ = false;
  spdy_header_block_value_size_ = other.spdy_header_block_value_size_;
  return *this;
}

Http2HeaderBlock::ValueProxy::~ValueProxy() {
  // If the ValueProxy is destroyed while lookup_result_ == block_->end(),
  // the assignment operator was never used, and the block's Http2HeaderStorage
  // can reclaim the memory used by the key. This makes lookup-only access to
  // Http2HeaderBlock through operator[] memory-neutral.
  if (valid_ && lookup_result_ == block_->map_.end()) {
    block_->storage_.Rewind(key_);
  }
}

Http2HeaderBlock::ValueProxy& Http2HeaderBlock::ValueProxy::operator=(
    absl::string_view value) {
  *spdy_header_block_value_size_ += value.size();
  Http2HeaderStorage* storage = &block_->storage_;
  if (lookup_result_ == block_->map_.end()) {
    QUICHE_DVLOG(1) << "Inserting: (" << key_ << ", " << value << ")";
    lookup_result_ =
        block_->map_
            .emplace(std::make_pair(
                key_, HeaderValue(storage, key_, storage->Write(value))))
            .first;
  } else {
    QUICHE_DVLOG(1) << "Updating key: " << key_ << " with value: " << value;
    *spdy_header_block_value_size_ -= lookup_result_->second.SizeEstimate();
    lookup_result_->second = HeaderValue(storage, key_, storage->Write(value));
  }
  return *this;
}

bool Http2HeaderBlock::ValueProxy::operator==(absl::string_view value) const {
  if (lookup_result_ == block_->map_.end()) {
    return false;
  } else {
    return value == lookup_result_->second.value();
  }
}

std::string Http2HeaderBlock::ValueProxy::as_string() const {
  if (lookup_result_ == block_->map_.end()) {
    return "";
  } else {
    return std::string(lookup_result_->second.value());
  }
}

Http2HeaderBlock::Http2HeaderBlock() : map_(kInitialMapBuckets) {}

Http2HeaderBlock::Http2HeaderBlock(Http2HeaderBlock&& other)
    : map_(kInitialMapBuckets) {
  map_.swap(other.map_);
  storage_ = std::move(other.storage_);
  for (auto& p : map_) {
    p.second.set_storage(&storage_);
  }
  key_size_ = other.key_size_;
  value_size_ = other.value_size_;
}

Http2HeaderBlock::~Http2HeaderBlock() = default;

Http2HeaderBlock& Http2HeaderBlock::operator=(Http2HeaderBlock&& other) {
  map_.swap(other.map_);
  storage_ = std::move(other.storage_);
  for (auto& p : map_) {
    p.second.set_storage(&storage_);
  }
  key_size_ = other.key_size_;
  value_size_ = other.value_size_;
  return *this;
}

Http2HeaderBlock Http2HeaderBlock::Clone() const {
  Http2HeaderBlock copy;
  for (const auto& p : *this) {
    copy.AppendHeader(p.first, p.second);
  }
  return copy;
}

bool Http2HeaderBlock::operator==(const Http2HeaderBlock& other) const {
  return size() == other.size() && std::equal(begin(), end(), other.begin());
}

bool Http2HeaderBlock::operator!=(const Http2HeaderBlock& other) const {
  return !(operator==(other));
}

std::string Http2HeaderBlock::DebugString() const {
  if (empty()) {
    return "{}";
  }

  std::string output = "\n{\n";
  for (auto it = begin(); it != end(); ++it) {
    absl::StrAppend(&output, "  ", it->first, " ", it->second, "\n");
  }
  absl::StrAppend(&output, "}\n");
  return output;
}

void Http2HeaderBlock::erase(absl::string_view key) {
  auto iter = map_.find(key);
  if (iter != map_.end()) {
    QUICHE_DVLOG(1) << "Erasing header with name: " << key;
    key_size_ -= key.size();
    value_size_ -= iter->second.SizeEstimate();
    map_.erase(iter);
  }
}

void Http2HeaderBlock::clear() {
  key_size_ = 0;
  value_size_ = 0;
  map_.clear();
  storage_.Clear();
}

void Http2HeaderBlock::insert(const Http2HeaderBlock::value_type& value) {
  // TODO(birenroy): Write new value in place of old value, if it fits.
  value_size_ += value.second.size();

  auto iter = map_.find(value.first);
  if (iter == map_.end()) {
    QUICHE_DVLOG(1) << "Inserting: (" << value.first << ", " << value.second
                    << ")";
    AppendHeader(value.first, value.second);
  } else {
    QUICHE_DVLOG(1) << "Updating key: " << iter->first
                    << " with value: " << value.second;
    value_size_ -= iter->second.SizeEstimate();
    iter->second =
        HeaderValue(&storage_, iter->first, storage_.Write(value.second));
  }
}

Http2HeaderBlock::ValueProxy Http2HeaderBlock::operator[](
    const absl::string_view key) {
  QUICHE_DVLOG(2) << "Operator[] saw key: " << key;
  absl::string_view out_key;
  auto iter = map_.find(key);
  if (iter == map_.end()) {
    // We write the key first, to assure that the ValueProxy has a
    // reference to a valid absl::string_view in its operator=.
    out_key = WriteKey(key);
    QUICHE_DVLOG(2) << "Key written as: " << std::hex
                    << static_cast<const void*>(key.data()) << ", " << std::dec
                    << key.size();
  } else {
    out_key = iter->first;
  }
  return ValueProxy(this, iter, out_key, &value_size_);
}

void Http2HeaderBlock::AppendValueOrAddHeader(const absl::string_view key,
                                              const absl::string_view value) {
  value_size_ += value.size();

  auto iter = map_.find(key);
  if (iter == map_.end()) {
    QUICHE_DVLOG(1) << "Inserting: (" << key << ", " << value << ")";

    AppendHeader(key, value);
    return;
  }
  QUICHE_DVLOG(1) << "Updating key: " << iter->first
                  << "; appending value: " << value;
  value_size_ += SeparatorForKey(key).size();
  iter->second.Append(storage_.Write(value));
}

void Http2HeaderBlock::AppendHeader(const absl::string_view key,
                                    const absl::string_view value) {
  auto backed_key = WriteKey(key);
  map_.emplace(std::make_pair(
      backed_key, HeaderValue(&storage_, backed_key, storage_.Write(value))));
}

absl::string_view Http2HeaderBlock::WriteKey(const absl::string_view key) {
  key_size_ += key.size();
  return storage_.Write(key);
}

size_t Http2HeaderBlock::bytes_allocated() const {
  return storage_.bytes_allocated();
}

}  // namespace spdy
