/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include <stdio.h>
#include <string.h>

#include "perfetto/ext/base/file_utils.h"
#include "src/trace_config_utils/pb_to_txt.h"
#include "src/trace_config_utils/txt_to_pb.h"

namespace {
void PrintUsage(const char* argv0) {
  printf(R"(
Converts a TraceConfig from pbtxt to proto-encoded bytes and viceversa

Usage: %s  txt_to_pb | pb_to_txt < in > out
)",
         argv0);
}

}  // namespace

int main(int argc, char** argv) {
  using namespace ::perfetto;

  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }

  const char* cmd = argv[1];
  std::string in_data;
  if (argc == 2) {
    base::ReadFileStream(stdin, &in_data);
  } else {
    bool ok = base::ReadFile(argv[2], &in_data);
    if (!ok) {
      printf("Failed to open input file %s\n", argv[2]);
      return 1;
    }
  }

  if (strcmp(cmd, "txt_to_pb") == 0) {
    base::StatusOr<std::vector<uint8_t>> res = TraceConfigTxtToPb(in_data);
    if (!res.ok()) {
      printf("%s\n", res.status().c_message());
      return 1;
    }
    fwrite(res->data(), res->size(), 1, stdout);
    return 0;
  }

  if (strcmp(cmd, "pb_to_txt") == 0) {
    std::string txt = TraceConfigPbToTxt(in_data.data(), in_data.size());
    printf("%s\n", txt.c_str());
    return 0;
  }

  PrintUsage(argv[0]);
  return 1;
}
