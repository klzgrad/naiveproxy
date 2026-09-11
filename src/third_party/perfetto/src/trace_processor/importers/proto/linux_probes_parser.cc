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

#include "src/trace_processor/importers/proto/linux_probes_parser.h"

#include <cstdint>
#include <optional>

#include "perfetto/ext/base/string_view.h"
#include "protos/perfetto/trace/linux/journald_event.pbzero.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/stats_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/log_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {

LinuxProbesParser::LinuxProbesParser(TraceProcessorContext* context)
    : context_(context),
      uid_key_id_(context->storage->InternString("uid")),
      comm_key_id_(context->storage->InternString("comm")),
      systemd_unit_key_id_(context->storage->InternString("systemd_unit")),
      hostname_key_id_(context->storage->InternString("hostname")),
      transport_key_id_(context->storage->InternString("transport")),
      journald_source_id_(context->storage->InternString("systemd_journald")) {}

void LinuxProbesParser::ParseSystemdJournaldEvent(int64_t ts,
                                                  protozero::ConstBytes blob) {
  protos::pbzero::SystemdJournaldEvent::Decoder evt(blob);

  // Flushes emit statistics in a dedicated journald event packet.
  const bool is_stats = evt.has_num_failed() || evt.has_num_total();
  if (evt.has_num_failed()) {
    context_->stats_tracker->SetStats(stats::systemd_journal_num_failed,
                                      static_cast<int64_t>(evt.num_failed()));
  }
  if (evt.has_num_total()) {
    context_->stats_tracker->SetStats(stats::systemd_journal_num_total,
                                      static_cast<int64_t>(evt.num_total()));
  }
  if (is_stats)
    return;

  auto pid = evt.has_pid() ? static_cast<uint32_t>(evt.pid()) : 0u;
  auto tid = evt.has_tid() ? static_cast<uint32_t>(evt.tid()) : pid;

  std::optional<uint32_t> utid;
  if (pid != 0) {
    utid = context_->process_tracker->UpdateThread(tid, pid);
  }

  // If no priority given, default to 0 (Unspecified in Android logcat,
  // Emergency in journald)
  uint32_t prio = 0u;
  if (evt.has_prio()) {
    prio = evt.prio();
  }

  // If no message given, default to empty string
  StringId msg_id;
  if (evt.has_message()) {
    msg_id = context_->storage->InternString(evt.message());
  } else {
    msg_id = context_->storage->InternString(base::StringView());
  }

  std::optional<uint32_t> uid;
  if (evt.has_uid())
    uid = static_cast<uint32_t>(evt.uid());

  std::optional<StringId> comm_id;
  if (evt.has_comm())
    comm_id = context_->storage->InternString(evt.comm());

  std::optional<StringId> unit_id;
  if (evt.has_systemd_unit())
    unit_id = context_->storage->InternString(evt.systemd_unit());

  std::optional<StringId> tag_id;
  if (evt.has_tag())
    tag_id = context_->storage->InternString(evt.tag());

  std::optional<StringId> hostname_id;
  if (evt.has_hostname())
    hostname_id = context_->storage->InternString(evt.hostname());

  std::optional<StringId> transport_id;
  if (evt.has_transport())
    transport_id = context_->storage->InternString(evt.transport());

  tables::LogTable::Row row;
  row.ts = ts;
  row.utid = utid;
  row.prio = prio;
  row.log_source = journald_source_id_;
  row.tag = tag_id;
  row.msg = msg_id;
  auto id = context_->storage->mutable_log_table()->Insert(row).id;

  if (uid || comm_id || unit_id || hostname_id || transport_id) {
    ArgsTracker args_tracker(context_);
    auto inserter = args_tracker.AddArgsTo(id);
    if (uid) {
      inserter.AddArg(uid_key_id_,
                      Variadic::Integer(static_cast<int64_t>(*uid)));
    }
    if (comm_id) {
      inserter.AddArg(comm_key_id_, Variadic::String(*comm_id));
    }
    if (unit_id) {
      inserter.AddArg(systemd_unit_key_id_, Variadic::String(*unit_id));
    }
    if (hostname_id) {
      inserter.AddArg(hostname_key_id_, Variadic::String(*hostname_id));
    }
    if (transport_id) {
      inserter.AddArg(transport_key_id_, Variadic::String(*transport_id));
    }
  }
}

}  // namespace perfetto::trace_processor
