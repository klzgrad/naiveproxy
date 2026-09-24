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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_LINUX_PROBES_PARSER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_LINUX_PROBES_PARSER_H_

#include "perfetto/protozero/field.h"
#include "src/trace_processor/containers/string_pool.h"

namespace perfetto::trace_processor {

class TraceProcessorContext;

class LinuxProbesParser {
 public:
  explicit LinuxProbesParser(TraceProcessorContext* context);

  void ParseSystemdJournaldEvent(int64_t ts, protozero::ConstBytes blob);

 private:
  TraceProcessorContext* context_;

  // Interned string ids for arg keys used when writing journald-specific
  // metadata into the args table.
  using StringId = StringPool::Id;
  StringId uid_key_id_;
  StringId comm_key_id_;
  StringId systemd_unit_key_id_;
  StringId hostname_key_id_;
  StringId transport_key_id_;
  StringId journald_source_id_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_LINUX_PROBES_PARSER_H_
