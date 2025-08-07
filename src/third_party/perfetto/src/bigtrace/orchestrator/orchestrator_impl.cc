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

#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

#include <grpcpp/client_context.h>
#include <grpcpp/support/status.h>

#include "perfetto/base/logging.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/utils.h"
#include "protos/perfetto/bigtrace/orchestrator.pb.h"
#include "src/bigtrace/orchestrator/orchestrator_impl.h"
#include "src/bigtrace/orchestrator/resizable_task_pool.h"
#include "src/bigtrace/orchestrator/trace_address_pool.h"

namespace perfetto::bigtrace {
namespace {
const uint32_t kBufferPushDelayMicroseconds = 100;

grpc::Status ExecuteQueryOnTrace(
    std::string sql_query,
    std::string trace,
    grpc::Status& query_status,
    std::mutex& worker_lock,
    std::vector<protos::BigtraceQueryResponse>& response_buffer,
    std::unique_ptr<protos::BigtraceWorker::Stub>& stub,
    ThreadWithContext* contextual_thread) {
  protos::BigtraceQueryTraceArgs trace_args;
  protos::BigtraceQueryTraceResponse trace_response;

  trace_args.set_sql_query(sql_query);
  trace_args.set_trace(trace);
  grpc::Status status = stub->QueryTrace(
      contextual_thread->client_context.get(), trace_args, &trace_response);

  if (!status.ok()) {
    {
      std::lock_guard<std::mutex> status_guard(worker_lock);
      // We check and only update the query status if it was not already errored
      // to avoid unnecessary updates.
      if (query_status.ok()) {
        query_status = status;
      }
    }

    return status;
  }

  protos::BigtraceQueryResponse response;
  response.set_trace(trace_response.trace());
  for (const protos::QueryResult& query_result : trace_response.result()) {
    response.add_result()->CopyFrom(query_result);
    if (query_result.has_error()) {
      // TODO(b/366410502) Add a mode of operation where some traces are allowed
      // to be dropped and a corresponding message is displayed to the user
      // alongside partial results
      std::lock_guard<std::mutex> status_guard(worker_lock);
      query_status = grpc::Status(grpc::StatusCode::INTERNAL,
                                  "[" + trace + "]: " + query_result.error());
      break;
    }
  }
  std::lock_guard<std::mutex> buffer_guard(worker_lock);
  response_buffer.emplace_back(std::move(response));

  return grpc::Status::OK;
}

void ThreadRunLoop(ThreadWithContext* contextual_thread,
                   TraceAddressPool& address_pool,
                   std::string sql_query,
                   grpc::Status& query_status,
                   std::mutex& worker_lock,
                   std::vector<protos::BigtraceQueryResponse>& response_buffer,
                   std::unique_ptr<protos::BigtraceWorker::Stub>& stub) {
  for (;;) {
    auto maybe_trace_address = address_pool.Pop();
    if (!maybe_trace_address) {
      return;
    }

    // The ordering of this context swap followed by the check on thread
    // cancellation is essential and should not be changed to avoid a race where
    // a request to cancel a thread is sent, followed by a context swap, causing
    // the cancel to not be caught and the execution of the loop body to
    // continue.
    contextual_thread->client_context = std::make_unique<grpc::ClientContext>();

    if (contextual_thread->IsCancelled()) {
      address_pool.MarkCancelled(std::move(*maybe_trace_address));
      return;
    }

    grpc::Status status = ExecuteQueryOnTrace(
        sql_query, *maybe_trace_address, query_status, worker_lock,
        response_buffer, stub, contextual_thread);

    if (!status.ok()) {
      if (status.error_code() == grpc::StatusCode::CANCELLED) {
        address_pool.MarkCancelled(std::move(*maybe_trace_address));
      }
      return;
    }
  }
}

}  // namespace

OrchestratorImpl::OrchestratorImpl(
    std::unique_ptr<protos::BigtraceWorker::Stub> stub,
    uint32_t max_query_concurrency)
    : stub_(std::move(stub)), max_query_concurrency_(max_query_concurrency) {}

grpc::Status OrchestratorImpl::Query(
    grpc::ServerContext*,
    const protos::BigtraceQueryArgs* args,
    grpc::ServerWriter<protos::BigtraceQueryResponse>* writer) {
  grpc::Status query_status;
  std::mutex worker_lock;
  const std::string& sql_query = args->sql_query();
  std::vector<std::string> traces(args->traces().begin(), args->traces().end());

  std::vector<protos::BigtraceQueryResponse> response_buffer;
  uint64_t trace_count = static_cast<uint64_t>(args->traces_size());

  TraceAddressPool address_pool(std::move(traces));

  // Update the query count on start and end ensuring that the query count is
  // always decremented whenever the function is exited.
  {
    std::lock_guard<std::mutex> lk(query_count_mutex_);
    query_count_++;
  }
  auto query_count_decrement = base::OnScopeExit([&]() {
    std::lock_guard<std::mutex> lk(query_count_mutex_);
    query_count_--;
  });

  ResizableTaskPool task_pool([&](ThreadWithContext* new_contextual_thread) {
    ThreadRunLoop(new_contextual_thread, address_pool, sql_query, query_status,
                  worker_lock, response_buffer, stub_);
  });

  uint64_t pushed_response_count = 0;
  uint32_t last_query_count = 0;
  uint32_t current_query_count = 0;

  for (;;) {
    {
      std::lock_guard<std::mutex> lk(query_count_mutex_);
      current_query_count = query_count_;
    }

    PERFETTO_CHECK(current_query_count != 0);

    // Update the number of threads to the lower of {the remaining number of
    // traces} and the {maximum concurrency divided by the number of active
    // queries}. This ensures that at most |max_query_concurrency_| calls to the
    // backend are outstanding at any one point.
    if (last_query_count != current_query_count) {
      auto new_size =
          std::min(std::max<uint32_t>(address_pool.RemainingCount(), 1u),
                   max_query_concurrency_ / current_query_count);
      task_pool.Resize(new_size);
      last_query_count = current_query_count;
    }

    // Exit the loop when either all responses have been successfully completed
    // or if there is an error.
    {
      std::lock_guard<std::mutex> status_guard(worker_lock);
      if (pushed_response_count == trace_count || !query_status.ok()) {
        break;
      }
    }

    // A buffer is used to periodically make writes to the client instead of
    // writing every individual response in order to reduce contention on the
    // writer.
    base::SleepMicroseconds(kBufferPushDelayMicroseconds);
    if (response_buffer.empty()) {
      continue;
    }
    std::vector<protos::BigtraceQueryResponse> buffer;
    {
      std::lock_guard<std::mutex> buffer_guard(worker_lock);
      buffer = std::move(response_buffer);
      response_buffer.clear();
    }
    for (protos::BigtraceQueryResponse& response : buffer) {
      writer->Write(std::move(response));
    }
    pushed_response_count += buffer.size();
  }

  task_pool.JoinAll();

  return query_status;
}

}  // namespace perfetto::bigtrace
