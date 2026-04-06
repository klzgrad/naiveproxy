/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "src/perfetto_cmd/perfetto_cmd.h"

#include <sys/sendfile.h>

#include <cinttypes>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_mmap.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/uuid.h"
#include "perfetto/protozero/proto_decoder.h"
#include "perfetto/tracing/core/forward_decls.h"
#include "src/android_internal/incident_service.h"
#include "src/android_internal/lazy_library_loader.h"
#include "src/android_internal/tracing_service_proxy.h"
#include "src/android_stats/statsd_logging_helper.h"

#include "protos/perfetto/config/trace_config.gen.h"

#include "protos/perfetto/trace/trace.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {
namespace {

// traced runs as 'user nobody' (AID_NOBODY), defined here:
// https://cs.android.com/android/platform/superproject/+/android-latest-release:system/core/libcutils/include/private/android_filesystem_config.h;l=203;drc=f5b540e2b7b9b325d99486d49c0ac57bdd0c5344
// We only trust packages written by traced.
static constexpr int32_t kTrustedUid = 9999;

// Directory for local state and temporary files. This is automatically
// created by the system by setting setprop persist.traced.enable=1.
const char* kStateDir = "/data/misc/perfetto-traces";

constexpr int64_t kSendfileTimeoutNs = 10UL * 1000 * 1000 * 1000;  // 10s

}  // namespace

void PerfettoCmd::SaveTraceIntoIncidentOrCrash() {
  PERFETTO_CHECK(save_to_incidentd_);

  const auto& cfg = trace_config_->incident_report_config();
  PERFETTO_CHECK(!cfg.destination_package().empty());
  PERFETTO_CHECK(!cfg.skip_incidentd());

  uint64_t bytes_written = GetBytesWritten();
  if (bytes_written == 0) {
    LogUploadEvent(PerfettoStatsdAtom::kNotUploadingEmptyTrace);
    PERFETTO_LOG("Skipping write to incident. Empty trace.");
    return;
  }

  // Save the trace as an incident.
  SaveOutputToIncidentTraceOrCrash();

  // Skip the trace-uuid link for traces that are too small. Realistically those
  // traces contain only a marker (e.g. seized_for_bugreport, or the trace
  // expired without triggers). Those are useless and introduce only noise.
  if (bytes_written > 4096) {
    base::Uuid uuid(uuid_);
    PERFETTO_LOG("go/trace-uuid/%s name=\"%s\" size=%" PRIu64,
                 uuid.ToPrettyString().c_str(),
                 trace_config_->unique_session_name().c_str(), bytes_written);
  }

  // Ask incidentd to create a report, which will read the file we just
  // wrote.
  PERFETTO_LAZY_LOAD(android_internal::StartIncidentReport, incident_fn);
  PERFETTO_CHECK(incident_fn(cfg.destination_package().c_str(),
                             cfg.destination_class().c_str(),
                             cfg.privacy_level()));
}

// static
base::Status PerfettoCmd::ReportTraceToAndroidFramework(
    int trace_fd,
    uint64_t trace_size,
    const base::Uuid& uuid,
    const std::string& unique_session_name,
    const protos::gen::TraceConfig_AndroidReportConfig& report_config,
    bool statsd_logging) {
  auto log_upload_event_fn = [statsd_logging, &uuid](PerfettoStatsdAtom atom) {
    if (statsd_logging) {
      android_stats::MaybeLogUploadEvent(atom, uuid.lsb(), uuid.msb());
    }
  };

  if (report_config.reporter_service_class().empty() ||
      report_config.reporter_service_package().empty()) {
    return base::ErrStatus("Invalid 'android_report_config'");
  }
  if (report_config.skip_report()) {
    return base::ErrStatus("'android_report_config.skip_report' is true.");
  }

  if (trace_size == 0) {
    log_upload_event_fn(PerfettoStatsdAtom::kCmdFwReportEmptyTrace);
    PERFETTO_LOG("Skipping reporting trace to Android. Empty trace.");
    return base::OkStatus();
  }

  log_upload_event_fn(PerfettoStatsdAtom::kCmdFwReportBegin);
  base::StackString<128> self_fd("/proc/self/fd/%d", trace_fd);
  base::ScopedFile fd(base::OpenFile(self_fd.c_str(), O_RDONLY | O_CLOEXEC));
  if (!fd) {
    return base::ErrStatus(
        "Failed to dup fd when reporting to Android: %s (errno: %d)",
        strerror(errno), errno);
  }

  PERFETTO_LAZY_LOAD(android_internal::ReportTrace, report_fn);
  bool report_ok = report_fn(report_config.reporter_service_package().c_str(),
                             report_config.reporter_service_class().c_str(),
                             fd.release(), uuid.lsb(), uuid.msb(),
                             report_config.use_pipe_in_framework_for_testing());

  if (!report_ok) {
    return base::ErrStatus("Failed in 'android_internal::ReportTrace'");
  }

  // Skip the trace-uuid link for traces that are too small. Realistically those
  // traces contain only a marker (e.g. seized_for_bugreport, or the trace
  // expired without triggers). Those are useless and introduce only noise.
  if (trace_size > 4096) {
    PERFETTO_LOG("go/trace-uuid/%s name=\"%s\" size=%" PRIu64,
                 uuid.ToPrettyString().c_str(), unique_session_name.c_str(),
                 trace_size);
  }
  log_upload_event_fn(PerfettoStatsdAtom::kCmdFwReportHandoff);
  return base::OkStatus();
}

