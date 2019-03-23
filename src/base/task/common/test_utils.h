// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_COMMON_TEST_UTILS_H_
#define BASE_TASK_COMMON_TEST_UTILS_H_

#include "base/task/common/intrusive_heap.h"

namespace base {
namespace internal {
namespace test {

struct TestElement {
  int key;
  HeapHandle* handle;
  bool stale = false;

  bool operator<=(const TestElement& other) const { return key <= other.key; }

  void SetHeapHandle(HeapHandle h) {
    if (handle)
      *handle = h;
  }

  void ClearHeapHandle() {
    if (handle)
      *handle = HeapHandle();
  }

  bool IsStale() { return stale; }
};

}  // namespace test
}  // namespace internal
}  // namespace base

#endif  // BASE_TASK_COMMON_TEST_UTILS_H_
