// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40284755): Remove this and spanify to fix the errors.
#pragma allow_unsafe_buffers
#endif

#include "base/logging_win.h"

#include <initguid.h>

#include "base/memory/singleton.h"

namespace logging {

using base::win::EtwEventLevel;
using base::win::EtwMofEvent;

DEFINE_GUID(kLogEventId,
            0x7fe69228,
            0x633e,
            0x4f06,
            0x80,
            0xc1,
            0x52,
            0x7f,
            0xea,
            0x23,
            0xe3,
            0xa7);

LogEventProvider::LogEventProvider() : old_log_level_(LOG_NONE) {}

LogEventProvider* LogEventProvider::GetInstance() {
  return base::Singleton<LogEventProvider, base::StaticMemorySingletonTraits<
                                               LogEventProvider>>::get();
}

bool LogEventProvider::LogMessage(logging::LogSeverity severity,
                                  const char* file,
                                  int line,
                                  size_t message_start,
                                  const std::string& message) {
  EtwEventLevel level = TRACE_LEVEL_NONE;

  // Convert the log severity to the most appropriate ETW trace level.
  if (severity >= 0) {
    switch (severity) {
      case LOGGING_INFO:
        level = TRACE_LEVEL_INFORMATION;
        break;
      case LOGGING_WARNING:
        level = TRACE_LEVEL_WARNING;
        break;
      case LOGGING_ERROR:
        level = TRACE_LEVEL_ERROR;
        break;
      case LOGGING_FATAL:
        level = TRACE_LEVEL_FATAL;
        break;
    }
  } else {  // severity < 0 is VLOG verbosity levels.
    level = static_cast<EtwEventLevel>(TRACE_LEVEL_INFORMATION - severity);
  }

  // Bail if we're not logging, not at that level,
  // or if we're post-atexit handling.
  LogEventProvider* provider = LogEventProvider::GetInstance();
  if (provider == NULL || level > provider->enable_level()) {
    return false;
  }

  // And now log the event.
  if (provider->enable_flags() & ENABLE_LOG_MESSAGE_ONLY) {
    EtwMofEvent<1> event(kLogEventId, LOG_MESSAGE, level);
    event.SetField(0, message.length() + 1 - message_start,
                   message.c_str() + message_start);

    provider->Log(event.get());
  } else {
    const size_t kMaxBacktraceDepth = 32;
    void* backtrace[kMaxBacktraceDepth];
    DWORD depth = 0;

    // Capture a stack trace if one is requested.
    // requested per our enable flags.
    if (provider->enable_flags() & ENABLE_STACK_TRACE_CAPTURE) {
      depth = CaptureStackBackTrace(2, kMaxBacktraceDepth, backtrace, NULL);
    }

    EtwMofEvent<5> event(kLogEventId, LOG_MESSAGE_FULL, level);
    if (file == NULL) {
      file = "";
    }

    // Add the stack trace.
    event.SetField(0, sizeof(depth), &depth);
    event.SetField(1, sizeof(backtrace[0]) * depth, &backtrace);
    // The line.
    event.SetField(2, sizeof(line), &line);
    // The file.
    event.SetField(3, strlen(file) + 1, file);
    // And finally the message.
    event.SetField(4, message.length() + 1 - message_start,
                   message.c_str() + message_start);

    provider->Log(event.get());
  }

  // Don't increase verbosity in other log destinations.
  if (severity < provider->old_log_level_) {
    return true;
  }

  return false;
}

void LogEventProvider::Initialize(const GUID& provider_name) {
  LogEventProvider* provider = LogEventProvider::GetInstance();

  provider->set_provider_name(provider_name);
  provider->Register();

  // Register our message handler with logging.
  SetLogMessageHandler(LogMessage);
}

void LogEventProvider::Uninitialize() {
  LogEventProvider::GetInstance()->Unregister();
}

void LogEventProvider::OnEventsEnabled() {
  // Grab the old log level so we can restore it later.
  old_log_level_ = GetMinLogLevel();

  // Convert the new trace level to a logging severity
  // and enable logging at that level.
  EtwEventLevel level = enable_level();
  if (level == TRACE_LEVEL_NONE || level == TRACE_LEVEL_FATAL) {
    SetMinLogLevel(LOGGING_FATAL);
  } else if (level == TRACE_LEVEL_ERROR) {
    SetMinLogLevel(LOGGING_ERROR);
  } else if (level == TRACE_LEVEL_WARNING) {
    SetMinLogLevel(LOGGING_WARNING);
  } else if (level == TRACE_LEVEL_INFORMATION) {
    SetMinLogLevel(LOGGING_INFO);
  } else if (level >= TRACE_LEVEL_VERBOSE) {
    // Above INFO, we enable verbose levels with negative severities.
    SetMinLogLevel(TRACE_LEVEL_INFORMATION - level);
  }
}

void LogEventProvider::OnEventsDisabled() {
  // Restore the old log level.
  SetMinLogLevel(old_log_level_);
}

}  // namespace logging
