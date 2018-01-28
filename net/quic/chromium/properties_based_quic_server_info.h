// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CHROMIUM_PROPERTIES_BASED_QUIC_SERVER_INFO_H_
#define NET_QUIC_CHROMIUM_PROPERTIES_BASED_QUIC_SERVER_INFO_H_

#include <string>
#include <vector>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "net/base/completion_callback.h"
#include "net/quic/chromium/quic_server_info.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {

class HttpServerProperties;

// PropertiesBasedQuicServerInfo fetches information about a QUIC server from
// HttpServerProperties. Since the information is defined to be non-sensitive,
// it's ok for us to keep it on disk.
class QUIC_EXPORT_PRIVATE PropertiesBasedQuicServerInfo
    : public QuicServerInfo {
 public:
  PropertiesBasedQuicServerInfo(const QuicServerId& server_id,
                                HttpServerProperties* http_server_properties);
  ~PropertiesBasedQuicServerInfo() override;

  // QuicServerInfo implementation.
  bool Load() override;
  void Persist() override;
  size_t EstimateMemoryUsage() const override;

 private:
  HttpServerProperties* http_server_properties_;

  DISALLOW_COPY_AND_ASSIGN(PropertiesBasedQuicServerInfo);
};

}  // namespace net

#endif  // NET_QUIC_CHROMIUM_PROPERTIES_BASED_QUIC_SERVER_INFO_H_
