// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// General-purpose abstractions for a write stream.

#ifndef QUICHE_COMMON_QUICHE_STREAM_H_
#define QUICHE_COMMON_QUICHE_STREAM_H_

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

// A general-purpose visitor API that gets notifications for WriteStream-related
// events.
class QUICHE_EXPORT WriteStreamVisitor {
 public:
  virtual ~WriteStreamVisitor() {}

  // Called whenever the stream is not write-blocked and can accept new data.
  virtual void OnCanWrite() = 0;
};

// Options for writing data into a WriteStream.
class QUICHE_EXPORT StreamWriteOptions {
 public:
  StreamWriteOptions() = default;

  // If send_fin() is sent to true, the write operation also sends a FIN on the
  // stream.
  bool send_fin() const { return send_fin_; }
  void set_send_fin(bool send_fin) { send_fin_ = send_fin; }

 private:
  bool send_fin_ = false;
};

inline constexpr StreamWriteOptions kDefaultStreamWriteOptions =
    StreamWriteOptions();

// WriteStream is an object that can accept a stream of bytes.
//
// The writes into a WriteStream are all-or-nothing.  A WriteStream object has
// to either accept all data written into it by returning absl::OkStatus, or ask
// the caller to try again once via OnCanWrite() by returning
// absl::UnavailableError.
class QUICHE_EXPORT WriteStream {
 public:
  virtual ~WriteStream() {}

  // Writes |data| into the stream.
  virtual absl::Status Writev(absl::Span<const absl::string_view> data,
                              const StreamWriteOptions& options) = 0;

  // Indicates whether it is possible to write into stream right now.
  virtual bool CanWrite() const = 0;

  // Legacy convenience method for writing a single string_view.  New users
  // should use quiche::WriteIntoStream instead, since this method does not
  // return useful failure information.
  [[nodiscard]] bool SendFin() {
    StreamWriteOptions options;
    options.set_send_fin(true);
    return Writev(absl::Span<const absl::string_view>(), options).ok();
  }

  // Legacy convenience method for writing a single string_view.  New users
  // should use quiche::WriteIntoStream instead, since this method does not
  // return useful failure information.
  [[nodiscard]] bool Write(absl::string_view data) {
    return Writev(absl::MakeSpan(&data, 1), kDefaultStreamWriteOptions).ok();
  }
};

// Convenience methods to write a single chunk of data into the stream.
inline absl::Status WriteIntoStream(
    WriteStream& stream, absl::string_view data,
    const StreamWriteOptions& options = kDefaultStreamWriteOptions) {
  return stream.Writev(absl::MakeSpan(&data, 1), options);
}

// Convenience methods to send a FIN on the stream.
inline absl::Status SendFinOnStream(WriteStream& stream) {
  StreamWriteOptions options;
  options.set_send_fin(true);
  return stream.Writev(absl::Span<const absl::string_view>(), options);
}

}  // namespace quiche

#endif  // QUICHE_COMMON_QUICHE_STREAM_H_
