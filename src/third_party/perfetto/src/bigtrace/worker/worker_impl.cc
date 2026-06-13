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

#include <mutex>
#include <thread>

#include <grpcpp/server_context.h>
#include <grpcpp/support/status.h>

#include "perfetto/base/time.h"
#include "perfetto/ext/trace_processor/rpc/query_result_serializer.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/bigtrace/worker/worker_impl.h"

namespace perfetto::bigtrace {

grpc::Status WorkerImpl::QueryTrace(
    grpc::ServerContext* server_context,
    const protos::BigtraceQueryTraceArgs* args,
    protos::BigtraceQueryTraceResponse* response) {
  std::mutex mutex;
  bool is_thread_done = false;

  std::string args_trace = args->trace();

  if (args_trace.empty()) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "Empty trace name is not valid");
  }

  if (args_trace[0] != '/') {
    return grpc::Status(
        grpc::StatusCode::INVALID_ARGUMENT,
        "Trace path must contain and begin with / for the prefix");
  }

  std::string prefix = args_trace.substr(0, args_trace.find("/", 1));
  if (registry_.find(prefix) == registry_.end()) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "Path prefix does not exist in registry");
  }

  if (prefix.length() == args_trace.length()) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "Empty path is invalid");
  }

  std::string path = args_trace.substr(prefix.length() + 1);

  base::StatusOr<std::unique_ptr<trace_processor::TraceProcessor>> tp_or =
      registry_[prefix]->LoadTraceProcessor(path);

  if (!tp_or.ok()) {
    const std::string& error_message = tp_or.status().message();
    return grpc::Status(grpc::StatusCode::INTERNAL, error_message);
  }

  std::unique_ptr<trace_processor::TraceProcessor> tp = std::move(*tp_or);
  std::optional<trace_processor::Iterator> iterator;

  std::thread execute_query_thread([&]() {
    iterator = tp->ExecuteQuery(args->sql_query());
    std::lock_guard<std::mutex> lk(mutex);
    is_thread_done = true;
  });

  for (;;) {
    if (server_context->IsCancelled()) {
      // If the thread is cancelled, we need to propagate the information to the
      // trace processor thread and we do this by attempting to interrupt the
      // trace processor every 10ms until the trace processor thread returns.
      //
      // A loop is necessary here because, due to scheduling delay, it is
      // possible we are cancelled before trace processor even started running.
      // InterruptQuery is ignored if it happens before entering TraceProcessor
      // which can cause the query to not be interrupted at all.
      while (!execute_query_thread.joinable()) {
        base::SleepMicroseconds(10000);
        tp->InterruptQuery();
      }
      execute_query_thread.join();
      return grpc::Status::CANCELLED;
    }

    std::lock_guard<std::mutex> lk(mutex);
    if (is_thread_done) {
      execute_query_thread.join();
      trace_processor::QueryResultSerializer serializer =
          trace_processor::QueryResultSerializer(*std::move(iterator));

      std::vector<uint8_t> serialized;
      for (bool has_more = true; has_more;) {
        serialized.clear();
        has_more = serializer.Serialize(&serialized);
        response->add_result()->ParseFromArray(
            serialized.data(), static_cast<int>(serialized.size()));
      }
      response->set_trace(args->trace());
      return grpc::Status::OK;
    }
  }
}

}  // namespace perfetto::bigtrace
