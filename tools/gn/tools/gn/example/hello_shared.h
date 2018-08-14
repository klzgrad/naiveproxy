// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_EXAMPLE_HELLO_SHARED_H_
#define TOOLS_GN_EXAMPLE_HELLO_SHARED_H_

#if defined(WIN32)

#if defined(HELLO_SHARED_IMPLEMENTATION)
#define HELLO_EXPORT __declspec(dllexport)
#define HELLO_EXPORT_PRIVATE __declspec(dllexport)
#else
#define HELLO_EXPORT __declspec(dllimport)
#define HELLO_EXPORT_PRIVATE __declspec(dllimport)
#endif  // defined(HELLO_SHARED_IMPLEMENTATION)

#else

#if defined(HELLO_SHARED_IMPLEMENTATION)
#define HELLO_EXPORT __attribute__((visibility("default")))
#define HELLO_EXPORT_PRIVATE __attribute__((visibility("default")))
#else
#define HELLO_EXPORT
#define HELLO_EXPORT_PRIVATE
#endif  // defined(HELLO_SHARED_IMPLEMENTATION)

#endif

HELLO_EXPORT const char* GetSharedText();

#endif  // TOOLS_GN_EXAMPLE_HELLO_SHARED_H_
