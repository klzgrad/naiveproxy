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

#include "src/tracing/service/tracing_service_session.h"
#include "protos/perfetto/trace/perfetto/tracing_service_event.pbzero.h"
#include "src/protozero/filtering/message_filter.h"
#include "src/tracing/service/trace_buffer.h"
#include "src/tracing/service/tracing_service_endpoints_impl.h"

namespace perfetto::tracing_service {

TracingSession::TracingSession(TracingSessionID session_id,
                               ConsumerEndpointImpl* consumer,
                               const TraceConfig& new_config,
                               base::TaskRunner* task_runner)
    : id(session_id),
      consumer_maybe_null(consumer),
      consumer_uid(consumer->uid_),
      config(new_config),
      snapshot_periodic_task(task_runner),
      timed_stop_task(task_runner) {
  // all_data_sources_flushed (and flush_started) is special because we store up
  // to 64 events of this type. Other events will go through the default case in
  // SnapshotLifecycleEvent() where they will be given a max history of 1.
  lifecycle_events.emplace_back(
      protos::pbzero::TracingServiceEvent::kAllDataSourcesFlushedFieldNumber,
      64 /* max_size */);
  lifecycle_events.emplace_back(
      protos::pbzero::TracingServiceEvent::kFlushStartedFieldNumber,
      64 /* max_size */);
}

bool TracingSession::IsCloneAllowed(uid_t clone_uid) const {
  if (clone_uid == 0)
    return true;  // Root is always allowed to clone everything.
  if (clone_uid == this->consumer_uid)
    return true;  // Allow cloning if the uids match.
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  // On Android allow shell to clone sessions marked as exported for bugreport.
  // Dumpstate (invoked by adb bugreport) invokes commands as shell.
  if (clone_uid == AID_SHELL && this->config.bugreport_score() > 0)
    return true;
#endif
  return false;
}

PacketSequenceID TracingSession::GetPacketSequenceID(MachineID machine_id,
                                                     ProducerID producer_id,
                                                     WriterID writer_id) {
  auto key = std::make_tuple(machine_id, producer_id, writer_id);
  auto it = packet_sequence_ids.find(key);
  if (it != packet_sequence_ids.end())
    return it->second;
  // We shouldn't run out of sequence IDs (producer ID is 16 bit, writer IDs
  // are limited to 1024).
  static_assert(kMaxPacketSequenceID > kMaxProducerID * kMaxWriterID,
                "PacketSequenceID value space doesn't cover service "
                "sequence ID and all producer/writer ID combinations!");
  PERFETTO_DCHECK(last_packet_sequence_id < kMaxPacketSequenceID);
  PacketSequenceID sequence_id = ++last_packet_sequence_id;
  packet_sequence_ids[key] = sequence_id;
  return sequence_id;
}

DataSourceInstance* TracingSession::GetDataSourceInstance(
    ProducerID producer_id,
    DataSourceInstanceID instance_id) {
  for (auto& inst_kv : data_source_instances) {
    if (inst_kv.first != producer_id ||
        inst_kv.second.instance_id != instance_id) {
      continue;
    }
    return &inst_kv.second;
  }
  return nullptr;
}

bool TracingSession::AllDataSourceInstancesStarted() {
  return std::all_of(data_source_instances.begin(), data_source_instances.end(),
                     [](decltype(data_source_instances)::const_reference x) {
                       return x.second.state == DataSourceInstance::STARTED;
                     });
}

bool TracingSession::AllDataSourceInstancesStopped() {
  return std::all_of(data_source_instances.begin(), data_source_instances.end(),
                     [](decltype(data_source_instances)::const_reference x) {
                       return x.second.state == DataSourceInstance::STOPPED;
                     });
}

}  // namespace perfetto::tracing_service