void PerfettoCmd::ReportTraceToAndroidFrameworkOrCrash() {
  PERFETTO_CHECK(trace_out_stream_);
  uint64_t bytes_written = GetBytesWritten();
  int trace_fd = fileno(*trace_out_stream_);
  base::Uuid uuid(uuid_);
  base::Status status = ReportTraceToAndroidFramework(
      trace_fd, bytes_written, uuid, trace_config_->unique_session_name(),
      trace_config_->android_report_config(), statsd_logging_);
  if (!status.ok()) {
    PERFETTO_FATAL("ReportTraceToAndroidFramework: %s", status.c_message());
  }
}

// Open a staging file (unlinking the previous instance), copy the trace
// contents over, then rename to a final hardcoded path (known to incidentd).
// Such tracing sessions should not normally overlap. We do not use unique
// unique filenames to avoid creating an unbounded amount of files in case of
// errors.
void PerfettoCmd::SaveOutputToIncidentTraceOrCrash() {
  LogUploadEvent(PerfettoStatsdAtom::kUploadIncidentBegin);
  base::StackString<256> kIncidentTracePath("%s/incident-trace", kStateDir);

  base::StackString<256> kTempIncidentTracePath("%s.temp",
                                                kIncidentTracePath.c_str());

  PERFETTO_CHECK(unlink(kTempIncidentTracePath.c_str()) == 0 ||
                 errno == ENOENT);

  // TODO(b/155024256) These should not be necessary (we flush when destroying
  // packet writer and sendfile should ignore file offset) however they should
  // not harm anything and it will help debug the linked issue.
  PERFETTO_CHECK(fflush(*trace_out_stream_) == 0);
  PERFETTO_CHECK(fseek(*trace_out_stream_, 0, SEEK_SET) == 0);

  // SELinux constrains the set of readers.
  base::ScopedFile staging_fd = base::OpenFile(kTempIncidentTracePath.c_str(),
                                               O_CREAT | O_EXCL | O_RDWR, 0666);
  PERFETTO_CHECK(staging_fd);

  uint64_t bytes_written = GetBytesWritten();
  int fd = fileno(*trace_out_stream_);
  off_t offset = 0;
  size_t remaining = static_cast<size_t>(bytes_written);

  // Count time in terms of CPU to avoid timeouts due to suspend:
  base::TimeNanos start = base::GetThreadCPUTimeNs();
  for (;;) {
    errno = 0;
    PERFETTO_DCHECK(static_cast<size_t>(offset) + remaining == bytes_written);
    auto wsize = PERFETTO_EINTR(sendfile(*staging_fd, fd, &offset, remaining));
    if (wsize < 0) {
      PERFETTO_FATAL("sendfile() failed wsize=%zd, off=%" PRId64
                     ", initial=%" PRIu64 ", remaining=%zu",
                     wsize, static_cast<int64_t>(offset), bytes_written,
                     remaining);
    }
    remaining -= static_cast<size_t>(wsize);
    if (remaining == 0) {
      break;
    }
    base::TimeNanos now = base::GetThreadCPUTimeNs();
    if (now < start || (now - start).count() > kSendfileTimeoutNs) {
      PERFETTO_FATAL("sendfile() timed out wsize=%zd, off=%" PRId64
                     ", initial=%" PRIu64
                     ", remaining=%zu, start=%lld, now=%lld",
                     wsize, static_cast<int64_t>(offset), bytes_written,
                     remaining, static_cast<long long int>(start.count()),
                     static_cast<long long int>(now.count()));
    }
  }

  staging_fd.reset();
  PERFETTO_CHECK(
      rename(kTempIncidentTracePath.c_str(), kIncidentTracePath.c_str()) == 0);
  // Note: not calling fsync(2), as we're not interested in the file being
  // consistent in case of a crash.
  LogUploadEvent(PerfettoStatsdAtom::kUploadIncidentSuccess);
}

// static
base::ScopedFile PerfettoCmd::CreateUnlinkedTmpFile() {
  // If we are tracing to DropBox, there's no need to make a
  // filesystem-visible temporary file.
  auto fd = base::OpenFile(kStateDir, O_TMPFILE | O_RDWR, 0600);
  if (!fd)
    PERFETTO_PLOG("Could not create a temporary trace file in %s", kStateDir);
  return fd;
}

// static
std::optional<TraceConfig> PerfettoCmd::ParseTraceConfigFromMmapedTrace(
    base::ScopedMmap mmapped_trace) {
  PERFETTO_CHECK(mmapped_trace.IsValid());

  protozero::ProtoDecoder trace_decoder(mmapped_trace.data(),
                                        mmapped_trace.length());

  for (auto packet = trace_decoder.ReadField(); packet;
       packet = trace_decoder.ReadField()) {
    if (packet.id() != protos::pbzero::Trace::kPacketFieldNumber ||
        packet.type() !=
            protozero::proto_utils::ProtoWireType::kLengthDelimited) {
      return std::nullopt;
    }

    protozero::ProtoDecoder packet_decoder(packet.as_bytes());

    auto trace_config_field = packet_decoder.FindField(
        protos::pbzero::TracePacket::kTraceConfigFieldNumber);
    if (!trace_config_field)
      continue;

    auto trusted_uid_field = packet_decoder.FindField(
        protos::pbzero::TracePacket::kTrustedUidFieldNumber);
    if (!trusted_uid_field)
      continue;

    int32_t uid_value = trusted_uid_field.as_int32();

    if (uid_value != kTrustedUid)
      continue;

    TraceConfig trace_config;
    if (trace_config.ParseFromArray(trace_config_field.data(),
                                    trace_config_field.size())) {
      return trace_config;
    }
  }

  return std::nullopt;
}

}  // namespace perfetto
