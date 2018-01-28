// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/platform/impl/quic_test_impl.h"

#include "base/logging.h"
#include "net/quic/platform/api/quic_flags.h"

QuicFlagSaver::QuicFlagSaver() {
#define QUIC_FLAG(type, flag, value)                                 \
  CHECK_EQ(value, flag)                                              \
      << "Flag set to an unexpected value.  A prior test is likely " \
      << "setting a flag without using a QuicFlagSaver";
#include "net/quic/core/quic_flags_list.h"
#undef QUIC_FLAG
}

QuicFlagSaver::~QuicFlagSaver() {
#define QUIC_FLAG(type, flag, value) flag = value;
#include "net/quic/core/quic_flags_list.h"
#undef QUIC_FLAG
}
