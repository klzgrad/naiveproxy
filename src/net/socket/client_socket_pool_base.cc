// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/client_socket_pool_base.h"

#include <algorithm>
#include <utility>

#include "base/bind.h"
#include "base/compiler_specific.h"
#include "base/format_macros.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "base/trace_event/memory_allocator_dump.h"
#include "base/trace_event/process_memory_dump.h"
#include "base/values.h"
#include "net/base/net_errors.h"
#include "net/log/net_log.h"
#include "net/log/net_log_event_type.h"
#include "net/log/net_log_source.h"

using base::TimeDelta;

namespace net {

namespace {

// Indicate whether or not we should establish a new transport layer connection
// after a certain timeout has passed without receiving an ACK.
bool g_connect_backup_jobs_enabled = true;

std::unique_ptr<base::Value> NetLogCreateConnectJobCallback(
    bool backup_job,
    const std::string* group_name,
    net::NetLogCaptureMode capture_mode) {
  auto dict = std::make_unique<base::DictionaryValue>();
  dict->SetBoolean("backup_job", backup_job);
  dict->SetString("group_name", *group_name);
  return std::move(dict);
}

}  // namespace

namespace internal {

ClientSocketPoolBaseHelper::Request::Request(
    ClientSocketHandle* handle,
    CompletionOnceCallback callback,
    const ClientSocketPool::ProxyAuthCallback& proxy_auth_callback,
    RequestPriority priority,
    const SocketTag& socket_tag,
    ClientSocketPool::RespectLimits respect_limits,
    Flags flags,
    const NetLogWithSource& net_log)
    : handle_(handle),
      callback_(std::move(callback)),
      proxy_auth_callback_(proxy_auth_callback),
      priority_(priority),
      respect_limits_(respect_limits),
      flags_(flags),
      net_log_(net_log),
      socket_tag_(socket_tag),
      job_(nullptr) {
  if (respect_limits_ == ClientSocketPool::RespectLimits::DISABLED)
    DCHECK_EQ(priority_, MAXIMUM_PRIORITY);
}

ClientSocketPoolBaseHelper::Request::~Request() {
  liveness_ = DEAD;
}

void ClientSocketPoolBaseHelper::Request::AssignJob(ConnectJob* job) {
  DCHECK(job);
  DCHECK(!job_);
  job_ = job;
  if (job_->priority() != priority_)
    job_->ChangePriority(priority_);
}

ConnectJob* ClientSocketPoolBaseHelper::Request::ReleaseJob() {
  DCHECK(job_);
  ConnectJob* job = job_;
  job_ = nullptr;
  return job;
}

void ClientSocketPoolBaseHelper::Request::CrashIfInvalid() const {
  CHECK_EQ(liveness_, ALIVE);
}

ClientSocketPoolBaseHelper::ClientSocketPoolBaseHelper(
    int max_sockets,
    int max_sockets_per_group,
    base::TimeDelta unused_idle_socket_timeout,
    base::TimeDelta used_idle_socket_timeout,
    ConnectJobFactory* connect_job_factory)
    : idle_socket_count_(0),
      connecting_socket_count_(0),
      handed_out_socket_count_(0),
      max_sockets_(max_sockets),
      max_sockets_per_group_(max_sockets_per_group),
      unused_idle_socket_timeout_(unused_idle_socket_timeout),
      used_idle_socket_timeout_(used_idle_socket_timeout),
      connect_job_factory_(connect_job_factory),
      connect_backup_jobs_enabled_(false),
      pool_generation_number_(0),
      weak_factory_(this) {
  DCHECK_LE(0, max_sockets_per_group);
  DCHECK_LE(max_sockets_per_group, max_sockets);

  NetworkChangeNotifier::AddIPAddressObserver(this);
}

ClientSocketPoolBaseHelper::~ClientSocketPoolBaseHelper() {
  // Clean up any idle sockets and pending connect jobs.  Assert that we have no
  // remaining active sockets or pending requests.  They should have all been
  // cleaned up prior to |this| being destroyed.
  FlushWithError(ERR_ABORTED);
  DCHECK(group_map_.empty());
  DCHECK(pending_callback_map_.empty());
  DCHECK_EQ(0, connecting_socket_count_);
  DCHECK_EQ(0, handed_out_socket_count_);
  CHECK(higher_pools_.empty());

  NetworkChangeNotifier::RemoveIPAddressObserver(this);
}

ClientSocketPoolBaseHelper::CallbackResultPair::CallbackResultPair()
    : result(OK) {
}

ClientSocketPoolBaseHelper::CallbackResultPair::CallbackResultPair(
    CompletionOnceCallback callback_in,
    int result_in)
    : callback(std::move(callback_in)), result(result_in) {}

ClientSocketPoolBaseHelper::CallbackResultPair::CallbackResultPair(
    ClientSocketPoolBaseHelper::CallbackResultPair&& other) = default;

ClientSocketPoolBaseHelper::CallbackResultPair&
ClientSocketPoolBaseHelper::CallbackResultPair::operator=(
    ClientSocketPoolBaseHelper::CallbackResultPair&& other) = default;

ClientSocketPoolBaseHelper::CallbackResultPair::~CallbackResultPair() = default;

bool ClientSocketPoolBaseHelper::IsStalled() const {
  // If fewer than |max_sockets_| are in use, then clearly |this| is not
  // stalled.
  if ((handed_out_socket_count_ + connecting_socket_count_) < max_sockets_)
    return false;
  // So in order to be stalled, |this| must be using at least |max_sockets_| AND
  // |this| must have a request that is actually stalled on the global socket
  // limit.  To find such a request, look for a group that has more requests
  // than jobs AND where the number of sockets is less than
  // |max_sockets_per_group_|.  (If the number of sockets is equal to
  // |max_sockets_per_group_|, then the request is stalled on the group limit,
  // which does not count.)
  for (auto it = group_map_.begin(); it != group_map_.end(); ++it) {
    if (it->second->CanUseAdditionalSocketSlot(max_sockets_per_group_))
      return true;
  }
  return false;
}

void ClientSocketPoolBaseHelper::AddHigherLayeredPool(
    HigherLayeredPool* higher_pool) {
  CHECK(higher_pool);
  CHECK(!base::ContainsKey(higher_pools_, higher_pool));
  higher_pools_.insert(higher_pool);
}

void ClientSocketPoolBaseHelper::RemoveHigherLayeredPool(
    HigherLayeredPool* higher_pool) {
  CHECK(higher_pool);
  CHECK(base::ContainsKey(higher_pools_, higher_pool));
  higher_pools_.erase(higher_pool);
}

int ClientSocketPoolBaseHelper::RequestSocket(
    const std::string& group_name,
    std::unique_ptr<Request> request) {
  CHECK(request->has_callback());
  CHECK(request->handle());

  // Cleanup any timed-out idle sockets.
  CleanupIdleSockets(false);

  request->net_log().BeginEvent(NetLogEventType::SOCKET_POOL);

  int rv = RequestSocketInternal(group_name, *request);
  if (rv != ERR_IO_PENDING) {
    if (rv == OK) {
      request->handle()->socket()->ApplySocketTag(request->socket_tag());
    }
    request->net_log().EndEventWithNetErrorCode(NetLogEventType::SOCKET_POOL,
                                                rv);
    CHECK(!request->handle()->is_initialized());
    request.reset();
  } else {
    Group* group = GetOrCreateGroup(group_name);
    group->InsertUnboundRequest(std::move(request));
    // Have to do this asynchronously, as closing sockets in higher level pools
    // call back in to |this|, which will cause all sorts of fun and exciting
    // re-entrancy issues if the socket pool is doing something else at the
    // time.
    if (group->CanUseAdditionalSocketSlot(max_sockets_per_group_)) {
      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE,
          base::BindOnce(
              &ClientSocketPoolBaseHelper::TryToCloseSocketsInLayeredPools,
              weak_factory_.GetWeakPtr()));
    }
  }
  return rv;
}

void ClientSocketPoolBaseHelper::RequestSockets(const std::string& group_name,
                                                const Request& request,
                                                int num_sockets) {
  DCHECK(!request.has_callback());
  DCHECK(!request.handle());

  // Cleanup any timed-out idle sockets.
  CleanupIdleSockets(false);

  if (num_sockets > max_sockets_per_group_) {
    num_sockets = max_sockets_per_group_;
  }

  request.net_log().BeginEvent(
      NetLogEventType::SOCKET_POOL_CONNECTING_N_SOCKETS,
      NetLog::IntCallback("num_sockets", num_sockets));

  Group* group = GetOrCreateGroup(group_name);

  // RequestSocketsInternal() may delete the group.
  bool deleted_group = false;

  int rv = OK;
  for (int num_iterations_left = num_sockets;
       group->NumActiveSocketSlots() < num_sockets &&
       num_iterations_left > 0 ; num_iterations_left--) {
    rv = RequestSocketInternal(group_name, request);
    if (rv < 0 && rv != ERR_IO_PENDING) {
      // We're encountering a synchronous error.  Give up.
      if (!base::ContainsKey(group_map_, group_name))
        deleted_group = true;
      break;
    }
    if (!base::ContainsKey(group_map_, group_name)) {
      // Unexpected.  The group should only be getting deleted on synchronous
      // error.
      NOTREACHED();
      deleted_group = true;
      break;
    }
  }

  if (!deleted_group && group->IsEmpty())
    RemoveGroup(group_name);

  if (rv == ERR_IO_PENDING)
    rv = OK;
  request.net_log().EndEventWithNetErrorCode(
      NetLogEventType::SOCKET_POOL_CONNECTING_N_SOCKETS, rv);
}

int ClientSocketPoolBaseHelper::RequestSocketInternal(
    const std::string& group_name,
    const Request& request) {
  ClientSocketHandle* const handle = request.handle();
  const bool preconnecting = !handle;

  Group* group = nullptr;
  auto group_it = group_map_.find(group_name);
  if (group_it != group_map_.end()) {
    group = group_it->second;

    if (!(request.flags() & NO_IDLE_SOCKETS)) {
      // Try to reuse a socket.
      if (AssignIdleSocketToRequest(request, group))
        return OK;
    }

    // If there are more ConnectJobs than pending requests, don't need to do
    // anything.  Can just wait for the extra job to connect, and then assign it
    // to the request.
    if (!preconnecting && group->TryToUseNeverAssignedConnectJob())
      return ERR_IO_PENDING;

    // Can we make another active socket now?
    if (!group->HasAvailableSocketSlot(max_sockets_per_group_) &&
        request.respect_limits() == ClientSocketPool::RespectLimits::ENABLED) {
      // TODO(willchan): Consider whether or not we need to close a socket in a
      // higher layered group. I don't think this makes sense since we would
      // just reuse that socket then if we needed one and wouldn't make it down
      // to this layer.
      request.net_log().AddEvent(
          NetLogEventType::SOCKET_POOL_STALLED_MAX_SOCKETS_PER_GROUP);
      return ERR_IO_PENDING;
    }
  }

  if (ReachedMaxSocketsLimit() &&
      request.respect_limits() == ClientSocketPool::RespectLimits::ENABLED) {
    // NOTE(mmenke):  Wonder if we really need different code for each case
    // here.  Only reason for them now seems to be preconnects.
    if (idle_socket_count() > 0) {
      // There's an idle socket in this pool. Either that's because there's
      // still one in this group, but we got here due to preconnecting
      // bypassing idle sockets, or because there's an idle socket in another
      // group.
      bool closed = CloseOneIdleSocketExceptInGroup(group);
      if (preconnecting && !closed)
        return ERR_PRECONNECT_MAX_SOCKET_LIMIT;
    } else {
      // We could check if we really have a stalled group here, but it
      // requires a scan of all groups, so just flip a flag here, and do the
      // check later.
      request.net_log().AddEvent(
          NetLogEventType::SOCKET_POOL_STALLED_MAX_SOCKETS);
      return ERR_IO_PENDING;
    }
  }

  // We couldn't find a socket to reuse, and there's space to allocate one,
  // so allocate and connect a new one.
  group = GetOrCreateGroup(group_name);
  connecting_socket_count_++;
  std::unique_ptr<ConnectJob> owned_connect_job(
      connect_job_factory_->NewConnectJob(request, group));
  owned_connect_job->net_log().AddEvent(
      NetLogEventType::SOCKET_POOL_CONNECT_JOB_CREATED,
      base::BindRepeating(&NetLogCreateConnectJobCallback,
                          false /* backup_job */, &group_name));
  ConnectJob* connect_job = owned_connect_job.get();
  bool was_group_empty = group->IsEmpty();
  // Need to add the ConnectJob to the group before connecting, to ensure
  // |group| is not empty.  Otherwise, if the ConnectJob calls back into the
  // socket pool with a new socket request (Like for DNS over HTTPS), the pool
  // would then notice the group is empty, and delete it. That would result in a
  // UAF when group is referenced later in this function.
  group->AddJob(std::move(owned_connect_job), preconnecting);

  int rv = connect_job->Connect();
  if (rv == OK) {
    LogBoundConnectJobToRequest(connect_job->net_log().source(), request);
    if (!preconnecting) {
      HandOutSocket(connect_job->PassSocket(), ClientSocketHandle::UNUSED,
                    connect_job->connect_timing(), handle, base::TimeDelta(),
                    group, request.net_log());
    } else {
      AddIdleSocket(connect_job->PassSocket(), group);
    }
    RemoveConnectJob(connect_job, group);
  } else if (rv == ERR_IO_PENDING) {
    // If we didn't have any sockets in this group, set a timer for potentially
    // creating a new one.  If the SYN is lost, this backup socket may complete
    // before the slow socket, improving end user latency.
    if (connect_backup_jobs_enabled_ && was_group_empty)
      group->StartBackupJobTimer(group_name);
  } else {
    LogBoundConnectJobToRequest(connect_job->net_log().source(), request);
    std::unique_ptr<StreamSocket> error_socket;
    if (!preconnecting) {
      DCHECK(handle);
      connect_job->GetAdditionalErrorState(handle);
      error_socket = connect_job->PassSocket();
    }
    if (error_socket) {
      HandOutSocket(std::move(error_socket), ClientSocketHandle::UNUSED,
                    connect_job->connect_timing(), handle, base::TimeDelta(),
                    group, request.net_log());
    }
    RemoveConnectJob(connect_job, group);
    if (group->IsEmpty())
      RemoveGroup(group_name);
  }

  return rv;
}

bool ClientSocketPoolBaseHelper::AssignIdleSocketToRequest(
    const Request& request, Group* group) {
  std::list<IdleSocket>* idle_sockets = group->mutable_idle_sockets();
  auto idle_socket_it = idle_sockets->end();

  // Iterate through the idle sockets forwards (oldest to newest)
  //   * Delete any disconnected ones.
  //   * If we find a used idle socket, assign to |idle_socket|.  At the end,
  //   the |idle_socket_it| will be set to the newest used idle socket.
  for (auto it = idle_sockets->begin(); it != idle_sockets->end();) {
    // Check whether socket is usable. Note that it's unlikely that the socket
    // is not usuable because this function is always invoked after a
    // reusability check, but in theory socket can be closed asynchronously.
    if (!it->IsUsable()) {
      DecrementIdleCount();
      delete it->socket;
      it = idle_sockets->erase(it);
      continue;
    }

    if (it->socket->WasEverUsed()) {
      // We found one we can reuse!
      idle_socket_it = it;
    }

    ++it;
  }

  // If we haven't found an idle socket, that means there are no used idle
  // sockets.  Pick the oldest (first) idle socket (FIFO).

  if (idle_socket_it == idle_sockets->end() && !idle_sockets->empty())
    idle_socket_it = idle_sockets->begin();

  if (idle_socket_it != idle_sockets->end()) {
    DecrementIdleCount();
    base::TimeDelta idle_time =
        base::TimeTicks::Now() - idle_socket_it->start_time;
    IdleSocket idle_socket = *idle_socket_it;
    idle_sockets->erase(idle_socket_it);
    // TODO(davidben): If |idle_time| is under some low watermark, consider
    // treating as UNUSED rather than UNUSED_IDLE. This will avoid
    // HttpNetworkTransaction retrying on some errors.
    ClientSocketHandle::SocketReuseType reuse_type =
        idle_socket.socket->WasEverUsed() ?
            ClientSocketHandle::REUSED_IDLE :
            ClientSocketHandle::UNUSED_IDLE;

    // If this socket took multiple attempts to obtain, don't report those
    // every time it's reused, just to the first user.
    if (idle_socket.socket->WasEverUsed())
      idle_socket.socket->ClearConnectionAttempts();

    HandOutSocket(std::unique_ptr<StreamSocket>(idle_socket.socket), reuse_type,
                  LoadTimingInfo::ConnectTiming(), request.handle(), idle_time,
                  group, request.net_log());
    return true;
  }

  return false;
}

// static
void ClientSocketPoolBaseHelper::LogBoundConnectJobToRequest(
    const NetLogSource& connect_job_source,
    const Request& request) {
  request.net_log().AddEvent(NetLogEventType::SOCKET_POOL_BOUND_TO_CONNECT_JOB,
                             connect_job_source.ToEventParametersCallback());
}

void ClientSocketPoolBaseHelper::SetPriority(const std::string& group_name,
                                             ClientSocketHandle* handle,
                                             RequestPriority priority) {
  auto group_it = group_map_.find(group_name);
  if (group_it == group_map_.end()) {
    DCHECK(base::ContainsKey(pending_callback_map_, handle));
    // The Request has already completed and been destroyed; nothing to
    // reprioritize.
    return;
  }

  group_it->second->SetPriority(handle, priority);
}

void ClientSocketPoolBaseHelper::CancelRequest(
    const std::string& group_name, ClientSocketHandle* handle) {
  auto callback_it = pending_callback_map_.find(handle);
  if (callback_it != pending_callback_map_.end()) {
    int result = callback_it->second.result;
    pending_callback_map_.erase(callback_it);
    std::unique_ptr<StreamSocket> socket = handle->PassSocket();
    if (socket) {
      if (result != OK)
        socket->Disconnect();
      ReleaseSocket(handle->group_name(), std::move(socket), handle->id());
    }
    return;
  }

  CHECK(base::ContainsKey(group_map_, group_name));
  Group* group = GetOrCreateGroup(group_name);

  std::unique_ptr<Request> request = group->FindAndRemoveBoundRequest(handle);
  if (request) {
    --connecting_socket_count_;
    OnAvailableSocketSlot(group_name, group);
    CheckForStalledSocketGroups();
    return;
  }

  // Search |unbound_requests_| for matching handle.
  request = group->FindAndRemoveUnboundRequest(handle);
  if (request) {
    request->net_log().AddEvent(NetLogEventType::CANCELLED);
    request->net_log().EndEvent(NetLogEventType::SOCKET_POOL);

    // We let the job run, unless we're at the socket limit and there is
    // not another request waiting on the job.
    if (group->jobs().size() > group->unbound_request_count() &&
        ReachedMaxSocketsLimit()) {
      RemoveConnectJob(group->jobs().begin()->get(), group);
      CheckForStalledSocketGroups();
    }
  }
}

bool ClientSocketPoolBaseHelper::HasGroup(const std::string& group_name) const {
  return base::ContainsKey(group_map_, group_name);
}

void ClientSocketPoolBaseHelper::CloseIdleSockets() {
  CleanupIdleSockets(true);
  DCHECK_EQ(0, idle_socket_count_);
}

void ClientSocketPoolBaseHelper::CloseIdleSocketsInGroup(
    const std::string& group_name) {
  if (idle_socket_count_ == 0)
    return;
  auto it = group_map_.find(group_name);
  if (it == group_map_.end())
    return;
  CleanupIdleSocketsInGroup(true, it->second, base::TimeTicks::Now());
  if (it->second->IsEmpty())
    RemoveGroup(it);
}

size_t ClientSocketPoolBaseHelper::IdleSocketCountInGroup(
    const std::string& group_name) const {
  auto i = group_map_.find(group_name);
  CHECK(i != group_map_.end());

  return i->second->idle_sockets().size();
}

LoadState ClientSocketPoolBaseHelper::GetLoadState(
    const std::string& group_name,
    const ClientSocketHandle* handle) const {
  if (base::ContainsKey(pending_callback_map_, handle))
    return LOAD_STATE_CONNECTING;

  auto group_it = group_map_.find(group_name);
  if (group_it == group_map_.end()) {
    // TODO(mmenke):  This is actually reached in the wild, for unknown reasons.
    // Would be great to understand why, and if it's a bug, fix it.  If not,
    // should have a test for that case.
    NOTREACHED();
    return LOAD_STATE_IDLE;
  }

  const Group& group = *group_it->second;
  ConnectJob* job = group.GetConnectJobForHandle(handle);
  if (job)
    return job->GetLoadState();

  if (group.CanUseAdditionalSocketSlot(max_sockets_per_group_))
    return LOAD_STATE_WAITING_FOR_STALLED_SOCKET_POOL;
  return LOAD_STATE_WAITING_FOR_AVAILABLE_SOCKET;
}

std::unique_ptr<base::DictionaryValue>
ClientSocketPoolBaseHelper::GetInfoAsValue(const std::string& name,
                                           const std::string& type) const {
  // TODO(mmenke): This currently doesn't return bound Requests or ConnectJobs.
  auto dict = std::make_unique<base::DictionaryValue>();
  dict->SetString("name", name);
  dict->SetString("type", type);
  dict->SetInteger("handed_out_socket_count", handed_out_socket_count_);
  dict->SetInteger("connecting_socket_count", connecting_socket_count_);
  dict->SetInteger("idle_socket_count", idle_socket_count_);
  dict->SetInteger("max_socket_count", max_sockets_);
  dict->SetInteger("max_sockets_per_group", max_sockets_per_group_);
  dict->SetInteger("pool_generation_number", pool_generation_number_);

  if (group_map_.empty())
    return dict;

  auto all_groups_dict = std::make_unique<base::DictionaryValue>();
  for (auto it = group_map_.begin(); it != group_map_.end(); it++) {
    const Group* group = it->second;
    auto group_dict = std::make_unique<base::DictionaryValue>();

    group_dict->SetInteger("pending_request_count",
                           group->unbound_request_count());
    if (group->has_unbound_requests()) {
      group_dict->SetString(
          "top_pending_priority",
          RequestPriorityToString(group->TopPendingPriority()));
    }

    group_dict->SetInteger("active_socket_count", group->active_socket_count());

    auto idle_socket_list = std::make_unique<base::ListValue>();
    std::list<IdleSocket>::const_iterator idle_socket;
    for (idle_socket = group->idle_sockets().begin();
         idle_socket != group->idle_sockets().end();
         idle_socket++) {
      int source_id = idle_socket->socket->NetLog().source().id;
      idle_socket_list->AppendInteger(source_id);
    }
    group_dict->Set("idle_sockets", std::move(idle_socket_list));

    auto connect_jobs_list = std::make_unique<base::ListValue>();
    for (auto job = group->jobs().begin(); job != group->jobs().end(); job++) {
      int source_id = (*job)->net_log().source().id;
      connect_jobs_list->AppendInteger(source_id);
    }
    group_dict->Set("connect_jobs", std::move(connect_jobs_list));

    group_dict->SetBoolean("is_stalled", group->CanUseAdditionalSocketSlot(
                                             max_sockets_per_group_));
    group_dict->SetBoolean("backup_job_timer_is_running",
                           group->BackupJobTimerIsRunning());

    all_groups_dict->SetWithoutPathExpansion(it->first, std::move(group_dict));
  }
  dict->Set("groups", std::move(all_groups_dict));
  return dict;
}

void ClientSocketPoolBaseHelper::DumpMemoryStats(
    base::trace_event::ProcessMemoryDump* pmd,
    const std::string& parent_dump_absolute_name) const {
  size_t socket_count = 0;
  size_t total_size = 0;
  size_t buffer_size = 0;
  size_t cert_count = 0;
  size_t cert_size = 0;
  for (const auto& kv : group_map_) {
    for (const auto& socket : kv.second->idle_sockets()) {
      StreamSocket::SocketMemoryStats stats;
      socket.socket->DumpMemoryStats(&stats);
      total_size += stats.total_size;
      buffer_size += stats.buffer_size;
      cert_count += stats.cert_count;
      cert_size += stats.cert_size;
      ++socket_count;
    }
  }
  // Only create a MemoryAllocatorDump if there is at least one idle socket
  if (socket_count > 0) {
    base::trace_event::MemoryAllocatorDump* socket_pool_dump =
        pmd->CreateAllocatorDump(base::StringPrintf(
            "%s/socket_pool", parent_dump_absolute_name.c_str()));
    socket_pool_dump->AddScalar(
        base::trace_event::MemoryAllocatorDump::kNameSize,
        base::trace_event::MemoryAllocatorDump::kUnitsBytes, total_size);
    socket_pool_dump->AddScalar(
        base::trace_event::MemoryAllocatorDump::kNameObjectCount,
        base::trace_event::MemoryAllocatorDump::kUnitsObjects, socket_count);
    socket_pool_dump->AddScalar(
        "buffer_size", base::trace_event::MemoryAllocatorDump::kUnitsBytes,
        buffer_size);
    socket_pool_dump->AddScalar(
        "cert_count", base::trace_event::MemoryAllocatorDump::kUnitsObjects,
        cert_count);
    socket_pool_dump->AddScalar(
        "cert_size", base::trace_event::MemoryAllocatorDump::kUnitsBytes,
        cert_size);
  }
}

bool ClientSocketPoolBaseHelper::IdleSocket::IsUsable() const {
  if (socket->WasEverUsed())
    return socket->IsConnectedAndIdle();
  return socket->IsConnected();
}

void ClientSocketPoolBaseHelper::CleanupIdleSockets(bool force) {
  if (idle_socket_count_ == 0)
    return;

  // Current time value. Retrieving it once at the function start rather than
  // inside the inner loop, since it shouldn't change by any meaningful amount.
  base::TimeTicks now = base::TimeTicks::Now();

  for (auto i = group_map_.begin(); i != group_map_.end();) {
    Group* group = i->second;
    CleanupIdleSocketsInGroup(force, group, now);
    // Delete group if no longer needed.
    if (group->IsEmpty()) {
      auto old = i++;
      RemoveGroup(old);
    } else {
      ++i;
    }
  }
}

void ClientSocketPoolBaseHelper::CleanupIdleSocketsInGroup(
    bool force,
    Group* group,
    const base::TimeTicks& now) {
  auto idle_socket_it = group->mutable_idle_sockets()->begin();
  while (idle_socket_it != group->idle_sockets().end()) {
    base::TimeDelta timeout = idle_socket_it->socket->WasEverUsed()
                                  ? used_idle_socket_timeout_
                                  : unused_idle_socket_timeout_;
    bool timed_out = (now - idle_socket_it->start_time) >= timeout;
    bool should_clean_up = force || timed_out || !idle_socket_it->IsUsable();
    if (should_clean_up) {
      delete idle_socket_it->socket;
      idle_socket_it = group->mutable_idle_sockets()->erase(idle_socket_it);
      DecrementIdleCount();
    } else {
      ++idle_socket_it;
    }
  }
}

ClientSocketPoolBaseHelper::Group* ClientSocketPoolBaseHelper::GetOrCreateGroup(
    const std::string& group_name) {
  auto it = group_map_.find(group_name);
  if (it != group_map_.end())
    return it->second;
  Group* group = new Group(group_name, this);
  group_map_[group_name] = group;
  return group;
}

void ClientSocketPoolBaseHelper::RemoveGroup(const std::string& group_name) {
  auto it = group_map_.find(group_name);
  CHECK(it != group_map_.end());

  RemoveGroup(it);
}

void ClientSocketPoolBaseHelper::RemoveGroup(GroupMap::iterator it) {
  delete it->second;
  group_map_.erase(it);
}

// static
bool ClientSocketPoolBaseHelper::connect_backup_jobs_enabled() {
  return g_connect_backup_jobs_enabled;
}

// static
bool ClientSocketPoolBaseHelper::set_connect_backup_jobs_enabled(bool enabled) {
  bool old_value = g_connect_backup_jobs_enabled;
  g_connect_backup_jobs_enabled = enabled;
  return old_value;
}

void ClientSocketPoolBaseHelper::EnableConnectBackupJobs() {
  connect_backup_jobs_enabled_ = g_connect_backup_jobs_enabled;
}

void ClientSocketPoolBaseHelper::IncrementIdleCount() {
  ++idle_socket_count_;
}

void ClientSocketPoolBaseHelper::DecrementIdleCount() {
  --idle_socket_count_;
}

void ClientSocketPoolBaseHelper::ReleaseSocket(
    const std::string& group_name,
    std::unique_ptr<StreamSocket> socket,
    int id) {
  auto i = group_map_.find(group_name);
  CHECK(i != group_map_.end());

  Group* group = i->second;

  CHECK_GT(handed_out_socket_count_, 0);
  handed_out_socket_count_--;

  CHECK_GT(group->active_socket_count(), 0);
  group->DecrementActiveSocketCount();

  const bool can_reuse = socket->IsConnectedAndIdle() &&
      id == pool_generation_number_;
  if (can_reuse) {
    // Add it to the idle list.
    AddIdleSocket(std::move(socket), group);
    OnAvailableSocketSlot(group_name, group);
  } else {
    socket.reset();
  }

  CheckForStalledSocketGroups();
}

void ClientSocketPoolBaseHelper::CheckForStalledSocketGroups() {
  // Loop until there's nothing more to do.
  while (true) {
    // If we have idle sockets, see if we can give one to the top-stalled group.
    std::string top_group_name;
    Group* top_group = NULL;
    if (!FindTopStalledGroup(&top_group, &top_group_name))
      return;

    if (ReachedMaxSocketsLimit()) {
      if (idle_socket_count() > 0) {
        CloseOneIdleSocket();
      } else {
        // We can't activate more sockets since we're already at our global
        // limit.
        return;
      }
    }

    // Note that this may delete top_group.
    OnAvailableSocketSlot(top_group_name, top_group);
  }
}

// Search for the highest priority pending request, amongst the groups that
// are not at the |max_sockets_per_group_| limit. Note: for requests with
// the same priority, the winner is based on group hash ordering (and not
// insertion order).
bool ClientSocketPoolBaseHelper::FindTopStalledGroup(
    Group** group,
    std::string* group_name) const {
  CHECK((group && group_name) || (!group && !group_name));
  Group* top_group = NULL;
  const std::string* top_group_name = NULL;
  bool has_stalled_group = false;
  for (auto i = group_map_.begin(); i != group_map_.end(); ++i) {
    Group* curr_group = i->second;
    if (!curr_group->has_unbound_requests())
      continue;
    if (curr_group->CanUseAdditionalSocketSlot(max_sockets_per_group_)) {
      if (!group)
        return true;
      has_stalled_group = true;
      bool has_higher_priority = !top_group ||
          curr_group->TopPendingPriority() > top_group->TopPendingPriority();
      if (has_higher_priority) {
        top_group = curr_group;
        top_group_name = &i->first;
      }
    }
  }

  if (top_group) {
    CHECK(group);
    *group = top_group;
    *group_name = *top_group_name;
  } else {
    CHECK(!has_stalled_group);
  }
  return has_stalled_group;
}

void ClientSocketPoolBaseHelper::OnIPAddressChanged() {
  FlushWithError(ERR_NETWORK_CHANGED);
}

void ClientSocketPoolBaseHelper::FlushWithError(int error) {
  pool_generation_number_++;
  CancelAllConnectJobs();
  CloseIdleSockets();
  CancelAllRequestsWithError(error);
}

void ClientSocketPoolBaseHelper::RemoveConnectJob(ConnectJob* job,
                                                  Group* group) {
  CHECK_GT(connecting_socket_count_, 0);
  connecting_socket_count_--;

  DCHECK(group);
  group->RemoveUnboundJob(job);
}

void ClientSocketPoolBaseHelper::OnAvailableSocketSlot(
    const std::string& group_name, Group* group) {
  DCHECK(base::ContainsKey(group_map_, group_name));
  if (group->IsEmpty()) {
    RemoveGroup(group_name);
  } else if (group->has_unbound_requests()) {
    ProcessPendingRequest(group_name, group);
  }
}

void ClientSocketPoolBaseHelper::ProcessPendingRequest(
    const std::string& group_name, Group* group) {
  const Request* next_request = group->GetNextUnboundRequest();
  DCHECK(next_request);

  // If the group has no idle sockets, and can't make use of an additional slot,
  // either because it's at the limit or because it's at the socket per group
  // limit, then there's nothing to do.
  if (group->idle_sockets().empty() &&
      !group->CanUseAdditionalSocketSlot(max_sockets_per_group_)) {
    return;
  }

  int rv = RequestSocketInternal(group_name, *next_request);
  if (rv != ERR_IO_PENDING) {
    std::unique_ptr<Request> request = group->PopNextUnboundRequest();
    DCHECK(request);
    if (group->IsEmpty())
      RemoveGroup(group_name);

    request->net_log().EndEventWithNetErrorCode(NetLogEventType::SOCKET_POOL,
                                                rv);
    InvokeUserCallbackLater(request->handle(), request->release_callback(), rv,
                            request->socket_tag());
  }
}

void ClientSocketPoolBaseHelper::HandOutSocket(
    std::unique_ptr<StreamSocket> socket,
    ClientSocketHandle::SocketReuseType reuse_type,
    const LoadTimingInfo::ConnectTiming& connect_timing,
    ClientSocketHandle* handle,
    base::TimeDelta idle_time,
    Group* group,
    const NetLogWithSource& net_log) {
  DCHECK(socket);
  handle->SetSocket(std::move(socket));
  handle->set_reuse_type(reuse_type);
  handle->set_idle_time(idle_time);
  handle->set_pool_id(pool_generation_number_);
  handle->set_connect_timing(connect_timing);

  if (reuse_type == ClientSocketHandle::REUSED_IDLE) {
    net_log.AddEvent(
        NetLogEventType::SOCKET_POOL_REUSED_AN_EXISTING_SOCKET,
        NetLog::IntCallback("idle_ms",
                            static_cast<int>(idle_time.InMilliseconds())));
  }

  if (reuse_type != ClientSocketHandle::UNUSED) {
    // The socket being handed out is no longer considered idle, but was
    // considered idle until just before this method was called.
    UMA_HISTOGRAM_CUSTOM_COUNTS("Net.Socket.NumIdleSockets",
                                idle_socket_count() + 1, 1, 256, 50);
  }

  net_log.AddEvent(
      NetLogEventType::SOCKET_POOL_BOUND_TO_SOCKET,
      handle->socket()->NetLog().source().ToEventParametersCallback());

  handed_out_socket_count_++;
  group->IncrementActiveSocketCount();
}

void ClientSocketPoolBaseHelper::AddIdleSocket(
    std::unique_ptr<StreamSocket> socket,
    Group* group) {
  DCHECK(socket);
  IdleSocket idle_socket;
  idle_socket.socket = socket.release();
  idle_socket.start_time = base::TimeTicks::Now();

  group->mutable_idle_sockets()->push_back(idle_socket);
  IncrementIdleCount();
}

void ClientSocketPoolBaseHelper::CancelAllConnectJobs() {
  for (auto i = group_map_.begin(); i != group_map_.end();) {
    Group* group = i->second;
    connecting_socket_count_ -= group->jobs().size();
    group->RemoveAllUnboundJobs();

    // Delete group if no longer needed.
    if (group->IsEmpty()) {
      auto old = i++;
      RemoveGroup(old);
    } else {
      ++i;
    }
  }
}

void ClientSocketPoolBaseHelper::CancelAllRequestsWithError(int error) {
  for (auto i = group_map_.begin(); i != group_map_.end();) {
    Group* group = i->second;

    while (true) {
      std::unique_ptr<Request> request = group->PopNextUnboundRequest();
      if (!request)
        break;
      InvokeUserCallbackLater(request->handle(), request->release_callback(),
                              error, request->socket_tag());
    }

    // Mark bound connect jobs as needing to fail. Can't fail them immediately
    // because they may have access to objects owned by the ConnectJob, and
    // could access them if a user callback invocation is queued. It would also
    // result in the consumer handling two messages at once, which in general
    // isn't safe for a lot of code.
    group->SetPendingErrorForAllBoundRequests(error);

    // Delete group if no longer needed.
    if (group->IsEmpty()) {
      auto old = i++;
      RemoveGroup(old);
    } else {
      ++i;
    }
  }
}

bool ClientSocketPoolBaseHelper::ReachedMaxSocketsLimit() const {
  // Each connecting socket will eventually connect and be handed out.
  int total = handed_out_socket_count_ + connecting_socket_count_ +
      idle_socket_count();
  // There can be more sockets than the limit since some requests can ignore
  // the limit
  if (total < max_sockets_)
    return false;
  return true;
}

bool ClientSocketPoolBaseHelper::CloseOneIdleSocket() {
  if (idle_socket_count() == 0)
    return false;
  return CloseOneIdleSocketExceptInGroup(NULL);
}

bool ClientSocketPoolBaseHelper::CloseOneIdleSocketExceptInGroup(
    const Group* exception_group) {
  CHECK_GT(idle_socket_count(), 0);

  for (auto i = group_map_.begin(); i != group_map_.end(); ++i) {
    Group* group = i->second;
    if (exception_group == group)
      continue;
    std::list<IdleSocket>* idle_sockets = group->mutable_idle_sockets();

    if (!idle_sockets->empty()) {
      delete idle_sockets->front().socket;
      idle_sockets->pop_front();
      DecrementIdleCount();
      if (group->IsEmpty())
        RemoveGroup(i);

      return true;
    }
  }

  return false;
}

bool ClientSocketPoolBaseHelper::CloseOneIdleConnectionInHigherLayeredPool() {
  // This pool doesn't have any idle sockets. It's possible that a pool at a
  // higher layer is holding one of this sockets active, but it's actually idle.
  // Query the higher layers.
  for (auto it = higher_pools_.begin(); it != higher_pools_.end(); ++it) {
    if ((*it)->CloseOneIdleConnection())
      return true;
  }
  return false;
}

void ClientSocketPoolBaseHelper::OnConnectJobComplete(Group* group,
                                                      int result,
                                                      ConnectJob* job) {
  DCHECK_NE(ERR_IO_PENDING, result);
  DCHECK(group_map_.find(group->group_name()) != group_map_.end());
  DCHECK_EQ(group, group_map_[group->group_name()]);

  std::unique_ptr<StreamSocket> socket = job->PassSocket();

  // Copies of these are needed because |job| may be deleted before they are
  // accessed.
  NetLogWithSource job_log = job->net_log();
  LoadTimingInfo::ConnectTiming connect_timing = job->connect_timing();

  // Check if the ConnectJob is already bound to a Request. If so, complete the
  // request.
  //
  // TODO(mmenke) this logic resembles the case where the job is assigned to a
  // request below. Look into merging the logic.
  int pending_result;
  std::unique_ptr<Request> request =
      group->FindAndRemoveBoundRequestForConnectJob(job, &pending_result);
  if (request) {
    --connecting_socket_count_;
    bool handed_out_socket = false;
    if (pending_result != OK) {
      result = pending_result;
    } else {
      if (socket) {
        handed_out_socket = true;
        HandOutSocket(std::move(socket), ClientSocketHandle::UNUSED,
                      connect_timing, request->handle(), base::TimeDelta(),
                      group, request->net_log());
      }
    }
    request->net_log().EndEventWithNetErrorCode(NetLogEventType::SOCKET_POOL,
                                                result);
    InvokeUserCallbackLater(request->handle(), request->release_callback(),
                            result, request->socket_tag());
    if (!handed_out_socket) {
      OnAvailableSocketSlot(group->group_name(), group);
      CheckForStalledSocketGroups();
    }
    return;
  }

  // RemoveConnectJob(job, _) must be called by all branches below;
  // otherwise, |job| will be leaked.

  if (result == OK) {
    DCHECK(socket.get());
    request = group->PopNextUnboundRequest();
    RemoveConnectJob(job, group);
    if (request) {
      LogBoundConnectJobToRequest(job_log.source(), *request);
      HandOutSocket(std::move(socket), ClientSocketHandle::UNUSED,
                    connect_timing, request->handle(), base::TimeDelta(), group,
                    request->net_log());
      request->net_log().EndEvent(NetLogEventType::SOCKET_POOL);
      InvokeUserCallbackLater(request->handle(), request->release_callback(),
                              result, request->socket_tag());
    } else {
      AddIdleSocket(std::move(socket), group);
      OnAvailableSocketSlot(group->group_name(), group);
      CheckForStalledSocketGroups();
    }
  } else {
    // If we got a socket, it must contain error information so pass that
    // up so that the caller can retrieve it.
    bool handed_out_socket = false;
    std::unique_ptr<Request> request = group->PopNextUnboundRequest();
    if (request) {
      LogBoundConnectJobToRequest(job_log.source(), *request);
      job->GetAdditionalErrorState(request->handle());
      RemoveConnectJob(job, group);
      if (socket.get()) {
        handed_out_socket = true;
        HandOutSocket(std::move(socket), ClientSocketHandle::UNUSED,
                      connect_timing, request->handle(), base::TimeDelta(),
                      group, request->net_log());
      }
      request->net_log().EndEventWithNetErrorCode(NetLogEventType::SOCKET_POOL,
                                                  result);
      InvokeUserCallbackLater(request->handle(), request->release_callback(),
                              result, request->socket_tag());
    } else {
      RemoveConnectJob(job, group);
    }
    if (!handed_out_socket) {
      OnAvailableSocketSlot(group->group_name(), group);
      CheckForStalledSocketGroups();
    }
  }
}

void ClientSocketPoolBaseHelper::OnNeedsProxyAuth(
    Group* group,
    const HttpResponseInfo& response,
    HttpAuthController* auth_controller,
    base::OnceClosure restart_with_auth_callback,
    ConnectJob* job) {
  DCHECK(group_map_.find(group->group_name()) != group_map_.end());
  DCHECK_EQ(group, group_map_[group->group_name()]);

  const Request* request = group->BindRequestToConnectJob(job);
  // If can't bind the ConnectJob to a request, treat this as a ConnectJob
  // failure.
  if (!request) {
    OnConnectJobComplete(group, ERR_PROXY_AUTH_REQUESTED, job);
    return;
  }

  request->proxy_auth_callback().Run(response, auth_controller,
                                     std::move(restart_with_auth_callback));
}

void ClientSocketPoolBaseHelper::InvokeUserCallbackLater(
    ClientSocketHandle* handle,
    CompletionOnceCallback callback,
    int rv,
    const SocketTag& socket_tag) {
  CHECK(!base::ContainsKey(pending_callback_map_, handle));
  pending_callback_map_[handle] = CallbackResultPair(std::move(callback), rv);
  if (rv == OK) {
    handle->socket()->ApplySocketTag(socket_tag);
  }
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(&ClientSocketPoolBaseHelper::InvokeUserCallback,
                                weak_factory_.GetWeakPtr(), handle));
}

