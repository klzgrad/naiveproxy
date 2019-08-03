// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/log/net_log_entry.h"

#include <utility>

#include "base/callback.h"
#include "base/values.h"
#include "net/log/net_log.h"

namespace net {

base::Value NetLogEntry::ToValue() const {
  base::DictionaryValue entry_dict;

  entry_dict.SetString("time", NetLog::TickCountToString(data_->time));

  // Set the entry source.
  base::DictionaryValue source_dict;
  source_dict.SetInteger("id", data_->source.id);
  source_dict.SetInteger("type", static_cast<int>(data_->source.type));
  entry_dict.SetKey("source", std::move(source_dict));

  // Set the event info.
  entry_dict.SetInteger("type", static_cast<int>(data_->type));
  entry_dict.SetInteger("phase", static_cast<int>(data_->phase));

  // Set the event-specific parameters.
  base::Value params = ParametersToValue();
  if (!params.is_none())
    entry_dict.SetKey("params", std::move(params));

  return std::move(entry_dict);
}

base::Value NetLogEntry::ParametersToValue() const {
  if (data_->parameters_callback)
    return data_->parameters_callback->Run(capture_mode_);
  return base::Value();
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
