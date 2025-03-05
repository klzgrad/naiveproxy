// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_HTTP_STREAM_POOL_ATTEMPT_MANAGER_QUIC_TASK_H_
#define NET_HTTP_HTTP_STREAM_POOL_ATTEMPT_MANAGER_QUIC_TASK_H_

#include <memory>
#include <optional>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "net/base/ip_endpoint.h"
#include "net/dns/host_resolver.h"
#include "net/http/http_stream_pool.h"
#include "net/http/http_stream_pool_attempt_manager.h"
#include "net/quic/quic_session_attempt.h"
#include "net/quic/quic_session_pool.h"
#include "net/third_party/quiche/src/quiche/quic/core/quic_versions.h"

namespace net {

class HttpStreamKey;
class QuicSessionAliasKey;

// Handles QUIC session attempts for HttpStreamPool::AttemptManager. Owned by an
// AttemptManager.
class HttpStreamPool::AttemptManager::QuicTask
    : public QuicSessionAttempt::Delegate {
 public:
  // `manager` must outlive `this`.
  QuicTask(AttemptManager* manager, quic::ParsedQuicVersion quic_version);

  QuicTask(const QuicTask&) = delete;
  QuicTask& operator=(const QuicTask&) = delete;

  ~QuicTask() override;

  // Attempts QUIC session(s). Looks up available QUIC endpoints from
  // `manager_`'s service endpoints results and `quic_version_`.
  void MaybeAttempt();

  // QuicSessionAttempt::Delegate implementation.
  QuicSessionPool* GetQuicSessionPool() override;
  const QuicSessionAliasKey& GetKey() override;
  const NetLogWithSource& GetNetLog() override;

  // Retrieves information on the current state of `this` as a base::Value.
  base::Value::Dict GetInfoAsValue() const;

  // Returns the first non-pending result of a QUIC session attempt start, if
  // any. Never returns ERR_IO_PENDING.
  std::optional<int> start_result() const { return start_result_; }

  // Returns the start time of a session attempt. Maybe null when no attempt is
  // made.
  base::TimeTicks attempt_start_time() const { return attempt_start_time_; }

 private:
  const HttpStreamKey& stream_key() const;

  HostResolver::ServiceEndpointRequest* service_endpoint_request();

  QuicSessionPool* quic_session_pool();

  // Returns a QUIC endpoint to make a connection attempt. See the comments in
  // QuicSessionPool::SelectQuicVersion() for the criteria to select a QUIC
  // endpoint.
  std::optional<QuicEndpoint> GetQuicEndpointToAttempt();
  std::optional<QuicEndpoint> GetQuicEndpointFromServiceEndpoint(
      const ServiceEndpoint& service_endpoint,
      bool svcb_optional);
  std::optional<IPEndPoint> GetPreferredIPEndPoint(
      const std::vector<IPEndPoint>& ip_endpoints);

  void OnSessionAttemptComplete(int rv);

  const raw_ptr<AttemptManager> manager_;
  const quic::ParsedQuicVersion quic_version_;
  const NetLogWithSource net_log_;

  std::optional<int> start_result_;

  // TODO(crbug.com/346835898): Support multiple attempts.
  std::unique_ptr<QuicSessionAttempt> session_attempt_;
  base::TimeTicks attempt_start_time_;

  base::WeakPtrFactory<QuicTask> weak_ptr_factory_{this};
};

}  // namespace net

#endif  // NET_HTTP_HTTP_STREAM_POOL_ATTEMPT_MANAGER_QUIC_TASK_H_
