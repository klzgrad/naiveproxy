# Copyright (c) 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# This handler serializes the received headers into a JSON string and sends it
# back to the client. In |headers_in|, the keys are converted to lower-case,
# while the original case is retained for the values.


import json


def web_socket_do_extra_handshake(request):
    pass


def web_socket_transfer_data(request):
    request.ws_stream.send_message(json.dumps(dict(request.headers_in.items())))
    # Wait for closing handshake
    request.ws_stream.receive_message()
