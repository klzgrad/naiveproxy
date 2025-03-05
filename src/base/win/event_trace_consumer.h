// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Declaration of a Windows event trace consumer base class.
#ifndef BASE_WIN_EVENT_TRACE_CONSUMER_H_
#define BASE_WIN_EVENT_TRACE_CONSUMER_H_

#include <windows.h>

#include <evntcons.h>
#include <evntrace.h>
#include <stddef.h>
#include <wmistr.h>

#include <vector>

#include "base/threading/scoped_blocking_call.h"

namespace base {
namespace win {

// This class is a base class that makes it easier to consume events
// from realtime or file sessions. Concrete consumers need to subclass a
// specialization of this class and override the ProcessEvent, the
// ProcessEventRecord and/or the ProcessBuffer methods to implement the
// event consumption logic.
// Usage might look like:
// class MyConsumer: public EtwTraceConsumerBase<MyConsumer, 1> {
//  protected:
//    static VOID WINAPI ProcessEvent(PEVENT_TRACE event);
// };
//
// MyConsumer consumer;
// consumer.OpenFileSession(file_path);
// consumer.Consume();
template <class ImplClass>
class EtwTraceConsumerBase {
 public:
  // If true, receive events in the new EVENT_RECORD format. To receive
  // events in the new format, ProcessEventRecord() must be overridden.
  // See PROCESS_TRACE_MODE_EVENT_RECORD from
  // https://learn.microsoft.com/en-us/windows/win32/api/evntrace/ns-evntrace-event_trace_logfilea
  static constexpr bool kEnableRecordMode = false;
  // If true, TimeStamps in EVENT_HEADER and EVENT_TRACE_HEADER are not
  // converted to system time. See PROCESS_TRACE_MODE_RAW_TIMESTAMP from
  // https://learn.microsoft.com/en-us/windows/win32/api/evntrace/ns-evntrace-event_trace_logfilea
  static constexpr bool kRawTimestamp = false;

  // Constructs a closed consumer.
  EtwTraceConsumerBase() = default;

  EtwTraceConsumerBase(const EtwTraceConsumerBase&) = delete;
  EtwTraceConsumerBase& operator=(const EtwTraceConsumerBase&) = delete;

  ~EtwTraceConsumerBase() { Close(); }

  // Opens the named realtime session, which must be existent.
  // Note: You can use OpenRealtimeSession or OpenFileSession
  //    to open as many as MAXIMUM_WAIT_OBJECTS (63) sessions at
  //    any one time, though only one of them may be a realtime
  //    session.
  HRESULT OpenRealtimeSession(const wchar_t* session_name);

  // Opens the event trace log in "file_name", which must be a full or
  // relative path to an existing event trace log file.
  // Note: You can use OpenRealtimeSession or OpenFileSession
  //    to open as many as kNumSessions at any one time.
  HRESULT OpenFileSession(const wchar_t* file_name);

  // Consume all open sessions from beginning to end.
  HRESULT Consume();

  // Close all open sessions.
  HRESULT Close();

 protected:
  // Override in subclasses to handle events.
  static void ProcessEvent(EVENT_TRACE* event) {}
  // Override in subclasses to handle events.
  static void ProcessEventRecord(EVENT_RECORD* event_record) {}
  // Override in subclasses to handle buffers.
  static bool ProcessBuffer(EVENT_TRACE_LOGFILE* buffer) {
    return true;  // keep going
  }

  HRESULT OpenSessionImpl(EVENT_TRACE_LOGFILE& logfile);

 protected:
  // Currently open sessions.
  std::vector<TRACEHANDLE> trace_handles_;

 private:
  // These delegate to ImplClass callbacks with saner signatures.
  static void WINAPI ProcessEventCallback(EVENT_TRACE* event) {
    ImplClass::ProcessEvent(event);
  }
  static void WINAPI ProcessEventRecordCallback(EVENT_RECORD* event_record) {
    ImplClass::ProcessEventRecord(event_record);
  }
  static ULONG WINAPI ProcessBufferCallback(PEVENT_TRACE_LOGFILE buffer) {
    return ImplClass::ProcessBuffer(buffer);
  }
};

template <class ImplClass>
inline HRESULT EtwTraceConsumerBase<ImplClass>::OpenRealtimeSession(
    const wchar_t* session_name) {
  EVENT_TRACE_LOGFILE logfile = {
      .LoggerName = const_cast<wchar_t*>(session_name),
      .ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME,
  };
  return OpenSessionImpl(logfile);
}

template <class ImplClass>
inline HRESULT EtwTraceConsumerBase<ImplClass>::OpenFileSession(
    const wchar_t* file_name) {
  EVENT_TRACE_LOGFILE logfile = {.LogFileName =
                                     const_cast<wchar_t*>(file_name)};
  return OpenSessionImpl(logfile);
}

template <class ImplClass>
HRESULT EtwTraceConsumerBase<ImplClass>::OpenSessionImpl(
    EVENT_TRACE_LOGFILE& logfile) {
  if (ImplClass::kEnableRecordMode) {
    logfile.ProcessTraceMode |= PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile.EventRecordCallback = &ProcessEventRecordCallback;
  } else {
    logfile.EventCallback = &ProcessEventCallback;
  }
  if (ImplClass::kRawTimestamp) {
    logfile.ProcessTraceMode |= PROCESS_TRACE_MODE_RAW_TIMESTAMP;
  }
  logfile.BufferCallback = &ProcessBufferCallback;
  logfile.Context = this;
  TRACEHANDLE trace_handle = ::OpenTrace(&logfile);
  if (reinterpret_cast<TRACEHANDLE>(INVALID_HANDLE_VALUE) == trace_handle) {
    return HRESULT_FROM_WIN32(::GetLastError());
  }

  trace_handles_.push_back(trace_handle);
  return S_OK;
}

template <class ImplClass>
inline HRESULT EtwTraceConsumerBase<ImplClass>::Consume() {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);
  ULONG err = ::ProcessTrace(&trace_handles_[0],
                             static_cast<ULONG>(trace_handles_.size()), nullptr,
                             nullptr);
  return HRESULT_FROM_WIN32(err);
}

template <class ImplClass>
inline HRESULT EtwTraceConsumerBase<ImplClass>::Close() {
  HRESULT hr = S_OK;
  for (size_t i = 0; i < trace_handles_.size(); ++i) {
    if (NULL != trace_handles_[i]) {
      ULONG ret = ::CloseTrace(trace_handles_[i]);
      trace_handles_[i] = NULL;

      if (FAILED(HRESULT_FROM_WIN32(ret))) {
        hr = HRESULT_FROM_WIN32(ret);
      }
    }
  }
  trace_handles_.clear();

  return hr;
}

}  // namespace win
}  // namespace base

#endif  // BASE_WIN_EVENT_TRACE_CONSUMER_H_