void ClientSocketPoolBaseHelper::InvokeUserCallback(
    ClientSocketHandle* handle) {
  auto it = pending_callback_map_.find(handle);

  // Exit if the request has already been cancelled.
  if (it == pending_callback_map_.end())
    return;

  CHECK(!handle->is_initialized());
  CompletionOnceCallback callback = std::move(it->second.callback);
  int result = it->second.result;
  pending_callback_map_.erase(it);
  std::move(callback).Run(result);
}

void ClientSocketPoolBaseHelper::TryToCloseSocketsInLayeredPools() {
  while (IsStalled()) {
    // Closing a socket will result in calling back into |this| to use the freed
    // socket slot, so nothing else is needed.
    if (!CloseOneIdleConnectionInHigherLayeredPool())
      return;
  }
}

ClientSocketPoolBaseHelper::Group::Group(
    const std::string& group_name,
    ClientSocketPoolBaseHelper* client_socket_pool_base_helper)
    : group_name_(group_name),
      client_socket_pool_base_helper_(client_socket_pool_base_helper),
      never_assigned_job_count_(0),
      unbound_requests_(NUM_PRIORITIES),
      active_socket_count_(0) {}

ClientSocketPoolBaseHelper::Group::~Group() {
  DCHECK_EQ(0u, never_assigned_job_count());
  DCHECK_EQ(0u, unassigned_job_count());
  DCHECK(unbound_requests_.empty());
  DCHECK(jobs_.empty());
  DCHECK(bound_requests_.empty());
}

