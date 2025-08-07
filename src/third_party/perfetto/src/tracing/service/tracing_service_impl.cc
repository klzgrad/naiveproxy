/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "src/tracing/service/tracing_service_impl.h"

#include <limits.h>
#include <string.h>

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN) && \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_NACL)
#include <sys/uio.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) && \
    PERFETTO_BUILDFLAG(PERFETTO_ANDROID_BUILD)
#include "src/android_internal/lazy_library_loader.h"    // nogncheck
#include "src/android_internal/tracing_service_proxy.h"  // nogncheck
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) ||   \
    PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
#define PERFETTO_HAS_CHMOD
#include <sys/stat.h>
#endif

#include "perfetto/base/build_config.h"
#include "perfetto/base/status.h"
#include "perfetto/base/task_runner.h"
#include "perfetto/ext/base/android_utils.h"
#include "perfetto/ext/base/clock_snapshots.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/metatrace.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/ext/base/sys_types.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/ext/base/uuid.h"
#include "perfetto/ext/base/version.h"
#include "perfetto/ext/base/watchdog.h"
#include "perfetto/ext/tracing/core/basic_types.h"
#include "perfetto/ext/tracing/core/client_identity.h"
#include "perfetto/ext/tracing/core/consumer.h"
#include "perfetto/ext/tracing/core/observable_events.h"
#include "perfetto/ext/tracing/core/producer.h"
#include "perfetto/ext/tracing/core/shared_memory.h"
#include "perfetto/ext/tracing/core/shared_memory_abi.h"
#include "perfetto/ext/tracing/core/trace_packet.h"
#include "perfetto/ext/tracing/core/trace_writer.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "perfetto/protozero/static_buffer.h"
#include "perfetto/tracing/core/data_source_descriptor.h"
#include "perfetto/tracing/core/tracing_service_capabilities.h"
#include "perfetto/tracing/core/tracing_service_state.h"
#include "src/android_stats/statsd_logging_helper.h"
#include "src/protozero/filtering/message_filter.h"
#include "src/protozero/filtering/string_filter.h"
#include "src/tracing/core/shared_memory_arbiter_impl.h"
#include "src/tracing/service/packet_stream_validator.h"
#include "src/tracing/service/trace_buffer.h"

#include "protos/perfetto/common/builtin_clock.gen.h"
#include "protos/perfetto/common/builtin_clock.pbzero.h"
#include "protos/perfetto/common/system_info.pbzero.h"
#include "protos/perfetto/common/trace_stats.pbzero.h"
#include "protos/perfetto/config/trace_config.pbzero.h"
#include "protos/perfetto/trace/clock_snapshot.pbzero.h"
#include "protos/perfetto/trace/perfetto/tracing_service_event.pbzero.h"
#include "protos/perfetto/trace/remote_clock_sync.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "protos/perfetto/trace/trace_uuid.pbzero.h"
#include "protos/perfetto/trace/trigger.pbzero.h"

// General note: this class must assume that Producers are malicious and will
// try to crash / exploit this class. We can trust pointers because they come
// from the IPC layer, but we should never assume that that the producer calls
// come in the right order or their arguments are sane / within bounds.

// This is a macro because we want the call-site line number for the ELOG.
#define PERFETTO_SVC_ERR(...) \
  (PERFETTO_ELOG(__VA_ARGS__), ::perfetto::base::ErrStatus(__VA_ARGS__))

namespace perfetto {

namespace {
constexpr int kMaxBuffersPerConsumer = 128;
constexpr uint32_t kDefaultSnapshotsIntervalMs = 10 * 1000;
constexpr int kDefaultWriteIntoFilePeriodMs = 5000;
constexpr int kMinWriteIntoFilePeriodMs = 100;
constexpr uint32_t kAllDataSourceStartedTimeout = 20000;
constexpr int kMaxConcurrentTracingSessions = 15;
constexpr int kMaxConcurrentTracingSessionsPerUid = 5;
constexpr int kMaxConcurrentTracingSessionsForStatsdUid = 10;
constexpr int64_t kMinSecondsBetweenTracesGuardrail = 5 * 60;

constexpr uint32_t kMillisPerHour = 3600000;
constexpr uint32_t kMillisPerDay = kMillisPerHour * 24;
constexpr uint32_t kMaxTracingDurationMillis = 7 * 24 * kMillisPerHour;

// These apply only if enable_extra_guardrails is true.
constexpr uint32_t kGuardrailsMaxTracingBufferSizeKb = 128 * 1024;
constexpr uint32_t kGuardrailsMaxTracingDurationMillis = 24 * kMillisPerHour;

constexpr size_t kMaxLifecycleEventsListedDataSources = 32;

constexpr uint32_t kTracePacketSystemInfoFieldId = 45;

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN) || PERFETTO_BUILDFLAG(PERFETTO_OS_NACL)
struct iovec {
  void* iov_base;  // Address
  size_t iov_len;  // Block size
};

// Simple implementation of writev. Note that this does not give the atomicity
// guarantees of a real writev, but we don't depend on these (we aren't writing
// to the same file from another thread).
ssize_t writev(int fd, const struct iovec* iov, int iovcnt) {
  ssize_t total_size = 0;
  for (int i = 0; i < iovcnt; ++i) {
    ssize_t current_size = base::WriteAll(fd, iov[i].iov_base, iov[i].iov_len);
    if (current_size != static_cast<ssize_t>(iov[i].iov_len))
      return -1;
    total_size += current_size;
  }
  return total_size;
}

#define IOV_MAX 1024  // Linux compatible limit.

#elif PERFETTO_BUILDFLAG(PERFETTO_OS_QNX)
#define IOV_MAX 1024  // Linux compatible limit.
#endif

// Partially encodes a CommitDataRequest in an int32 for the purposes of
// metatracing. Note that it encodes only the bottom 10 bits of the producer id
// (which is technically 16 bits wide).
//
// Format (by bit range):
// [   31 ][         30 ][             29:20 ][            19:10 ][        9:0]
// [unused][has flush id][num chunks to patch][num chunks to move][producer id]
int32_t EncodeCommitDataRequest(ProducerID producer_id,
                                const CommitDataRequest& req_untrusted) {
  uint32_t cmov = static_cast<uint32_t>(req_untrusted.chunks_to_move_size());
  uint32_t cpatch = static_cast<uint32_t>(req_untrusted.chunks_to_patch_size());
  uint32_t has_flush_id = req_untrusted.flush_request_id() != 0;

  uint32_t mask = (1 << 10) - 1;
  uint32_t acc = 0;
  acc |= has_flush_id << 30;
  acc |= (cpatch & mask) << 20;
  acc |= (cmov & mask) << 10;
  acc |= (producer_id & mask);
  return static_cast<int32_t>(acc);
}

void SerializeAndAppendPacket(std::vector<TracePacket>* packets,
                              std::vector<uint8_t> packet) {
  Slice slice = Slice::Allocate(packet.size());
  memcpy(slice.own_data(), packet.data(), packet.size());
  packets->emplace_back();
  packets->back().AddSlice(std::move(slice));
}

std::tuple<size_t /*shm_size*/, size_t /*page_size*/> EnsureValidShmSizes(
    size_t shm_size,
    size_t page_size) {
  // Theoretically the max page size supported by the ABI is 64KB.
  // However, the current implementation of TraceBuffer (the non-shared
  // userspace buffer where the service copies data) supports at most
  // 32K. Setting 64K "works" from the producer<>consumer viewpoint
  // but then causes the data to be discarded when copying it into
  // TraceBuffer.
  constexpr size_t kMaxPageSize = 32 * 1024;
  static_assert(kMaxPageSize <= SharedMemoryABI::kMaxPageSize, "");

  if (page_size == 0)
    page_size = TracingServiceImpl::kDefaultShmPageSize;
  if (shm_size == 0)
    shm_size = TracingServiceImpl::kDefaultShmSize;

  page_size = std::min<size_t>(page_size, kMaxPageSize);
  shm_size = std::min<size_t>(shm_size, TracingServiceImpl::kMaxShmSize);

  // The tracing page size has to be multiple of 4K. On some systems (e.g. Mac
  // on Arm64) the system page size can be larger (e.g., 16K). That doesn't
  // matter here, because the tracing page size is just a logical partitioning
  // and does not have any dependencies on kernel mm syscalls (read: it's fine
  // to have trace page sizes of 4K on a system where the kernel page size is
  // 16K).
  bool page_size_is_valid = page_size >= SharedMemoryABI::kMinPageSize;
  page_size_is_valid &= page_size % SharedMemoryABI::kMinPageSize == 0;

  // Only allow power of two numbers of pages, i.e. 1, 2, 4, 8 pages.
  size_t num_pages = page_size / SharedMemoryABI::kMinPageSize;
  page_size_is_valid &= (num_pages & (num_pages - 1)) == 0;

  if (!page_size_is_valid || shm_size < page_size ||
      shm_size % page_size != 0) {
    return std::make_tuple(TracingServiceImpl::kDefaultShmSize,
                           TracingServiceImpl::kDefaultShmPageSize);
  }
  return std::make_tuple(shm_size, page_size);
}

bool NameMatchesFilter(const std::string& name,
                       const std::vector<std::string>& name_filter,
                       const std::vector<std::string>& name_regex_filter) {
  bool filter_is_set = !name_filter.empty() || !name_regex_filter.empty();
  if (!filter_is_set)
    return true;
  bool filter_matches = std::find(name_filter.begin(), name_filter.end(),
                                  name) != name_filter.end();
  bool filter_regex_matches =
      std::find_if(name_regex_filter.begin(), name_regex_filter.end(),
                   [&](const std::string& regex) {
                     return std::regex_match(
                         name, std::regex(regex, std::regex::extended));
                   }) != name_regex_filter.end();
  return filter_matches || filter_regex_matches;
}

// Used when TraceConfig.write_into_file == true and output_path is not empty.
base::ScopedFile CreateTraceFile(const std::string& path, bool overwrite) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) && \
    PERFETTO_BUILDFLAG(PERFETTO_ANDROID_BUILD)
  // This is NOT trying to preserve any security property, SELinux does that.
  // It just improves the actionability of the error when people try to save the
  // trace in a location that is not SELinux-allowed (a generic "permission
  // denied" vs "don't put it here, put it there").
  // These are the only SELinux approved dir for trace files that are created
  // directly by traced.
  static const char* kTraceDirBasePath = "/data/misc/perfetto-traces/";
  if (!base::StartsWith(path, kTraceDirBasePath)) {
    PERFETTO_ELOG("Invalid output_path %s. On Android it must be within %s.",
                  path.c_str(), kTraceDirBasePath);
    return base::ScopedFile();
  }
#endif
  // O_CREAT | O_EXCL will fail if the file exists already.
  const int flags = O_RDWR | O_CREAT | (overwrite ? O_TRUNC : O_EXCL);
  auto fd = base::OpenFile(path, flags, 0600);
  if (fd) {
#if defined(PERFETTO_HAS_CHMOD)
    // Passing 0644 directly above won't work because of umask.
    PERFETTO_CHECK(fchmod(*fd, 0644) == 0);
#endif
  } else {
    PERFETTO_PLOG("Failed to create %s", path.c_str());
  }
  return fd;
}

bool ShouldLogEvent(const TraceConfig& cfg) {
  switch (cfg.statsd_logging()) {
    case TraceConfig::STATSD_LOGGING_ENABLED:
      return true;
    case TraceConfig::STATSD_LOGGING_DISABLED:
      return false;
    case TraceConfig::STATSD_LOGGING_UNSPECIFIED:
      break;
  }
  // For backward compatibility with older versions of perfetto_cmd.
  return cfg.enable_extra_guardrails();
}

// Appends `data` (which has `size` bytes), to `*packet`. Splits the data in
// slices no larger than `max_slice_size`.
void AppendOwnedSlicesToPacket(std::unique_ptr<uint8_t[]> data,
                               size_t size,
                               size_t max_slice_size,
                               perfetto::TracePacket* packet) {
  if (size <= max_slice_size) {
    packet->AddSlice(Slice::TakeOwnership(std::move(data), size));
    return;
  }
  uint8_t* src_ptr = data.get();
  for (size_t size_left = size; size_left > 0;) {
    const size_t slice_size = std::min(size_left, max_slice_size);

    Slice slice = Slice::Allocate(slice_size);
    memcpy(slice.own_data(), src_ptr, slice_size);
    packet->AddSlice(std::move(slice));

    src_ptr += slice_size;
    size_left -= slice_size;
  }
}

using TraceFilter = protos::gen::TraceConfig::TraceFilter;
std::optional<protozero::StringFilter::Policy> ConvertPolicy(
    TraceFilter::StringFilterPolicy policy) {
  switch (policy) {
    case TraceFilter::SFP_UNSPECIFIED:
      return std::nullopt;
    case TraceFilter::SFP_MATCH_REDACT_GROUPS:
      return protozero::StringFilter::Policy::kMatchRedactGroups;
    case TraceFilter::SFP_ATRACE_MATCH_REDACT_GROUPS:
      return protozero::StringFilter::Policy::kAtraceMatchRedactGroups;
    case TraceFilter::SFP_MATCH_BREAK:
      return protozero::StringFilter::Policy::kMatchBreak;
    case TraceFilter::SFP_ATRACE_MATCH_BREAK:
      return protozero::StringFilter::Policy::kAtraceMatchBreak;
    case TraceFilter::SFP_ATRACE_REPEATED_SEARCH_REDACT_GROUPS:
      return protozero::StringFilter::Policy::kAtraceRepeatedSearchRedactGroups;
  }
  return std::nullopt;
}

}  // namespace

// static
std::unique_ptr<TracingService> TracingService::CreateInstance(
    std::unique_ptr<SharedMemory::Factory> shm_factory,
    base::TaskRunner* task_runner,
    InitOpts init_opts) {
  tracing_service::Dependencies deps;
  deps.clock = std::make_unique<tracing_service::ClockImpl>();
  uint32_t seed = static_cast<uint32_t>(deps.clock->GetWallTimeMs().count());
  deps.random = std::make_unique<tracing_service::RandomImpl>(seed);
  return std::unique_ptr<TracingService>(new TracingServiceImpl(
      std::move(shm_factory), task_runner, std::move(deps), init_opts));
}

TracingServiceImpl::TracingServiceImpl(
    std::unique_ptr<SharedMemory::Factory> shm_factory,
    base::TaskRunner* task_runner,
    tracing_service::Dependencies deps,
    InitOpts init_opts)
    : clock_(std::move(deps.clock)),
      random_(std::move(deps.random)),
      init_opts_(init_opts),
      shm_factory_(std::move(shm_factory)),
      uid_(base::GetCurrentUserId()),
      buffer_ids_(kMaxTraceBufferID),
      weak_runner_(task_runner) {
  PERFETTO_DCHECK(task_runner);
}

TracingServiceImpl::~TracingServiceImpl() {
  // TODO(fmayer): handle teardown of all Producer.
}

std::unique_ptr<TracingService::ProducerEndpoint>
TracingServiceImpl::ConnectProducer(Producer* producer,
                                    const ClientIdentity& client_identity,
                                    const std::string& producer_name,
                                    size_t shared_memory_size_hint_bytes,
                                    bool in_process,
                                    ProducerSMBScrapingMode smb_scraping_mode,
                                    size_t shared_memory_page_size_hint_bytes,
                                    std::unique_ptr<SharedMemory> shm,
                                    const std::string& sdk_version) {
  PERFETTO_DCHECK_THREAD(thread_checker_);

  auto uid = client_identity.uid();
  if (lockdown_mode_ && uid != base::GetCurrentUserId()) {
    PERFETTO_DLOG("Lockdown mode. Rejecting producer with UID %ld",
                  static_cast<unsigned long>(uid));
    return nullptr;
  }

  if (producers_.size() >= kMaxProducerID) {
    PERFETTO_DFATAL("Too many producers.");
    return nullptr;
  }
  const ProducerID id = GetNextProducerID();
  PERFETTO_DLOG("Producer %" PRIu16 " connected, uid=%d", id,
                static_cast<int>(uid));
  bool smb_scraping_enabled = smb_scraping_enabled_;
  switch (smb_scraping_mode) {
    case ProducerSMBScrapingMode::kDefault:
      break;
    case ProducerSMBScrapingMode::kEnabled:
      smb_scraping_enabled = true;
      break;
    case ProducerSMBScrapingMode::kDisabled:
      smb_scraping_enabled = false;
      break;
  }

  std::unique_ptr<ProducerEndpointImpl> endpoint(new ProducerEndpointImpl(
      id, client_identity, this, weak_runner_.task_runner(), producer,
      producer_name, sdk_version, in_process, smb_scraping_enabled));
  auto it_and_inserted = producers_.emplace(id, endpoint.get());
  PERFETTO_DCHECK(it_and_inserted.second);
  endpoint->shmem_size_hint_bytes_ = shared_memory_size_hint_bytes;
  endpoint->shmem_page_size_hint_bytes_ = shared_memory_page_size_hint_bytes;

  // Producer::OnConnect() should run before Producer::OnTracingSetup(). The
  // latter may be posted by SetupSharedMemory() below, so post OnConnect() now.
  endpoint->weak_runner_.PostTask(
      [endpoint = endpoint.get()] { endpoint->producer_->OnConnect(); });

  if (shm) {
    // The producer supplied an SMB. This is used only by Chrome; in the most
    // common cases the SMB is created by the service and passed via
    // OnTracingSetup(). Verify that it is correctly sized before we attempt to
    // use it. The transport layer has to verify the integrity of the SMB (e.g.
    // ensure that the producer can't resize if after the fact).
    size_t shm_size, page_size;
    std::tie(shm_size, page_size) =
        EnsureValidShmSizes(shm->size(), endpoint->shmem_page_size_hint_bytes_);
    if (shm_size == shm->size() &&
        page_size == endpoint->shmem_page_size_hint_bytes_) {
      PERFETTO_DLOG(
          "Adopting producer-provided SMB of %zu kB for producer \"%s\"",
          shm_size / 1024, endpoint->name_.c_str());
      endpoint->SetupSharedMemory(std::move(shm), page_size,
                                  /*provided_by_producer=*/true);
    } else {
      PERFETTO_LOG(
          "Discarding incorrectly sized producer-provided SMB for producer "
          "\"%s\", falling back to service-provided SMB. Requested sizes: %zu "
          "B total, %zu B page size; suggested corrected sizes: %zu B total, "
          "%zu B page size",
          endpoint->name_.c_str(), shm->size(),
          endpoint->shmem_page_size_hint_bytes_, shm_size, page_size);
      shm.reset();
    }
  }

  return std::unique_ptr<ProducerEndpoint>(std::move(endpoint));
}

void TracingServiceImpl::DisconnectProducer(ProducerID id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  PERFETTO_DLOG("Producer %" PRIu16 " disconnected", id);
  PERFETTO_DCHECK(producers_.count(id));

  // Scrape remaining chunks for this producer to ensure we don't lose data.
  if (auto* producer = GetProducer(id)) {
    for (auto& session_id_and_session : tracing_sessions_)
      ScrapeSharedMemoryBuffers(&session_id_and_session.second, producer);
  }

  for (auto it = data_sources_.begin(); it != data_sources_.end();) {
    auto next = it;
    next++;
    if (it->second.producer_id == id)
      UnregisterDataSource(id, it->second.descriptor.name());
    it = next;
  }

  producers_.erase(id);
  UpdateMemoryGuardrail();
}

TracingServiceImpl::ProducerEndpointImpl* TracingServiceImpl::GetProducer(
    ProducerID id) const {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  auto it = producers_.find(id);
  if (it == producers_.end())
    return nullptr;
  return it->second;
}

std::unique_ptr<TracingService::ConsumerEndpoint>
TracingServiceImpl::ConnectConsumer(Consumer* consumer, uid_t uid) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  PERFETTO_DLOG("Consumer %p connected from UID %" PRIu64,
                reinterpret_cast<void*>(consumer), static_cast<uint64_t>(uid));
  std::unique_ptr<ConsumerEndpointImpl> endpoint(new ConsumerEndpointImpl(
      this, weak_runner_.task_runner(), consumer, uid));
  // Consumer might go away before we're able to send the connect notification,
  // if that is the case just bail out.
  auto weak_ptr = endpoint->weak_ptr_factory_.GetWeakPtr();
  weak_runner_.task_runner()->PostTask([weak_ptr = std::move(weak_ptr)] {
    if (weak_ptr)
      weak_ptr->consumer_->OnConnect();
  });
  return std::unique_ptr<ConsumerEndpoint>(std::move(endpoint));
}

void TracingServiceImpl::DisconnectConsumer(ConsumerEndpointImpl* consumer) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  PERFETTO_DLOG("Consumer %p disconnected", reinterpret_cast<void*>(consumer));

  if (consumer->tracing_session_id_)
    FreeBuffers(consumer->tracing_session_id_);  // Will also DisableTracing().

  // At this point no more pointers to |consumer| should be around.
  PERFETTO_DCHECK(!std::any_of(
      tracing_sessions_.begin(), tracing_sessions_.end(),
      [consumer](const std::pair<const TracingSessionID, TracingSession>& kv) {
        return kv.second.consumer_maybe_null == consumer;
      }));
}

bool TracingServiceImpl::DetachConsumer(ConsumerEndpointImpl* consumer,
                                        const std::string& key) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  PERFETTO_DLOG("Consumer %p detached", reinterpret_cast<void*>(consumer));

  TracingSessionID tsid = consumer->tracing_session_id_;
  TracingSession* tracing_session;
  if (!tsid || !(tracing_session = GetTracingSession(tsid)))
    return false;

  if (GetDetachedSession(consumer->uid_, key)) {
    PERFETTO_ELOG("Another session has been detached with the same key \"%s\"",
                  key.c_str());
    return false;
  }

  PERFETTO_DCHECK(tracing_session->consumer_maybe_null == consumer);
  tracing_session->consumer_maybe_null = nullptr;
  tracing_session->detach_key = key;
  consumer->tracing_session_id_ = 0;
  return true;
}

std::unique_ptr<TracingService::RelayEndpoint>
TracingServiceImpl::ConnectRelayClient(RelayClientID relay_client_id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);

  auto endpoint = std::make_unique<RelayEndpointImpl>(relay_client_id, this);
  relay_clients_[relay_client_id] = endpoint.get();

  return std::move(endpoint);
}

void TracingServiceImpl::DisconnectRelayClient(RelayClientID relay_client_id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);

  if (relay_clients_.find(relay_client_id) == relay_clients_.end())
    return;
  relay_clients_.erase(relay_client_id);
}

bool TracingServiceImpl::AttachConsumer(ConsumerEndpointImpl* consumer,
                                        const std::string& key) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  PERFETTO_DLOG("Consumer %p attaching to session %s",
                reinterpret_cast<void*>(consumer), key.c_str());

  if (consumer->tracing_session_id_) {
    PERFETTO_ELOG(
        "Cannot reattach consumer to session %s"
        " while it already attached tracing session ID %" PRIu64,
        key.c_str(), consumer->tracing_session_id_);
    return false;
  }

  auto* tracing_session = GetDetachedSession(consumer->uid_, key);
  if (!tracing_session) {
    PERFETTO_ELOG(
        "Failed to attach consumer, session '%s' not found for uid %d",
        key.c_str(), static_cast<int>(consumer->uid_));
    return false;
  }

  consumer->tracing_session_id_ = tracing_session->id;
  tracing_session->consumer_maybe_null = consumer;
  tracing_session->detach_key.clear();
  return true;
}

