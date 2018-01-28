// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_FTP_FTP_NETWORK_LAYER_H_
#define NET_FTP_FTP_NETWORK_LAYER_H_

#include <memory>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "net/base/net_export.h"
#include "net/ftp/ftp_transaction_factory.h"

namespace net {

class FtpNetworkSession;
class HostResolver;

class NET_EXPORT FtpNetworkLayer : public FtpTransactionFactory {
 public:
  explicit FtpNetworkLayer(HostResolver* host_resolver);
  ~FtpNetworkLayer() override;

  // FtpTransactionFactory methods:
  std::unique_ptr<FtpTransaction> CreateTransaction() override;
  void Suspend(bool suspend) override;

 private:
  std::unique_ptr<FtpNetworkSession> session_;
  bool suspended_;
  DISALLOW_COPY_AND_ASSIGN(FtpNetworkLayer);
};

}  // namespace net

#endif  // NET_FTP_FTP_NETWORK_LAYER_H_
