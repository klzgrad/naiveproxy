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

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/uuid.h"
#include "perfetto/tracing/core/trace_config.h"
#include "src/android_internal/incident_service.h"
#include "src/android_internal/lazy_library_loader.h"
#include "src/android_internal/tracing_service_proxy.h"

namespace perfetto {
namespace {

constexpr int64_t kSendfileTimeoutNs = 10UL * 1000 * 1000 * 1000;  // 10s

}  // namespace

void PerfettoCmd::SaveTraceIntoIncidentOrCrash() {
  PERFETTO_CHECK(save_to_incidentd_);

  const auto& cfg = trace_config_->incident_report_config();
  PERFETTO_CHECK(!cfg.destination_package().empty());
  PERFETTO_CHECK(!cfg.skip_incidentd());

  if (bytes_written_ == 0) {
    LogUploadEvent(PerfettoStatsdAtom::kNotUploadingEmptyTrace);
    PERFETTO_LOG("Skipping write to incident. Empty trace.");
    return;
  }

  // Save the trace as an incident.
  SaveOutputToIncidentTraceOrCrash();

  // Skip the trace-uuid link for traces that are too small. Realistically those
  // traces contain only a marker (e.g. seized_for_bugreport, or the trace
  // expired without triggers). Those are useless and introduce only noise.
  if (bytes_written_ > 4096) {
    base::Uuid uuid(uuid_);
    PERFETTO_LOG("go/trace-uuid/%s name=\"%s\" size=%" PRIu64,
                 uuid.ToPrettyString().c_str(),
                 trace_config_->unique_session_name().c_str(), bytes_written_);
  }

  // Ask incidentd to create a report, which will read the file we just
  // wrote.
  PERFETTO_LAZY_LOAD(android_internal::StartIncidentReport, incident_fn);
  PERFETTO_CHECK(incident_fn(cfg.destination_package().c_str(),
                             cfg.destination_class().c_str(),
                             cfg.privacy_level()));
}

void PerfettoCmd::ReportTraceToAndroidFrameworkOrCrash() {
  PERFETTO_CHECK(report_to_android_framework_);
  PERFETTO_CHECK(trace_out_stream_);

  const auto& cfg = trace_config_->android_report_config();
  PERFETTO_CHECK(!cfg.reporter_service_package().empty());
  PERFETTO_CHECK(!cfg.skip_report());

  if (bytes_written_ == 0) {
    LogUploadEvent(PerfettoStatsdAtom::kCmdFwReportEmptyTrace);
    PERFETTO_LOG("Skipping reporting trace to Android. Empty trace.");
    return;
  }

  LogUploadEvent(PerfettoStatsdAtom::kCmdFwReportBegin);
  base::StackString<128> self_fd("/proc/self/fd/%d",
                                 fileno(*trace_out_stream_));
  base::ScopedFile fd(base::OpenFile(self_fd.c_str(), O_RDONLY | O_CLOEXEC));
  if (!fd) {
    PERFETTO_FATAL("Failed to dup fd when reporting to Android");
  }

  base::Uuid uuid(uuid_);
  PERFETTO_LAZY_LOAD(android_internal::ReportTrace, report_fn);
  PERFETTO_CHECK(report_fn(cfg.reporter_service_package().c_str(),
                           cfg.reporter_service_class().c_str(), fd.release(),
                           uuid.lsb(), uuid.msb(),
                           cfg.use_pipe_in_framework_for_testing()));

  // Skip the trace-uuid link for traces that are too small. Realistically those
  // traces contain only a marker (e.g. seized_for_bugreport, or the trace
  // expired without triggers). Those are useless and introduce only noise.
  if (bytes_written_ > 4096) {
    PERFETTO_LOG("go/trace-uuid/%s name=\"%s\" size=%" PRIu64,
                 uuid.ToPrettyString().c_str(),
                 trace_config_->unique_session_name().c_str(), bytes_written_);
  }
  LogUploadEvent(PerfettoStatsdAtom::kCmdFwReportHandoff);
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

  int fd = fileno(*trace_out_stream_);
  off_t offset = 0;
  size_t remaining = static_cast<size_t>(bytes_written_);

  // Count time in terms of CPU to avoid timeouts due to suspend:
  base::TimeNanos start = base::GetThreadCPUTimeNs();
  for (;;) {
    errno = 0;
    PERFETTO_DCHECK(static_cast<size_t>(offset) + remaining == bytes_written_);
    auto wsize = PERFETTO_EINTR(sendfile(*staging_fd, fd, &offset, remaining));
    if (wsize < 0) {
      PERFETTO_FATAL("sendfile() failed wsize=%zd, off=%" PRId64
                     ", initial=%" PRIu64 ", remaining=%zu",
                     wsize, static_cast<int64_t>(offset), bytes_written_,
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
                     wsize, static_cast<int64_t>(offset), bytes_written_,
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

}  // namespace perfetto
