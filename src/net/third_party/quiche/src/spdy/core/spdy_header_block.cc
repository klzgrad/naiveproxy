// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/spdy/core/spdy_header_block.h"

#include <string.h>

#include <algorithm>
#include <utility>

#include "net/third_party/quiche/src/spdy/platform/api/spdy_estimate_memory_usage.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_logging.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_string_utils.h"

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

quiche::QuicheStringPiece SeparatorForKey(quiche::QuicheStringPiece key) {
  if (key == kCookieKey) {
    static quiche::QuicheStringPiece cookie_separator = "; ";
    return cookie_separator;
  } else {
    return quiche::QuicheStringPiece(&kNullSeparator, 1);
  }
}

}  // namespace

SpdyHeaderBlock::HeaderValue::HeaderValue(
    SpdyHeaderStorage* storage,
    quiche::QuicheStringPiece key,
    quiche::QuicheStringPiece initial_value)
    : storage_(storage),
      fragments_({initial_value}),
      pair_({key, {}}),
      size_(initial_value.size()),
      separator_size_(SeparatorForKey(key).size()) {}

SpdyHeaderBlock::HeaderValue::HeaderValue(HeaderValue&& other)
    : storage_(other.storage_),
      fragments_(std::move(other.fragments_)),
      pair_(std::move(other.pair_)),
      size_(other.size_),
      separator_size_(other.separator_size_) {}

SpdyHeaderBlock::HeaderValue& SpdyHeaderBlock::HeaderValue::operator=(
    HeaderValue&& other) {
  storage_ = other.storage_;
  fragments_ = std::move(other.fragments_);
  pair_ = std::move(other.pair_);
  size_ = other.size_;
  separator_size_ = other.separator_size_;
  return *this;
}

void SpdyHeaderBlock::HeaderValue::set_storage(SpdyHeaderStorage* storage) {
  storage_ = storage;
}

SpdyHeaderBlock::HeaderValue::~HeaderValue() = default;

quiche::QuicheStringPiece SpdyHeaderBlock::HeaderValue::ConsolidatedValue()
    const {
  if (fragments_.empty()) {
    return quiche::QuicheStringPiece();
  }
  if (fragments_.size() > 1) {
    fragments_ = {
        storage_->WriteFragments(fragments_, SeparatorForKey(pair_.first))};
  }
  return fragments_[0];
}

void SpdyHeaderBlock::HeaderValue::Append(quiche::QuicheStringPiece fragment) {
  size_ += (fragment.size() + separator_size_);
  fragments_.push_back(fragment);
}

const std::pair<quiche::QuicheStringPiece, quiche::QuicheStringPiece>&
SpdyHeaderBlock::HeaderValue::as_pair() const {
  pair_.second = ConsolidatedValue();
  return pair_;
}

SpdyHeaderBlock::iterator::iterator(MapType::const_iterator it) : it_(it) {}

SpdyHeaderBlock::iterator::iterator(const iterator& other) = default;

SpdyHeaderBlock::iterator::~iterator() = default;

SpdyHeaderBlock::ValueProxy::ValueProxy(
    SpdyHeaderBlock* block,
    SpdyHeaderBlock::MapType::iterator lookup_result,
    const quiche::QuicheStringPiece key,
    size_t* spdy_header_block_value_size)
    : block_(block),
      lookup_result_(lookup_result),
      key_(key),
      spdy_header_block_value_size_(spdy_header_block_value_size),
      valid_(true) {}

SpdyHeaderBlock::ValueProxy::ValueProxy(ValueProxy&& other)
    : block_(other.block_),
      lookup_result_(other.lookup_result_),
      key_(other.key_),
      spdy_header_block_value_size_(other.spdy_header_block_value_size_),
      valid_(true) {
  other.valid_ = false;
}

SpdyHeaderBlock::ValueProxy& SpdyHeaderBlock::ValueProxy::operator=(
    SpdyHeaderBlock::ValueProxy&& other) {
  block_ = other.block_;
  lookup_result_ = other.lookup_result_;
  key_ = other.key_;
  valid_ = true;
  other.valid_ = false;
  spdy_header_block_value_size_ = other.spdy_header_block_value_size_;
  return *this;
}

SpdyHeaderBlock::ValueProxy::~ValueProxy() {
  // If the ValueProxy is destroyed while lookup_result_ == block_->end(),
  // the assignment operator was never used, and the block's SpdyHeaderStorage
  // can reclaim the memory used by the key. This makes lookup-only access to
  // SpdyHeaderBlock through operator[] memory-neutral.
  if (valid_ && lookup_result_ == block_->map_.end()) {
    block_->storage_.Rewind(key_);
  }
}

SpdyHeaderBlock::ValueProxy& SpdyHeaderBlock::ValueProxy::operator=(
    quiche::QuicheStringPiece value) {
  *spdy_header_block_value_size_ += value.size();
  SpdyHeaderStorage* storage = &block_->storage_;
  if (lookup_result_ == block_->map_.end()) {
    SPDY_DVLOG(1) << "Inserting: (" << key_ << ", " << value << ")";
    lookup_result_ =
        block_->map_
            .emplace(std::make_pair(
                key_, HeaderValue(storage, key_, storage->Write(value))))
            .first;
  } else {
    SPDY_DVLOG(1) << "Updating key: " << key_ << " with value: " << value;
    *spdy_header_block_value_size_ -= lookup_result_->second.SizeEstimate();
    lookup_result_->second = HeaderValue(storage, key_, storage->Write(value));
  }
  return *this;
}