void ClientSocketPoolBaseHelper::Group::OnConnectJobComplete(int result,
                                                             ConnectJob* job) {
  DCHECK_NE(ERR_IO_PENDING, result);
  client_socket_pool_base_helper_->OnConnectJobComplete(this, result, job);
}

void ClientSocketPoolBaseHelper::Group::OnNeedsProxyAuth(
    const HttpResponseInfo& response,
    HttpAuthController* auth_controller,
    base::OnceClosure restart_with_auth_callback,
    ConnectJob* job) {
  client_socket_pool_base_helper_->OnNeedsProxyAuth(
      this, response, auth_controller, std::move(restart_with_auth_callback),
      job);
}

void ClientSocketPoolBaseHelper::Group::StartBackupJobTimer(
    const std::string& group_name) {
  // Only allow one timer to run at a time.
  if (BackupJobTimerIsRunning())
    return;

  // Unretained here is okay because |backup_job_timer_| is
  // automatically cancelled when it's destroyed.
  backup_job_timer_.Start(
      FROM_HERE, client_socket_pool_base_helper_->ConnectRetryInterval(),
      base::Bind(&Group::OnBackupJobTimerFired, base::Unretained(this),
                 group_name));
}

bool ClientSocketPoolBaseHelper::Group::BackupJobTimerIsRunning() const {
  return backup_job_timer_.IsRunning();
}

