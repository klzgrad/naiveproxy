// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/reporting/reporting_uploader.h"

#include <string>
#include <utility>
#include <vector>

#include "base/callback_helpers.h"
#include "base/metrics/histogram_macros.h"
#include "net/base/elements_upload_data_stream.h"
#include "net/base/load_flags.h"
#include "net/base/upload_bytes_element_reader.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/url_request/redirect_info.h"
#include "net/url_request/url_request_context.h"
#include "url/gurl.h"

namespace net {

namespace {

ReportingUploader::Outcome ResponseCodeToOutcome(int response_code) {
  if (response_code >= 200 && response_code <= 299)
    return ReportingUploader::Outcome::SUCCESS;
  if (response_code == 410)
    return ReportingUploader::Outcome::REMOVE_ENDPOINT;
  return ReportingUploader::Outcome::FAILURE;
}

class ReportingUploaderImpl : public ReportingUploader, URLRequest::Delegate {
 public:
  ReportingUploaderImpl(const URLRequestContext* context) : context_(context) {
    DCHECK(context_);
  }

  ~ReportingUploaderImpl() override {
    for (auto& it : uploads_) {
      base::ResetAndReturn(&it.second->second).Run(Outcome::FAILURE);
      it.second->first->Cancel();
    }
    uploads_.clear();
  }

  void StartUpload(const GURL& url,
                   const std::string& json,
                   const Callback& callback) override {
    net::NetworkTrafficAnnotationTag traffic_annotation =
        net::DefineNetworkTrafficAnnotation("reporting", R"(
        semantics {
          sender: "Reporting API"
          description:
            "The Reporting API reports various issues back to website owners "
            "to help them detect and fix problems."
          trigger:
            "Encountering issues. Examples of these issues are Content "
            "Security Policy violations and Interventions/Deprecations "
            "encountered. See draft of reporting spec here: "
            "https://wicg.github.io/reporting."
          data: "Details of the issue, depending on issue type."
          destination: OTHER
        }
        policy {
          cookies_allowed: NO
          setting: "This feature cannot be disabled by settings."
          policy_exception_justification: "Not implemented."
        })");
    std::unique_ptr<URLRequest> request =
        context_->CreateRequest(url, IDLE, this, traffic_annotation);

    request->set_method("POST");

    request->SetLoadFlags(LOAD_DISABLE_CACHE | LOAD_DO_NOT_SAVE_COOKIES |
                          LOAD_DO_NOT_SEND_COOKIES);

    request->SetExtraRequestHeaderByName(HttpRequestHeaders::kContentType,
                                         kUploadContentType, true);

    std::vector<char> json_data(json.begin(), json.end());
    std::unique_ptr<UploadElementReader> reader(
        new UploadOwnedBytesElementReader(&json_data));
    request->set_upload(
        ElementsUploadDataStream::CreateWithReader(std::move(reader), 0));

    // This inherently sets mode = "no-cors", but that doesn't matter, because
    // the origins that are included in the upload don't actually get to see
    // the response.
    //
    // This inherently skips Service Worker, too.
    request->Start();

    // Have to grab the unique_ptr* first to ensure request.get() happens
    // before std::move(request).
    std::unique_ptr<Upload>* upload = &uploads_[request.get()];
    *upload = std::make_unique<Upload>(std::move(request), callback);
  }

  // URLRequest::Delegate implementation:

  void OnReceivedRedirect(URLRequest* request,
                          const RedirectInfo& redirect_info,
                          bool* defer_redirect) override {
    if (!redirect_info.new_url.SchemeIsCryptographic()) {
      request->Cancel();
      return;
    }
  }

  void OnAuthRequired(URLRequest* request,
                      AuthChallengeInfo* auth_info) override {
    request->Cancel();
  }

  void OnCertificateRequested(URLRequest* request,
                              SSLCertRequestInfo* cert_request_info) override {
    request->Cancel();
  }

  void OnSSLCertificateError(URLRequest* request,
                             const SSLInfo& ssl_info,
                             bool fatal) override {
    request->Cancel();
  }

  void OnResponseStarted(URLRequest* request, int net_error) override {
    // Grab Upload from map, and hold on to it in a local unique_ptr so it's
    // removed at the end of the method.
    auto it = uploads_.find(request);
    DCHECK(it != uploads_.end());
    std::unique_ptr<Upload> upload = std::move(it->second);
    uploads_.erase(it);

    // request->GetResponseCode() should work, but doesn't in the cases above
    // where the request was canceled, so get the response code by hand.
    // TODO(juliatuttle): Check if mmenke fixed this yet.
    HttpResponseHeaders* headers = request->response_headers();
    int response_code = headers ? headers->response_code() : 0;
    Outcome outcome = ResponseCodeToOutcome(response_code);

    upload->second.Run(outcome);

    request->Cancel();
  }

  void OnReadCompleted(URLRequest* request, int bytes_read) override {
    // Reporting doesn't need anything in the body of the response, so it
    // doesn't read it, so it should never get OnReadCompleted calls.
    NOTREACHED();
  }

 private:
  using Upload = std::pair<std::unique_ptr<URLRequest>, Callback>;

  const URLRequestContext* context_;
  std::map<const URLRequest*, std::unique_ptr<Upload>> uploads_;
};

}  // namespace

// static
const char ReportingUploader::kUploadContentType[] = "application/report";

ReportingUploader::~ReportingUploader() {}

// static
std::unique_ptr<ReportingUploader> ReportingUploader::Create(
    const URLRequestContext* context) {
  return std::make_unique<ReportingUploaderImpl>(context);
}

}  // namespace net
