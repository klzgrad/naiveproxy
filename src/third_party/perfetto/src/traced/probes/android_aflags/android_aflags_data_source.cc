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

#include "src/traced/probes/android_aflags/android_aflags_data_source.h"

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/task_runner.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/base64.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/pipe.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/subprocess.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/tracing/core/data_source_config.h"

#include "protos/perfetto/config/android/android_aflags_config.pbzero.h"
#include "protos/perfetto/trace/android/android_aflags.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {
namespace {
constexpr uint32_t kAflagsExitPollMs = 100;
constexpr uint32_t kMinPollPeriodMs = 1000;
constexpr uint32_t kAflagsExitTimeoutMs = 10000;
}  // namespace

// static
const ProbesDataSource::Descriptor AndroidAflagsDataSource::descriptor = {
    /* name */ "android.aflags",
    /* flags */ Descriptor::kFlagsNone,
    /* fill_descriptor_func */ nullptr,
};

AndroidAflagsDataSource::AndroidAflagsDataSource(
    const DataSourceConfig& ds_config,
    base::TaskRunner* task_runner,
    TracingSessionID session_id,
    std::unique_ptr<TraceWriter> writer)
    : ProbesDataSource(session_id, &descriptor),
      task_runner_(task_runner),
      writer_(std::move(writer)),
      weak_factory_(this) {
  protos::pbzero::AndroidAflagsConfig::Decoder cfg(
      ds_config.android_aflags_config_raw());
  poll_period_ms_ = cfg.poll_ms();
  if (poll_period_ms_ > 0 && poll_period_ms_ < kMinPollPeriodMs) {
    PERFETTO_ILOG("poll_ms %" PRIu32 " is less than minimum of %" PRIu32
                  "ms. Increasing to %" PRIu32 "ms.",
                  poll_period_ms_, kMinPollPeriodMs, kMinPollPeriodMs);
    poll_period_ms_ = kMinPollPeriodMs;
  }
}

AndroidAflagsDataSource::~AndroidAflagsDataSource() {
  if (aflags_output_pipe_) {
    task_runner_->RemoveFileDescriptorWatch(*aflags_output_pipe_);
  }
  if (aflags_process_) {
    // At this point we want to kill the process to avoid leaking subprocesses
    // if there is any bug with aflags.
    // Send SIGKILL synchronously, then defer the waitpid() by
    // kAflagsExitTimeoutMs so the child almost certainly has exited by then and
    // Wait() returns without blocking the task runner.
    aflags_process_->Kill();
    task_runner_->PostDelayedTask(
        [process = std::shared_ptr<base::Subprocess>(
             std::move(aflags_process_))] { process->Wait(); },
        kAflagsExitTimeoutMs);
  }

  // It is theoretically possible that at this point pending_flushes_ will
  // be non-empty. If it is the case there is nothing we can do. We cannot
  // flush now because the tracing session has already terminated.
  // TracingServiceImpl will deal with timeout detection.
}

void AndroidAflagsDataSource::Start() {
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  PERFETTO_ELOG("Aflags only supported on Android.");
  return;
#else
  Tick();
#endif
}

void AndroidAflagsDataSource::Tick() {
  if (poll_period_ms_ > 0) {
    uint32_t delay_ms =
        poll_period_ms_ -
        static_cast<uint32_t>(base::GetWallTimeMs().count() % poll_period_ms_);
    task_runner_->PostDelayedTask(
        [weak_this = weak_factory_.GetWeakPtr()] {
          if (weak_this) {
            weak_this->Tick();
          }
        },
        delay_ms);
  }

  if (aflags_process_) {
    PERFETTO_DLOG("Aflags process still running, skipping tick.");
    return;
  }

  auto pipe_pair = base::Pipe::Create(base::Pipe::kRdNonBlock);
  aflags_output_pipe_ = std::move(pipe_pair.rd);
  // Watch the read end of the pipe for output from the aflags process.
  task_runner_->AddFileDescriptorWatch(
      *aflags_output_pipe_, [weak_this = weak_factory_.GetWeakPtr()] {
        if (weak_this) {
          weak_this->OnAflagsOutput();
        }
      });

  aflags_output_.clear();

  // It returns a base64-encoded binary proto that needs to be decoded before
  // being written to the trace.
  aflags_process_ =
      std::make_unique<base::Subprocess>(std::initializer_list<std::string>{
          "/system/bin/aflags", "list", "--format", "proto"});
  aflags_process_->args.stdout_mode = base::Subprocess::OutputMode::kFd;
  aflags_process_->args.stderr_mode = base::Subprocess::OutputMode::kFd;
  // Keeps track of both stdout and stderr in the same pipe.
  aflags_process_->args.out_fd = std::move(pipe_pair.wr);
  aflags_process_->Start();
}

