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
#include <sys/system_properties.h>

#include <chrono>
#include <cinttypes>
#include <thread>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/android_utils.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_mmap.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/ext/base/uuid.h"
#include "perfetto/protozero/proto_decoder.h"
#include "perfetto/tracing/core/forward_decls.h"
#include "src/android_internal/incident_service.h"
#include "src/android_internal/lazy_library_loader.h"
#include "src/android_internal/tracing_service_proxy.h"
#include "src/android_stats/statsd_logging_helper.h"

#include "perfetto/base/time.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "protos/perfetto/config/trace_config.gen.h"

#include "protos/perfetto/trace/android/recovered_trace_info.pbzero.h"
#include "protos/perfetto/trace/trace.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {
namespace {

// traced runs as 'user nobody' (AID_NOBODY), defined here:
// https://cs.android.com/android/platform/superproject/+/android-latest-release:system/core/libcutils/include/private/android_filesystem_config.h;l=203;drc=f5b540e2b7b9b325d99486d49c0ac57bdd0c5344
// We only trust packages written by traced.
static constexpr int32_t kTrustedUid = 9999;

// State: <status>:<boot_time_ns>
// Empty : Not started yet
// 1:ts  : Started but not finished yet
// 2:ts  : Finished
const char* kRebootTraceStatusProp = "traced.reboot_trace.status";

// Maximum duration to wait for the boot recovery service to start up
// and unlink pre-existing trace files from disk. Sized to 5 minutes to
// accommodate worst-case full device boot and service scheduling.
constexpr auto kBootTraceCleanupTimeoutNs = std::chrono::minutes(5);

enum class RebootTraceUploadState {
  kTraceUploadStarted = 1,
  kTraceUploadFinished = 2,
};

void SetRebootTraceStatusProp(RebootTraceUploadState status) {
  uint64_t boot_time = static_cast<uint64_t>(base::GetBootTimeNs().count());
  base::StackString<64> prop_val("%d:%" PRIu64, static_cast<int>(status),
                                 boot_time);
  __system_property_set(kRebootTraceStatusProp, prop_val.c_str());
}

// Directory for local state and temporary files. This is automatically
// created by the system by setting setprop persist.traced.enable=1.
std::string SanitizeSessionName(const std::string& session_name) {
  std::string sanitized_name;
  sanitized_name.reserve(session_name.size());
  for (char c : session_name) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-') {
      sanitized_name.push_back(c);
    }
  }
  return sanitized_name.empty() ? "default" : sanitized_name;
}

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

  if (!persistent_file_path_.empty()) {
    PERFETTO_CHECK(!unlink(persistent_file_path_.c_str()));
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

void PerfettoCmd::WaitForPreviousRebootTraceUpload(
    const std::string& session_name,
    const std::string& target_file_path) {
  // Only block if a persistent trace file with the SAME session name exists on
  // disk.
  if (!base::FileExists(target_file_path)) {
    return;
  }

  const auto deadline = base::GetBootTimeNs() + kBootTraceCleanupTimeoutNs;
  std::string cur_prop;
  do {
    cur_prop = base::GetAndroidProp(kRebootTraceStatusProp);
    if (cur_prop.empty()) {
      base::SleepMicroseconds(500 * 1000);
    }
  } while (cur_prop.empty() && base::GetBootTimeNs() < deadline);

  if (cur_prop.empty()) {
    android_stats::MaybeLogUploadEvent(
        PerfettoStatsdAtom::kRebootTraceUploadTimeout, /*uuid_lsb=*/0,
        /*uuid_msb=*/0, session_name);
    remove(target_file_path.c_str());
    PERFETTO_ELOG(
        "Timed out waiting for uploader to set property status for session "
        "'%s'! Unlinked '%s'.",
        session_name.c_str(), target_file_path.c_str());
    return;
  }

  // If property is set, but the persistent trace file STILL exists on disk,
  // log error and unlink file.
  if (base::FileExists(target_file_path)) {
    android_stats::MaybeLogUploadEvent(
        PerfettoStatsdAtom::kRebootTraceUploadLeftover, /*uuid_lsb=*/0,
        /*uuid_msb=*/0, session_name);
    remove(target_file_path.c_str());
    PERFETTO_ELOG(
        "Persistent trace file '%s' still exists on disk even though property "
        "is set to '%s'! Unlinked file.",
        target_file_path.c_str(), cur_prop.c_str());
  }
}

// static
base::ScopedFile PerfettoCmd::WaitForUploadCompleteAndCreatePersistentTmpFile(
    const std::string& session_name,
    std::string* out_file_path) {
  std::string sanitized_name = SanitizeSessionName(session_name);
  base::StackString<256> dir_path("%s/persistent", kStateDir);
  base::StackString<256> file_path("%s/%s.tmp", dir_path.c_str(),
                                   sanitized_name.c_str());

  // Wait for any previous pending reboot trace upload for this session name to
  // complete if the persistent trace file exists on disk.
  WaitForPreviousRebootTraceUpload(sanitized_name, file_path.c_str());

  // Unconditionally unlink any pre-existing file on disk before creating a new
  // one. If another process (e.g. background uploader) is currently reading the
  // previous file, unlinking preserves its open inode so the upload is not
  // corrupted while allowing us to create a fresh file for the new session.
  remove(file_path.c_str());

  auto fd = base::OpenFile(file_path.c_str(),
                           O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600);
  if (!fd) {
    PERFETTO_PLOG("Could not create persistent trace file %s",
                  file_path.c_str());
  } else if (out_file_path) {
    *out_file_path = file_path.c_str();
  }
  return fd;
}

size_t PerfettoCmd::TruncateAndAnnotatePersistentTrace(
    int fd,
    const base::ScopedMmap& mmap,
    const std::string& file_name) {
  // Compute the valid boundary of complete trace packets in the file.
  size_t valid_offset = 0;
  protozero::ProtoDecoder trace_decoder(mmap.data(), mmap.length());
  for (auto packet = trace_decoder.ReadField(); packet;
       packet = trace_decoder.ReadField()) {
    valid_offset = trace_decoder.read_offset();
  }

  // Truncate any incomplete trailing trace packet caused by abrupt reboot.
  uint64_t bytes_truncated = 0;
  if (valid_offset < mmap.length()) {
    bytes_truncated = static_cast<uint64_t>(mmap.length() - valid_offset);
    PERFETTO_LOG(
        "reboot-trace: Truncating incomplete trailing trace packet from %zu to "
        "%zu bytes (%" PRIu64 " bytes truncated) in %s",
        mmap.length(), valid_offset, bytes_truncated, file_name.c_str());
    if (ftruncate(fd, static_cast<off_t>(valid_offset)) != 0) {
      PERFETTO_PLOG("Failed to ftruncate trace file %s", file_name.c_str());
    }
  }

  // Inject RecoveredTraceInfo packet into trace stream with recovery info.
  protozero::HeapBuffered<protos::pbzero::Trace> extra;
  auto* recovered_info = extra->add_packet()->set_recovered_trace_info();
  recovered_info->set_reason(
      protos::pbzero::RecoveredTraceInfo::REASON_UNEXPECTED_REBOOT);
  recovered_info->set_original_file_size_bytes(
      static_cast<uint64_t>(mmap.length()));
  recovered_info->set_bytes_truncated(bytes_truncated);

  std::vector<uint8_t> packet_bytes = extra.SerializeAsArray();
  if (lseek(fd, static_cast<off_t>(valid_offset), SEEK_SET) == -1) {
    PERFETTO_PLOG("Failed to lseek trace file %s", file_name.c_str());
  } else {
    base::WriteAll(fd, packet_bytes.data(), packet_bytes.size());
    valid_offset += packet_bytes.size();
  }
  return valid_offset;
}

// This function is called when --upload-after-reboot is passed to perfetto_cmd
int PerfettoCmd::UploadPersistentTracesAfterReboot() {
  base::StackString<256> persistent_dir("%s/persistent", kStateDir);

  // Ensure property status is updated to Finished (2:TS) on any exit path,
  // preventing newly started tracing sessions from timing out.
  auto on_exit = base::OnScopeExit([] {
    SetRebootTraceStatusProp(RebootTraceUploadState::kTraceUploadFinished);
  });

  std::vector<std::string> files;
  base::Status status = base::ListFilesRecursive(persistent_dir.c_str(), files);
  if (!status.ok()) {
    PERFETTO_ELOG("reboot-trace: Failed to list files in %s: %s",
                  persistent_dir.c_str(), status.c_message());
    return 0;
  }

  // Step 1: Upfront Unlink & Open.
  // Open ALL persistent files upfront and immediately unlink them from disk.
  // If the process crashes during processing or uploading of any trace,
  // no persistent trace files remain on disk to cause crash loops.
  struct PendingTrace {
    base::ScopedFile fd;
    std::string file_name;
  };
  std::vector<PendingTrace> pending_traces;

  for (const std::string& file_name : files) {
    // Only process immediate top-level .tmp trace files. Ignore subdirectories.
    if (file_name.find('/') != std::string::npos ||
        !base::EndsWith(file_name, ".tmp")) {
      continue;
    }

    base::StackString<256> full_path("%s/%s", persistent_dir.c_str(),
                                     file_name.c_str());

    base::ScopedFile fd = base::OpenFile(full_path.c_str(), O_RDWR | O_CLOEXEC);
    if (!fd) {
      PERFETTO_PLOG("reboot-trace: Failed to open persistent trace %s",
                    full_path.c_str());
    }
    // Unlink immediately regardless of open status to guarantee disk cleanup.
    unlink(full_path.c_str());

    if (fd) {
      pending_traces.emplace_back(PendingTrace{std::move(fd), file_name});
    }
  }

  // Set property status to 1:TS after all persistent trace files are unlinked.
  // At this point any new reboot-aware trace session can proceed immediately,
  // since the target persistent file on disk has been unlinked.
  SetRebootTraceStatusProp(RebootTraceUploadState::kTraceUploadStarted);

  // Step 2: Process pre-opened descriptors directly to avoid packet corruption.
  for (auto& pending : pending_traces) {
    int fd = pending.fd.get();
    std::optional<uint64_t> file_size = base::GetFileSize(fd);
    if (!file_size.has_value() || *file_size == 0) {
      PERFETTO_LOG("reboot-trace: Skipping empty persistent trace %s",
                   pending.file_name.c_str());
      continue;
    }

    base::ScopedMmap mmap = base::ScopedMmap::FromHandle(
        base::ScopedFile(dup(fd)), static_cast<size_t>(*file_size));
    if (!mmap.IsValid()) {
      PERFETTO_PLOG("reboot-trace: Failed to mmap persistent trace %s",
                    pending.file_name.c_str());
      continue;
    }

    std::optional<TraceConfig> config = ParseTraceConfigFromMmapedTrace(mmap);
    if (!config.has_value() || !config->has_android_report_config()) {
      android_stats::MaybeLogUploadEvent(
          PerfettoStatsdAtom::kRebootTraceParseFailed, /*uuid_lsb=*/0,
          /*uuid_msb=*/0, pending.file_name);
      PERFETTO_ELOG(
          "reboot-trace: Failed to parse TraceConfig from persistent trace %s",
          pending.file_name.c_str());
      continue;
    }

    base::Uuid uuid(config->trace_uuid_lsb(), config->trace_uuid_msb());
    android_stats::MaybeLogUploadEvent(
        PerfettoStatsdAtom::kRebootTraceRecovered, uuid.lsb(), uuid.msb());

    size_t valid_offset =
        TruncateAndAnnotatePersistentTrace(fd, mmap, pending.file_name);

    base::Status report_status = ReportTraceToAndroidFramework(
        fd, valid_offset, uuid, config->unique_session_name(),
        config->android_report_config(), true);

    if (!report_status.ok()) {
      PERFETTO_ELOG(
          "reboot-trace: Failed to upload recovered reboot trace %s: %s",
          pending.file_name.c_str(), report_status.c_message());
    } else {
      PERFETTO_LOG(
          "reboot-trace: Successfully uploaded recovered reboot trace %s",
          pending.file_name.c_str());
    }
  }

  return 0;
}

// static
std::optional<TraceConfig> PerfettoCmd::ParseTraceConfigFromMmapedTrace(
    const base::ScopedMmap& mmapped_trace) {
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