base::Status TracingServiceImpl::EnableTracing(ConsumerEndpointImpl* consumer,
                                               const TraceConfig& cfg,
                                               base::ScopedFile fd) {
  PERFETTO_DCHECK_THREAD(thread_checker_);

  // If the producer is specifying a UUID, respect that (at least for the first
  // snapshot). Otherwise generate a new UUID.
  base::Uuid uuid(cfg.trace_uuid_lsb(), cfg.trace_uuid_msb());
  if (!uuid)
    uuid = base::Uuidv4();

  PERFETTO_DLOG("Enabling tracing for consumer %p, UUID: %s",
                reinterpret_cast<void*>(consumer),
                uuid.ToPrettyString().c_str());
  MaybeLogUploadEvent(cfg, uuid, PerfettoStatsdAtom::kTracedEnableTracing);
  if (cfg.lockdown_mode() == TraceConfig::LOCKDOWN_SET)
    lockdown_mode_ = true;
  if (cfg.lockdown_mode() == TraceConfig::LOCKDOWN_CLEAR)
    lockdown_mode_ = false;

  // Scope |tracing_session| to this block to prevent accidental use of a null
  // pointer later in this function.
  {
    TracingSession* tracing_session =
        GetTracingSession(consumer->tracing_session_id_);
    if (tracing_session) {
      MaybeLogUploadEvent(
          cfg, uuid,
          PerfettoStatsdAtom::kTracedEnableTracingExistingTraceSession);
      return PERFETTO_SVC_ERR(
          "A Consumer is trying to EnableTracing() but another tracing "
          "session is already active (forgot a call to FreeBuffers() ?)");
    }
  }

  const uint32_t max_duration_ms = cfg.enable_extra_guardrails()
                                       ? kGuardrailsMaxTracingDurationMillis
                                       : kMaxTracingDurationMillis;
  if (cfg.duration_ms() > max_duration_ms) {
    MaybeLogUploadEvent(cfg, uuid,
                        PerfettoStatsdAtom::kTracedEnableTracingTooLongTrace);
    return PERFETTO_SVC_ERR("Requested too long trace (%" PRIu32
                            "ms  > %" PRIu32 " ms)",
                            cfg.duration_ms(), max_duration_ms);
  }

  const bool has_trigger_config =
      GetTriggerMode(cfg) != TraceConfig::TriggerConfig::UNSPECIFIED;
  if (has_trigger_config &&
      (cfg.trigger_config().trigger_timeout_ms() == 0 ||
       cfg.trigger_config().trigger_timeout_ms() > max_duration_ms)) {
    MaybeLogUploadEvent(
        cfg, uuid,
        PerfettoStatsdAtom::kTracedEnableTracingInvalidTriggerTimeout);
    return PERFETTO_SVC_ERR(
        "Traces with START_TRACING triggers must provide a positive "
        "trigger_timeout_ms < 7 days (received %" PRIu32 "ms)",
        cfg.trigger_config().trigger_timeout_ms());
  }

  // This check has been introduced in May 2023 after finding b/274931668.
  if (static_cast<int>(cfg.trigger_config().trigger_mode()) >
      TraceConfig::TriggerConfig::TriggerMode_MAX) {
    MaybeLogUploadEvent(
        cfg, uuid, PerfettoStatsdAtom::kTracedEnableTracingInvalidTriggerMode);
    return PERFETTO_SVC_ERR(
        "The trace config specified an invalid trigger_mode");
  }

  if (cfg.trigger_config().use_clone_snapshot_if_available() &&
      cfg.trigger_config().trigger_mode() !=
          TraceConfig::TriggerConfig::STOP_TRACING) {
    MaybeLogUploadEvent(
        cfg, uuid, PerfettoStatsdAtom::kTracedEnableTracingInvalidTriggerMode);
    return PERFETTO_SVC_ERR(
        "trigger_mode must be STOP_TRACING when "
        "use_clone_snapshot_if_available=true");
  }

  if (has_trigger_config && cfg.duration_ms() != 0) {
    MaybeLogUploadEvent(
        cfg, uuid, PerfettoStatsdAtom::kTracedEnableTracingDurationWithTrigger);
    return PERFETTO_SVC_ERR(
        "duration_ms was set, this must not be set for traces with triggers.");
  }

  for (char c : cfg.bugreport_filename()) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) {
      MaybeLogUploadEvent(
          cfg, uuid, PerfettoStatsdAtom::kTracedEnableTracingInvalidBrFilename);
      return PERFETTO_SVC_ERR(
          "bugreport_filename contains invalid chars. Use [a-zA-Z0-9-_.]+");
    }
  }

  if ((GetTriggerMode(cfg) == TraceConfig::TriggerConfig::STOP_TRACING ||
       GetTriggerMode(cfg) == TraceConfig::TriggerConfig::CLONE_SNAPSHOT) &&
      cfg.write_into_file()) {
    // We don't support this usecase because there are subtle assumptions which
    // break around TracingServiceEvents and windowed sorting (i.e. if we don't
    // drain the events in ReadBuffersIntoFile because we are waiting for
    // STOP_TRACING, we can end up queueing up a lot of TracingServiceEvents and
    // emitting them wildy out of order breaking windowed sorting in trace
    // processor).
    MaybeLogUploadEvent(
        cfg, uuid,
        PerfettoStatsdAtom::kTracedEnableTracingStopTracingWriteIntoFile);
    return PERFETTO_SVC_ERR(
        "Specifying trigger mode STOP_TRACING/CLONE_SNAPSHOT and "
        "write_into_file together is unsupported");
  }

  std::unordered_set<std::string> triggers;
  for (const auto& trigger : cfg.trigger_config().triggers()) {
    if (!triggers.insert(trigger.name()).second) {
      MaybeLogUploadEvent(
          cfg, uuid,
          PerfettoStatsdAtom::kTracedEnableTracingDuplicateTriggerName);
      return PERFETTO_SVC_ERR("Duplicate trigger name: %s",
                              trigger.name().c_str());
    }
  }

  if (cfg.enable_extra_guardrails()) {
    if (cfg.deferred_start()) {
      MaybeLogUploadEvent(
          cfg, uuid,
          PerfettoStatsdAtom::kTracedEnableTracingInvalidDeferredStart);
      return PERFETTO_SVC_ERR(
          "deferred_start=true is not supported in unsupervised traces");
    }
    uint64_t buf_size_sum = 0;
    for (const auto& buf : cfg.buffers()) {
      if (buf.size_kb() % 4 != 0) {
        MaybeLogUploadEvent(
            cfg, uuid,
            PerfettoStatsdAtom::kTracedEnableTracingInvalidBufferSize);
        return PERFETTO_SVC_ERR(
            "buffers.size_kb must be a multiple of 4, got %" PRIu32,
            buf.size_kb());
      }
      buf_size_sum += buf.size_kb();
    }

    uint32_t max_tracing_buffer_size_kb =
        std::max(kGuardrailsMaxTracingBufferSizeKb,
                 cfg.guardrail_overrides().max_tracing_buffer_size_kb());
    if (buf_size_sum > max_tracing_buffer_size_kb) {
      MaybeLogUploadEvent(
          cfg, uuid,
          PerfettoStatsdAtom::kTracedEnableTracingBufferSizeTooLarge);
      return PERFETTO_SVC_ERR("Requested too large trace buffer (%" PRIu64
                              "kB  > %" PRIu32 " kB)",
                              buf_size_sum, max_tracing_buffer_size_kb);
    }
  }

  if (cfg.buffers_size() > kMaxBuffersPerConsumer) {
    MaybeLogUploadEvent(cfg, uuid,
                        PerfettoStatsdAtom::kTracedEnableTracingTooManyBuffers);
    return PERFETTO_SVC_ERR("Too many buffers configured (%d)",
                            cfg.buffers_size());
  }
  // Check that the config specifies all buffers for its data sources. This
  // is also checked in SetupDataSource, but it is simpler to return a proper
  // error to the consumer from here (and there will be less state to undo).
  for (const TraceConfig::DataSource& cfg_data_source : cfg.data_sources()) {
    size_t num_buffers = static_cast<size_t>(cfg.buffers_size());
    size_t target_buffer = cfg_data_source.config().target_buffer();
    if (target_buffer >= num_buffers) {
      MaybeLogUploadEvent(
          cfg, uuid, PerfettoStatsdAtom::kTracedEnableTracingOobTargetBuffer);
      return PERFETTO_SVC_ERR(
          "Data source \"%s\" specified an out of bounds target_buffer (%zu >= "
          "%zu)",
          cfg_data_source.config().name().c_str(), target_buffer, num_buffers);
    }
  }

  if (!cfg.unique_session_name().empty()) {
    const std::string& name = cfg.unique_session_name();
    for (auto& kv : tracing_sessions_) {
      if (kv.second.state == TracingSession::CLONED_READ_ONLY)
        continue;  // Don't consider cloned sessions in uniqueness checks.
      if (kv.second.config.unique_session_name() == name) {
        MaybeLogUploadEvent(
            cfg, uuid,
            PerfettoStatsdAtom::kTracedEnableTracingDuplicateSessionName);
        static const char fmt[] =
            "A trace with this unique session name (%s) already exists";
        // This happens frequently, don't make it an "E"LOG.
        PERFETTO_LOG(fmt, name.c_str());
        return base::ErrStatus(fmt, name.c_str());
      }
    }
  }

  if (!cfg.session_semaphores().empty()) {
    struct SemaphoreSessionsState {
      uint64_t smallest_max_other_session_count =
          std::numeric_limits<uint64_t>::max();
      uint64_t session_count = 0;
    };
    // For each semaphore, compute the number of active sessions and the
    // MIN(limit).
    std::unordered_map<std::string, SemaphoreSessionsState>
        sem_to_sessions_state;
    for (const auto& id_and_session : tracing_sessions_) {
      const auto& session = id_and_session.second;
      if (session.state == TracingSession::CLONED_READ_ONLY ||
          session.state == TracingSession::DISABLED) {
        // Don't consider cloned or disabled sessions in checks.
        continue;
      }
      for (const auto& sem : session.config.session_semaphores()) {
        auto& sessions_state = sem_to_sessions_state[sem.name()];
        sessions_state.smallest_max_other_session_count =
            std::min(sessions_state.smallest_max_other_session_count,
                     sem.max_other_session_count());
        sessions_state.session_count++;
      }
    }

    // Check if any of the semaphores declared by the config clashes with any of
    // the currently active semaphores.
    for (const auto& semaphore : cfg.session_semaphores()) {
      auto it = sem_to_sessions_state.find(semaphore.name());
      if (it == sem_to_sessions_state.end()) {
        continue;
      }
      uint64_t max_other_session_count =
          std::min(semaphore.max_other_session_count(),
                   it->second.smallest_max_other_session_count);
      if (it->second.session_count > max_other_session_count) {
        MaybeLogUploadEvent(
            cfg, uuid,
            PerfettoStatsdAtom::
                kTracedEnableTracingFailedSessionSemaphoreCheck);
        return PERFETTO_SVC_ERR(
            "Semaphore \"%s\" exceeds maximum allowed other session count "
            "(%" PRIu64 " > min(%" PRIu64 ", %" PRIu64 "))",
            semaphore.name().c_str(), it->second.session_count,
            semaphore.max_other_session_count(),
            it->second.smallest_max_other_session_count);
      }
    }
  }

  if (cfg.enable_extra_guardrails()) {
    // unique_session_name can be empty
    const std::string& name = cfg.unique_session_name();
    int64_t now_s = clock_->GetBootTimeS().count();

    // Remove any entries where the time limit has passed so this map doesn't
    // grow indefinitely:
    std::map<std::string, int64_t>& sessions = session_to_last_trace_s_;
    for (auto it = sessions.cbegin(); it != sessions.cend();) {
      if (now_s - it->second > kMinSecondsBetweenTracesGuardrail) {
        it = sessions.erase(it);
      } else {
        ++it;
      }
    }

    int64_t& previous_s = session_to_last_trace_s_[name];
    if (previous_s == 0) {
      previous_s = now_s;
    } else {
      MaybeLogUploadEvent(
          cfg, uuid,
          PerfettoStatsdAtom::kTracedEnableTracingSessionNameTooRecent);
      return PERFETTO_SVC_ERR(
          "A trace with unique session name \"%s\" began less than %" PRId64
          "s ago (%" PRId64 "s)",
          name.c_str(), kMinSecondsBetweenTracesGuardrail, now_s - previous_s);
    }
  }

  const int sessions_for_uid = static_cast<int>(std::count_if(
      tracing_sessions_.begin(), tracing_sessions_.end(),
      [consumer](const decltype(tracing_sessions_)::value_type& s) {
        return s.second.consumer_uid == consumer->uid_;
      }));

  int per_uid_limit = kMaxConcurrentTracingSessionsPerUid;
  if (consumer->uid_ == 1066 /* AID_STATSD*/) {
    per_uid_limit = kMaxConcurrentTracingSessionsForStatsdUid;
  }
  if (sessions_for_uid >= per_uid_limit) {
    MaybeLogUploadEvent(
        cfg, uuid,
        PerfettoStatsdAtom::kTracedEnableTracingTooManySessionsForUid);
    return PERFETTO_SVC_ERR(
        "Too many concurrent tracing sesions (%d) for uid %d limit is %d",
        sessions_for_uid, static_cast<int>(consumer->uid_), per_uid_limit);
  }

  // TODO(primiano): This is a workaround to prevent that a producer gets stuck
  // in a state where it stalls by design by having more TraceWriterImpl
  // instances than free pages in the buffer. This is really a bug in
  // trace_probes and the way it handles stalls in the shmem buffer.
  if (tracing_sessions_.size() >= kMaxConcurrentTracingSessions) {
    MaybeLogUploadEvent(
        cfg, uuid,
        PerfettoStatsdAtom::kTracedEnableTracingTooManyConcurrentSessions);
    return PERFETTO_SVC_ERR("Too many concurrent tracing sesions (%zu)",
                            tracing_sessions_.size());
  }

  // If the trace config provides a filter bytecode, setup the filter now.
  // If the filter loading fails, abort the tracing session rather than running
  // unfiltered.
  std::unique_ptr<protozero::MessageFilter> trace_filter;
  if (cfg.has_trace_filter()) {
    const auto& filt = cfg.trace_filter();
    trace_filter.reset(new protozero::MessageFilter());

    protozero::StringFilter& string_filter = trace_filter->string_filter();
    for (const auto& rule : filt.string_filter_chain().rules()) {
      auto opt_policy = ConvertPolicy(rule.policy());
      if (!opt_policy.has_value()) {
        MaybeLogUploadEvent(
            cfg, uuid, PerfettoStatsdAtom::kTracedEnableTracingInvalidFilter);
        return PERFETTO_SVC_ERR(
            "Trace filter has invalid string filtering rules, aborting");
      }
      string_filter.AddRule(*opt_policy, rule.regex_pattern(),
                            rule.atrace_payload_starts_with());
    }

    const std::string& bytecode_v1 = filt.bytecode();
    const std::string& bytecode_v2 = filt.bytecode_v2();
    const std::string& bytecode =
        bytecode_v2.empty() ? bytecode_v1 : bytecode_v2;
    if (!trace_filter->LoadFilterBytecode(bytecode.data(), bytecode.size())) {
      MaybeLogUploadEvent(
          cfg, uuid, PerfettoStatsdAtom::kTracedEnableTracingInvalidFilter);
      return PERFETTO_SVC_ERR("Trace filter bytecode invalid, aborting");
    }

    // The filter is created using perfetto.protos.Trace as root message
    // (because that makes it possible to play around with the `proto_filter`
    // tool on actual traces). Here in the service, however, we deal with
    // perfetto.protos.TracePacket(s), which are one level down (Trace.packet).
    // The IPC client (or the write_into_filte logic in here) are responsible
    // for pre-pending the packet preamble (See GetProtoPreamble() calls), but
    // the preamble is not there at ReadBuffer time. Hence we change the root of
    // the filtering to start at the Trace.packet level.
    if (!trace_filter->SetFilterRoot({TracePacket::kPacketFieldNumber})) {
      MaybeLogUploadEvent(
          cfg, uuid, PerfettoStatsdAtom::kTracedEnableTracingInvalidFilter);
      return PERFETTO_SVC_ERR("Failed to set filter root.");
    }
  }

  const TracingSessionID tsid = ++last_tracing_session_id_;
  TracingSession* tracing_session =
      &tracing_sessions_
           .emplace(std::piecewise_construct, std::forward_as_tuple(tsid),
                    std::forward_as_tuple(tsid, consumer, cfg,
                                          weak_runner_.task_runner()))
           .first->second;

  tracing_session->trace_uuid = uuid;

  if (trace_filter)
    tracing_session->trace_filter = std::move(trace_filter);

  if (cfg.write_into_file()) {
    if (!fd ^ !cfg.output_path().empty()) {
      MaybeLogUploadEvent(
          tracing_session->config, uuid,
          PerfettoStatsdAtom::kTracedEnableTracingInvalidFdOutputFile);
      tracing_sessions_.erase(tsid);
      return PERFETTO_SVC_ERR(
          "When write_into_file==true either a FD needs to be passed or "
          "output_path must be populated (but not both)");
    }
    if (!cfg.output_path().empty()) {
      fd = CreateTraceFile(cfg.output_path(), /*overwrite=*/false);
      if (!fd) {
        MaybeLogUploadEvent(
            tracing_session->config, uuid,
            PerfettoStatsdAtom::kTracedEnableTracingFailedToCreateFile);
        tracing_sessions_.erase(tsid);
        return PERFETTO_SVC_ERR("Failed to create the trace file %s",
                                cfg.output_path().c_str());
      }
    }
    tracing_session->write_into_file = std::move(fd);
    uint32_t write_period_ms = cfg.file_write_period_ms();
    if (write_period_ms == 0)
      write_period_ms = kDefaultWriteIntoFilePeriodMs;
    if (write_period_ms < kMinWriteIntoFilePeriodMs)
      write_period_ms = kMinWriteIntoFilePeriodMs;
    tracing_session->write_period_ms = write_period_ms;
    tracing_session->max_file_size_bytes = cfg.max_file_size_bytes();
    tracing_session->bytes_written_into_file = 0;
  }

  if (cfg.compression_type() == TraceConfig::COMPRESSION_TYPE_DEFLATE) {
    if (init_opts_.compressor_fn) {
      tracing_session->compress_deflate = true;
    } else {
      PERFETTO_LOG(
          "COMPRESSION_TYPE_DEFLATE is not supported in the current build "
          "configuration. Skipping compression");
    }
  }

  // Initialize the log buffers.
  bool did_allocate_all_buffers = true;
  bool invalid_buffer_config = false;

  // Allocate the trace buffers. Also create a map to translate a consumer
  // relative index (TraceConfig.DataSourceConfig.target_buffer) into the
  // corresponding BufferID, which is a global ID namespace for the service and
  // all producers.
  size_t total_buf_size_kb = 0;
  const size_t num_buffers = static_cast<size_t>(cfg.buffers_size());
  tracing_session->buffers_index.reserve(num_buffers);
  for (size_t i = 0; i < num_buffers; i++) {
    const TraceConfig::BufferConfig& buffer_cfg = cfg.buffers()[i];
    BufferID global_id = buffer_ids_.Allocate();
    if (!global_id) {
      did_allocate_all_buffers = false;  // We ran out of IDs.
      break;
    }
    tracing_session->buffers_index.push_back(global_id);
    // TraceBuffer size is limited to 32-bit.
    const uint32_t buf_size_kb = buffer_cfg.size_kb();
    const uint64_t buf_size_bytes = buf_size_kb * static_cast<uint64_t>(1024);
    const size_t buf_size = static_cast<size_t>(buf_size_bytes);
    if (buf_size_bytes == 0 ||
        buf_size_bytes > std::numeric_limits<uint32_t>::max() ||
        buf_size != buf_size_bytes) {
      invalid_buffer_config = true;
      did_allocate_all_buffers = false;
      break;
    }
    total_buf_size_kb += buf_size_kb;
    TraceBuffer::OverwritePolicy policy =
        buffer_cfg.fill_policy() == TraceConfig::BufferConfig::DISCARD
            ? TraceBuffer::kDiscard
            : TraceBuffer::kOverwrite;
    auto it_and_inserted =
        buffers_.emplace(global_id, TraceBuffer::Create(buf_size, policy));
    PERFETTO_DCHECK(it_and_inserted.second);  // buffers_.count(global_id) == 0.
    std::unique_ptr<TraceBuffer>& trace_buffer = it_and_inserted.first->second;
    if (!trace_buffer) {
      did_allocate_all_buffers = false;
      break;
    }
  }

  // This can happen if either:
  // - All the kMaxTraceBufferID slots are taken.
  // - OOM, or, more realistically, we exhausted virtual memory.
  // - The buffer size in the config is invalid.
  // In any case, free all the previously allocated buffers and abort.
  if (!did_allocate_all_buffers) {
    for (BufferID global_id : tracing_session->buffers_index) {
      buffer_ids_.Free(global_id);
      buffers_.erase(global_id);
    }
    MaybeLogUploadEvent(tracing_session->config, uuid,
                        PerfettoStatsdAtom::kTracedEnableTracingOom);
    tracing_sessions_.erase(tsid);
    if (invalid_buffer_config) {
      return PERFETTO_SVC_ERR(
          "Failed to allocate tracing buffers: Invalid buffer sizes");
    }
    return PERFETTO_SVC_ERR(
        "Failed to allocate tracing buffers: OOM or too many buffers");
  }

  UpdateMemoryGuardrail();

  consumer->tracing_session_id_ = tsid;

  // Setup the data sources on the producers without starting them.
  for (const TraceConfig::DataSource& cfg_data_source : cfg.data_sources()) {
    // Scan all the registered data sources with a matching name.
    auto range = data_sources_.equal_range(cfg_data_source.config().name());
    for (auto it = range.first; it != range.second; it++) {
      TraceConfig::ProducerConfig producer_config;
      for (const auto& config : cfg.producers()) {
        if (GetProducer(it->second.producer_id)->name_ ==
            config.producer_name()) {
          producer_config = config;
          break;
        }
      }
      SetupDataSource(cfg_data_source, producer_config, it->second,
                      tracing_session);
    }
  }

  bool has_start_trigger = false;
  switch (GetTriggerMode(cfg)) {
    case TraceConfig::TriggerConfig::UNSPECIFIED:
      // no triggers are specified so this isn't a trace that is using triggers.
      PERFETTO_DCHECK(!has_trigger_config);
      break;
    case TraceConfig::TriggerConfig::START_TRACING:
      // For traces which use START_TRACE triggers we need to ensure that the
      // tracing session will be cleaned up when it times out.
      has_start_trigger = true;
      weak_runner_.PostDelayedTask(
          [tsid, this]() { OnStartTriggersTimeout(tsid); },
          cfg.trigger_config().trigger_timeout_ms());
      break;
    case TraceConfig::TriggerConfig::STOP_TRACING:
    case TraceConfig::TriggerConfig::CLONE_SNAPSHOT:
      // Update the tracing_session's duration_ms to ensure that if no trigger
      // is received the session will end and be cleaned up equal to the
      // timeout.
      //
      // TODO(nuskos): Refactor this so that rather then modifying the config we
      // have a field we look at on the tracing_session.
      tracing_session->config.set_duration_ms(
          cfg.trigger_config().trigger_timeout_ms());
      break;

      // The case of unknown modes (coming from future versions of the service)
      // is handled few lines above (search for TriggerMode_MAX).
  }

  tracing_session->state = TracingSession::CONFIGURED;
  PERFETTO_LOG(
      "Configured tracing session %" PRIu64
      ", #sources:%zu, duration:%u ms%s, #buffers:%d, total "
      "buffer size:%zu KB, total sessions:%zu, uid:%u session name: \"%s\"",
      tsid, cfg.data_sources().size(), tracing_session->config.duration_ms(),
      tracing_session->config.prefer_suspend_clock_for_duration()
          ? " (suspend_clock)"
          : "",
      cfg.buffers_size(), total_buf_size_kb, tracing_sessions_.size(),
      static_cast<unsigned int>(consumer->uid_),
      cfg.unique_session_name().c_str());

  // Start the data sources, unless this is a case of early setup + fast
  // triggering, either through TraceConfig.deferred_start or
  // TraceConfig.trigger_config(). If both are specified which ever one occurs
  // first will initiate the trace.
  if (!cfg.deferred_start() && !has_start_trigger)
    StartTracing(tsid);

  return base::OkStatus();
}

void TracingServiceImpl::ChangeTraceConfig(ConsumerEndpointImpl* consumer,
                                           const TraceConfig& updated_cfg) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  TracingSession* tracing_session =
      GetTracingSession(consumer->tracing_session_id_);
  PERFETTO_DCHECK(tracing_session);

  if ((tracing_session->state != TracingSession::STARTED) &&
      (tracing_session->state != TracingSession::CONFIGURED)) {
    PERFETTO_ELOG(
        "ChangeTraceConfig() was called for a tracing session which isn't "
        "running.");
    return;
  }

  // We only support updating producer_name_{,regex}_filter (and pass-through
  // configs) for now; null out any changeable fields and make sure the rest are
  // identical.
  TraceConfig new_config_copy(updated_cfg);
  for (auto& ds_cfg : *new_config_copy.mutable_data_sources()) {
    ds_cfg.clear_producer_name_filter();
    ds_cfg.clear_producer_name_regex_filter();
  }

  TraceConfig current_config_copy(tracing_session->config);
  for (auto& ds_cfg : *current_config_copy.mutable_data_sources()) {
    ds_cfg.clear_producer_name_filter();
    ds_cfg.clear_producer_name_regex_filter();
  }

  if (new_config_copy != current_config_copy) {
    PERFETTO_LOG(
        "ChangeTraceConfig() was called with a config containing unsupported "
        "changes; only adding to the producer_name_{,regex}_filter is "
        "currently supported and will have an effect.");
  }

  for (TraceConfig::DataSource& cfg_data_source :
       *tracing_session->config.mutable_data_sources()) {
    // Find the updated producer_filter in the new config.
    std::vector<std::string> new_producer_name_filter;
    std::vector<std::string> new_producer_name_regex_filter;
    bool found_data_source = false;
    for (const auto& it : updated_cfg.data_sources()) {
      if (cfg_data_source.config().name() == it.config().name()) {
        new_producer_name_filter = it.producer_name_filter();
        new_producer_name_regex_filter = it.producer_name_regex_filter();
        found_data_source = true;
        break;
      }
    }

    // Bail out if data source not present in the new config.
    if (!found_data_source) {
      PERFETTO_ELOG(
          "ChangeTraceConfig() called without a current data source also "
          "present in the new config: %s",
          cfg_data_source.config().name().c_str());
      continue;
    }

    // TODO(oysteine): Just replacing the filter means that if
    // there are any filter entries which were present in the original config,
    // but removed from the config passed to ChangeTraceConfig, any matching
    // producers will keep producing but newly added producers after this
    // point will never start.
    *cfg_data_source.mutable_producer_name_filter() = new_producer_name_filter;
    *cfg_data_source.mutable_producer_name_regex_filter() =
        new_producer_name_regex_filter;

    // Get the list of producers that are already set up.
    std::unordered_set<uint16_t> set_up_producers;
    auto& ds_instances = tracing_session->data_source_instances;
    for (auto instance_it = ds_instances.begin();
         instance_it != ds_instances.end(); ++instance_it) {
      set_up_producers.insert(instance_it->first);
    }

    // Scan all the registered data sources with a matching name.
    auto range = data_sources_.equal_range(cfg_data_source.config().name());
    for (auto it = range.first; it != range.second; it++) {
      ProducerEndpointImpl* producer = GetProducer(it->second.producer_id);
      PERFETTO_DCHECK(producer);

      // Check if the producer name of this data source is present
      // in the name filters. We currently only support new filters, not
      // removing old ones.
      if (!NameMatchesFilter(producer->name_, new_producer_name_filter,
                             new_producer_name_regex_filter)) {
        continue;
      }

      // If this producer is already set up, we assume that all datasources
      // in it started already.
      if (set_up_producers.count(it->second.producer_id))
        continue;

      // If it wasn't previously setup, set it up now.
      // (The per-producer config is optional).
      TraceConfig::ProducerConfig producer_config;
      for (const auto& config : tracing_session->config.producers()) {
        if (producer->name_ == config.producer_name()) {
          producer_config = config;
          break;
        }
      }

      DataSourceInstance* ds_inst = SetupDataSource(
          cfg_data_source, producer_config, it->second, tracing_session);

      if (ds_inst && tracing_session->state == TracingSession::STARTED)
        StartDataSourceInstance(producer, tracing_session, ds_inst);
    }
  }
}

uint32_t TracingServiceImpl::DelayToNextWritePeriodMs(
    const TracingSession& session) {
  PERFETTO_DCHECK(session.write_period_ms > 0);
  return session.write_period_ms -
         static_cast<uint32_t>(clock_->GetWallTimeMs().count() %
                               session.write_period_ms);
}

