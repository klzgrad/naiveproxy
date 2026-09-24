/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_RPC_HTTPD_H_
#define SRC_TRACE_PROCESSOR_RPC_HTTPD_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "src/trace_processor/rpc/rpc.h"
#include "src/trace_processor/rpc/session_lifecycle.h"

namespace perfetto::trace_processor {

class TraceProcessor;

// Starts a RPC server that handles requests using protobuf-over-HTTP.
// It takes control of the calling thread. It returns only if the idle reaper
// (below) fires; otherwise it runs forever.
//
// `rpc` is the Rpc instance that will handle the requests.
// `listen_ip` is the ip address which http server will listen on,
// it can be an ipv4 or an ipv6 or a domain.
// `port_number` is the port which http server will listen on.
// `additional_cors_origins` is a list of origins to allow for CORS requests, in
// addition to the default origins defined in httpd.cc.
// `idle_timeout_ms` reaps the server after that much inactivity (0 = never,
// preserving the always-on behaviour the UI relies on). `idle_start` controls
// when the idle clock applies.
void RunHttpRPCServer(Rpc& rpc,
                      const std::string& listen_ip,
                      const std::string& port_number,
                      const std::vector<std::string>& additional_cors_origins,
                      uint32_t idle_timeout_ms,
                      IdleStart idle_start);

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_RPC_HTTPD_H_
