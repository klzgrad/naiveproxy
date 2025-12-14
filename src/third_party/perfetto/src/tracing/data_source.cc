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

#include "perfetto/tracing/data_source.h"
#include "perfetto/base/logging.h"
#include "perfetto/tracing/buffer_exhausted_policy.h"
#include "protos/perfetto/config/data_source_config.gen.h"

namespace perfetto {

DataSourceBase::StopArgs::~StopArgs() = default;
DataSourceBase::FlushArgs::~FlushArgs() = default;
DataSourceBase::~DataSourceBase() = default;
void DataSourceBase::OnSetup(const SetupArgs&) {}
void DataSourceBase::OnStart(const StartArgs&) {}
void DataSourceBase::OnStop(const StopArgs&) {}
void DataSourceBase::WillClearIncrementalState(
    const ClearIncrementalStateArgs&) {}
void DataSourceBase::OnFlush(const FlushArgs&) {}

bool DataSourceBase::CanAdoptStartupSession(
    const DataSourceConfig& startup_config,
    const DataSourceConfig& service_config) {
  // Clear target buffer and tracing-service provided fields for comparison of
  // configs for startup tracing, since these fields are not available when
  // setting up data sources for startup tracing.
  DataSourceConfig startup_config_stripped = startup_config;
  DataSourceConfig service_config_stripped = service_config;

  startup_config_stripped.set_target_buffer(0);
  startup_config_stripped.set_tracing_session_id(0);
  startup_config_stripped.set_session_initiator(
      DataSourceConfig::SESSION_INITIATOR_UNSPECIFIED);
  startup_config_stripped.set_trace_duration_ms(0);
  startup_config_stripped.set_stop_timeout_ms(0);
  startup_config_stripped.set_enable_extra_guardrails(false);

  service_config_stripped.set_target_buffer(0);
  service_config_stripped.set_tracing_session_id(0);
  service_config_stripped.set_session_initiator(
      DataSourceConfig::SESSION_INITIATOR_UNSPECIFIED);
  service_config_stripped.set_trace_duration_ms(0);
  service_config_stripped.set_stop_timeout_ms(0);
  service_config_stripped.set_enable_extra_guardrails(false);

  return startup_config_stripped == service_config_stripped;
}

namespace internal {

void DataSourceType::PopulateTlsInst(
    DataSourceInstanceThreadLocalState* tls_inst,
    DataSourceState* instance_state,
    uint32_t instance_index) {
  auto* tracing_impl = TracingMuxer::Get();
  tls_inst->muxer_id_for_testing = instance_state->muxer_id_for_testing;
  tls_inst->backend_id = instance_state->backend_id;
  tls_inst->backend_connection_id = instance_state->backend_connection_id;
  tls_inst->buffer_id = instance_state->buffer_id;
  tls_inst->startup_target_buffer_reservation =
      instance_state->startup_target_buffer_reservation.load(
          std::memory_order_relaxed);
  tls_inst->data_source_instance_id = instance_state->data_source_instance_id;
  tls_inst->is_intercepted = instance_state->interceptor_id != 0;
  tls_inst->trace_writer =
      tracing_impl->CreateTraceWriter(&state_, instance_index, instance_state,
                                      instance_state->buffer_exhausted_policy);
  if (create_incremental_state_fn_) {
    PERFETTO_DCHECK(!tls_inst->incremental_state);
    CreateIncrementalState(tls_inst, instance_index);
  }
  if (create_custom_tls_fn_) {
    tls_inst->data_source_custom_tls =
        create_custom_tls_fn_(tls_inst, instance_index, user_arg_);
  }
  // Even in the case of out-of-IDs, SharedMemoryArbiterImpl returns a
  // NullTraceWriter. The returned pointer should never be null.
  PERFETTO_DCHECK(tls_inst->trace_writer);
}

}  // namespace internal
}  // namespace perfetto