void TracingServiceImpl::StartTracing(TracingSessionID tsid) {
  PERFETTO_DCHECK_THREAD(thread_checker_);

  TracingSession* tracing_session = GetTracingSession(tsid);
  if (!tracing_session) {
    PERFETTO_ELOG("StartTracing() failed, invalid session ID %" PRIu64, tsid);
    return;
  }

  MaybeLogUploadEvent(tracing_session->config, tracing_session->trace_uuid,
                      PerfettoStatsdAtom::kTracedStartTracing);

  if (tracing_session->state != TracingSession::CONFIGURED) {
    MaybeLogUploadEvent(
        tracing_session->config, tracing_session->trace_uuid,
        PerfettoStatsdAtom::kTracedStartTracingInvalidSessionState);
    PERFETTO_ELOG("StartTracing() failed, invalid session state: %d",
                  tracing_session->state);
    return;
  }

  tracing_session->state = TracingSession::STARTED;

  // We store the start of trace snapshot separately as it's important to make
  // sure we can interpret all the data in the trace and storing it in the ring
  // buffer means it could be overwritten by a later snapshot.
  if (!tracing_session->config.builtin_data_sources()
           .disable_clock_snapshotting()) {
    SnapshotClocks(&tracing_session->initial_clock_snapshot);
  }

  // We don't snapshot the clocks here because we just did this above.
  SnapshotLifecycleEvent(
      tracing_session,
      protos::pbzero::TracingServiceEvent::kTracingStartedFieldNumber,
      false /* snapshot_clocks */);

  // Periodically snapshot clocks, stats, sync markers while the trace is
  // active. The snapshots are emitted on the future ReadBuffers() calls, which
  // means that:
  //  (a) If we're streaming to a file (or to a consumer) while tracing, we
  //      write snapshots periodically into the trace.
  //  (b) If ReadBuffers() is only called after tracing ends, we emit the latest
  //      snapshot into the trace. For clock snapshots, we keep track of the
  //      snapshot recorded at the beginning of the session
  //      (initial_clock_snapshot above), as well as the most recent sampled
  //      snapshots that showed significant new drift between different clocks.
  //      The latter clock snapshots are sampled periodically and at lifecycle
  //      events.
  base::PeriodicTask::Args snapshot_task_args;
  snapshot_task_args.start_first_task_immediately = true;
  snapshot_task_args.use_suspend_aware_timer =
      tracing_session->config.builtin_data_sources()
          .prefer_suspend_clock_for_snapshot();
  snapshot_task_args.task = [this, tsid] { PeriodicSnapshotTask(tsid); };
  snapshot_task_args.period_ms =
      tracing_session->config.builtin_data_sources().snapshot_interval_ms();
  if (!snapshot_task_args.period_ms)
    snapshot_task_args.period_ms = kDefaultSnapshotsIntervalMs;
  tracing_session->snapshot_periodic_task.Start(snapshot_task_args);

  // Trigger delayed task if the trace is time limited.
  const uint32_t trace_duration_ms = tracing_session->config.duration_ms();
  if (trace_duration_ms > 0) {
    auto stop_task =
        std::bind(&TracingServiceImpl::StopOnDurationMsExpiry, this, tsid);
    if (tracing_session->config.prefer_suspend_clock_for_duration()) {
      base::PeriodicTask::Args stop_args;
      stop_args.use_suspend_aware_timer = true;
      stop_args.period_ms = trace_duration_ms;
      stop_args.one_shot = true;
      stop_args.task = std::move(stop_task);
      tracing_session->timed_stop_task.Start(stop_args);
    } else {
      weak_runner_.PostDelayedTask(std::move(stop_task), trace_duration_ms);
    }
  }  // if (trace_duration_ms > 0).

  // Start the periodic drain tasks if we should to save the trace into a file.
  if (tracing_session->config.write_into_file()) {
    weak_runner_.PostDelayedTask([this, tsid] { ReadBuffersIntoFile(tsid); },
                                 DelayToNextWritePeriodMs(*tracing_session));
  }

  // Start the periodic flush tasks if the config specified a flush period.
  if (tracing_session->config.flush_period_ms())
    PeriodicFlushTask(tsid, /*post_next_only=*/true);

  // Start the periodic incremental state clear tasks if the config specified a
  // period.
  if (tracing_session->config.incremental_state_config().clear_period_ms()) {
    PeriodicClearIncrementalStateTask(tsid, /*post_next_only=*/true);
  }

  for (auto& [prod_id, data_source] : tracing_session->data_source_instances) {
    ProducerEndpointImpl* producer = GetProducer(prod_id);
    if (!producer) {
      PERFETTO_DFATAL("Producer does not exist.");
      continue;
    }
    StartDataSourceInstance(producer, tracing_session, &data_source);
  }

  MaybeNotifyAllDataSourcesStarted(tracing_session);

  // `did_notify_all_data_source_started` is only set if a consumer is
  // connected.
  if (tracing_session->consumer_maybe_null) {
    weak_runner_.PostDelayedTask(
        [this, tsid] { OnAllDataSourceStartedTimeout(tsid); },
        kAllDataSourceStartedTimeout);
  }
}

void TracingServiceImpl::StopOnDurationMsExpiry(TracingSessionID tsid) {
  auto* tracing_session_ptr = GetTracingSession(tsid);
  if (!tracing_session_ptr)
    return;
  // If this trace was using STOP_TRACING triggers and we've seen
  // one, then the trigger overrides the normal timeout. In this
  // case we just return and let the other task clean up this trace.
  if (GetTriggerMode(tracing_session_ptr->config) ==
          TraceConfig::TriggerConfig::STOP_TRACING &&
      !tracing_session_ptr->received_triggers.empty())
    return;
  // In all other cases (START_TRACING or no triggers) we flush
  // after |trace_duration_ms| unconditionally.
  FlushAndDisableTracing(tsid);
}

void TracingServiceImpl::StartDataSourceInstance(
    ProducerEndpointImpl* producer,
    TracingSession* tracing_session,
    TracingServiceImpl::DataSourceInstance* instance) {
  PERFETTO_DCHECK(instance->state == DataSourceInstance::CONFIGURED);

  bool start_immediately = !instance->will_notify_on_start;

  if (producer->IsAndroidProcessFrozen()) {
    PERFETTO_DLOG(
        "skipping waiting of data source \"%s\" on producer \"%s\" (pid=%u) "
        "because it is frozen",
        instance->data_source_name.c_str(), producer->name_.c_str(),
        producer->pid());
    start_immediately = true;
  }

  if (!start_immediately) {
    instance->state = DataSourceInstance::STARTING;
  } else {
    instance->state = DataSourceInstance::STARTED;
  }
  if (tracing_session->consumer_maybe_null) {
    tracing_session->consumer_maybe_null->OnDataSourceInstanceStateChange(
        *producer, *instance);
  }
  producer->StartDataSource(instance->instance_id, instance->config);

  // If all data sources are started, notify the consumer.
  if (instance->state == DataSourceInstance::STARTED)
    MaybeNotifyAllDataSourcesStarted(tracing_session);
}

// DisableTracing just stops the data sources but doesn't free up any buffer.
// This is to allow the consumer to freeze the buffers (by stopping the trace)
// and then drain the buffers. The actual teardown of the TracingSession happens
// in FreeBuffers().
void TracingServiceImpl::DisableTracing(TracingSessionID tsid,
                                        bool disable_immediately) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  TracingSession* tracing_session = GetTracingSession(tsid);
  if (!tracing_session) {
    // Can happen if the consumer calls this before EnableTracing() or after
    // FreeBuffers().
    PERFETTO_DLOG("DisableTracing() failed, invalid session ID %" PRIu64, tsid);
    return;
  }

  MaybeLogUploadEvent(tracing_session->config, tracing_session->trace_uuid,
                      PerfettoStatsdAtom::kTracedDisableTracing);

  switch (tracing_session->state) {
    // Spurious call to DisableTracing() while already disabled, nothing to do.
    case TracingSession::DISABLED:
      PERFETTO_DCHECK(tracing_session->AllDataSourceInstancesStopped());
      return;

    case TracingSession::CLONED_READ_ONLY:
      return;

    // This is either:
    // A) The case of a graceful DisableTracing() call followed by a call to
    //    FreeBuffers(), iff |disable_immediately| == true. In this case we want
    //    to forcefully transition in the disabled state without waiting for the
    //    outstanding acks because the buffers are going to be destroyed soon.
    // B) A spurious call, iff |disable_immediately| == false, in which case
    //    there is nothing to do.
    case TracingSession::DISABLING_WAITING_STOP_ACKS:
      PERFETTO_DCHECK(!tracing_session->AllDataSourceInstancesStopped());
      if (disable_immediately)
        DisableTracingNotifyConsumerAndFlushFile(tracing_session);
      return;

    // Continues below.
    case TracingSession::CONFIGURED:
      // If the session didn't even start there is no need to orchestrate a
      // graceful stop of data sources.
      disable_immediately = true;
      break;

    // This is the nominal case, continues below.
    case TracingSession::STARTED:
      break;
  }

  for (auto& data_source_inst : tracing_session->data_source_instances) {
    const ProducerID producer_id = data_source_inst.first;
    DataSourceInstance& instance = data_source_inst.second;
    ProducerEndpointImpl* producer = GetProducer(producer_id);
    PERFETTO_DCHECK(producer);
    PERFETTO_DCHECK(instance.state == DataSourceInstance::CONFIGURED ||
                    instance.state == DataSourceInstance::STARTING ||
                    instance.state == DataSourceInstance::STARTED);
    StopDataSourceInstance(producer, tracing_session, &instance,
                           disable_immediately);
  }

  // If the periodic task is running, we can stop the periodic snapshot timer
  // here instead of waiting until FreeBuffers to prevent useless snapshots
  // which won't be read.
  tracing_session->snapshot_periodic_task.Reset();

  // Either this request is flagged with |disable_immediately| or there are no
  // data sources that are requesting a final handshake. In both cases just mark
  // the session as disabled immediately, notify the consumer and flush the
  // trace file (if used).
  if (tracing_session->AllDataSourceInstancesStopped())
    return DisableTracingNotifyConsumerAndFlushFile(tracing_session);

  tracing_session->state = TracingSession::DISABLING_WAITING_STOP_ACKS;
  weak_runner_.PostDelayedTask([this, tsid] { OnDisableTracingTimeout(tsid); },
                               tracing_session->data_source_stop_timeout_ms());

  // Deliberately NOT removing the session from |tracing_session_|, it's still
  // needed to call ReadBuffers(). FreeBuffers() will erase() the session.
}

void TracingServiceImpl::NotifyDataSourceStarted(
    ProducerID producer_id,
    DataSourceInstanceID instance_id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  for (auto& kv : tracing_sessions_) {
    TracingSession& tracing_session = kv.second;
    DataSourceInstance* instance =
        tracing_session.GetDataSourceInstance(producer_id, instance_id);

    if (!instance)
      continue;

    // If the tracing session was already stopped, ignore this notification.
    if (tracing_session.state != TracingSession::STARTED)
      continue;

    if (instance->state != DataSourceInstance::STARTING) {
      PERFETTO_ELOG("Started data source instance in incorrect state: %d",
                    instance->state);
      continue;
    }

    instance->state = DataSourceInstance::STARTED;

    ProducerEndpointImpl* producer = GetProducer(producer_id);
    PERFETTO_DCHECK(producer);
    if (tracing_session.consumer_maybe_null) {
      tracing_session.consumer_maybe_null->OnDataSourceInstanceStateChange(
          *producer, *instance);
    }

    // If all data sources are started, notify the consumer.
    MaybeNotifyAllDataSourcesStarted(&tracing_session);
  }  // for (tracing_session)
}

void TracingServiceImpl::OnAllDataSourceStartedTimeout(TracingSessionID tsid) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  TracingSession* tracing_session = GetTracingSession(tsid);
  // It would be possible to check for `AllDataSourceInstancesStarted()` here,
  // but it doesn't make much sense, because a data source can be registered
  // after the session has started. Therefore this is tied to
  // `did_notify_all_data_source_started`: if that notification happened, do not
  // record slow data sources.
  if (!tracing_session || !tracing_session->consumer_maybe_null ||
      tracing_session->did_notify_all_data_source_started) {
    return;
  }

  int64_t timestamp = clock_->GetBootTimeNs().count();

  protozero::HeapBuffered<protos::pbzero::TracePacket> packet;
  packet->set_timestamp(static_cast<uint64_t>(timestamp));
  packet->set_trusted_uid(static_cast<int32_t>(uid_));
  packet->set_trusted_packet_sequence_id(kServicePacketSequenceID);

  size_t i = 0;
  protos::pbzero::TracingServiceEvent::DataSources* slow_data_sources =
      packet->set_service_event()->set_slow_starting_data_sources();
  for (const auto& [producer_id, ds_instance] :
       tracing_session->data_source_instances) {
    if (ds_instance.state == DataSourceInstance::STARTED) {
      continue;
    }
    ProducerEndpointImpl* producer = GetProducer(producer_id);
    if (!producer) {
      continue;
    }
    if (++i > kMaxLifecycleEventsListedDataSources) {
      break;
    }
    auto* ds = slow_data_sources->add_data_source();
    ds->set_producer_name(producer->name_);
    ds->set_data_source_name(ds_instance.data_source_name);
    PERFETTO_LOG(
        "Data source failed to start within 20s data_source=\"%s\", "
        "producer=\"%s\", tsid=%" PRIu64,
        ds_instance.data_source_name.c_str(), producer->name_.c_str(), tsid);
  }

  tracing_session->slow_start_event = TracingSession::ArbitraryLifecycleEvent{
      timestamp, packet.SerializeAsArray()};
}

void TracingServiceImpl::MaybeNotifyAllDataSourcesStarted(
    TracingSession* tracing_session) {
  if (!tracing_session->consumer_maybe_null)
    return;

  if (!tracing_session->AllDataSourceInstancesStarted())
    return;

  // In some rare cases, we can get in this state more than once. Consider the
  // following scenario: 3 data sources are registered -> trace starts ->
  // all 3 data sources ack -> OnAllDataSourcesStarted() is called.
  // Imagine now that a 4th data source registers while the trace is ongoing.
  // This would hit the AllDataSourceInstancesStarted() condition again.
  // In this case, however, we don't want to re-notify the consumer again.
  // That would be unexpected (even if, perhaps, technically correct) and
  // trigger bugs in the consumer.
  if (tracing_session->did_notify_all_data_source_started)
    return;

  PERFETTO_DLOG("All data sources started");

  SnapshotLifecycleEvent(
      tracing_session,
      protos::pbzero::TracingServiceEvent::kAllDataSourcesStartedFieldNumber,
      true /* snapshot_clocks */);

  tracing_session->did_notify_all_data_source_started = true;
  tracing_session->consumer_maybe_null->OnAllDataSourcesStarted();
}

void TracingServiceImpl::NotifyDataSourceStopped(
    ProducerID producer_id,
    DataSourceInstanceID instance_id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  for (auto& kv : tracing_sessions_) {
    TracingSession& tracing_session = kv.second;
    DataSourceInstance* instance =
        tracing_session.GetDataSourceInstance(producer_id, instance_id);

    if (!instance)
      continue;

    if (instance->state != DataSourceInstance::STOPPING) {
      PERFETTO_ELOG("Stopped data source instance in incorrect state: %d",
                    instance->state);
      continue;
    }

    instance->state = DataSourceInstance::STOPPED;

    ProducerEndpointImpl* producer = GetProducer(producer_id);
    PERFETTO_DCHECK(producer);
    if (tracing_session.consumer_maybe_null) {
      tracing_session.consumer_maybe_null->OnDataSourceInstanceStateChange(
          *producer, *instance);
    }

    if (!tracing_session.AllDataSourceInstancesStopped())
      continue;

    if (tracing_session.state != TracingSession::DISABLING_WAITING_STOP_ACKS)
      continue;

    // All data sources acked the termination.
    DisableTracingNotifyConsumerAndFlushFile(&tracing_session);
  }  // for (tracing_session)
}

void TracingServiceImpl::ActivateTriggers(
    ProducerID producer_id,
    const std::vector<std::string>& triggers) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  auto* producer = GetProducer(producer_id);
  PERFETTO_DCHECK(producer);

  int64_t now_ns = clock_->GetBootTimeNs().count();
  for (const auto& trigger_name : triggers) {
    PERFETTO_DLOG("Received ActivateTriggers request for \"%s\"",
                  trigger_name.c_str());
    android_stats::MaybeLogTriggerEvent(PerfettoTriggerAtom::kTracedTrigger,
                                        trigger_name);

    base::Hasher hash;
    hash.Update(trigger_name.c_str(), trigger_name.size());
    std::string triggered_session_name;
    base::Uuid triggered_session_uuid;
    TracingSessionID triggered_session_id = 0;
    auto trigger_mode = TraceConfig::TriggerConfig::UNSPECIFIED;

    uint64_t trigger_name_hash = hash.digest();
    size_t count_in_window =
        PurgeExpiredAndCountTriggerInWindow(now_ns, trigger_name_hash);

    bool trigger_matched = false;
    bool trigger_activated = false;
    for (auto& id_and_tracing_session : tracing_sessions_) {
      auto& tracing_session = id_and_tracing_session.second;
      TracingSessionID tsid = id_and_tracing_session.first;
      auto iter = std::find_if(
          tracing_session.config.trigger_config().triggers().begin(),
          tracing_session.config.trigger_config().triggers().end(),
          [&trigger_name](const TraceConfig::TriggerConfig::Trigger& trigger) {
            return trigger.name() == trigger_name;
          });
      if (iter == tracing_session.config.trigger_config().triggers().end())
        continue;
      if (tracing_session.state == TracingSession::CLONED_READ_ONLY)
        continue;

      // If this trigger requires a certain producer to have sent it
      // (non-empty producer_name()) ensure the producer who sent this trigger
      // matches.
      if (!iter->producer_name_regex().empty() &&
          !std::regex_match(
              producer->name_,
              std::regex(iter->producer_name_regex(), std::regex::extended))) {
        continue;
      }

      // Use a random number between 0 and 1 to check if we should allow this
      // trigger through or not.
      double trigger_rnd = random_->GetValue();
      PERFETTO_DCHECK(trigger_rnd >= 0 && trigger_rnd < 1);
      if (trigger_rnd < iter->skip_probability()) {
        MaybeLogTriggerEvent(tracing_session.config,
                             PerfettoTriggerAtom::kTracedLimitProbability,
                             trigger_name);
        continue;
      }

      // If we already triggered more times than the limit, silently ignore
      // this trigger.
      if (iter->max_per_24_h() > 0 && count_in_window >= iter->max_per_24_h()) {
        MaybeLogTriggerEvent(tracing_session.config,
                             PerfettoTriggerAtom::kTracedLimitMaxPer24h,
                             trigger_name);
        continue;
      }
      trigger_matched = true;
      triggered_session_id = tracing_session.id;
      triggered_session_name = tracing_session.config.unique_session_name();
      triggered_session_uuid.set_lsb_msb(tracing_session.trace_uuid.lsb(),
                                         tracing_session.trace_uuid.msb());
      trigger_mode = GetTriggerMode(tracing_session.config);

      const bool triggers_already_received =
          !tracing_session.received_triggers.empty();
      const TriggerInfo trigger = {static_cast<uint64_t>(now_ns), iter->name(),
                                   producer->name_, producer->uid(),
                                   iter->stop_delay_ms()};
      MaybeSnapshotClocksIntoRingBuffer(&tracing_session);
      tracing_session.received_triggers.push_back(trigger);
      switch (trigger_mode) {
        case TraceConfig::TriggerConfig::START_TRACING:
          // If the session has already been triggered and moved past
          // CONFIGURED then we don't need to repeat StartTracing. This would
          // work fine (StartTracing would return false) but would add error
          // logs.
          if (tracing_session.state != TracingSession::CONFIGURED)
            break;

          trigger_activated = true;
          MaybeLogUploadEvent(
              tracing_session.config, tracing_session.trace_uuid,
              PerfettoStatsdAtom::kTracedTriggerStartTracing, iter->name());

          // We override the trace duration to be the trigger's requested
          // value, this ensures that the trace will end after this amount
          // of time has passed.
          tracing_session.config.set_duration_ms(iter->stop_delay_ms());
          StartTracing(tsid);
          break;
        case TraceConfig::TriggerConfig::STOP_TRACING:
          // Only stop the trace once to avoid confusing log messages. I.E.
          // when we've already hit the first trigger we've already Posted the
          // task to FlushAndDisable. So all future triggers will just break
          // out.
          if (triggers_already_received)
            break;

          trigger_activated = true;
          MaybeLogUploadEvent(
              tracing_session.config, tracing_session.trace_uuid,
              PerfettoStatsdAtom::kTracedTriggerStopTracing, iter->name());

          // Now that we've seen a trigger we need to stop, flush, and disable
          // this session after the configured |stop_delay_ms|.
          weak_runner_.PostDelayedTask(
              [this, tsid] {
                // Skip entirely the flush if the trace session doesn't exist
                // anymore. This is to prevent misleading error messages to be
                // logged.
                if (GetTracingSession(tsid))
                  FlushAndDisableTracing(tsid);
              },
              // If this trigger is zero this will immediately executable and
              // will happen shortly.
              iter->stop_delay_ms());
          break;

        case TraceConfig::TriggerConfig::CLONE_SNAPSHOT:
          trigger_activated = true;
          MaybeLogUploadEvent(
              tracing_session.config, tracing_session.trace_uuid,
              PerfettoStatsdAtom::kTracedTriggerCloneSnapshot, iter->name());
          weak_runner_.PostDelayedTask(
              [this, tsid, trigger] {
                auto* tsess = GetTracingSession(tsid);
                if (!tsess || !tsess->consumer_maybe_null)
                  return;
                tsess->consumer_maybe_null->NotifyCloneSnapshotTrigger(trigger);
              },
              iter->stop_delay_ms());
          break;

        case TraceConfig::TriggerConfig::UNSPECIFIED:
          PERFETTO_ELOG("Trigger activated but trigger mode unspecified.");
          break;
      }
    }  // for (.. : tracing_sessions_)

    if (trigger_matched) {
      trigger_history_.emplace_back(TriggerHistory{now_ns, trigger_name_hash});
    }

    if (trigger_activated) {
      // Log only the trigger that actually caused a trace stop/start, don't log
      // the follow-up ones, even if they matched.
      PERFETTO_LOG(
          "Trace trigger activated: trigger_name=\"%s\" trigger_mode=%d "
          "trace_name=\"%s\" trace_uuid=\"%s\" tsid=%" PRIu64,
          trigger_name.c_str(), trigger_mode, triggered_session_name.c_str(),
          triggered_session_uuid.ToPrettyString().c_str(),
          triggered_session_id);
    }
  }  // for (trigger_name : triggers)
}

// Always invoked TraceConfig.data_source_stop_timeout_ms (by default
// kDataSourceStopTimeoutMs) after DisableTracing(). In nominal conditions all
// data sources should have acked the stop and this will early out.
void TracingServiceImpl::OnDisableTracingTimeout(TracingSessionID tsid) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  TracingSession* tracing_session = GetTracingSession(tsid);
  if (!tracing_session ||
      tracing_session->state != TracingSession::DISABLING_WAITING_STOP_ACKS) {
    return;  // Tracing session was successfully disabled.
  }

  PERFETTO_ILOG("Timeout while waiting for ACKs for tracing session %" PRIu64,
                tsid);
  PERFETTO_DCHECK(!tracing_session->AllDataSourceInstancesStopped());
  DisableTracingNotifyConsumerAndFlushFile(tracing_session);
}

void TracingServiceImpl::DisableTracingNotifyConsumerAndFlushFile(
    TracingSession* tracing_session) {
  PERFETTO_DCHECK(tracing_session->state != TracingSession::DISABLED);
  for (auto& inst_kv : tracing_session->data_source_instances) {
    if (inst_kv.second.state == DataSourceInstance::STOPPED)
      continue;
    inst_kv.second.state = DataSourceInstance::STOPPED;
    ProducerEndpointImpl* producer = GetProducer(inst_kv.first);
    PERFETTO_DCHECK(producer);
    if (tracing_session->consumer_maybe_null) {
      tracing_session->consumer_maybe_null->OnDataSourceInstanceStateChange(
          *producer, inst_kv.second);
    }
  }
  tracing_session->state = TracingSession::DISABLED;

  // Scrape any remaining chunks that weren't flushed by the producers.
  for (auto& producer_id_and_producer : producers_)
    ScrapeSharedMemoryBuffers(tracing_session, producer_id_and_producer.second);

  SnapshotLifecycleEvent(
      tracing_session,
      protos::pbzero::TracingServiceEvent::kTracingDisabledFieldNumber,
      true /* snapshot_clocks */);

  if (tracing_session->write_into_file) {
    tracing_session->write_period_ms = 0;
    ReadBuffersIntoFile(tracing_session->id);
  }

  MaybeLogUploadEvent(tracing_session->config, tracing_session->trace_uuid,
                      PerfettoStatsdAtom::kTracedNotifyTracingDisabled);

  if (tracing_session->consumer_maybe_null)
    tracing_session->consumer_maybe_null->NotifyOnTracingDisabled("");
}

void TracingServiceImpl::Flush(TracingSessionID tsid,
                               uint32_t timeout_ms,
                               ConsumerEndpoint::FlushCallback callback,
                               FlushFlags flush_flags) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  TracingSession* tracing_session = GetTracingSession(tsid);
  if (!tracing_session) {
    PERFETTO_DLOG("Flush() failed, invalid session ID %" PRIu64, tsid);
    return;
  }

  SnapshotLifecycleEvent(
      tracing_session,
      protos::pbzero::TracingServiceEvent::kFlushStartedFieldNumber,
      false /* snapshot_clocks */);

  std::map<ProducerID, std::vector<DataSourceInstanceID>> data_source_instances;
  for (const auto& [producer_id, ds_inst] :
       tracing_session->data_source_instances) {
    if (ds_inst.no_flush)
      continue;
    data_source_instances[producer_id].push_back(ds_inst.instance_id);
  }
  FlushDataSourceInstances(tracing_session, timeout_ms, data_source_instances,
                           std::move(callback), flush_flags);
}