bool ClientSocketPoolBaseHelper::Group::TryToUseNeverAssignedConnectJob() {
  SanityCheck();

  if (never_assigned_job_count_ == 0)
    return false;
  --never_assigned_job_count_;
  return true;
}

void ClientSocketPoolBaseHelper::Group::AddJob(std::unique_ptr<ConnectJob> job,
                                               bool is_preconnect) {
  SanityCheck();

  if (is_preconnect)
    ++never_assigned_job_count_;
  jobs_.push_back(std::move(job));
  TryToAssignUnassignedJob(jobs_.back().get());

  SanityCheck();
}

std::unique_ptr<ConnectJob> ClientSocketPoolBaseHelper::Group::RemoveUnboundJob(
    ConnectJob* job) {
  SanityCheck();

  // Check that |job| is in the list.
  auto it = std::find_if(jobs_.begin(), jobs_.end(),
                         [job](const std::unique_ptr<ConnectJob>& ptr) {
                           return ptr.get() == job;
                         });
  DCHECK(it != jobs_.end());

  // Check if |job| is in the unassigned jobs list. If so, remove it.
  auto it2 = std::find(unassigned_jobs_.begin(), unassigned_jobs_.end(), job);
  if (it2 != unassigned_jobs_.end()) {
    unassigned_jobs_.erase(it2);
  } else {
    // Otherwise, |job| must be assigned to some Request. Unassign it, then
    // try to replace it with another job if possible (either by taking an
    // unassigned job or stealing from another request, if any requests after it
    // have a job).
    RequestQueue::Pointer request_with_job = FindUnboundRequestWithJob(job);
    DCHECK(!request_with_job.is_null());
    request_with_job.value()->ReleaseJob();
    TryToAssignJobToRequest(request_with_job);
  }
  std::unique_ptr<ConnectJob> owned_job = std::move(*it);
  jobs_.erase(it);

  size_t job_count = jobs_.size();
  if (job_count < never_assigned_job_count_)
    never_assigned_job_count_ = job_count;

  // If we've got no more jobs for this group, then we no longer need a
  // backup job either.
  if (jobs_.empty()) {
    DCHECK(unassigned_jobs_.empty());
    backup_job_timer_.Stop();
  }

  SanityCheck();
  return owned_job;
}

