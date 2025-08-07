// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_stream_pool_tcp_based_attempt.h"

#include <memory>
#include <optional>
#include <string_view>

#include "base/metrics/histogram_functions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/trace_event/trace_id_helper.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "net/base/completion_once_callback.h"
#include "net/base/host_port_pair.h"
#include "net/base/ip_endpoint.h"
#include "net/base/tracing.h"
#include "net/http/http_stream_key.h"
#include "net/http/http_stream_pool.h"
#include "net/http/http_stream_pool_attempt_manager.h"
#include "net/log/net_log_with_source.h"
#include "net/socket/stream_attempt.h"
#include "net/socket/stream_socket_close_reason.h"
#include "net/socket/tcp_stream_attempt.h"
#include "net/socket/tls_stream_attempt.h"

namespace net {

namespace {

std::string_view GetResultHistogramSuffix(std::optional<int> result) {
  if (!result.has_value()) {
    return "Canceled";
  }

  switch (*result) {
    case OK:
      return "Success";
    case ERR_TIMED_OUT:
      return "TimedOut";
    case ERR_CONNECTION_TIMED_OUT:
      return "ConnectionTimedOut";
    default:
      return "OtherFailure";
  }
}

std::string_view GetHistogramSuffixForTcpBasedAttemptCancel(
    StreamSocketCloseReason reason) {
  switch (reason) {
    case StreamSocketCloseReason::kSpdySessionCreated:
      return "NewSpdySession";
    case StreamSocketCloseReason::kQuicSessionCreated:
      return "NewQuicSession";
    case StreamSocketCloseReason::kUsingExistingSpdySession:
      return "ExistingSpdySession";
    case StreamSocketCloseReason::kUsingExistingQuicSession:
      return "ExistingQuicSession";
    case StreamSocketCloseReason::kUnspecified:
    case StreamSocketCloseReason::kCloseAllConnections:
    case StreamSocketCloseReason::kIpAddressChanged:
    case StreamSocketCloseReason::kSslConfigChanged:
    case StreamSocketCloseReason::kCannotUseTcpBasedProtocols:
    case StreamSocketCloseReason::kAbort:
      return "Other";
  }
}

}  // namespace

HttpStreamPool::TcpBasedAttempt::TcpBasedAttempt(AttemptManager* manager,
                                                 bool using_tls,
                                                 IPEndPoint ip_endpoint)
    : manager_(manager),
      track_(base::trace_event::GetNextGlobalTraceId()),
      flow_(perfetto::Flow::ProcessScoped(
          base::trace_event::GetNextGlobalTraceId())) {
  TRACE_EVENT_INSTANT("net.stream", "TcpBasedAttemptStart", manager_->track(),
                      flow_);
  TRACE_EVENT_BEGIN("net.stream", "TcpBasedAttempt::TcpBasedAttempt", track_,
                    flow_, "ip_endpoint", ip_endpoint.ToString());
  if (using_tls) {
    attempt_ = std::make_unique<TlsStreamAttempt>(
        manager_->pool()->stream_attempt_params(), std::move(ip_endpoint),
        track_,
        HostPortPair::FromSchemeHostPort(manager_->stream_key().destination()),
        /*delegate=*/this);
  } else {
    attempt_ = std::make_unique<TcpStreamAttempt>(
        manager_->pool()->stream_attempt_params(), std::move(ip_endpoint),
        track_);
  }
}

HttpStreamPool::TcpBasedAttempt::~TcpBasedAttempt() {
  base::TimeDelta elapsed = base::TimeTicks::Now() - start_time_;
  base::UmaHistogramMediumTimes(
      base::StrCat({"Net.HttpStreamPool.TcpBasedAttemptTime2.",
                    GetResultHistogramSuffix(result_)}),
      elapsed);

  if (cancel_reason_.has_value()) {
    base::UmaHistogramEnumeration(
        "Net.HttpStreamPool.TcpBasedAttemptCancelReason", *cancel_reason_);

    std::string_view suffix =
        GetHistogramSuffixForTcpBasedAttemptCancel(*cancel_reason_);
    CHECK(manager_->initial_attempt_state().has_value());
    base::UmaHistogramEnumeration(
        base::StrCat(
            {"Net.HttpStreamPool.TcpBasedAttemptCanceledInitialAttemptState.",
             suffix}),
        *manager_->initial_attempt_state());
    base::UmaHistogramLongTimes100(
        base::StrCat(
            {"Net.HttpStreamPool.TcpBasedAttemptCanceledTime2.", suffix}),
        elapsed);
  }

  // Reset `attempt_` before emitting trace events to ensure that trace events
  // in `attempt_` balances.
  attempt_.reset();
  TRACE_EVENT_END(
      "net.stream", track_, "result", result_.value_or(ERR_ABORTED),
      "cancel_reason",
      cancel_reason_.value_or(StreamSocketCloseReason::kUnspecified));
  TRACE_EVENT_INSTANT("net.stream", "TcpBasedAttemptEnd", manager_->track(),
                      flow_);
}

void HttpStreamPool::TcpBasedAttempt::Start() {
  CHECK(attempt_);
  start_time_ = base::TimeTicks::Now();
  int rv = attempt_->Start(base::BindOnce(&TcpBasedAttempt::OnAttemptComplete,
                                          weak_ptr_factory_.GetWeakPtr()));
  manager_->net_log().AddEvent(
      NetLogEventType::HTTP_STREAM_POOL_TCP_BASED_ATTEMPT_START, [&] {
        base::Value::Dict dict = manager_->GetStatesAsNetLogParams();
        dict.Set("ip_endpoint", ip_endpoint().ToString());
        attempt()->net_log().source().AddToEventParameters(dict);
        return dict;
      });
  // Add NetLog dependency after Start() so that the first event of the
  // attempt can have meaningful description in the NetLog viewer.
  attempt()->net_log().AddEventReferencingSource(
      NetLogEventType::TCP_BASED_ATTEMPT_BOUND_TO_POOL,
      manager_->net_log().source());

  if (rv == ERR_IO_PENDING) {
    // base::Unretained() is safe here because `this` owns `slow_timer_`.
    slow_timer_.Start(FROM_HERE, HttpStreamPool::GetConnectionAttemptDelay(),
                      base::BindOnce(&TcpBasedAttempt::OnAttemptSlow,
                                     base::Unretained(this)));
  } else {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&TcpBasedAttempt::OnAttemptComplete,
                                  weak_ptr_factory_.GetWeakPtr(), rv));
  }
}