void TracingServiceImpl::FlushDataSourceInstances(
    TracingSession* tracing_session,
    uint32_t timeout_ms,
    const std::map<ProducerID, std::vector<DataSourceInstanceID>>&
        data_source_instances,
    ConsumerEndpoint::FlushCallback callback,
    FlushFlags flush_flags) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!timeout_ms)
    timeout_ms = tracing_session->flush_timeout_ms();

  if (tracing_session->pending_flushes.size() > 1000) {
    PERFETTO_ELOG("Too many flushes (%zu) pending for the tracing session",
                  tracing_session->pending_flushes.size());
    callback(false);
    return;
  }

  if (tracing_session->state != TracingSession::STARTED) {
    PERFETTO_LOG("Flush() called, but tracing has not been started");
    callback(false);
    return;
  }

  tracing_session->last_flush_events.clear();

  ++tracing_session->flushes_requested;
  FlushRequestID flush_request_id = ++last_flush_request_id_;
  PendingFlush& pending_flush =
      tracing_session->pending_flushes
          .emplace_hint(tracing_session->pending_flushes.end(),
                        flush_request_id, PendingFlush(std::move(callback)))
          ->second;

  // Send a flush request to each producer involved in the tracing session. In
  // order to issue a flush request we have to build a map of all data source
  // instance ids enabled for each producer.

  for (const auto& [producer_id, data_sources] : data_source_instances) {
    ProducerEndpointImpl* producer = GetProducer(producer_id);
    producer->Flush(flush_request_id, data_sources, flush_flags);
    if (!producer->IsAndroidProcessFrozen()) {
      pending_flush.producers.insert(producer_id);
    } else {
      PERFETTO_DLOG(
          "skipping waiting flush for on producer \"%s\" (pid=%" PRIu32
          ") because it is frozen",
          producer->name_.c_str(), static_cast<uint32_t>(producer->pid()));
    }
  }

  // If there are no producers to flush (realistically this happens only in
  // some tests) fire OnFlushTimeout() straight away, without waiting.
  if (data_source_instances.empty())
    timeout_ms = 0;

  weak_runner_.PostDelayedTask(
      [this, tsid = tracing_session->id, flush_request_id, flush_flags] {
        OnFlushTimeout(tsid, flush_request_id, flush_flags);
      },
      timeout_ms);
}

void TracingServiceImpl::NotifyFlushDoneForProducer(
    ProducerID producer_id,
    FlushRequestID flush_request_id) {
  for (auto& kv : tracing_sessions_) {
    // Remove all pending flushes <= |flush_request_id| for |producer_id|.
    auto& pending_flushes = kv.second.pending_flushes;
    auto end_it = pending_flushes.upper_bound(flush_request_id);
    for (auto it = pending_flushes.begin(); it != end_it;) {
      PendingFlush& pending_flush = it->second;
      pending_flush.producers.erase(producer_id);
      if (pending_flush.producers.empty()) {
        TracingSessionID tsid = kv.first;
        auto callback = std::move(pending_flush.callback);
        weak_runner_.PostTask([this, tsid, callback = std::move(callback)]() {
          CompleteFlush(tsid, std::move(callback),
                        /*success=*/true);
        });
        it = pending_flushes.erase(it);
      } else {
        it++;
      }
    }  // for (pending_flushes)
  }  // for (tracing_session)
}

void TracingServiceImpl::OnFlushTimeout(TracingSessionID tsid,
                                        FlushRequestID flush_request_id,
                                        FlushFlags flush_flags) {
  TracingSession* tracing_session = GetTracingSession(tsid);
  if (!tracing_session)
    return;
  auto it = tracing_session->pending_flushes.find(flush_request_id);
  if (it == tracing_session->pending_flushes.end())
    return;  // Nominal case: flush was completed and acked on time.

  PendingFlush& pending_flush = it->second;

  // If there were no producers to flush, consider it a success.
  bool success = pending_flush.producers.empty();
  auto callback = std::move(pending_flush.callback);
  // If flush failed and this is a "final" flush, log which data sources were
  // slow.
  if ((flush_flags.reason() == FlushFlags::Reason::kTraceClone ||
       flush_flags.reason() == FlushFlags::Reason::kTraceStop) &&
      !success) {
    int64_t timestamp = clock_->GetBootTimeNs().count();

    protozero::HeapBuffered<protos::pbzero::TracePacket> packet;
    packet->set_timestamp(static_cast<uint64_t>(timestamp));
    packet->set_trusted_uid(static_cast<int32_t>(uid_));
    packet->set_trusted_packet_sequence_id(kServicePacketSequenceID);

    size_t i = 0;
    protos::pbzero::TracingServiceEvent::DataSources* event =
        packet->set_service_event()->set_last_flush_slow_data_sources();
    for (const auto& producer_id : pending_flush.producers) {
      ProducerEndpointImpl* producer = GetProducer(producer_id);
      if (!producer) {
        continue;
      }
      if (++i > kMaxLifecycleEventsListedDataSources) {
        break;
      }

      auto ds_id_range =
          tracing_session->data_source_instances.equal_range(producer_id);
      for (auto ds_it = ds_id_range.first; ds_it != ds_id_range.second;
           ds_it++) {
        auto* ds = event->add_data_source();
        ds->set_producer_name(producer->name_);
        ds->set_data_source_name(ds_it->second.data_source_name);
        if (++i > kMaxLifecycleEventsListedDataSources) {
          break;
        }
      }
    }

    tracing_session->last_flush_events.push_back(
        {timestamp, packet.SerializeAsArray()});
  }
  tracing_session->pending_flushes.erase(it);
  CompleteFlush(tsid, std::move(callback), success);
}

void TracingServiceImpl::CompleteFlush(TracingSessionID tsid,
                                       ConsumerEndpoint::FlushCallback callback,
                                       bool success) {
  TracingSession* tracing_session = GetTracingSession(tsid);
  if (!tracing_session) {
    callback(false);
    return;
  }
  // Producers may not have been able to flush all their data, even if they
  // indicated flush completion. If possible, also collect uncommitted chunks
  // to make sure we have everything they wrote so far.
  for (auto& producer_id_and_producer : producers_) {
    ScrapeSharedMemoryBuffers(tracing_session, producer_id_and_producer.second);
  }
  SnapshotLifecycleEvent(
      tracing_session,
      protos::pbzero::TracingServiceEvent::kAllDataSourcesFlushedFieldNumber,
      true /* snapshot_clocks */);

  tracing_session->flushes_succeeded += success ? 1 : 0;
  tracing_session->flushes_failed += success ? 0 : 1;
  callback(success);
}

void TracingServiceImpl::ScrapeSharedMemoryBuffers(
    TracingSession* tracing_session,
    ProducerEndpointImpl* producer) {
  if (!producer->smb_scraping_enabled_)
    return;

  // Can't copy chunks if we don't know about any trace writers.
  if (producer->writers_.empty())
    return;

  // Performance optimization: On flush or session disconnect, this method is
  // called for each producer. If the producer doesn't participate in the
  // session, there's no need to scape its chunks right now. We can tell if a
  // producer participates in the session by checking if the producer is allowed
  // to write into the session's log buffers.
  const auto& session_buffers = tracing_session->buffers_index;
  bool producer_in_session =
      std::any_of(session_buffers.begin(), session_buffers.end(),
                  [producer](BufferID buffer_id) {
                    return producer->allowed_target_buffers_.count(buffer_id);
                  });
  if (!producer_in_session)
    return;

  PERFETTO_DLOG("Scraping SMB for producer %" PRIu16, producer->id_);

  // Find and copy any uncommitted chunks from the SMB.
  //
  // In nominal conditions, the page header bitmap of the used SMB pages should
  // never change because the service is the only one who is supposed to modify
  // used pages (to make them free again).
  //
  // However, the code here needs to deal with the case of a malicious producer
  // altering the SMB in unpredictable ways. Thankfully the SMB size is
  // immutable, so a chunk will always point to some valid memory, even if the
  // producer alters the intended layout and chunk header concurrently.
  // Ultimately a malicious producer altering the SMB's chunk header bitamp
  // while we are iterating in this function is not any different from the case
  // of a malicious producer asking to commit a chunk made of random data,
  // which is something this class has to deal with regardless.
  //
  // The only legitimate mutations that can happen from sane producers,
  // concurrently to this function, are:
  //   A. free pages being partitioned,
  //   B. free chunks being migrated to kChunkBeingWritten,
  //   C. kChunkBeingWritten chunks being migrated to kChunkCompleted.

  SharedMemoryABI* abi = &producer->shmem_abi_;
  // num_pages() is immutable after the SMB is initialized and cannot be changed
  // even by a producer even if malicious.
  for (size_t page_idx = 0; page_idx < abi->num_pages(); page_idx++) {
    uint32_t header_bitmap = abi->GetPageHeaderBitmap(page_idx);

    uint32_t used_chunks =
        abi->GetUsedChunks(header_bitmap);  // Returns a bitmap.
    // Skip empty pages.
    if (used_chunks == 0)
      continue;

    // Scrape the chunks that are currently used. These should be either in
    // state kChunkBeingWritten or kChunkComplete.
    for (uint32_t chunk_idx = 0; used_chunks; chunk_idx++, used_chunks >>= 1) {
      if (!(used_chunks & 1))
        continue;

      SharedMemoryABI::ChunkState state =
          SharedMemoryABI::GetChunkStateFromHeaderBitmap(header_bitmap,
                                                         chunk_idx);
      PERFETTO_DCHECK(state == SharedMemoryABI::kChunkBeingWritten ||
                      state == SharedMemoryABI::kChunkComplete);
      bool chunk_complete = state == SharedMemoryABI::kChunkComplete;

      SharedMemoryABI::Chunk chunk =
          abi->GetChunkUnchecked(page_idx, header_bitmap, chunk_idx);

      uint16_t packet_count;
      uint8_t flags;
      // GetPacketCountAndFlags has acquire_load semantics.
      std::tie(packet_count, flags) = chunk.GetPacketCountAndFlags();

      // It only makes sense to copy an incomplete chunk if there's at least
      // one full packet available. (The producer may not have completed the
      // last packet in it yet, so we need at least 2.)
      if (!chunk_complete && packet_count < 2)
        continue;

      // At this point, it is safe to access the remaining header fields of
      // the chunk. Even if the chunk was only just transferred from
      // kChunkFree into kChunkBeingWritten state, the header should be
      // written completely once the packet count increased above 1 (it was
      // reset to 0 by the service when the chunk was freed).

      WriterID writer_id = chunk.writer_id();
      std::optional<BufferID> target_buffer_id =
          producer->buffer_id_for_writer(writer_id);

      // We can only scrape this chunk if we know which log buffer to copy it
      // into.
      if (!target_buffer_id)
        continue;

      // Skip chunks that don't belong to the requested tracing session.
      bool target_buffer_belongs_to_session =
          std::find(session_buffers.begin(), session_buffers.end(),
                    *target_buffer_id) != session_buffers.end();
      if (!target_buffer_belongs_to_session)
        continue;

      uint32_t chunk_id =
          chunk.header()->chunk_id.load(std::memory_order_relaxed);

      CopyProducerPageIntoLogBuffer(
          producer->id_, producer->client_identity_, writer_id, chunk_id,
          *target_buffer_id, packet_count, flags, chunk_complete,
          chunk.payload_begin(), chunk.payload_size());
    }
  }
}

void TracingServiceImpl::FlushAndDisableTracing(TracingSessionID tsid) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  PERFETTO_DLOG("Triggering final flush for %" PRIu64, tsid);
  Flush(
      tsid, 0,
      [this, tsid](bool success) {
        // This was a DLOG up to Jun 2021 (v16, Android S).
        PERFETTO_LOG("FlushAndDisableTracing(%" PRIu64 ") done, success=%d",
                     tsid, success);
        TracingSession* session = GetTracingSession(tsid);
        if (!session) {
          return;
        }
        session->final_flush_outcome = success
                                           ? TraceStats::FINAL_FLUSH_SUCCEEDED
                                           : TraceStats::FINAL_FLUSH_FAILED;
        if (session->consumer_maybe_null) {
          // If the consumer is still attached, just disable the session but
          // give it a chance to read the contents.
          DisableTracing(tsid);
        } else {
          // If the consumer detached, destroy the session. If the consumer did
          // start the session in long-tracing mode, the service will have saved
          // the contents to the passed file. If not, the contents will be
          // destroyed.
          FreeBuffers(tsid);
        }
      },
      FlushFlags(FlushFlags::Initiator::kTraced,
                 FlushFlags::Reason::kTraceStop));
}

void TracingServiceImpl::PeriodicFlushTask(TracingSessionID tsid,
                                           bool post_next_only) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  TracingSession* tracing_session = GetTracingSession(tsid);
  if (!tracing_session || tracing_session->state != TracingSession::STARTED)
    return;

  uint32_t flush_period_ms = tracing_session->config.flush_period_ms();
  weak_runner_.PostDelayedTask(
      [this, tsid] { PeriodicFlushTask(tsid, /*post_next_only=*/false); },
      flush_period_ms - static_cast<uint32_t>(clock_->GetWallTimeMs().count() %
                                              flush_period_ms));

  if (post_next_only)
    return;

  PERFETTO_DLOG("Triggering periodic flush for trace session %" PRIu64, tsid);
  Flush(
      tsid, 0,
      [](bool success) {
        if (!success)
          PERFETTO_ELOG("Periodic flush timed out");
      },
      FlushFlags(FlushFlags::Initiator::kTraced,
                 FlushFlags::Reason::kPeriodic));
}

void TracingServiceImpl::PeriodicClearIncrementalStateTask(
    TracingSessionID tsid,
    bool post_next_only) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  TracingSession* tracing_session = GetTracingSession(tsid);
  if (!tracing_session || tracing_session->state != TracingSession::STARTED)
    return;

  uint32_t clear_period_ms =
      tracing_session->config.incremental_state_config().clear_period_ms();
  weak_runner_.PostDelayedTask(
      [this, tsid] {
        PeriodicClearIncrementalStateTask(tsid, /*post_next_only=*/false);
      },
      clear_period_ms - static_cast<uint32_t>(clock_->GetWallTimeMs().count() %
                                              clear_period_ms));

  if (post_next_only)
    return;

  PERFETTO_DLOG(
      "Performing periodic incremental state clear for trace session %" PRIu64,
      tsid);

  // Queue the IPCs to producers with active data sources that opted in.
  std::map<ProducerID, std::vector<DataSourceInstanceID>> clear_map;
  for (const auto& kv : tracing_session->data_source_instances) {
    ProducerID producer_id = kv.first;
    const DataSourceInstance& data_source = kv.second;
    if (data_source.handles_incremental_state_clear) {
      clear_map[producer_id].push_back(data_source.instance_id);
    }
  }

  for (const auto& kv : clear_map) {
    ProducerID producer_id = kv.first;
    const std::vector<DataSourceInstanceID>& data_sources = kv.second;
    ProducerEndpointImpl* producer = GetProducer(producer_id);
    if (!producer) {
      PERFETTO_DFATAL("Producer does not exist.");
      continue;
    }
    producer->ClearIncrementalState(data_sources);
  }
}

bool TracingServiceImpl::ReadBuffersIntoConsumer(
    TracingSessionID tsid,
    ConsumerEndpointImpl* consumer) {
  PERFETTO_DCHECK(consumer);
  PERFETTO_DCHECK_THREAD(thread_checker_);
  TracingSession* tracing_session = GetTracingSession(tsid);
  if (!tracing_session) {
    PERFETTO_DLOG(
        "Cannot ReadBuffersIntoConsumer(): no tracing session is active");
    return false;
  }

  if (tracing_session->write_into_file) {
    // If the consumer enabled tracing and asked to save the contents into the
    // passed file makes little sense to also try to read the buffers over IPC,
    // as that would just steal data from the periodic draining task.
    PERFETTO_ELOG("Consumer trying to read from write_into_file session.");
    return false;
  }

  if (IsWaitingForTrigger(tracing_session))
    return false;

  // This is a rough threshold to determine how much to read from the buffer in
  // each task. This is to avoid executing a single huge sending task for too
  // long and risk to hit the watchdog. This is *not* an upper bound: we just
  // stop accumulating new packets and PostTask *after* we cross this threshold.
  // This constant essentially balances the PostTask and IPC overhead vs the
  // responsiveness of the service. An extremely small value will cause one IPC
  // and one PostTask for each slice but will keep the service extremely
  // responsive. An extremely large value will batch the send for the full
  // buffer in one large task, will hit the blocking send() once the socket
  // buffers are full and hang the service for a bit (until the consumer
  // catches up).
  static constexpr size_t kApproxBytesPerTask = 32768;
  bool has_more;
  std::vector<TracePacket> packets =
      ReadBuffers(tracing_session, kApproxBytesPerTask, &has_more);

  if (has_more) {
    auto weak_consumer = consumer->weak_ptr_factory_.GetWeakPtr();
    weak_runner_.PostTask(
        [this, weak_consumer = std::move(weak_consumer), tsid] {
          if (!weak_consumer)
            return;
          ReadBuffersIntoConsumer(tsid, weak_consumer.get());
        });
  }

  // Keep this as tail call, just in case the consumer re-enters.
  consumer->consumer_->OnTraceData(std::move(packets), has_more);
  return true;
}

bool TracingServiceImpl::ReadBuffersIntoFile(TracingSessionID tsid) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  TracingSession* tracing_session = GetTracingSession(tsid);
  if (!tracing_session) {
    // This will be hit systematically from the PostDelayedTask. Avoid logging,
    // it would be just spam.
    return false;
  }

  // This can happen if the file is closed by a previous task because it reaches
  // |max_file_size_bytes|.
  if (!tracing_session->write_into_file)
    return false;

  if (IsWaitingForTrigger(tracing_session))
    return false;

  // ReadBuffers() can allocate memory internally, for filtering. By limiting
  // the data that ReadBuffers() reads to kWriteIntoChunksSize per iteration,
  // we limit the amount of memory used on each iteration.
  //
  // It would be tempting to split this into multiple tasks like in
  // ReadBuffersIntoConsumer, but that's not currently possible.
  // ReadBuffersIntoFile has to read the whole available data before returning,
  // to support the disable_immediately=true code paths.
  bool has_more = true;
  bool stop_writing_into_file = false;
  do {
    std::vector<TracePacket> packets =
        ReadBuffers(tracing_session, kWriteIntoFileChunkSize, &has_more);

    stop_writing_into_file = WriteIntoFile(tracing_session, std::move(packets));
  } while (has_more && !stop_writing_into_file);

  if (stop_writing_into_file || tracing_session->write_period_ms == 0) {
    // Ensure all data was written to the file before we close it.
    base::FlushFile(tracing_session->write_into_file.get());
    tracing_session->write_into_file.reset();
    tracing_session->write_period_ms = 0;
    if (tracing_session->state == TracingSession::STARTED)
      DisableTracing(tsid);
    return true;
  }

  weak_runner_.PostDelayedTask([this, tsid] { ReadBuffersIntoFile(tsid); },
                               DelayToNextWritePeriodMs(*tracing_session));
  return true;
}

bool TracingServiceImpl::IsWaitingForTrigger(TracingSession* tracing_session) {
  // Ignore the logic below for cloned tracing sessions. In this case we
  // actually want to read the (cloned) trace buffers even if no trigger was
  // hit.
  if (tracing_session->state == TracingSession::CLONED_READ_ONLY) {
    return false;
  }

  // When a tracing session is waiting for a trigger, it is considered empty. If
  // a tracing session finishes and moves into DISABLED without ever receiving a
  // trigger, the trace should never return any data. This includes the
  // synthetic packets like TraceConfig and Clock snapshots. So we bail out
  // early and let the consumer know there is no data.
  if (!tracing_session->config.trigger_config().triggers().empty() &&
      tracing_session->received_triggers.empty()) {
    PERFETTO_DLOG(
        "ReadBuffers(): tracing session has not received a trigger yet.");
    return true;
  }

  // Traces with CLONE_SNAPSHOT triggers are a special case of the above. They
  // can be read only via a CloneSession() request. This is to keep the
  // behavior consistent with the STOP_TRACING+triggers case and avoid periodic
  // finalizations and uploads of the main CLONE_SNAPSHOT triggers.
  if (GetTriggerMode(tracing_session->config) ==
      TraceConfig::TriggerConfig::CLONE_SNAPSHOT) {
    PERFETTO_DLOG(
        "ReadBuffers(): skipping because the tracing session has "
        "CLONE_SNAPSHOT triggers defined");
    return true;
  }

  return false;
}

std::vector<TracePacket> TracingServiceImpl::ReadBuffers(
    TracingSession* tracing_session,
    size_t threshold,
    bool* has_more) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  PERFETTO_DCHECK(tracing_session);
  *has_more = false;

  std::vector<TracePacket> packets;
  packets.reserve(1024);  // Just an educated guess to avoid trivial expansions.

  if (!tracing_session->initial_clock_snapshot.empty()) {
    EmitClockSnapshot(tracing_session,
                      std::move(tracing_session->initial_clock_snapshot),
                      &packets);
  }

  for (auto& snapshot : tracing_session->clock_snapshot_ring_buffer) {
    PERFETTO_DCHECK(!snapshot.empty());
    EmitClockSnapshot(tracing_session, std::move(snapshot), &packets);
  }
  tracing_session->clock_snapshot_ring_buffer.clear();

  if (tracing_session->should_emit_sync_marker) {
    EmitSyncMarker(&packets);
    tracing_session->should_emit_sync_marker = false;
  }

  if (!tracing_session->config.builtin_data_sources().disable_trace_config()) {
    MaybeEmitTraceConfig(tracing_session, &packets);
    MaybeEmitCloneTrigger(tracing_session, &packets);
    MaybeEmitReceivedTriggers(tracing_session, &packets);
  }
  if (!tracing_session->did_emit_initial_packets) {
    EmitUuid(tracing_session, &packets);
    if (!tracing_session->config.builtin_data_sources().disable_system_info()) {
      EmitSystemInfo(&packets);
      if (!relay_clients_.empty())
        MaybeEmitRemoteSystemInfo(&packets);
    }
  }
  tracing_session->did_emit_initial_packets = true;

  // Note that in the proto comment, we guarantee that the tracing_started
  // lifecycle event will be emitted before any data packets so make sure to
  // keep this before reading the tracing buffers.
  if (!tracing_session->config.builtin_data_sources().disable_service_events())
    EmitLifecycleEvents(tracing_session, &packets);

  // In a multi-machine tracing session, emit clock synchronization messages for
  // remote machines.
  if (!relay_clients_.empty())
    MaybeEmitRemoteClockSync(tracing_session, &packets);

  size_t packets_bytes = 0;  // SUM(slice.size() for each slice in |packets|).

  // Add up size for packets added by the Maybe* calls above.
  for (const TracePacket& packet : packets) {
    packets_bytes += packet.size();
  }

  bool did_hit_threshold = false;

  for (size_t buf_idx = 0;
       buf_idx < tracing_session->num_buffers() && !did_hit_threshold;
       buf_idx++) {
    auto tbuf_iter = buffers_.find(tracing_session->buffers_index[buf_idx]);
    if (tbuf_iter == buffers_.end()) {
      PERFETTO_DFATAL("Buffer not found.");
      continue;
    }
    TraceBuffer& tbuf = *tbuf_iter->second;
    tbuf.BeginRead();
    while (!did_hit_threshold) {
      TracePacket packet;
      TraceBuffer::PacketSequenceProperties sequence_properties{};
      bool previous_packet_dropped;
      if (!tbuf.ReadNextTracePacket(&packet, &sequence_properties,
                                    &previous_packet_dropped)) {
        break;
      }
      packet.set_buffer_index_for_stats(static_cast<uint32_t>(buf_idx));
      PERFETTO_DCHECK(sequence_properties.producer_id_trusted != 0);
      PERFETTO_DCHECK(sequence_properties.writer_id != 0);
      PERFETTO_DCHECK(sequence_properties.client_identity_trusted.has_uid());
      // Not checking sequence_properties.client_identity_trusted.has_pid():
      // it is false if the platform doesn't support it.

      PERFETTO_DCHECK(packet.size() > 0);
      if (!PacketStreamValidator::Validate(packet.slices())) {
        tracing_session->invalid_packets++;
        PERFETTO_DLOG("Dropping invalid packet");
        continue;
      }

      // Append a slice with the trusted field data. This can't be spoofed
      // because above we validated that the existing slices don't contain any
      // trusted fields. For added safety we append instead of prepending
      // because according to protobuf semantics, if the same field is
      // encountered multiple times the last instance takes priority. Note that
      // truncated packets are also rejected, so the producer can't give us a
      // partial packet (e.g., a truncated string) which only becomes valid when
      // the trusted data is appended here.
      Slice slice = Slice::Allocate(32);
      protozero::StaticBuffered<protos::pbzero::TracePacket> trusted_packet(
          slice.own_data(), slice.size);
      const auto& client_identity_trusted =
          sequence_properties.client_identity_trusted;
      trusted_packet->set_trusted_uid(
          static_cast<int32_t>(client_identity_trusted.uid()));
      trusted_packet->set_trusted_packet_sequence_id(
          tracing_session->GetPacketSequenceID(
              client_identity_trusted.machine_id(),
              sequence_properties.producer_id_trusted,
              sequence_properties.writer_id));
      if (client_identity_trusted.has_pid()) {
        // Not supported on all platforms.
        trusted_packet->set_trusted_pid(
            static_cast<int32_t>(client_identity_trusted.pid()));
      }
      if (client_identity_trusted.has_non_default_machine_id()) {
        trusted_packet->set_machine_id(client_identity_trusted.machine_id());
      }
      if (previous_packet_dropped)
        trusted_packet->set_previous_packet_dropped(previous_packet_dropped);
      slice.size = trusted_packet.Finalize();
      packet.AddSlice(std::move(slice));

      // Append the packet (inclusive of the trusted uid) to |packets|.
      packets_bytes += packet.size();
      did_hit_threshold = packets_bytes >= threshold;
      packets.emplace_back(std::move(packet));
    }  // for(packets...)
  }  // for(buffers...)

  *has_more = did_hit_threshold;

  // Only emit the "read complete" lifetime event when there is no more trace
  // data available to read. These events are used as safe points to limit
  // sorting in trace processor: the code shouldn't emit the event unless the
  // buffers are empty.
  if (!*has_more && !tracing_session->config.builtin_data_sources()
                         .disable_service_events()) {
    // We don't bother snapshotting clocks here because we wouldn't be able to
    // emit it and we shouldn't have significant drift from the last snapshot in
    // any case.
    SnapshotLifecycleEvent(tracing_session,
                           protos::pbzero::TracingServiceEvent::
                               kReadTracingBuffersCompletedFieldNumber,
                           false /* snapshot_clocks */);
    EmitLifecycleEvents(tracing_session, &packets);
  }

  // Only emit the stats when there is no more trace data is available to read.
  // That way, any problems that occur while reading from the buffers are
  // reflected in the emitted stats. This is particularly important for use
  // cases where ReadBuffers is only ever called after the tracing session is
  // stopped.
  if (!*has_more && tracing_session->should_emit_stats) {
    EmitStats(tracing_session, &packets);
    tracing_session->should_emit_stats = false;
  }

  MaybeFilterPackets(tracing_session, &packets);

  MaybeCompressPackets(tracing_session, &packets);

  if (!*has_more) {
    // We've observed some extremely high memory usage by scudo after
    // MaybeFilterPackets in the past. The original bug (b/195145848) is fixed
    // now, but this code asks scudo to release memory just in case.
    base::MaybeReleaseAllocatorMemToOS();
  }

  return packets;
}