void ClientSocketPoolBaseHelper::Group::OnBackupJobTimerFired(
    std::string group_name) {
  // If there are no more jobs pending, there is no work to do.
  // If we've done our cleanups correctly, this should not happen.
  if (jobs_.empty()) {
    NOTREACHED();
    return;
  }

  // If the old job has already established a connection, don't start a backup
  // job. Backup jobs are only for issues establishing the initial TCP
  // connection - the timeout they used is tuned for that, and tests expect that
  // behavior.
  //
  // TODO(https://crbug.com/929814): Replace both this and the
  // LOAD_STATE_RESOLVING_HOST check with a callback. Use the
  // LOAD_STATE_RESOLVING_HOST callback to start the timer (And invoke the
  // OnHostResolved callback of any pending requests), and the
  // HasEstablishedConnection() callback to stop the timer. That should result
  // in a more robust, testable API.
  if ((*jobs_.begin())->HasEstablishedConnection())
    return;

  // If our old job is waiting on DNS, or if we can't create any sockets
  // right now due to limits, just reset the timer.
  if (client_socket_pool_base_helper_->ReachedMaxSocketsLimit() ||
      !HasAvailableSocketSlot(
          client_socket_pool_base_helper_->max_sockets_per_group_) ||
      (*jobs_.begin())->GetLoadState() == LOAD_STATE_RESOLVING_HOST) {
    StartBackupJobTimer(group_name);
    return;
  }

  if (unbound_requests_.empty())
    return;

  std::unique_ptr<ConnectJob> owned_backup_job =
      client_socket_pool_base_helper_->connect_job_factory_->NewConnectJob(
          *unbound_requests_.FirstMax().value(), this);
  owned_backup_job->net_log().AddEvent(
      NetLogEventType::SOCKET_POOL_CONNECT_JOB_CREATED,
      base::BindRepeating(&NetLogCreateConnectJobCallback,
                          true /* backup_job */, &group_name_));
  ConnectJob* backup_job = owned_backup_job.get();
  AddJob(std::move(owned_backup_job), false);
  client_socket_pool_base_helper_->connecting_socket_count_++;
  int rv = backup_job->Connect();
  if (rv != ERR_IO_PENDING) {
    client_socket_pool_base_helper_->OnConnectJobComplete(this, rv, backup_job);
  }
}

