// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_BALSA_HEADER_API_H_
#define QUICHE_BALSA_HEADER_API_H_

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_lower_case_string.h"
#include "quiche/common/quiche_callbacks.h"

namespace quiche {

// An API so we can reuse functions for BalsaHeaders and Envoy's HeaderMap.
// Contains only const member functions, so it can wrap const HeaderMaps;
// non-const functions are in HeaderApi.
//
// Depending on the implementation, the headers may act like HTTP/1 headers
// (BalsaHeaders) or HTTP/2 headers (HeaderMap). For HTTP-version-specific
// headers or pseudoheaders like "host" or ":authority", use this API's
// implementation-independent member functions, like Authority(). Looking those
// headers up by name is deprecated and may QUICHE_DCHECK-fail.
// For the differences between HTTP/1 and HTTP/2 headers, see RFC 7540:
// https://tools.ietf.org/html/rfc7540#section-8.1.2
//
// Operations on header keys are case-insensitive while operations on header
// values are case-sensitive.
//
// Some methods have overloads which accept Envoy-style LowerCaseStrings. Often
// these keys are accessible from Envoy::Http::Headers::get().SomeHeader,
// already lowercaseified. It's faster to avoid converting them to and from
// lowercase. Additionally, some implementations of ConstHeaderApi might take
// advantage of a constant-time lookup for inlined headers.
class QUICHE_EXPORT ConstHeaderApi {
 public:
  virtual ~ConstHeaderApi() {}

  // Determine whether the headers are empty.
  virtual bool IsEmpty() const = 0;

  // Returns the header entry for the first instance with key |key|
  // If header isn't present, returns absl::string_view().
  virtual absl::string_view GetHeader(absl::string_view key) const = 0;

  virtual absl::string_view GetHeader(const QuicheLowerCaseString& key) const {
    // Default impl for BalsaHeaders, etc.
    return GetHeader(key.get());
  }

  // Collects all of the header entries with key |key| and returns them in |out|
  // Headers are returned in the order they are inserted.
  virtual void GetAllOfHeader(absl::string_view key,
                              std::vector<absl::string_view>* out) const = 0;
  virtual std::vector<absl::string_view> GetAllOfHeader(
      absl::string_view key) const {
    std::vector<absl::string_view> out;
    GetAllOfHeader(key, &out);
    return out;
  }
  virtual void GetAllOfHeader(const QuicheLowerCaseString& key,
                              std::vector<absl::string_view>* out) const {
    return GetAllOfHeader(key.get(), out);
  }

  // Determine if a given header is present.
  virtual bool HasHeader(absl::string_view key) const = 0;

  // Determines if a given header is present with non-empty value.
  virtual bool HasNonEmptyHeader(absl::string_view key) const = 0;

  // Goes through all headers with key |key| and checks to see if one of the
  // values is |value|.  Returns true if there are headers with the desired key
  // and value, false otherwise.
  virtual bool HeaderHasValue(absl::string_view key,
                              absl::string_view value) const = 0;

  // Same as above, but value is treated as case insensitive.
  virtual bool HeaderHasValueIgnoreCase(absl::string_view key,
                                        absl::string_view value) const = 0;

  // Joins all values for header entries with `key` into a comma-separated
  // string.  Headers are returned in the order they are inserted.
  virtual std::string GetAllOfHeaderAsString(absl::string_view key) const = 0;
  virtual std::string GetAllOfHeaderAsString(
      const QuicheLowerCaseString& key) const {
    return GetAllOfHeaderAsString(key.get());
  }

  // Returns true if we have at least one header with given prefix
  // [case insensitive]. Currently for test use only.
  virtual bool HasHeadersWithPrefix(absl::string_view key) const = 0;

  // Returns the key value pairs for all headers where the header key begins
  // with the specified prefix.
  // Headers are returned in the order they are inserted.
  virtual void GetAllOfHeaderWithPrefix(
      absl::string_view prefix,
      std::vector<std::pair<absl::string_view, absl::string_view>>* out)
      const = 0;

  // Returns the key value pairs for all headers in this object. If 'limit' is
  // >= 0, return at most 'limit' headers.
  virtual void GetAllHeadersWithLimit(
      std::vector<std::pair<absl::string_view, absl::string_view>>* out,
      int limit) const = 0;

  // Returns a textual representation of the header object. The format of the
  // string may depend on the underlying implementation.
  virtual std::string DebugString() const = 0;

  // Applies the argument function to each header line.  If the argument
  // function returns false, iteration stops and ForEachHeader returns false;
  // otherwise, ForEachHeader returns true.
  virtual bool ForEachHeader(
      quiche::UnretainedCallback<bool(const absl::string_view key,
                                      const absl::string_view value)>
          fn) const = 0;

  // Returns the upper bound byte size of the headers. This can be used to size
  // a Buffer when serializing headers.
  virtual size_t GetSizeForWriteBuffer() const = 0;

  // Returns the response code for response headers. If no status code exists,
  // the return value is implementation-specific.
  virtual absl::string_view response_code() const = 0;

  // Returns the response code for response headers or 0 if no status code
  // exists.
  virtual size_t parsed_response_code() const = 0;

  // Returns the response reason phrase; the stored one for HTTP/1 headers, or a
  // phrase determined from the response code for HTTP/2 headers..
  virtual absl::string_view response_reason_phrase() const = 0;

  // Return the HTTP first line of this request, generally of the format:
  // GET /path/ HTTP/1.1
  // TODO(b/110421449): deprecate this method.
  virtual std::string first_line_of_request() const = 0;

  // Return the method for this request, such as GET or POST.
  virtual absl::string_view request_method() const = 0;

  // Return the request URI from the first line of this request, such as
  // "/path/".
  virtual absl::string_view request_uri() const = 0;

