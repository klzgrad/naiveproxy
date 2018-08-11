// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/mac/mach_port_util.h"

#include "base/logging.h"

namespace base {

namespace {

// Struct for sending a complex Mach message.
struct MachSendComplexMessage {
  mach_msg_header_t header;
  mach_msg_body_t body;
  mach_msg_port_descriptor_t data;
};

// Struct for receiving a complex message.
struct MachReceiveComplexMessage {
  mach_msg_header_t header;
  mach_msg_body_t body;
  mach_msg_port_descriptor_t data;
  mach_msg_trailer_t trailer;
};

}  // namespace

kern_return_t SendMachPort(mach_port_t endpoint,
                           mach_port_t port_to_send,
                           int disposition) {
  MachSendComplexMessage send_msg;
  send_msg.header.msgh_bits =
      MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE, 0) | MACH_MSGH_BITS_COMPLEX;
  send_msg.header.msgh_size = sizeof(send_msg);
  send_msg.header.msgh_remote_port = endpoint;
  send_msg.header.msgh_local_port = MACH_PORT_NULL;
  send_msg.header.msgh_reserved = 0;
  send_msg.header.msgh_id = 0;
  send_msg.body.msgh_descriptor_count = 1;
  send_msg.data.name = port_to_send;
  send_msg.data.disposition = disposition;
  send_msg.data.type = MACH_MSG_PORT_DESCRIPTOR;

  kern_return_t kr =
      mach_msg(&send_msg.header, MACH_SEND_MSG | MACH_SEND_TIMEOUT,
               send_msg.header.msgh_size,
               0,                // receive limit
               MACH_PORT_NULL,   // receive name
               0,                // timeout
               MACH_PORT_NULL);  // notification port

  if (kr != KERN_SUCCESS)
    mach_port_deallocate(mach_task_self(), endpoint);

  return kr;
}

base::mac::ScopedMachSendRight ReceiveMachPort(mach_port_t port_to_listen_on) {
  MachReceiveComplexMessage recv_msg;
  mach_msg_header_t* recv_hdr = &recv_msg.header;
  recv_hdr->msgh_local_port = port_to_listen_on;
  recv_hdr->msgh_size = sizeof(recv_msg);

  kern_return_t kr =
      mach_msg(recv_hdr, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0,
               recv_hdr->msgh_size, port_to_listen_on, 0, MACH_PORT_NULL);
  if (kr != KERN_SUCCESS)
    return base::mac::ScopedMachSendRight(MACH_PORT_NULL);
  if (recv_msg.header.msgh_id != 0)
    return base::mac::ScopedMachSendRight(MACH_PORT_NULL);
  return base::mac::ScopedMachSendRight(recv_msg.data.name);
}

mach_port_name_t CreateIntermediateMachPort(
    mach_port_t task_port,
    base::mac::ScopedMachSendRight port_to_insert,
    MachCreateError* error_code) {
  DCHECK_NE(mach_task_self(), task_port);
  DCHECK_NE(static_cast<mach_port_name_t>(MACH_PORT_NULL), task_port);

  // Make a port with receive rights in the destination task.
  mach_port_name_t endpoint;
  kern_return_t kr =
      mach_port_allocate(task_port, MACH_PORT_RIGHT_RECEIVE, &endpoint);
  if (kr != KERN_SUCCESS) {
    if (error_code)
      *error_code = MachCreateError::ERROR_MAKE_RECEIVE_PORT;
    return MACH_PORT_NULL;
  }

  // Change its message queue limit so that it accepts one message.
  mach_port_limits limits = {};
  limits.mpl_qlimit = 1;
  kr = mach_port_set_attributes(task_port, endpoint, MACH_PORT_LIMITS_INFO,
                                reinterpret_cast<mach_port_info_t>(&limits),
                                MACH_PORT_LIMITS_INFO_COUNT);
  if (kr != KERN_SUCCESS) {
    if (error_code)
      *error_code = MachCreateError::ERROR_SET_ATTRIBUTES;
    mach_port_deallocate(task_port, endpoint);
    return MACH_PORT_NULL;
  }

  // Get a send right.
  mach_port_t send_once_right;
  mach_msg_type_name_t send_right_type;
  kr =
      mach_port_extract_right(task_port, endpoint, MACH_MSG_TYPE_MAKE_SEND_ONCE,
                              &send_once_right, &send_right_type);
  if (kr != KERN_SUCCESS) {
    if (error_code)
      *error_code = MachCreateError::ERROR_EXTRACT_DEST_RIGHT;
    mach_port_deallocate(task_port, endpoint);
    return MACH_PORT_NULL;
  }
  DCHECK_EQ(static_cast<mach_msg_type_name_t>(MACH_MSG_TYPE_PORT_SEND_ONCE),
            send_right_type);

  // This call takes ownership of |send_once_right|.
  kr = base::SendMachPort(
      send_once_right, port_to_insert.get(), MACH_MSG_TYPE_COPY_SEND);
  if (kr != KERN_SUCCESS) {
    if (error_code)
      *error_code = MachCreateError::ERROR_SEND_MACH_PORT;
    mach_port_deallocate(task_port, endpoint);
    return MACH_PORT_NULL;
  }

  // Endpoint is intentionally leaked into the destination task. An IPC must be
  // sent to the destination task so that it can clean up this port.
  return endpoint;
}

}  // namespace base
