// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert_net/nss_ocsp.h"

#include <certt.h>
#include <certdb.h>
#include <nspr.h>
#include <nss.h>
#include <ocsp.h>
#include <pthread.h>
#include <secerr.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/lazy_instance.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/metrics/histogram_macros.h"
#include "base/no_destructor.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/synchronization/condition_variable.h"
#include "base/synchronization/lock.h"
#include "base/threading/thread_restrictions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "net/base/host_port_pair.h"
#include "url/gurl.h"

namespace net {

namespace {

// Protects |g_request_session_delegate_factory|.
pthread_mutex_t g_request_session_delegate_factory_lock =
    PTHREAD_MUTEX_INITIALIZER;

std::unique_ptr<OCSPRequestSessionDelegateFactory>&
GetRequestSessionDelegateFactoryPtr() {
  static base::NoDestructor<std::unique_ptr<OCSPRequestSessionDelegateFactory>>
      wrapper;
  return *wrapper.get();
}
// The default timeout for network fetches in NSS is 60 seconds. Choose a
// saner upper limit for OCSP/CRL/AIA fetches.
const int kNetworkFetchTimeoutInSecs = 15;

class OCSPRequestSession;

// All OCSP handlers should be called in the context of
// CertVerifier's thread (i.e. worker pool, not on the I/O thread).
// It supports blocking mode only.

SECStatus OCSPCreateSession(const char* host, PRUint16 portnum,
                            SEC_HTTP_SERVER_SESSION* pSession);
SECStatus OCSPKeepAliveSession(SEC_HTTP_SERVER_SESSION session,
                               PRPollDesc **pPollDesc);
SECStatus OCSPFreeSession(SEC_HTTP_SERVER_SESSION session);

SECStatus OCSPCreate(SEC_HTTP_SERVER_SESSION session,
                     const char* http_protocol_variant,
                     const char* path_and_query_string,
                     const char* http_request_method,
                     const PRIntervalTime timeout,
                     SEC_HTTP_REQUEST_SESSION* pRequest);
SECStatus OCSPSetPostData(SEC_HTTP_REQUEST_SESSION request,
                          const char* http_data,
                          const PRUint32 http_data_len,
                          const char* http_content_type);
SECStatus OCSPAddHeader(SEC_HTTP_REQUEST_SESSION request,
                        const char* http_header_name,
                        const char* http_header_value);
SECStatus OCSPTrySendAndReceive(SEC_HTTP_REQUEST_SESSION request,
                                PRPollDesc** pPollDesc,
                                PRUint16* http_response_code,
                                const char** http_response_content_type,
                                const char** http_response_headers,
                                const char** http_response_data,
                                PRUint32* http_response_data_len);
SECStatus OCSPFree(SEC_HTTP_REQUEST_SESSION request);

char* GetAlternateOCSPAIAInfo(CERTCertificate *cert);

class OCSPNSSInitialization {
 private:
  friend struct base::LazyInstanceTraitsBase<OCSPNSSInitialization>;

  OCSPNSSInitialization();
  // This class is only instantiated as a leaky LazyInstance, so its destructor
  // is never called.
  ~OCSPNSSInitialization() = delete;

  SEC_HttpClientFcn client_fcn_;

