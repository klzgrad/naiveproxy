// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_DEBUG_GDI_DEBUG_UTIL_WIN_H_
#define BASE_DEBUG_GDI_DEBUG_UTIL_WIN_H_

#include <windows.h>

#include "base/base_export.h"

namespace base {
namespace debug {

// Crashes the process, using base::debug::Alias to leave valuable debugging
// information in the crash dump. Pass values for |header| and |shared_section|
// in the event of a bitmap allocation failure, to gather information about
// those as well.
void BASE_EXPORT CollectGDIUsageAndDie(BITMAPINFOHEADER* header = nullptr,
                                       HANDLE shared_section = nullptr);

}  // namespace debug
}  // namespace base

#endif  // BASE_DEBUG_GDI_DEBUG_UTIL_WIN_H_
