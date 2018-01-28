// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/ref_counted_memory.h"

#include "base/logging.h"

namespace base {

bool RefCountedMemory::Equals(
    const scoped_refptr<RefCountedMemory>& other) const {
  return other.get() &&
         size() == other->size() &&
         (memcmp(front(), other->front(), size()) == 0);
}

RefCountedMemory::RefCountedMemory() = default;

RefCountedMemory::~RefCountedMemory() = default;

const unsigned char* RefCountedStaticMemory::front() const {
  return data_;
}

size_t RefCountedStaticMemory::size() const {
  return length_;
}

RefCountedStaticMemory::~RefCountedStaticMemory() = default;

RefCountedBytes::RefCountedBytes() = default;

RefCountedBytes::RefCountedBytes(const std::vector<unsigned char>& initializer)
    : data_(initializer) {
}

RefCountedBytes::RefCountedBytes(const unsigned char* p, size_t size)
    : data_(p, p + size) {}

scoped_refptr<RefCountedBytes> RefCountedBytes::TakeVector(
    std::vector<unsigned char>* to_destroy) {
  scoped_refptr<RefCountedBytes> bytes(new RefCountedBytes);
  bytes->data_.swap(*to_destroy);
  return bytes;
}

const unsigned char* RefCountedBytes::front() const {
  // STL will assert if we do front() on an empty vector, but calling code
  // expects a NULL.
  return size() ? &data_.front() : nullptr;
}

size_t RefCountedBytes::size() const {
  return data_.size();
}

RefCountedBytes::~RefCountedBytes() = default;

RefCountedString::RefCountedString() = default;

RefCountedString::~RefCountedString() = default;

// static
scoped_refptr<RefCountedString> RefCountedString::TakeString(
    std::string* to_destroy) {
  scoped_refptr<RefCountedString> self(new RefCountedString);
  to_destroy->swap(self->data_);
  return self;
}

const unsigned char* RefCountedString::front() const {
  return data_.empty() ? nullptr
                       : reinterpret_cast<const unsigned char*>(data_.data());
}

size_t RefCountedString::size() const {
  return data_.size();
}

}  //  namespace base