void ClientSocketPoolBaseHelper::Group::SanityCheck() const {
#if DCHECK_IS_ON()
  DCHECK_LE(never_assigned_job_count(), jobs_.size());
  DCHECK_LE(unassigned_job_count(), jobs_.size());

  // Check that |unassigned_jobs_| is empty iff there are at least as many
  // requests as jobs.
  DCHECK_EQ(unassigned_jobs_.empty(), jobs_.size() <= unbound_requests_.size());

  size_t num_assigned_jobs = jobs_.size() - unassigned_jobs_.size();

  RequestQueue::Pointer pointer = unbound_requests_.FirstMax();
  for (size_t i = 0; i < unbound_requests_.size();
       ++i, pointer = unbound_requests_.GetNextTowardsLastMin(pointer)) {
    DCHECK(!pointer.is_null());
    DCHECK(pointer.value());
    // Check that the first |num_assigned_jobs| requests have valid job
    // assignments.
    if (i < num_assigned_jobs) {
      // The request has a job.
      ConnectJob* job = pointer.value()->job();
      DCHECK(job);
      // The request's job is not in |unassigned_jobs_|
      DCHECK(std::find(unassigned_jobs_.begin(), unassigned_jobs_.end(), job) ==
             unassigned_jobs_.end());
      // The request's job is in |jobs_|
      DCHECK(std::find_if(jobs_.begin(), jobs_.end(),
                          [job](const std::unique_ptr<ConnectJob>& ptr) {
                            return ptr.get() == job;
                          }) != jobs_.end());
      // The same job is not assigned to any other request with a job.
      RequestQueue::Pointer pointer2 =
          unbound_requests_.GetNextTowardsLastMin(pointer);
      for (size_t j = i + 1; j < num_assigned_jobs;
           ++j, pointer2 = unbound_requests_.GetNextTowardsLastMin(pointer2)) {
        DCHECK(!pointer2.is_null());
        ConnectJob* job2 = pointer2.value()->job();
        DCHECK(job2);
        DCHECK_NE(job, job2);
      }
      DCHECK_EQ(pointer.value()->priority(), job->priority());
    } else {
      // Check that any subsequent requests do not have a job.
      DCHECK(!pointer.value()->job());
    }
  }

  for (auto it = unassigned_jobs_.begin(); it != unassigned_jobs_.end(); ++it) {
    // Check that all unassigned jobs are in |jobs_|
    ConnectJob* job = *it;
    DCHECK(std::find_if(jobs_.begin(), jobs_.end(),
                        [job](const std::unique_ptr<ConnectJob>& ptr) {
                          return ptr.get() == job;
                        }) != jobs_.end());
    // Check that there are no duplicated entries in |unassigned_jobs_|
    for (auto it2 = std::next(it); it2 != unassigned_jobs_.end(); ++it2) {
      DCHECK_NE(job, *it2);
    }

    // Check that no |unassigned_jobs_| are in |bound_requests_|.
    DCHECK(std::find_if(bound_requests_.begin(), bound_requests_.end(),
                        [job](const BoundRequest& bound_request) {
                          return bound_request.connect_job.get() == job;
                        }) == bound_requests_.end());
  }
#endif
}