bool SpdyHeaderBlock::ValueProxy::operator==(
    quiche::QuicheStringPiece value) const {
  if (lookup_result_ == block_->map_.end()) {
    return false;
  } else {
    return value == lookup_result_->second.value();
  }
}

std::string SpdyHeaderBlock::ValueProxy::as_string() const {
  if (lookup_result_ == block_->map_.end()) {
    return "";
  } else {
    return std::string(lookup_result_->second.value());
  }
}

SpdyHeaderBlock::SpdyHeaderBlock() : map_(kInitialMapBuckets) {}

SpdyHeaderBlock::SpdyHeaderBlock(SpdyHeaderBlock&& other)
    : map_(kInitialMapBuckets) {
  map_.swap(other.map_);
  storage_ = std::move(other.storage_);
  for (auto& p : map_) {
    p.second.set_storage(&storage_);
  }
  key_size_ = other.key_size_;
  value_size_ = other.value_size_;
}

SpdyHeaderBlock::~SpdyHeaderBlock() = default;

SpdyHeaderBlock& SpdyHeaderBlock::operator=(SpdyHeaderBlock&& other) {
  map_.swap(other.map_);
  storage_ = std::move(other.storage_);
  for (auto& p : map_) {
    p.second.set_storage(&storage_);
  }
  key_size_ = other.key_size_;
  value_size_ = other.value_size_;
  return *this;
}

SpdyHeaderBlock SpdyHeaderBlock::Clone() const {
  SpdyHeaderBlock copy;
  for (const auto& p : *this) {
    copy.AppendHeader(p.first, p.second);
  }
  return copy;
}

bool SpdyHeaderBlock::operator==(const SpdyHeaderBlock& other) const {
  return size() == other.size() && std::equal(begin(), end(), other.begin());
}

bool SpdyHeaderBlock::operator!=(const SpdyHeaderBlock& other) const {
  return !(operator==(other));
}

std::string SpdyHeaderBlock::DebugString() const {
  if (empty()) {
    return "{}";
  }

  std::string output = "\n{\n";
  for (auto it = begin(); it != end(); ++it) {
    SpdyStrAppend(&output, "  ", it->first, " ", it->second, "\n");
  }
  SpdyStrAppend(&output, "}\n");
  return output;
}

void SpdyHeaderBlock::erase(quiche::QuicheStringPiece key) {
  auto iter = map_.find(key);
  if (iter != map_.end()) {
    SPDY_DVLOG(1) << "Erasing header with name: " << key;
    key_size_ -= key.size();
    value_size_ -= iter->second.SizeEstimate();
    map_.erase(iter);
  }
}

void SpdyHeaderBlock::clear() {
  key_size_ = 0;
  value_size_ = 0;
  map_.clear();
  storage_.Clear();
}

void SpdyHeaderBlock::insert(const SpdyHeaderBlock::value_type& value) {
  // TODO(birenroy): Write new value in place of old value, if it fits.
  value_size_ += value.second.size();

  auto iter = map_.find(value.first);
  if (iter == map_.end()) {
    SPDY_DVLOG(1) << "Inserting: (" << value.first << ", " << value.second
                  << ")";
    AppendHeader(value.first, value.second);
  } else {
    SPDY_DVLOG(1) << "Updating key: " << iter->first
                  << " with value: " << value.second;
    value_size_ -= iter->second.SizeEstimate();
    iter->second =
        HeaderValue(&storage_, iter->first, storage_.Write(value.second));
  }
}

SpdyHeaderBlock::ValueProxy SpdyHeaderBlock::operator[](
    const quiche::QuicheStringPiece key) {
  SPDY_DVLOG(2) << "Operator[] saw key: " << key;
  quiche::QuicheStringPiece out_key;
  auto iter = map_.find(key);
  if (iter == map_.end()) {
    // We write the key first, to assure that the ValueProxy has a
    // reference to a valid QuicheStringPiece in its operator=.
    out_key = WriteKey(key);
    SPDY_DVLOG(2) << "Key written as: " << std::hex
                  << static_cast<const void*>(key.data()) << ", " << std::dec
                  << key.size();
  } else {
    out_key = iter->first;
  }
  return ValueProxy(this, iter, out_key, &value_size_);
}

void SpdyHeaderBlock::AppendValueOrAddHeader(
    const quiche::QuicheStringPiece key,
    const quiche::QuicheStringPiece value) {
  value_size_ += value.size();

  auto iter = map_.find(key);
  if (iter == map_.end()) {
    SPDY_DVLOG(1) << "Inserting: (" << key << ", " << value << ")";

    AppendHeader(key, value);
    return;
  }
  SPDY_DVLOG(1) << "Updating key: " << iter->first
                << "; appending value: " << value;
  value_size_ += SeparatorForKey(key).size();
  iter->second.Append(storage_.Write(value));
}

size_t SpdyHeaderBlock::EstimateMemoryUsage() const {
  // TODO(xunjieli): https://crbug.com/669108. Also include |map_| when EMU()
  // supports linked_hash_map.
  return SpdyEstimateMemoryUsage(storage_);
}

void SpdyHeaderBlock::AppendHeader(const quiche::QuicheStringPiece key,
                                   const quiche::QuicheStringPiece value) {
  auto backed_key = WriteKey(key);
  map_.emplace(std::make_pair(
      backed_key, HeaderValue(&storage_, backed_key, storage_.Write(value))));
}

quiche::QuicheStringPiece SpdyHeaderBlock::WriteKey(
    const quiche::QuicheStringPiece key) {
  key_size_ += key.size();
  return storage_.Write(key);
}

size_t SpdyHeaderBlock::bytes_allocated() const {
  return storage_.bytes_allocated();
}

}  // namespace spdy
