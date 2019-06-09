// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MAC_MACH_PORT_BROKER_H_
#define BASE_MAC_MACH_PORT_BROKER_H_

#include <mach/mach.h>

#include <map>
#include <memory>
#include <string>

#include "base/base_export.h"
#include "base/mac/dispatch_source_mach.h"
#include "base/mac/scoped_mach_port.h"
#include "base/macros.h"
#include "base/process/port_provider_mac.h"
#include "base/process/process_handle.h"
#include "base/synchronization/lock.h"

namespace base {

// On OS X, the task port of a process is required to collect metrics about the
// process, and to insert Mach ports into the process. Running |task_for_pid()|
// is only allowed for privileged code. However, a process has port rights to
// all its subprocesses, so let the child processes send their Mach port to the
// parent over IPC.
//
// Mach ports can only be sent over Mach IPC, not over the |socketpair()| that
// the regular IPC system uses. Hence, the child processes opens a Mach
// connection shortly after launching and ipc their mach data to the parent
// process. A single |MachPortBroker| with a given name is expected to exist in
// the parent process.
//
// Since this data arrives over a separate channel, it is not available
// immediately after a child process has been started.
class BASE_EXPORT MachPortBroker : public base::PortProvider {
 public:
  // For use in child processes. This will send the task port of the current
  // process over Mach IPC to the port registered by name (via this class) in
  // the parent process. Returns true if the message was sent successfully
  // and false if otherwise.
  static bool ChildSendTaskPortToParent(const std::string& name);

  // Returns the Mach port name to use when sending or receiving messages.
  // Does the Right Thing in the browser and in child processes.
  static std::string GetMachPortName(const std::string& name, bool is_child);

  MachPortBroker(const std::string& name);
  ~MachPortBroker() override;

  // Performs any initialization work.
  bool Init();

  // Adds a placeholder to the map for the given pid with MACH_PORT_NULL.
  // Callers are expected to later update the port with FinalizePid(). Callers
  // MUST acquire the lock given by GetLock() before calling this method (and
  // release the lock afterwards).
  void AddPlaceholderForPid(base::ProcessHandle pid);

  // Removes |pid| from the task port map. Callers MUST acquire the lock given
  // by GetLock() before calling this method (and release the lock afterwards).
  void InvalidatePid(base::ProcessHandle pid);

  // The lock that protects this MachPortBroker object. Callers MUST acquire
  // and release this lock around calls to AddPlaceholderForPid(),
  // InvalidatePid(), and FinalizePid();
  base::Lock& GetLock() { return lock_; }

  // Implement |base::PortProvider|.
  mach_port_t TaskForPid(base::ProcessHandle process) const override;

 private:
  friend class MachPortBrokerTest;

  // Message handler that is invoked on |dispatch_source_| when an
  // incoming message needs to be received.
  void HandleRequest();

  // Updates the mapping for |pid| to include the given |mach_info|. Does
  // nothing if PlaceholderForPid() has not already been called for the given
  // |pid|. Callers MUST acquire the lock given by GetLock() before calling
  // this method (and release the lock afterwards). Returns true if the port
  // was accepeted for the PID, or false if it was rejected (e.g. due to an
  // unknown sender).
  bool FinalizePid(base::ProcessHandle pid, mach_port_t task_port);

  // Name used to identify a particular port broker.
  const std::string name_;

  // The Mach port on which the server listens.
  base::mac::ScopedMachReceiveRight server_port_;

  // The dispatch source and queue on which Mach messages will be received.
  std::unique_ptr<base::DispatchSourceMach> dispatch_source_;

  // Stores mach info for every process in the broker.
  typedef std::map<base::ProcessHandle, mach_port_t> MachMap;
  MachMap mach_map_;

  // Mutex that guards |mach_map_|.
  mutable base::Lock lock_;

  DISALLOW_COPY_AND_ASSIGN(MachPortBroker);
};

}  // namespace base

#endif  // BASE_MAC_MACH_PORT_BROKER_H_
