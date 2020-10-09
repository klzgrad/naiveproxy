// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/platform/impl/spdy_flags_impl.h"

// If true, use indexed name if possible when sending
// Literal Header Field without Indexing instruction.
bool spdy_hpack_use_indexed_name = true;

namespace spdy {}  // namespace spdy
