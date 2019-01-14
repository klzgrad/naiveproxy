// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/ptr_util.h"
#include "base/time/time.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/perf/perf_test.h"

namespace base {

namespace {
// Generates a simple dictionary value with simple data types, a string and a
// list.
std::unique_ptr<DictionaryValue> GenerateDict() {
  auto root = std::make_unique<DictionaryValue>();
  root->SetDouble("Double", 3.141);
  root->SetBoolean("Bool", true);
  root->SetInteger("Int", 42);
  root->SetString("String", "Foo");

  auto list = std::make_unique<ListValue>();
  list->Set(0, std::make_unique<Value>(2.718));
  list->Set(1, std::make_unique<Value>(false));
  list->Set(2, std::make_unique<Value>(123));
  list->Set(3, std::make_unique<Value>("Bar"));
  root->Set("List", std::move(list));

  return root;
}

// Generates a tree-like dictionary value with a size of O(breadth ** depth).
std::unique_ptr<DictionaryValue> GenerateLayeredDict(int breadth, int depth) {
  if (depth == 1)
    return GenerateDict();

  auto root = GenerateDict();
  auto next = GenerateLayeredDict(breadth, depth - 1);

  for (int i = 0; i < breadth; ++i) {
    root->Set("Dict" + std::to_string(i), next->CreateDeepCopy());
  }

  return root;
}

}  // namespace

class JSONPerfTest : public testing::Test {
 public:
  void TestWriteAndRead(int breadth, int depth) {
    std::string description = "Breadth: " + std::to_string(breadth) +
                              ", Depth: " + std::to_string(depth);
    auto dict = GenerateLayeredDict(breadth, depth);
    std::string json;

    TimeTicks start_write = TimeTicks::Now();
    JSONWriter::Write(*dict, &json);
    TimeTicks end_write = TimeTicks::Now();
    perf_test::PrintResult("Write", "", description,
                           (end_write - start_write).InMillisecondsF(), "ms",
                           true);

    TimeTicks start_read = TimeTicks::Now();
    JSONReader::Read(json);
    TimeTicks end_read = TimeTicks::Now();
    perf_test::PrintResult("Read", "", description,
                           (end_read - start_read).InMillisecondsF(), "ms",
                           true);
  }
};

TEST_F(JSONPerfTest, StressTest) {
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 12; ++j) {
      TestWriteAndRead(i + 1, j + 1);
    }
  }
}

}  // namespace base
