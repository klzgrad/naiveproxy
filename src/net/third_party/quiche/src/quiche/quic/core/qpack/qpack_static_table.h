// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_QPACK_STATIC_TABLE_H_
#define QUICHE_QUIC_CORE_QPACK_QPACK_STATIC_TABLE_H_

#include <vector>

#include "quiche/http2/hpack/hpack_constants.h"
#include "quiche/http2/hpack/hpack_static_table.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

using QpackStaticEntry = spdy::HpackStaticEntry;
using QpackStaticTable = spdy::HpackStaticTable;

// QPACK static table defined at
// https://rfc-editor.org/rfc/rfc9204.html#section-3.1.
QUICHE_EXPORT const std::vector<QpackStaticEntry>& QpackStaticTableVector();

// Returns a QpackStaticTable instance initialized with kQpackStaticTable.
// The instance is read-only, has static lifetime, and is safe to share amoung
// threads. This function is thread-safe.
QUICHE_EXPORT const QpackStaticTable& ObtainQpackStaticTable();

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_STATIC_TABLE_H_
