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

#include "src/bigtrace/orchestrator/trace_address_pool.h"
#include "perfetto/base/logging.h"

namespace perfetto::bigtrace {

TraceAddressPool::TraceAddressPool(
    const std::vector<std::string>& trace_addresses)
    : trace_addresses_(trace_addresses) {}

// Pops a trace address from the pool, blocking if necessary
//
// Returns a nullopt if the pool is empty
std::optional<std::string> TraceAddressPool::Pop() {
  std::lock_guard<std::mutex> trace_addresses_guard(trace_addresses_lock_);
  if (trace_addresses_.size() == 0) {
    return std::nullopt;
  }
  std::string trace_address = trace_addresses_.back();
  trace_addresses_.pop_back();
  running_queries_++;
  return trace_address;
}

// Marks a trace address as cancelled
//
// Returns cancelled trace addresses to the pool for future calls to |Pop|
void TraceAddressPool::MarkCancelled(std::string trace_address) {
  std::lock_guard<std::mutex> guard(trace_addresses_lock_);
  PERFETTO_CHECK(running_queries_-- > 0);
  trace_addresses_.push_back(std::move(trace_address));
}

// Returns the number of remaining trace addresses which require processing
uint32_t TraceAddressPool::RemainingCount() {
  std::lock_guard<std::mutex> guard(trace_addresses_lock_);
  return static_cast<uint32_t>(trace_addresses_.size()) + running_queries_;
}

}  // namespace perfetto::bigtrace
