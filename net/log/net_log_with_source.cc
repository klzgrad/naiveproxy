// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/log/net_log_with_source.h"

#include <memory>
#include <utility>

#include "base/bind.h"
#include "base/debug/alias.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "net/base/net_errors.h"
#include "net/log/net_log.h"
#include "net/log/net_log_capture_mode.h"

namespace net {

namespace {

// Returns parameters for logging data transferred events. At a minimum includes
// the number of bytes transferred. If the capture mode allows logging byte
// contents and |byte_count| > 0, then will include the actual bytes. The
// bytes are hex-encoded, since base::Value only supports UTF-8.
std::unique_ptr<base::Value> BytesTransferredCallback(
    int byte_count,
    const char* bytes,
    NetLogCaptureMode capture_mode) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  dict->SetInteger("byte_count", byte_count);
  if (capture_mode.include_socket_bytes() && byte_count > 0)
    dict->SetString("hex_encoded_bytes", base::HexEncode(bytes, byte_count));
  return std::move(dict);
}

}  // namespace

NetLogWithSource::~NetLogWithSource() {
  liveness_ = DEAD;
}

void NetLogWithSource::AddEntry(NetLogEventType type,
                                NetLogEventPhase phase) const {
  CrashIfInvalid();

  if (!net_log_)
    return;
  net_log_->AddEntry(type, source_, phase, NULL);
}

void NetLogWithSource::AddEntry(
    NetLogEventType type,
    NetLogEventPhase phase,
    const NetLogParametersCallback& get_parameters) const {
  CrashIfInvalid();

  if (!net_log_)
    return;
  net_log_->AddEntry(type, source_, phase, &get_parameters);
}

void NetLogWithSource::AddEvent(NetLogEventType type) const {
  AddEntry(type, NetLogEventPhase::NONE);
}

void NetLogWithSource::AddEvent(
    NetLogEventType type,
    const NetLogParametersCallback& get_parameters) const {
  AddEntry(type, NetLogEventPhase::NONE, get_parameters);
}

void NetLogWithSource::BeginEvent(NetLogEventType type) const {
  AddEntry(type, NetLogEventPhase::BEGIN);
}

void NetLogWithSource::BeginEvent(
    NetLogEventType type,
    const NetLogParametersCallback& get_parameters) const {
  AddEntry(type, NetLogEventPhase::BEGIN, get_parameters);
}

void NetLogWithSource::EndEvent(NetLogEventType type) const {
  AddEntry(type, NetLogEventPhase::END);
}

void NetLogWithSource::EndEvent(
    NetLogEventType type,
    const NetLogParametersCallback& get_parameters) const {
  AddEntry(type, NetLogEventPhase::END, get_parameters);
}

void NetLogWithSource::AddEventWithNetErrorCode(NetLogEventType event_type,
                                                int net_error) const {
  DCHECK_NE(ERR_IO_PENDING, net_error);
  if (net_error >= 0) {
    AddEvent(event_type);
  } else {
    AddEvent(event_type, NetLog::IntCallback("net_error", net_error));
  }
}

void NetLogWithSource::EndEventWithNetErrorCode(NetLogEventType event_type,
                                                int net_error) const {
  DCHECK_NE(ERR_IO_PENDING, net_error);
  if (net_error >= 0) {
    EndEvent(event_type);
  } else {
    EndEvent(event_type, NetLog::IntCallback("net_error", net_error));
  }
}

void NetLogWithSource::AddByteTransferEvent(NetLogEventType event_type,
                                            int byte_count,
                                            const char* bytes) const {
  AddEvent(event_type, base::Bind(BytesTransferredCallback, byte_count, bytes));
}

bool NetLogWithSource::IsCapturing() const {
  CrashIfInvalid();
  return net_log_ && net_log_->IsCapturing();
}

// static
NetLogWithSource NetLogWithSource::Make(NetLog* net_log,
                                        NetLogSourceType source_type) {
  if (!net_log)
    return NetLogWithSource();

  NetLogSource source(source_type, net_log->NextID());
  return NetLogWithSource(source, net_log);
}

void NetLogWithSource::CrashIfInvalid() const {
  Liveness liveness = liveness_;

  if (liveness == ALIVE)
    return;

  base::debug::Alias(&liveness);
  CHECK_EQ(ALIVE, liveness);
}

}  // namespace net
