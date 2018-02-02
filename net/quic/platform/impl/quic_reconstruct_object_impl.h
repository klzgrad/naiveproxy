// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_IMPL_QUIC_RECONSTRUCT_OBJECT_IMPL_H_
#define NET_QUIC_PLATFORM_IMPL_QUIC_RECONSTRUCT_OBJECT_IMPL_H_

// Support for marking memory as uninitialized, or otherwise corrupting it. Used
// in testing in an attempt to ensure that there isn't "leakage" of state from
// one sub-test to another. For example, in tests based on RandomDecoderTest,
// the same objects (decoder or decoder destination), will be used multiple
// times as a single encoded input is repeatedly decoded with multiple
// segmentations of the input.
//
// If compiled with Memory Sanitizer, the memory is marked as uninitialized;
// else the memory is overwritten with random bytes.

#include <stddef.h>

#include <algorithm>
#include <new>
#include <utility>

namespace net {
namespace test {

// Reconstruct an object so that it is initialized as when it was first
// constructed. Runs the destructor to handle objects that might own resources,
// marks the object's memory as as uninitialized, and finally runs the
// constructor with the provided arguments, if any.
template <class T, class... Args>
void QuicReconstructObjectImpl(T* ptr,
                               QuicTestRandomBase* rng,
                               Args&&... args) {
  ptr->~T();
  ::new (ptr) T(std::forward<Args>(args)...);
}
// This version applies default-initialization to the object.
template <class T>
void QuicDefaultReconstructObjectImpl(T* ptr, QuicTestRandomBase* rng) {
  ptr->~T();
  ::new (ptr) T;
}

}  // namespace test

}  // namespace net

#endif  // NET_QUIC_PLATFORM_IMPL_QUIC_RECONSTRUCT_OBJECT_IMPL_H_