void TracingServiceImpl::MaybeFilterPackets(TracingSession* tracing_session,
                                            std::vector<TracePacket>* packets) {
  // If the tracing session specified a filter, run all packets through the
  // filter and replace them with the filter results.
  // The process below mantains the cardinality of input packets. Even if an
  // entire packet is filtered out, we emit a zero-sized TracePacket proto. That
  // makes debugging and reasoning about the trace stats easier.
  // This place swaps the contents of each |packets| entry in place.
  if (!tracing_session->trace_filter) {
    return;
  }
  protozero::MessageFilter& trace_filter = *tracing_session->trace_filter;
  // The filter root should be reset from protos.Trace to protos.TracePacket
  // by the earlier call to SetFilterRoot() in EnableTracing().
  PERFETTO_DCHECK(trace_filter.config().root_msg_index() != 0);
  std::vector<protozero::MessageFilter::InputSlice> filter_input;
  auto start = clock_->GetWallTimeNs();
  for (TracePacket& packet : *packets) {
    const auto& packet_slices = packet.slices();
    const size_t input_packet_size = packet.size();
    filter_input.clear();
    filter_input.resize(packet_slices.size());
    ++tracing_session->filter_input_packets;
    tracing_session->filter_input_bytes += input_packet_size;
    for (size_t i = 0; i < packet_slices.size(); ++i)
      filter_input[i] = {packet_slices[i].start, packet_slices[i].size};
    auto filtered_packet = trace_filter.FilterMessageFragments(
        &filter_input[0], filter_input.size());

    // Replace the packet in-place with the filtered one (unless failed).
    std::optional<uint32_t> maybe_buffer_idx = packet.buffer_index_for_stats();
    packet = TracePacket();
    if (filtered_packet.error) {
      ++tracing_session->filter_errors;
      PERFETTO_DLOG("Trace packet filtering failed @ packet %" PRIu64,
                    tracing_session->filter_input_packets);
      continue;
    }
    tracing_session->filter_output_bytes += filtered_packet.size;
    if (maybe_buffer_idx.has_value()) {
      // Keep the per-buffer stats updated. Also propagate the
      // buffer_index_for_stats in the output packet to allow accounting by
      // other parts of the ReadBuffer pipeline.
      uint32_t buffer_idx = maybe_buffer_idx.value();
      packet.set_buffer_index_for_stats(buffer_idx);
      auto& vec = tracing_session->filter_bytes_discarded_per_buffer;
      if (static_cast<size_t>(buffer_idx) >= vec.size())
        vec.resize(buffer_idx + 1);
      PERFETTO_DCHECK(input_packet_size >= filtered_packet.size);
      size_t bytes_filtered_out = input_packet_size - filtered_packet.size;
      vec[buffer_idx] += bytes_filtered_out;
    }
    AppendOwnedSlicesToPacket(std::move(filtered_packet.data),
                              filtered_packet.size, kMaxTracePacketSliceSize,
                              &packet);
  }
  auto end = clock_->GetWallTimeNs();
  tracing_session->filter_time_taken_ns +=
      static_cast<uint64_t>((end - start).count());
}

void TracingServiceImpl::MaybeCompressPackets(
    TracingSession* tracing_session,
    std::vector<TracePacket>* packets) {
  if (!tracing_session->compress_deflate) {
    return;
  }

  init_opts_.compressor_fn(packets);
}

bool TracingServiceImpl::WriteIntoFile(TracingSession* tracing_session,
                                       std::vector<TracePacket> packets) {
  if (!tracing_session->write_into_file) {
    return false;
  }
  const uint64_t max_size = tracing_session->max_file_size_bytes
                                ? tracing_session->max_file_size_bytes
                                : std::numeric_limits<size_t>::max();

  size_t total_slices = 0;
  for (const TracePacket& packet : packets) {
    total_slices += packet.slices().size();
  }
  // When writing into a file, the file should look like a root trace.proto
  // message. Each packet should be prepended with a proto preamble stating
  // its field id (within trace.proto) and size. Hence the addition below.
  const size_t max_iovecs = total_slices + packets.size();

  size_t num_iovecs = 0;
  bool stop_writing_into_file = false;
  std::unique_ptr<struct iovec[]> iovecs(new struct iovec[max_iovecs]);
  size_t num_iovecs_at_last_packet = 0;
  uint64_t bytes_about_to_be_written = 0;
  for (TracePacket& packet : packets) {
    std::tie(iovecs[num_iovecs].iov_base, iovecs[num_iovecs].iov_len) =
        packet.GetProtoPreamble();
    bytes_about_to_be_written += iovecs[num_iovecs].iov_len;
    num_iovecs++;
    for (const Slice& slice : packet.slices()) {
      // writev() doesn't change the passed pointer. However, struct iovec
      // take a non-const ptr because it's the same struct used by readv().
      // Hence the const_cast here.
      char* start = static_cast<char*>(const_cast<void*>(slice.start));
      bytes_about_to_be_written += slice.size;
      iovecs[num_iovecs++] = {start, slice.size};
    }

    if (tracing_session->bytes_written_into_file + bytes_about_to_be_written >=
        max_size) {
      stop_writing_into_file = true;
      num_iovecs = num_iovecs_at_last_packet;
      break;
    }

    num_iovecs_at_last_packet = num_iovecs;
  }
  PERFETTO_DCHECK(num_iovecs <= max_iovecs);
  int fd = *tracing_session->write_into_file;

  uint64_t total_wr_size = 0;

  // writev() can take at most IOV_MAX entries per call. Batch them.
  constexpr size_t kIOVMax = IOV_MAX;
  for (size_t i = 0; i < num_iovecs; i += kIOVMax) {
    int iov_batch_size = static_cast<int>(std::min(num_iovecs - i, kIOVMax));
    ssize_t wr_size = PERFETTO_EINTR(writev(fd, &iovecs[i], iov_batch_size));
    if (wr_size <= 0) {
      PERFETTO_PLOG("writev() failed");
      stop_writing_into_file = true;
      break;
    }
    total_wr_size += static_cast<size_t>(wr_size);
  }

  tracing_session->bytes_written_into_file += total_wr_size;

  PERFETTO_DLOG("Draining into file, written: %" PRIu64 " KB, stop: %d",
                (total_wr_size + 1023) / 1024, stop_writing_into_file);
  return stop_writing_into_file;
}

void TracingServiceImpl::FreeBuffers(TracingSessionID tsid) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  PERFETTO_DLOG("Freeing buffers for session %" PRIu64, tsid);
  TracingSession* tracing_session = GetTracingSession(tsid);
  if (!tracing_session) {
    PERFETTO_DLOG("FreeBuffers() failed, invalid session ID %" PRIu64, tsid);
    return;  // TODO(primiano): signal failure?
  }
  DisableTracing(tsid, /*disable_immediately=*/true);

  PERFETTO_DCHECK(tracing_session->AllDataSourceInstancesStopped());
  tracing_session->data_source_instances.clear();

  for (auto& producer_entry : producers_) {
    ProducerEndpointImpl* producer = producer_entry.second;
    producer->OnFreeBuffers(tracing_session->buffers_index);
  }

  for (BufferID buffer_id : tracing_session->buffers_index) {
    buffer_ids_.Free(buffer_id);
    PERFETTO_DCHECK(buffers_.count(buffer_id) == 1);
    buffers_.erase(buffer_id);
  }
  bool notify_traceur =
      tracing_session->config.notify_traceur() &&
      tracing_session->state != TracingSession::CLONED_READ_ONLY;
  bool is_long_trace =
      (tracing_session->config.write_into_file() &&
       tracing_session->config.file_write_period_ms() < kMillisPerDay);
  auto pending_clones = std::move(tracing_session->pending_clones);
  tracing_sessions_.erase(tsid);
  tracing_session = nullptr;
  UpdateMemoryGuardrail();

  for (const auto& id_to_clone_op : pending_clones) {
    const PendingClone& clone_op = id_to_clone_op.second;
    if (clone_op.weak_consumer) {
      weak_runner_.task_runner()->PostTask(
          [weak_consumer = clone_op.weak_consumer] {
            if (weak_consumer) {
              weak_consumer->consumer_->OnSessionCloned(
                  {false, "Original session ended", {}});
            }
          });
    }
  }

  PERFETTO_LOG("Tracing session %" PRIu64 " ended, total sessions:%zu", tsid,
               tracing_sessions_.size());
#if PERFETTO_BUILDFLAG(PERFETTO_ANDROID_BUILD) && \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  if (notify_traceur && is_long_trace) {
    PERFETTO_LAZY_LOAD(android_internal::NotifyTraceSessionEnded, notify_fn);
    if (!notify_fn || !notify_fn(/*session_stolen=*/false))
      PERFETTO_ELOG("Failed to notify Traceur long tracing has ended");
  }
#else
  base::ignore_result(notify_traceur);
  base::ignore_result(is_long_trace);
#endif
}

void TracingServiceImpl::RegisterDataSource(ProducerID producer_id,
                                            const DataSourceDescriptor& desc) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (desc.name().empty()) {
    PERFETTO_DLOG("Received RegisterDataSource() with empty name");
    return;
  }

  ProducerEndpointImpl* producer = GetProducer(producer_id);
  if (!producer) {
    PERFETTO_DFATAL("Producer not found.");
    return;
  }

  // Check that the producer doesn't register two data sources with the same ID.
  // Note that we tolerate |id| == 0 because until Android T / v22 the |id|
  // field didn't exist.
  for (const auto& kv : data_sources_) {
    if (desc.id() && kv.second.producer_id == producer_id &&
        kv.second.descriptor.id() == desc.id()) {
      PERFETTO_ELOG(
          "Failed to register data source \"%s\". A data source with the same "
          "id %" PRIu64 " (name=\"%s\") is already registered for producer %d",
          desc.name().c_str(), desc.id(), kv.second.descriptor.name().c_str(),
          producer_id);
      return;
    }
  }

  PERFETTO_DLOG("Producer %" PRIu16 " registered data source \"%s\"",
                producer_id, desc.name().c_str());

  auto reg_ds = data_sources_.emplace(desc.name(),
                                      RegisteredDataSource{producer_id, desc});

  // If there are existing tracing sessions, we need to check if the new
  // data source is enabled by any of them.
  for (auto& iter : tracing_sessions_) {
    TracingSession& tracing_session = iter.second;
    if (tracing_session.state != TracingSession::STARTED &&
        tracing_session.state != TracingSession::CONFIGURED) {
      continue;
    }

    TraceConfig::ProducerConfig producer_config;
    for (const auto& config : tracing_session.config.producers()) {
      if (producer->name_ == config.producer_name()) {
        producer_config = config;
        break;
      }
    }
    for (const TraceConfig::DataSource& cfg_data_source :
         tracing_session.config.data_sources()) {
      if (cfg_data_source.config().name() != desc.name())
        continue;
      DataSourceInstance* ds_inst = SetupDataSource(
          cfg_data_source, producer_config, reg_ds->second, &tracing_session);
      if (ds_inst && tracing_session.state == TracingSession::STARTED)
        StartDataSourceInstance(producer, &tracing_session, ds_inst);
    }
  }  // for(iter : tracing_sessions_)
}

void TracingServiceImpl::UpdateDataSource(
    ProducerID producer_id,
    const DataSourceDescriptor& new_desc) {
  if (new_desc.id() == 0) {
    PERFETTO_ELOG("UpdateDataSource() must have a non-zero id");
    return;
  }

  // If this producer has already registered a matching descriptor name and id,
  // just update the descriptor.
  RegisteredDataSource* data_source = nullptr;
  auto range = data_sources_.equal_range(new_desc.name());
  for (auto it = range.first; it != range.second; ++it) {
    if (it->second.producer_id == producer_id &&
        it->second.descriptor.id() == new_desc.id()) {
      data_source = &it->second;
      break;
    }
  }

  if (!data_source) {
    PERFETTO_ELOG(
        "UpdateDataSource() failed, could not find an existing data source "
        "with name=\"%s\" id=%" PRIu64,
        new_desc.name().c_str(), new_desc.id());
    return;
  }

  data_source->descriptor = new_desc;
}

void TracingServiceImpl::StopDataSourceInstance(ProducerEndpointImpl* producer,
                                                TracingSession* tracing_session,
                                                DataSourceInstance* instance,
                                                bool disable_immediately) {
  const DataSourceInstanceID ds_inst_id = instance->instance_id;
  if (producer->IsAndroidProcessFrozen()) {
    PERFETTO_DLOG(
        "skipping waiting of data source \"%s\" on producer \"%s\" (pid=%u) "
        "because it is frozen",
        instance->data_source_name.c_str(), producer->name_.c_str(),
        producer->pid());
    disable_immediately = true;
  }
  if (instance->will_notify_on_stop && !disable_immediately) {
    instance->state = DataSourceInstance::STOPPING;
  } else {
    instance->state = DataSourceInstance::STOPPED;
  }
  if (tracing_session->consumer_maybe_null) {
    tracing_session->consumer_maybe_null->OnDataSourceInstanceStateChange(
        *producer, *instance);
  }
  producer->StopDataSource(ds_inst_id);
}

void TracingServiceImpl::UnregisterDataSource(ProducerID producer_id,
                                              const std::string& name) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  PERFETTO_DLOG("Producer %" PRIu16 " unregistered data source \"%s\"",
                producer_id, name.c_str());
  PERFETTO_CHECK(producer_id);
  ProducerEndpointImpl* producer = GetProducer(producer_id);
  PERFETTO_DCHECK(producer);
  for (auto& kv : tracing_sessions_) {
    auto& ds_instances = kv.second.data_source_instances;
    bool removed = false;
    for (auto it = ds_instances.begin(); it != ds_instances.end();) {
      if (it->first == producer_id && it->second.data_source_name == name) {
        DataSourceInstanceID ds_inst_id = it->second.instance_id;
        if (it->second.state != DataSourceInstance::STOPPED) {
          if (it->second.state != DataSourceInstance::STOPPING) {
            StopDataSourceInstance(producer, &kv.second, &it->second,
                                   /* disable_immediately = */ false);
          }

          // Mark the instance as stopped immediately, since we are
          // unregistering it below.
          //
          //  The StopDataSourceInstance above might have set the state to
          //  STOPPING so this condition isn't an else.
          if (it->second.state == DataSourceInstance::STOPPING)
            NotifyDataSourceStopped(producer_id, ds_inst_id);
        }
        it = ds_instances.erase(it);
        removed = true;
      } else {
        ++it;
      }
    }  // for (data_source_instances)
    if (removed)
      MaybeNotifyAllDataSourcesStarted(&kv.second);
  }  // for (tracing_session)

  for (auto it = data_sources_.begin(); it != data_sources_.end(); ++it) {
    if (it->second.producer_id == producer_id &&
        it->second.descriptor.name() == name) {
      data_sources_.erase(it);
      return;
    }
  }

  PERFETTO_DFATAL(
      "Tried to unregister a non-existent data source \"%s\" for "
      "producer %" PRIu16,
      name.c_str(), producer_id);
}

bool TracingServiceImpl::IsInitiatorPrivileged(
    const TracingSession& tracing_session) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  if (tracing_session.consumer_uid == 1066 /* AID_STATSD */ &&
      tracing_session.config.statsd_metadata().triggering_config_uid() !=
          2000 /* AID_SHELL */
      && tracing_session.config.statsd_metadata().triggering_config_uid() !=
             0 /* AID_ROOT */) {
    // StatsD can be triggered either by shell, root or an app that has DUMP and
    // USAGE_STATS permission. When triggered by shell or root, we do not want
    // to consider the trace a trusted system trace, as it was initiated by the
    // user. Otherwise, it has to come from an app with DUMP and
    // PACKAGE_USAGE_STATS, which has to be preinstalled and trusted by the
    // system.
    // Check for shell / root: https://bit.ly/3b7oZNi
    // Check for DUMP or PACKAGE_USAGE_STATS: https://bit.ly/3ep0NrR
    return true;
  }
  if (tracing_session.consumer_uid == 1000 /* AID_SYSTEM */) {
    // AID_SYSTEM is considered a privileged initiator so that system_server can
    // profile apps that are not profileable by shell. Other AID_SYSTEM
    // processes are not allowed by SELinux to connect to the consumer socket or
    // to exec perfetto.
    return true;
  }
#else
  base::ignore_result(tracing_session);
#endif
  return false;
}

TracingServiceImpl::DataSourceInstance* TracingServiceImpl::SetupDataSource(
    const TraceConfig::DataSource& cfg_data_source,
    const TraceConfig::ProducerConfig& producer_config,
    const RegisteredDataSource& data_source,
    TracingSession* tracing_session) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  ProducerEndpointImpl* producer = GetProducer(data_source.producer_id);
  PERFETTO_DCHECK(producer);
  // An existing producer that is not ftrace could have registered itself as
  // ftrace, we must not enable it in that case.
  if (lockdown_mode_ && producer->uid() != uid_) {
    PERFETTO_DLOG("Lockdown mode: not enabling producer %hu", producer->id_);
    return nullptr;
  }
  // TODO(primiano): Add tests for registration ordering (data sources vs
  // consumers).
  if (!NameMatchesFilter(producer->name_,
                         cfg_data_source.producer_name_filter(),
                         cfg_data_source.producer_name_regex_filter())) {
    PERFETTO_DLOG("Data source: %s is filtered out for producer: %s",
                  cfg_data_source.config().name().c_str(),
                  producer->name_.c_str());
    return nullptr;
  }

  auto relative_buffer_id = cfg_data_source.config().target_buffer();
  if (relative_buffer_id >= tracing_session->num_buffers()) {
    PERFETTO_LOG(
        "The TraceConfig for DataSource %s specified a target_buffer out of "
        "bound (%u). Skipping it.",
        cfg_data_source.config().name().c_str(), relative_buffer_id);
    return nullptr;
  }

  // Create a copy of the DataSourceConfig specified in the trace config. This
  // will be passed to the producer after translating the |target_buffer| id.
  // The |target_buffer| parameter passed by the consumer in the trace config is
  // relative to the buffers declared in the same trace config. This has to be
  // translated to the global BufferID before passing it to the producers, which
  // don't know anything about tracing sessions and consumers.

  DataSourceInstanceID inst_id = ++last_data_source_instance_id_;
  auto insert_iter = tracing_session->data_source_instances.emplace(
      std::piecewise_construct,  //
      std::forward_as_tuple(producer->id_),
      std::forward_as_tuple(
          inst_id,
          cfg_data_source.config(),  //  Deliberate copy.
          data_source.descriptor.name(),
          data_source.descriptor.will_notify_on_start(),
          data_source.descriptor.will_notify_on_stop(),
          data_source.descriptor.handles_incremental_state_clear(),
          data_source.descriptor.no_flush()));
  DataSourceInstance* ds_instance = &insert_iter->second;

  // New data source instance starts out in CONFIGURED state.
  if (tracing_session->consumer_maybe_null) {
    tracing_session->consumer_maybe_null->OnDataSourceInstanceStateChange(
        *producer, *ds_instance);
  }

  DataSourceConfig& ds_config = ds_instance->config;
  ds_config.set_trace_duration_ms(tracing_session->config.duration_ms());

  // Rationale for `if (prefer) set_prefer(true)`, rather than `set(prefer)`:
  // ComputeStartupConfigHash() in tracing_muxer_impl.cc compares hashes of the
  // DataSourceConfig and expects to know (and clear) the fields generated by
  // the tracing service. Unconditionally adding a new field breaks backward
  // compatibility of startup tracing with older SDKs, because the serialization
  // also propagates unkonwn fields, breaking the hash matching check.
  if (tracing_session->config.prefer_suspend_clock_for_duration())
    ds_config.set_prefer_suspend_clock_for_duration(true);

  ds_config.set_stop_timeout_ms(tracing_session->data_source_stop_timeout_ms());
  ds_config.set_enable_extra_guardrails(
      tracing_session->config.enable_extra_guardrails());
  if (IsInitiatorPrivileged(*tracing_session)) {
    ds_config.set_session_initiator(
        DataSourceConfig::SESSION_INITIATOR_TRUSTED_SYSTEM);
  } else {
    // Unset in case the consumer set it.
    // We need to be able to trust this field.
    ds_config.set_session_initiator(
        DataSourceConfig::SESSION_INITIATOR_UNSPECIFIED);
  }
  ds_config.set_tracing_session_id(tracing_session->id);
  BufferID global_id = tracing_session->buffers_index[relative_buffer_id];
  PERFETTO_DCHECK(global_id);
  ds_config.set_target_buffer(global_id);

  PERFETTO_DLOG("Setting up data source %s with target buffer %" PRIu16,
                ds_config.name().c_str(), global_id);
  if (!producer->shared_memory()) {
    // Determine the SMB page size. Must be an integer multiple of 4k.
    // As for the SMB size below, the decision tree is as follows:
    // 1. Give priority to what is defined in the trace config.
    // 2. If unset give priority to the hint passed by the producer.
    // 3. Keep within bounds and ensure it's a multiple of 4k.
    size_t page_size = producer_config.page_size_kb() * 1024;
    if (page_size == 0)
      page_size = producer->shmem_page_size_hint_bytes_;

    // Determine the SMB size. Must be an integer multiple of the SMB page size.
    // The decision tree is as follows:
    // 1. Give priority to what defined in the trace config.
    // 2. If unset give priority to the hint passed by the producer.
    // 3. Keep within bounds and ensure it's a multiple of the page size.
    size_t shm_size = producer_config.shm_size_kb() * 1024;
    if (shm_size == 0)
      shm_size = producer->shmem_size_hint_bytes_;

    auto valid_sizes = EnsureValidShmSizes(shm_size, page_size);
    if (valid_sizes != std::tie(shm_size, page_size)) {
      PERFETTO_DLOG(
          "Invalid configured SMB sizes: shm_size %zu page_size %zu. Falling "
          "back to shm_size %zu page_size %zu.",
          shm_size, page_size, std::get<0>(valid_sizes),
          std::get<1>(valid_sizes));
    }
    std::tie(shm_size, page_size) = valid_sizes;

    // TODO(primiano): right now Create() will suicide in case of OOM if the
    // mmap fails. We should instead gracefully fail the request and tell the
    // client to go away.
    PERFETTO_DLOG("Creating SMB of %zu KB for producer \"%s\"", shm_size / 1024,
                  producer->name_.c_str());
    auto shared_memory = shm_factory_->CreateSharedMemory(shm_size);
    producer->SetupSharedMemory(std::move(shared_memory), page_size,
                                /*provided_by_producer=*/false);
  }
  producer->SetupDataSource(inst_id, ds_config);
  return ds_instance;
}

// Note: all the fields % *_trusted ones are untrusted, as in, the Producer
// might be lying / returning garbage contents. |src| and |size| can be trusted
// in terms of being a valid pointer, but not the contents.
void TracingServiceImpl::CopyProducerPageIntoLogBuffer(
    ProducerID producer_id_trusted,
    const ClientIdentity& client_identity_trusted,
    WriterID writer_id,
    ChunkID chunk_id,
    BufferID buffer_id,
    uint16_t num_fragments,
    uint8_t chunk_flags,
    bool chunk_complete,
    const uint8_t* src,
    size_t size) {
  PERFETTO_DCHECK_THREAD(thread_checker_);

  ProducerEndpointImpl* producer = GetProducer(producer_id_trusted);
  if (!producer) {
    PERFETTO_DFATAL("Producer not found.");
    chunks_discarded_++;
    return;
  }

  TraceBuffer* buf = GetBufferByID(buffer_id);
  if (!buf) {
    PERFETTO_DLOG("Could not find target buffer %" PRIu16
                  " for producer %" PRIu16,
                  buffer_id, producer_id_trusted);
    chunks_discarded_++;
    return;
  }

  // Verify that the producer is actually allowed to write into the target
  // buffer specified in the request. This prevents a malicious producer from
  // injecting data into a log buffer that belongs to a tracing session the
  // producer is not part of.
  if (!producer->is_allowed_target_buffer(buffer_id)) {
    PERFETTO_ELOG("Producer %" PRIu16
                  " tried to write into forbidden target buffer %" PRIu16,
                  producer_id_trusted, buffer_id);
    PERFETTO_DFATAL("Forbidden target buffer");
    chunks_discarded_++;
    return;
  }

  // If the writer was registered by the producer, it should only write into the
  // buffer it was registered with.
  std::optional<BufferID> associated_buffer =
      producer->buffer_id_for_writer(writer_id);
  if (associated_buffer && *associated_buffer != buffer_id) {
    PERFETTO_ELOG("Writer %" PRIu16 " of producer %" PRIu16
                  " was registered to write into target buffer %" PRIu16
                  ", but tried to write into buffer %" PRIu16,
                  writer_id, producer_id_trusted, *associated_buffer,
                  buffer_id);
    PERFETTO_DFATAL("Wrong target buffer");
    chunks_discarded_++;
    return;
  }

  buf->CopyChunkUntrusted(producer_id_trusted, client_identity_trusted,
                          writer_id, chunk_id, num_fragments, chunk_flags,
                          chunk_complete, src, size);
}