void ClientSocketPoolBaseHelper::Group::RemoveAllUnboundJobs() {
  SanityCheck();

  // Remove jobs from any requests that have them.
  if (!unbound_requests_.empty()) {
    for (RequestQueue::Pointer pointer = unbound_requests_.FirstMax();
         !pointer.is_null() && pointer.value()->job();
         pointer = unbound_requests_.GetNextTowardsLastMin(pointer)) {
      pointer.value()->ReleaseJob();
    }
  }
  unassigned_jobs_.clear();
  never_assigned_job_count_ = 0;
  // Delete active jobs.
  jobs_.clear();
  // Stop backup job timer.
  backup_job_timer_.Stop();

  SanityCheck();
}

size_t ClientSocketPoolBaseHelper::Group::ConnectJobCount() const {
  return bound_requests_.size() + jobs_.size();
}

ConnectJob* ClientSocketPoolBaseHelper::Group::GetConnectJobForHandle(
    const ClientSocketHandle* handle) const {
  // Search through bound requests for |handle|.
  for (const auto& bound_pair : bound_requests_) {
    if (handle == bound_pair.request->handle())
      return bound_pair.connect_job.get();
  }

  // Search through the unbound requests that have corresponding jobs for a
  // request with |handle|.
  for (RequestQueue::Pointer pointer = unbound_requests_.FirstMax();
       !pointer.is_null() && pointer.value()->job();
       pointer = unbound_requests_.GetNextTowardsLastMin(pointer)) {
    if (pointer.value()->handle() == handle)
      return pointer.value()->job();
  }

  return nullptr;
}

void ClientSocketPoolBaseHelper::Group::InsertUnboundRequest(
    std::unique_ptr<Request> request) {
  SanityCheck();

  // Should not have a job because it is not already in |unbound_requests_|
  DCHECK(!request->job());
  // This value must be cached before we release |request|.
  RequestPriority priority = request->priority();

  RequestQueue::Pointer new_position;
  if (request->respect_limits() == ClientSocketPool::RespectLimits::DISABLED) {
    // Put requests with RespectLimits::DISABLED (which should have
    // priority == MAXIMUM_PRIORITY) ahead of other requests with
    // MAXIMUM_PRIORITY.
    DCHECK_EQ(priority, MAXIMUM_PRIORITY);
    new_position =
        unbound_requests_.InsertAtFront(std::move(request), priority);
  } else {
    new_position = unbound_requests_.Insert(std::move(request), priority);
  }
  DCHECK(!unbound_requests_.empty());

  TryToAssignJobToRequest(new_position);

  SanityCheck();
}

const ClientSocketPoolBaseHelper::Request*
ClientSocketPoolBaseHelper::Group::GetNextUnboundRequest() const {
  return unbound_requests_.empty() ? nullptr
                                   : unbound_requests_.FirstMax().value().get();
}

std::unique_ptr<ClientSocketPoolBaseHelper::Request>
ClientSocketPoolBaseHelper::Group::PopNextUnboundRequest() {
  if (unbound_requests_.empty())
    return std::unique_ptr<ClientSocketPoolBaseHelper::Request>();
  return RemoveUnboundRequest(unbound_requests_.FirstMax());
}

std::unique_ptr<ClientSocketPoolBaseHelper::Request>
ClientSocketPoolBaseHelper::Group::FindAndRemoveUnboundRequest(
    ClientSocketHandle* handle) {
  for (RequestQueue::Pointer pointer = unbound_requests_.FirstMax();
       !pointer.is_null();
       pointer = unbound_requests_.GetNextTowardsLastMin(pointer)) {
    if (pointer.value()->handle() == handle) {
      DCHECK_EQ(static_cast<RequestPriority>(pointer.priority()),
                pointer.value()->priority());
      std::unique_ptr<Request> request = RemoveUnboundRequest(pointer);
      return request;
    }
  }
  return std::unique_ptr<ClientSocketPoolBaseHelper::Request>();
}

void ClientSocketPoolBaseHelper::Group::SetPendingErrorForAllBoundRequests(
    int pending_error) {
  for (auto bound_pair = bound_requests_.begin();
       bound_pair != bound_requests_.end(); ++bound_pair) {
    // Earlier errors take precedence.
    if (bound_pair->pending_error == OK)
      bound_pair->pending_error = pending_error;
  }
}

