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

#include <emscripten/emscripten.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "perfetto/ext/base/string_utils.h"
#include "src/proto_utils/pb_to_txt.h"
#include "src/proto_utils/txt_to_pb.h"

namespace {
// The buffer used to exchange input and output arguments. We assume 16MB
// is enough to handle trace configs and proto specs.
char wasm_buf[16 * 1024 * 1024];

// Helper: converts proto bytes in wasm_buf to text using the given converter,
// writes result back to wasm_buf, returns the text size.
uint32_t pb_to_txt(std::string (*converter)(const void*, size_t),
                   uint32_t size) {
  std::string txt = converter(wasm_buf, size);
  size_t wsize = perfetto::base::SprintfTrunc(wasm_buf, sizeof(wasm_buf), "%s",
                                              txt.c_str());
  return static_cast<uint32_t>(wsize);
}

// Helper: converts a pbtxt string in wasm_buf to proto-encoded bytes using the
// given converter. Because this can fail (the C++ function returns a StatusOr)
// we write a success/failure marker in the first byte.
using TxtToPbFn =
    perfetto::base::StatusOr<std::vector<uint8_t>> (*)(const std::string&,
                                                       const std::string&);
uint32_t txt_to_pb(TxtToPbFn converter, uint32_t size) {
  auto res = converter(std::string(wasm_buf, size), "-");
  if (!res.ok()) {
    wasm_buf[0] = 0;
    size_t wsize = perfetto::base::SprintfTrunc(
        &wasm_buf[1], sizeof(wasm_buf) - 1, "%s", res.status().c_message());
    return static_cast<uint32_t>(wsize);
  }
  const size_t resp_size = std::min(res->size(), sizeof(wasm_buf) - 1);
  wasm_buf[0] = 1;
  memcpy(&wasm_buf[1], res->data(), resp_size);
  return static_cast<uint32_t>(resp_size);
}
}  // namespace

extern "C" {

// Returns the pointer to the buffer.
void* EMSCRIPTEN_KEEPALIVE proto_utils_buf();
void* proto_utils_buf() {
  return &wasm_buf;
}

// Returns the size of the buffer.
uint32_t EMSCRIPTEN_KEEPALIVE proto_utils_buf_size();
uint32_t proto_utils_buf_size() {
  return static_cast<uint32_t>(sizeof(wasm_buf));
}

uint32_t EMSCRIPTEN_KEEPALIVE trace_config_pb_to_txt(uint32_t size);
uint32_t trace_config_pb_to_txt(uint32_t size) {
  return pb_to_txt(perfetto::TraceConfigPbToTxt, size);
}

uint32_t EMSCRIPTEN_KEEPALIVE trace_config_txt_to_pb(uint32_t size);
uint32_t trace_config_txt_to_pb(uint32_t size) {
  return txt_to_pb(perfetto::TraceConfigTxtToPb, size);
}

uint32_t EMSCRIPTEN_KEEPALIVE trace_summary_spec_to_text(uint32_t size);
uint32_t trace_summary_spec_to_text(uint32_t size) {
  return pb_to_txt(perfetto::TraceSummarySpecPbToTxt, size);
}

uint32_t EMSCRIPTEN_KEEPALIVE trace_summary_spec_txt_to_pb(uint32_t size);
uint32_t trace_summary_spec_txt_to_pb(uint32_t size) {
  return txt_to_pb(perfetto::TraceSummarySpecTxtToPb, size);
}

}  // extern "C"

// This is unused but is needed to keep emscripten happy.
int main(int, char**) {
  return 0;
}
