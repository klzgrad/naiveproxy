// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_FTP_FTP_TRANSACTION_FACTORY_H_
#define NET_FTP_FTP_TRANSACTION_FACTORY_H_

#include <memory>

#include "net/base/net_export.h"

namespace net {

class FtpTransaction;

// An interface to a class that can create FtpTransaction objects.
class NET_EXPORT FtpTransactionFactory {
 public:
  virtual ~FtpTransactionFactory() {}

  // Creates a FtpTransaction object.
  virtual std::unique_ptr<FtpTransaction> CreateTransaction() = 0;

  // Suspends the creation of new transactions. If |suspend| is false, creation
  // of new transactions is resumed.
  virtual void Suspend(bool suspend) = 0;
};

}  // namespace net

#endif  // NET_FTP_FTP_TRANSACTION_FACTORY_H_
