// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_HTTP_STREAM_POOL_GROUP_H_
#define NET_HTTP_HTTP_STREAM_POOL_GROUP_H_

#include <list>
#include <memory>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "base/types/expected.h"
#include "net/base/load_timing_info.h"
#include "net/base/net_export.h"
#include "net/http/http_stream_key.h"
#include "net/http/http_stream_pool.h"
#include "net/http/http_stream_request.h"
#include "net/socket/stream_socket_handle.h"
#include "net/spdy/spdy_session_key.h"

namespace net {

class HttpNetworkSession;
class HttpStream;
class HttpStreamPoolHandle;
class StreamSocket;

// Maintains active/idle text-based HTTP streams.
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
        SpdySessionKey spdy_session_key);

  Group(const Group&) = delete;
  Group& operator=(const Group&) = delete;

  ~Group();

  const HttpStreamKey& stream_key() const { return stream_key_; }

  const SpdySessionKey& spdy_session_key() const { return spdy_session_key_; }

  const QuicSessionKey& quic_session_key() const { return quic_session_key_; }

  HttpStreamPool* pool() { return pool_; }
  const HttpStreamPool* pool() const { return pool_; }

  HttpNetworkSession* http_network_session() const {
    return pool_->http_network_session();
  }

  const NetLogWithSource& net_log() { return net_log_; }

  // Creates an HttpStreamRequest. Will call delegate's methods. See the
  // comments of HttpStreamRequest::Delegate methods for details.
  // TODO(crbug.com/346835898): Support QUIC.
  std::unique_ptr<HttpStreamRequest> RequestStream(
      HttpStreamRequest::Delegate* delegate,
      RequestPriority priority,
      const std::vector<SSLConfig::CertAndStatus>& allowed_bad_certs,
      bool enable_ip_based_pooling,
      bool enable_alternative_services,
      quic::ParsedQuicVersion quic_version,
      const NetLogWithSource& net_log);

  // Creates idle streams or sessions for `num_streams` be opened.
  // Note that this method finishes synchronously, or `callback` is called, once
  // `this` has enough streams/sessions for `num_streams` be opened. This means
  // that when there are two preconnect requests with `num_streams = 1`, all
  // callbacks are invoked when one stream/session is established (not two).
  int Preconnect(size_t num_streams,
                 quic::ParsedQuicVersion quic_version,
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

  // Closes one idle stream socket. Returns true if it closed a stream.
  bool CloseOneIdleStreamSocket();

  // Returns the number of idle streams.
  size_t IdleStreamSocketCount() const { return idle_stream_sockets_.size(); }

  // Returns the number of active streams.
  size_t ActiveStreamSocketCount() const;

  bool ReachedMaxStreamLimit() const;

  // Returns the highest pending request priority if the group is stalled due to
  // the per-pool limit, not the per-group limit.
  std::optional<RequestPriority> GetPriorityIfStalledByPoolLimit() const;

  // Increments the generation of this group. Closes idle streams. Streams
  // handed out before this increment won't be reused. Cancels in-flight
  // connection attempts.
  void Refresh(std::string_view net_log_close_reason_utf8);

  void CloseIdleStreams(std::string_view net_log_close_reason_utf8);

  // Cancels all on-going requests.
  void CancelRequests(int error);

  // Called when the server required HTTP/1.1. Clears the current SPDY session
  // if exists.
  void OnRequiredHttp11();

  // Called when the in-flight job has completed.
  void OnJobComplete();

  void CleanupTimedoutIdleStreamSocketsForTesting();

  Job* GetJobForTesting() const { return in_flight_job_.get(); }

 private:
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

  static base::expected<void, std::string_view> IsIdleStreamSocketUsable(
      const IdleStreamSocket& idle);

  void CleanupIdleStreamSockets(CleanupMode mode,
                                std::string_view net_log_close_reason_utf8);

  void EnsureInFlightJob();

  void MaybeComplete();

  const raw_ptr<HttpStreamPool> pool_;
  const HttpStreamKey stream_key_;
  const SpdySessionKey spdy_session_key_;
  const QuicSessionKey quic_session_key_;
  const NetLogWithSource net_log_;

  size_t handed_out_stream_count_ = 0;
  int64_t generation_ = 0;
  std::list<IdleStreamSocket> idle_stream_sockets_;

  std::unique_ptr<Job> in_flight_job_;
};

}  // namespace net

#endif  // NET_HTTP_HTTP_STREAM_POOL_GROUP_H_