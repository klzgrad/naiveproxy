// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/log/net_log_entry.h"

#include <utility>

#include "base/callback.h"
#include "base/values.h"
#include "net/log/net_log.h"

namespace net {

std::unique_ptr<base::Value> NetLogEntry::ToValue() const {
  std::unique_ptr<base::DictionaryValue> entry_dict(
      new base::DictionaryValue());

  entry_dict->SetString("time", NetLog::TickCountToString(data_->time));

  // Set the entry source.
  std::unique_ptr<base::DictionaryValue> source_dict(
      new base::DictionaryValue());
  source_dict->SetInteger("id", data_->source.id);
  source_dict->SetInteger("type", static_cast<int>(data_->source.type));
  entry_dict->Set("source", std::move(source_dict));

  // Set the event info.
  entry_dict->SetInteger("type", static_cast<int>(data_->type));
  entry_dict->SetInteger("phase", static_cast<int>(data_->phase));

  // Set the event-specific parameters.
  if (data_->parameters_callback) {
    std::unique_ptr<base::Value> value(
        data_->parameters_callback->Run(capture_mode_));
    if (value)
      entry_dict->Set("params", std::move(value));
  }

  return std::move(entry_dict);
}

std::unique_ptr<base::Value> NetLogEntry::ParametersToValue() const {
  if (data_->parameters_callback)
    return data_->parameters_callback->Run(capture_mode_);
  return nullptr;
}

NetLogEntryData::NetLogEntryData(
    NetLogEventType type,
    NetLogSource source,
    NetLogEventPhase phase,
    base::TimeTicks time,
    const NetLogParametersCallback* parameters_callback)
    : type(type),
      source(source),
      phase(phase),
      time(time),
      parameters_callback(parameters_callback) {}

NetLogEntryData::~NetLogEntryData() = default;

NetLogEntry::NetLogEntry(const NetLogEntryData* data,
                         NetLogCaptureMode capture_mode)
    : data_(data), capture_mode_(capture_mode) {}

NetLogEntry::~NetLogEntry() = default;

}  // namespace net
