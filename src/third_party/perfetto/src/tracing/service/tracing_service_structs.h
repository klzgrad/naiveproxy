/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef SRC_TRACING_SERVICE_TRACING_SERVICE_STRUCTS_H_
#define SRC_TRACING_SERVICE_TRACING_SERVICE_STRUCTS_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/sys_types.h"
#include "perfetto/ext/base/weak_ptr.h"
#include "perfetto/ext/tracing/core/basic_types.h"
#include "perfetto/ext/tracing/core/tracing_service.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "perfetto/tracing/core/data_source_descriptor.h"

// This file contains common structs that are part of TracingServiceImpl and are
// used across its various parts (e.g. tracing_service_session.cc,
// tracing_service_endpoints_impl.cc). They exist in this dedicated header to
// prevent dependency cycles and for better readability.

namespace perfetto {

class TraceBuffer;

namespace tracing_service {

class ConsumerEndpointImpl;

struct TriggerInfo {
  uint64_t boot_time_ns = 0;
  std::string trigger_name;
  std::string producer_name;
  uid_t producer_uid = 0;
  uint64_t trigger_delay_ms = 0;
};

struct PendingClone {
  size_t pending_flush_cnt = 0;
  // This vector might not be populated all at once. Some buffers might be
  // nullptr while flushing is not done.
  std::vector<std::unique_ptr<TraceBuffer>> buffers;
  std::vector<int64_t> buffer_cloned_timestamps;
  bool flush_failed = false;
  base::WeakPtr<ConsumerEndpointImpl> weak_consumer;
  bool skip_trace_filter = false;
  std::optional<TriggerInfo> clone_trigger;
  int64_t clone_started_timestamp_ns = 0;
  base::ScopedFile output_file_fd;
};

struct PendingFlush {
  std::set<ProducerID> producers;
  ConsumerEndpoint::FlushCallback callback;
  explicit PendingFlush(decltype(callback) cb) : callback(std::move(cb)) {}
};

struct TriggerHistory {
  int64_t timestamp_ns;
  uint64_t name_hash;

  bool operator<(const TriggerHistory& other) const {
    return timestamp_ns < other.timestamp_ns;
  }
};

struct RegisteredDataSource {
  ProducerID producer_id;
  DataSourceDescriptor descriptor;
};

// Represents an active data source for a tracing session.
struct DataSourceInstance {
  DataSourceInstance(DataSourceInstanceID id,
                     const DataSourceConfig& cfg,
                     const std::string& ds_name,
                     bool notify_on_start,
                     bool notify_on_stop,
                     bool handles_incremental_state_invalidation,
                     bool no_flush_)
      : instance_id(id),
        config(cfg),
        data_source_name(ds_name),
        will_notify_on_start(notify_on_start),
        will_notify_on_stop(notify_on_stop),
        handles_incremental_state_clear(handles_incremental_state_invalidation),
        no_flush(no_flush_) {}
  DataSourceInstance(const DataSourceInstance&) = delete;
  DataSourceInstance& operator=(const DataSourceInstance&) = delete;

  DataSourceInstanceID instance_id;
  DataSourceConfig config;
  std::string data_source_name;
  bool will_notify_on_start;
  bool will_notify_on_stop;
  bool handles_incremental_state_clear;
  bool no_flush;

  enum DataSourceInstanceState {
    CONFIGURED,
    STARTING,
    STARTED,
    STOPPING,
    STOPPED
  };
  DataSourceInstanceState state = CONFIGURED;
};

}  // namespace tracing_service
}  // namespace perfetto

#endif  // SRC_TRACING_SERVICE_TRACING_SERVICE_STRUCTS_H_