void TracingServiceImpl::ApplyChunkPatches(
    ProducerID producer_id_trusted,
    const std::vector<CommitDataRequest::ChunkToPatch>& chunks_to_patch) {
  PERFETTO_DCHECK_THREAD(thread_checker_);

  for (const auto& chunk : chunks_to_patch) {
    const ChunkID chunk_id = static_cast<ChunkID>(chunk.chunk_id());
    const WriterID writer_id = static_cast<WriterID>(chunk.writer_id());
    TraceBuffer* buf =
        GetBufferByID(static_cast<BufferID>(chunk.target_buffer()));
    static_assert(std::numeric_limits<ChunkID>::max() == kMaxChunkID,
                  "Add a '|| chunk_id > kMaxChunkID' below if this fails");
    if (!writer_id || writer_id > kMaxWriterID || !buf) {
      // This can genuinely happen when the trace is stopped. The producers
      // might see the stop signal with some delay and try to keep sending
      // patches left soon after.
      PERFETTO_DLOG(
          "Received invalid chunks_to_patch request from Producer: %" PRIu16
          ", BufferID: %" PRIu32 " ChunkdID: %" PRIu32 " WriterID: %" PRIu16,
          producer_id_trusted, chunk.target_buffer(), chunk_id, writer_id);
      patches_discarded_ += static_cast<uint64_t>(chunk.patches_size());
      continue;
    }

    // Note, there's no need to validate that the producer is allowed to write
    // to the specified buffer ID (or that it's the correct buffer ID for a
    // registered TraceWriter). That's because TraceBuffer uses the producer ID
    // and writer ID to look up the chunk to patch. If the producer specifies an
    // incorrect buffer, this lookup will fail and TraceBuffer will ignore the
    // patches. Because the producer ID is trusted, there's also no way for a
    // malicious producer to patch another producer's data.

    // Speculate on the fact that there are going to be a limited amount of
    // patches per request, so we can allocate the |patches| array on the stack.
    std::array<TraceBuffer::Patch, 1024> patches;  // Uninitialized.
    if (chunk.patches().size() > patches.size()) {
      PERFETTO_ELOG("Too many patches (%zu) batched in the same request",
                    patches.size());
      PERFETTO_DFATAL("Too many patches");
      patches_discarded_ += static_cast<uint64_t>(chunk.patches_size());
      continue;
    }

    size_t i = 0;
    for (const auto& patch : chunk.patches()) {
      const std::string& patch_data = patch.data();
      if (patch_data.size() != patches[i].data.size()) {
        PERFETTO_ELOG("Received patch from producer: %" PRIu16
                      " of unexpected size %zu",
                      producer_id_trusted, patch_data.size());
        patches_discarded_++;
        continue;
      }
      patches[i].offset_untrusted = patch.offset();
      memcpy(&patches[i].data[0], patch_data.data(), patches[i].data.size());
      i++;
    }
    buf->TryPatchChunkContents(producer_id_trusted, writer_id, chunk_id,
                               &patches[0], i, chunk.has_more_patches());
  }
}

TracingServiceImpl::TracingSession* TracingServiceImpl::GetDetachedSession(
    uid_t uid,
    const std::string& key) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  for (auto& kv : tracing_sessions_) {
    TracingSession* session = &kv.second;
    if (session->consumer_uid == uid && session->detach_key == key) {
      PERFETTO_DCHECK(session->consumer_maybe_null == nullptr);
      return session;
    }
  }
  return nullptr;
}

TracingServiceImpl::TracingSession* TracingServiceImpl::GetTracingSession(
    TracingSessionID tsid) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  auto it = tsid ? tracing_sessions_.find(tsid) : tracing_sessions_.end();
  if (it == tracing_sessions_.end())
    return nullptr;
  return &it->second;
}

TracingServiceImpl::TracingSession*
TracingServiceImpl::GetTracingSessionByUniqueName(
    const std::string& unique_session_name) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (unique_session_name.empty()) {
    return nullptr;
  }
  for (auto& session_id_and_session : tracing_sessions_) {
    TracingSession& session = session_id_and_session.second;
    if (session.state == TracingSession::CLONED_READ_ONLY) {
      continue;
    }
    if (session.config.unique_session_name() == unique_session_name) {
      return &session;
    }
  }
  return nullptr;
}

TracingServiceImpl::TracingSession*
TracingServiceImpl::FindTracingSessionWithMaxBugreportScore() {
  TracingSession* max_session = nullptr;
  for (auto& session_id_and_session : tracing_sessions_) {
    auto& session = session_id_and_session.second;
    const int32_t score = session.config.bugreport_score();
    // Exclude sessions with 0 (or below) score. By default tracing sessions
    // should NOT be eligible to be attached to bugreports.
    if (score <= 0 || session.state != TracingSession::STARTED)
      continue;

    if (!max_session || score > max_session->config.bugreport_score())
      max_session = &session;
  }
  return max_session;
}

ProducerID TracingServiceImpl::GetNextProducerID() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  PERFETTO_CHECK(producers_.size() < kMaxProducerID);
  do {
    ++last_producer_id_;
  } while (producers_.count(last_producer_id_) || last_producer_id_ == 0);
  PERFETTO_DCHECK(last_producer_id_ > 0 && last_producer_id_ <= kMaxProducerID);
  return last_producer_id_;
}

TraceBuffer* TracingServiceImpl::GetBufferByID(BufferID buffer_id) {
  auto buf_iter = buffers_.find(buffer_id);
  if (buf_iter == buffers_.end())
    return nullptr;
  return &*buf_iter->second;
}

void TracingServiceImpl::OnStartTriggersTimeout(TracingSessionID tsid) {
  // Skip entirely the flush if the trace session doesn't exist anymore.
  // This is to prevent misleading error messages to be logged.
  //
  // if the trace has started from the trigger we rely on
  // the |stop_delay_ms| from the trigger so don't flush and
  // disable if we've moved beyond a CONFIGURED state
  auto* tracing_session_ptr = GetTracingSession(tsid);
  if (tracing_session_ptr &&
      tracing_session_ptr->state == TracingSession::CONFIGURED) {
    PERFETTO_DLOG("Disabling TracingSession %" PRIu64
                  " since no triggers activated.",
                  tsid);
    // No data should be returned from ReadBuffers() regardless of if we
    // call FreeBuffers() or DisableTracing(). This is because in
    // STOP_TRACING we need this promise in either case, and using
    // DisableTracing() allows a graceful shutdown. Consumers can follow
    // their normal path and check the buffers through ReadBuffers() and
    // the code won't hang because the tracing session will still be
    // alive just disabled.
    DisableTracing(tsid);
  }
}

void TracingServiceImpl::UpdateMemoryGuardrail() {
#if PERFETTO_BUILDFLAG(PERFETTO_WATCHDOG)
  uint64_t total_buffer_bytes = 0;

  // Sum up all the shared memory buffers.
  for (const auto& id_to_producer : producers_) {
    if (id_to_producer.second->shared_memory())
      total_buffer_bytes += id_to_producer.second->shared_memory()->size();
  }

  // Sum up all the trace buffers.
  for (const auto& id_to_buffer : buffers_) {
    total_buffer_bytes += id_to_buffer.second->size();
  }

  // Sum up all the cloned traced buffers.
  for (const auto& id_to_ts : tracing_sessions_) {
    const TracingSession& ts = id_to_ts.second;
    for (const auto& id_to_clone_op : ts.pending_clones) {
      const PendingClone& clone_op = id_to_clone_op.second;
      for (const std::unique_ptr<TraceBuffer>& buf : clone_op.buffers) {
        if (buf) {
          total_buffer_bytes += buf->size();
        }
      }
    }
  }

  // Set the guard rail to 32MB + the sum of all the buffers over a 30 second
  // interval.
  uint64_t guardrail = base::kWatchdogDefaultMemorySlack + total_buffer_bytes;
  base::Watchdog::GetInstance()->SetMemoryLimit(guardrail, 30 * 1000);
#endif
}

void TracingServiceImpl::PeriodicSnapshotTask(TracingSessionID tsid) {
  auto* tracing_session = GetTracingSession(tsid);
  if (!tracing_session)
    return;
  if (tracing_session->state != TracingSession::STARTED)
    return;
  tracing_session->should_emit_sync_marker = true;
  tracing_session->should_emit_stats = true;
  MaybeSnapshotClocksIntoRingBuffer(tracing_session);
}

void TracingServiceImpl::SnapshotLifecycleEvent(TracingSession* tracing_session,
                                                uint32_t field_id,
                                                bool snapshot_clocks) {
  // field_id should be an id of a field in TracingServiceEvent.
  auto& lifecycle_events = tracing_session->lifecycle_events;
  auto event_it =
      std::find_if(lifecycle_events.begin(), lifecycle_events.end(),
                   [field_id](const TracingSession::LifecycleEvent& event) {
                     return event.field_id == field_id;
                   });

  TracingSession::LifecycleEvent* event;
  if (event_it == lifecycle_events.end()) {
    lifecycle_events.emplace_back(field_id);
    event = &lifecycle_events.back();
  } else {
    event = &*event_it;
  }

  // Snapshot the clocks before capturing the timestamp for the event so we can
  // use this snapshot to resolve the event timestamp if necessary.
  if (snapshot_clocks)
    MaybeSnapshotClocksIntoRingBuffer(tracing_session);

  // Erase before emplacing to prevent a unncessary doubling of memory if
  // not needed.
  if (event->timestamps.size() >= event->max_size) {
    event->timestamps.erase_front(1 + event->timestamps.size() -
                                  event->max_size);
  }
  event->timestamps.emplace_back(clock_->GetBootTimeNs().count());
}

void TracingServiceImpl::SetSingleLifecycleEvent(
    TracingSession* tracing_session,
    uint32_t field_id,
    int64_t boot_timestamp_ns) {
  // field_id should be an id of a field in TracingServiceEvent.
  auto& lifecycle_events = tracing_session->lifecycle_events;
  auto event_it =
      std::find_if(lifecycle_events.begin(), lifecycle_events.end(),
                   [field_id](const TracingSession::LifecycleEvent& event) {
                     return event.field_id == field_id;
                   });

  TracingSession::LifecycleEvent* event;
  if (event_it == lifecycle_events.end()) {
    lifecycle_events.emplace_back(field_id);
    event = &lifecycle_events.back();
  } else {
    event = &*event_it;
  }

  event->timestamps.clear();
  event->timestamps.emplace_back(boot_timestamp_ns);
}

void TracingServiceImpl::MaybeSnapshotClocksIntoRingBuffer(
    TracingSession* tracing_session) {
  if (tracing_session->config.builtin_data_sources()
          .disable_clock_snapshotting()) {
    return;
  }

  // We are making an explicit copy of the latest snapshot (if it exists)
  // because SnapshotClocks reads this data and computes the drift based on its
  // content. If the clock drift is high enough, it will update the contents of
  // |snapshot| and return true. Otherwise, it will return false.
  TracingSession::ClockSnapshotData snapshot =
      tracing_session->clock_snapshot_ring_buffer.empty()
          ? TracingSession::ClockSnapshotData()
          : tracing_session->clock_snapshot_ring_buffer.back();
  bool did_update = SnapshotClocks(&snapshot);
  if (did_update) {
    // This means clocks drifted enough since last snapshot. See the comment
    // in SnapshotClocks.
    auto* snapshot_buffer = &tracing_session->clock_snapshot_ring_buffer;

    // Erase before emplacing to prevent a unncessary doubling of memory if
    // not needed.
    static constexpr uint32_t kClockSnapshotRingBufferSize = 16;
    if (snapshot_buffer->size() >= kClockSnapshotRingBufferSize) {
      snapshot_buffer->erase_front(1 + snapshot_buffer->size() -
                                   kClockSnapshotRingBufferSize);
    }
    snapshot_buffer->emplace_back(std::move(snapshot));
  }
}

// Returns true when the data in |snapshot_data| is updated with the new state
// of the clocks and false otherwise.
bool TracingServiceImpl::SnapshotClocks(
    TracingSession::ClockSnapshotData* snapshot_data) {
  // Minimum drift that justifies replacing a prior clock snapshot that hasn't
  // been emitted into the trace yet (see comment below).
  static constexpr int64_t kSignificantDriftNs = 10 * 1000 * 1000;  // 10 ms

  TracingSession::ClockSnapshotData new_snapshot_data =
      base::CaptureClockSnapshots();
  // If we're about to update a session's latest clock snapshot that hasn't been
  // emitted into the trace yet, check whether the clocks have drifted enough to
  // warrant overriding the current snapshot values. The older snapshot would be
  // valid for a larger part of the currently buffered trace data because the
  // clock sync protocol in trace processor uses the latest clock <= timestamp
  // to translate times (see https://perfetto.dev/docs/concepts/clock-sync), so
  // we try to keep it if we can.
  if (!snapshot_data->empty()) {
    PERFETTO_DCHECK(snapshot_data->size() == new_snapshot_data.size());
    PERFETTO_DCHECK((*snapshot_data)[0].clock_id ==
                    protos::gen::BUILTIN_CLOCK_BOOTTIME);

    bool update_snapshot = false;
    uint64_t old_boot_ns = (*snapshot_data)[0].timestamp;
    uint64_t new_boot_ns = new_snapshot_data[0].timestamp;
    int64_t boot_diff =
        static_cast<int64_t>(new_boot_ns) - static_cast<int64_t>(old_boot_ns);

    for (size_t i = 1; i < snapshot_data->size(); i++) {
      uint64_t old_ns = (*snapshot_data)[i].timestamp;
      uint64_t new_ns = new_snapshot_data[i].timestamp;

      int64_t diff =
          static_cast<int64_t>(new_ns) - static_cast<int64_t>(old_ns);

      // Compare the boottime delta against the delta of this clock.
      if (std::abs(boot_diff - diff) >= kSignificantDriftNs) {
        update_snapshot = true;
        break;
      }
    }
    if (!update_snapshot)
      return false;
    snapshot_data->clear();
  }

  *snapshot_data = std::move(new_snapshot_data);
  return true;
}

void TracingServiceImpl::EmitClockSnapshot(
    TracingSession* tracing_session,
    TracingSession::ClockSnapshotData snapshot_data,
    std::vector<TracePacket>* packets) {
  PERFETTO_DCHECK(!tracing_session->config.builtin_data_sources()
                       .disable_clock_snapshotting());

  protozero::HeapBuffered<protos::pbzero::TracePacket> packet;
  auto* snapshot = packet->set_clock_snapshot();

  protos::gen::BuiltinClock trace_clock =
      tracing_session->config.builtin_data_sources().primary_trace_clock();
  if (!trace_clock)
    trace_clock = protos::gen::BUILTIN_CLOCK_BOOTTIME;
  snapshot->set_primary_trace_clock(
      static_cast<protos::pbzero::BuiltinClock>(trace_clock));

  for (auto& clock_id_and_ts : snapshot_data) {
    auto* c = snapshot->add_clocks();
    c->set_clock_id(clock_id_and_ts.clock_id);
    c->set_timestamp(clock_id_and_ts.timestamp);
  }

  packet->set_trusted_uid(static_cast<int32_t>(uid_));
  packet->set_trusted_packet_sequence_id(kServicePacketSequenceID);
  SerializeAndAppendPacket(packets, packet.SerializeAsArray());
}

void TracingServiceImpl::EmitSyncMarker(std::vector<TracePacket>* packets) {
  // The sync marks are used to tokenize large traces efficiently.
  // See description in trace_packet.proto.
  if (sync_marker_packet_size_ == 0) {
    // The marker ABI expects that the marker is written after the uid.
    // Protozero guarantees that fields are written in the same order of the
    // calls. The ResynchronizeTraceStreamUsingSyncMarker test verifies the ABI.
    protozero::StaticBuffered<protos::pbzero::TracePacket> packet(
        &sync_marker_packet_[0], sizeof(sync_marker_packet_));
    packet->set_trusted_uid(static_cast<int32_t>(uid_));
    packet->set_trusted_packet_sequence_id(kServicePacketSequenceID);

    // Keep this last.
    packet->set_synchronization_marker(kSyncMarker, sizeof(kSyncMarker));
    sync_marker_packet_size_ = packet.Finalize();
  }
  packets->emplace_back();
  packets->back().AddSlice(&sync_marker_packet_[0], sync_marker_packet_size_);
}

void TracingServiceImpl::EmitStats(TracingSession* tracing_session,
                                   std::vector<TracePacket>* packets) {
  protozero::HeapBuffered<protos::pbzero::TracePacket> packet;
  packet->set_trusted_uid(static_cast<int32_t>(uid_));
  packet->set_trusted_packet_sequence_id(kServicePacketSequenceID);
  GetTraceStats(tracing_session).Serialize(packet->set_trace_stats());
  SerializeAndAppendPacket(packets, packet.SerializeAsArray());
}

TraceStats TracingServiceImpl::GetTraceStats(TracingSession* tracing_session) {
  TraceStats trace_stats;
  trace_stats.set_producers_connected(static_cast<uint32_t>(producers_.size()));
  trace_stats.set_producers_seen(last_producer_id_);
  trace_stats.set_data_sources_registered(
      static_cast<uint32_t>(data_sources_.size()));
  trace_stats.set_data_sources_seen(last_data_source_instance_id_);
  trace_stats.set_tracing_sessions(
      static_cast<uint32_t>(tracing_sessions_.size()));
  trace_stats.set_total_buffers(static_cast<uint32_t>(buffers_.size()));
  trace_stats.set_chunks_discarded(chunks_discarded_);
  trace_stats.set_patches_discarded(patches_discarded_);
  trace_stats.set_invalid_packets(tracing_session->invalid_packets);
  trace_stats.set_flushes_requested(tracing_session->flushes_requested);
  trace_stats.set_flushes_succeeded(tracing_session->flushes_succeeded);
  trace_stats.set_flushes_failed(tracing_session->flushes_failed);
  trace_stats.set_final_flush_outcome(tracing_session->final_flush_outcome);

  if (tracing_session->trace_filter) {
    auto* filt_stats = trace_stats.mutable_filter_stats();
    filt_stats->set_input_packets(tracing_session->filter_input_packets);
    filt_stats->set_input_bytes(tracing_session->filter_input_bytes);
    filt_stats->set_output_bytes(tracing_session->filter_output_bytes);
    filt_stats->set_errors(tracing_session->filter_errors);
    filt_stats->set_time_taken_ns(tracing_session->filter_time_taken_ns);
    for (uint64_t value : tracing_session->filter_bytes_discarded_per_buffer)
      filt_stats->add_bytes_discarded_per_buffer(value);
  }

  for (BufferID buf_id : tracing_session->buffers_index) {
    TraceBuffer* buf = GetBufferByID(buf_id);
    if (!buf) {
      PERFETTO_DFATAL("Buffer not found.");
      continue;
    }
    *trace_stats.add_buffer_stats() = buf->stats();
  }  // for (buf in session).

  if (!tracing_session->config.builtin_data_sources()
           .disable_chunk_usage_histograms()) {
    // Emit chunk usage stats broken down by sequence ID (i.e. by trace-writer).
    // Writer stats are updated by each TraceBuffer object at ReadBuffers time,
    // and there can be >1 buffer per session. A trace writer never writes to
    // more than one buffer (it's technically allowed but doesn't happen in the
    // current impl of the tracing SDK).

    bool has_written_bucket_definition = false;
    uint32_t buf_idx = static_cast<uint32_t>(-1);
    for (const BufferID buf_id : tracing_session->buffers_index) {
      ++buf_idx;
      const TraceBuffer* buf = GetBufferByID(buf_id);
      if (!buf)
        continue;
      for (auto it = buf->writer_stats().GetIterator(); it; ++it) {
        const auto& hist = it.value().used_chunk_hist;
        ProducerID p;
        WriterID w;
        GetProducerAndWriterID(it.key(), &p, &w);
        if (!has_written_bucket_definition) {
          // Serialize one-off the histogram bucket definition, which is the
          // same for all entries in the map.
          has_written_bucket_definition = true;
          // The -1 in the loop below is to skip the implicit overflow bucket.
          for (size_t i = 0; i < hist.num_buckets() - 1; ++i) {
            trace_stats.add_chunk_payload_histogram_def(hist.GetBucketThres(i));
          }
        }  // if(!has_written_bucket_definition)
        auto* wri_stats = trace_stats.add_writer_stats();
        wri_stats->set_sequence_id(
            tracing_session->GetPacketSequenceID(kDefaultMachineID, p, w));
        wri_stats->set_buffer(buf_idx);
        for (size_t i = 0; i < hist.num_buckets(); ++i) {
          wri_stats->add_chunk_payload_histogram_counts(hist.GetBucketCount(i));
          wri_stats->add_chunk_payload_histogram_sum(hist.GetBucketSum(i));
        }
      }  // for each sequence (writer).
    }  // for each buffer.
  }  // if (!disable_chunk_usage_histograms)

  return trace_stats;
}

void TracingServiceImpl::EmitUuid(TracingSession* tracing_session,
                                  std::vector<TracePacket>* packets) {
  protozero::HeapBuffered<protos::pbzero::TracePacket> packet;
  packet->set_trusted_uid(static_cast<int32_t>(uid_));
  packet->set_trusted_packet_sequence_id(kServicePacketSequenceID);
  auto* uuid = packet->set_trace_uuid();
  uuid->set_lsb(tracing_session->trace_uuid.lsb());
  uuid->set_msb(tracing_session->trace_uuid.msb());
  SerializeAndAppendPacket(packets, packet.SerializeAsArray());
}

void TracingServiceImpl::MaybeEmitTraceConfig(
    TracingSession* tracing_session,
    std::vector<TracePacket>* packets) {
  if (tracing_session->did_emit_initial_packets)
    return;
  protozero::HeapBuffered<protos::pbzero::TracePacket> packet;
  packet->set_trusted_uid(static_cast<int32_t>(uid_));
  packet->set_trusted_packet_sequence_id(kServicePacketSequenceID);
  tracing_session->config.Serialize(packet->set_trace_config());
  SerializeAndAppendPacket(packets, packet.SerializeAsArray());
}

void TracingServiceImpl::EmitSystemInfo(std::vector<TracePacket>* packets) {
  protozero::HeapBuffered<protos::pbzero::TracePacket> packet;
  auto* info = packet->set_system_info();

  base::SystemInfo sys_info = base::GetSystemInfo();
  info->set_tracing_service_version(base::GetVersionString());

  if (sys_info.timezone_off_mins.has_value())
    info->set_timezone_off_mins(*sys_info.timezone_off_mins);

  if (sys_info.utsname_info.has_value()) {
    auto* utsname_info = info->set_utsname();
    utsname_info->set_sysname(sys_info.utsname_info->sysname);
    utsname_info->set_version(sys_info.utsname_info->version);
    utsname_info->set_machine(sys_info.utsname_info->machine);
    utsname_info->set_release(sys_info.utsname_info->release);
  }

  if (sys_info.page_size.has_value())
    info->set_page_size(*sys_info.page_size);
  if (sys_info.num_cpus.has_value())
    info->set_num_cpus(*sys_info.num_cpus);

  if (!sys_info.android_build_fingerprint.empty())
    info->set_android_build_fingerprint(sys_info.android_build_fingerprint);
  if (!sys_info.android_device_manufacturer.empty())
    info->set_android_device_manufacturer(sys_info.android_device_manufacturer);
  if (sys_info.android_sdk_version.has_value())
    info->set_android_sdk_version(*sys_info.android_sdk_version);
  if (!sys_info.android_soc_model.empty())
    info->set_android_soc_model(sys_info.android_soc_model);
  if (!sys_info.android_guest_soc_model.empty())
    info->set_android_guest_soc_model(sys_info.android_guest_soc_model);
  if (!sys_info.android_hardware_revision.empty())
    info->set_android_hardware_revision(sys_info.android_hardware_revision);
  if (!sys_info.android_storage_model.empty())
    info->set_android_storage_model(sys_info.android_storage_model);
  if (!sys_info.android_ram_model.empty())
    info->set_android_ram_model(sys_info.android_ram_model);
  if (!sys_info.android_serial_console.empty())
    info->set_android_serial_console(sys_info.android_serial_console);

  packet->set_trusted_uid(static_cast<int32_t>(uid_));
  packet->set_trusted_packet_sequence_id(kServicePacketSequenceID);
  SerializeAndAppendPacket(packets, packet.SerializeAsArray());
}

void TracingServiceImpl::MaybeEmitRemoteSystemInfo(
    std::vector<TracePacket>* packets) {
  std::unordered_set<MachineID> did_emit_machines;
  for (const auto& id_and_relay_client : relay_clients_) {
    const auto& relay_client = id_and_relay_client.second;
    auto machine_id = relay_client->machine_id();
    if (did_emit_machines.find(machine_id) != did_emit_machines.end())
      continue;  // Already emitted for the machine (e.g. multiple clients).

    if (relay_client->serialized_system_info().empty()) {
      PERFETTO_DLOG("System info not provided for machine ID = %" PRIu32,
                    machine_id);
      continue;
    }

    // Don't emit twice for the same machine.
    did_emit_machines.insert(machine_id);

    protozero::HeapBuffered<protos::pbzero::TracePacket> packet;
    auto& system_info = relay_client->serialized_system_info();

    packet->AppendBytes(kTracePacketSystemInfoFieldId, system_info.data(),
                        system_info.size());

    packet->set_machine_id(machine_id);
    packet->set_trusted_uid(static_cast<int32_t>(uid_));
    packet->set_trusted_packet_sequence_id(kServicePacketSequenceID);
    SerializeAndAppendPacket(packets, packet.SerializeAsArray());
  }
}

