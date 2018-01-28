// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_WIN_SCOPED_COMPTR_H_
#define BASE_WIN_SCOPED_COMPTR_H_

#include <wrl/client.h>

namespace base {
namespace win {

template <typename T>
using ScopedComPtr = Microsoft::WRL::ComPtr<T>;

}  // namespace win
}  // namespace base

#endif  // BASE_WIN_SCOPED_COMPTR_H_
