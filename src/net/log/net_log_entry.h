// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_LOG_NET_LOG_ENTRY_H_
#define NET_LOG_NET_LOG_ENTRY_H_

#include <memory>

#include "base/macros.h"
#include "base/time/time.h"
#include "net/base/net_export.h"
#include "net/log/net_log_capture_mode.h"
#include "net/log/net_log_event_type.h"
#include "net/log/net_log_parameters_callback.h"
#include "net/log/net_log_source.h"

namespace base {
class Value;
}

namespace net {

struct NET_EXPORT NetLogEntryData {
  NetLogEntryData(NetLogEventType type,
                  NetLogSource source,
                  NetLogEventPhase phase,
                  base::TimeTicks time,
                  const NetLogParametersCallback* parameters_callback);
  ~NetLogEntryData();

  const NetLogEventType type;
  const NetLogSource source;
  const NetLogEventPhase phase;
  const base::TimeTicks time;
  const NetLogParametersCallback* const parameters_callback;
};

// A NetLogEntry pre-binds NetLogEntryData to a capture mode, so observers will
// observe the output of ToValue() and ParametersToValue() at their log
// capture mode rather than the current maximum.
class NET_EXPORT NetLogEntry {
 public:
  NetLogEntry(const NetLogEntryData* data, NetLogCaptureMode capture_mode);
  ~NetLogEntry();

  NetLogEventType type() const { return data_->type; }
  NetLogSource source() const { return data_->source; }
  NetLogEventPhase phase() const { return data_->phase; }

  // Serializes the specified event to a Value.  The Value also includes the
  // current time.  Takes in a time to allow back-dating entries.
  std::unique_ptr<base::Value> ToValue() const;

  // Returns the parameters as a Value.  Returns nullptr if there are no
  // parameters.
  std::unique_ptr<base::Value> ParametersToValue() const;

 private:
  const NetLogEntryData* const data_;

  // Log capture mode when the event occurred.
  const NetLogCaptureMode capture_mode_;

  // It is not safe to copy this class, since |parameters_callback_| may
  // include pointers that become stale immediately after the event is added,
  // even if the code were modified to keep its own copy of the callback.
  DISALLOW_COPY_AND_ASSIGN(NetLogEntry);
};

}  // namespace net

#endif  // NET_LOG_NET_LOG_ENTRY_H_
