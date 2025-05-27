// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/test/mock_log.h"

namespace base::test {

// static
MockLog* MockLog::g_instance_ = nullptr;
Lock MockLog::g_lock;

MockLog::MockLog() : is_capturing_logs_(false) {}

MockLog::~MockLog() {
  if (is_capturing_logs_) {
    StopCapturingLogs();
  }
}

void MockLog::StartCapturingLogs() {
  AutoLock scoped_lock(g_lock);

  // We don't use CHECK(), which can generate a new LOG message, and
  // thus can confuse MockLog objects or other registered
  // LogSinks.
  RAW_CHECK(!is_capturing_logs_);
  RAW_CHECK(!g_instance_);

  is_capturing_logs_ = true;
  g_instance_ = this;
  previous_handler_ = logging::GetLogMessageHandler();
  logging::SetLogMessageHandler(LogMessageHandler);
}

void MockLog::StopCapturingLogs() {
  AutoLock scoped_lock(g_lock);

  // We don't use CHECK(), which can generate a new LOG message, and
  // thus can confuse MockLog objects or other registered
  // LogSinks.
  RAW_CHECK(is_capturing_logs_);
  RAW_CHECK(g_instance_ == this);

  is_capturing_logs_ = false;
  logging::SetLogMessageHandler(previous_handler_);
  g_instance_ = nullptr;
}

// static
bool MockLog::LogMessageHandler(int severity,
                                const char* file,
                                int line,
                                size_t message_start,
                                const std::string& str) {
  // gMock guarantees thread-safety for calling a mocked method
  // (https://github.com/google/googlemock/blob/master/googlemock/docs/CookBook.md#using-google-mock-and-threads)
  // but we also need to make sure that Start/StopCapturingLogs are synchronized
  // with LogMessageHandler.
  AutoLock scoped_lock(g_lock);

  return g_instance_->Log(severity, file, line, message_start, str);
}

}  // namespace base::test
