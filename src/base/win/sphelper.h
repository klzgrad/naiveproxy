// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef BASE_WIN_SPHELPER_H_
#define BASE_WIN_SPHELPER_H_

// Check no prior poisonous defines were made.
#include "base/win/windows_defines.inc"
// Undefine before windows header will make the poisonous defines
#include "base/win/windows_undefines.inc"

#include <sphelper.h>

// Undefine the poisonous defines
#include "base/win/windows_undefines.inc"
// Check no poisonous defines follow this include
#include "base/win/windows_defines.inc"

#endif  // BASE_WIN_SPHELPER_H_
