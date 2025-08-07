/*
 * Copyright (C) 2022 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/traceconv/trace_to_text.h"

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/hash.h"
#include "test/gtest_and_gmock.h"

#include <fstream>

using std::string;

namespace perfetto {
namespace trace_to_text {

// Given a file, compute the checksum/hash of file.
// Learn more @ base::Hasher.
// Precondition: File should exist and be accessible.
static uint64_t FileHash(const string& filename) {
  base::Hasher hash;
  std::ifstream input_f(filename, std::ios::binary);
  PERFETTO_DCHECK(input_f.good());
  char buffer[4096];
  while (!input_f.eof()) {
    input_f.read(buffer, sizeof(buffer));
    if (input_f.gcount() > 0) {
      hash.Update(buffer, size_t(input_f.gcount()));
    }
  }
  return hash.digest();
}

TEST(TraceToText, DISABLED_Basic) {
  auto tmp_file = "/tmp/trace_" + std::to_string(rand()) + ".txt";
  auto input_file_names = {"test/data/example_android_trace_30s.pb.gz",
                           "test/data/example_android_trace_30s.pb"};
  PERFETTO_LOG("tmp_file = %s.", tmp_file.c_str());
  for (auto filename : input_file_names) {
    {
      std::ifstream input_f(filename, std::ios::binary);
      std::ofstream output_f(tmp_file, std::ios::out | std::ios::binary);
      EXPECT_TRUE(TraceToText(&input_f, &output_f));
      PERFETTO_LOG("Processed %s", filename);
    }
    EXPECT_EQ(0xCD794377594BC7DCull, FileHash(tmp_file));
    remove(tmp_file.c_str());
  }
}

}  // namespace trace_to_text
}  // namespace perfetto
