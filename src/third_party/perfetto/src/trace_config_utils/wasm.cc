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
#include "src/trace_config_utils/pb_to_txt.h"
#include "src/trace_config_utils/txt_to_pb.h"

namespace {
// The buffer used to exchange input and output arguments. We assume 16MB
// is enough to handle trace configs.
char wasm_buf[16 * 1024 * 1024];
}  // namespace

extern "C" {

// Returns the pointer to the buffer.
void* EMSCRIPTEN_KEEPALIVE trace_config_utils_buf();
void* trace_config_utils_buf() {
  return &wasm_buf;
}

// Returns the size of the buffer, so trace_config_utils_wasm.ts doesn't have
// to hardcode the 16MB.
uint32_t EMSCRIPTEN_KEEPALIVE trace_config_utils_buf_size();
uint32_t trace_config_utils_buf_size() {
  return static_cast<uint32_t>(sizeof(wasm_buf));
}

// Converts a proto-encoded protos.TraceConfig to text.
// The caller must memcpy the bytes into the wasm_buf and pass the size of the
// copied data into `size`. The returned pbtxt will be written in wasm_buf and
// its size returned here.
uint32_t EMSCRIPTEN_KEEPALIVE trace_config_pb_to_txt(uint32_t size);

uint32_t trace_config_pb_to_txt(uint32_t size) {
  std::string txt = perfetto::TraceConfigPbToTxt(wasm_buf, size);
  size_t wsize = perfetto::base::SprintfTrunc(wasm_buf, sizeof(wasm_buf), "%s",
                                              txt.c_str());
  return static_cast<uint32_t>(wsize);
}

// Like the above, but converts a pbtxt into proto-encoded bytes.
// Because this can fail (the C++ function returns a StatusOr) we write
// a success/failure in the first byte to tell the difference.
uint32_t EMSCRIPTEN_KEEPALIVE trace_config_txt_to_pb(uint32_t size);

uint32_t trace_config_txt_to_pb(uint32_t size) {
  auto res = perfetto::TraceConfigTxtToPb(std::string(wasm_buf, size));
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
}  // extern "C"

// This is unused but is needed to keep emscripten happy.
int main(int, char**) {
  return 0;
}
