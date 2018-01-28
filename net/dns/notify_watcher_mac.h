// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DNS_NOTIFY_WATCHER_MAC_H_
#define NET_DNS_NOTIFY_WATCHER_MAC_H_

#include "base/callback.h"
#include "base/macros.h"
#include "base/message_loop/message_loop.h"

namespace net {

// Watches for notifications from Libnotify and delivers them to a Callback.
// After failure the watch is cancelled and will have to be restarted.
class NotifyWatcherMac : public base::MessageLoopForIO::Watcher {
 public:
  // Called on received notification with true on success and false on error.
  typedef base::Callback<void(bool succeeded)> CallbackType;

  NotifyWatcherMac();

  // When deleted, automatically cancels.
  ~NotifyWatcherMac() override;

  // Registers for notifications for |key|. Returns true if succeeds. If so,
  // will deliver asynchronous notifications and errors to |callback|.
  bool Watch(const char* key, const CallbackType& callback);

  // Cancels the watch.
  void Cancel();

 private:
  // MessageLoopForIO::Watcher:
  void OnFileCanReadWithoutBlocking(int fd) override;
  void OnFileCanWriteWithoutBlocking(int fd) override {}

  int notify_fd_;
  int notify_token_;
  CallbackType callback_;
  base::MessageLoopForIO::FileDescriptorWatcher watcher_;

  DISALLOW_COPY_AND_ASSIGN(NotifyWatcherMac);
};

}  // namespace net

#endif  // NET_DNS_NOTIFY_WATCHER_MAC_H_
