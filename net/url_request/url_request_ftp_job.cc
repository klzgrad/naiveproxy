// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/url_request/url_request_ftp_job.h"

#include "base/compiler_specific.h"
#include "base/location.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "net/base/auth.h"
#include "net/base/host_port_pair.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/ftp/ftp_auth_cache.h"
#include "net/ftp/ftp_response_info.h"
#include "net/ftp/ftp_transaction_factory.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_transaction_factory.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_error_job.h"

namespace net {

class URLRequestFtpJob::AuthData {
 public:
  AuthState state;  // Whether we need, have, or gave up on authentication.
  AuthCredentials credentials;  // The credentials to use for auth.

  AuthData();
  ~AuthData();
};

URLRequestFtpJob::AuthData::AuthData() : state(AUTH_STATE_NEED_AUTH) {}

URLRequestFtpJob::AuthData::~AuthData() = default;

URLRequestFtpJob::URLRequestFtpJob(
    URLRequest* request,
    NetworkDelegate* network_delegate,
    FtpTransactionFactory* ftp_transaction_factory,
    FtpAuthCache* ftp_auth_cache)
    : URLRequestJob(request, network_delegate),
      priority_(DEFAULT_PRIORITY),
      proxy_resolution_service_(
          request_->context()->proxy_resolution_service()),
      proxy_resolve_request_(NULL),
      http_response_info_(NULL),
      read_in_progress_(false),
      ftp_transaction_factory_(ftp_transaction_factory),
      ftp_auth_cache_(ftp_auth_cache),
      weak_factory_(this) {
  DCHECK(proxy_resolution_service_);
  DCHECK(ftp_transaction_factory);
  DCHECK(ftp_auth_cache);
}

URLRequestFtpJob::~URLRequestFtpJob() {
  Kill();
}

bool URLRequestFtpJob::IsSafeRedirect(const GURL& location) {
  // Disallow all redirects.
  return false;
}

bool URLRequestFtpJob::GetMimeType(std::string* mime_type) const {
  if (proxy_info_.is_direct()) {
    if (ftp_transaction_->GetResponseInfo()->is_directory_listing) {
      *mime_type = "text/vnd.chromium.ftp-dir";
      return true;
    }
  } else {
    // No special handling of MIME type is needed. As opposed to direct FTP
    // transaction, we do not get a raw directory listing to parse.
    return http_transaction_->GetResponseInfo()->
        headers->GetMimeType(mime_type);
  }
  return false;
}

void URLRequestFtpJob::GetResponseInfo(HttpResponseInfo* info) {
  if (http_response_info_)
    *info = *http_response_info_;
}

HostPortPair URLRequestFtpJob::GetSocketAddress() const {
  if (proxy_info_.is_direct()) {
    if (!ftp_transaction_)
      return HostPortPair();
    return ftp_transaction_->GetResponseInfo()->socket_address;
  } else {
    if (!http_transaction_)
      return HostPortPair();
    return http_transaction_->GetResponseInfo()->socket_address;
  }
}

void URLRequestFtpJob::SetPriority(RequestPriority priority) {
  priority_ = priority;
  if (http_transaction_)
    http_transaction_->SetPriority(priority);
}

void URLRequestFtpJob::Start() {
  DCHECK(!proxy_resolve_request_);
  DCHECK(!ftp_transaction_);
  DCHECK(!http_transaction_);

  int rv = OK;
  if (request_->load_flags() & LOAD_BYPASS_PROXY) {
    proxy_info_.UseDirect();
  } else {
    DCHECK_EQ(request_->context()->proxy_resolution_service(),
              proxy_resolution_service_);
    rv = proxy_resolution_service_->ResolveProxy(
        request_->url(), "GET", &proxy_info_,
        base::Bind(&URLRequestFtpJob::OnResolveProxyComplete,
                   base::Unretained(this)),
        &proxy_resolve_request_, NULL, request_->net_log());

    if (rv == ERR_IO_PENDING)
      return;
  }
  OnResolveProxyComplete(rv);
}

void URLRequestFtpJob::Kill() {
  if (proxy_resolve_request_) {
    proxy_resolution_service_->CancelRequest(proxy_resolve_request_);
    proxy_resolve_request_ = nullptr;
  }
  if (ftp_transaction_)
    ftp_transaction_.reset();
  if (http_transaction_)
    http_transaction_.reset();
  URLRequestJob::Kill();
  weak_factory_.InvalidateWeakPtrs();
}

void URLRequestFtpJob::OnResolveProxyComplete(int result) {
  proxy_resolve_request_ = NULL;

  if (result != OK) {
    OnStartCompletedAsync(result);
    return;
  }

  // Remove unsupported proxies from the list.
  proxy_info_.RemoveProxiesWithoutScheme(
      ProxyServer::SCHEME_DIRECT |
      ProxyServer::SCHEME_HTTP |
      ProxyServer::SCHEME_HTTPS);

  // TODO(phajdan.jr): Implement proxy fallback, http://crbug.com/171495 .
  if (proxy_info_.is_direct())
    StartFtpTransaction();
  else if (proxy_info_.is_http() || proxy_info_.is_https())
    StartHttpTransaction();
  else
    OnStartCompletedAsync(ERR_NO_SUPPORTED_PROXIES);
}

void URLRequestFtpJob::StartFtpTransaction() {
  // Create a transaction.
  DCHECK(!ftp_transaction_);

  ftp_request_info_.url = request_->url();
  ftp_transaction_ = ftp_transaction_factory_->CreateTransaction();

  int rv;
  if (ftp_transaction_) {
    rv = ftp_transaction_->Start(
        &ftp_request_info_,
        base::Bind(&URLRequestFtpJob::OnStartCompleted, base::Unretained(this)),
        request_->net_log(), request_->traffic_annotation());
    if (rv == ERR_IO_PENDING)
      return;
  } else {
    rv = ERR_FAILED;
  }
  // The transaction started synchronously, but we need to notify the
  // URLRequest delegate via the message loop.
  OnStartCompletedAsync(rv);
}

void URLRequestFtpJob::StartHttpTransaction() {
  // Create a transaction.
  DCHECK(!http_transaction_);

  // Do not cache FTP responses sent through HTTP proxy.
  request_->SetLoadFlags(request_->load_flags() |
                         LOAD_DISABLE_CACHE |
                         LOAD_DO_NOT_SAVE_COOKIES |
                         LOAD_DO_NOT_SEND_COOKIES);

  http_request_info_.url = request_->url();
  http_request_info_.method = request_->method();
  http_request_info_.load_flags = request_->load_flags();
  http_request_info_.traffic_annotation =
      net::MutableNetworkTrafficAnnotationTag(request_->traffic_annotation());

  int rv = request_->context()->http_transaction_factory()->CreateTransaction(
      priority_, &http_transaction_);
  if (rv == OK) {
    rv = http_transaction_->Start(
        &http_request_info_,
        base::Bind(&URLRequestFtpJob::OnStartCompleted,
                  base::Unretained(this)),
        request_->net_log());
    if (rv == ERR_IO_PENDING)
      return;
  }
  // The transaction started synchronously, but we need to notify the
  // URLRequest delegate via the message loop.
  OnStartCompletedAsync(rv);
}

void URLRequestFtpJob::OnStartCompleted(int result) {
  // Note that ftp_transaction_ may be NULL due to a creation failure.
  if (ftp_transaction_) {
    // FTP obviously doesn't have HTTP Content-Length header. We have to pass
    // the content size information manually.
    set_expected_content_size(
        ftp_transaction_->GetResponseInfo()->expected_content_size);
  }

  if (result == OK) {
    if (http_transaction_) {
      http_response_info_ = http_transaction_->GetResponseInfo();
      SetProxyServer(http_response_info_->proxy_server);

      if (http_response_info_->headers->response_code() == 401 ||
          http_response_info_->headers->response_code() == 407) {
        HandleAuthNeededResponse();
        return;
      }
    }
    NotifyHeadersComplete();
  } else if (ftp_transaction_ &&
             ftp_transaction_->GetResponseInfo()->needs_auth) {
    HandleAuthNeededResponse();
    return;
  } else {
    NotifyStartError(URLRequestStatus(URLRequestStatus::FAILED, result));
  }
}

void URLRequestFtpJob::OnStartCompletedAsync(int result) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(&URLRequestFtpJob::OnStartCompleted,
                            weak_factory_.GetWeakPtr(), result));
}

