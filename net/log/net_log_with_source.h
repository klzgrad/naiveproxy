// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_LOG_NET_LOG_WITH_SOURCE_H_
#define NET_LOG_NET_LOG_WITH_SOURCE_H_

#include "net/base/net_export.h"
#include "net/log/net_log_event_type.h"
#include "net/log/net_log_parameters_callback.h"
#include "net/log/net_log_source.h"
#include "net/log/net_log_source_type.h"

namespace net {

class NetLog;

// Helper that binds a Source to a NetLog, and exposes convenience methods to
// output log messages without needing to pass in the source.
class NET_EXPORT NetLogWithSource {
 public:
  NetLogWithSource() : net_log_(NULL) {}
  ~NetLogWithSource();

  // Add a log entry to the NetLog for the bound source.
  void AddEntry(NetLogEventType type, NetLogEventPhase phase) const;
  void AddEntry(NetLogEventType type,
                NetLogEventPhase phase,
                const NetLogParametersCallback& get_parameters) const;

  // Convenience methods that call AddEntry with a fixed "capture phase"
  // (begin, end, or none).
  void BeginEvent(NetLogEventType type) const;
  void BeginEvent(NetLogEventType type,
                  const NetLogParametersCallback& get_parameters) const;

  void EndEvent(NetLogEventType type) const;
  void EndEvent(NetLogEventType type,
                const NetLogParametersCallback& get_parameters) const;

  void AddEvent(NetLogEventType type) const;
  void AddEvent(NetLogEventType type,
                const NetLogParametersCallback& get_parameters) const;

  // Just like AddEvent, except |net_error| is a net error code.  A parameter
  // called "net_error" with the indicated value will be recorded for the event.
  // |net_error| must be negative, and not ERR_IO_PENDING, as it's not a true
  // error.
  void AddEventWithNetErrorCode(NetLogEventType event_type,
                                int net_error) const;

  // Just like EndEvent, except |net_error| is a net error code.  If it's
  // negative, a parameter called "net_error" with a value of |net_error| is
  // associated with the event.  Otherwise, the end event has no parameters.
  // |net_error| must not be ERR_IO_PENDING, as it's not a true error.
  void EndEventWithNetErrorCode(NetLogEventType event_type,
                                int net_error) const;

  // Logs a byte transfer event to the NetLog.  Determines whether to log the
  // received bytes or not based on the current logging level.
  void AddByteTransferEvent(NetLogEventType event_type,
                            int byte_count,
                            const char* bytes) const;

  bool IsCapturing() const;

  // Helper to create a NetLogWithSource given a NetLog and a NetLogSourceType.
  // Takes care of creating a unique source ID, and handles
  //  the case of NULL net_log.
  static NetLogWithSource Make(NetLog* net_log, NetLogSourceType source_type);

  // TODO(eroman): Temporary until crbug.com/467797 is solved.
  void CrashIfInvalid() const;

  const NetLogSource& source() const { return source_; }
  NetLog* net_log() const { return net_log_; }

 private:
  // TODO(eroman): Temporary until crbug.com/467797 is solved.
  enum Liveness {
    ALIVE = 0xCA11AB13,
    DEAD = 0xDEADBEEF,
  };

  NetLogWithSource(const NetLogSource& source, NetLog* net_log)
      : source_(source), net_log_(net_log) {}

  NetLogSource source_;
  NetLog* net_log_;

  // TODO(eroman): Temporary until crbug.com/467797 is solved.
  Liveness liveness_ = ALIVE;
};

}  // namespace net

#endif  // NET_LOG_NET_LOG_WITH_SOURCE_H_
