// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/logging.h"

#include <limits.h>
#include <stdint.h>

#include <thread>

#include "base/macros.h"
#include "util/build_config.h"

#if defined(OS_WIN)
#include <io.h>
#include <windows.h>
// Windows warns on using write().  It prefers _write().
#define write(fd, buf, count) _write(fd, buf, static_cast<unsigned int>(count))
// Windows doesn't define STDERR_FILENO.  Define it here.
#define STDERR_FILENO 2

#elif defined(OS_POSIX) || defined(OS_FUCHSIA)
#include <sys/time.h>
#include <time.h>
#endif

#if defined(OS_POSIX) || defined(OS_FUCHSIA)
#include <errno.h>
#include <paths.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <ostream>
#include <string>
#include <utility>

#include "base/callback.h"
#include "base/containers/stack.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"

#if defined(OS_POSIX) || defined(OS_FUCHSIA)
#include "base/posix/safe_strerror.h"
#endif

namespace logging {

namespace {

const char* const log_severity_names[] = {"INFO", "WARNING", "ERROR", "FATAL"};
static_assert(LOG_NUM_SEVERITIES == arraysize(log_severity_names),
              "Incorrect number of log_severity_names");

const char* log_severity_name(int severity) {
  if (severity >= 0 && severity < LOG_NUM_SEVERITIES)
    return log_severity_names[severity];
  return "UNKNOWN";
}

int g_min_log_level = 0;

// For LOG_ERROR and above, always print to stderr.
const int kAlwaysPrintErrorLevel = LOG_ERROR;

}  // namespace

#if DCHECK_IS_CONFIGURABLE
// In DCHECK-enabled Chrome builds, allow the meaning of LOG_DCHECK to be
// determined at run-time. We default it to INFO, to avoid it triggering
// crashes before the run-time has explicitly chosen the behaviour.
logging::LogSeverity LOG_DCHECK = LOG_INFO;
#endif  // DCHECK_IS_CONFIGURABLE

// This is never instantiated, it's just used for EAT_STREAM_PARAMETERS to have
// an object of the correct type on the LHS of the unused part of the ternary
// operator.
std::ostream* g_swallow_stream;

void SetMinLogLevel(int level) {
  g_min_log_level = std::min(LOG_FATAL, level);
}

int GetMinLogLevel() {
  return g_min_log_level;
}

bool ShouldCreateLogMessage(int severity) {
  if (severity < g_min_log_level)
    return false;

  // Return true here unless we know ~LogMessage won't do anything. Note that
  // ~LogMessage writes to stderr if severity_ >= kAlwaysPrintErrorLevel, even
  // when g_logging_destination is LOG_NONE.
  return severity >= kAlwaysPrintErrorLevel;
}

// Explicit instantiations for commonly used comparisons.
template std::string* MakeCheckOpString<int, int>(const int&,
                                                  const int&,
                                                  const char* names);
template std::string* MakeCheckOpString<unsigned long, unsigned long>(
    const unsigned long&,
    const unsigned long&,
    const char* names);
template std::string* MakeCheckOpString<unsigned long, unsigned int>(
    const unsigned long&,
    const unsigned int&,
    const char* names);
template std::string* MakeCheckOpString<unsigned int, unsigned long>(
    const unsigned int&,
    const unsigned long&,
    const char* names);
template std::string* MakeCheckOpString<std::string, std::string>(
    const std::string&,
    const std::string&,
    const char* name);

void MakeCheckOpValueString(std::ostream* os, std::nullptr_t p) {
  (*os) << "nullptr";
}

#if defined(OS_WIN)
LogMessage::SaveLastError::SaveLastError() : last_error_(::GetLastError()) {}

LogMessage::SaveLastError::~SaveLastError() {
  ::SetLastError(last_error_);
}
#endif  // defined(OS_WIN)

LogMessage::LogMessage(const char* file, int line, LogSeverity severity)
    : severity_(severity), file_(file), line_(line) {
  Init(file, line);
}

LogMessage::LogMessage(const char* file, int line, const char* condition)
    : severity_(LOG_FATAL), file_(file), line_(line) {
  Init(file, line);
  stream_ << "Check failed: " << condition << ". ";
}

LogMessage::LogMessage(const char* file, int line, std::string* result)
    : severity_(LOG_FATAL), file_(file), line_(line) {
  Init(file, line);
  stream_ << "Check failed: " << *result;
  delete result;
}

LogMessage::LogMessage(const char* file,
                       int line,
                       LogSeverity severity,
                       std::string* result)
    : severity_(severity), file_(file), line_(line) {
  Init(file, line);
  stream_ << "Check failed: " << *result;
  delete result;
}

LogMessage::~LogMessage() {
  if (severity_ == LOG_FATAL) {
    stream_ << std::endl;  // Newline to separate from log message.
  }
  stream_ << std::endl;
  std::string str_newline(stream_.str());

#if defined(OS_WIN)
  OutputDebugStringA(str_newline.c_str());
#endif
  ignore_result(fwrite(str_newline.data(), str_newline.size(), 1, stderr));
  fflush(stderr);

  if (severity_ == LOG_FATAL) {
    abort();
  }
}

// writes the common header info to the stream
void LogMessage::Init(const char* file, int line) {
  base::StringPiece filename(file);
  size_t last_slash_pos = filename.find_last_of("\\/");
  if (last_slash_pos != base::StringPiece::npos)
    filename.remove_prefix(last_slash_pos + 1);

  // TODO(darin): It might be nice if the columns were fixed width.

  stream_ << '[';
  stream_ << std::this_thread::get_id() << ':';
#if defined(OS_WIN)
  SYSTEMTIME local_time;
  GetLocalTime(&local_time);
  stream_ << std::setfill('0') << std::setw(2) << local_time.wMonth
          << std::setw(2) << local_time.wDay << '/' << std::setw(2)
          << local_time.wHour << std::setw(2) << local_time.wMinute
          << std::setw(2) << local_time.wSecond << '.' << std::setw(3)
          << local_time.wMilliseconds << ':';
#elif defined(OS_POSIX) || defined(OS_FUCHSIA)
  timeval tv;
  gettimeofday(&tv, nullptr);
  time_t t = tv.tv_sec;
  struct tm local_time;
  localtime_r(&t, &local_time);
  struct tm* tm_time = &local_time;
  stream_ << std::setfill('0') << std::setw(2) << 1 + tm_time->tm_mon
          << std::setw(2) << tm_time->tm_mday << '/' << std::setw(2)
          << tm_time->tm_hour << std::setw(2) << tm_time->tm_min << std::setw(2)
          << tm_time->tm_sec << '.' << std::setw(6) << tv.tv_usec << ':';
#else
#error Unsupported platform
#endif
  if (severity_ >= 0)
    stream_ << log_severity_name(severity_);
  else
    stream_ << "VERBOSE" << -severity_;

  stream_ << ":" << filename << "(" << line << ")] ";

  message_start_ = stream_.str().length();
}

#if defined(OS_WIN)
// This has already been defined in the header, but defining it again as DWORD
// ensures that the type used in the header is equivalent to DWORD. If not,
// the redefinition is a compile error.
typedef DWORD SystemErrorCode;
#endif

SystemErrorCode GetLastSystemErrorCode() {
#if defined(OS_WIN)
  return ::GetLastError();
#elif defined(OS_POSIX) || defined(OS_FUCHSIA)
  return errno;
#endif
}

std::string SystemErrorCodeToString(SystemErrorCode error_code) {
#if defined(OS_WIN)
  const int kErrorMessageBufferSize = 256;
  char msgbuf[kErrorMessageBufferSize];
  DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
  DWORD len = FormatMessageA(flags, nullptr, error_code, 0, msgbuf,
                             arraysize(msgbuf), nullptr);
  if (len) {
    // Messages returned by system end with line breaks.
    return base::CollapseWhitespaceASCII(msgbuf, true) +
           base::StringPrintf(" (0x%lX)", error_code);
  }
  return base::StringPrintf("Error (0x%lX) while retrieving error. (0x%lX)",
                            GetLastError(), error_code);
#elif defined(OS_POSIX) || defined(OS_FUCHSIA)
  return base::safe_strerror(error_code) +
         base::StringPrintf(" (%d)", error_code);
#endif  // defined(OS_WIN)
}

#if defined(OS_WIN)
Win32ErrorLogMessage::Win32ErrorLogMessage(const char* file,
                                           int line,
                                           LogSeverity severity,
                                           SystemErrorCode err)
    : err_(err), log_message_(file, line, severity) {}

Win32ErrorLogMessage::~Win32ErrorLogMessage() {
  stream() << ": " << SystemErrorCodeToString(err_);
}
#elif defined(OS_POSIX) || defined(OS_FUCHSIA)
ErrnoLogMessage::ErrnoLogMessage(const char* file,
                                 int line,
                                 LogSeverity severity,
                                 SystemErrorCode err)
    : err_(err), log_message_(file, line, severity) {}

ErrnoLogMessage::~ErrnoLogMessage() {
  stream() << ": " << SystemErrorCodeToString(err_);
}
#endif  // defined(OS_WIN)

void RawLog(int level, const char* message) {
  if (level >= g_min_log_level && message) {
    size_t bytes_written = 0;
    const size_t message_len = strlen(message);
    int rv;
    while (bytes_written < message_len) {
      rv = HANDLE_EINTR(write(STDERR_FILENO, message + bytes_written,
                              message_len - bytes_written));
      if (rv < 0) {
        // Give up, nothing we can do now.
        break;
      }
      bytes_written += rv;
    }

    if (message_len > 0 && message[message_len - 1] != '\n') {
      do {
        rv = HANDLE_EINTR(write(STDERR_FILENO, "\n", 1));
        if (rv < 0) {
          // Give up, nothing we can do now.
          break;
        }
      } while (rv != 1);
    }
  }

  if (level == LOG_FATAL)
    abort();
}

// This was defined at the beginning of this file.
#undef write

void LogErrorNotReached(const char* file, int line) {
  LogMessage(file, line, LOG_ERROR).stream() << "NOTREACHED() hit.";
}

}  // namespace logging

std::ostream& std::operator<<(std::ostream& out, const wchar_t* wstr) {
  return out << (wstr ? base::WideToUTF8(wstr) : std::string());
}
