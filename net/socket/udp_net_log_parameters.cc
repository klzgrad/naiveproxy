// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/udp_net_log_parameters.h"

#include <utility>

#include "base/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "net/base/ip_endpoint.h"

namespace net {

namespace {

std::unique_ptr<base::Value> NetLogUDPDataTranferCallback(
    int byte_count,
    const char* bytes,
    const IPEndPoint* address,
    NetLogCaptureMode capture_mode) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  dict->SetInteger("byte_count", byte_count);
  if (capture_mode.include_socket_bytes())
    dict->SetString("hex_encoded_bytes", base::HexEncode(bytes, byte_count));
  if (address)
    dict->SetString("address", address->ToString());
  return std::move(dict);
}

std::unique_ptr<base::Value> NetLogUDPConnectCallback(
    const IPEndPoint* address,
    NetworkChangeNotifier::NetworkHandle network,
    NetLogCaptureMode /* capture_mode */) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  dict->SetString("address", address->ToString());
  if (network != NetworkChangeNotifier::kInvalidNetworkHandle)
    dict->SetInteger("bound_to_network", network);
  return std::move(dict);
}

}  // namespace

NetLogParametersCallback CreateNetLogUDPDataTranferCallback(
    int byte_count,
    const char* bytes,
    const IPEndPoint* address) {
  DCHECK(bytes);
  return base::Bind(&NetLogUDPDataTranferCallback, byte_count, bytes, address);
}

NetLogParametersCallback CreateNetLogUDPConnectCallback(
    const IPEndPoint* address,
    NetworkChangeNotifier::NetworkHandle network) {
  DCHECK(address);
  return base::Bind(&NetLogUDPConnectCallback, address, network);
}

}  // namespace net
