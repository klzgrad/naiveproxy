// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BALSA_FRAMER_INTERFACE_H_
#define QUICHE_BALSA_FRAMER_INTERFACE_H_

#include <cstddef>

#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

// A minimal interface supported by BalsaFrame and other framer types. For use
// in HttpReader.
class QUICHE_EXPORT FramerInterface {
 public:
  virtual ~FramerInterface() {}
  virtual size_t ProcessInput(const char* input, size_t length) = 0;
};

}  // namespace quiche

#endif  // QUICHE_BALSA_FRAMER_INTERFACE_H_
