// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_QPACK_STATIC_TABLE_H_
#define QUICHE_QUIC_CORE_QPACK_QPACK_STATIC_TABLE_H_

#include <vector>

#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/spdy/core/hpack/hpack_constants.h"
#include "net/third_party/quiche/src/spdy/core/hpack/hpack_static_table.h"

namespace quic {

using QpackStaticEntry = spdy::HpackStaticEntry;
using QpackStaticTable = spdy::HpackStaticTable;

// QPACK static table defined at
// https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#static-table.
QUIC_EXPORT_PRIVATE const std::vector<QpackStaticEntry>&
QpackStaticTableVector();

// Returns a QpackStaticTable instance initialized with kQpackStaticTable.
// The instance is read-only, has static lifetime, and is safe to share amoung
// threads. This function is thread-safe.
QUIC_EXPORT_PRIVATE const QpackStaticTable& ObtainQpackStaticTable();

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_STATIC_TABLE_H_
