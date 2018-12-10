// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_REPORTING_REPORTING_UPLOADER_H_
#define NET_REPORTING_REPORTING_UPLOADER_H_

#include <memory>
#include <string>

#include "base/callback.h"
#include "net/base/net_export.h"

class GURL;

namespace url {
class Origin;
}  // namespace url

namespace net {

class URLRequest;
class URLRequestContext;

// Uploads already-serialized reports and converts responses to one of the
// specified outcomes.
class NET_EXPORT ReportingUploader {
 public:
  enum class Outcome { SUCCESS, REMOVE_ENDPOINT, FAILURE };

  using UploadCallback = base::OnceCallback<void(Outcome outcome)>;

  virtual ~ReportingUploader();

  // Starts to upload the reports in |json| (properly tagged as JSON data) to
  // |url|, and calls |callback| when complete (whether successful or not).
  // All of the reports in |json| must describe requests to the same origin;
  // |report_origin| must be that origin.
  virtual void StartUpload(const url::Origin& report_origin,
                           const GURL& url,
                           const std::string& json,
                           int max_depth,
                           UploadCallback callback) = 0;

  // Returns whether |request| is an upload request sent by this uploader.
  virtual int GetUploadDepth(const URLRequest& request) = 0;

  // Creates a real implementation of |ReportingUploader| that uploads reports
  // using |context|.
  static std::unique_ptr<ReportingUploader> Create(
      const URLRequestContext* context);
};

}  // namespace net

#endif  // NET_REPORTING_REPORTING_UPLOADER_H_