void URLRequestFtpJob::OnReadCompleted(int result) {
  read_in_progress_ = false;
  ReadRawDataComplete(result);
}

void URLRequestFtpJob::RestartTransactionWithAuth() {
  DCHECK(auth_data_.get() && auth_data_->state == AUTH_STATE_HAVE_AUTH);

  int rv;
  if (proxy_info_.is_direct()) {
    rv = ftp_transaction_->RestartWithAuth(
        auth_data_->credentials,
        base::Bind(&URLRequestFtpJob::OnStartCompleted,
                   base::Unretained(this)));
  } else {
    rv = http_transaction_->RestartWithAuth(
        auth_data_->credentials,
        base::Bind(&URLRequestFtpJob::OnStartCompleted,
                   base::Unretained(this)));
  }
  if (rv == ERR_IO_PENDING)
    return;

  OnStartCompletedAsync(rv);
}

LoadState URLRequestFtpJob::GetLoadState() const {
  if (proxy_resolve_request_)
    return proxy_resolution_service_->GetLoadState(proxy_resolve_request_);
  if (proxy_info_.is_direct()) {
    return ftp_transaction_ ?
        ftp_transaction_->GetLoadState() : LOAD_STATE_IDLE;
  } else {
    return http_transaction_ ?
        http_transaction_->GetLoadState() : LOAD_STATE_IDLE;
  }
}

