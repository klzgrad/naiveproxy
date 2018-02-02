// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_API_QUIC_RECONSTRUCT_OBJECT_H_
#define NET_QUIC_PLATFORM_API_QUIC_RECONSTRUCT_OBJECT_H_

#include <utility>

#include "net/quic/platform/impl/quic_reconstruct_object_impl.h"

namespace net {
namespace test {

// Reconstruct an object so that it is initialized as when it was first
// constructed. Runs the destructor to handle objects that might own resources,
// and runs the constructor with the provided arguments, if any.
template <class T, class... Args>
void QuicReconstructObject(T* ptr, QuicTestRandomBase* rng, Args&&... args) {
  QuicReconstructObjectImpl(ptr, rng, std::forward<Args>(args)...);
}

// This version applies default-initialization to the object.
template <class T>
void QuicDefaultReconstructObject(T* ptr, QuicTestRandomBase* rng) {
  QuicDefaultReconstructObjectImpl(ptr, rng);
}

}  // namespace test
}  // namespace net

#endif  // NET_QUIC_PLATFORM_API_QUIC_RECONSTRUCT_OBJECT_H_
