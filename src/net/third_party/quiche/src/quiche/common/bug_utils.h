#ifndef QUICHE_COMMON_BUG_UTILS_H_
#define QUICHE_COMMON_BUG_UTILS_H_

// This file contains macros for emitting bug log events when invariants are
// violated.
//
// Each instance of a QUICHE_BUG and friends has an associated id, which can be
// helpful for quickly finding the associated source code.
//
// The IDs are free form, but are expected to be unique. Best practice is to
// provide a *short* description of the condition being detected, without
// quotes, e.g.,
//
//    QUICHE_BUG(http2_decoder_invalid_parse_state) << "...";
//
// QUICHE_BUG is defined in platform/api/quiche_bug_tracker.h.
//

#include <atomic>
#include <cstdint>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>

#include "absl/base/log_severity.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {
namespace internal {

class QUICHE_EXPORT GenericBugListener {
 public:
  virtual ~GenericBugListener() = default;
  virtual void OnBug(const char* bug_id, const char* file, int line,
                     absl::string_view bug_message) = 0;
};

struct QUICHE_NO_EXPORT GenericBugOptions {
  explicit GenericBugOptions(absl::LogSeverity log_severity,
                             const char* file_name, int line)
      : severity(log_severity), file_name(file_name), line(line) {}

  GenericBugOptions& SetCondition(absl::string_view condition) {
    this->condition_str = condition;
    return *this;
  }

  GenericBugOptions& SetBugListener(GenericBugListener* listener) {
    this->bug_listener = listener;
    return *this;
  }

  absl::LogSeverity severity;
  const char* const file_name;
  const int line;
  // !empty() for conditional GENERIC_BUGs.
  absl::string_view condition_str;
  // If not nullptr, |bug_listener| will be notified of this GENERIC_BUG hit.
  // Since GenericBugListener may be a temporary object, this is only safe to
  // access from GenericBugStreamHandler, whose scope is strictly narrower.
  GenericBugListener* bug_listener = nullptr;
};

QUICHE_EXPORT inline GenericBugOptions DefaultBugOptions(const char* file_name,
                                                         int line) {
  return GenericBugOptions(absl::kLogDebugFatal, file_name, line);
}

// Called if a GENERIC_BUG is hit, but logging is omitted.
QUICHE_EXPORT void GenericBugWithoutLog(const char* bug_id,
                                        const GenericBugOptions& options);

// GenericBugStreamHandler exposes an interface similar to Abseil log objects,
// and is used by GENERIC_BUG to trigger a function which can be overridden in
// tests. By default, this class performs no action. SetOverrideFunction must be
// called to accomplish anything interesting.
class QUICHE_EXPORT GenericBugStreamHandler {
 public:
  // |prefix| and |bug_id| must be literal strings. They are used in the
  // destructor.
  explicit GenericBugStreamHandler(const char* prefix, const char* bug_id,
                                   const GenericBugOptions& options);

  ~GenericBugStreamHandler();

  template <typename T,
            std::enable_if_t<absl::HasAbslStringify<T>::value, bool> = true>
  GenericBugStreamHandler& operator<<(const T& v) {
    absl::StrAppend(&str_, v);
    return *this;
  }

  // For types that support only operator<<. There's a better solution in
  // Abseil, but unfortunately OStringStream is in a namespace marked internal.
  template <typename T,
            std::enable_if_t<!absl::HasAbslStringify<T>::value, bool> = true>
  GenericBugStreamHandler& operator<<(const T& v) {
    absl::StrAppend(&str_, (std::ostringstream() << v).str());
    return *this;
  }

  // Interface similar to Abseil log objects.
  GenericBugStreamHandler& stream() { return *this; }

  using OverrideFunction = void (*)(absl::LogSeverity severity,
                                    const char* file, int line,
                                    absl::string_view log_message);

  // Allows overriding the internal implementation. Call with nullptr to make
  // this class a no-op. This getter and setter are thread-safe.
  static void SetOverrideFunction(OverrideFunction override_function);
  static OverrideFunction GetOverrideFunction();

 private:
  static std::atomic<OverrideFunction> atomic_override_function_;

  const char* bug_id_;
  std::string str_;
  const GenericBugOptions& options_;
};

}  // namespace internal
}  // namespace quiche

// The GNU compiler emits a warning for code like:
//
//   if (foo)
//     if (bar) { } else baz;
//
// because it thinks you might want the else to bind to the first if.  This
// leads to problems with code like:
//
//   if (do_expr) GENERIC_BUG(bug_id) << "Some message";
//
// The "switch (0) case 0:" idiom is used to suppress this.
#define GENERIC_BUG_UNBRACED_ELSE_BLOCKER \
  switch (0)                              \
  case 0:                                 \
  default:

#define GENERIC_BUG_IMPL(prefix, bug_id, skip_log_condition, options)       \
  if (skip_log_condition) {                                                 \
    ::quiche::internal::GenericBugWithoutLog(#bug_id, (options));           \
  } else /* NOLINT */                                                       \
    ::quiche::internal::GenericBugStreamHandler(prefix, #bug_id, (options)) \
        .stream()

#endif  // QUICHE_COMMON_BUG_UTILS_H_