void AndroidAflagsDataSource::OnAflagsOutput() {
  errno = 0;

  bool eof = base::ReadFileDescriptor(*aflags_output_pipe_, &aflags_output_);
  if (!eof && base::IsAgain(errno)) {
    return;  // Read is blocked, wait for the next notification.
  }

  // If we get here either EOF or read() errored out (unlikely, but SELinux).
  task_runner_->RemoveFileDescriptorWatch(*aflags_output_pipe_);

  std::string error;
  if (!eof) {
    base::StackString<255> err_str("aflags failed: pipe read (%d, %s)", errno,
                                   strerror(errno));
    if (aflags_process_) {
      aflags_process_->Kill();
    }
    error = err_str.ToStdString();
  }

  FinalizeAflagsCapture(std::move(error));
}

// We get to this function if either we read the stdout through EOF or if the
// stdout.read() failed. In either case the process might have not terminated
// yet (a subprocess usually terminates a bit after closing stdout/err).
void AndroidAflagsDataSource::FinalizeAflagsCapture(std::string error) {
  if (!aflags_process_) {
    return;
  }

  aflags_process_->Poll();
  auto status = aflags_process_->status();
  if (status == base::Subprocess::kRunning) {
    // Process hasn't finished running yet, reschedule and check later.
    task_runner_->PostDelayedTask(
        [weak_this = weak_factory_.GetWeakPtr(), error_mv = std::move(error)] {
          if (weak_this)
            weak_this->FinalizeAflagsCapture(std::move(error_mv));
        },
        kAflagsExitPollMs);
    return;
  }

  int returncode = aflags_process_->returncode();
  aflags_process_.reset();
  aflags_output_pipe_.reset();

  if (error.empty() &&
      !(status == base::Subprocess::kTerminated && returncode == 0)) {
    error =
        "aflags failed: status: " + std::to_string(static_cast<int>(status)) +
        ", code: " + std::to_string(returncode) + ". Output: " + aflags_output_;
  }

  // Whether we error or not, at this point we decided that we are going to
  // write _something_ in the trace, hence we should ack the pending flushes.
  auto ack_pending_flushes_on_exit = base::OnScopeExit([&] {
    // Drain any Flush() callbacks deferred while the subprocess was running.
    auto pending_flushes_vector = std::move(pending_flushes_);
    for (auto& flush_callback : pending_flushes_vector)
      flush_callback();
  });

  if (!error.empty()) {
    EmitErrorPacket(error);
    aflags_output_.clear();
    return;
  }

  std::string output = base::TrimWhitespace(aflags_output_);
  aflags_output_.clear();

  // The output of `aflags list --format proto` is base64-encoded.
  std::optional<std::string> decoded =
      base::Base64Decode(output.data(), output.size());
  if (!decoded) {
    EmitErrorPacket("Failed to decode aflags output (len=" +
                    std::to_string(output.size()) + "): " + output);
    return;
  }

  TraceWriter::TracePacketHandle packet = writer_->NewTracePacket();
  packet->set_timestamp(static_cast<uint64_t>(base::GetBootTimeNs().count()));
  protos::pbzero::AndroidAflags* aflags_proto = packet->set_android_aflags();
  aflags_proto->AppendRawProtoBytes(decoded->data(), decoded->size());
  packet->Finalize();
  writer_->Flush();
}

void AndroidAflagsDataSource::EmitErrorPacket(const std::string& error_msg) {
  PERFETTO_ELOG("%s", error_msg.c_str());
  auto packet = writer_->NewTracePacket();
  packet->set_timestamp(static_cast<uint64_t>(base::GetBootTimeNs().count()));
  auto* aflags_proto = packet->set_android_aflags();
  aflags_proto->set_error(error_msg);
  packet->Finalize();
  writer_->Flush();
}

void AndroidAflagsDataSource::Flush(FlushRequestID,
                                    std::function<void()> callback) {
  // Fast path: no subprocess in flight, flush immediately.
  if (!aflags_process_) {
    writer_->Flush(std::move(callback));
    return;
  }

  // Subprocess still running. Defer the callback until FinalizeAflagsCapture().
  // If the subprocess takes longer than the flush timeout, we don't do anything
  // special here, as ProbesProducer already has its own timeout to gate the
  // flush of all the various data sources.
  // What we really want to achieve here is the following: if aflags takes
  // time to respond (normally it takes ~350ms) AND if the trace is short
  // (some heap dump traces have 1s duration) we want to stall the trace
  // termination to maximize the chance we get the aflags output in the trace,
  // up to ProbesProducer::kFlushTimeoutMs.
  pending_flushes_.emplace_back(std::move(callback));
}

}  // namespace perfetto
