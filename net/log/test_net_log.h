// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_LOG_TEST_NET_LOG_H_
#define NET_LOG_TEST_NET_LOG_H_

#include <stddef.h>

#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "net/log/net_log.h"
#include "net/log/net_log_with_source.h"
#include "net/log/test_net_log_entry.h"

namespace net {

class NetLogCaptureMode;
struct NetLogSource;

// TestNetLog is NetLog subclass which records all NetLog events that occur and
// their parameters.  It is intended for testing only, and is part of the
// net_test_support project.
class TestNetLog : public NetLog {
 public:
  TestNetLog();
  ~TestNetLog() override;

  void SetCaptureMode(NetLogCaptureMode capture_mode);

  // Below methods are forwarded to test_net_log_observer_.
  void GetEntries(TestNetLogEntry::List* entry_list) const;
  void GetEntriesForSource(NetLogSource source,
                           TestNetLogEntry::List* entry_list) const;
  size_t GetSize() const;
  void Clear();

  // Returns a NetLog observer that will write entries to the TestNetLog's event
  // store. For testing code that bypasses NetLogs and adds events directly to
  // an observer.
  NetLog::ThreadSafeObserver* GetObserver() const;

 private:
  // The underlying observer class that does all the work.
  class Observer;

  std::unique_ptr<Observer> observer_;

  DISALLOW_COPY_AND_ASSIGN(TestNetLog);
};

// Helper class that exposes a similar API as NetLogWithSource, but uses a
// TestNetLog rather than the more generic NetLog.
//
// A BoundTestNetLog can easily be converted to a NetLogWithSource using the
// bound() method.
class BoundTestNetLog {
 public:
  BoundTestNetLog();
  ~BoundTestNetLog();

  // The returned NetLogWithSource is only valid while |this| is alive.
  NetLogWithSource bound() const { return net_log_; }

  // Fills |entry_list| with all entries in the log.
  void GetEntries(TestNetLogEntry::List* entry_list) const;

  // Fills |entry_list| with all entries in the log from the specified Source.
  void GetEntriesForSource(NetLogSource source,
                           TestNetLogEntry::List* entry_list) const;

  // Returns number of entries in the log.
  size_t GetSize() const;

  void Clear();

  // Sets the capture mode of the underlying TestNetLog.
  void SetCaptureMode(NetLogCaptureMode capture_mode);

 private:
  TestNetLog test_net_log_;
  const NetLogWithSource net_log_;

  DISALLOW_COPY_AND_ASSIGN(BoundTestNetLog);
};

}  // namespace net

#endif  // NET_LOG_TEST_NET_LOG_H_
