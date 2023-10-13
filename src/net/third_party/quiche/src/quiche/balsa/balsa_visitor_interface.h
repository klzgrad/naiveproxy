// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BALSA_BALSA_VISITOR_INTERFACE_H_
#define QUICHE_BALSA_BALSA_VISITOR_INTERFACE_H_

#include <cstddef>
#include <memory>

#include "absl/strings/string_view.h"
#include "quiche/balsa/balsa_enums.h"
#include "quiche/balsa/balsa_headers.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

// By default the BalsaFrame instantiates a class derived from this interface
// that does absolutely nothing. If you'd prefer to have interesting
// functionality execute when any of the below functions are called by the
// BalsaFrame, then you should subclass it, and set an instantiation of your
// subclass as the current visitor for the BalsaFrame class using
// BalsaFrame::set_visitor().
class QUICHE_EXPORT BalsaVisitorInterface {
 public:
  virtual ~BalsaVisitorInterface() {}

  // Summary:
  //   This is how the BalsaFrame passes you the raw input that it knows to be a
  //   part of the body. To be clear, every byte of the Balsa that isn't part of
  //   the header (or its framing), or trailers will be passed through this
  //   function.  This includes data as well as chunking framing.
  // Arguments:
  //   input - the raw input that is part of the body.
  virtual void OnRawBodyInput(absl::string_view input) = 0;

  // Summary:
  //   This is like OnRawBodyInput, but it will only include those parts of the
  //   body that would be stored by a program such as wget, i.e. the bytes
  //   indicating chunking will have been removed. Trailers will not be passed
  //   in through this function-- they'll be passed in through OnTrailerInput.
  // Arguments:
  //   input - the part of the body.
  virtual void OnBodyChunkInput(absl::string_view input) = 0;

  // Summary:
  //   BalsaFrame passes the raw header data through this function. This is not
  //   cleaned up in any way.
  // Arguments:
  //   input - raw header data.
  virtual void OnHeaderInput(absl::string_view input) = 0;

  // Summary:
  //   BalsaFrame passes each header through this function as soon as it is
  //   parsed.
  // Argument:
  //   key - the header name.
  //   value - the associated header value.
  virtual void OnHeader(absl::string_view key, absl::string_view value) = 0;

  // Summary:
  //   BalsaFrame passes the raw trailer data through this function. This is not
  //   cleaned up in any way.  Note that trailers only occur in a message if
  //   there was a chunked encoding, and not always then.
  // Arguments:
  //   input - raw trailer data.
  virtual void OnTrailerInput(absl::string_view input) = 0;

  // Summary:
  //   Since the BalsaFrame already has to parse the headers in order to
  //   determine proper framing, it might as well pass the parsed and cleaned-up
  //   results to whatever might need it.  This function exists for that
  //   purpose-- parsed headers are passed into this function.
  // Arguments:
  //   headers - contains the parsed headers in the order in which
  //             they occurred in the header.
  virtual void ProcessHeaders(const BalsaHeaders& headers) = 0;

  // Summary:
  //   Since the BalsaFrame already has to parse the trailer, it might as well
  //   pass the parsed and cleaned-up results to whatever might need it.  This
  //   function exists for that purpose-- parsed trailer is passed into this
  //   function. This will not be called if the trailer_ object is not set in
  //   the framer, even if trailer exists in request/response.
  // Arguments:
  //   trailer - contains the parsed headers in the order in which
  //             they occurred in the trailer.
  // TODO(b/134507471): Remove this and update the OnTrailers() comment.
  virtual void ProcessTrailers(const BalsaHeaders& trailer) = 0;

  // Summary:
  //   Called when the trailers are framed and processed. This callback is only
  //   called when the trailers option is set in the framer, and it is mutually
  //   exclusive with ProcessTrailers().
  // Arguments:
  //   trailers - contains the parsed headers in the order in which they
  //              occurred in the trailers.
  virtual void OnTrailers(std::unique_ptr<BalsaHeaders> trailers) = 0;

  // Summary:
  //   Called when the first line of the message is parsed, in this case, for a
  //   request.
  // Arguments:
  //   line_input        - the first line string,
  //   method_input      - the method substring,
  //   request_uri_input - request uri substring,
  //   version_input     - the version substring.
  virtual void OnRequestFirstLineInput(absl::string_view line_input,
                                       absl::string_view method_input,
                                       absl::string_view request_uri,
                                       absl::string_view version_input) = 0;

  // Summary:
  //   Called when the first line of the message is parsed, in this case, for a
  //   response.
  // Arguments:
  //   line_input    - the first line string,
  //   version_input - the version substring,
  //   status_input  - the status substring,
  //   reason_input  - the reason substring.
  virtual void OnResponseFirstLineInput(absl::string_view line_input,
                                        absl::string_view version_input,
                                        absl::string_view status_input,
                                        absl::string_view reason_input) = 0;

  // Summary:
  //   Called when a chunk length is parsed.
  // Arguments:
  //   chunk length - the length of the next incoming chunk.
  virtual void OnChunkLength(size_t chunk_length) = 0;

  // Summary:
  //   BalsaFrame passes the raw chunk extension data through this function.
  //   The data is not cleaned up at all.
  // Arguments:
  //   input - contains the bytes available for read.
  virtual void OnChunkExtensionInput(absl::string_view input) = 0;

  // Summary:
  //   Called when an interim response (response code 1xx) is framed and
  //   processed. This callback is mutually exclusive with ContinueHeaderDone().
  // Arguments:
  //   headers - contains the parsed headers in the order in which they occurred
  //             in the interim response.
  virtual void OnInterimHeaders(std::unique_ptr<BalsaHeaders> headers) = 0;

  // Summary:
  //   Called when the 100 Continue headers are framed and processed. This
  //   callback is mutually exclusive with OnInterimHeaders().
  // TODO(b/68801833): Remove this and update the OnInterimHeaders() comment.
  virtual void ContinueHeaderDone() = 0;

  // Summary:
  //   Called when the header is framed and processed.
  virtual void HeaderDone() = 0;

  // Summary:
  //   Called when the message is framed and processed.
  virtual void MessageDone() = 0;

  // Summary:
  //   Called when an error is detected
  // Arguments:
  //   error_code - the error which is to be reported.
  virtual void HandleError(BalsaFrameEnums::ErrorCode error_code) = 0;

  // Summary:
  //   Called when something meriting a warning is detected
  // Arguments:
  //   error_code - the warning which is to be reported.
  virtual void HandleWarning(BalsaFrameEnums::ErrorCode error_code) = 0;
};

}  // namespace quiche

#endif  // QUICHE_BALSA_BALSA_VISITOR_INTERFACE_H_
