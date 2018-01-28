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

namespace net {

class URLRequestContext;

// Uploads already-serialized reports and converts responses to one of the
// specified outcomes.
class NET_EXPORT ReportingUploader {
 public:
  enum class Outcome { SUCCESS, REMOVE_ENDPOINT, FAILURE };

  using Callback = base::Callback<void(Outcome outcome)>;

  static const char kUploadContentType[];

  virtual ~ReportingUploader();

  // Starts to upload the reports in |json| (properly tagged as JSON data) to
  // |url|, and calls |callback| when complete (whether successful or not).
  virtual void StartUpload(const GURL& url,
                           const std::string& json,
                           const Callback& callback) = 0;

  // Creates a real implementation of |ReportingUploader| that uploads reports
  // using |context|.
  static std::unique_ptr<ReportingUploader> Create(
      const URLRequestContext* context);
};

}  // namespace net

#endif  // NET_REPORTING_REPORTING_UPLOADER_H_
