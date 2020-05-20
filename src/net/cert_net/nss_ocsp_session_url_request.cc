// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert_net/nss_ocsp_session_url_request.h"

#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_loop_current.h"
#include "base/synchronization/condition_variable.h"
#include "base/synchronization/lock.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "net/base/elements_upload_data_stream.h"
#include "net/base/load_flags.h"
#include "net/base/upload_bytes_element_reader.h"
#include "net/cert_net/nss_ocsp.h"
#include "net/url_request/redirect_info.h"
#include "net/url_request/url_request_context.h"
#include "url/gurl.h"

namespace net {

namespace {
// Size of the IOBuffer that used for reading the result.
const int kRecvBufferSize = 4096;

// The maximum size in bytes for the response body when fetching an OCSP/CRL
// URL.
const int kMaxResponseSizeInBytes = 5 * 1024 * 1024;
}  // namespace

class OCSPRequestSessionDelegateURLRequest;

class NET_EXPORT OCSPIOLoop {
 public:
  // This class is only instantiated in a base::NoDestructor, so its destructor
  // is never called.
  ~OCSPIOLoop() = delete;

  // Called on IO task runner.
  void StartUsing();

  // Called on IO task runner.
  void Shutdown();

  // Called from worker thread.
  void PostTaskToIOLoop(const base::Location& from_here,
                        base::OnceClosure task);

  // Returns true if and only if StartUsing() has been called, Shutdown() has
  // not been called, and this is currently running on the OCSP IO task runner.
  bool RunsTasksInCurrentSequence();

  // Adds a request to cancel if |this|->Shutdown() is called during the
  // request.
  void AddRequest(OCSPRequestSessionDelegateURLRequest* request_delegate);

  // Remove the request from tracking when the request has finished.
  void RemoveRequest(OCSPRequestSessionDelegateURLRequest* request_delegate);

 private:
  friend class base::NoDestructor<OCSPIOLoop>;

  OCSPIOLoop();

  void CancelAllRequests();

  // Protects all members below.
  mutable base::Lock lock_;
  std::set<OCSPRequestSessionDelegateURLRequest*> request_delegates_;
  scoped_refptr<base::SequencedTaskRunner> io_task_runner_;

