// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_WIN_SCOPED_COM_INITIALIZER_H_
#define BASE_WIN_SCOPED_COM_INITIALIZER_H_

#include <objbase.h>

#include "base/base_export.h"
#include "base/macros.h"
#include "base/threading/thread_checker.h"
#include "base/win/scoped_windows_thread_environment.h"

namespace base {
namespace win {

// Initializes COM in the constructor (STA or MTA), and uninitializes COM in the
// destructor.
//
// WARNING: This should only be used once per thread, ideally scoped to a
// similar lifetime as the thread itself.  You should not be using this in
// random utility functions that make COM calls -- instead ensure these
// functions are running on a COM-supporting thread!
class BASE_EXPORT ScopedCOMInitializer : public ScopedWindowsThreadEnvironment {
 public:
  // Enum value provided to initialize the thread as an MTA instead of STA.
  enum SelectMTA { kMTA };

  // Constructor for STA initialization.
  ScopedCOMInitializer();

  // Constructor for MTA initialization.
  explicit ScopedCOMInitializer(SelectMTA mta);

  ~ScopedCOMInitializer() override;

  // ScopedWindowsThreadEnvironment:
  bool Succeeded() const override;

 private:
  void Initialize(COINIT init);

  HRESULT hr_;
  THREAD_CHECKER(thread_checker_);

  DISALLOW_COPY_AND_ASSIGN(ScopedCOMInitializer);
};

}  // namespace win
}  // namespace base

#endif  // BASE_WIN_SCOPED_COM_INITIALIZER_H_
