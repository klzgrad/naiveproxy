// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/mac/mach_port_broker.h"

#include <bsm/libbsm.h>
#include <servers/bootstrap.h>

#include "base/logging.h"
#include "base/mac/foundation_util.h"
#include "base/mac/mach_logging.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"

namespace base {

namespace {

// Mach message structure used in the child as a sending message.
struct MachPortBroker_ChildSendMsg {
  mach_msg_header_t header;
  mach_msg_body_t body;
  mach_msg_port_descriptor_t child_task_port;
};

// Complement to the ChildSendMsg, this is used in the parent for receiving
// a message. Contains a message trailer with audit information.
struct MachPortBroker_ParentRecvMsg : public MachPortBroker_ChildSendMsg {
  mach_msg_audit_trailer_t trailer;
};

}  // namespace

// static
bool MachPortBroker::ChildSendTaskPortToParent(const std::string& name) {
  // Look up the named MachPortBroker port that's been registered with the
  // bootstrap server.
  mach_port_t parent_port;
  kern_return_t kr = bootstrap_look_up(bootstrap_port,
      const_cast<char*>(GetMachPortName(name, true).c_str()), &parent_port);
  if (kr != KERN_SUCCESS) {
    BOOTSTRAP_LOG(ERROR, kr) << "bootstrap_look_up";
    return false;
  }
  base::mac::ScopedMachSendRight scoped_right(parent_port);

  // Create the check in message. This will copy a send right on this process'
  // (the child's) task port and send it to the parent.
  MachPortBroker_ChildSendMsg msg;
  bzero(&msg, sizeof(msg));
  msg.header.msgh_bits = MACH_MSGH_BITS_REMOTE(MACH_MSG_TYPE_COPY_SEND) |
                         MACH_MSGH_BITS_COMPLEX;
  msg.header.msgh_remote_port = parent_port;
  msg.header.msgh_size = sizeof(msg);
  msg.body.msgh_descriptor_count = 1;
  msg.child_task_port.name = mach_task_self();
  msg.child_task_port.disposition = MACH_MSG_TYPE_PORT_SEND;
  msg.child_task_port.type = MACH_MSG_PORT_DESCRIPTOR;

  kr = mach_msg(&msg.header, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof(msg),
      0, MACH_PORT_NULL, 100 /*milliseconds*/, MACH_PORT_NULL);
  if (kr != KERN_SUCCESS) {
    MACH_LOG(ERROR, kr) << "mach_msg";
    return false;
  }

  return true;
}

// static
std::string MachPortBroker::GetMachPortName(const std::string& name,
                                            bool is_child) {
  // In child processes, use the parent's pid.
  const pid_t pid = is_child ? getppid() : getpid();
  return base::StringPrintf(
      "%s.%s.%d", base::mac::BaseBundleID(), name.c_str(), pid);
}

mach_port_t MachPortBroker::TaskForPid(base::ProcessHandle pid) const {
  base::AutoLock lock(lock_);
  MachPortBroker::MachMap::const_iterator it = mach_map_.find(pid);
  if (it == mach_map_.end())
    return MACH_PORT_NULL;
  return it->second;
}

MachPortBroker::MachPortBroker(const std::string& name) : name_(name) {}

MachPortBroker::~MachPortBroker() {}

bool MachPortBroker::Init() {
  DCHECK(server_port_.get() == MACH_PORT_NULL);

  // Check in with launchd and publish the service name.
  mach_port_t port;
  kern_return_t kr = bootstrap_check_in(
      bootstrap_port, GetMachPortName(name_, false).c_str(), &port);
  if (kr != KERN_SUCCESS) {
    BOOTSTRAP_LOG(ERROR, kr) << "bootstrap_check_in";
    return false;
  }
  server_port_.reset(port);

  // Start the dispatch source.
  std::string queue_name =
      base::StringPrintf("%s.MachPortBroker", base::mac::BaseBundleID());
  dispatch_source_.reset(new base::DispatchSourceMach(
      queue_name.c_str(), server_port_.get(), ^{ HandleRequest(); }));
  dispatch_source_->Resume();

  return true;
}

void MachPortBroker::AddPlaceholderForPid(base::ProcessHandle pid) {
  lock_.AssertAcquired();
  DCHECK_EQ(0u, mach_map_.count(pid));
  mach_map_[pid] = MACH_PORT_NULL;
}

void MachPortBroker::InvalidatePid(base::ProcessHandle pid) {
  lock_.AssertAcquired();

  MachMap::iterator mach_it = mach_map_.find(pid);
  if (mach_it != mach_map_.end()) {
    kern_return_t kr = mach_port_deallocate(mach_task_self(), mach_it->second);
    MACH_LOG_IF(WARNING, kr != KERN_SUCCESS, kr) << "mach_port_deallocate";
    mach_map_.erase(mach_it);
  }
}

void MachPortBroker::HandleRequest() {
  MachPortBroker_ParentRecvMsg msg;
  bzero(&msg, sizeof(msg));
  msg.header.msgh_size = sizeof(msg);
  msg.header.msgh_local_port = server_port_.get();

  const mach_msg_option_t options = MACH_RCV_MSG |
      MACH_RCV_TRAILER_TYPE(MACH_RCV_TRAILER_AUDIT) |
      MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AUDIT);

  kern_return_t kr = mach_msg(&msg.header,
                              options,
                              0,
                              sizeof(msg),
                              server_port_.get(),
                              MACH_MSG_TIMEOUT_NONE,
                              MACH_PORT_NULL);
  if (kr != KERN_SUCCESS) {
    MACH_LOG(ERROR, kr) << "mach_msg";
    return;
  }

  // Use the kernel audit information to make sure this message is from
  // a task that this process spawned. The kernel audit token contains the
  // unspoofable pid of the task that sent the message.
  pid_t child_pid = audit_token_to_pid(msg.trailer.msgh_audit);
  mach_port_t child_task_port = msg.child_task_port.name;

  // Take the lock and update the broker information.
  {
    base::AutoLock lock(lock_);
    FinalizePid(child_pid, child_task_port);
  }
  NotifyObservers(child_pid);
}

void MachPortBroker::FinalizePid(base::ProcessHandle pid,
                                 mach_port_t task_port) {
  lock_.AssertAcquired();

  MachMap::iterator it = mach_map_.find(pid);
  if (it == mach_map_.end()) {
    // Do nothing for unknown pids.
    LOG(ERROR) << "Unknown process " << pid << " is sending Mach IPC messages!";
    return;
  }

  DCHECK(it->second == MACH_PORT_NULL);
  if (it->second == MACH_PORT_NULL)
    it->second = task_port;
}

}  // namespace base