bool URLRequestFtpJob::NeedsAuth() {
  return auth_data_.get() && auth_data_->state == AUTH_STATE_NEED_AUTH;
}

void URLRequestFtpJob::GetAuthChallengeInfo(
    scoped_refptr<AuthChallengeInfo>* result) {
  DCHECK(NeedsAuth());

  if (http_response_info_) {
    *result = http_response_info_->auth_challenge;
    return;
  }

  scoped_refptr<AuthChallengeInfo> auth_info(new AuthChallengeInfo);
  auth_info->is_proxy = false;
  auth_info->challenger = url::Origin::Create(request_->url());
  // scheme and realm are kept empty.
  DCHECK(auth_info->scheme.empty());
  DCHECK(auth_info->realm.empty());
  result->swap(auth_info);
}

void URLRequestFtpJob::SetAuth(const AuthCredentials& credentials) {
  DCHECK(ftp_transaction_ || http_transaction_);
  DCHECK(NeedsAuth());

  auth_data_->state = AUTH_STATE_HAVE_AUTH;
  auth_data_->credentials = credentials;

  if (ftp_transaction_) {
    ftp_auth_cache_->Add(request_->url().GetOrigin(),
                         auth_data_->credentials);
  }

  RestartTransactionWithAuth();
}

void URLRequestFtpJob::CancelAuth() {
  DCHECK(ftp_transaction_ || http_transaction_);
  DCHECK(NeedsAuth());

  auth_data_->state = AUTH_STATE_CANCELED;

  // Once the auth is cancelled, we proceed with the request as though
  // there were no auth.  Schedule this for later so that we don't cause
  // any recursing into the caller as a result of this call.
  OnStartCompletedAsync(OK);
}

int URLRequestFtpJob::ReadRawData(IOBuffer* buf, int buf_size) {
  DCHECK_NE(buf_size, 0);
  DCHECK(!read_in_progress_);

  int rv;

  if (proxy_info_.is_direct()) {
    rv = ftp_transaction_->Read(buf, buf_size,
                                base::Bind(&URLRequestFtpJob::OnReadCompleted,
                                           base::Unretained(this)));
  } else {
    rv = http_transaction_->Read(buf, buf_size,
                                 base::Bind(&URLRequestFtpJob::OnReadCompleted,
                                            base::Unretained(this)));
  }

  if (rv == ERR_IO_PENDING)
    read_in_progress_ = true;
  return rv;
}

void URLRequestFtpJob::HandleAuthNeededResponse() {
  GURL origin = request_->url().GetOrigin();

  if (auth_data_.get()) {
    if (auth_data_->state == AUTH_STATE_CANCELED) {
      NotifyHeadersComplete();
      return;
    }

    if (ftp_transaction_ && auth_data_->state == AUTH_STATE_HAVE_AUTH)
      ftp_auth_cache_->Remove(origin, auth_data_->credentials);
  } else {
    auth_data_ = std::make_unique<AuthData>();
  }
  auth_data_->state = AUTH_STATE_NEED_AUTH;

  FtpAuthCache::Entry* cached_auth = NULL;
  if (ftp_transaction_ && ftp_transaction_->GetResponseInfo()->needs_auth)
    cached_auth = ftp_auth_cache_->Lookup(origin);
  if (cached_auth) {
    // Retry using cached auth data.
    SetAuth(cached_auth->credentials);
  } else {
    // Prompt for a username/password.
    NotifyHeadersComplete();
  }
}

}  // namespace net