  DISALLOW_COPY_AND_ASSIGN(OCSPIOLoop);
};

OCSPIOLoop* GetOCSPIOLoop() {
  static base::NoDestructor<OCSPIOLoop> ocsp_io_loop;
  return ocsp_io_loop.get();
}

class OCSPRequestSessionDelegateFactoryURLRequest
    : public OCSPRequestSessionDelegateFactory {
 public:
  OCSPRequestSessionDelegateFactoryURLRequest(
      URLRequestContext* request_context)
      : request_context_(request_context), weak_ptr_factory_(this) {
    weak_ptr_ = weak_ptr_factory_.GetWeakPtr();
  }

  scoped_refptr<OCSPRequestSessionDelegate> CreateOCSPRequestSessionDelegate()
      override;

  ~OCSPRequestSessionDelegateFactoryURLRequest() override = default;

  URLRequestContext* request_context() const { return request_context_; }

 private:
  URLRequestContext* request_context_;

  base::WeakPtr<OCSPRequestSessionDelegateFactoryURLRequest> weak_ptr_;
  base::WeakPtrFactory<OCSPRequestSessionDelegateFactoryURLRequest>
      weak_ptr_factory_;
};

class OCSPRequestSessionDelegateURLRequest : public OCSPRequestSessionDelegate,
                                             public URLRequest::Delegate {
 public:
  OCSPRequestSessionDelegateURLRequest(
      base::WeakPtr<OCSPRequestSessionDelegateFactoryURLRequest>
          delegate_factory)
      : buffer_(base::MakeRefCounted<IOBuffer>(kRecvBufferSize)),
        delegate_factory_(std::move(delegate_factory)),
        cv_(&lock_) {}

  // OCSPRequestSessionDelegate overrides.
  std::unique_ptr<OCSPRequestSessionResult> StartAndWait(
      const OCSPRequestSessionParams* params) override {
    GetOCSPIOLoop()->PostTaskToIOLoop(
        FROM_HERE,
        base::BindOnce(&OCSPRequestSessionDelegateURLRequest::StartLoad, this,
                       params));

    // Wait with a timeout.
    base::TimeDelta timeout = params->timeout;
    base::AutoLock autolock(lock_);
    while (!finished_) {
      base::TimeTicks last_time = base::TimeTicks::Now();
      cv_.TimedWait(timeout);
      // Check elapsed time
      base::TimeDelta elapsed_time = base::TimeTicks::Now() - last_time;
      timeout -= elapsed_time;
      if (timeout < base::TimeDelta()) {
        VLOG(1) << "OCSP Timed out";
        if (!finished_) {
          // Safe to call CancelLoad even if the request successfully finished
          // after our timeout, because if the request has finished it will be
          // reset and CancelLoad will be a no-op.
          GetOCSPIOLoop()->PostTaskToIOLoop(
              FROM_HERE,
              base::BindOnce(&OCSPRequestSessionDelegateURLRequest::CancelLoad,
                             this));
        }
        break;
      }
    }

    if (!finished_)
      return nullptr;
    return std::move(result_);
  }

  void CancelLoad() {
    DCHECK(GetOCSPIOLoop()->RunsTasksInCurrentSequence());
    if (request_) {
      FinishLoad();
    }
  }

  // URLRequest::Delegate overrides
  void OnReceivedRedirect(URLRequest* request,
                          const RedirectInfo& redirect_info,
                          bool* defer_redirect) override {
    DCHECK_EQ(request_.get(), request);
    DCHECK(GetOCSPIOLoop()->RunsTasksInCurrentSequence());

    if (!redirect_info.new_url.SchemeIs("http")) {
      // Prevent redirects to non-HTTP schemes, including HTTPS. This matches
      // the initial check in OCSPServerSession::CreateRequest().
      CancelLoad();
    }
  }

  void OnResponseStarted(URLRequest* request, int net_error) override {
    DCHECK_EQ(request_.get(), request);
    DCHECK(GetOCSPIOLoop()->RunsTasksInCurrentSequence());
    DCHECK_NE(ERR_IO_PENDING, net_error);

    int bytes_read = 0;
    if (net_error == OK) {
      result_->response_code = request_->GetResponseCode();
      result_->response_headers = request_->response_headers();
      result_->response_headers->GetMimeType(&result_->response_content_type);
      bytes_read = request_->Read(buffer_.get(), kRecvBufferSize);
    }
    OnReadCompleted(request_.get(), bytes_read);
  }

  void OnReadCompleted(URLRequest* request, int bytes_read) override {
    DCHECK(!finished_);
    DCHECK_EQ(request_.get(), request);
    DCHECK(GetOCSPIOLoop()->RunsTasksInCurrentSequence());

    while (bytes_read > 0) {
      result_->data.append(buffer_->data(), bytes_read);
      bytes_read = request_->Read(buffer_.get(), kRecvBufferSize);
    }

    // Check max size.
    if (result_->data.size() > kMaxResponseSizeInBytes) {
      // Reset the result to indicate error.
      result_.reset();
      FinishLoad();
    }

    // If we are done reading, return results.
    if (bytes_read != ERR_IO_PENDING) {
      FinishLoad();
    }
  }

 private:
  friend class base::RefCountedThreadSafe<OCSPRequestSessionDelegateURLRequest>;

  ~OCSPRequestSessionDelegateURLRequest() override {
    // When this destructor is called, there should be only one thread that has
    // a reference to this object, and so that thread doesn't need to lock
    // |lock_| here.
    DCHECK(finished_);
    DCHECK(!request_);
  }

  // Runs on the OCSP IO task runner.
  void StartLoad(const OCSPRequestSessionParams* params) {
    DCHECK(GetOCSPIOLoop()->RunsTasksInCurrentSequence());
    if (request_) {
      NOTREACHED();
      FinishLoad();  // Will return a nullptr as the result.
      return;
    }
    if (!delegate_factory_) {
      return;
    }

    GetOCSPIOLoop()->AddRequest(this);

    net::NetworkTrafficAnnotationTag traffic_annotation =
        net::DefineNetworkTrafficAnnotation("ocsp_start_url_request", R"(
        semantics {
          sender: "OCSP"
          description:
            "Verifying the revocation status of a certificate via OCSP."
          trigger:
            "This may happen in response to visiting a website that uses "
            "https://"
          data:
            "Identifier for the certificate whose revocation status is being "
            "checked. See https://tools.ietf.org/html/rfc6960#section-2.1 for "
            "more details."
          destination: OTHER
          destination_other:
            "The URI specified in the certificate."
        }
        policy {
          cookies_allowed: NO
          setting: "This feature cannot be disabled by settings."
          policy_exception_justification: "Not implemented."
        })");
    request_ = delegate_factory_->request_context()->CreateRequest(
        params->url, DEFAULT_PRIORITY, this, traffic_annotation);
    request_->SetLoadFlags(LOAD_DISABLE_CACHE);
    request_->set_allow_credentials(false);
    // Disable secure DNS for hostname lookups triggered by certificate network
    // fetches to prevent deadlock.
    request_->SetDisableSecureDns(true);

    if (!params->extra_request_headers.IsEmpty())
      request_->SetExtraRequestHeaders(params->extra_request_headers);

    if (params->http_request_method == "POST") {
      DCHECK(!params->upload_content.empty());
      DCHECK(!params->upload_content_type.empty());

      request_->set_method("POST");
      request_->SetExtraRequestHeaderByName(HttpRequestHeaders::kContentType,
                                            params->upload_content_type, true);

      std::unique_ptr<UploadElementReader> reader(new UploadBytesElementReader(
          params->upload_content.data(), params->upload_content.size()));
      request_->set_upload(
          ElementsUploadDataStream::CreateWithReader(std::move(reader), 0));
    }

    request_->Start();
    result_ = std::make_unique<OCSPRequestSessionResult>();
    AddRef();  // Release after |request_| deleted.
  }

  void FinishLoad() {
    DCHECK(GetOCSPIOLoop()->RunsTasksInCurrentSequence());
    {
      base::AutoLock autolock(lock_);
      finished_ = true;
    }
    delegate_factory_.reset();
    request_.reset();
    GetOCSPIOLoop()->RemoveRequest(this);

    cv_.Signal();

    Release();  // Balanced with StartLoad().
  }

  std::unique_ptr<URLRequest> request_;  // The actual request this wraps
  scoped_refptr<IOBuffer> buffer_;       // Read buffer
  base::WeakPtr<OCSPRequestSessionDelegateFactoryURLRequest> delegate_factory_;

  std::unique_ptr<OCSPRequestSessionResult> result_;
  bool finished_ = false;

  // |lock_| protects |finished_|.
  mutable base::Lock lock_;
  base::ConditionVariable cv_;
};

OCSPIOLoop::OCSPIOLoop() = default;

void OCSPIOLoop::StartUsing() {
  base::AutoLock autolock(lock_);
  DCHECK(base::MessageLoopCurrentForIO::IsSet());
  io_task_runner_ = base::SequencedTaskRunnerHandle::Get();
}

void OCSPIOLoop::Shutdown() {
  // Safe to read outside lock since we only write on IO thread anyway.
  DCHECK(io_task_runner_->RunsTasksInCurrentSequence());

  // Prevent the worker thread from trying to access |io_task_runner_|.
  {
    base::AutoLock autolock(lock_);
    io_task_runner_ = nullptr;
  }

  CancelAllRequests();

  SetOCSPRequestSessionDelegateFactory(nullptr);
}

void OCSPIOLoop::PostTaskToIOLoop(const base::Location& from_here,
                                  base::OnceClosure task) {
  base::AutoLock autolock(lock_);
  if (io_task_runner_)
    io_task_runner_->PostTask(from_here, std::move(task));
}

void OCSPIOLoop::AddRequest(
    OCSPRequestSessionDelegateURLRequest* request_delegate) {
  DCHECK(!base::Contains(request_delegates_, request_delegate));
  request_delegates_.insert(request_delegate);
}

void OCSPIOLoop::RemoveRequest(
    OCSPRequestSessionDelegateURLRequest* request_delegate) {
  DCHECK(base::Contains(request_delegates_, request_delegate));
  request_delegates_.erase(request_delegate);
}

bool OCSPIOLoop::RunsTasksInCurrentSequence() {
  base::AutoLock autolock(lock_);
  return io_task_runner_ && io_task_runner_->RunsTasksInCurrentSequence();
}

void OCSPIOLoop::CancelAllRequests() {
  // CancelLoad() always removes the request from the requests_
  // set synchronously.
  while (!request_delegates_.empty())
    (*request_delegates_.begin())->CancelLoad();
}

scoped_refptr<OCSPRequestSessionDelegate>
OCSPRequestSessionDelegateFactoryURLRequest::
    CreateOCSPRequestSessionDelegate() {
  return base::MakeRefCounted<OCSPRequestSessionDelegateURLRequest>(weak_ptr_);
}

void SetURLRequestContextForNSSHttpIO(URLRequestContext* request_context) {
  if (request_context) {
    SetOCSPRequestSessionDelegateFactory(
        std::make_unique<OCSPRequestSessionDelegateFactoryURLRequest>(
            request_context));
  } else {
    SetOCSPRequestSessionDelegateFactory(nullptr);
  }

  if (request_context) {
    GetOCSPIOLoop()->StartUsing();
  } else {
    GetOCSPIOLoop()->Shutdown();
  }
}
}  // namespace net
