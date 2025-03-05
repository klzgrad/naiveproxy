// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_HTTP_STREAM_POOL_GROUP_H_
#define NET_HTTP_HTTP_STREAM_POOL_GROUP_H_

#include <list>
#include <memory>
#include <string_view>
#include <vector>

#include "base/containers/unique_ptr_adapters.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "net/base/load_timing_info.h"
#include "net/base/net_export.h"
#include "net/http/http_stream_key.h"
#include "net/http/http_stream_pool.h"
#include "net/http/http_stream_pool_job.h"
#include "net/http/http_stream_request.h"
#include "net/quic/quic_session_alias_key.h"
#include "net/socket/stream_socket_close_reason.h"
#include "net/socket/stream_socket_handle.h"
#include "net/spdy/spdy_session_key.h"
#include "net/third_party/quiche/src/quiche/quic/core/quic_versions.h"
#include "url/scheme_host_port.h"

namespace net {

class HttpNetworkSession;
class HttpStream;
class HttpStreamPoolHandle;
class StreamSocket;

// Maintains active/idle text-based HTTP streams. If new streams are needed,
// creates an HttpStreamPool::AttemptManager and starts connection attempts for
// streams.
//
// Keeps incoming jobs (called paused jobs) when the current AttemptManager is
// "failing", i.e., AttemptManager is notifying the failure to associated jobs
// and waiting for completions of these jobs. Once the failing AttemptManager
// completes, this starts a new AttemptManager and pass paused jobs to the new
// AttemptManager.
//
// Owned by an HttpStreamPool, keyed by HttpStreamKey. Destroyed when all
// streams associated with this group are completed.
class HttpStreamPool::Group {
 public:
  // The same timeout as ClientSocketPool::used_idle_socket_timeout().
  static constexpr base::TimeDelta kUsedIdleStreamSocketTimeout =
      base::Seconds(300);

  // The same timeout as
  // ClientSocketPoolManager::unused_idle_socket_timeout().
  static constexpr base::TimeDelta kUnusedIdleStreamSocketTimeout =
      base::Seconds(60);

  Group(HttpStreamPool* pool,
        HttpStreamKey stream_key,
        std::optional<QuicSessionAliasKey> quic_session_alias_key);

  Group(const Group&) = delete;
  Group& operator=(const Group&) = delete;

  ~Group();

  const HttpStreamKey& stream_key() const { return stream_key_; }

  const SpdySessionKey& spdy_session_key() const { return spdy_session_key_; }

  const QuicSessionAliasKey& quic_session_alias_key() const {
    return quic_session_alias_key_;
  }

  HttpStreamPool* pool() { return pool_; }
  const HttpStreamPool* pool() const { return pool_; }

  HttpNetworkSession* http_network_session() const {
    return pool_->http_network_session();
  }

  AttemptManager* attempt_manager() const { return attempt_manager_.get(); }

  const NetLogWithSource& net_log() { return net_log_; }

  bool force_quic() const { return force_quic_; }

  // Creates a Job to attempt connection(s). We have separate methods for
  // creating and starting a Job to ensure that the owner of the Job can
  // properly manage the lifetime of the Job, even when StartJob() synchronously
  // calls one of the delegate's methods.
  std::unique_ptr<Job> CreateJob(Job::Delegate* delegate,
                                 quic::ParsedQuicVersion quic_version,
                                 NextProto expected_protocol,
                                 const NetLogWithSource& request_net_log);

  // Called by `job` to see whether `job` can start.
  bool CanStartJob(Job* job);

  // Called when `job` is going to be destroyed.
  void OnJobComplete(Job* job);

  // Creates idle streams or sessions for `num_streams` be opened.
  // Note that this method finishes synchronously, or `callback` is called, once
  // `this` has enough streams/sessions for `num_streams` be opened. This means
  // that when there are two preconnect requests with `num_streams = 1`, all
  // callbacks are invoked when one stream/session is established (not two).
  int Preconnect(size_t num_streams,
                 quic::ParsedQuicVersion quic_version,
                 const NetLogWithSource& job_controller_net_log,
                 CompletionOnceCallback callback);

  // Creates an HttpStreamPoolHandle from `socket`. Call sites must ensure that
  // the number of active streams do not exceed the global/per-group limits.
  std::unique_ptr<HttpStreamPoolHandle> CreateHandle(
      std::unique_ptr<StreamSocket> socket,
      StreamSocketHandle::SocketReuseType reuse_type,
      LoadTimingInfo::ConnectTiming connect_timing);

  // Creates a text-based HttpStream from `socket`. Call sites must ensure that
  // the number of active streams do not exceed the global/per-group limits.
  // `socket` must not be negotiated to use HTTP/2.
  std::unique_ptr<HttpStream> CreateTextBasedStream(
      std::unique_ptr<StreamSocket> socket,
      StreamSocketHandle::SocketReuseType reuse_type,
      LoadTimingInfo::ConnectTiming connect_timing);

  // Releases a StreamSocket that was used to create a text-based HttpStream.
  void ReleaseStreamSocket(std::unique_ptr<StreamSocket> socket,
                           int64_t generation);

  // Adds `socket` as an idle StreamSocket for text-based HttpStream. Call sites
  // must ensure that the number of idle streams do not exceed the global/per-
  // group limits.
  // `socket` must not be negotiated to use HTTP/2.
  void AddIdleStreamSocket(std::unique_ptr<StreamSocket> socket);

  // Retrieves an existing idle StreamSocket. Returns nullptr when there is no
  // idle stream.
  std::unique_ptr<StreamSocket> GetIdleStreamSocket();