  // Return the version portion of the first line of this request, such as
  // "HTTP/1.1".
  // TODO(b/110421449): deprecate this method.
  virtual absl::string_view request_version() const = 0;

  virtual absl::string_view response_version() const = 0;

  // Returns the authority portion of a request, or an empty string if missing.
  // This is the value of the host header for HTTP/1 headers and the value of
  // the :authority pseudo-header for HTTP/2 headers.
  virtual absl::string_view Authority() const = 0;

  // Call the provided function on the cookie, avoiding
  // copies if possible. The cookie is the value of the Cookie header; for
  // HTTP/2 headers, if there are multiple Cookie headers, they will be joined
  // by "; ", per go/rfc/7540#section-8.1.2.5. If there is no Cookie header,
  // cookie.data() will be nullptr. The lifetime of the cookie isn't guaranteed
  // to extend beyond this call.
  virtual void ApplyToCookie(
      quiche::UnretainedCallback<void(absl::string_view cookie)> f) const = 0;

  virtual size_t content_length() const = 0;
  virtual bool content_length_valid() const = 0;

  // TODO(b/118501626): Add functions for working with other headers and
  // pseudo-headers whose presence or value depends on HTTP version, including:
  // :method, :scheme, :path, connection, and cookie.
};

// An API so we can reuse functions for BalsaHeaders and Envoy's HeaderMap.
// Inherits const functions from ConstHeaderApi and adds non-const functions,
// for use with non-const HeaderMaps.
//
// For HTTP-version-specific headers and pseudo-headers, the same caveats apply
// as with ConstHeaderApi.
//
// Operations on header keys are case-insensitive while operations on header
// values are case-sensitive.
class QUICHE_EXPORT HeaderApi : public virtual ConstHeaderApi {
 public:
  // Replaces header entries with key |key| if they exist, or appends
  // a new header if none exist.
  virtual void ReplaceOrAppendHeader(absl::string_view key,
                                     absl::string_view value) = 0;

  // Removes all headers in given set of |keys| at once
  virtual void RemoveAllOfHeaderInList(
      const std::vector<absl::string_view>& keys) = 0;

  // Removes all headers with key |key|.
  virtual void RemoveAllOfHeader(absl::string_view key) = 0;

  // Append a new header entry to the header object with key |key| and value
  // |value|.
  virtual void AppendHeader(absl::string_view key, absl::string_view value) = 0;

  // Removes all headers starting with 'key' [case insensitive]
  virtual void RemoveAllHeadersWithPrefix(absl::string_view key) = 0;

  // Appends ',value' to an existing header named 'key'.  If no header with the
  // correct key exists, it will call AppendHeader(key, value).  Calling this
  // function on a key which exists several times in the headers will produce
  // unpredictable results.
  virtual void AppendToHeader(absl::string_view key,
                              absl::string_view value) = 0;

  // Appends ', value' to an existing header named 'key'.  If no header with the
  // correct key exists, it will call AppendHeader(key, value).  Calling this
  // function on a key which exists several times in the headers will produce
  // unpredictable results.
  virtual void AppendToHeaderWithCommaAndSpace(absl::string_view key,
                                               absl::string_view value) = 0;

  // Set the header or pseudo-header corresponding to the authority portion of a
  // request: host for HTTP/1 headers, or :authority for HTTP/2 headers.
  virtual void ReplaceOrAppendAuthority(absl::string_view value) = 0;
  virtual void RemoveAuthority() = 0;

  // These set portions of the first line for HTTP/1 headers, or the
  // corresponding pseudo-headers for HTTP/2 headers.
  virtual void SetRequestMethod(absl::string_view method) = 0;
  virtual void SetResponseCode(absl::string_view code) = 0;
  // As SetResponseCode, but slightly faster for BalsaHeaders if the caller
  // represents the response code as an integer and not a string.
  virtual void SetParsedResponseCodeAndUpdateFirstline(
      size_t parsed_response_code) = 0;

  // Sets the request URI.
  //
  // For HTTP/1 headers, sets the request URI portion of the first line (the
  // second token). Doesn't parse the URI; leaves the Host header unchanged.
  //
  // For HTTP/2 headers, sets the :path pseudo-header, and also :scheme and
  // :authority if they're present in the URI; otherwise, leaves :scheme and
  // :authority unchanged.
  //
  // The caller is responsible for verifying that the URI is in a valid format.
  virtual void SetRequestUri(absl::string_view uri) = 0;

  // These are only meaningful for HTTP/1 headers; for HTTP/2 headers, they do
  // nothing.
  virtual void SetRequestVersion(absl::string_view version) = 0;
  virtual void SetResponseVersion(absl::string_view version) = 0;
  virtual void SetResponseReasonPhrase(absl::string_view reason_phrase) = 0;

  // SetContentLength, SetTransferEncodingToChunkedAndClearContentLength, and
  // SetNoTransferEncoding modifies the header object to use
  // content-length and transfer-encoding headers in a consistent
  // manner. They set all internal flags and status, if applicable, so client
  // can get a consistent view from various accessors.
  virtual void SetContentLength(size_t length) = 0;
  // Sets transfer-encoding to chunked and updates internal state.
  virtual void SetTransferEncodingToChunkedAndClearContentLength() = 0;
  // Removes transfer-encoding headers and updates internal state.
  virtual void SetNoTransferEncoding() = 0;

  // If true, QUICHE_BUG if a header that starts with an invalid prefix is
  // explicitly set. Not implemented for Envoy headers; can only be set false.
  virtual void set_enforce_header_policy(bool enforce) = 0;
};

}  // namespace quiche

#endif  // QUICHE_BALSA_HEADER_API_H_