void TracingServiceImpl::EmitLifecycleEvents(
    TracingSession* tracing_session,
    std::vector<TracePacket>* packets) {
  using TimestampedPacket =
      std::pair<int64_t /* ts */, std::vector<uint8_t> /* serialized packet */>;

  std::vector<TimestampedPacket> timestamped_packets;
  for (auto& event : tracing_session->lifecycle_events) {
    for (int64_t ts : event.timestamps) {
      protozero::HeapBuffered<protos::pbzero::TracePacket> packet;
      packet->set_timestamp(static_cast<uint64_t>(ts));
      packet->set_trusted_uid(static_cast<int32_t>(uid_));
      packet->set_trusted_packet_sequence_id(kServicePacketSequenceID);

      auto* service_event = packet->set_service_event();
      service_event->AppendVarInt(event.field_id, 1);
      timestamped_packets.emplace_back(ts, packet.SerializeAsArray());
    }
    event.timestamps.clear();
  }

  if (tracing_session->slow_start_event.has_value()) {
    const TracingSession::ArbitraryLifecycleEvent& event =
        *tracing_session->slow_start_event;
    timestamped_packets.emplace_back(event.timestamp, std::move(event.data));
  }
  tracing_session->slow_start_event.reset();

  for (auto& event : tracing_session->last_flush_events) {
    timestamped_packets.emplace_back(event.timestamp, std::move(event.data));
  }
  tracing_session->last_flush_events.clear();

  for (size_t i = 0; i < tracing_session->buffer_cloned_timestamps.size();
       i++) {
    protozero::HeapBuffered<protos::pbzero::TracePacket> packet;
    int64_t ts = tracing_session->buffer_cloned_timestamps[i];
    packet->set_timestamp(static_cast<uint64_t>(ts));
    packet->set_trusted_uid(static_cast<int32_t>(uid_));
    packet->set_trusted_packet_sequence_id(kServicePacketSequenceID);

    auto* service_event = packet->set_service_event();
    service_event->set_buffer_cloned(static_cast<uint32_t>(i));

    timestamped_packets.emplace_back(ts, packet.SerializeAsArray());
  }
  tracing_session->buffer_cloned_timestamps.clear();

  // We sort by timestamp here to ensure that the "sequence" of lifecycle
  // packets has monotonic timestamps like other sequences in the trace.
  // Note that these events could still be out of order with respect to other
  // events on the service packet sequence (e.g. trigger received packets).
  std::sort(timestamped_packets.begin(), timestamped_packets.end(),
            [](const TimestampedPacket& a, const TimestampedPacket& b) {
              return a.first < b.first;
            });

  for (auto& pair : timestamped_packets)
    SerializeAndAppendPacket(packets, std::move(pair.second));
}

void TracingServiceImpl::MaybeEmitRemoteClockSync(
    TracingSession* tracing_session,
    std::vector<TracePacket>* packets) {
  if (tracing_session->did_emit_remote_clock_sync_)
    return;

  std::unordered_set<MachineID> did_emit_machines;
  for (const auto& id_and_relay_client : relay_clients_) {
    const auto& relay_client = id_and_relay_client.second;
    auto machine_id = relay_client->machine_id();
    if (did_emit_machines.find(machine_id) != did_emit_machines.end())
      continue;  // Already emitted for the machine (e.g. multiple clients).

    auto& sync_clock_snapshots = relay_client->synced_clocks();
    if (sync_clock_snapshots.empty()) {
      PERFETTO_DLOG("Clock not synchronized for machine ID = %" PRIu32,
                    machine_id);
      continue;
    }

    // Don't emit twice for the same machine.
    did_emit_machines.insert(machine_id);

    protozero::HeapBuffered<protos::pbzero::TracePacket> sync_packet;
    sync_packet->set_machine_id(machine_id);
    sync_packet->set_trusted_uid(static_cast<int32_t>(uid_));
    auto* remote_clock_sync = sync_packet->set_remote_clock_sync();
    for (const auto& sync_exchange : relay_client->synced_clocks()) {
      auto* sync_exchange_msg = remote_clock_sync->add_synced_clocks();

      auto* client_snapshots = sync_exchange_msg->set_client_clocks();
      for (const auto& client_clock : sync_exchange.client_clocks) {
        auto* clock = client_snapshots->add_clocks();
        clock->set_clock_id(client_clock.clock_id);
        clock->set_timestamp(client_clock.timestamp);
      }

      auto* host_snapshots = sync_exchange_msg->set_host_clocks();
      for (const auto& host_clock : sync_exchange.host_clocks) {
        auto* clock = host_snapshots->add_clocks();
        clock->set_clock_id(host_clock.clock_id);
        clock->set_timestamp(host_clock.timestamp);
      }
    }

    SerializeAndAppendPacket(packets, sync_packet.SerializeAsArray());
  }

  tracing_session->did_emit_remote_clock_sync_ = true;
}

void TracingServiceImpl::MaybeEmitCloneTrigger(
    TracingSession* tracing_session,
    std::vector<TracePacket>* packets) {
  if (tracing_session->did_emit_initial_packets)
    return;

  if (tracing_session->clone_trigger.has_value()) {
    protozero::HeapBuffered<protos::pbzero::TracePacket> packet;
    auto* trigger = packet->set_clone_snapshot_trigger();
    const auto& info = tracing_session->clone_trigger.value();
    trigger->set_trigger_name(info.trigger_name);
    trigger->set_producer_name(info.producer_name);
    trigger->set_trusted_producer_uid(static_cast<int32_t>(info.producer_uid));
    trigger->set_stop_delay_ms(info.trigger_delay_ms);

    packet->set_timestamp(info.boot_time_ns);
    packet->set_trusted_uid(static_cast<int32_t>(uid_));
    packet->set_trusted_packet_sequence_id(kServicePacketSequenceID);
    SerializeAndAppendPacket(packets, packet.SerializeAsArray());
  }
}

void TracingServiceImpl::MaybeEmitReceivedTriggers(
    TracingSession* tracing_session,
    std::vector<TracePacket>* packets) {
  PERFETTO_DCHECK(tracing_session->num_triggers_emitted_into_trace <=
                  tracing_session->received_triggers.size());
  for (size_t i = tracing_session->num_triggers_emitted_into_trace;
       i < tracing_session->received_triggers.size(); ++i) {
    const auto& info = tracing_session->received_triggers[i];
    protozero::HeapBuffered<protos::pbzero::TracePacket> packet;
    auto* trigger = packet->set_trigger();
    trigger->set_trigger_name(info.trigger_name);
    trigger->set_producer_name(info.producer_name);
    trigger->set_trusted_producer_uid(static_cast<int32_t>(info.producer_uid));
    trigger->set_stop_delay_ms(info.trigger_delay_ms);

    packet->set_timestamp(info.boot_time_ns);
    packet->set_trusted_uid(static_cast<int32_t>(uid_));
    packet->set_trusted_packet_sequence_id(kServicePacketSequenceID);
    SerializeAndAppendPacket(packets, packet.SerializeAsArray());
    ++tracing_session->num_triggers_emitted_into_trace;
  }
}

void TracingServiceImpl::MaybeLogUploadEvent(const TraceConfig& cfg,
                                             const base::Uuid& uuid,
                                             PerfettoStatsdAtom atom,
                                             const std::string& trigger_name) {
  if (!ShouldLogEvent(cfg))
    return;

  PERFETTO_DCHECK(uuid);  // The UUID must be set at this point.
  android_stats::MaybeLogUploadEvent(atom, uuid.lsb(), uuid.msb(),
                                     trigger_name);
}

void TracingServiceImpl::MaybeLogTriggerEvent(const TraceConfig& cfg,
                                              PerfettoTriggerAtom atom,
                                              const std::string& trigger_name) {
  if (!ShouldLogEvent(cfg))
    return;
  android_stats::MaybeLogTriggerEvent(atom, trigger_name);
}

size_t TracingServiceImpl::PurgeExpiredAndCountTriggerInWindow(
    int64_t now_ns,
    uint64_t trigger_name_hash) {
  constexpr int64_t kOneDayInNs = 24ll * 60 * 60 * 1000 * 1000 * 1000;
  PERFETTO_DCHECK(
      std::is_sorted(trigger_history_.begin(), trigger_history_.end()));
  size_t remove_count = 0;
  size_t trigger_count = 0;
  for (const TriggerHistory& h : trigger_history_) {
    if (h.timestamp_ns < now_ns - kOneDayInNs) {
      remove_count++;
    } else if (h.name_hash == trigger_name_hash) {
      trigger_count++;
    }
  }
  trigger_history_.erase_front(remove_count);
  return trigger_count;
}

base::Status TracingServiceImpl::FlushAndCloneSession(
    ConsumerEndpointImpl* consumer,
    ConsumerEndpoint::CloneSessionArgs args) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  auto clone_target = FlushFlags::CloneTarget::kUnknown;

  TracingSession* session = nullptr;
  if (args.for_bugreport) {
    clone_target = FlushFlags::CloneTarget::kBugreport;
  }
  if (args.tsid != 0) {
    if (args.tsid == kBugreportSessionId) {
      // This branch is only here to support the legacy protocol where we could
      // clone only a single session using the magic ID kBugreportSessionId.
      // The newer perfetto --clone-all-for-bugreport first queries the existing
      // sessions and then issues individual clone requests specifying real
      // session IDs, setting args.{for_bugreport,skip_trace_filter}=true.
      PERFETTO_LOG("Looking for sessions for bugreport");
      session = FindTracingSessionWithMaxBugreportScore();
      if (!session) {
        return base::ErrStatus(
            "No tracing sessions eligible for bugreport found");
      }
      args.tsid = session->id;
      clone_target = FlushFlags::CloneTarget::kBugreport;
      args.skip_trace_filter = true;
    } else {
      session = GetTracingSession(args.tsid);
    }
  } else if (!args.unique_session_name.empty()) {
    session = GetTracingSessionByUniqueName(args.unique_session_name);
  }

  if (!session) {
    return base::ErrStatus("Tracing session not found");
  }

  // Skip the UID check for sessions marked with a bugreport_score > 0.
  // Those sessions, by design, can be stolen by any other consumer for the
  // sake of creating snapshots for bugreports.
  if (!session->IsCloneAllowed(consumer->uid_)) {
    return PERFETTO_SVC_ERR("Not allowed to clone a session from another UID");
  }

  // If any of the buffers are marked as clear_before_clone, reset them before
  // issuing the Flush(kCloneReason).
  size_t buf_idx = 0;
  for (BufferID src_buf_id : session->buffers_index) {
    if (!session->config.buffers()[buf_idx++].clear_before_clone())
      continue;
    auto buf_iter = buffers_.find(src_buf_id);
    PERFETTO_CHECK(buf_iter != buffers_.end());
    std::unique_ptr<TraceBuffer>& buf = buf_iter->second;

    // No need to reset the buffer if nothing has been written into it yet.
    // This is the canonical case if producers behive nicely and don't timeout
    // the handling of writes during the flush.
    // This check avoids a useless re-mmap upon every Clone() if the buffer is
    // already empty (when used in combination with `transfer_on_clone`).
    if (!buf->has_data())
      continue;

    // Some leftover data was left in the buffer. Recreate it to empty it.
    const auto buf_policy = buf->overwrite_policy();
    const auto buf_size = buf->size();
    std::unique_ptr<TraceBuffer> old_buf = std::move(buf);
    buf = TraceBuffer::Create(buf_size, buf_policy);
    if (!buf) {
      // This is extremely rare but could happen on 32-bit. If the new buffer
      // allocation failed, put back the buffer where it was and fail the clone.
      // We cannot leave the original tracing session buffer-less as it would
      // cause crashes when data sources commit new data.
      buf = std::move(old_buf);
      return base::ErrStatus(
          "Buffer allocation failed while attempting to clone");
    }
  }

  auto weak_consumer = consumer->GetWeakPtr();

  const PendingCloneID clone_id = session->last_pending_clone_id_++;

  auto& clone_op = session->pending_clones[clone_id];
  clone_op.pending_flush_cnt = 0;
  // Pre-initialize these vectors just as an optimization to avoid reallocations
  // in DoCloneBuffers().
  clone_op.buffers.reserve(session->buffers_index.size());
  clone_op.buffer_cloned_timestamps.reserve(session->buffers_index.size());
  clone_op.weak_consumer = weak_consumer;
  clone_op.skip_trace_filter = args.skip_trace_filter;
  if (!args.clone_trigger_name.empty()) {
    clone_op.clone_trigger = {
        args.clone_trigger_boot_time_ns, args.clone_trigger_name,
        args.clone_trigger_producer_name,
        args.clone_trigger_trusted_producer_uid, args.clone_trigger_delay_ms};
  }

  // Issue separate flush requests for separate buffer groups. The buffer marked
  // as transfer_on_clone will be flushed and cloned separately: even if they're
  // slower (like in the case of Winscope tracing), they will not delay the
  // snapshot of the other buffers.
  //
  // In the future we might want to split the buffer into more groups and maybe
  // allow this to be configurable.
  std::array<std::set<BufferID>, 2> bufs_groups;
  for (size_t i = 0; i < session->buffers_index.size(); i++) {
    if (session->config.buffers()[i].transfer_on_clone()) {
      bufs_groups[0].insert(session->buffers_index[i]);
    } else {
      bufs_groups[1].insert(session->buffers_index[i]);
    }
  }

  SnapshotLifecycleEvent(
      session, protos::pbzero::TracingServiceEvent::kFlushStartedFieldNumber,
      /*snapshot_clocks=*/true);
  clone_op.pending_flush_cnt = bufs_groups.size();
  clone_op.clone_started_timestamp_ns = clock_->GetBootTimeNs().count();
  for (const std::set<BufferID>& buf_group : bufs_groups) {
    FlushDataSourceInstances(
        session, 0,
        GetFlushableDataSourceInstancesForBuffers(session, buf_group),
        [tsid = session->id, clone_id, buf_group, this](bool final_flush) {
          OnFlushDoneForClone(tsid, clone_id, buf_group, final_flush);
        },
        FlushFlags(FlushFlags::Initiator::kTraced,
                   FlushFlags::Reason::kTraceClone, clone_target));
  }

  return base::OkStatus();
}

std::map<ProducerID, std::vector<DataSourceInstanceID>>
TracingServiceImpl::GetFlushableDataSourceInstancesForBuffers(
    TracingSession* session,
    const std::set<BufferID>& bufs) {
  std::map<ProducerID, std::vector<DataSourceInstanceID>> data_source_instances;

  for (const auto& [producer_id, ds_inst] : session->data_source_instances) {
    // TODO(ddiproietto): Consider if we should skip instances if ds_inst.state
    // != DataSourceInstance::STARTED
    if (ds_inst.no_flush) {
      continue;
    }
    if (!bufs.count(static_cast<BufferID>(ds_inst.config.target_buffer()))) {
      continue;
    }
    data_source_instances[producer_id].push_back(ds_inst.instance_id);
  }

  return data_source_instances;
}

void TracingServiceImpl::OnFlushDoneForClone(TracingSessionID tsid,
                                             PendingCloneID clone_id,
                                             const std::set<BufferID>& buf_ids,
                                             bool final_flush_outcome) {
  TracingSession* src = GetTracingSession(tsid);
  // The session might be gone by the time we try to clone it.
  if (!src) {
    return;
  }

  auto it = src->pending_clones.find(clone_id);
  if (it == src->pending_clones.end()) {
    return;
  }
  auto& clone_op = it->second;

  if (final_flush_outcome == false) {
    clone_op.flush_failed = true;
  }

  base::Status result;
  base::Uuid uuid;

  // First clone the flushed TraceBuffer(s). This can fail because of ENOMEM. If
  // it happens bail out early before creating any session.
  if (!DoCloneBuffers(*src, buf_ids, &clone_op)) {
    result = PERFETTO_SVC_ERR("Buffer allocation failed");
  }

  if (result.ok()) {
    UpdateMemoryGuardrail();

    if (--clone_op.pending_flush_cnt != 0) {
      // Wait for more pending flushes.
      return;
    }

    PERFETTO_LOG("FlushAndCloneSession(%" PRIu64 ") started, success=%d", tsid,
                 final_flush_outcome);

    if (clone_op.weak_consumer) {
      result = FinishCloneSession(
          &*clone_op.weak_consumer, tsid, std::move(clone_op.buffers),
          std::move(clone_op.buffer_cloned_timestamps),
          clone_op.skip_trace_filter, !clone_op.flush_failed,
          clone_op.clone_trigger, &uuid, clone_op.clone_started_timestamp_ns);
    }
  }  // if (result.ok())

  if (clone_op.weak_consumer) {
    clone_op.weak_consumer->consumer_->OnSessionCloned(
        {result.ok(), result.message(), uuid});
  }

  src->pending_clones.erase(it);
  UpdateMemoryGuardrail();
}

bool TracingServiceImpl::DoCloneBuffers(const TracingSession& src,
                                        const std::set<BufferID>& buf_ids,
                                        PendingClone* clone_op) {
  PERFETTO_DCHECK(src.num_buffers() == src.config.buffers().size());
  clone_op->buffers.resize(src.buffers_index.size());
  clone_op->buffer_cloned_timestamps.resize(src.buffers_index.size());

  int64_t now = clock_->GetBootTimeNs().count();

  for (size_t buf_idx = 0; buf_idx < src.buffers_index.size(); buf_idx++) {
    BufferID src_buf_id = src.buffers_index[buf_idx];
    if (buf_ids.count(src_buf_id) == 0)
      continue;
    auto buf_iter = buffers_.find(src_buf_id);
    PERFETTO_CHECK(buf_iter != buffers_.end());
    std::unique_ptr<TraceBuffer>& src_buf = buf_iter->second;
    std::unique_ptr<TraceBuffer> new_buf;
    if (src.config.buffers()[buf_idx].transfer_on_clone()) {
      const auto buf_policy = src_buf->overwrite_policy();
      const auto buf_size = src_buf->size();
      new_buf = std::move(src_buf);
      src_buf = TraceBuffer::Create(buf_size, buf_policy);
      if (!src_buf) {
        // If the allocation fails put the buffer back and let the code below
        // handle the failure gracefully.
        src_buf = std::move(new_buf);
      }
    } else {
      new_buf = src_buf->CloneReadOnly();
    }
    if (!new_buf.get()) {
      return false;
    }
    clone_op->buffers[buf_idx] = std::move(new_buf);
    clone_op->buffer_cloned_timestamps[buf_idx] = now;
  }
  return true;
}

base::Status TracingServiceImpl::FinishCloneSession(
    ConsumerEndpointImpl* consumer,
    TracingSessionID src_tsid,
    std::vector<std::unique_ptr<TraceBuffer>> buf_snaps,
    std::vector<int64_t> buf_cloned_timestamps,
    bool skip_trace_filter,
    bool final_flush_outcome,
    std::optional<TriggerInfo> clone_trigger,
    base::Uuid* new_uuid,
    int64_t clone_started_timestamp_ns) {
  PERFETTO_DLOG("CloneSession(%" PRIu64
                ", skip_trace_filter=%d) started, consumer uid: %d",
                src_tsid, skip_trace_filter, static_cast<int>(consumer->uid_));

  TracingSession* src = GetTracingSession(src_tsid);

  // The session might be gone by the time we try to clone it.
  if (!src)
    return PERFETTO_SVC_ERR("session not found");

  if (consumer->tracing_session_id_) {
    return PERFETTO_SVC_ERR(
        "The consumer is already attached to another tracing session");
  }

  std::vector<BufferID> buf_ids =
      buffer_ids_.AllocateMultiple(buf_snaps.size());
  if (buf_ids.size() != buf_snaps.size()) {
    return PERFETTO_SVC_ERR("Buffer id allocation failed");
  }

  PERFETTO_CHECK(std::none_of(
      buf_snaps.begin(), buf_snaps.end(),
      [](const std::unique_ptr<TraceBuffer>& buf) { return buf == nullptr; }));

  const TracingSessionID tsid = ++last_tracing_session_id_;
  TracingSession* cloned_session =
      &tracing_sessions_
           .emplace(std::piecewise_construct, std::forward_as_tuple(tsid),
                    std::forward_as_tuple(tsid, consumer, src->config,
                                          weak_runner_.task_runner()))
           .first->second;

  // Generate a new UUID for the cloned session, but preserve the LSB. In some
  // contexts the LSB is used to tie the trace back to the statsd subscription
  // that triggered it. See the corresponding code in perfetto_cmd.cc which
  // reads at triggering_subscription_id().
  const int64_t orig_uuid_lsb = src->trace_uuid.lsb();
  cloned_session->state = TracingSession::CLONED_READ_ONLY;
  cloned_session->trace_uuid = base::Uuidv4();
  cloned_session->trace_uuid.set_lsb(orig_uuid_lsb);
  *new_uuid = cloned_session->trace_uuid;

  for (size_t i = 0; i < buf_snaps.size(); i++) {
    BufferID buf_global_id = buf_ids[i];
    std::unique_ptr<TraceBuffer>& buf = buf_snaps[i];
    // This is only needed for transfer_on_clone. Other buffers are already
    // marked as read-only by CloneReadOnly(). We cannot do this early because
    // in case of an allocation failure we will put std::move() the original
    // buffer back in its place and in that case should not be made read-only.
    buf->set_read_only();
    buffers_.emplace(buf_global_id, std::move(buf));
    cloned_session->buffers_index.emplace_back(buf_global_id);
  }
  UpdateMemoryGuardrail();

  // Copy over relevant state that we want to persist in the cloned session.
  // Mostly stats and metadata that is emitted in the trace file by the service.
  // Also clear the received trigger list in the main tracing session. A
  // CLONE_SNAPSHOT session can go in ring buffer mode for several hours and get
  // snapshotted several times. This causes two issues with `received_triggers`:
  // 1. Adding noise in the cloned trace emitting triggers that happened too
  //    far back (see b/290799105).
  // 2. Bloating memory (see b/290798988).
  cloned_session->should_emit_stats = true;
  cloned_session->clone_trigger = clone_trigger;
  cloned_session->received_triggers = std::move(src->received_triggers);
  src->received_triggers.clear();
  src->num_triggers_emitted_into_trace = 0;
  cloned_session->lifecycle_events =
      std::vector<TracingSession::LifecycleEvent>(src->lifecycle_events);
  cloned_session->slow_start_event = src->slow_start_event;
  cloned_session->last_flush_events = src->last_flush_events;
  cloned_session->initial_clock_snapshot = src->initial_clock_snapshot;
  cloned_session->clock_snapshot_ring_buffer = src->clock_snapshot_ring_buffer;
  cloned_session->invalid_packets = src->invalid_packets;
  cloned_session->flushes_requested = src->flushes_requested;
  cloned_session->flushes_succeeded = src->flushes_succeeded;
  cloned_session->flushes_failed = src->flushes_failed;
  cloned_session->compress_deflate = src->compress_deflate;
  if (src->trace_filter && !skip_trace_filter) {
    // Copy the trace filter, unless it's a clone-for-bugreport (b/317065412).
    cloned_session->trace_filter.reset(
        new protozero::MessageFilter(src->trace_filter->config()));
  }

  cloned_session->buffer_cloned_timestamps = std::move(buf_cloned_timestamps);

  SetSingleLifecycleEvent(
      cloned_session,
      protos::pbzero::TracingServiceEvent::kCloneStartedFieldNumber,
      clone_started_timestamp_ns);

  SnapshotLifecycleEvent(
      cloned_session,
      protos::pbzero::TracingServiceEvent::kTracingDisabledFieldNumber,
      true /* snapshot_clocks */);

  PERFETTO_DLOG("Consumer (uid:%d) cloned tracing session %" PRIu64
                " -> %" PRIu64,
                static_cast<int>(consumer->uid_), src_tsid, tsid);

  consumer->tracing_session_id_ = tsid;
  cloned_session->final_flush_outcome = final_flush_outcome
                                            ? TraceStats::FINAL_FLUSH_SUCCEEDED
                                            : TraceStats::FINAL_FLUSH_FAILED;
  return base::OkStatus();
}

bool TracingServiceImpl::TracingSession::IsCloneAllowed(uid_t clone_uid) const {
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

////////////////////////////////////////////////////////////////////////////////
// TracingServiceImpl::ConsumerEndpointImpl implementation
////////////////////////////////////////////////////////////////////////////////

TracingServiceImpl::ConsumerEndpointImpl::ConsumerEndpointImpl(
    TracingServiceImpl* service,
    base::TaskRunner* task_runner,
    Consumer* consumer,
    uid_t uid)
    : task_runner_(task_runner),
      service_(service),
      consumer_(consumer),
      uid_(uid),
      weak_ptr_factory_(this) {}

TracingServiceImpl::ConsumerEndpointImpl::~ConsumerEndpointImpl() {
  service_->DisconnectConsumer(this);
  consumer_->OnDisconnect();
}

void TracingServiceImpl::ConsumerEndpointImpl::NotifyOnTracingDisabled(
    const std::string& error) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  task_runner_->PostTask([weak_this = weak_ptr_factory_.GetWeakPtr(),
                          error /* deliberate copy */] {
    if (weak_this)
      weak_this->consumer_->OnTracingDisabled(error);
  });
}

void TracingServiceImpl::ConsumerEndpointImpl::EnableTracing(
    const TraceConfig& cfg,
    base::ScopedFile fd) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  auto status = service_->EnableTracing(this, cfg, std::move(fd));
  if (!status.ok())
    NotifyOnTracingDisabled(status.message());
}

void TracingServiceImpl::ConsumerEndpointImpl::ChangeTraceConfig(
    const TraceConfig& cfg) {
  if (!tracing_session_id_) {
    PERFETTO_LOG(
        "Consumer called ChangeTraceConfig() but tracing was "
        "not active");
    return;
  }
  service_->ChangeTraceConfig(this, cfg);
}

void TracingServiceImpl::ConsumerEndpointImpl::StartTracing() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!tracing_session_id_) {
    PERFETTO_LOG("Consumer called StartTracing() but tracing was not active");
    return;
  }
  service_->StartTracing(tracing_session_id_);
}

void TracingServiceImpl::ConsumerEndpointImpl::DisableTracing() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!tracing_session_id_) {
    PERFETTO_LOG("Consumer called DisableTracing() but tracing was not active");
    return;
  }
  service_->DisableTracing(tracing_session_id_);
}

