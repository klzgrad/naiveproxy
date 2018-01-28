// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_GDIG_FILE_NET_LOG_H_
#define NET_TOOLS_GDIG_FILE_NET_LOG_H_

#include <string>

#include "base/synchronization/lock.h"
#include "base/time/time.h"
#include "net/log/net_log.h"

namespace net {

// FileNetLogObserver is a simple implementation of NetLog::ThreadSafeObserver
// that prints out all the events received into the stream passed
// to the constructor.
class FileNetLogObserver : public NetLog::ThreadSafeObserver {
 public:
  explicit FileNetLogObserver(FILE* destination);
  ~FileNetLogObserver() override;

  // NetLog::ThreadSafeObserver implementation:
  void OnAddEntry(const net::NetLogEntry& entry) override;

 private:
  FILE* const destination_;
  base::Lock lock_;

  base::Time first_event_time_;
};

}  // namespace net

#endif  // NET_TOOLS_GDIG_FILE_NET_LOG_H_
