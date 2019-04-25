# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# The purpose of this test is to verify that the WebSocket handshake correctly
# ignores trailing whitespace on response headers.
# It is used by test case WebSocketEndToEndTest.TrailingWhitespace.

from mod_pywebsocket import handshake
from mod_pywebsocket.handshake.hybi import compute_accept


def web_socket_do_extra_handshake(request):
  accept = compute_accept(request.headers_in['Sec-WebSocket-Key'])[0]
  message = ('HTTP/1.1 101 Switching Protocols\r\n'
             'Upgrade: websocket\r\n'
             'Connection: Upgrade\r\n'
             'Sec-WebSocket-Accept: %s\r\n'
             'Sec-WebSocket-Protocol: sip \r\n'
             '\r\n' % accept)
  request.connection.write(message)
  # Prevent pywebsocket from sending its own handshake message.
  raise handshake.AbortedByUserException('Close the connection')


def web_socket_transfer_data(request):
  pass
