// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_FAILING_HTTP_TRANSACTION_FACTORY_H_
#define NET_HTTP_FAILING_HTTP_TRANSACTION_FACTORY_H_

#include <memory>

#include "net/base/net_errors.h"
#include "net/base/net_export.h"
#include "net/base/request_priority.h"
#include "net/http/http_transaction.h"
#include "net/http/http_transaction_factory.h"

namespace net {

class HttpCache;
class HttpNetworkSession;

// Creates transactions that always (asynchronously) return a specified
// error.  The error is returned asynchronously, just after the transaction is
// started.
class NET_EXPORT FailingHttpTransactionFactory : public HttpTransactionFactory {
 public:
  // The caller must guarantee that |session| outlives this object.
  FailingHttpTransactionFactory(HttpNetworkSession* session, Error error);
  ~FailingHttpTransactionFactory() override;

  // HttpTransactionFactory:
  int CreateTransaction(RequestPriority priority,
                        std::unique_ptr<HttpTransaction>* trans) override;
  HttpCache* GetCache() override;
  HttpNetworkSession* GetSession() override;

 private:
  HttpNetworkSession* session_;
  Error error_;
};

}  // namespace net

#endif  // NET_HTTP_FAILING_HTTP_TRANSACTION_FACTORY_H_
