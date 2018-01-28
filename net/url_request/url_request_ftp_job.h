// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_URL_REQUEST_URL_REQUEST_FTP_JOB_H_
#define NET_URL_REQUEST_URL_REQUEST_FTP_JOB_H_

#include <memory>
#include <string>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "net/base/auth.h"
#include "net/base/net_export.h"
#include "net/ftp/ftp_request_info.h"
#include "net/ftp/ftp_transaction.h"
#include "net/http/http_request_info.h"
#include "net/http/http_transaction.h"
#include "net/proxy/proxy_info.h"
#include "net/proxy/proxy_service.h"
#include "net/url_request/url_request_job.h"

namespace net {

class NetworkDelegate;
class FtpTransactionFactory;
class FtpAuthCache;

// A URLRequestJob subclass that is built on top of FtpTransaction. It
// provides an implementation for FTP.
class NET_EXPORT_PRIVATE URLRequestFtpJob : public URLRequestJob {
 public:
  URLRequestFtpJob(URLRequest* request,
                   NetworkDelegate* network_delegate,
                   FtpTransactionFactory* ftp_transaction_factory,
                   FtpAuthCache* ftp_auth_cache);

 protected:
  ~URLRequestFtpJob() override;

  // Overridden from URLRequestJob:
  bool IsSafeRedirect(const GURL& location) override;
  bool GetMimeType(std::string* mime_type) const override;
  void GetResponseInfo(HttpResponseInfo* info) override;
  HostPortPair GetSocketAddress() const override;
  void SetPriority(RequestPriority priority) override;
  void Start() override;
  void Kill() override;

  RequestPriority priority() const { return priority_; }

 private:
  class AuthData;

  void OnResolveProxyComplete(int result);

  void StartFtpTransaction();
  void StartHttpTransaction();

  void OnStartCompleted(int result);
  void OnStartCompletedAsync(int result);
  void OnReadCompleted(int result);

  void RestartTransactionWithAuth();

  void LogFtpServerType(char server_type);

  // Overridden from URLRequestJob:
  LoadState GetLoadState() const override;
  bool NeedsAuth() override;
  void GetAuthChallengeInfo(
      scoped_refptr<AuthChallengeInfo>* auth_info) override;
  void SetAuth(const AuthCredentials& credentials) override;
  void CancelAuth() override;

  int ReadRawData(IOBuffer* buf, int buf_size) override;

  void HandleAuthNeededResponse();

  RequestPriority priority_;

  ProxyService* proxy_service_;
  ProxyInfo proxy_info_;
  ProxyService::PacRequest* pac_request_;

  FtpRequestInfo ftp_request_info_;
  std::unique_ptr<FtpTransaction> ftp_transaction_;

  HttpRequestInfo http_request_info_;
  std::unique_ptr<HttpTransaction> http_transaction_;
  const HttpResponseInfo* http_response_info_;

  bool read_in_progress_;

  std::unique_ptr<AuthData> auth_data_;

  FtpTransactionFactory* ftp_transaction_factory_;
  FtpAuthCache* ftp_auth_cache_;

  base::WeakPtrFactory<URLRequestFtpJob> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(URLRequestFtpJob);
};

}  // namespace net

#endif  // NET_URL_REQUEST_URL_REQUEST_FTP_JOB_H_
