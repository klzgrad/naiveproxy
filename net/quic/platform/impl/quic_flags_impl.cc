// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/platform/impl/quic_flags_impl.h"

#define QUIC_FLAG(type, flag, value) type flag = value;
#include "net/quic/core/quic_flags_list.h"
#undef QUIC_FLAG
