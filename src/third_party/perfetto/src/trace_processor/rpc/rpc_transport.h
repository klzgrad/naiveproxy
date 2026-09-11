/*
 * Copyright (C) 2026 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_RPC_RPC_TRANSPORT_H_
#define SRC_TRACE_PROCESSOR_RPC_RPC_TRANSPORT_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_or.h"

namespace perfetto::trace_processor {

// A bidirectional byte channel carrying the TraceProcessor RPC byte-pipe
// protocol (serialized TraceProcessorRpcStream messages). Two implementations
// exist: a raw AF_UNIX byte pipe and a WebSocket client (to the http server's
// /websocket endpoint). Both ultimately carry the same framed bytes, so the
// RemoteTraceProcessor's encode/decode logic is transport-agnostic.
class RpcTransport {
 public:
  virtual ~RpcTransport();

  // Sends a complete, already-serialized TraceProcessorRpcStream.
  virtual base::Status Send(const uint8_t* data, size_t len) = 0;

  // Receives up to |len| bytes into |buf|. Returns the number of bytes read;
  // 0 means the peer closed the channel.
  virtual base::StatusOr<size_t> Recv(uint8_t* buf, size_t len) = 0;
};

// Connects an RpcTransport for |addr|, resolved as in `--remote`:
//   - host:port / scheme://  -> WebSocket to the http server's /websocket.
//   - absolute path / *.sock  -> AF_UNIX byte pipe at that path.
//   - bare session name       -> AF_UNIX byte pipe at the convention path.
base::StatusOr<std::unique_ptr<RpcTransport>> ConnectRpcTransport(
    const std::string& addr);

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_RPC_RPC_TRANSPORT_H_
