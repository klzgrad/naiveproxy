// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/platform/impl/quic_test_impl.h"

QuicFlagSaverImpl::QuicFlagSaverImpl() {
#define QUIC_FLAG(type, flag, value) saved_##flag##_ = flag;
#include "net/quic/quic_flags_list.h"
#undef QUIC_FLAG
}

QuicFlagSaverImpl::~QuicFlagSaverImpl() {
#define QUIC_FLAG(type, flag, value) flag = saved_##flag##_;
#include "net/quic/quic_flags_list.h"
#undef QUIC_FLAG
}
