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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_ETM_ETM_TRACKER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_ETM_ETM_TRACKER_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "perfetto/base/flat_set.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/tables/etm_tables_py.h"
#include "src/trace_processor/types/destructible.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor::etm {

class Configuration;

using PerCpuConfiguration =
    base::FlatHashMap<uint32_t, std::unique_ptr<Configuration>>;

class EtmTracker : public Destructible {
 public:
  explicit EtmTracker(TraceProcessorContext* context);
  ~EtmTracker() override;

  base::Status Finalize();

  void AddSessionData(tables::EtmV4SessionTable::Id session_id,
                      std::vector<TraceBlobView> chunks);

  base::FlatSet<tables::EtmV4ConfigurationTable::Id> InsertEtmV4Config(
      PerCpuConfiguration per_cpu_configs);

 private:
  TraceProcessorContext* context_;
};

}  // namespace perfetto::trace_processor::etm

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_ETM_ETM_TRACKER_H_
