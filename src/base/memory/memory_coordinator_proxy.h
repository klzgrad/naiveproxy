// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MEMORY_MEMORY_COORDINATOR_PROXY_H_
#define BASE_MEMORY_MEMORY_COORDINATOR_PROXY_H_

#include "base/base_export.h"
#include "base/callback.h"
#include "base/memory/memory_coordinator_client.h"
#include "base/memory/singleton.h"

namespace base {

// The MemoryCoordinator interface. See comments in MemoryCoordinatorProxy for
// method descriptions.
class BASE_EXPORT MemoryCoordinator {
 public:
  virtual ~MemoryCoordinator() = default;

  virtual MemoryState GetCurrentMemoryState() const = 0;
};

// The proxy of MemoryCoordinator to be accessed from components that are not
// in content/browser e.g. net.
class BASE_EXPORT MemoryCoordinatorProxy {
 public:
  static MemoryCoordinatorProxy* GetInstance();

  // Sets an implementation of MemoryCoordinator. MemoryCoordinatorProxy doesn't
  // take the ownership of |coordinator|. It must outlive this proxy.
  // This should be called before any components starts using this proxy.
  static void SetMemoryCoordinator(MemoryCoordinator* coordinator);

  // Returns the current memory state.
  MemoryState GetCurrentMemoryState() const;

 private:
  friend struct base::DefaultSingletonTraits<MemoryCoordinatorProxy>;

  MemoryCoordinatorProxy();
  virtual ~MemoryCoordinatorProxy();

  DISALLOW_COPY_AND_ASSIGN(MemoryCoordinatorProxy);
};

}  // namespace base

#endif  // BASE_MEMORY_MEMORY_COORDINATOR_PROXY_H_
