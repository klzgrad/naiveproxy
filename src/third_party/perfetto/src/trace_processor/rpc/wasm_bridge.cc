/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>

#include "perfetto/base/compiler.h"
#include "src/trace_processor/rpc/rpc.h"

namespace perfetto::trace_processor {

namespace {
using RpcResponseFn = void(const void*, uint32_t);

Rpc* g_trace_processor_rpc;

// The buffer used to pass the request arguments. The caller (JS) decides how
// big this buffer should be in the Initialize() call.
uint8_t* g_req_buf;

PERFETTO_NO_INLINE void OutOfMemoryHandler() {
  fprintf(stderr, "\nCannot enlarge memory\n");
  abort();
}

}  // namespace

// +---------------------------------------------------------------------------+
// | Exported functions called by the JS/TS running in the worker.             |
// +---------------------------------------------------------------------------+
extern "C" {

// Returns the address of the allocated request buffer.
uint8_t* EMSCRIPTEN_KEEPALIVE
trace_processor_rpc_init(RpcResponseFn* RpcResponseFn, uint32_t);
uint8_t* trace_processor_rpc_init(RpcResponseFn* resp_function,
                                  uint32_t req_buffer_size) {
  // Usually OOMs manifest as a failure in dlmalloc() -> sbrk() ->
  //_emscripten_resize_heap() which aborts itself. However in some rare cases
  // sbrk() can fail outside of _emscripten_resize_heap and just return null.
  // When that happens, just abort with the same message that
  // _emscripten_resize_heap uses, so error_dialog.ts shows a OOM message.
  std::set_new_handler(&OutOfMemoryHandler);

  g_trace_processor_rpc = new Rpc();

  // |resp_function| is a JS-bound function passed by wasm_bridge.ts. It will
  // call back into JavaScript. There the JS code will copy the passed
  // buffer with the response (a proto-encoded TraceProcessorRpc message) and
  // postMessage() it to the controller. See the comment in wasm_bridge.ts for
  // an overview of the JS<>Wasm callstack.
  g_trace_processor_rpc->SetRpcResponseFunction(resp_function);

  g_req_buf = new uint8_t[req_buffer_size];
  return g_req_buf;
}

void EMSCRIPTEN_KEEPALIVE trace_processor_on_rpc_request(uint32_t);
void trace_processor_on_rpc_request(uint32_t size) {
  g_trace_processor_rpc->OnRpcRequest(g_req_buf, size);
}

}  // extern "C"
}  // namespace perfetto::trace_processor

int main(int, char**) {
  // This is unused but is needed for the following reasons:
  // - We need the callMain() Emscripten JS helper function for traceconv (but
  //   not for trace_processor).
  // - Newer versions of emscripten require that callMain is explicitly exported
  //   via EXPORTED_RUNTIME_METHODS = ['callMain'].
  // - We have one set of EXPORTED_RUNTIME_METHODS for both
  //   trace_processor.wasm (which does not need a main()) and traceconv (which
  //   does).
  // - Without this main(), the Wasm bootstrap code will cause a JS error at
  //   runtime when trying to load trace_processor.js.
  return 0;
}
