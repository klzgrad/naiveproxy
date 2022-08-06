// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_THREAD_LOCAL_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_THREAD_LOCAL_H_

#include "quiche_platform_impl/quiche_thread_local_impl.h"

// Define a thread local |type*| with |name|. Conceptually, this is a
//
//  static thread_local type* name = nullptr;
//
// It is wrapped in a macro because the thread_local keyword is banned from
// Chromium.
#define DEFINE_QUICHE_THREAD_LOCAL_POINTER(name, type) \
  DEFINE_QUICHE_THREAD_LOCAL_POINTER_IMPL(name, type)

// Get the value of |name| for the current thread.
#define GET_QUICHE_THREAD_LOCAL_POINTER(name) \
  GET_QUICHE_THREAD_LOCAL_POINTER_IMPL(name)

// Set the |value| of |name| for the current thread.
#define SET_QUICHE_THREAD_LOCAL_POINTER(name, value) \
  SET_QUICHE_THREAD_LOCAL_POINTER_IMPL(name, value)

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_THREAD_LOCAL_H_
