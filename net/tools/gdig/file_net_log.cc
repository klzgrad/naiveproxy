// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/gdig/file_net_log.h"

#include <stdio.h>

#include <memory>

#include "base/json/json_string_value_serializer.h"
#include "base/logging.h"
#include "base/values.h"
#include "net/log/net_log_entry.h"

namespace net {

FileNetLogObserver::FileNetLogObserver(FILE* destination)
    : destination_(destination) {
  DCHECK(destination != NULL);
}

FileNetLogObserver::~FileNetLogObserver() {
}

void FileNetLogObserver::OnAddEntry(const net::NetLogEntry& entry) {
  // Only NetLogWithSources without a NetLog should have an invalid source.
  DCHECK(entry.source().IsValid());

  const char* source = NetLog::SourceTypeToString(entry.source().type);
  const char* type = NetLog::EventTypeToString(entry.type());

  std::unique_ptr<base::Value> param_value(entry.ParametersToValue());
  std::string params;
  if (param_value.get() != NULL) {
    JSONStringValueSerializer serializer(&params);
    bool ret = serializer.Serialize(*param_value);
    DCHECK(ret);
  }
  base::Time now = base::Time::NowFromSystemTime();
  base::AutoLock lock(lock_);
  if (first_event_time_.is_null()) {
    first_event_time_ = now;
  }
  base::TimeDelta elapsed_time = now - first_event_time_;
  fprintf(destination_ , "%u\t%u\t%s\t%s\t%s\n",
          static_cast<unsigned>(elapsed_time.InMilliseconds()),
          entry.source().id, source, type, params.c_str());
}

}  // namespace net