void TracingServiceImpl::ConsumerEndpointImpl::ReadBuffers() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!tracing_session_id_) {
    PERFETTO_LOG("Consumer called ReadBuffers() but tracing was not active");
    consumer_->OnTraceData({}, /* has_more = */ false);
    return;
  }
  if (!service_->ReadBuffersIntoConsumer(tracing_session_id_, this)) {
    consumer_->OnTraceData({}, /* has_more = */ false);
  }
}

void TracingServiceImpl::ConsumerEndpointImpl::FreeBuffers() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!tracing_session_id_) {
    PERFETTO_LOG("Consumer called FreeBuffers() but tracing was not active");
    return;
  }
  service_->FreeBuffers(tracing_session_id_);
  tracing_session_id_ = 0;
}

void TracingServiceImpl::ConsumerEndpointImpl::Flush(uint32_t timeout_ms,
                                                     FlushCallback callback,
                                                     FlushFlags flush_flags) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!tracing_session_id_) {
    PERFETTO_LOG("Consumer called Flush() but tracing was not active");
    return;
  }
  service_->Flush(tracing_session_id_, timeout_ms, callback, flush_flags);
}

void TracingServiceImpl::ConsumerEndpointImpl::Detach(const std::string& key) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  bool success = service_->DetachConsumer(this, key);
  auto weak_this = weak_ptr_factory_.GetWeakPtr();
  task_runner_->PostTask([weak_this = std::move(weak_this), success] {
    if (weak_this)
      weak_this->consumer_->OnDetach(success);
  });
}

void TracingServiceImpl::ConsumerEndpointImpl::Attach(const std::string& key) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  bool success = service_->AttachConsumer(this, key);
  task_runner_->PostTask([weak_this = weak_ptr_factory_.GetWeakPtr(), success] {
    if (!weak_this)
      return;
    Consumer* consumer = weak_this->consumer_;
    TracingSession* session =
        weak_this->service_->GetTracingSession(weak_this->tracing_session_id_);
    if (!session) {
      consumer->OnAttach(false, TraceConfig());
      return;
    }
    consumer->OnAttach(success, session->config);
  });
}

void TracingServiceImpl::ConsumerEndpointImpl::GetTraceStats() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  bool success = false;
  TraceStats stats;
  TracingSession* session = service_->GetTracingSession(tracing_session_id_);
  if (session) {
    success = true;
    stats = service_->GetTraceStats(session);
  }
  auto weak_this = weak_ptr_factory_.GetWeakPtr();
  task_runner_->PostTask(
      [weak_this = std::move(weak_this), success, stats = std::move(stats)] {
        if (weak_this)
          weak_this->consumer_->OnTraceStats(success, stats);
      });
}

void TracingServiceImpl::ConsumerEndpointImpl::ObserveEvents(
    uint32_t events_mask) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  observable_events_mask_ = events_mask;
  TracingSession* session = service_->GetTracingSession(tracing_session_id_);
  if (!session)
    return;

  if (observable_events_mask_ & ObservableEvents::TYPE_DATA_SOURCES_INSTANCES) {
    // Issue initial states.
    for (const auto& kv : session->data_source_instances) {
      ProducerEndpointImpl* producer = service_->GetProducer(kv.first);
      PERFETTO_DCHECK(producer);
      OnDataSourceInstanceStateChange(*producer, kv.second);
    }
  }

  // If the ObserveEvents() call happens after data sources have acked already
  // notify immediately.
  if (observable_events_mask_ &
      ObservableEvents::TYPE_ALL_DATA_SOURCES_STARTED) {
    service_->MaybeNotifyAllDataSourcesStarted(session);
  }
}

void TracingServiceImpl::ConsumerEndpointImpl::OnDataSourceInstanceStateChange(
    const ProducerEndpointImpl& producer,
    const DataSourceInstance& instance) {
  if (!(observable_events_mask_ &
        ObservableEvents::TYPE_DATA_SOURCES_INSTANCES)) {
    return;
  }

  if (instance.state != DataSourceInstance::CONFIGURED &&
      instance.state != DataSourceInstance::STARTED &&
      instance.state != DataSourceInstance::STOPPED) {
    return;
  }

  auto* observable_events = AddObservableEvents();
  auto* change = observable_events->add_instance_state_changes();
  change->set_producer_name(producer.name_);
  change->set_data_source_name(instance.data_source_name);
  if (instance.state == DataSourceInstance::STARTED) {
    change->set_state(ObservableEvents::DATA_SOURCE_INSTANCE_STATE_STARTED);
  } else {
    change->set_state(ObservableEvents::DATA_SOURCE_INSTANCE_STATE_STOPPED);
  }
}

void TracingServiceImpl::ConsumerEndpointImpl::OnAllDataSourcesStarted() {
  if (!(observable_events_mask_ &
        ObservableEvents::TYPE_ALL_DATA_SOURCES_STARTED)) {
    return;
  }
  auto* observable_events = AddObservableEvents();
  observable_events->set_all_data_sources_started(true);
}

void TracingServiceImpl::ConsumerEndpointImpl::NotifyCloneSnapshotTrigger(
    const TriggerInfo& trigger) {
  if (!(observable_events_mask_ & ObservableEvents::TYPE_CLONE_TRIGGER_HIT)) {
    return;
  }
  auto* observable_events = AddObservableEvents();
  auto* clone_trig = observable_events->mutable_clone_trigger_hit();
  clone_trig->set_tracing_session_id(static_cast<int64_t>(tracing_session_id_));
  clone_trig->set_trigger_name(trigger.trigger_name);
  clone_trig->set_producer_name(trigger.producer_name);
  clone_trig->set_producer_uid(static_cast<uint32_t>(trigger.producer_uid));
  clone_trig->set_boot_time_ns(trigger.boot_time_ns);
  clone_trig->set_trigger_delay_ms(trigger.trigger_delay_ms);
}

ObservableEvents*
TracingServiceImpl::ConsumerEndpointImpl::AddObservableEvents() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  if (!observable_events_) {
    observable_events_.reset(new ObservableEvents());
    task_runner_->PostTask([weak_this = weak_ptr_factory_.GetWeakPtr()] {
      if (!weak_this)
        return;

      // Move into a temporary to allow reentrancy in OnObservableEvents.
      auto observable_events = std::move(weak_this->observable_events_);
      weak_this->consumer_->OnObservableEvents(*observable_events);
    });
  }
  return observable_events_.get();
}

void TracingServiceImpl::ConsumerEndpointImpl::QueryServiceState(
    QueryServiceStateArgs args,
    QueryServiceStateCallback callback) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  TracingServiceState svc_state;

  const auto& sessions = service_->tracing_sessions_;
  svc_state.set_tracing_service_version(base::GetVersionString());
  svc_state.set_num_sessions(static_cast<int>(sessions.size()));

  int num_started = 0;
  for (const auto& kv : sessions)
    num_started += kv.second.state == TracingSession::State::STARTED ? 1 : 0;
  svc_state.set_num_sessions_started(num_started);

  for (const auto& kv : service_->producers_) {
    if (args.sessions_only)
      break;
    auto* producer = svc_state.add_producers();
    producer->set_id(static_cast<int>(kv.first));
    producer->set_name(kv.second->name_);
    producer->set_sdk_version(kv.second->sdk_version_);
    producer->set_uid(static_cast<int32_t>(kv.second->uid()));
    producer->set_pid(static_cast<int32_t>(kv.second->pid()));
    producer->set_frozen(kv.second->IsAndroidProcessFrozen());
  }

  for (const auto& kv : service_->data_sources_) {
    if (args.sessions_only)
      break;
    const auto& registered_data_source = kv.second;
    auto* data_source = svc_state.add_data_sources();
    *data_source->mutable_ds_descriptor() = registered_data_source.descriptor;
    data_source->set_producer_id(
        static_cast<int>(registered_data_source.producer_id));
  }

  svc_state.set_supports_tracing_sessions(true);
  for (const auto& kv : service_->tracing_sessions_) {
    const TracingSession& s = kv.second;
    if (!s.IsCloneAllowed(uid_))
      continue;
    auto* session = svc_state.add_tracing_sessions();
    session->set_id(s.id);
    session->set_consumer_uid(static_cast<int>(s.consumer_uid));
    session->set_duration_ms(s.config.duration_ms());
    session->set_num_data_sources(
        static_cast<uint32_t>(s.data_source_instances.size()));
    session->set_unique_session_name(s.config.unique_session_name());
    if (s.config.has_bugreport_score())
      session->set_bugreport_score(s.config.bugreport_score());
    if (s.config.has_bugreport_filename())
      session->set_bugreport_filename(s.config.bugreport_filename());
    for (const auto& snap_kv : s.initial_clock_snapshot) {
      if (snap_kv.clock_id == protos::pbzero::BUILTIN_CLOCK_REALTIME)
        session->set_start_realtime_ns(static_cast<int64_t>(snap_kv.timestamp));
    }
    for (const auto& buf : s.config.buffers())
      session->add_buffer_size_kb(buf.size_kb());

    switch (s.state) {
      case TracingSession::State::DISABLED:
        session->set_state("DISABLED");
        break;
      case TracingSession::State::CONFIGURED:
        session->set_state("CONFIGURED");
        break;
      case TracingSession::State::STARTED:
        session->set_is_started(true);
        session->set_state("STARTED");
        break;
      case TracingSession::State::DISABLING_WAITING_STOP_ACKS:
        session->set_state("STOP_WAIT");
        break;
      case TracingSession::State::CLONED_READ_ONLY:
        session->set_state("CLONED_READ_ONLY");
        break;
    }
  }
  callback(/*success=*/true, svc_state);
}

void TracingServiceImpl::ConsumerEndpointImpl::QueryCapabilities(
    QueryCapabilitiesCallback callback) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  TracingServiceCapabilities caps;
  caps.set_has_query_capabilities(true);
  caps.set_has_trace_config_output_path(true);
  caps.set_has_clone_session(true);
  caps.add_observable_events(ObservableEvents::TYPE_DATA_SOURCES_INSTANCES);
  caps.add_observable_events(ObservableEvents::TYPE_ALL_DATA_SOURCES_STARTED);
  caps.add_observable_events(ObservableEvents::TYPE_CLONE_TRIGGER_HIT);
  static_assert(
      ObservableEvents::Type_MAX == ObservableEvents::TYPE_CLONE_TRIGGER_HIT,
      "");
  callback(caps);
}

void TracingServiceImpl::ConsumerEndpointImpl::SaveTraceForBugreport(
    SaveTraceForBugreportCallback consumer_callback) {
  consumer_callback(false,
                    "SaveTraceForBugreport is deprecated. Use "
                    "CloneSession(kBugreportSessionId) instead.");
}

void TracingServiceImpl::ConsumerEndpointImpl::CloneSession(
    CloneSessionArgs args) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  // FlushAndCloneSession will call OnSessionCloned after the async flush.
  base::Status result = service_->FlushAndCloneSession(this, std::move(args));

  if (!result.ok()) {
    consumer_->OnSessionCloned({false, result.message(), {}});
  }
}

////////////////////////////////////////////////////////////////////////////////
// TracingServiceImpl::ProducerEndpointImpl implementation
////////////////////////////////////////////////////////////////////////////////

TracingServiceImpl::ProducerEndpointImpl::ProducerEndpointImpl(
    ProducerID id,
    const ClientIdentity& client_identity,
    TracingServiceImpl* service,
    base::TaskRunner* task_runner,
    Producer* producer,
    const std::string& producer_name,
    const std::string& sdk_version,
    bool in_process,
    bool smb_scraping_enabled)
    : id_(id),
      client_identity_(client_identity),
      service_(service),
      producer_(producer),
      name_(producer_name),
      sdk_version_(sdk_version),
      in_process_(in_process),
      smb_scraping_enabled_(smb_scraping_enabled),
      weak_runner_(task_runner) {}

TracingServiceImpl::ProducerEndpointImpl::~ProducerEndpointImpl() {
  service_->DisconnectProducer(id_);
  producer_->OnDisconnect();
}

void TracingServiceImpl::ProducerEndpointImpl::Disconnect() {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  // Disconnection is only supported via destroying the ProducerEndpoint.
  PERFETTO_FATAL("Not supported");
}

void TracingServiceImpl::ProducerEndpointImpl::RegisterDataSource(
    const DataSourceDescriptor& desc) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  service_->RegisterDataSource(id_, desc);
}

void TracingServiceImpl::ProducerEndpointImpl::UpdateDataSource(
    const DataSourceDescriptor& desc) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  service_->UpdateDataSource(id_, desc);
}

void TracingServiceImpl::ProducerEndpointImpl::UnregisterDataSource(
    const std::string& name) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  service_->UnregisterDataSource(id_, name);
}

void TracingServiceImpl::ProducerEndpointImpl::RegisterTraceWriter(
    uint32_t writer_id,
    uint32_t target_buffer) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  writers_[static_cast<WriterID>(writer_id)] =
      static_cast<BufferID>(target_buffer);
}

void TracingServiceImpl::ProducerEndpointImpl::UnregisterTraceWriter(
    uint32_t writer_id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  writers_.erase(static_cast<WriterID>(writer_id));
}

void TracingServiceImpl::ProducerEndpointImpl::CommitData(
    const CommitDataRequest& req_untrusted,
    CommitDataCallback callback) {
  PERFETTO_DCHECK_THREAD(thread_checker_);

  if (metatrace::IsEnabled(metatrace::TAG_TRACE_SERVICE)) {
    PERFETTO_METATRACE_COUNTER(TAG_TRACE_SERVICE, TRACE_SERVICE_COMMIT_DATA,
                               EncodeCommitDataRequest(id_, req_untrusted));
  }

  if (!shared_memory_) {
    PERFETTO_DLOG(
        "Attempted to commit data before the shared memory was allocated.");
    return;
  }
  PERFETTO_DCHECK(shmem_abi_.is_valid());
  for (const auto& entry : req_untrusted.chunks_to_move()) {
    const uint32_t page_idx = entry.page();
    if (page_idx >= shmem_abi_.num_pages())
      continue;  // A buggy or malicious producer.

    SharedMemoryABI::Chunk chunk;
    bool commit_data_over_ipc = entry.has_data();
    if (PERFETTO_UNLIKELY(commit_data_over_ipc)) {
      // Chunk data is passed over the wire. Create a chunk using the serialized
      // protobuf message.
      const std::string& data = entry.data();
      if (data.size() > SharedMemoryABI::Chunk::kMaxSize) {
        PERFETTO_DFATAL("IPC data commit too large: %zu", data.size());
        continue;  // A malicious or buggy producer
      }
      // |data| is not altered, but we need to const_cast becasue Chunk data
      // members are non-const.
      chunk = SharedMemoryABI::MakeChunkFromSerializedData(
          reinterpret_cast<uint8_t*>(const_cast<char*>(data.data())),
          static_cast<uint16_t>(entry.data().size()),
          static_cast<uint8_t>(entry.chunk()));
    } else
      chunk = shmem_abi_.TryAcquireChunkForReading(page_idx, entry.chunk());
    if (!chunk.is_valid()) {
      PERFETTO_DLOG("Asked to move chunk %d:%d, but it's not complete",
                    entry.page(), entry.chunk());
      continue;
    }

    // TryAcquireChunkForReading() has load-acquire semantics. Once acquired,
    // the ABI contract expects the producer to not touch the chunk anymore
    // (until the service marks that as free). This is why all the reads below
    // are just memory_order_relaxed. Also, the code here assumes that all this
    // data can be malicious and just gives up if anything is malformed.
    BufferID buffer_id = static_cast<BufferID>(entry.target_buffer());
    const SharedMemoryABI::ChunkHeader& chunk_header = *chunk.header();
    WriterID writer_id = chunk_header.writer_id.load(std::memory_order_relaxed);
    ChunkID chunk_id = chunk_header.chunk_id.load(std::memory_order_relaxed);
    auto packets = chunk_header.packets.load(std::memory_order_relaxed);
    uint16_t num_fragments = packets.count;
    uint8_t chunk_flags = packets.flags;

    service_->CopyProducerPageIntoLogBuffer(
        id_, client_identity_, writer_id, chunk_id, buffer_id, num_fragments,
        chunk_flags,
        /*chunk_complete=*/true, chunk.payload_begin(), chunk.payload_size());

    if (!commit_data_over_ipc) {
      // This one has release-store semantics.
      shmem_abi_.ReleaseChunkAsFree(std::move(chunk));
    }
  }  // for(chunks_to_move)

  service_->ApplyChunkPatches(id_, req_untrusted.chunks_to_patch());

  if (req_untrusted.flush_request_id()) {
    service_->NotifyFlushDoneForProducer(id_, req_untrusted.flush_request_id());
  }

  // Keep this invocation last. ProducerIPCService::CommitData() relies on this
  // callback being invoked within the same callstack and not posted. If this
  // changes, the code there needs to be changed accordingly.
  if (callback)
    callback();
}

void TracingServiceImpl::ProducerEndpointImpl::SetupSharedMemory(
    std::unique_ptr<SharedMemory> shared_memory,
    size_t page_size_bytes,
    bool provided_by_producer) {
  PERFETTO_DCHECK(!shared_memory_ && !shmem_abi_.is_valid());
  PERFETTO_DCHECK(page_size_bytes % 1024 == 0);

  shared_memory_ = std::move(shared_memory);
  shared_buffer_page_size_kb_ = page_size_bytes / 1024;
  is_shmem_provided_by_producer_ = provided_by_producer;

  shmem_abi_.Initialize(reinterpret_cast<uint8_t*>(shared_memory_->start()),
                        shared_memory_->size(),
                        shared_buffer_page_size_kb() * 1024,
                        SharedMemoryABI::ShmemMode::kDefault);
  if (in_process_) {
    inproc_shmem_arbiter_.reset(new SharedMemoryArbiterImpl(
        shared_memory_->start(), shared_memory_->size(),
        SharedMemoryABI::ShmemMode::kDefault,
        shared_buffer_page_size_kb_ * 1024, this, weak_runner_.task_runner()));
    inproc_shmem_arbiter_->SetDirectSMBPatchingSupportedByService();
  }

  OnTracingSetup();
  service_->UpdateMemoryGuardrail();
}

SharedMemory* TracingServiceImpl::ProducerEndpointImpl::shared_memory() const {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  return shared_memory_.get();
}

size_t TracingServiceImpl::ProducerEndpointImpl::shared_buffer_page_size_kb()
    const {
  return shared_buffer_page_size_kb_;
}

void TracingServiceImpl::ProducerEndpointImpl::ActivateTriggers(
    const std::vector<std::string>& triggers) {
  service_->ActivateTriggers(id_, triggers);
}

void TracingServiceImpl::ProducerEndpointImpl::StopDataSource(
    DataSourceInstanceID ds_inst_id) {
  // TODO(primiano): When we'll support tearing down the SMB, at this point we
  // should send the Producer a TearDownTracing if all its data sources have
  // been disabled (see b/77532839 and aosp/655179 PS1).
  PERFETTO_DCHECK_THREAD(thread_checker_);
  weak_runner_.PostTask(
      [this, ds_inst_id] { producer_->StopDataSource(ds_inst_id); });
}

SharedMemoryArbiter*
TracingServiceImpl::ProducerEndpointImpl::MaybeSharedMemoryArbiter() {
  if (!inproc_shmem_arbiter_) {
    PERFETTO_FATAL(
        "The in-process SharedMemoryArbiter can only be used when "
        "CreateProducer has been called with in_process=true and after tracing "
        "has started.");
  }

  PERFETTO_DCHECK(in_process_);
  return inproc_shmem_arbiter_.get();
}

bool TracingServiceImpl::ProducerEndpointImpl::IsShmemProvidedByProducer()
    const {
  return is_shmem_provided_by_producer_;
}

// Can be called on any thread.
std::unique_ptr<TraceWriter>
TracingServiceImpl::ProducerEndpointImpl::CreateTraceWriter(
    BufferID buf_id,
    BufferExhaustedPolicy buffer_exhausted_policy) {
  PERFETTO_DCHECK(MaybeSharedMemoryArbiter());
  return MaybeSharedMemoryArbiter()->CreateTraceWriter(buf_id,
                                                       buffer_exhausted_policy);
}

void TracingServiceImpl::ProducerEndpointImpl::NotifyFlushComplete(
    FlushRequestID id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  PERFETTO_DCHECK(MaybeSharedMemoryArbiter());
  return MaybeSharedMemoryArbiter()->NotifyFlushComplete(id);
}

void TracingServiceImpl::ProducerEndpointImpl::OnTracingSetup() {
  weak_runner_.PostTask([this] { producer_->OnTracingSetup(); });
}

void TracingServiceImpl::ProducerEndpointImpl::Flush(
    FlushRequestID flush_request_id,
    const std::vector<DataSourceInstanceID>& data_sources,
    FlushFlags flush_flags) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  weak_runner_.PostTask([this, flush_request_id, data_sources, flush_flags] {
    producer_->Flush(flush_request_id, data_sources.data(), data_sources.size(),
                     flush_flags);
  });
}

void TracingServiceImpl::ProducerEndpointImpl::SetupDataSource(
    DataSourceInstanceID ds_id,
    const DataSourceConfig& config) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  allowed_target_buffers_.insert(static_cast<BufferID>(config.target_buffer()));
  weak_runner_.PostTask([this, ds_id, config] {
    producer_->SetupDataSource(ds_id, std::move(config));
  });
}

void TracingServiceImpl::ProducerEndpointImpl::StartDataSource(
    DataSourceInstanceID ds_id,
    const DataSourceConfig& config) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  weak_runner_.PostTask([this, ds_id, config] {
    producer_->StartDataSource(ds_id, std::move(config));
  });
}

void TracingServiceImpl::ProducerEndpointImpl::NotifyDataSourceStarted(
    DataSourceInstanceID data_source_id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  service_->NotifyDataSourceStarted(id_, data_source_id);
}

void TracingServiceImpl::ProducerEndpointImpl::NotifyDataSourceStopped(
    DataSourceInstanceID data_source_id) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  service_->NotifyDataSourceStopped(id_, data_source_id);
}

void TracingServiceImpl::ProducerEndpointImpl::OnFreeBuffers(
    const std::vector<BufferID>& target_buffers) {
  if (allowed_target_buffers_.empty())
    return;
  for (BufferID buffer : target_buffers)
    allowed_target_buffers_.erase(buffer);
}

void TracingServiceImpl::ProducerEndpointImpl::ClearIncrementalState(
    const std::vector<DataSourceInstanceID>& data_sources) {
  PERFETTO_DCHECK_THREAD(thread_checker_);
  weak_runner_.PostTask([this, data_sources] {
    base::StringView producer_name(name_);
    producer_->ClearIncrementalState(data_sources.data(), data_sources.size());
  });
}

void TracingServiceImpl::ProducerEndpointImpl::Sync(
    std::function<void()> callback) {
  weak_runner_.task_runner()->PostTask(callback);
}

bool TracingServiceImpl::ProducerEndpointImpl::IsAndroidProcessFrozen() {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  if (in_process_ || uid() == base::kInvalidUid || pid() == base::kInvalidPid)
    return false;

  // As per aosp/3406861, there are three possible mount points for the cgroup.
  // Look at all of them.
  // - Historically everything was in /uid_xxx/pid_yyy (and still is if
  //   PRODUCT_CGROUP_V2_SYS_APP_ISOLATION_ENABLED = false)
  // - cgroup isolation introduces /apps /system subdirectories.
  base::StackString<255> path_v1(
      "/sys/fs/cgroup/uid_%" PRIu32 "/pid_%" PRIu32 "/cgroup.freeze",
      static_cast<uint32_t>(uid()), static_cast<uint32_t>(pid()));
  base::StackString<255> path_v2_app(
      "/sys/fs/cgroup/apps/uid_%" PRIu32 "/pid_%" PRIu32 "/cgroup.freeze",
      static_cast<uint32_t>(uid()), static_cast<uint32_t>(pid()));
  base::StackString<255> path_v2_system(
      "/sys/fs/cgroup/system/uid_%" PRIu32 "/pid_%" PRIu32 "/cgroup.freeze",
      static_cast<uint32_t>(uid()), static_cast<uint32_t>(pid()));
  const char* paths[] = {path_v1.c_str(), path_v2_app.c_str(),
                         path_v2_system.c_str()};

  for (const char* path : paths) {
    char frozen = '0';
    auto fd = base::OpenFile(path, O_RDONLY);
    ssize_t rsize = 0;
    if (fd) {
      rsize = base::Read(*fd, &frozen, sizeof(frozen));
      if (rsize > 0) {
        return frozen == '1';
      }
    }
  }
  PERFETTO_DLOG("Failed to read cgroup.freeze from [%s, %s, %s]",
                path_v1.c_str(), path_v2_app.c_str(), path_v2_system.c_str());

#endif
  return false;
}

////////////////////////////////////////////////////////////////////////////////
// TracingServiceImpl::TracingSession implementation
////////////////////////////////////////////////////////////////////////////////

TracingServiceImpl::TracingSession::TracingSession(
    TracingSessionID session_id,
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

////////////////////////////////////////////////////////////////////////////////
// TracingServiceImpl::RelayEndpointImpl implementation
////////////////////////////////////////////////////////////////////////////////
TracingServiceImpl::RelayEndpointImpl::RelayEndpointImpl(
    RelayClientID relay_client_id,
    TracingServiceImpl* service)
    : relay_client_id_(relay_client_id),
      service_(service),
      serialized_system_info_({}) {}
TracingServiceImpl::RelayEndpointImpl::~RelayEndpointImpl() = default;

void TracingServiceImpl::RelayEndpointImpl::SyncClocks(
    SyncMode sync_mode,
    base::ClockSnapshotVector client_clocks,
    base::ClockSnapshotVector host_clocks) {
  // We keep only the most recent 5 clock sync snapshots.
  static constexpr size_t kNumSyncClocks = 5;
  if (synced_clocks_.size() >= kNumSyncClocks)
    synced_clocks_.pop_front();

  synced_clocks_.emplace_back(sync_mode, std::move(client_clocks),
                              std::move(host_clocks));
}

void TracingServiceImpl::RelayEndpointImpl::Disconnect() {
  service_->DisconnectRelayClient(relay_client_id_);
}

}  // namespace perfetto
