# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

numOpenConnections = 0
numClosedConnections = 0


def web_socket_do_extra_handshake(request):
  global numOpenConnections
  numOpenConnections += 1


def web_socket_transfer_data(request):
  request.ws_stream.send_message('open: %d, closed: %d' %
      (numOpenConnections, numClosedConnections), binary=False)
  # Just waiting...
  request.ws_stream.receive_message()


def web_socket_passive_closing_handshake(request):
  global numOpenConnections
  global numClosedConnections
  numOpenConnections -= 1
  numClosedConnections += 1
  return (1000, '')