void HttpStreamPool::TcpBasedAttempt::SetCancelReason(
    StreamSocketCloseReason reason) {
  cancel_reason_ = reason;
  if (attempt_) {
    attempt_->SetCancelReason(reason);
  }
}

int HttpStreamPool::TcpBasedAttempt::WaitForSSLConfigReady(
    CompletionOnceCallback callback) {
  if (manager_->service_endpoint_request()->EndpointsCryptoReady()) {
    return OK;
  }

  ssl_config_wait_start_time_ = base::TimeTicks::Now();
  ssl_config_waiting_callback_ = std::move(callback);
  return ERR_IO_PENDING;
}

base::expected<SSLConfig, TlsStreamAttempt::GetSSLConfigError>
HttpStreamPool::TcpBasedAttempt::GetSSLConfig() {
  base::expected<SSLConfig, TlsStreamAttempt::GetSSLConfigError> result =
      manager_->GetSSLConfig(ip_endpoint());
  if (!result.has_value()) {
    is_aborted_ = true;
  }

  return result;
}

std::optional<CompletionOnceCallback>
HttpStreamPool::TcpBasedAttempt::MaybeTakeSSLConfigWaitingCallback() {
  if (ssl_config_waiting_callback_.is_null()) {
    return std::nullopt;
  }

  CHECK(!ssl_config_wait_start_time_.is_null());
  base::UmaHistogramMediumTimes(
      "Net.HttpStreamPool.TcpBasedAttemptSSLConfigWaitTime2",
      base::TimeTicks::Now() - ssl_config_wait_start_time_);

  if (!is_slow_ && !slow_timer_.IsRunning()) {
    // Resume the slow timer as `attempt_` will start a TLS handshake.
    // TODO(crbug.com/346835898): Should we use a different delay other than
    // the connection attempt delay?
    // base::Unretained() is safe here because `this` owns `slow_timer_`.
    slow_timer_.Start(FROM_HERE, HttpStreamPool::GetConnectionAttemptDelay(),
                      base::BindOnce(&TcpBasedAttempt::OnAttemptSlow,
                                     base::Unretained(this)));
  }

  return std::move(ssl_config_waiting_callback_);
}

base::Value::Dict HttpStreamPool::TcpBasedAttempt::GetInfoAsValue() const {
  base::Value::Dict dict;
  if (attempt_) {
    dict.Set("attempt_state", attempt_->GetInfoAsValue());
    dict.Set("ip_endpoint", attempt_->ip_endpoint().ToString());
    if (attempt_->stream_socket()) {
      attempt_->stream_socket()->NetLog().source().AddToEventParameters(dict);
    }
  }
  dict.Set("is_slow", is_slow_);
  dict.Set("is_aborted", is_aborted_);
  dict.Set("started", !start_time_.is_null());
  if (!start_time_.is_null()) {
    base::TimeDelta elapsed = base::TimeTicks::Now() - start_time_;
    dict.Set("elapsed_ms", static_cast<int>(elapsed.InMilliseconds()));
  }
  if (result_.has_value()) {
    dict.Set("result", *result_);
  }
  if (cancel_reason_.has_value()) {
    dict.Set("cancel_reason", static_cast<int>(*cancel_reason_));
  }
  manager_->net_log().source().AddToEventParameters(dict);
  return dict;
}

void HttpStreamPool::TcpBasedAttempt::OnTcpHandshakeComplete() {
  // Pause the slow timer until `attempt_` starts a TLS handshake to exclude the
  // time spent waiting for SSLConfig from the time `this` is considered slow.
  slow_timer_.Stop();
}

void HttpStreamPool::TcpBasedAttempt::OnAttemptSlow() {
  CHECK(!is_slow_);
  is_slow_ = true;
  manager_->OnTcpBasedAttemptSlow(this);
}

void HttpStreamPool::TcpBasedAttempt::OnAttemptComplete(int rv) {
  manager_->net_log().AddEvent(
      NetLogEventType::HTTP_STREAM_POOL_TCP_BASED_ATTEMPT_END, [&] {
        base::Value::Dict dict = manager_->GetStatesAsNetLogParams();
        dict.Set("result", ErrorToString(rv));
        attempt()->net_log().source().AddToEventParameters(dict);
        return dict;
      });

  CHECK(!result_.has_value());
  result_ = rv;
  slow_timer_.Stop();
  manager_->OnTcpBasedAttemptComplete(this, rv);
}

}  // namespace net
