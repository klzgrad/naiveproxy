// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_THREAD_LOCAL_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_THREAD_LOCAL_IMPL_H_

#define DEFINE_QUICHE_THREAD_LOCAL_POINTER_IMPL(name, type) \
  struct QuicheThreadLocalPointer_##name {                  \
    static type** Instance() {                              \
      static thread_local type* instance = nullptr;         \
      return &instance;                                     \
    }                                                       \
    static type* Get() { return *Instance(); }              \
    static void Set(type* ptr) { *Instance() = ptr; }       \
  }

#define GET_QUICHE_THREAD_LOCAL_POINTER_IMPL(name) \
  QuicheThreadLocalPointer_##name::Get()

#define SET_QUICHE_THREAD_LOCAL_POINTER_IMPL(name, value) \
  QuicheThreadLocalPointer_##name::Set(value)

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_THREAD_LOCAL_IMPL_H_
