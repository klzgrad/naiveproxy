// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP2_PLATFORM_IMPL_HTTP2_RECONSTRUCT_OBJECT_IMPL_H_
#define NET_HTTP2_PLATFORM_IMPL_HTTP2_RECONSTRUCT_OBJECT_IMPL_H_

#include <utility>

namespace http2 {
namespace test {

class Http2Random;

// Reconstruct an object so that it is initialized as when it was first
// constructed. Runs the destructor to handle objects that might own resources,
// and runs the constructor with the provided arguments, if any.
template <class T, class... Args>
void Http2ReconstructObjectImpl(T* ptr, Http2Random* rng, Args&&... args) {
  ptr->~T();
  ::new (ptr) T(std::forward<Args>(args)...);
}

// This version applies default-initialization to the object.
template <class T>
void Http2DefaultReconstructObjectImpl(T* ptr, Http2Random* rng) {
  ptr->~T();
  ::new (ptr) T;
}

}  // namespace test
}  // namespace http2

#endif  // NET_HTTP2_PLATFORM_IMPL_HTTP2_RECONSTRUCT_OBJECT_IMPL_H_
