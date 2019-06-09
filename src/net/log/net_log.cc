// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/log/net_log.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/base64.h"
#include "base/bind.h"
#include "base/callback.h"
#include "base/logging.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "net/base/escape.h"

namespace net {

namespace {

base::Value NetLogBoolCallback(const char* name,
                               bool value,
                               NetLogCaptureMode /* capture_mode */) {
  base::DictionaryValue event_params;
  event_params.SetBoolean(name, value);
  return std::move(event_params);
}

base::Value NetLogIntCallback(const char* name,
                              int value,
                              NetLogCaptureMode /* capture_mode */) {
  base::DictionaryValue event_params;
  event_params.SetInteger(name, value);
  return std::move(event_params);
}

base::Value NetLogInt64Callback(const char* name,
                                int64_t value,
                                NetLogCaptureMode /* capture_mode */) {
  base::DictionaryValue event_params;
  event_params.SetKey(name, NetLogNumberValue(value));
  return std::move(event_params);
}

base::Value NetLogStringCallback(const char* name,
                                 const std::string* value,
                                 NetLogCaptureMode /* capture_mode */) {
  base::DictionaryValue event_params;
  event_params.SetString(name, *value);
  return std::move(event_params);
}

base::Value NetLogCharStringCallback(const char* name,
                                     const char* value,
                                     NetLogCaptureMode /* capture_mode */) {
  base::DictionaryValue event_params;
  event_params.SetString(name, value);
  return std::move(event_params);
}

base::Value NetLogString16Callback(const char* name,
                                   const base::string16* value,
                                   NetLogCaptureMode /* capture_mode */) {
  base::DictionaryValue event_params;
  event_params.SetString(name, *value);
  return std::move(event_params);
}

// IEEE 64-bit doubles have a 52-bit mantissa, and can therefore represent
// 53-bits worth of precision (see also documentation for JavaScript's
// Number.MAX_SAFE_INTEGER for more discussion on this).
//
// If the number can be represented with an int or double use that. Otherwise
// fallback to encoding it as a string.
template <typename T>
base::Value NetLogNumberValueHelper(T num) {
  // Fits in a (32-bit) int: [-2^31, 2^31 - 1]
  if ((!std::is_signed<T>::value || (num >= static_cast<T>(-2147483648))) &&
      (num <= static_cast<T>(2147483647))) {
    return base::Value(static_cast<int>(num));
  }

  // Fits in a double: (-2^53, 2^53)
  if ((!std::is_signed<T>::value ||
       (num >= static_cast<T>(-9007199254740991))) &&
      (num <= static_cast<T>(9007199254740991))) {
    return base::Value(static_cast<double>(num));
  }

  // Otherwise format as a string.
  return base::Value(base::NumberToString(num));
}

}  // namespace

NetLog::ThreadSafeObserver::ThreadSafeObserver() : net_log_(nullptr) {}

NetLog::ThreadSafeObserver::~ThreadSafeObserver() {
  // Make sure we aren't watching a NetLog on destruction.  Because the NetLog
  // may pass events to each observer on multiple threads, we cannot safely
  // stop watching a NetLog automatically from a parent class.
  DCHECK(!net_log_);
}

NetLogCaptureMode NetLog::ThreadSafeObserver::capture_mode() const {
  DCHECK(net_log_);
  return capture_mode_;
}

NetLog* NetLog::ThreadSafeObserver::net_log() const {
  return net_log_;
}

void NetLog::ThreadSafeObserver::OnAddEntryData(
    const NetLogEntryData& entry_data) {
  OnAddEntry(NetLogEntry(&entry_data, capture_mode()));
}

NetLog::NetLog() : last_id_(0), is_capturing_(0) {
}

NetLog::~NetLog() = default;

void NetLog::AddGlobalEntry(NetLogEventType type) {
  AddEntry(type, NetLogSource(NetLogSourceType::NONE, NextID()),
           NetLogEventPhase::NONE, nullptr);
}

void NetLog::AddGlobalEntry(
    NetLogEventType type,
    const NetLogParametersCallback& parameters_callback) {
  AddEntry(type, NetLogSource(NetLogSourceType::NONE, NextID()),
           NetLogEventPhase::NONE, &parameters_callback);
}

uint32_t NetLog::NextID() {
  return base::subtle::NoBarrier_AtomicIncrement(&last_id_, 1);
}

bool NetLog::IsCapturing() const {
  return base::subtle::NoBarrier_Load(&is_capturing_) != 0;
}

void NetLog::AddObserver(NetLog::ThreadSafeObserver* observer,
                         NetLogCaptureMode capture_mode) {
  base::AutoLock lock(lock_);

  DCHECK(!observer->net_log_);
  DCHECK(!HasObserver(observer));
  DCHECK_LT(observers_.size(), 20u);  // Performance sanity check.

  observers_.push_back(observer);

  observer->net_log_ = this;
  observer->capture_mode_ = capture_mode;
  UpdateIsCapturing();
}

void NetLog::SetObserverCaptureMode(NetLog::ThreadSafeObserver* observer,
                                    NetLogCaptureMode capture_mode) {
  base::AutoLock lock(lock_);

  DCHECK(HasObserver(observer));
  DCHECK_EQ(this, observer->net_log_);
  observer->capture_mode_ = capture_mode;
}

void NetLog::RemoveObserver(NetLog::ThreadSafeObserver* observer) {
  base::AutoLock lock(lock_);

  DCHECK_EQ(this, observer->net_log_);

  auto it = std::find(observers_.begin(), observers_.end(), observer);
  DCHECK(it != observers_.end());
  observers_.erase(it);

  observer->net_log_ = nullptr;
  observer->capture_mode_ = NetLogCaptureMode();
  UpdateIsCapturing();
}

void NetLog::UpdateIsCapturing() {
  lock_.AssertAcquired();
  base::subtle::NoBarrier_Store(&is_capturing_, observers_.size() ? 1 : 0);
}

bool NetLog::HasObserver(ThreadSafeObserver* observer) {
  lock_.AssertAcquired();
  return base::ContainsValue(observers_, observer);
}

// static
std::string NetLog::TickCountToString(const base::TimeTicks& time) {
  int64_t delta_time = time.since_origin().InMilliseconds();
  // TODO(https://crbug.com/915391): Use NetLogNumberValue().
  return base::NumberToString(delta_time);
}

// static
std::string NetLog::TimeToString(const base::Time& time) {
  // Convert the base::Time to its (approximate) equivalent in base::TimeTicks.
  base::TimeTicks time_ticks =
      base::TimeTicks::UnixEpoch() + (time - base::Time::UnixEpoch());
  return TickCountToString(time_ticks);
}

// static
const char* NetLog::EventTypeToString(NetLogEventType event) {
  switch (event) {
#define EVENT_TYPE(label)      \
  case NetLogEventType::label: \
    return #label;
#include "net/log/net_log_event_type_list.h"
#undef EVENT_TYPE
    default:
      NOTREACHED();
      return nullptr;
  }
}

// static
base::Value NetLog::GetEventTypesAsValue() {
  base::DictionaryValue dict;
  for (int i = 0; i < static_cast<int>(NetLogEventType::COUNT); ++i) {
    dict.SetInteger(EventTypeToString(static_cast<NetLogEventType>(i)), i);
  }
  return std::move(dict);
}

// static
const char* NetLog::SourceTypeToString(NetLogSourceType source) {
  switch (source) {
#define SOURCE_TYPE(label)      \
  case NetLogSourceType::label: \
    return #label;
#include "net/log/net_log_source_type_list.h"
#undef SOURCE_TYPE
    default:
      NOTREACHED();
      return nullptr;
  }
}

// static
base::Value NetLog::GetSourceTypesAsValue() {
  base::DictionaryValue dict;
  for (int i = 0; i < static_cast<int>(NetLogSourceType::COUNT); ++i) {
    dict.SetInteger(SourceTypeToString(static_cast<NetLogSourceType>(i)), i);
  }
  return std::move(dict);
}

// static
const char* NetLog::EventPhaseToString(NetLogEventPhase phase) {
  switch (phase) {
    case NetLogEventPhase::BEGIN:
      return "PHASE_BEGIN";
    case NetLogEventPhase::END:
      return "PHASE_END";
    case NetLogEventPhase::NONE:
      return "PHASE_NONE";
  }
  NOTREACHED();
  return nullptr;
}

// static
NetLogParametersCallback NetLog::BoolCallback(const char* name, bool value) {
  return base::Bind(&NetLogBoolCallback, name, value);
}

// static
NetLogParametersCallback NetLog::IntCallback(const char* name, int value) {
  return base::Bind(&NetLogIntCallback, name, value);
}

// static
NetLogParametersCallback NetLog::Int64Callback(const char* name,
                                               int64_t value) {
  return base::Bind(&NetLogInt64Callback, name, value);
}

// static
NetLogParametersCallback NetLog::StringCallback(const char* name,
                                                const std::string* value) {
  DCHECK(value);
  return base::Bind(&NetLogStringCallback, name, value);
}

// static
NetLogParametersCallback NetLog::StringCallback(const char* name,
                                                const char* value) {
  DCHECK(value);
  return base::Bind(&NetLogCharStringCallback, name, value);
}

// static
NetLogParametersCallback NetLog::StringCallback(const char* name,
                                                const base::string16* value) {
  DCHECK(value);
  return base::Bind(&NetLogString16Callback, name, value);
}

void NetLog::AddEntry(NetLogEventType type,
                      const NetLogSource& source,
                      NetLogEventPhase phase,
                      const NetLogParametersCallback* parameters_callback) {
  if (!IsCapturing())
    return;
  NetLogEntryData entry_data(type, source, phase, base::TimeTicks::Now(),
                             parameters_callback);

  // Notify all of the log observers.
  base::AutoLock lock(lock_);
  for (auto* observer : observers_)
    observer->OnAddEntryData(entry_data);
}

base::Value NetLogStringValue(base::StringPiece raw) {
  // The common case is that |raw| is ASCII. Represent this directly.
  if (base::IsStringASCII(raw))
    return base::Value(raw);

  // For everything else (including valid UTF-8) percent-escape |raw|, and add a
  // prefix that "tags" the value as being a percent-escaped representation.
  //
  // Note that the sequence E2 80 8B is U+200B (zero-width space) in UTF-8. It
  // is added so the escaped string is not itself also ASCII (otherwise there
  // would be ambiguity for consumers as to when the value needs to be
  // unescaped).
  return base::Value("%ESCAPED:\xE2\x80\x8B " + EscapeNonASCIIAndPercent(raw));
}

base::Value NetLogBinaryValue(const void* bytes, size_t length) {
  std::string b64;
  Base64Encode(base::StringPiece(reinterpret_cast<const char*>(bytes), length),
               &b64);
  return base::Value(std::move(b64));
}

base::Value NetLogNumberValue(int64_t num) {
  return NetLogNumberValueHelper(num);
}

base::Value NetLogNumberValue(uint64_t num) {
  return NetLogNumberValueHelper(num);
}

}  // namespace net