  // Tries to process a pending request.
  void ProcessPendingRequest();

  // Closes one idle stream socket. Returns true if it closed a stream. Called
  // when the pool reached the stream count limit.
  bool CloseOneIdleStreamSocket();

  // Returns the number of handed out streams.
  size_t HandedOutStreamSocketCount() const { return handed_out_stream_count_; }

  // Returns the number of idle streams.
  size_t IdleStreamSocketCount() const { return idle_stream_sockets_.size(); }

  // Returns the number of connecting streams.
  size_t ConnectingStreamSocketCount() const;

  // Returns the number of active streams.
  size_t ActiveStreamSocketCount() const;

  // True when the number of active streams reached the group limit.
  bool ReachedMaxStreamLimit() const;

  // Returns the number of paused jobs. See the comment of `paused_jobs_`.
  size_t PausedJobCount() const { return paused_jobs_.size(); }

  // Returns the highest pending request priority if the group is stalled due to
  // the per-pool limit, not the per-group limit.
  std::optional<RequestPriority> GetPriorityIfStalledByPoolLimit() const;

  // Closes all streams in this group and cancels all pending requests.
  void FlushWithError(int error,
                      StreamSocketCloseReason attempt_cancel_reason,
                      std::string_view net_log_close_reason_utf8);

  // Increments the generation of this group. Closes idle streams. Streams
  // handed out before this increment won't be reused. Cancels in-flight
  // connection attempts.
  void Refresh(std::string_view net_log_close_reason_utf8,
               StreamSocketCloseReason cancel_reason);

  void CloseIdleStreams(std::string_view net_log_close_reason_utf8);

  // Cancels all on-going jobs.
  void CancelJobs(int error);

  // Called when the server required HTTP/1.1. Clears the current SPDY session
  // if exists.
  void OnRequiredHttp11();

  // Called when the attempt manager has completed.
  void OnAttemptManagerComplete();

  // Retrieves information on the current state of the group as a base::Value.
  base::Value::Dict GetInfoAsValue() const;

  // Returns true when `this` can be deleted.
  // TODO(crbug.com/346835898): This is public for consistency checks. Make this
  // private once we stabilize the implementation.
  bool CanComplete() const;

  void CleanupTimedoutIdleStreamSocketsForTesting();

  AttemptManager* GetAttemptManagerForTesting() const {
    return attempt_manager_.get();
  }

 private:
  FRIEND_TEST_ALL_PREFIXES(HttpStreamPoolGroupTest, ComparePausedJobSet);

  struct IdleStreamSocket {
    IdleStreamSocket(std::unique_ptr<StreamSocket> stream_socket,
                     base::TimeTicks start_time);
    ~IdleStreamSocket();

    IdleStreamSocket(const IdleStreamSocket&) = delete;
    IdleStreamSocket& operator=(const IdleStreamSocket&) = delete;

    std::unique_ptr<StreamSocket> stream_socket;
    base::TimeTicks time_became_idle;
  };

  enum class CleanupMode {
    // Clean up only timed out idle streams.
    kTimeoutOnly,
    // Clean up all idle streams.
    kForce,
  };

  // Compares jobs based on their creation time. Used for `paused_jobs_`.
  struct PausedJobComparator {
    bool operator()(Job* a, Job* b) const;
  };

  using PausedJobSet = std::set<raw_ptr<Job>, PausedJobComparator>;

  static base::expected<void, std::string_view> IsIdleStreamSocketUsable(
      const IdleStreamSocket& idle);

  bool IsFailing() const;

  // Resumes a paused job. Schedules another task if more paused jobs exist.
  void ResumePausedJob();

  // Cancels a paused job. Schedules another task if more paused jobs exist.
  void CancelPausedJob(int error);

  // Extracts a paused job from `paused_jobs_`. The ownership of the raw_ptr of
  // the job is moved to `notified_paused_jobs_`.
  HttpStreamPool::Job* ExtractOnePausedJob();

  void CleanupIdleStreamSockets(CleanupMode mode,
                                std::string_view net_log_close_reason_utf8);

  void EnsureAttemptManager();

  void MaybeComplete();

  // Posts a task to call MaybeComplete() later.
  void MaybeCompleteLater();

  const raw_ptr<HttpStreamPool> pool_;
  const HttpStreamKey stream_key_;
  const SpdySessionKey spdy_session_key_;
  const QuicSessionAliasKey quic_session_alias_key_;
  const NetLogWithSource net_log_;
  const bool force_quic_;

  size_t handed_out_stream_count_ = 0;
  int64_t generation_ = 0;
  std::list<IdleStreamSocket> idle_stream_sockets_;

  std::unique_ptr<AttemptManager> attempt_manager_;

  // Keeps jobs that are created while the current AttemptManager is failing.
  // Once the AttemptManager completes notifying the failure to its jobs, we
  // create a new AttemptManager and pass these jobs to the new AttemptManager.
  // We call these jobs "paused". Note that there are another type of jobs that
  // are called "pending". Pending jobs are associated with an AttemptManager
  // but haven't attempted connections yet.
  PausedJobSet paused_jobs_;
  // Keeps jobs that are previously paused and already notified results. We
  // need to keep them to avoid dangling pointers.
  PausedJobSet notified_paused_jobs_;

  base::WeakPtrFactory<Group> weak_ptr_factory_{this};
};

}  // namespace net

#endif  // NET_HTTP_HTTP_STREAM_POOL_GROUP_H_
