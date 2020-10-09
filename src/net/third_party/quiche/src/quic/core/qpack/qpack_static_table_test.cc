// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_static_table.h"

#include <set>

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

namespace test {

namespace {

// Check that an initialized instance has the right number of entries.
TEST(QpackStaticTableTest, Initialize) {
  QpackStaticTable table;
  EXPECT_FALSE(table.IsInitialized());

  table.Initialize(QpackStaticTableVector().data(),
                   QpackStaticTableVector().size());
  EXPECT_TRUE(table.IsInitialized());

  auto static_entries = table.GetStaticEntries();
  EXPECT_EQ(QpackStaticTableVector().size(), static_entries.size());

  auto static_index = table.GetStaticIndex();
  EXPECT_EQ(QpackStaticTableVector().size(), static_index.size());

  auto static_name_index = table.GetStaticNameIndex();
  std::set<quiche::QuicheStringPiece> names;
  for (auto entry : static_index) {
    names.insert(entry->name());
  }
  EXPECT_EQ(names.size(), static_name_index.size());
}

// Test that ObtainQpackStaticTable returns the same instance every time.
TEST(QpackStaticTableTest, IsSingleton) {
  const QpackStaticTable* static_table_one = &ObtainQpackStaticTable();
  const QpackStaticTable* static_table_two = &ObtainQpackStaticTable();
  EXPECT_EQ(static_table_one, static_table_two);
}

}  // namespace

}  // namespace test

}  // namespace quic