  DISALLOW_COPY_AND_ASSIGN(OCSPNSSInitialization);
};

base::LazyInstance<OCSPNSSInitialization>::Leaky g_ocsp_nss_initialization =
    LAZY_INSTANCE_INITIALIZER;

// Concrete class for SEC_HTTP_REQUEST_SESSION.
// NSS defines a C API to allow embedders to provide an HTTP abstraction,
// the SEC_HTTP_REQUEST_SESSION. This class provides a C++ abstraction
// used to implement that, but is not a direct 1:1 mapping.
class NET_EXPORT OCSPRequestSession {
 public:
  // Creates a new OCSPRequestSession.
  // |url| should be constructed from the |host| and |portnum| provided in
  // SEC_HttpServer_CreateSessionFcn and the |path_and_query_string| provided
  // in SEC_HttpRequest_CreateFcn, together representing the full URL to
  // query. The only supported |http_protocol_variant| is http, which should
  // be the scheme.
  // |http_request_method| and |timeout| correspond to their
  // SEC_HttpRequest_CreateFcn equivalents.
  // |delegate| should normally be an OCSPRequestSessionDelegateFactory obtained
  // from GetOCSPRequestSessionDelegateFactoryPtr().
  OCSPRequestSession(const GURL& url,
                     const char* http_request_method,
                     base::TimeDelta timeout,
                     scoped_refptr<OCSPRequestSessionDelegate> delegate)
      : delegate_(std::move(delegate)) {
    params_.url = url;
    params_.http_request_method = http_request_method;
    params_.timeout = timeout;
  }
  ~OCSPRequestSession() = default;

  // Implements the functionality of SEC_HttpRequest_SetPostDataFcn
  void SetPostData(const char* http_data,
                   PRUint32 http_data_len,
                   const char* http_content_type) {
    // |upload_content_| should not be modified if the load has already started.
    params_.upload_content.assign(http_data, http_data_len);
    params_.upload_content_type.assign(http_content_type);
  }

  // Implements the functionality of SEC_HttpRequest_AddHeaderFcn
  void AddHeader(const char* http_header_name, const char* http_header_value) {
    params_.extra_request_headers.SetHeader(http_header_name,
                                            http_header_value);
  }

  // Begins communication with the OCSP endpoint, then blocks the thread until
  // the result is available or an error has occurred. Used by
  // SEC_HttpRequest_TrySendAndReceiveFcn.
  bool StartAndWait() {
    DCHECK(!finished_);
    result_ = delegate_->StartAndWait(&params_);
    finished_ = true;
    return result_ != nullptr;
  }

  // Returns true if the OCSP response has been received (or an error occurred).
  bool Finished() const { return finished_; }

  const std::string& http_request_method() const {
    return params_.http_request_method;
  }

  base::TimeDelta timeout() const { return params_.timeout; }

  PRUint16 http_response_code() const {
    DCHECK(finished_);
    return result_->response_code;
  }

  const std::string& http_response_content_type() const {
    DCHECK(finished_);
    return result_->response_content_type;
  }

  const std::string& http_response_headers() const {
    DCHECK(finished_);
    return result_->response_headers->raw_headers();
  }

  const std::string& http_response_data() const {
    DCHECK(finished_);
    return result_->data;
  }

 private:
  OCSPRequestSessionParams params_;
  std::unique_ptr<OCSPRequestSessionResult> result_;
  scoped_refptr<OCSPRequestSessionDelegate> delegate_;
  bool finished_ = false;

  DISALLOW_COPY_AND_ASSIGN(OCSPRequestSession);
};

// Concrete class for SEC_HTTP_SERVER_SESSION.
class OCSPServerSession {
 public:
  OCSPServerSession(const char* host, PRUint16 port)
      : host_and_port_(host, port) {}
  ~OCSPServerSession() = default;