const ClientSocketPoolBaseHelper::Request*
ClientSocketPoolBaseHelper::Group::BindRequestToConnectJob(
    ConnectJob* connect_job) {
  // Check if |job| is already bound to a Request.
  for (const auto& bound_pair : bound_requests_) {
    if (bound_pair.connect_job.get() == connect_job)
      return bound_pair.request.get();
  }

  // If not, try to bind it to a Request.
  const Request* request = GetNextUnboundRequest();
  // If there are no pending requests, or the highest priority request has no
  // callback to handle auth challenges, return nullptr.
  if (!request || request->proxy_auth_callback().is_null())
    return nullptr;

  // Otherwise, bind the ConnectJob to the Request.
  std::unique_ptr<Request> owned_request = PopNextUnboundRequest();
  DCHECK_EQ(owned_request.get(), request);
  std::unique_ptr<ConnectJob> owned_connect_job = RemoveUnboundJob(connect_job);
  LogBoundConnectJobToRequest(owned_connect_job->net_log().source(), *request);
  bound_requests_.emplace_back(
      BoundRequest(std::move(owned_connect_job), std::move(owned_request)));
  return request;
}

std::unique_ptr<ClientSocketPoolBaseHelper::Request>
ClientSocketPoolBaseHelper::Group::FindAndRemoveBoundRequestForConnectJob(
    ConnectJob* connect_job,
    int* pending_error) {
  for (auto bound_pair = bound_requests_.begin();
       bound_pair != bound_requests_.end(); ++bound_pair) {
    if (bound_pair->connect_job.get() != connect_job)
      continue;
    std::unique_ptr<Request> request = std::move(bound_pair->request);
    *pending_error = bound_pair->pending_error;
    bound_requests_.erase(bound_pair);
    return request;
  }
  return nullptr;
}

std::unique_ptr<ClientSocketPoolBaseHelper::Request>
ClientSocketPoolBaseHelper::Group::FindAndRemoveBoundRequest(
    ClientSocketHandle* client_socket_handle) {
  for (auto bound_pair = bound_requests_.begin();
       bound_pair != bound_requests_.end(); ++bound_pair) {
    if (bound_pair->request->handle() != client_socket_handle)
      continue;
    std::unique_ptr<Request> request = std::move(bound_pair->request);
    bound_requests_.erase(bound_pair);
    return request;
  }
  return nullptr;
}

void ClientSocketPoolBaseHelper::Group::SetPriority(ClientSocketHandle* handle,
                                                    RequestPriority priority) {
  for (RequestQueue::Pointer pointer = unbound_requests_.FirstMax();
       !pointer.is_null();
       pointer = unbound_requests_.GetNextTowardsLastMin(pointer)) {
    if (pointer.value()->handle() == handle) {
      if (pointer.value()->priority() == priority)
        return;

      std::unique_ptr<Request> request = RemoveUnboundRequest(pointer);

      // Requests that ignore limits much be created and remain at the highest
      // priority, and should not be reprioritized.
      DCHECK_EQ(request->respect_limits(),
                ClientSocketPool::RespectLimits::ENABLED);

      request->set_priority(priority);
      InsertUnboundRequest(std::move(request));
      return;
    }
  }

  // This function must be called with a valid ClientSocketHandle.
  NOTREACHED();
}

bool ClientSocketPoolBaseHelper::Group::RequestWithHandleHasJobForTesting(
    const ClientSocketHandle* handle) const {
  SanityCheck();
  if (GetConnectJobForHandle(handle))
    return true;

  // There's no corresponding ConnectJob. Verify that the handle is at least
  // owned by a request.
  RequestQueue::Pointer pointer = unbound_requests_.FirstMax();
  for (size_t i = 0; i < unbound_requests_.size(); ++i) {
    if (pointer.value()->handle() == handle)
      return false;
    pointer = unbound_requests_.GetNextTowardsLastMin(pointer);
  }
  NOTREACHED();
  return false;
}

ClientSocketPoolBaseHelper::Group::BoundRequest::BoundRequest()
    : pending_error(OK) {}

ClientSocketPoolBaseHelper::Group::BoundRequest::BoundRequest(
    std::unique_ptr<ConnectJob> connect_job,
    std::unique_ptr<Request> request)
    : connect_job(std::move(connect_job)),
      request(std::move(request)),
      pending_error(OK) {}

ClientSocketPoolBaseHelper::Group::BoundRequest::BoundRequest(
    BoundRequest&& other) = default;

ClientSocketPoolBaseHelper::Group::BoundRequest&
ClientSocketPoolBaseHelper::Group::BoundRequest::operator=(
    BoundRequest&& other) = default;

ClientSocketPoolBaseHelper::Group::BoundRequest::~BoundRequest() = default;

std::unique_ptr<ClientSocketPoolBaseHelper::Request>
ClientSocketPoolBaseHelper::Group::RemoveUnboundRequest(
    const RequestQueue::Pointer& pointer) {
  SanityCheck();

  // TODO(eroman): Temporary for debugging http://crbug.com/467797.
  CHECK(!pointer.is_null());
  std::unique_ptr<Request> request = unbound_requests_.Erase(pointer);
  if (request->job()) {
    TryToAssignUnassignedJob(request->ReleaseJob());
  }
  // If there are no more unbound requests, kill the backup timer.
  if (unbound_requests_.empty())
    backup_job_timer_.Stop();

  request->CrashIfInvalid();
  SanityCheck();
  return request;
}

ClientSocketPoolBaseHelper::RequestQueue::Pointer
ClientSocketPoolBaseHelper::Group::FindUnboundRequestWithJob(
    const ConnectJob* job) const {
  SanityCheck();

  for (RequestQueue::Pointer pointer = unbound_requests_.FirstMax();
       !pointer.is_null() && pointer.value()->job();
       pointer = unbound_requests_.GetNextTowardsLastMin(pointer)) {
    if (pointer.value()->job() == job)
      return pointer;
  }
  // If a request with the job was not found, it must be in |unassigned_jobs_|.
  DCHECK(std::find(unassigned_jobs_.begin(), unassigned_jobs_.end(), job) !=
         unassigned_jobs_.end());
  return RequestQueue::Pointer();
}

ClientSocketPoolBaseHelper::RequestQueue::Pointer
ClientSocketPoolBaseHelper::Group::GetFirstRequestWithoutJob() const {
  RequestQueue::Pointer pointer = unbound_requests_.FirstMax();
  size_t i = 0;
  for (; !pointer.is_null() && pointer.value()->job();
       pointer = unbound_requests_.GetNextTowardsLastMin(pointer)) {
    ++i;
  }
  DCHECK_EQ(i, jobs_.size() - unassigned_jobs_.size());
  DCHECK(pointer.is_null() || !pointer.value()->job());
  return pointer;
}

void ClientSocketPoolBaseHelper::Group::TryToAssignUnassignedJob(
    ConnectJob* job) {
  unassigned_jobs_.push_back(job);
  RequestQueue::Pointer first_request_without_job = GetFirstRequestWithoutJob();
  if (!first_request_without_job.is_null()) {
    first_request_without_job.value()->AssignJob(unassigned_jobs_.back());
    unassigned_jobs_.pop_back();
  }
}

void ClientSocketPoolBaseHelper::Group::TryToAssignJobToRequest(
    ClientSocketPoolBaseHelper::RequestQueue::Pointer request_pointer) {
  DCHECK(!request_pointer.value()->job());
  if (!unassigned_jobs_.empty()) {
    request_pointer.value()->AssignJob(unassigned_jobs_.front());
    unassigned_jobs_.pop_front();
    return;
  }

  // If the next request in the queue does not have a job, then there are no
  // requests with a job after |request_pointer| from which we can steal.
  RequestQueue::Pointer next_request =
      unbound_requests_.GetNextTowardsLastMin(request_pointer);
  if (next_request.is_null() || !next_request.value()->job())
    return;

  // Walk down the queue to find the last request with a job.
  RequestQueue::Pointer cur = next_request;
  RequestQueue::Pointer next = unbound_requests_.GetNextTowardsLastMin(cur);
  while (!next.is_null() && next.value()->job()) {
    cur = next;
    next = unbound_requests_.GetNextTowardsLastMin(next);
  }
  // Steal the job from the last request with a job.
  TransferJobBetweenRequests(cur.value().get(), request_pointer.value().get());
}

void ClientSocketPoolBaseHelper::Group::TransferJobBetweenRequests(
    ClientSocketPoolBaseHelper::Request* source,
    ClientSocketPoolBaseHelper::Request* dest) {
  DCHECK(!dest->job());
  DCHECK(source->job());
  dest->AssignJob(source->ReleaseJob());
}

}  // namespace internal

}  // namespace net
