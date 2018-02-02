// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MAC_MACH_PORT_UTIL_H_
#define BASE_MAC_MACH_PORT_UTIL_H_

#include <mach/mach.h>

#include "base/base_export.h"
#include "base/mac/scoped_mach_port.h"

namespace base {

enum class MachCreateError {
    ERROR_MAKE_RECEIVE_PORT,
    ERROR_SET_ATTRIBUTES,
    ERROR_EXTRACT_DEST_RIGHT,
    ERROR_SEND_MACH_PORT,
};

// Sends a Mach port to |dest_port|. Assumes that |dest_port| is a send once
// right. Takes ownership of |dest_port|.
BASE_EXPORT kern_return_t SendMachPort(mach_port_t dest_port,
                                       mach_port_t port_to_send,
                                       int disposition);

// Receives a Mach port from |port_to_listen_on|, which should have exactly one
// queued message. Returns |MACH_PORT_NULL| on any error.
BASE_EXPORT base::mac::ScopedMachSendRight ReceiveMachPort(
    mach_port_t port_to_listen_on);

// Creates an intermediate Mach port in |task_port| and sends |port_to_insert|
// as a mach_msg to the intermediate Mach port.
// |task_port| is the task port of another process.
// |port_to_insert| must be a send right in the current task's name space.
// Returns the intermediate port on success, and MACH_PORT_NULL on failure.
// On failure, |error_code| is set if not null.
// This method takes ownership of |port_to_insert|. On success, ownership is
// passed to the intermediate Mach port.
BASE_EXPORT mach_port_name_t CreateIntermediateMachPort(
    mach_port_t task_port,
    base::mac::ScopedMachSendRight port_to_insert,
    MachCreateError* error_code);

}  // namespace base

#endif  // BASE_MAC_MACH_PORT_UTIL_H_
