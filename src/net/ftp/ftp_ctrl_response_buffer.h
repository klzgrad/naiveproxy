// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_FTP_FTP_CTRL_RESPONSE_BUFFER_H_
#define NET_FTP_FTP_CTRL_RESPONSE_BUFFER_H_

#include <string>
#include <vector>

#include "base/containers/queue.h"
#include "base/macros.h"
#include "net/base/net_export.h"
#include "net/log/net_log_with_source.h"

namespace net {

struct NET_EXPORT_PRIVATE FtpCtrlResponse {
  static const int kInvalidStatusCode;

  FtpCtrlResponse();
  FtpCtrlResponse(const FtpCtrlResponse& other);
  ~FtpCtrlResponse();

  int status_code;                 // Three-digit status code.
  std::vector<std::string> lines;  // Response lines, without CRLFs.
};

class NET_EXPORT_PRIVATE FtpCtrlResponseBuffer {
 public:
  FtpCtrlResponseBuffer(const NetLogWithSource& net_log);
  ~FtpCtrlResponseBuffer();

  // Called when data is received from the control socket. Returns error code.
  int ConsumeData(const char* data, int data_length);

  bool ResponseAvailable() const {
    return !responses_.empty();
  }

  // Returns the next response. It is an error to call this function
  // unless ResponseAvailable returns true.
  FtpCtrlResponse PopResponse();

 private:
  struct ParsedLine {
    ParsedLine();
    ParsedLine(const ParsedLine& other);

    // Indicates that this line begins with a valid 3-digit status code.
    bool has_status_code;

    // Indicates that this line has the dash (-) after the code, which
    // means a multiline response.
    bool is_multiline;

    // Indicates that this line could be parsed as a complete and valid
    // response line, without taking into account preceding lines (which
    // may change its meaning into a continuation of the previous line).
    bool is_complete;

    // Part of response parsed as status code.
    int status_code;

    // Part of response parsed as status text.
    std::string status_text;

    // Text before parsing, without terminating CRLF.
    std::string raw_text;
  };

  static ParsedLine ParseLine(const std::string& line);

  void ExtractFullLinesFromBuffer();

  // We keep not-yet-parsed data in a string buffer.
  std::string buffer_;

  base::queue<ParsedLine> lines_;

  // True if we are in the middle of parsing a multi-line response.
  bool multiline_;

  // When parsing a multiline response, we don't know beforehand if a line
  // will have a continuation. So always store last line of multiline response
  // so we can append the continuation to it.
  std::string line_buf_;

  // Keep the response data while we add all lines to it.
  FtpCtrlResponse response_buf_;

  // As we read full responses (possibly multiline), we add them to the queue.
  base::queue<FtpCtrlResponse> responses_;

  NetLogWithSource net_log_;

  DISALLOW_COPY_AND_ASSIGN(FtpCtrlResponseBuffer);
};

}  // namespace net

#endif  // NET_FTP_FTP_CTRL_RESPONSE_BUFFER_H_
