#include "quiche/common/bug_utils.h"

#include <atomic>
#include <cstdint>
#include <limits>
#include <string>

#include "absl/base/log_severity.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace quiche {
namespace internal {

GenericBugStreamHandler::GenericBugStreamHandler(
    const char* prefix, const char* bug_id, const GenericBugOptions& options)
    : bug_id_(bug_id), options_(options) {
  if (options_.condition_str.empty()) {
    absl::StrAppend(&str_, prefix, "(", bug_id, "): ");
  } else {
    absl::StrAppend(&str_, prefix, "_IF(", bug_id, ", ", options_.condition_str,
                    "): ");
  }
}

GenericBugStreamHandler::~GenericBugStreamHandler() {
  GenericBugStreamHandler::OverrideFunction override_function =
      GetOverrideFunction();
  if (options_.bug_listener != nullptr) {
    options_.bug_listener->OnBug(bug_id_, options_.file_name, options_.line,
                                 str_);
  }
  if (override_function != nullptr) {
    override_function(options_.severity, options_.file_name, options_.line,
                      str_);
  }
}

// static
void GenericBugStreamHandler::SetOverrideFunction(
    GenericBugStreamHandler::OverrideFunction override_function) {
  atomic_override_function_.store(override_function);
}

// static
GenericBugStreamHandler::OverrideFunction
GenericBugStreamHandler::GetOverrideFunction() {
  return atomic_override_function_.load(std::memory_order_relaxed);
}

// static
std::atomic<GenericBugStreamHandler::OverrideFunction>
    GenericBugStreamHandler::atomic_override_function_ = nullptr;

void GenericBugWithoutLog(const char* bug_id,
                          const GenericBugOptions& options) {
  if (options.bug_listener != nullptr) {
    options.bug_listener->OnBug(bug_id, options.file_name, options.line,
                                /*No bug message*/ "");
  }
}

}  // namespace internal
}  // namespace quiche
