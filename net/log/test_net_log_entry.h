// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_LOG_TEST_NET_LOG_ENTRY_H_
#define NET_LOG_TEST_NET_LOG_ENTRY_H_

#include <memory>
#include <string>
#include <vector>

#include "base/time/time.h"
#include "net/log/net_log_event_type.h"
#include "net/log/net_log_source.h"

namespace base {
class DictionaryValue;
class ListValue;
}

namespace net {

// TestNetLogEntry is much like NetLogEntry, except it has its own copy of all
// log data, so a list of entries can be gathered over the course of a test, and
// then inspected at the end.  It is intended for testing only, and is part of
// the net_test_support project.
struct TestNetLogEntry {
  // Ordered set of logged entries.
  typedef std::vector<TestNetLogEntry> List;

  TestNetLogEntry(NetLogEventType type,
                  const base::TimeTicks& time,
                  NetLogSource source,
                  NetLogEventPhase phase,
                  std::unique_ptr<base::DictionaryValue> params);
  // Copy constructor needed to store in a std::vector because of the
  // scoped_ptr.
  TestNetLogEntry(const TestNetLogEntry& entry);

  ~TestNetLogEntry();

  // Equality operator needed to store in a std::vector because of the
  // scoped_ptr.
  TestNetLogEntry& operator=(const TestNetLogEntry& entry);

  // Attempt to retrieve an value of the specified type with the given name
  // from |params|.  Returns true on success, false on failure.  Does not
  // modify |value| on failure.
  bool GetStringValue(const std::string& name, std::string* value) const;
  bool GetIntegerValue(const std::string& name, int* value) const;
  bool GetBooleanValue(const std::string& name, bool* value) const;
  bool GetListValue(const std::string& name, base::ListValue** value) const;

  // Same as GetIntegerValue, but returns the error code associated with a
  // log entry.
  bool GetNetErrorCode(int* value) const;

  // Returns the parameters as a JSON string, or empty string if there are no
  // parameters.
  std::string GetParamsJson() const;

  NetLogEventType type;
  base::TimeTicks time;
  NetLogSource source;
  NetLogEventPhase phase;
  std::unique_ptr<base::DictionaryValue> params;
};

}  // namespace net

#endif  // NET_LOG_TEST_NET_LOG_ENTRY_H_
