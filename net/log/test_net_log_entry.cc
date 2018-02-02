// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/log/test_net_log_entry.h"

#include <utility>

#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/values.h"

namespace net {

TestNetLogEntry::TestNetLogEntry(NetLogEventType type,
                                 const base::TimeTicks& time,
                                 NetLogSource source,
                                 NetLogEventPhase phase,
                                 std::unique_ptr<base::DictionaryValue> params)
    : type(type),
      time(time),
      source(source),
      phase(phase),
      params(std::move(params)) {
  // Only entries without a NetLog should have an invalid source.
  CHECK(source.IsValid());
}

TestNetLogEntry::TestNetLogEntry(const TestNetLogEntry& entry) {
  *this = entry;
}

TestNetLogEntry::~TestNetLogEntry() {
}

TestNetLogEntry& TestNetLogEntry::operator=(const TestNetLogEntry& entry) {
  type = entry.type;
  time = entry.time;
  source = entry.source;
  phase = entry.phase;
  params.reset(entry.params ? entry.params->DeepCopy() : NULL);
  return *this;
}

bool TestNetLogEntry::GetStringValue(const std::string& name,
                                     std::string* value) const {
  if (!params)
    return false;
  return params->GetString(name, value);
}

bool TestNetLogEntry::GetIntegerValue(const std::string& name,
                                      int* value) const {
  if (!params)
    return false;
  return params->GetInteger(name, value);
}

bool TestNetLogEntry::GetBooleanValue(const std::string& name,
                                      bool* value) const {
  if (!params)
    return false;
  return params->GetBoolean(name, value);
}

bool TestNetLogEntry::GetListValue(const std::string& name,
                                   base::ListValue** value) const {
  if (!params)
    return false;
  return params->GetList(name, value);
}

bool TestNetLogEntry::GetNetErrorCode(int* value) const {
  return GetIntegerValue("net_error", value);
}

std::string TestNetLogEntry::GetParamsJson() const {
  if (!params)
    return std::string();
  std::string json;
  base::JSONWriter::Write(*params, &json);
  return json;
}

}  // namespace net
