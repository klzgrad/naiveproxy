// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/trace_event/log_message.h"

#include <stdint.h>

#include <string>
#include <string_view>

#include "base/json/string_escape.h"
#include "base/notreached.h"
#include "base/strings/stringprintf.h"

namespace base::trace_event {

LogMessage::LogMessage(const char* file, std::string_view message, int line)
    : file_(file), message_(message), line_number_(line) {}

LogMessage::~LogMessage() = default;

void LogMessage::AppendAsTraceFormat(std::string* out) const {
  out->append("{");
  out->append(base::StringPrintf("\"line\":\"%d\",", line_number_));
  out->append("\"message\":");
  base::EscapeJSONString(message_, true, out);
  out->append(",");
  out->append(base::StringPrintf("\"file\":\"%s\"", file_));
  out->append("}");
}

bool LogMessage::AppendToProto(ProtoAppender* appender) const {
  // LogMessage is handled in a special way in
  // track_event_thread_local_event_sink.cc in the function |AddTraceEvent|, so
  // this call should never happen.
  NOTREACHED();
}

}  // namespace base::trace_event
