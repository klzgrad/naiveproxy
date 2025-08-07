/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef INCLUDE_PERFETTO_EXT_IPC_CLIENT_INFO_H_
#define INCLUDE_PERFETTO_EXT_IPC_CLIENT_INFO_H_

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/sys_types.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/ext/ipc/basic_types.h"

namespace perfetto {
namespace ipc {

// Passed to Service(s) to identify remote clients.
class ClientInfo {
 public:
  ClientInfo() = default;
  ClientInfo(ClientID client_id,
             uid_t uid,
             pid_t pid,
             base::MachineID machine_id)
      : client_id_(client_id), uid_(uid), pid_(pid), machine_id_(machine_id) {}

  bool operator==(const ClientInfo& other) const {
    return std::tie(client_id_, uid_, pid_, machine_id_) ==
           std::tie(other.client_id_, other.uid_, other.pid_,
                    other.machine_id_);
  }
  bool operator!=(const ClientInfo& other) const { return !(*this == other); }

  // For map<> and other sorted containers.
  bool operator<(const ClientInfo& other) const {
    PERFETTO_DCHECK(client_id_ != other.client_id_ || *this == other);
    return client_id_ < other.client_id_;
  }

  bool is_valid() const { return client_id_ != 0; }

  // A monotonic counter.
  ClientID client_id() const { return client_id_; }

  // Posix User ID. Comes from the kernel, can be trusted.
  uid_t uid() const { return uid_; }

  // Posix process ID. Comes from the kernel and can be trusted.
  int32_t pid() const { return pid_; }

  // An integral ID that identifies the machine the client is on.
  base::MachineID machine_id() const { return machine_id_; }

 private:
  ClientID client_id_ = 0;
  // The following fields are emitted to trace packets and should be kept in
  // sync with perfetto::ClientIdentity.
  uid_t uid_ = kInvalidUid;
  pid_t pid_ = base::kInvalidPid;
  base::MachineID machine_id_ = base::kDefaultMachineID;
};

}  // namespace ipc
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_EXT_IPC_CLIENT_INFO_H_
