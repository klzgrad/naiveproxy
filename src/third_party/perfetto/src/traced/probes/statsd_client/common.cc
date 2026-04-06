/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "src/traced/probes/statsd_client/common.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "perfetto/tracing/core/data_source_config.h"

#include "protos/perfetto/config/statsd/statsd_tracing_config.pbzero.h"
#include "protos/perfetto/trace/statsd/statsd_atom.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "protos/third_party/statsd/shell_config.pbzero.h"

using ::perfetto::protos::pbzero::StatsdPullAtomConfig;
using ::perfetto::protos::pbzero::StatsdShellSubscription;
using ::perfetto::protos::pbzero::StatsdTracingConfig;

namespace perfetto {
namespace {

void AddPullAtoms(const StatsdPullAtomConfig::Decoder& cfg,
                  protozero::RepeatedFieldIterator<int32_t> it,
                  StatsdShellSubscription* msg) {
  constexpr int32_t kDefaultPullFreqMs = 5000;
  int32_t pull_freq_ms = kDefaultPullFreqMs;
  if (cfg.has_pull_frequency_ms()) {
    pull_freq_ms = cfg.pull_frequency_ms();
  }

  for (; it; ++it) {
    auto* pulled_msg = msg->add_pulled();
    pulled_msg->set_freq_millis(pull_freq_ms);

    for (auto package = cfg.packages(); package; ++package) {
      pulled_msg->add_packages(*package);
    }

    auto* matcher_msg = pulled_msg->set_matcher();
    matcher_msg->set_atom_id(*it);
  }
}

void AddPushAtoms(protozero::RepeatedFieldIterator<int32_t> it,
                  StatsdShellSubscription* msg) {
  for (; it; ++it) {
    auto* matcher_msg = msg->add_pushed();
    matcher_msg->set_atom_id(*it);
  }
}

}  // namespace

std::string CreateStatsdShellConfig(const DataSourceConfig& config) {
  StatsdTracingConfig::Decoder cfg(config.statsd_tracing_config_raw());
  protozero::HeapBuffered<StatsdShellSubscription> msg;
  for (auto pull_it = cfg.pull_config(); pull_it; ++pull_it) {
    StatsdPullAtomConfig::Decoder pull_cfg(*pull_it);
    AddPullAtoms(pull_cfg, pull_cfg.raw_pull_atom_id(), msg.get());
    AddPullAtoms(pull_cfg, pull_cfg.pull_atom_id(), msg.get());
  }
  AddPushAtoms(cfg.push_atom_id(), msg.get());
  AddPushAtoms(cfg.raw_push_atom_id(), msg.get());
  return msg.SerializeAsString();
}

}  // namespace perfetto