  // Caller is in charge of deleting the returned pointer.
  OCSPRequestSession* CreateRequest(const char* http_protocol_variant,
                                    const char* path_and_query_string,
                                    const char* http_request_method,
                                    const PRIntervalTime timeout) {
    // We dont' support "https" because we haven't thought about
    // whether it's safe to re-enter this code from talking to an OCSP
    // responder over SSL.
    if (strcmp(http_protocol_variant, "http") != 0) {
      PORT_SetError(PR_NOT_IMPLEMENTED_ERROR);
      return NULL;
    }

    std::string url_string(base::StringPrintf(
        "%s://%s%s",
        http_protocol_variant,
        host_and_port_.ToString().c_str(),
        path_and_query_string));
    VLOG(1) << "URL [" << url_string << "]";
    GURL url(url_string);

    // NSS does not expose public functions to adjust the fetch timeout when
    // using libpkix, so hardcode the upper limit for network fetches.
    base::TimeDelta actual_timeout = std::min(
        base::TimeDelta::FromSeconds(kNetworkFetchTimeoutInSecs),
        base::TimeDelta::FromMilliseconds(PR_IntervalToMilliseconds(timeout)));

    scoped_refptr<OCSPRequestSessionDelegate> request_session_delegate;
    pthread_mutex_lock(&g_request_session_delegate_factory_lock);
    OCSPRequestSessionDelegateFactory* request_session_delegate_factory =
        GetRequestSessionDelegateFactoryPtr().get();
    if (request_session_delegate_factory != nullptr) {
      request_session_delegate =
          request_session_delegate_factory->CreateOCSPRequestSessionDelegate();
    }
    pthread_mutex_unlock(&g_request_session_delegate_factory_lock);

    if (request_session_delegate == nullptr)
      return nullptr;
    return new OCSPRequestSession(url, http_request_method, actual_timeout,
                                  std::move(request_session_delegate));
  }

 private:
  HostPortPair host_and_port_;

