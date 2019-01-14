// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/process_info.h"

#include "base/time/time.h"

namespace base {

// static
const Time CurrentProcessInfo::CreationTime() {
  // TODO(https://crbug.com/726484): There is no syscall providing this data.
  return Time();
}

}  // namespace base
