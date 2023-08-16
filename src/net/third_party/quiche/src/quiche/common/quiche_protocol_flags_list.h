// Copyright (c) 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// NOLINT(build/header_guard)
// This file intentionally does not have header guards, it's intended to be
// included multiple times, each time with a different definition of
// QUICHE_PROTOCOL_FLAG.

#if defined(QUICHE_PROTOCOL_FLAG)

QUICHE_PROTOCOL_FLAG(bool, quiche_oghttp2_debug_trace, false,
                     "If true, emits trace logs for HTTP/2 events.")

#endif