  DISALLOW_COPY_AND_ASSIGN(OCSPServerSession);
};

OCSPNSSInitialization::OCSPNSSInitialization() {
  // NSS calls the functions in the function table to download certificates
  // or CRLs or talk to OCSP responders over HTTP.  These functions must
  // set an NSS/NSPR error code when they fail.  Otherwise NSS will get the
  // residual error code from an earlier failed function call.
  client_fcn_.version = 1;
  SEC_HttpClientFcnV1Struct *ft = &client_fcn_.fcnTable.ftable1;
  ft->createSessionFcn = OCSPCreateSession;
  ft->keepAliveSessionFcn = OCSPKeepAliveSession;
  ft->freeSessionFcn = OCSPFreeSession;
  ft->createFcn = OCSPCreate;
  ft->setPostDataFcn = OCSPSetPostData;
  ft->addHeaderFcn = OCSPAddHeader;
  ft->trySendAndReceiveFcn = OCSPTrySendAndReceive;
  ft->cancelFcn = nullptr;
  ft->freeFcn = OCSPFree;
  SECStatus status = SEC_RegisterDefaultHttpClient(&client_fcn_);
  if (status != SECSuccess) {
    NOTREACHED() << "Error initializing OCSP: " << PR_GetError();
  }

  // Work around NSS bugs 524013 and 564334.  NSS incorrectly thinks the
  // CRLs for Network Solutions Certificate Authority have bad signatures,
  // which causes certificates issued by that CA to be reported as revoked.
  // By using OCSP for those certificates, which don't have AIA extensions,
  // we can work around these bugs.  See http://crbug.com/41730.
  CERT_StringFromCertFcn old_callback = nullptr;
  status = CERT_RegisterAlternateOCSPAIAInfoCallBack(
      GetAlternateOCSPAIAInfo, &old_callback);
  if (status == SECSuccess) {
    DCHECK(!old_callback);
  } else {
    NOTREACHED() << "Error initializing OCSP: " << PR_GetError();
  }
}


// OCSP Http Client functions.
// Our Http Client functions operate in blocking mode.
SECStatus OCSPCreateSession(const char* host, PRUint16 portnum,
                            SEC_HTTP_SERVER_SESSION* pSession) {
  VLOG(1) << "OCSP create session: host=" << host << " port=" << portnum;
  pthread_mutex_lock(&g_request_session_delegate_factory_lock);
  bool factory_configured = GetRequestSessionDelegateFactoryPtr() != nullptr;
  pthread_mutex_unlock(&g_request_session_delegate_factory_lock);
  if (!factory_configured) {
    LOG(ERROR)
        << "No OCSPRequestSessionDelegateFactory for NSS HTTP handler. host: "
        << host;
    // The application failed to call SetOCSPRequestSessionDelegateFactory or
    // has already called SetOCSPRequestSessionDelegateFactory(nullptr).
    // PR_NOT_IMPLEMENTED_ERROR is not an accurate error code for these error
    // conditions, but is close enough.
    PORT_SetError(PR_NOT_IMPLEMENTED_ERROR);
    return SECFailure;
  }
  *pSession = new OCSPServerSession(host, portnum);
  return SECSuccess;
}

SECStatus OCSPKeepAliveSession(SEC_HTTP_SERVER_SESSION session,
                               PRPollDesc **pPollDesc) {
  VLOG(1) << "OCSP keep alive";
  if (pPollDesc)
    *pPollDesc = NULL;
  return SECSuccess;
}

SECStatus OCSPFreeSession(SEC_HTTP_SERVER_SESSION session) {
  VLOG(1) << "OCSP free session";
  delete reinterpret_cast<OCSPServerSession*>(session);
  return SECSuccess;
}

SECStatus OCSPCreate(SEC_HTTP_SERVER_SESSION session,
                     const char* http_protocol_variant,
                     const char* path_and_query_string,
                     const char* http_request_method,
                     const PRIntervalTime timeout,
                     SEC_HTTP_REQUEST_SESSION* pRequest) {
  VLOG(1) << "OCSP create protocol=" << http_protocol_variant
          << " path_and_query=" << path_and_query_string
          << " http_request_method=" << http_request_method
          << " timeout=" << timeout;
  OCSPServerSession* ocsp_session =
      reinterpret_cast<OCSPServerSession*>(session);

  OCSPRequestSession* req = ocsp_session->CreateRequest(http_protocol_variant,
                                                        path_and_query_string,
                                                        http_request_method,
                                                        timeout);
  SECStatus rv = SECFailure;
  if (req) {
    rv = SECSuccess;
  }
  *pRequest = req;
  return rv;
}

SECStatus OCSPSetPostData(SEC_HTTP_REQUEST_SESSION request,
                          const char* http_data,
                          const PRUint32 http_data_len,
                          const char* http_content_type) {
  VLOG(1) << "OCSP set post data len=" << http_data_len;
  OCSPRequestSession* req = reinterpret_cast<OCSPRequestSession*>(request);

  req->SetPostData(http_data, http_data_len, http_content_type);
  return SECSuccess;
}

SECStatus OCSPAddHeader(SEC_HTTP_REQUEST_SESSION request,
                        const char* http_header_name,
                        const char* http_header_value) {
  VLOG(1) << "OCSP add header name=" << http_header_name
          << " value=" << http_header_value;
  OCSPRequestSession* req = reinterpret_cast<OCSPRequestSession*>(request);

  req->AddHeader(http_header_name, http_header_value);
  return SECSuccess;
}

// Sets response of |req| in the output parameters.
// It is helper routine for OCSP trySendAndReceiveFcn.
// |http_response_data_len| could be used as input parameter.  If it has
// non-zero value, it is considered as maximum size of |http_response_data|.
SECStatus OCSPSetResponse(OCSPRequestSession* req,
                          PRUint16* http_response_code,
                          const char** http_response_content_type,
                          const char** http_response_headers,
                          const char** http_response_data,
                          PRUint32* http_response_data_len) {
  DCHECK(req->Finished());
  const std::string& data = req->http_response_data();
  if (http_response_data_len && *http_response_data_len) {
    if (*http_response_data_len < data.size()) {
      LOG(ERROR) << "response body too large: " << *http_response_data_len
                 << " < " << data.size();
      *http_response_data_len = data.size();
      PORT_SetError(SEC_ERROR_BAD_HTTP_RESPONSE);
      return SECFailure;
    }
  }
  VLOG(1) << "OCSP response "
          << " response_code=" << req->http_response_code()
          << " content_type=" << req->http_response_content_type()
          << " header=" << req->http_response_headers()
          << " data_len=" << data.size();
  if (http_response_code)
    *http_response_code = req->http_response_code();
  if (http_response_content_type)
    *http_response_content_type = req->http_response_content_type().c_str();
  if (http_response_headers)
    *http_response_headers = req->http_response_headers().c_str();
  if (http_response_data)
    *http_response_data = data.data();
  if (http_response_data_len)
    *http_response_data_len = data.size();
  return SECSuccess;
}

SECStatus OCSPTrySendAndReceive(SEC_HTTP_REQUEST_SESSION request,
                                PRPollDesc** pPollDesc,
                                PRUint16* http_response_code,
                                const char** http_response_content_type,
                                const char** http_response_headers,
                                const char** http_response_data,
                                PRUint32* http_response_data_len) {
  if (http_response_data_len) {
    // We must always set an output value, even on failure.  The output value 0
    // means the failure was unrelated to the acceptable response data length.
    *http_response_data_len = 0;
  }

  VLOG(1) << "OCSP try send and receive";
  OCSPRequestSession* req = reinterpret_cast<OCSPRequestSession*>(request);
  // We support blocking mode only.
  if (pPollDesc)
    *pPollDesc = NULL;

  if (!req->StartAndWait() ||
      req->http_response_code() == static_cast<PRUint16>(-1)) {
    // If the response code is -1, the request failed and there is no response.
    PORT_SetError(SEC_ERROR_BAD_HTTP_RESPONSE);  // Simple approximation.
    return SECFailure;
  }

  return OCSPSetResponse(
      req, http_response_code,
      http_response_content_type,
      http_response_headers,
      http_response_data,
      http_response_data_len);
}

SECStatus OCSPFree(SEC_HTTP_REQUEST_SESSION request) {
  VLOG(1) << "OCSP free";
  OCSPRequestSession* req = reinterpret_cast<OCSPRequestSession*>(request);
  delete req;
  return SECSuccess;
}

// Data for GetAlternateOCSPAIAInfo.

// CN=Network Solutions Certificate Authority,O=Network Solutions L.L.C.,C=US
//
// There are two CAs with this name.  Their key IDs are listed next.
const unsigned char network_solutions_ca_name[] = {
  0x30, 0x62, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04,
  0x06, 0x13, 0x02, 0x55, 0x53, 0x31, 0x21, 0x30, 0x1f, 0x06,
  0x03, 0x55, 0x04, 0x0a, 0x13, 0x18, 0x4e, 0x65, 0x74, 0x77,
  0x6f, 0x72, 0x6b, 0x20, 0x53, 0x6f, 0x6c, 0x75, 0x74, 0x69,
  0x6f, 0x6e, 0x73, 0x20, 0x4c, 0x2e, 0x4c, 0x2e, 0x43, 0x2e,
  0x31, 0x30, 0x30, 0x2e, 0x06, 0x03, 0x55, 0x04, 0x03, 0x13,
  0x27, 0x4e, 0x65, 0x74, 0x77, 0x6f, 0x72, 0x6b, 0x20, 0x53,
  0x6f, 0x6c, 0x75, 0x74, 0x69, 0x6f, 0x6e, 0x73, 0x20, 0x43,
  0x65, 0x72, 0x74, 0x69, 0x66, 0x69, 0x63, 0x61, 0x74, 0x65,
  0x20, 0x41, 0x75, 0x74, 0x68, 0x6f, 0x72, 0x69, 0x74, 0x79
};
const unsigned int network_solutions_ca_name_len =
    base::size(network_solutions_ca_name);

// This CA is an intermediate CA, subordinate to UTN-USERFirst-Hardware.
const unsigned char network_solutions_ca_key_id[] = {
  0x3c, 0x41, 0xe2, 0x8f, 0x08, 0x08, 0xa9, 0x4c, 0x25, 0x89,
  0x8d, 0x6d, 0xc5, 0x38, 0xd0, 0xfc, 0x85, 0x8c, 0x62, 0x17
};
const unsigned int network_solutions_ca_key_id_len =
    base::size(network_solutions_ca_key_id);

// This CA is a root CA.  It is also cross-certified by
// UTN-USERFirst-Hardware.
const unsigned char network_solutions_ca_key_id2[] = {
  0x21, 0x30, 0xc9, 0xfb, 0x00, 0xd7, 0x4e, 0x98, 0xda, 0x87,
  0xaa, 0x2a, 0xd0, 0xa7, 0x2e, 0xb1, 0x40, 0x31, 0xa7, 0x4c
};
const unsigned int network_solutions_ca_key_id2_len =
    base::size(network_solutions_ca_key_id2);

// An entry in our OCSP responder table.  |issuer| and |issuer_key_id| are
// the key.  |ocsp_url| is the value.
struct OCSPResponderTableEntry {
  SECItem issuer;
  SECItem issuer_key_id;
  const char *ocsp_url;
};

const OCSPResponderTableEntry g_ocsp_responder_table[] = {
  {
    {
      siBuffer,
      const_cast<unsigned char*>(network_solutions_ca_name),
      network_solutions_ca_name_len
    },
    {
      siBuffer,
      const_cast<unsigned char*>(network_solutions_ca_key_id),
      network_solutions_ca_key_id_len
    },
    "http://ocsp.netsolssl.com"
  },
  {
    {
      siBuffer,
      const_cast<unsigned char*>(network_solutions_ca_name),
      network_solutions_ca_name_len
    },
    {
      siBuffer,
      const_cast<unsigned char*>(network_solutions_ca_key_id2),
      network_solutions_ca_key_id2_len
    },
    "http://ocsp.netsolssl.com"
  }
};

char* GetAlternateOCSPAIAInfo(CERTCertificate *cert) {
  if (cert && !cert->isRoot && cert->authKeyID) {
    for (const auto& responder : g_ocsp_responder_table) {
      if (SECITEM_CompareItem(&responder.issuer, &cert->derIssuer) ==
              SECEqual &&
          SECITEM_CompareItem(&responder.issuer_key_id,
                              &cert->authKeyID->keyID) == SECEqual) {
        return PORT_Strdup(responder.ocsp_url);
      }
    }
  }

  return NULL;
}

}  // anonymous namespace

OCSPRequestSessionParams::OCSPRequestSessionParams() = default;
OCSPRequestSessionParams::~OCSPRequestSessionParams() = default;

OCSPRequestSessionResult::OCSPRequestSessionResult() = default;
OCSPRequestSessionResult::~OCSPRequestSessionResult() = default;

OCSPRequestSessionDelegate::~OCSPRequestSessionDelegate() = default;

OCSPRequestSessionDelegateFactory::OCSPRequestSessionDelegateFactory() =
    default;
OCSPRequestSessionDelegateFactory::~OCSPRequestSessionDelegateFactory() =
    default;

void EnsureNSSHttpIOInit() {
  g_ocsp_nss_initialization.Get();
}

void SetOCSPRequestSessionDelegateFactory(
    std::unique_ptr<OCSPRequestSessionDelegateFactory> new_factory) {
  std::unique_ptr<OCSPRequestSessionDelegateFactory> factory_to_be_destructed;

  pthread_mutex_lock(&g_request_session_delegate_factory_lock);
  std::unique_ptr<OCSPRequestSessionDelegateFactory>& current_factory =
      GetRequestSessionDelegateFactoryPtr();
  // The same NSS-using process should only ever use one concrete
  // OCSPRequestSessionDelegateFactory for the lifetime of that process. If this
  // DCHECK triggers, two different instances are trying to be used in the
  // same process, and that underlying issue should be fixed, rather than
  // trying to silence this by calling with nullptr first to reset the old
  // instance.
  DCHECK(!new_factory || !current_factory.get());

  factory_to_be_destructed =
      std::exchange(current_factory, std::move(new_factory));
  pthread_mutex_unlock(&g_request_session_delegate_factory_lock);
}

}  // namespace net
