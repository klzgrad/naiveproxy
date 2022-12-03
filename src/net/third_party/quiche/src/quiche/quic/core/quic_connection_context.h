// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_CONNECTION_CONTEXT_H_
#define QUICHE_QUIC_CORE_QUIC_CONNECTION_CONTEXT_H_

#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {

// QuicConnectionTracer is responsible for emit trace messages for a single
// QuicConnection.
// QuicConnectionTracer is part of the QuicConnectionContext.
class QUIC_EXPORT_PRIVATE QuicConnectionTracer {
 public:
  virtual ~QuicConnectionTracer() = default;

  // Emit a trace message from a string literal. The trace may simply remember
  // the address of the literal in this function and read it at a later time.
  virtual void PrintLiteral(const char* literal) = 0;

  // Emit a trace message from a string_view. Unlike PrintLiteral, this function
  // will not read |s| after it returns.
  virtual void PrintString(absl::string_view s) = 0;

  // Emit a trace message from printf-style arguments.
  template <typename... Args>
  void Printf(const absl::FormatSpec<Args...>& format, const Args&... args) {
    std::string s = absl::StrFormat(format, args...);
    PrintString(s);
  }

 private:
  friend class QuicConnectionContextSwitcher;

  // Called by QuicConnectionContextSwitcher, when |this| becomes the current
  // thread's QUIC connection tracer.
  //
  // Activate/Deactivate are only called by QuicConnectionContextSwitcher's
  // constructor/destructor, they always come in pairs.
  virtual void Activate() {}

  // Called by QuicConnectionContextSwitcher, when |this| stops from being the
  // current thread's QUIC connection tracer.
  //
  // Activate/Deactivate are only called by QuicConnectionContextSwitcher's
  // constructor/destructor, they always come in pairs.
  virtual void Deactivate() {}
};

// QuicBugListener is a helper class for implementing QUIC_BUG. The QUIC_BUG
// implementation can send the bug information into quic::CurrentBugListener().
class QUIC_EXPORT_PRIVATE QuicBugListener {
 public:
  virtual ~QuicBugListener() = default;
  virtual void OnQuicBug(const char* bug_id, const char* file, int line,
                         absl::string_view bug_message) = 0;
};

// QuicConnectionProcessPacketContext is a member of QuicConnectionContext that
// contains information of the packet currently being processed by the owning
// QuicConnection.
struct QUIC_EXPORT_PRIVATE QuicConnectionProcessPacketContext final {
  // If !empty(), the decrypted payload of the packet currently being processed.
  absl::string_view decrypted_payload;

  // The offset within |decrypted_payload|, if it's non-empty, that marks the
  // start of the frame currently being processed.
  // Should not be used when |decrypted_payload| is empty.
  size_t current_frame_offset = 0;

  // NOTE: This can be very expansive. If used in logs, make sure it is rate
  // limited via QUIC_BUG etc.
  std::string DebugString() const;
};

// QuicConnectionContext is a per-QuicConnection context that includes
// facilities useable by any part of a QuicConnection. A QuicConnectionContext
// is owned by a QuicConnection.
//
// The 'top-level' QuicConnection functions are responsible for maintaining the
// thread-local QuicConnectionContext pointer, such that any function called by
// them(directly or indirectly) can access the context.
//
// Like QuicConnection, all facilities in QuicConnectionContext are assumed to
// be called from a single thread at a time, they are NOT thread-safe.
struct QUIC_EXPORT_PRIVATE QuicConnectionContext final {
  // Get the context on the current executing thread. nullptr if the current
  // function is not called from a 'top-level' QuicConnection function.
  static QuicConnectionContext* Current();

  std::unique_ptr<QuicConnectionTracer> tracer;
  std::unique_ptr<QuicBugListener> bug_listener;

  // Information about the packet currently being processed.
  QuicConnectionProcessPacketContext process_packet_context;
};

// QuicConnectionContextSwitcher is a RAII object used for maintaining the
// thread-local QuicConnectionContext pointer.
class QUIC_EXPORT_PRIVATE QuicConnectionContextSwitcher final {
 public:
  // The constructor switches from QuicConnectionContext::Current() to
  // |new_context|.
  explicit QuicConnectionContextSwitcher(QuicConnectionContext* new_context);

  // The destructor switches from QuicConnectionContext::Current() back to the
  // old context.
  ~QuicConnectionContextSwitcher();

 private:
  QuicConnectionContext* old_context_;
};

// Emit a trace message from a string literal to the current tracer(if any).
inline void QUIC_TRACELITERAL(const char* literal) {
  QuicConnectionContext* current = QuicConnectionContext::Current();
  if (current && current->tracer) {
    current->tracer->PrintLiteral(literal);
  }
}

// Emit a trace message from a string_view to the current tracer(if any).
inline void QUIC_TRACESTRING(absl::string_view s) {
  QuicConnectionContext* current = QuicConnectionContext::Current();
  if (current && current->tracer) {
    current->tracer->PrintString(s);
  }
}

// Emit a trace message from printf-style arguments to the current tracer(if
// any).
template <typename... Args>
void QUIC_TRACEPRINTF(const absl::FormatSpec<Args...>& format,
                      const Args&... args) {
  QuicConnectionContext* current = QuicConnectionContext::Current();
  if (current && current->tracer) {
    current->tracer->Printf(format, args...);
  }
}

inline QuicBugListener* CurrentBugListener() {
  QuicConnectionContext* current = QuicConnectionContext::Current();
  return (current != nullptr) ? current->bug_listener.get() : nullptr;
}

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_CONNECTION_CONTEXT_H_
